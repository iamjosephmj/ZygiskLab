---
title: "Hello, Zygisk"
description: "Lab 1: a one-file module that logs its pid and uid from inside a named app, packaged, installed, and rebooted into place."
sidebar:
  order: 4
status: unverified
---

A hello-world module has one job, and it is not to say hello. It is to answer a
question you will be asking for the rest of this book: *am I actually running
where I think I am running?* Almost every hard bug in Zygisk work is a variant
of that question — code that fires in the wrong process, at the wrong moment,
or does not fire at all and leaves you debugging a hook that was never
installed. So the first module you build is a witness. It prints, from each of
the three callbacks Zygisk gives you, enough state to prove which process it is
in and which side of the specialization boundary it is standing on.

The module is in the repo at `modules/01-hello-zygisk/`. Sixteen lines of
substance in one `main.cpp`, two make fragments, a `module.prop`, and a build
script that zips it. Read it with this chapter open beside it.

## The module, line by line

```cpp
#include <android/log.h>
#include <unistd.h>
#include <sys/types.h>

#include "zygisk.hpp"

#define LOG_TAG "ZygiskLab"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
```

Three system headers and one API header. `<android/log.h>` gives you
`__android_log_print`, which is the only output channel you can rely on this
early — you have no stdout worth the name, no file you are allowed to create
yet (Chapter 7 covers where an injected process may and may not write), and no
debugger attached. `<unistd.h>` and `<sys/types.h>` are for `getpid()` and
`getuid()`. The fixed `LOG_TAG` matters more than it looks: it is what makes
`adb logcat -s ZygiskLab` a usable filter instead of a firehose, and every
module in this book uses the same tag so the labs compose.

`zygisk.hpp` is vendored into `jni/` verbatim from topjohnwu's official
`zygisk-module-sample`, targeting `ZYGISK_API_VERSION 5`. It is a
header-only shim — it declares no implementation, only an ABI. Do not modify
it; the header says so and it means it, because the loader on the other side
of that ABI was compiled against the same declarations.

```cpp
using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

class HelloZygisk : public zygisk::ModuleBase {
```

`zygisk::ModuleBase` is a pure-virtual-in-spirit base class: five `virtual`
methods, all with empty default bodies. You override the ones you care about
and inherit no-ops for the rest. This module overrides three and leaves
`preServerSpecialize` and `postServerSpecialize` alone, so it is silent in
`system_server`.

### `onLoad`

```cpp
void onLoad(Api *api, JNIEnv *env) override {
    this->api = api;
    this->env = env;
    LOGI("onLoad: module loaded into pid=%d", getpid());
}
```

`onLoad` is the first thing that runs, and it hands you the two handles you
will need everywhere else: an `Api *` and a `JNIEnv *`. Stash both. There is no
getter to recover them later — if you do not keep them here, the rest of your
module has nothing to call.

Two things about `Api` are worth internalising now. First, from the header:
*all API methods stop working after `post[XXX]Specialize`*, because Zygisk
unloads itself from the specialized process afterwards. The handle stays
non-null and stops being useful, which is a nastier failure mode than a crash.
Second, several `Api` methods narrow further than that —
`connectCompanion()`, `getModuleDir()` and `exemptFd()` are documented as
working only in the pre-specialize methods, for SELinux and uid reasons. That
is the shape of the whole framework: your privilege and your capabilities both
shrink as the process specializes, and the interesting work is choosing the
right side of the line. Chapters 8 through 11 live on the early side; 12
onward on the late side.

The `JNIEnv *` is the environment of the thread Zygisk called you on. It is
valid in the callbacks, on that thread. Chapter 13 deals with what happens when
you want JNI from a thread you created yourself.

### `preAppSpecialize` — the point of the whole module

```cpp
void preAppSpecialize(AppSpecializeArgs *args) override {
    const char *name = env->GetStringUTFChars(args->nice_name, nullptr);
    LOGI("preAppSpecialize: pid=%d getuid=%d (current identity) args->uid=%d (destination) nice_name=%s",
         getpid(), getuid(), args->uid, name);
    env->ReleaseStringUTFChars(args->nice_name, name);
}
```

You are past the fork and before the specialization. The header is blunt about
what that means: the process "does not have any sandbox restrictions and still
runs with the same privilege of zygote". You are root, in a process that is
going to become an app and has not become one yet.

This line logs `getuid()` and `args->uid` in the same breath, and the entire
value of the module is in the fact that they are **different things**.

`getuid()` is a syscall. It asks the kernel what this process's real uid *is*,
right now. In `preAppSpecialize` the answer is zygote's — 0 — because nothing
has changed it yet. `args->uid` is not a syscall and not a property of the
process. It is a `jint &` reference into the specialization request: an
argument that zygote is *about to* apply. It is a destination label, printed in
advance.

Get this wrong and you will write a module that proves nothing while looking
like it proves everything. The naive version logs `args->uid` in
`preAppSpecialize` and `getuid()` in `postAppSpecialize`, sees the same number
twice, and concludes it crossed the boundary. It did not demonstrate that at
all. It read a plan, then read a fact, and the fact happened to match the plan
— which it would also do if the module's `post` callback had never fired and
you were looking at a stale line. The proof is the *change*: `getuid()`
returning 0 in `pre` and the app's uid in `post`, with `args->uid` in `pre`
having told you in advance which number to expect. One value moves; the other
was a forecast.

That distinction is the axis the rest of this book is organised on. "Before" is
not a stylistic preference about where to put your code — it is a different
process identity, a different mount namespace, a different set of things the
kernel will let you do. Chapter 12 catalogues exactly what changed at the
boundary.

:::note
`args->uid` is a non-const reference, and Zygisk hands you `AppSpecializeArgs *`
(not `const *`) in `preAppSpecialize` precisely so you *can* write to it. This
module does not. Reading the specialization arguments is Chapter 9; the
consequences of rewriting them are not something to discover by accident.
:::

### The JNI string, and why it needs care

`args->nice_name` is a `jstring` — a Java object reference, not a C string.
`GetStringUTFChars` asks the runtime for a modified-UTF-8 char buffer for it.
The runtime may hand back a pointer into its own memory or it may allocate and
copy; the second argument (`isCopy`, passed `nullptr` here because the module
does not care which happened) is how you would find out. Either way the buffer
is *owned by the runtime*, and `ReleaseStringUTFChars` is how you tell the
runtime you are done with it. Skip the release and, in the copy case, you leak;
in the pin case, you can leave the string pinned against the collector.

In ordinary JNI code a leaked string is a slow bleed you might not notice. Here
it is worse in two specific ways. First, this callback runs in *every* app
process the module is enabled for, at launch, forever — a leak per launch is a
leak that scales with how much the user uses their phone. Second, you are
leaking inside a process that has not yet finished becoming an app, in a
runtime that is about to be handed to third-party code that will be blamed for
whatever memory weirdness follows. Native mistakes made before specialization
have a way of surfacing later as symptoms with your name nowhere near them. Do
the release, on every path, including error paths. If you add an early `return`
to this function later, check it does not step over line 34.

### `postAppSpecialize`

```cpp
void postAppSpecialize(const AppSpecializeArgs *) override {
    LOGI("postAppSpecialize: pid=%d getuid=%d", getpid(), getuid());
}
```

Now you are the app: sandbox on, uid dropped, running with exactly the
privilege the app's own code has. `getpid()` prints the same number as in
`preAppSpecialize` — specialization is not a fork, it is the same process being
constrained — and `getuid()` prints something new. That pid staying constant
and the uid changing is the whole result.

Note the parameter is unnamed. The `const AppSpecializeArgs *` is still
delivered, but the module does not read it, and an unnamed parameter is how you
say that without a compiler warning.

### `REGISTER_ZYGISK_MODULE`, unpacked

```cpp
REGISTER_ZYGISK_MODULE(HelloZygisk)
```

Not magic. From `zygisk.hpp`:

```cpp
#define REGISTER_ZYGISK_MODULE(clazz) \
void zygisk_module_entry(zygisk::internal::api_table *table, JNIEnv *env) { \
    zygisk::internal::entry_impl<clazz>(table, env);                        \
}
```

It defines one function with a fixed name, `zygisk_module_entry`, declared
`extern "C"` and `[[gnu::visibility("default")]]` at the bottom of the header.
That fixed, exported, unmangled symbol is the entire contract between your
`.so` and the loader: the loader `dlopen`s your library, looks up
`zygisk_module_entry`, and calls it. Chapter 6 follows that lookup in detail.
This is also why `-fvisibility=hidden` in `Android.mk` is safe — the one symbol
that must stay exported is explicitly marked visible in the header.

The body instantiates a template:

```cpp
template <class T>
void entry_impl(api_table *table, JNIEnv *env) {
    static Api api;
    api.tbl = table;
    static T module;
    ModuleBase *m = &module;
    static module_abi abi(m);
    if (!table->registerModule(table, &abi)) return;
    m->onLoad(&api, env);
}
```

Three function-local statics: the `Api` wrapper, your module instance, and a
`module_abi` struct. That struct is the version handshake — it carries
`api_version = ZYGISK_API_VERSION` and a table of plain function pointers
(lambdas that forward to your virtuals), so the loader never needs to know your
C++ vtable layout. `registerModule` is where a version mismatch is refused, and
the early `return` on failure is why an incompatible module is silent rather
than crashing: `onLoad` is simply never called.

Remember those `static`s. They are the reason this module cannot be built
without a C++ runtime, which is the next section, and you will meet
`entry_impl` again in Chapter 6 when the loader's side of the call is on the
table.

## The build files

```make
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE    := zygisklab
LOCAL_SRC_FILES := main.cpp
LOCAL_LDLIBS    := -llog
LOCAL_CPPFLAGS  := -std=c++20 -fvisibility=hidden -fvisibility-inlines-hidden
include $(BUILD_SHARED_LIBRARY)
```

`LOCAL_MODULE := zygisklab` names the output `libzygisklab.so` — ndk-build adds
the `lib` prefix and `.so` suffix. The name is arbitrary; the packaging step
renames the file by ABI anyway. `-llog` links `liblog`, without which
`__android_log_print` will not resolve. The two `-fvisibility` flags hide
everything not explicitly exported, which keeps the dynamic symbol table small
and makes your module a smaller target for anything enumerating symbols in a
process — a theme Chapter 21 returns to. `BUILD_SHARED_LIBRARY` is mandatory:
the loader `dlopen`s you.

```make
APP_ABI      := arm64-v8a
APP_PLATFORM := android-29
APP_STL      := c++_static
APP_CPPFLAGS := -fno-exceptions -fno-rtti
APP_CFLAGS   := -Oz -flto
APP_LDFLAGS  := -flto -Wl,--gc-sections
```

`APP_ABI := arm64-v8a` builds one ABI, matching the reference rig. A device
that still runs 32-bit apps forks them from a 32-bit zygote, and a module with
no `armeabi-v7a` binary simply will not load into those processes. Add the ABI
if you need it; the packaging layout has a slot for it.

`APP_PLATFORM := android-29` sets the minimum API level the NDK compiles
against. It is a floor for the symbols you may reference, not a target.

`APP_STL := c++_static` is the interesting one, and the two obvious
alternatives are both wrong here.

`APP_STL := none` cannot link this module at all — and not because of anything
in `main.cpp`. It is `REGISTER_ZYGISK_MODULE` that does it. The macro expands
to `entry_impl<T>()` with its function-local `static Api api`, `static T
module`, `static module_abi abi(m)`. Function-local statics with non-trivial
construction get thread-safe one-time initialisation, and the compiler
implements that by emitting calls to `__cxa_guard_acquire` and
`__cxa_guard_release`. Those live in the C++ runtime. Strip the runtime and
those symbols are undefined at link time. You cannot use this API header
without a C++ runtime, however C-like your own code is.

`c++_shared` links against `libc++_shared.so` and expects to find it at
runtime. Your module package would have to ship that library, and `build.sh`
does not package it — nor is a Zygisk module's `.so` loaded from a directory
with a helpful search path. Static is the correct answer for a self-contained
module.

The cost is size. The built `libzygisklab.so` is around 232KB for a module that
logs three lines. That is almost entirely the statically linked runtime:
static-archive linking pulls libc++abi in at object-file granularity, leaving
`.eh_frame`, `.gcc_except_table` and `.text` that `--gc-sections` cannot prove
dead even with `-fno-exceptions -fno-rtti`. `-Oz` and LTO are already applied
and stripping barely moves the number. Chapter 6 revisits module size and what
actually shrinks it; for now, note that a quarter-megabyte mapped into every
app process is a footprint, and footprints are Part VI's subject.

## Packaging

`build.sh` runs `ndk-build` and then assembles a zip. Reading what it actually
does gives you the layout a root manager expects:

```text
01-hello-zygisk.zip
├── module.prop
└── zygisk/
    └── arm64-v8a.so
```

`module.prop` at the root is how the manager identifies the module — `id`,
`name`, `version`, `versionCode`, `author`, `description`. The `id`
(`zygisklab_hello`) is the important field: it becomes the directory name under
`/data/adb/modules/`, and `deploy.sh` reads it straight out of the file to
build that path. Chapter 3 covers the fields in full.

The `zygisk/` directory is the part Zygisk-specific. Each file is named for the
ABI it serves — `arm64-v8a.so`, and `armeabi-v7a.so` if you built one. That is
the rename: `libs/arm64-v8a/libzygisklab.so` from ndk-build becomes
`zygisk/arm64-v8a.so` in the package. The loader picks the file matching the
zygote it is loading into.

## Installing, and why you must reboot

```bash
cd modules/01-hello-zygisk
./build.sh                     # -> out/01-hello-zygisk.zip
```

Flash `out/01-hello-zygisk.zip` in your root manager, then **reboot**.

The reboot is not ceremony. Zygisk modules are loaded by zygote, and zygote is
started once at boot. A module that was not present when zygote started has no
route into the running one. Nothing about installing a zip causes zygote to
rescan. Until you reboot, your module exists on disk and in the manager's list
and is running in exactly zero processes.

The same reasoning explains why iterating on a module is not a matter of
copying a new `.so` over the old one. Zygote holds the file mapped; rewriting
that inode changes pages under executing code. `deploy.sh` in the module
directory pushes to a staging path and `mv`s — an atomic rename gives a new
inode and leaves existing mappings intact — then fixes the SELinux label,
because `mv` carries the label of where the file came from and a mismatched
label makes the loader refuse the file silently. Then it still tells you to
reboot. Chapter 7 and [Lab 2](/ZygiskLab/labs/lab-02-safe-deploy/) are entirely
about this.

Enable the module for your target app in your manager's Zygisk scope list, if
your manager has one. On the reference rig — Pixel 6 Pro, Android 16, arm64,
KernelSU-Next 3.3.0, Zygisk Next 1.4.5 — that list is what decides which
processes your module is injected into. Then watch:

```bash
adb logcat -s ZygiskLab
```

Launch the target app. You should expect three lines per launch, in order:
`onLoad` with a pid; `preAppSpecialize` with the same pid, `getuid=0`, an
`args->uid` in the app range, and the app's process name; then
`postAppSpecialize` with the same pid again and `getuid` now equal to what
`args->uid` predicted. The exact pid and uid values will be different on your
device and different on every launch — a pid is whatever the kernel handed out,
and an app's uid is assigned at install time and differs per device. What you
are checking is the *shape*: same pid three times, uid moving from 0 to
non-zero, and the app's own `nice_name`.

## Failure catalogue

Four ways this goes wrong. Each has a distinguishing next step, which matters
more than the description — the point is to tell them apart quickly.

**The manager does not list the module.** The zip was rejected before Zygisk
was ever involved, so this is a packaging problem, not a code problem. Check
`module.prop` is at the zip root, not nested inside a directory — a zip made by
compressing the folder rather than its contents is the usual cause, and
`unzip -l out/01-hello-zygisk.zip` shows it immediately. Check the file has all
required fields and no trailing whitespace on `id`. Next step: `unzip -l`, then
the manager's own install log.

**Listed, enabled, rebooted — and silent.** No `ZygiskLab` lines at all. Work
outward from the least likely. Confirm the file is where the manager put it and
readable: `ls -lZ /data/adb/modules/zygisklab_hello/zygisk/` under `su -M`, and
look at the SELinux label as well as the mode — a wrong label is the classic
silent refusal, and it is exactly what `deploy.sh`'s `restorecon` exists to
prevent. Confirm the ABI filename matches the device's zygote. Confirm Zygisk
itself is enabled in the manager, and that your app is in scope. If `onLoad`
never prints, the loader either never opened your library or refused it at
`registerModule` — an API version mismatch returns early and produces no output
whatsoever, so check the header you built against is the one your loader
implementation supports. Widen the filter to `adb logcat | grep -i zygisk` and
read what the loader says about itself; that is usually where the answer is.
Chapter 6 covers the load path and [Appendix B](/ZygiskLab/book/appendices/b-troubleshooting/)
collects these.

**The lines appear for every process, not your target.** Your module is
working; your scope is wrong. Zygisk's default without an allowlist is to
inject broadly, and a hello-world that prints in every app is a hello-world
that prints hundreds of lines a minute. Fix it in the manager's scope list
first. Fix it in code second — a module that decides for itself whether to be
present is Chapter 11 and [Lab 3](/ZygiskLab/labs/lab-03-choosing-not-to-run/),
and it is the more robust answer because it does not depend on a manager
setting you might forget. Note that a genuinely global module is also a
detection surface: Chapter 22 covers what an app can see of a module it did not
expect.

**The device bootloops.** Your code runs inside zygote's children, and
`system_server` is one of them — a native crash there takes the system down and
Android's response to repeated `system_server` death is a reboot loop. This
module overrides neither server callback, so it is an unlikely failure for Lab
1 specifically; it becomes very likely the moment you start hooking. Recover
first: boot to recovery or use your manager's documented safe mode (on
KernelSU-Next, holding volume-down through boot disables all modules) and
remove `/data/adb/modules/zygisklab_hello/`. Then diagnose from the tombstone
in `/data/tombstones/` rather than by guessing — `adb pull` it once you are
booted. Chapter 3 covers bootloop recovery on the rig in full; do not attempt
Lab 1 on a device you cannot afford to have offline.

:::caution
Run this on your own device, against your own apps or apps you have written
permission to instrument. That is not a formality here — a module injected
system-wide sees every app on the device.
:::

The lab that follows is the same material as a procedure, with a self-check
that distinguishes "I saw output" from "I proved something".

:::note[Lab 1]
This chapter carries [Lab 1](/ZygiskLab/labs/lab-01-hello-zygisk/).
:::

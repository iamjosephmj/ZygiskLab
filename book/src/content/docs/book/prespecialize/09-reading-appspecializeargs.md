---
title: "Reading `AppSpecializeArgs`"
description: "Reading AppSpecializeArgs fields safely, identifying your target process, and matching one process of one package."
sidebar:
  order: 2
status: unverified
---

Every module has to answer one question before it does anything else: *is this
process one I care about?* You are called for every specialization the provider
scopes you to, and the overwhelming majority of those are processes you have no
interest in. Getting the answer wrong in one direction means your module never
fires. Getting it wrong in the other means it fires everywhere — which, as this
chapter's worked example insists, looks exactly like firing correctly until
somebody checks.

The only evidence you have is `AppSpecializeArgs`. The authority for what is in
it is the vendored header at `modules/01-hello-zygisk/jni/zygisk.hpp`, targeting
`ZYGISK_API_VERSION 5`. Nothing in this chapter is a field the header does not
declare, and where a field's *content* depends on the Android version or the
provider rather than on the header, this chapter says so instead of guessing.

## The struct, field by field

The header splits the struct into two blocks with a comment that is itself the
contract. The first block is labelled "Required arguments. These arguments are
guaranteed to exist on all Android versions." The second is labelled "Optional
arguments. Please check whether the pointer is null before de-referencing."

```cpp
struct AppSpecializeArgs {
    // Required
    jint &uid;
    jint &gid;
    jintArray &gids;
    jint &runtime_flags;
    jobjectArray &rlimits;
    jint &mount_external;
    jstring &se_info;
    jstring &nice_name;
    jstring &instruction_set;
    jstring &app_data_dir;

    // Optional — check for null before dereferencing
    jintArray *const fds_to_ignore;
    jboolean *const is_child_zygote;
    jboolean *const is_top_app;
    jobjectArray *const pkg_data_info_list;
    jobjectArray *const whitelisted_data_info_list;
    jboolean *const mount_data_dirs;
    jboolean *const mount_storage_dirs;
    jboolean *const mount_sysprop_overrides;

    AppSpecializeArgs() = delete;
};
```

The required ten are **references**. A reference cannot be null, so there is no
null check to write on `args->uid` or `args->nice_name` *as a field*. That is a
narrower guarantee than it first reads: the reference is always there, but a
`jstring &` is a reference to a Java object reference, and the object reference
it names can perfectly well be `null`. `args->nice_name` existing does not mean
`args->nice_name != nullptr`. Both checks are different and you need the second
one on every `jstring` and every array field before you hand it to JNI.

Taking the required block in order:

- **`uid`, `gid`** — the identity the process is *about to be given*. Not what it
  has now; in `preAppSpecialize` you are still zygote, as
  [Chapter 4](/ZygiskLab/book/foundations/04-hello-zygisk/) demonstrated by
  logging `getuid()` and `args->uid` side by side and getting two different
  numbers. Safe to read, and cheap — a plain `jint`, no JNI call.
- **`gids`** — the supplementary group list, a `jintArray`. Reading it means
  `GetIntArrayElements`/`ReleaseIntArrayElements` or a `Get…Region` copy, with
  the same pairing discipline as strings. Rarely what you want for identification.
- **`runtime_flags`** — the bitfield the framework passes to control ART's
  behaviour for this process (debuggable, JIT settings, and so on). The specific
  bits are framework constants that have changed across releases; the header
  neither defines nor promises them, so treat any bit-level interpretation as
  version-dependent and verify it against the platform source for the release you
  are on.
- **`rlimits`** — resource limits, as a `jobjectArray`. Read-only interest for
  almost every module.
- **`mount_external`** — which external-storage view the process gets. Again an
  integer whose meaning is a framework constant, not a header one.
- **`se_info`** — the SELinux info string the process will be labelled from. A
  `jstring`, and one whose format is a platform detail rather than an API. It is
  informative in logs; do not build identification on its shape.
- **`nice_name`** — the process name. The centrepiece of the next section, and
  the field most modules reach for first and misuse.
- **`instruction_set`** — the ABI string for the process, when the framework
  supplies one. Useful if your module genuinely needs to distinguish 32- from
  64-bit, though the more direct answer is which of your two `.so` builds is the
  one running. The `jstring` here can be null.
- **`app_data_dir`** — the app's data directory path. Also a `jstring` that can
  be null, notably for processes that are not going to be an ordinary app.

The optional eight are pointers, `const` pointers to mutable data, and the null
check the header demands is not optional advice. They exist because the
underlying framework specialization call gained parameters across Android
releases; a device whose zygote does not pass one hands you `nullptr`. Which are
non-null on which release is provider- and version-dependent, and the honest
statement is that you find out by checking, not by reading a table.

Two of them matter for identification. **`is_child_zygote`**, when non-null and
true, says the process you are in is going to become another zygote rather than
an app — the WebView and isolated-process hosts work this way — and in that case
much of the rest of the struct is not describing an app at all.
[Chapter 11](/ZygiskLab/book/prespecialize/11-choosing-not-to-run/) makes the
case that this is the first thing to test. **`is_top_app`** tells you the
process is being started as the foreground app, which is a scheduling hint, not
an identity.

`pkg_data_info_list` and `whitelisted_data_info_list` are `jobjectArray`s of
package data information used by the storage-mounting logic; `mount_data_dirs`,
`mount_storage_dirs` and `mount_sysprop_overrides` are booleans controlling
which mounts the specialization performs. `fds_to_ignore` is the list of file
descriptors zygote will not close — related to, but not the same as,
`Api::exemptFd()`, which is [Chapter 10](/ZygiskLab/book/prespecialize/10-setoption-and-flags/).

Finally: `AppSpecializeArgs() = delete`. You cannot construct one, and there is
no sensible copy of one, because it is a bundle of references into somebody
else's stack. Treat the pointer as valid for exactly the duration of the call.

## These are mutable references, and that is not an accident

In `preAppSpecialize` the parameter is `AppSpecializeArgs *`, not
`const AppSpecializeArgs *`. The header states the intent without ceremony: "You
can read and overwrite these arguments to change how the app process will be
specialized." `args->uid = 1000;` compiles, and it does what it says — the
process is specialized to that uid.

Be clear-eyed about what that is. These fields are not settings your module owns;
they are the request zygote is about to act on. Writing one alters the identity
the process assumes: its uid, its groups, its SELinux label, its name, its data
directory, the storage it can see. There is no confirmation step and no error
path. A process specialized to the wrong identity does not fail loudly; it runs,
with the wrong sandbox.

This book does not teach argument rewriting as a technique, and this chapter is
not the place it would appear if it did. What you need from the interface is
honesty about it, and one practical consequence: because these are references,
a typo turns a read into a write. `if (args->uid = target)` compiles. So does
assigning into `args->nice_name` when you meant to copy out of it. Read the
required block through `const` locals wherever you can, and never take a
non-`const` reference to one of these fields just to shorten an expression.

By contrast, `postAppSpecialize` receives `const AppSpecializeArgs *`. The same
struct, now a record of what happened rather than a request. That asymmetry is
the whole architecture, and
[Chapter 5](/ZygiskLab/book/load/05-anatomy-of-a-module/) treats it as such.

## Identifying your target: three candidates, none of them clean

You want "this is my app's main process". The struct offers three ways to
approximate that, and they approximate different things.

| | `nice_name` | `uid` | `app_data_dir` |
|---|---|---|---|
| Identifies | the **process** | the app's install identity | the app's **package** |
| Type | `jstring` (may be null) | `jint` | `jstring` (may be null) |
| Cost to read | JNI call + release | free | JNI call + release |
| Stable across devices | yes | **no** | yes |
| Distinguishes processes of one app | **yes** | no | no |
| Main risk | not the package name | per-device, per-install | filesystem layout |

### `nice_name` is the process name

This is the field that gets misused, so state the rule flatly: **`nice_name` is
the process name, not the package name.** They coincide for the common case,
because a component with no `android:process` attribute runs in a process named
after the package — which is exactly why matching `nice_name` against a package
name appears to work and is subtly wrong.

It diverges in two directions. A component declared with
`android:process=":remote"` runs in a process named `com.example.app:remote`,
which is not equal to `com.example.app`. And a component declared with a
process name that does not begin with a colon gets that name globally, with no
required relationship to the package at all — so a `nice_name` can belong to a
package whose name it does not contain.

The failure this produces is asymmetric and easy to miss. `name == package`
misses every non-default process of your own target, including — often — the one
that actually does the work. `name.starts_with(package)` catches the `:remote`
processes but also catches any package that happens to share your target's name
as a prefix. Neither is what you asked for.

### `uid` identifies the app, not the process, and not portably

`args->uid` is free to read and unambiguous about *which installed app* the
process belongs to. It is also the wrong shape twice over.

It does not distinguish processes: every process of one app shares its uid, so a
uid match arms you for all of them. And it is assigned at install time, so it
varies between devices and between installs on the same device. A uid hard-coded
from your development phone identifies a different app, or nothing, on anybody
else's. Multi-user devices compound this: the uid encodes both the user and the
app identity, so the same app for a second user is a different number.

Legacy `sharedUserId` packages take it further — several packages can share one
uid, and a uid match then covers all of them.

Where uid earns its place is as a *cheap filter*, not an identifier. It costs no
JNI call, so testing it first lets you skip the string work for the vast majority
of launches. That is the cost argument
[Chapter 8](/ZygiskLab/book/prespecialize/08-specialization-window/) makes: your
common path is the non-match, and it must be nearly free.

### `app_data_dir` carries the package

The data directory path for an ordinary app ends in the package name — the
`/data/user/<user-id>/<package>` shape, and the older `/data/data/<package>`
form it is presented as in some configurations. Taking the last path component
gives you the package, and it gives you the same answer on every device, unlike
uid.

The cost is that you are now depending on filesystem layout rather than on an
API. The prefix has changed across releases with multi-user and with
device-encrypted versus credential-encrypted storage, and whether you are handed
the CE path, the DE path, or a null is not something the header promises. Parse
the *basename*, never match the whole string, and handle null and a trailing
slash.

It also cannot tell processes apart. Every process of one app has the same data
directory.

### What to actually do

Nothing here is a single field. The strategy that survives contact is to use
them for what each one is:

1. **Reject the obvious non-targets first, for free.** Check `is_child_zygote`
   (if non-null) and check `args->uid` is in the app range at all. No JNI.
2. **Establish the package from `app_data_dir`** — its basename — because that is
   the field that actually names the package and does so identically on every
   device.
3. **Select the process from `nice_name`**, compared against the exact process
   name you want, not against the package. If you want the default process,
   that string is the package name; if you want `:remote`, it is
   `package:remote`. Either way it is a value you looked up, not one you assumed.
4. **Use `uid` as corroboration and as the cheap pre-filter**, never as the
   identity you hard-code.

If you only have the energy for one rule, make it this: match the *package* on
the data directory and the *process* on `nice_name`, and never let one field do
both jobs.

## Reading a JNI string safely in this window

Chapter 4 covered the mechanics of one call: `GetStringUTFChars` gives you a
modified-UTF-8 buffer owned by the runtime, and `ReleaseStringUTFChars` gives it
back. This chapter needs the discipline, because now you are doing it several
times, on fields that can be null, inside a function with early returns — and
early returns are where the release gets lost.

Three rules on top of the pairing.

**Null-check the `jstring` itself.** Passing null to `GetStringUTFChars` is
undefined behaviour, not an error return.

**Never leave an exception pending.** If the runtime cannot allocate the buffer
it returns `nullptr` and leaves an `OutOfMemoryError` pending. Calling almost
any other JNI function with an exception pending is undefined; `ExceptionCheck`,
`ExceptionClear` and `ExceptionDescribe` are the safe ones. An uncleared pending
exception is worse here than in ordinary JNI code for a specific reason: you are
not returning into your own Java frame, you are returning into the middle of
zygote's specialization path, in a process that has not finished becoming an app
and is about to be handed to third-party code. Whatever the runtime does with
your exception next, it will not look like your bug.

**Do not let the pointer outlive the release.** The buffer is valid until you
release it. If you want to keep the name, copy it into a `std::string`.

An RAII wrapper is the natural shape, and it removes the early-return problem by
construction:

```cpp
#include <jni.h>
#include <string>

class ScopedUtf {
public:
    ScopedUtf(JNIEnv *env, jstring s) : env_(env), obj_(s) {
        if (env_ == nullptr || obj_ == nullptr) return;
        if (env_->ExceptionCheck()) return;          // never call JNI with one pending
        chars_ = env_->GetStringUTFChars(obj_, nullptr);
        if (chars_ == nullptr && env_->ExceptionCheck()) {
            env_->ExceptionClear();                  // typically OutOfMemoryError
        }
    }

    ~ScopedUtf() {
        if (chars_ != nullptr) env_->ReleaseStringUTFChars(obj_, chars_);
    }

    ScopedUtf(const ScopedUtf &) = delete;
    ScopedUtf &operator=(const ScopedUtf &) = delete;

    explicit operator bool() const { return chars_ != nullptr; }
    const char *c_str() const { return chars_; }
    std::string copy() const { return chars_ ? std::string(chars_) : std::string(); }

private:
    JNIEnv *env_;
    jstring obj_;
    const char *chars_ = nullptr;
};
```

Every path out of a function using this releases correctly, including the one
where you decide in the first line that this is not your process. The `bool`
conversion collapses "field was null", "exception was already pending" and
"allocation failed" into one honest answer: you do not have a string.

:::caution
`GetStringUTFChars` returns *modified* UTF-8, not standard UTF-8. For process
names and package names — ASCII in practice — this never bites. Do not assume it
generalises to arbitrary Java strings you read elsewhere in the module.
:::

## Multi-process apps

An app is not a process. An app is a package that may declare any number of
processes through `android:process` on its components, and every one of them is
a separate fork of zygote, a separate `preAppSpecialize` call, a separate copy
of your module with its own globals ([Chapter 5](/ZygiskLab/book/load/05-anatomy-of-a-module/)).

The concrete consequences are the two failure modes named earlier, now with
mechanism attached. Arm on the package — by uid or by data directory — and you
arm on all of them: the UI process, the `:remote` service process, the push
process, whatever else the app declares. Your code runs several times per app
launch, in processes with different lifetimes, and any log you emit appears
several times with different pids. Arm on one process name, on the other hand,
and you get exactly one — which is what you want, provided it is the one that
does the work. Plenty of apps do their networking, their media handling or their
camera work in a `:remote` process precisely so it can be killed independently
of the UI. If your hook targets are in that process and you armed on the default
one, everything installs cleanly and nothing ever fires.

So find out rather than guess. Two ways, both from the host:

```bash
# Every running process of the package, with its uid and process name.
adb shell ps -A -o USER,PID,NAME | grep com.example.app

# What the package declares, and its uid on this device.
adb shell dumpsys package com.example.app | grep -iE 'userId|processName|process='
adb shell pm list packages -U | grep com.example.app
```

Exercise the app while `ps` is running — open it, background it, trigger the
feature you care about — because processes declared in the manifest only exist
once something in them is started. A process you never provoked will not appear
in the list, and that is the one you will miss.

:::note
Whether a given process of a scoped package is one your provider actually loads
you into is a provider question, not an interface question. Zygisk Next and
Magisk each have their own scope model. Do not infer the policy from one
observation; verify on your rig, and see
[Where it breaks](/ZygiskLab/book/companion/20-where-it-breaks/).
:::

## Worked example: arm on exactly one process of one package

The goal is narrow: fire in `com.example.app:remote` and nowhere else, and be
able to prove both halves. This is the identification core of the armed module
that [Chapter 11](/ZygiskLab/book/prespecialize/11-choosing-not-to-run/) and
[Lab 3](/ZygiskLab/labs/lab-03-choosing-not-to-run/) build out with its cost
measurement and its bail-out discipline; here it is only the decision.

```cpp
static constexpr const char *kPackage = "com.example.app";
static constexpr const char *kProcess = "com.example.app:remote";

// Basename of a data dir path: "/data/user/0/com.example.app" -> "com.example.app"
static std::string dir_basename(const char *path) {
    if (path == nullptr) return {};
    std::string p(path);
    while (!p.empty() && p.back() == '/') p.pop_back();
    const size_t slash = p.rfind('/');
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
    armed_ = false;

    // (1) Free rejections first. This path runs for every specialization.
    if (args->is_child_zygote != nullptr && *args->is_child_zygote) return;
    if (args->uid < 10000) return;   // not an installed-app uid

    // (2) Package, from the data directory.
    ScopedUtf data_dir(env_, args->app_data_dir);
    if (!data_dir) return;
    if (dir_basename(data_dir.c_str()) != kPackage) return;

    // (3) Process, from nice_name.
    ScopedUtf name(env_, args->nice_name);
    if (!name) return;
    if (std::strcmp(name.c_str(), kProcess) != 0) {
        LOGD("skip: pkg matched, process=%s uid=%d", name.c_str(), args->uid);
        return;
    }

    armed_ = true;
    LOGI("ARMED pid=%d process=%s uid=%d", getpid(), name.c_str(), args->uid);
}
```

`env_` is the `JNIEnv *` stashed in `onLoad`; `armed_` is a member consulted in
`postAppSpecialize`, where the actual work belongs. Note the ordering: the two
integer tests cost nothing and eliminate almost every launch on the device
before a single JNI call happens.

### Proving the match

Launch the process that hosts your target's `:remote` components and filter
logcat by your tag. You expect exactly one `ARMED` line, with a pid you can
confirm against `ps`:

```bash
adb logcat -c
# provoke the :remote process, then:
adb logcat -s ZygiskLab:V
adb shell ps -A -o PID,NAME | grep com.example.app
```

### Proving the non-match — the half that matters

A module that fires everywhere and a module that fires correctly produce
identical evidence when you only look at the target. So look elsewhere.

The `LOGD("skip: …")` line above is deliberate: it fires for the *other*
processes of the same package, and it is your proof that step (3) is doing work
rather than being redundant. Launch the app's UI process and you expect a `skip`
naming `com.example.app`, and no `ARMED`. Then take the negative control
further: launch two or three unrelated apps, and you expect **nothing at all** —
no `ARMED`, and no `skip` either, because they were rejected at step (2) before
any logging.

Three observations, all of which must hold:

| Process launched | Expect |
|---|---|
| `com.example.app:remote` | one `ARMED`, right pid |
| `com.example.app` (UI) | one `skip`, no `ARMED` |
| any unrelated app | no output from your tag |

If the third row produces output, your gate is not the gate you thought it was.
If the second row produces `ARMED`, you matched the package and called it a
process. If the first row produces nothing, you have the process name wrong —
go back to `ps` and read it rather than reasoning about the manifest.

Remove the `skip` log once you have the evidence. It costs a JNI string read
and a log write on every launch of the target package, which is exactly the
overhead Chapter 8 tells you not to leave in.

## What this chapter cannot tell you

Everything above about the struct comes from the header, and the header's
guarantees are narrow: ten required references that always exist, eight optional
pointers that may be null, and a deleted default constructor. It says nothing
about the *content* of any field. Which optional pointers are populated on your
device, what `runtime_flags` bits mean on your release, what shape
`app_data_dir` takes under your storage configuration, whether `nice_name` is
ever null in practice, and whether your provider loads you into a given process
at all — none of that is in the interface, and this chapter has not measured it.

Nothing here has been run on the reference rig — Pixel 6 Pro, Android 16, arm64,
KernelSU-Next 3.3.0, Zygisk Next 1.4.5. The identification logic is a reading of
the header plus the platform's documented process-naming rules. The three-row
table above is the experiment that turns it into knowledge, and it is worth
running before you build anything on top of it.

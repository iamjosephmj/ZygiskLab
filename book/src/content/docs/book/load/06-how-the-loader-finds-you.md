---
title: "How the loader finds you"
description: "ABI selection, linker namespaces, what you may link against, and why a stray exported symbol is a footprint."
sidebar:
  order: 2
status: unverified
---

Everything in Chapter 4 assumed a step that had already succeeded: your `.so`
got opened, and `zygisk_module_entry` got called. This chapter is about that
step, because it is the one that fails silently. A Zygisk module is a shared
library being `dlopen`ed into a freshly forked child of the most privileged
userspace process on the device, by a loader you did not write, in a linker
context you did not choose. None of the conveniences you get when Android loads
an app's own JNI library apply. If your library cannot be found, cannot be
linked, or was built for the wrong machine, the loader does not throw an
exception into your code — it has no code of yours to throw into. It gives up
and the app starts normally, which is indistinguishable from a module that ran
and did nothing.

A reader who finishes this chapter should be able to look at a build
configuration and predict which processes it will fail to load in, before
flashing anything.

## One `.so` per ABI

The packaging layout from Chapter 4 is not arbitrary:

```text
01-hello-zygisk.zip
├── module.prop
└── zygisk/
    └── arm64-v8a.so
```

Each file under `zygisk/` is named for the ABI it serves, using the NDK's ABI
names verbatim: `arm64-v8a.so`, `armeabi-v7a.so`, and on an x86 device
`x86_64.so` and `x86.so`. That is the whole naming convention. There is no
manifest listing them and no fallback: the loader, running in a process of a
given bitness and architecture, looks for the file whose name matches and opens
it.

The important word is *matches*. If there is no such file, nothing happens.
There is no error surfaced to the app, no crash, and typically nothing in
logcat under your own tag — because your tag comes from your code, and your
code was never mapped. Whatever the loader logs about the miss is the loader's
business and varies by provider; on the reference rig (Pixel 6 Pro, Android 16,
arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5) the place to look is the
provider's own log, not yours. From inside your module you cannot detect this
condition at all, which is the point: absence has no voice.

### The 64-bit device with 32-bit apps

This is where the rule bites people who thought they had shipped a working
module. A 64-bit Android device does not necessarily run a single zygote. Where
32-bit app support is present, the system runs a 64-bit zygote and a 32-bit
`zygote32`, and an app whose native code is 32-bit — or which the system
decides to launch 32-bit for compatibility — is forked from the 32-bit one.
That child process is `armeabi-v7a`. A module package containing only
`arm64-v8a.so` loads into every 64-bit app on the device and into none of the
32-bit ones.

The symptom is maddening precisely because it is partial. Your logging works.
Your hooks fire. They fire in nine apps out of ten, and the tenth — usually
some older thing, often exactly the app you were hired to look at — behaves as
if the module did not exist. Nothing is broken. You shipped one architecture.

Newer devices increasingly ship 64-bit-only, with no 32-bit runtime at all, in
which case the question does not arise. Whether a given device has a 32-bit
zygote is a property of that device's build, so check rather than assume:

```bash
adb shell getprop ro.zygote
```

A value naming both bitnesses (the `zygote64_32` family) tells you a 32-bit
zygote exists and that 32-bit app processes are possible. That is a device
fact, not a Zygisk fact, and it is the fact your ABI list has to satisfy.

### What shipping more ABIs costs

Adding an ABI to `Application.mk` is one line:

```make
APP_ABI := arm64-v8a armeabi-v7a
```

The cost is not build complexity, it is that every ABI is a complete,
independent copy of your module — the size numbers later in this chapter apply
per architecture — and, more expensively, a second target you are obliged to
test. A 32-bit build of the same source is a different compilation: different
pointer width, different alignment, different struct layout, different
integer-promotion edges. Code that is correct on `arm64-v8a` and merely
compiles on `armeabi-v7a` is code that will crash inside somebody's app
process, and a native crash in a zygote child is not a polite failure.

The honest rule: ship only the ABIs you will actually run and check. For the
labs in this book that is `arm64-v8a`, which is why the repo's
`Application.mk` says exactly that. Add `armeabi-v7a` when you have a 32-bit
process you genuinely need to be inside, and then treat it as a second port,
not a second checkbox.

## Linker namespaces, and why you are not an app's JNI library

When an app loads its own native library through `System.loadLibrary`, Android
gives it a lot of help. The library sits in the APK's `lib/<abi>/` directory,
the classloader has an associated linker namespace whose search path includes
that directory, and that namespace is linked to the system namespace with a
filter that lets exactly the public NDK libraries through. Dependencies inside
the APK resolve against each other; `libc.so`, `liblog.so` and friends resolve
across the link; a private platform library resolves against nothing, and you
get the familiar `library "libfoo.so" not found` at load time.

That machinery is Android's *isolated namespaces* feature, configured by the
platform's `ld.config.txt`. The mechanism is worth holding in your head at one
level of detail, because it explains every link error you will meet here. The
dynamic linker maintains several named namespaces. Each has search paths
(directories it will look in), permitted paths (directories a full-path load is
allowed to touch), and an isolation flag. Namespaces are joined by *links*, and
a link may carry a whitelist of library names — the classic case being the link
that exposes only the public LL-NDK set. Link resolution is not transitive:
namespace A linked to B does not reach what B is linked to. So "can I load
`libfoo.so`?" is not a question about the file system. It is a question about
which namespace the loading library is in, and what that namespace is linked
to.

Your module is not in the app's classloader namespace. It is not loaded by
`System.loadLibrary`, it is not in the APK, and at the moment it is loaded the
process may not have an app classloader yet at all. It is `dlopen`ed by the
Zygisk provider, out of `/data/adb/modules/<id>/zygisk/`, in whatever linker
context that provider's own code is running in — which derives from zygote's,
not the app's.

:::caution
The exact namespace your module lands in is a property of the provider's
implementation and of the Android version's `ld.config.txt`, and I have not
instrumented it on the rig. Do not build a design on a specific namespace name
or a specific configuration path — those differ between Android releases and
between Magisk's Zygisk and Zygisk Next. What is stable is the shape of the
consequence, below.
:::

The consequence is this. You are loaded in a context that resolves the platform
public libraries, and you should treat everything past that as unresolved until
proven otherwise, on the specific device and Android version in front of you.
Two failure modes follow from that, and they are worth distinguishing:

- **A link-time dependency that does not resolve kills the whole module.** The
  dynamic linker resolves your `DT_NEEDED` entries when it opens you. If one is
  not reachable from your namespace, `dlopen` fails, `zygisk_module_entry` is
  never found because there is no library to find it in, and you are in the
  silent-absence case again — indistinguishable, from your side, from shipping
  the wrong ABI.
- **A `dlopen` that fails is a null pointer you can handle.** Same underlying
  restriction, completely different position to be in.

That asymmetry is the practical heart of this chapter.

## What to link, what to `dlopen`, what will not resolve

The floor you can rely on is the stable NDK surface: `libc`, `libm`, `libdl`,
`liblog`. These are LL-NDK libraries, exposed by name across namespace links
precisely because they are the platform's stable native contract. The repo's
`Android.mk` links exactly one of them explicitly:

```make
LOCAL_LDLIBS := -llog
```

`libc`, `libm` and `libdl` come in from the toolchain's own defaults; `liblog`
does not, which is why `__android_log_print` needs the flag. That single line
is the whole external dependency set of Lab 1, and keeping it that short is a
deliberate choice, not an accident of the module being small.

Above that floor, the risk rises with distance from the NDK:

| Category | Link at build time? | Notes |
| --- | --- | --- |
| `libc`, `libm`, `libdl`, `liblog` | Yes | Stable, present everywhere |
| Other public NDK libs (`libz`, `libandroid`, `libEGL`, …) | Usually | Present, but confirm your target process actually has them mapped |
| Platform internals (anything not in the NDK list) | No | May resolve on one device and not the next |
| Anything you ship yourself | No — static-link it | Nothing loads companion `.so` files for you |

Note the third row is not a promise that platform internals are unreachable. It
is a statement that whether they are reachable is version- and
provider-dependent, and a build that links one is a build whose loadability you
cannot reason about. If you need one, `dlopen` it at the moment of use and
handle the null:

```cpp
void *h = dlopen("libsomething.so", RTLD_NOW);
if (!h) {
    LOGI("libsomething.so unavailable here: %s", dlerror());
    return;               // module still loaded, feature simply off
}
```

The difference between that and a `DT_NEEDED` entry is the difference between a
feature that is absent in some processes and a module that is absent in some
processes. In an environment where you are injected into hundreds of unrelated
processes with different library sets mapped, that is not a stylistic
preference. It is how you avoid a whole class of "works on my phone" bugs.

:::note
Deferring to `dlopen` also solves the API-level problem the NDK documents
separately: a symbol newer than `APP_PLATFORM` cannot be referenced directly,
but can be resolved at runtime with `dlsym` on a device new enough to have it.
The repo sets `APP_PLATFORM := android-29`, a floor for what you may reference
at build time, not a statement about what the device has.
:::

## The C++ runtime, generalised

Chapter 4 worked through why this module's `Application.mk` sets
`APP_STL := c++_static` — `none` cannot link because
`REGISTER_ZYGISK_MODULE`'s function-local statics need `__cxa_guard_acquire`
and `__cxa_guard_release`, and `c++_shared` is wrong because the package ships
no `libc++_shared.so`. Both of those are instances of one rule, and the rule is
what you should actually carry forward:

**A Zygisk module must be self-contained, because nothing guarantees any
runtime you did not bring is present in the process you land in.**

The library search that resolves an app's dependencies against its own APK
directory has no analogue for you. There is no directory alongside your `.so`
that the linker searches on your behalf, and even if a `libc++_shared.so`
happened to exist somewhere on the device, whether it is reachable from your
namespace and whether its ABI matches the one you compiled against are two more
things you would be assuming. Static linking here is not a preference about
binary size or startup cost. It is the only configuration whose success does
not depend on facts about the host process that you cannot check before you are
loaded.

The same rule extends past the STL. Any third-party code you want — a hooking
framework, a compression library, a parser — arrives as a static archive linked
into your one `.so`, or it does not arrive. "Ship two libraries and load the
second one" is a design that works in an app and does not work here.

## Size, and where the floor comes from

Your module is mapped into every process the provider injects it into. On a
broadly scoped module that is essentially every app on the device, at every
launch, for the life of the boot. Size is therefore not a build-hygiene concern
you can defer; it is resident memory multiplied by the process count, and it is
a footprint in the Part VI sense — a quarter-megabyte of unexplained mapped
code in an app's own address space is a thing that can be noticed.

The number for Lab 1: `libzygisklab.so` is around 232KB, for a module whose own
contribution is three log lines and a class with three overrides. Essentially
all of it is the statically linked C++ runtime.

The repo's build already applies everything that genuinely helps:

```make
APP_CPPFLAGS := -fno-exceptions -fno-rtti
APP_CFLAGS   := -Oz -flto
APP_LDFLAGS  := -flto -Wl,--gc-sections
```

plus `-fvisibility=hidden -fvisibility-inlines-hidden` in `Android.mk`. `-Oz`
optimises for size rather than speed. `-flto` on both sides lets the linker
optimise across translation units. `--gc-sections` drops sections nothing
references, and hidden visibility is what makes `--gc-sections` effective —
a symbol that might be called from outside cannot be proven dead, so hiding
symbols is a size optimisation as well as a correctness one. `-fno-exceptions`
and `-fno-rtti` remove your code's need for the machinery that supports them.

And after all of that, 232KB. It is worth being precise about why, because the
reason tells you what would and would not help.

Static archives are linked at **object-file granularity**. The linker pulls in
whole `.o` members from `libc++_static.a` and `libc++abi.a` to satisfy the
symbols you need, and each member brings whatever else it contains. The guard
functions you cannot avoid live in libc++abi alongside the unwinding and
type-info machinery, so the `.eh_frame` and `.gcc_except_table` you get are not
your exceptions — they are the runtime's own, and `--gc-sections` cannot prove
them dead because unwind tables are referenced by the personality machinery
rather than by a call you could trace. `-fno-exceptions` disables exceptions in
*your* translation units. It does not recompile the archive.

So the floor is real, and this repo does not have a fix for it. Stripping moves
the number very little; LTO cannot cross into a prebuilt archive. The honest
options are to accept the floor, or to leave the C++ API surface behind
entirely and write against the loader's C ABI with no function-local statics
anywhere — which means not using `REGISTER_ZYGISK_MODULE`, taking on the
`api_table` handshake yourself, and giving up the thing the header exists to
provide. That is a trade worth knowing about and, for a book whose modules are
meant to be read, not one worth making. What you should do is stop adding to
it: every static library you link is added on top of a floor you already
cannot lower.

## Symbol visibility

```make
LOCAL_CPPFLAGS := -std=c++20 -fvisibility=hidden -fvisibility-inlines-hidden
```

There are two independent reasons for this line, and module authors tend to
know only the second.

The first is correctness. Your library is loaded into a process alongside the
host's own libraries, and symbol resolution in a process is not
compartmentalised the way you might hope. A default-visibility symbol in your
`.so` with a name that collides with one the host resolves — a common C name,
an operator, a helper you called `init` — is a symbol that can be bound to
instead of the intended one, depending on load order and lookup scope. You did
not intend to hook anything; you interposed by accident, and the resulting
misbehaviour appears in the host's code with no trace leading back to you.
Hidden by default means the only symbol you export is the one you meant to.

For a Zygisk module that is `zygisk_module_entry`, and the header marks it
`[[gnu::visibility("default")]]` explicitly so it survives the flag — plus
`zygisk_companion_entry` if your module has a companion. `-fvisibility=hidden`
is safe precisely because the contract symbol opts back in by name.

The second reason is that an exported symbol table is *readable*. Any code
running in the process — including the app's own code, including code written
to look — can walk its loaded libraries and read their dynamic symbol tables.
It does not need root and it does not need to be clever; the linked-list of
loaded objects and the symbol tables they point at are ordinary memory in the
process's own address space. A library exporting `HelloZygisk::onLoad` or a
tidy set of `hook_camera_*` names is a library that has described itself to
whatever asks.

Hidden visibility does not make you invisible. The library is still mapped, the
mapping is still enumerable, and one exported entry point with a well-known
Zygisk name is itself a signal. What it does is reduce a description to a
presence. Chapter 21 takes this apart properly — what is actually visible from
inside an app process, how it is enumerated, and which of your build choices
change the answer.

## What to check before you flash

A short list, in the order that catches problems earliest:

1. `unzip -l` your package and confirm a `zygisk/<abi>.so` exists for every ABI
   you intend to run in, spelled exactly as the NDK spells it.
2. `adb shell getprop ro.zygote` on the target device. If it names two
   bitnesses and you shipped one ABI, you have already found your bug.
3. `readelf -d libzygisklab.so | grep NEEDED` and read the list. Anything
   outside the stable NDK floor is a load-time gamble; move it to `dlopen`.
4. `readelf --dyn-syms libzygisklab.so` and confirm the exported set is
   `zygisk_module_entry` and nothing you did not intend.
5. Check the size, and know which part of it is yours.

None of this has been run on the rig for this chapter — it is reasoning from
the build files and from the platform's documented linker behaviour, and the
namespace details in particular are the kind of thing that shifts between
Android versions. Chapter 7 puts the module on the device properly, which is
where these predictions start becoming observations.

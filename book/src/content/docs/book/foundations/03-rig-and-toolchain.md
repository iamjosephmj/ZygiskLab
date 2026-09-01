---
title: "The rig and the toolchain"
description: "The Pixel 6 Pro reference rig, NDK setup, module.prop fields, on-device paths, and recovering from a bootloop."
sidebar:
  order: 3
status: unverified
---

The hardest bug in Zygisk work is not a bug in your module. It is spending an
afternoon debugging code that never ran. A module that fails to load, or is
silently rejected by the provider, looks exactly like a module whose callbacks
are broken: nothing in the log, nothing in the app, no crash. This chapter
builds the rig, but more importantly it builds the habit of answering one
question before any other — *did my code run at all?*

## The rig

Every procedure in this book is written against one machine:

| | |
|---|---|
| Device | Pixel 6 Pro |
| OS | Android 16 |
| ABI | arm64 |
| Root | KernelSU-Next 3.3.0 |
| Zygisk provider | Zygisk Next 1.4.5 |

That is stated once and referred to as "the rig" from here on. It is not a
recommendation to buy a Pixel 6 Pro; it is a statement about what has been
observed. Where behaviour is likely to differ on Magisk, on another provider
version, or on another Android release, the book says so rather than quietly
generalising.

Two of those values do most of the work. **arm64** determines the ABI your `.so`
is named for and the toolchain you build with. **Zygisk Next 1.4.5** is what
actually loads your module: KernelSU-Next provides root and module mounting, but
does not implement Zygisk. Zygisk Next is a separate module installed on top,
and it is what reads your `zygisk/` directory and calls your callbacks. If you
remember one distinction from this chapter, make it that one — "root works" and
"Zygisk works" are independent claims, and beginners conflate them constantly.

On Magisk, Zygisk is built in and toggled in the Magisk app's settings, and
Zygisk Next exists partly as a replacement for it. The module you write is the
same either way — that is the point of the Zygisk API — but "is the provider
present" and "is it enabled" have different answers in different places.

### Verifying each layer is actually active

Check these in order. Each one presupposes the one before it, so the first
failure tells you where to stop looking.

**Is the device reachable and is root working?**

```bash
adb devices -l
adb shell su -c id
```

The second should print a `uid=0(root)` line. If it hangs, look at the device
screen: KernelSU prompts for permission the first time a shell asks for root,
and an unanswered prompt looks identical to a hang. If it fails outright,
nothing further in this chapter will work.

:::note
On KernelSU, `su -c` and `su -M -c` are not interchangeable. `-M` runs in the
global mount namespace, which is what writing into the module directory
requires — the repo's `deploy.sh` uses `su -M -c` for exactly this reason, and a
plain `su -c` write there is denied even as root. If a write into
`/data/adb/modules` mysteriously fails, check that flag first.
:::

**Is the module infrastructure processing modules at all?**

```bash
adb shell su -c 'ls -l /data/adb/modules'
```

You should see one directory per installed module, named by its `id`. KernelSU
also ships a command-line tool, `ksud`, used by its own rescue documentation:

```bash
adb shell su -c 'ksud module list'
```

That distinguishes "the directory exists on disk" from "the manager knows about
these modules" — different failures. `ksud`'s availability and output are
version-specific; confirm its subcommands against your own KernelSU build
rather than trusting this page.

**Is the Zygisk provider loaded and enabled?**

This is the check that matters most and the one with the least portable answer.
Zygisk Next is itself a module, so it appears in the module list like any other,
and its entry reports its own status — whether it is running, and which modules
it could not load — in the manager's module description line and in its WebUI.
Read that line. A provider that is *installed but not running* still lists
cleanly in a bare `ls /data/adb/modules`, which is exactly why the directory
listing is not sufficient evidence. On Magisk, the equivalent is the Zygisk
toggle in the app's settings and the Zygisk state on its home screen.

None of this substitutes for the real proof, which is the one Lab 1 gives you: a
log line printed from inside your own `onLoad`. Provider status tells you the
machinery is running. Only your own log line tells you it reached *your* code.

## Use a spare device

Use a device you can afford to destroy. This is not caution for its own sake.

You will bootloop this device. Not "might" — a module that crashes during
zygote's startup takes every app process with it, and the symptom is a phone
that reaches the boot animation and stays there. That is a normal Tuesday in
module development, and the recovery procedure below exists because you will
use it.

More to the point, Part II's Lab 2 asks you to deliberately corrupt a running
zygote — to overwrite a mapped `.so` in place and watch app specialization
segfault, so you understand *why* `deploy.sh` uses an atomic `mv` rather than
`cp`. That lab only teaches if you are willing to run it. On your daily driver
you will read about it and skip it, and learn nothing, because the mechanism
only becomes real once you have watched it happen.

A device you cannot afford to reflash makes you timid, and timid experimentation
teaches nothing. The requirements are modest: an unlockable bootloader, a
factory image you can flash, and nothing on it you would miss. Download the
image *before* you start, not after the phone stops booting.

:::caution
Unlocking the bootloader wipes the device, and on many devices it cannot be
undone without wiping again. Do this on hardware whose data you have already
written off. And re-read
[Rules of Engagement](/ZygiskLab/book/foundations/02-rules-of-engagement/): your own
device, your own apps, or written permission — nothing else.
:::

## The NDK and the API level

A Zygisk module is a native shared library: no Java, no Gradle, no APK. The
build is `ndk-build` over a `jni/` directory, and the output is a single `.so`.

Get the NDK through Android Studio's SDK Manager (SDK Tools → NDK) or as a
standalone download. Either way you end up with a versioned directory
containing an `ndk-build` script at its root. The repo's `build.sh` finds it
like this:

```bash
NDK_BUILD="${ANDROID_NDK_HOME:+$ANDROID_NDK_HOME/ndk-build}"
NDK_BUILD="${NDK_BUILD:-$(command -v ndk-build || true)}"
[ -x "$NDK_BUILD" ] || { echo "ndk-build not found; set ANDROID_NDK_HOME" >&2; exit 1; }
```

So `ANDROID_NDK_HOME` is not magic: it is the one variable the build scripts
consult, and it must point at the NDK directory itself — the one containing
`ndk-build` — not the SDK root, and not the `ndk/` folder holding several
versions. Export it in your shell profile:

```bash
export ANDROID_NDK_HOME="$HOME/Android/Sdk/ndk/<version>"
```

If `ndk-build` is on your `PATH` the variable is optional, but setting it is the
better habit: it pins which NDK you built with when you have several installed.

### Why `android-29` and not the newest

The repo's `jni/Application.mk` says:

```make
APP_ABI      := arm64-v8a
APP_PLATFORM := android-29
```

`APP_PLATFORM` sets the *minimum* API level: it selects the platform headers and
stub libraries you link against, and therefore which libc symbols your `.so` may
reference. It is a floor, not a target. A library built against `android-29`
loads and runs fine on Android 16; one built against a newer platform may
reference symbols an older device's libc does not export, and then it fails to
load on a device otherwise perfectly capable of running it.

For ordinary app development you raise the target to gain new APIs. Here that
reasoning does not apply: your module calls a handful of libc and liblog
functions plus the Zygisk API, and the Zygisk API comes from the provider at
runtime, not from the platform. Raising `APP_PLATFORM` buys you nothing you are
using and costs you the range of devices the module loads on. So you pick a low
floor and leave it there.

`APP_ABI := arm64-v8a` is the matching decision on the architecture axis: the
rig is arm64, so that is the only ABI built. A module for wider distribution
would list more ABIs and ship one `.so` each; the naming convention that makes
that work is covered below.

The rest of `Application.mk` is about size and linkage, and the file's own
comments explain it: `APP_STL := c++_static`, because `zygisk.hpp`'s
`REGISTER_ZYGISK_MODULE` macro expands into function-local statics whose guarded
initialisation needs `__cxa_guard_acquire` from the C++ runtime — so
`APP_STL := none` cannot link the header at all — and `c++_static` rather than
`c++_shared` so the `.so` stays self-contained. Chapter 6 returns to what that
costs in binary size.

## `module.prop`, field by field

`module.prop` is the module's identity card: a plain `key=value` file with UNIX
(LF) line endings, since CRLF breaks parsing. The repo's real one:

```properties
id=zygisklab_hello
name=ZygiskLab 01 - Hello Zygisk
version=1.0
versionCode=1
author=Joseph James
description=Lab 1. Logs pid, uid, and nice_name at each Zygisk callback. Does nothing else.
```

Six fields, all required by the Magisk module format that KernelSU also follows:

**`id`** — the only machine-significant field. It becomes the directory name
under `/data/adb/modules`, so it is how the installer, the manager and your own
`deploy.sh` find the module. Magisk's documentation specifies the regex
`^[a-zA-Z][a-zA-Z0-9._-]+$`: start with a letter, then letters, digits, dots,
underscores and hyphens. No spaces. Changing `id` between versions does not
upgrade the module — it installs a second, unrelated one alongside the old
directory.

**`name`** — the human-readable title in the manager's module list. Display
only.

**`version`** — a display string shown next to the name. Not compared or parsed.

**`versionCode`** — an integer, and the field that *is* compared: the manager
and any update mechanism use it to decide whether an installation is an upgrade
or a downgrade. Bump it every time you produce a package you intend to install
over an existing one. Non-integer content here is a malformed file.

**`author`** — display only.

**`description`** — display only, one line. Providers may append to or overwrite
it at runtime to report status: Zygisk Next surfaces load failures and
crash-implicated modules through the description shown in the manager. Text you
did not write appearing there is the provider talking, and it is worth reading.

The one common optional field is **`updateJson`**, a URL pointing at update
metadata for the manager's in-app update check. The repo's module omits it, and
so should you until you are distributing something.

Malformed `module.prop` fails quietly. An invalid `id`, a missing required
field, or CRLF line endings can leave you with a module that installs, appears
in the directory listing, and never loads, with no error text pointing at the
file. When a module refuses to appear or behave, re-read this file before you
re-read your C++.

## The module directory on the device

Modules live under `/data/adb/modules/<id>/`. The repo's `build.sh` packages two
things into the flashable zip:

```bash
mkdir -p "out/pkg/zygisk"
cp module.prop "out/pkg/module.prop"
cp libs/arm64-v8a/libzygisklab.so "out/pkg/zygisk/arm64-v8a.so"
```

which on the device becomes:

```text
/data/adb/modules/zygisklab_hello/
├── module.prop
└── zygisk/
    └── arm64-v8a.so
```

That is the entire module. Note the rename: the build output is
`libzygisklab.so`, but inside `zygisk/` it is `arm64-v8a.so`. **The file name
inside `zygisk/` is the ABI, not the library name.** The provider looks for a
file matching the ABI of the process it is injecting into, so a module
supporting both 64- and 32-bit apps ships `arm64-v8a.so` and `armeabi-v7a.so`
side by side — different builds of the same source. Getting this name wrong
produces the pure form of the failure this chapter opened with: the module is
listed, the provider is running, and nothing ever happens.

That tree does not appear the moment you install, though. On this rig —
KernelSU-Next 3.3.0 — `ksud module install <zip>` **stages** the module rather
than placing it live. It extracts the zip to `/data/adb/modules_update/<id>/`
and writes an `update` marker file into `/data/adb/modules/<id>/`. The staged
tree only becomes the live one at the next reboot: after rebooting,
`/data/adb/modules_update/` was empty and
`/data/adb/modules/zygisklab_hello/zygisk/arm64-v8a.so` existed. This is
observed on KernelSU-Next 3.3.0; other providers stage differently or not at
all, so do not carry the path names to Magisk without checking.

Two things follow. The first is that between install and reboot there is a
window in which `/data/adb/modules/<id>/zygisk/` does not exist yet, which is
why a fresh install followed immediately by `deploy.sh` refuses to run —
[Chapter 7](/ZygiskLab/book/load/07-deploying-safely/) covers that. The second
is about labels: the installer set the staged `.so`'s SELinux context to
`u:object_r:system_file:s0` by itself, which is the context the API header names
when it says the module directory must be readable by zygote. (`module.prop` in
the live directory carried `u:object_r:adb_data_file:s0`, and the `.so` in a
live, working module carries a third label again,
`u:object_r:system_lib_file:s0`.) Three sources, three labels. A file you push
and `mv` into place yourself lands with the staging path's label, which is none
of them, so
[Chapter 7](/ZygiskLab/book/load/07-deploying-safely/) has `deploy.sh` set the
label explicitly and verify it rather than deriving it with `restorecon` — which
on this path yields `adb_data_file`.

The Magisk module format defines other paths the *module author* may create,
none used by Lab 1: `system/`, mounted over `/system`; the `post-fs-data.sh` and
`service.sh` boot scripts; `uninstall.sh`; `action.sh` for the manager's action
button; `system.prop`; and `sepolicy.rule`. They are listed so you recognise
them in other people's modules — the book uses `service.sh` later, for a
root-side companion.

Separately there are marker files the *provider* manages, which you never create
by hand:

- `disable` — an empty marker file. Its presence disables the module. This is
  what the manager's disable toggle creates, and what safe mode creates for
  every module at once.
- `remove` — marks the module for deletion at the next reboot.
- `skip_mount` — suppresses mounting of the module's `system/` directory.
- `update` — used by the installation flow to stage an update.

The distinction matters when you are cleaning up by hand from a recovery shell:
creating a `disable` file is the least destructive way to take a module out of
the picture.

:::caution
Beyond `module.prop` and the `zygisk/` directory, exact on-device layout is
provider-specific and version-specific. A provider keeps its own state — cached
copies, per-module status, scope configuration — in locations of its own
choosing, and those are not part of the module format contract. Do not build
tooling against a path you found by browsing; confirm it against your own
provider version and expect it to move between releases.
:::

## Reading logs that matter

Your own log is the first-class diagnostic. The repo's `main.cpp` sets up one
tag:

```cpp
#define LOG_TAG "ZygiskLab"
```

So the primary command, from the module's README, is:

```bash
adb logcat -s ZygiskLab
```

`-s` suppresses everything except the tags you name. Clear the buffer first when
you are about to reproduce something:

```bash
adb logcat -c && adb logcat -s ZygiskLab:V
```

To watch your tag and the provider together, name both — `-s` takes a list:

```bash
adb logcat -s ZygiskLab:V zygisk:V
```

The provider's exact tag is not part of any API and this chapter will not invent
one for you. Find it once, empirically, then reuse it: reboot with
`adb logcat -b all` running and grep the early output for the provider's name,
or run `adb logcat | grep -i zygisk` and see which tags appear. Five minutes of
work that pays for itself every subsequent debugging session.

### Where a crash in your module actually surfaces

This is the part beginners get wrong, and it costs hours.

Your module's code runs inside a process that is not yours: in
`preAppSpecialize` a zygote fork, in `postAppSpecialize` the app. Dereference a
null pointer there and the process that takes the SIGSEGV is the app. So:

- There is **no stack trace attributed to your module** and no dialog naming
  you. Android reports the crash against the process that died — the app.
- The visible symptom is **the app failing to start**, or dying immediately
  after launch, with no message. Early enough, or in zygote itself, the symptom
  is a **boot loop**.
- The crash *is* recorded, under the native crash tag, in a tombstone whose
  faulting frames point into your `.so` by path. That is the evidence it was you.

So when an armed app stops launching after you install a module:

```bash
adb logcat -b crash
adb logcat | grep -i -E 'DEBUG|tombstone|SIGSEGV'
adb shell su -c 'ls -lt /data/tombstones | head'
```

The first two show the native crash report as it happens; the third lists
tombstones newest-first, for a crash that already happened. In a tombstone, look
in the backtrace for a frame whose mapped file is your `.so` under
`/data/adb/modules`. That frame is your answer.

The other place to look is the provider. Zygisk Next detects modules that fail
to load and modules implicated in a crash, and reports them in its module status
and WebUI — often faster than reading a tombstone, and the reason to check the
manager before assuming the log has nothing.

:::note
A module that produces *no* output is a different failure from one that crashes,
and they need different investigations. No `onLoad` line means the provider
never reached your code: wrong ABI file name, wrong SELinux label, malformed
`module.prop`, module disabled, provider not running, or the app not in scope. A
crash means your code ran and was wrong. Establish which you have before
theorising.
:::

## Recovering a device that will not boot

Read this section now, while your device still boots. The thing that makes a
bootloop expensive is discovering the procedure while you are inside it.

Four levers, in increasing order of severity.

**1. Safe mode.** KernelSU has a built-in safe mode that disables every module on
the modules page. Its documentation describes triggering it by pressing
volume-down more than three times after the first boot screen — distinct
press-and-release motions, not a held button. The timing is fiddly and often
misses on the first attempt; that is normal, try again. Some Android builds also
have their own system safe mode via a long-press of volume-down: a different
mechanism, same effect on modules. Once booted, uninstall the offending module
through the manager and reboot. Magisk's equivalent is Core Only Mode.

**2. ADB, if the device gets far enough.** A device stuck at the boot animation
sometimes still has `adbd` running, which turns a brick into a two-command fix.
Check `adb devices`; if it responds, use KernelSU's own tool:

```bash
adb shell su -c 'ksud module list'
adb shell su -c 'ksud module disable <id>'
adb reboot
```

`ksud module uninstall <id>` removes it outright. Prefer disabling — you want
the module still on disk so you can inspect what you shipped.

**3. A recovery shell.** From recovery, mount data and remove the module:

```bash
mount /data
rm -rf /data/adb/modules/<id>
```

Or, less destructively, disable it and keep the evidence:

```bash
touch /data/adb/modules/<id>/disable
```

The `disable` marker is honoured by both Magisk and KernelSU, since KernelSU
follows the Magisk module format. Whether your recovery has a shell depends on
what you flashed — a custom recovery generally does, stock recovery generally
does not — which is an argument for having one available before you need it.

:::caution
If the device will not boot *and* has no recovery shell and no ADB, safe mode is
your last software option before reflashing. That is the concrete reason the
spare-device rule is not optional.
:::

**4. Reflash.** Factory image, fastboot, start again.

There is also a **KernelSU-specific** escape hatch for when root itself, rather
than one module, is the problem: the KernelSU rescue guide describes removing
`/data/adb/ksud` from recovery and optionally deleting the injection files at
`/metadata/ksu/modules.rc` and `/metadata/watchdog/ksu/modules.rc`. That takes
out the whole KernelSU module pipeline. Confirm those paths against the rescue
guide for your KernelSU version — they are implementation details, not a stable
contract.

## Before you write any module code

A two-minute checklist. Each line is a claim you can falsify, which is the only
kind worth checking.

- `adb devices -l` lists the device
- `adb shell su -c id` prints `uid=0(root)`
- `/data/adb/modules` exists and lists your installed modules
- The manager shows the Zygisk provider present *and* running — not merely
  installed
- `ndk-build` runs, either on `PATH` or via `ANDROID_NDK_HOME`
- `./build.sh` in `modules/01-hello-zygisk` produces `out/01-hello-zygisk.zip`
- You know your provider's logcat tag, having found it once yourself
- You have a factory image on disk and you know how to enter safe mode

The last two are the ones people skip, and the two that decide whether a bad
afternoon costs ten minutes or two days.

Everything above is *expected* behaviour, drawn from the repository's real build
scripts and from the KernelSU and Magisk documentation. This chapter is marked
unverified: none of these commands has been run on the rig for this write-up.
Where a path, tag or menu item is provider-specific it is labelled as such —
check those against your own provider version, and trust your own `onLoad` log
line over anything on this page.

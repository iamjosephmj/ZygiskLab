---
title: "Cheatsheet"
description: "The book's commands, gathered onto one page for quick reference."
sidebar:
  order: 3
status: unverified
---

Every command here is one this repository or this book actually uses. Where a
command is provider-specific or destructive, it says so on the line.

Rig: Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5.
`<id>` is the `id=` line in the module's `module.prop`.

## Build a module

```bash
export ANDROID_NDK_HOME="$HOME/Android/Sdk/ndk/<version>"   # dir containing ndk-build
cd modules/01-hello-zygisk && ./build.sh                    # same in 03-06
```

Output: `libs/arm64-v8a/libzygisklab.so` and a flashable `out/<module>.zip`.

Lab 7 is the one Gradle project:

```bash
export JAVA_HOME=/path/to/jdk17+
cd modules/07-detection-harness && ./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n dev.zygisklab.detectionharness/.MainActivity
```

## Deploy safely

Install the zip once through your manager first, so the module directory exists.
Then, for every rebuild — stage, rename, reboot. Never `cp` over a mapped `.so`.

```bash
cd modules/01-hello-zygisk && ./deploy.sh [serial]
```

The same thing by hand:

```bash
adb push libs/arm64-v8a/libzygisklab.so /data/local/tmp/arm64-v8a.so
adb shell su -M -c 'mv /data/local/tmp/arm64-v8a.so \
  /data/adb/modules/<id>/zygisk/arm64-v8a.so'
adb shell su -M -c 'restorecon /data/adb/modules/<id>/zygisk/arm64-v8a.so'
adb reboot
```

`su -M` (global mount namespace) is required to write into the module
directory; plain `su -c` is denied there under KernelSU.

Did the file land? Hash both sides — a match still needs the reboot, because
the stale mapping, not the file, is what you are fighting:

```bash
md5sum libs/arm64-v8a/libzygisklab.so
adb shell su -M -c 'md5sum /data/adb/modules/<id>/zygisk/arm64-v8a.so'
adb shell su -M -c 'ls -Z /data/adb/modules/<id>/zygisk/'   # labels match module.prop
```

## Verify the provider is live

```bash
adb devices -l
adb shell su -c id                          # expect uid=0(root)
adb shell su -c 'ls -l /data/adb/modules'   # on disk
adb shell su -c 'ksud module list'          # KernelSU; subcommands are version-specific
```

Installed is not running. Read Zygisk Next's own status line in the manager (on
Magisk: the Zygisk toggle in settings). The only proof that the provider reached
*your* code is your own log line from `onLoad`.

## Watch logs

```bash
adb logcat -s ZygiskLab                     # your tag only
adb logcat -c && adb logcat -s ZygiskLab:V  # clear, then reproduce
adb logcat -s ZygiskLab:V zygisk:V          # yours + provider; find its real tag once
adb logcat | grep -i zygisk                 # how to find that tag
```

A crash in your module does not appear on your tag:

```bash
adb logcat -b crash
adb logcat | grep -i -E 'DEBUG|tombstone|SIGSEGV'
adb shell su -c 'ls -lt /data/tombstones | head'
```

In the tombstone backtrace, look for a frame mapped from your `.so` under
`/data/adb/modules`.

## Inspect a process

From an app's own process, via Lab 7's harness. From a shell, the same files
read as a different process with different privilege — useful, but not what an
app sees:

```bash
adb shell su -c 'cat /proc/<pid>/maps'       # mapped .so paths
adb shell su -c 'ls -l /proc/<pid>/fd'
adb shell su -c 'cat /proc/<pid>/mountinfo'
adb shell su -c 'grep TracerPid /proc/<pid>/status'
adb shell pidof <package.name>
```

## Recover a device

```bash
adb shell su -c 'ksud module disable <id>' && adb reboot   # preferred: keeps evidence
adb shell su -c 'ksud module uninstall <id>'               # removes it outright
```

If it will not boot, from a recovery shell:

```bash
mount /data
touch /data/adb/modules/<id>/disable   # honoured by Magisk and KernelSU
rm -rf /data/adb/modules/<id>          # DESTRUCTIVE: deletes what you shipped
```

:::caution
Spare device only. Lab 2 deliberately corrupts a running zygote, and a module
that crashes in zygote bootloops the phone. Have a factory image downloaded
before you start. Stock recovery generally has no shell.
:::

Full context: [The rig and the toolchain](/ZygiskLab/book/foundations/03-rig-and-toolchain/)
and [Deploying safely](/ZygiskLab/book/load/07-deploying-safely/).

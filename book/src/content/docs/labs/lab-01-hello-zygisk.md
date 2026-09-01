---
title: "Lab 1: Hello, Zygisk"
description: "A log line from inside a named app's process, with the pid and uid printed, proving you ran inside that app and not zygote."
sidebar:
  order: 1
status: proven
---

**Chapter:** 4
**Module:** `modules/01-hello-zygisk/`

## Deliverable

A log line from inside a named app's process, with the pid and uid printed, proving you ran inside that app and not zygote.

## Prerequisites

Reference rig: Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5. Use a spare device, not your daily driver.

- Zygisk enabled in your root manager, and the manager reachable over `adb`.
- Android NDK installed, with `ndk-build` on `PATH` or `ANDROID_NDK_HOME` set.
- `zip` and `adb` available on the host.
- A target app you own or have written permission to instrument. Note its process name — that is what `nice_name` will print.
- A known way back from a bootloop. On KernelSU-Next, holding volume-down through boot disables all modules. Confirm you can do this *before* you flash anything. Chapter 3 covers recovery in full.

## Steps

1. **Read the module first.** Open `modules/01-hello-zygisk/jni/main.cpp`. You are looking for one thing: `preAppSpecialize` logs both `getuid()` and `args->uid`, and they are not the same kind of value. `getuid()` is what the process *is*. `args->uid` is what it is *about to become*. The rest of this lab is a test of that sentence.

2. **Build.**

   ```bash
   cd modules/01-hello-zygisk
   ./build.sh
   ```

   This runs `ndk-build` and packages `out/01-hello-zygisk.zip`. If it fails with `ndk-build not found`, set `ANDROID_NDK_HOME`.

3. **Check what you built.**

   ```bash
   unzip -l out/01-hello-zygisk.zip
   ```

   You want exactly `module.prop` at the zip root and `zygisk/arm64-v8a.so`. `module.prop` nested inside a folder is the most common packaging mistake and the manager will reject the zip without saying much.

4. **Flash it.** Install `out/01-hello-zygisk.zip` from your root manager. Confirm the manager now lists **ZygiskLab 01 - Hello Zygisk**.

5. **Scope it to your target app.** In the manager's Zygisk scope or denylist UI, enable the module for your target app only. Skipping this does not stop the lab working — it makes it print in every app on the device, which is noise you do not want.

6. **Reboot.** Not optional. Zygote loads modules once, at boot; a module installed into a running system is loaded into nothing. `adb reboot`.

7. **Watch the log.**

   ```bash
   adb logcat -s ZygiskLab
   ```

8. **Launch the target app** and read what appears. Expect three lines per launch: `onLoad`, `preAppSpecialize`, `postAppSpecialize`. The pid and uid values on your device will not match anyone else's — a pid is whatever the kernel handed out, and an app's uid is assigned at install time.

9. **Record the three lines.** Copy them somewhere. They are the deliverable, and they are also your baseline: every later lab that goes quiet is diagnosed by asking which of these three lines still appears.

## Verified result

This lab has been run on the reference rig — Pixel 6 Pro, Android 16, arm64,
KernelSU-Next 3.3.0 (`ksud 3.3.0 (uapi: 2)`), Zygisk Next 1.4.5
(`version=1.4.5 (836-b13d58a-release)`, versionCode 836). Two independent app
launches, captured verbatim from `adb logcat -s ZygiskLab`:

```text
09-01 02:13:16.413  8973  8973 I ZygiskLab: onLoad: module loaded into pid=8973
09-01 02:13:16.414  8973  8973 I ZygiskLab: preAppSpecialize: pid=8973 getuid=0 (current identity) args->uid=10215 (destination) nice_name=com.google.android.calculator
09-01 02:13:16.423  8973  8973 I ZygiskLab: postAppSpecialize: pid=8973 getuid=10215
```

```text
09-01 02:13:52.373  9203  9203 I ZygiskLab: onLoad: module loaded into pid=9203
09-01 02:13:52.374  9203  9203 I ZygiskLab: preAppSpecialize: pid=9203 getuid=0 (current identity) args->uid=10144 (destination) nice_name=com.google.android.deskclock
09-01 02:13:52.381  9203  9203 I ZygiskLab: postAppSpecialize: pid=9203 getuid=10144
```

Both launches show the shape the self-check asks for: one pid across all three
lines, `getuid=0` in `preAppSpecialize`, and `getuid` equal to the app's uid in
`postAppSpecialize`. The uid moved across the boundary, which is the proof —
and `args->uid` named the destination in advance, in both cases correctly. It
also makes the design point concrete: a module that logged only `args->uid`
before and `getuid()` after would have printed 10215 twice, and 10144 twice,
and demonstrated nothing about whether `postAppSpecialize` ran.

The elapsed time from `preAppSpecialize` to `postAppSpecialize` was about 9ms
in the first launch and 7ms in the second. Two samples on one device are not a
benchmark; do not plan around those numbers.

:::note
This is two launches on one rig, not a general guarantee. Different providers,
Android versions and devices are not covered by it, and neither is your device.
Run the lab yourself — the point is not that these lines exist somewhere, it is
that you can produce and read them on the hardware you will be working on.
:::

## Self-check

Seeing output is not the same as proving the thing. Work through all five.

1. **Same pid in all three lines?** Specialization is not a fork — it is the same process being constrained. Three different pids means you are looking at three different app launches interleaved in the log, not one process crossing the boundary.

2. **`getuid=0` in `preAppSpecialize`?** If it is anything else, you are not reading the pre-specialize line. That zero is the process still holding zygote's root identity.

3. **`getuid` in `postAppSpecialize` non-zero, and different from the pre line?** This is the actual proof. One value changed across the boundary, so something happened between the two callbacks. Without this change you have shown nothing.

4. **Does the `post` `getuid` equal the `pre` line's `args->uid`?** It should. But be clear about what that agreement buys you: `args->uid` was a forecast printed in advance, and the forecast being right confirms you read the specialization arguments correctly. It is corroboration, not the proof. The proof is step 3. If you had logged only `args->uid` before and `getuid()` after, you would have printed the same number twice and learned nothing about whether `postAppSpecialize` ran at all.

5. **Is `nice_name` your target app's process name, and nothing else appearing?** If other processes are printing too, your scope is not applied — the module works, the targeting does not. Fix the scope before moving on, or Lab 2's log will be unreadable.

If all five hold, you have run native code inside a named app's process and can prove it. If step 3 fails but steps 1 and 2 pass, `postAppSpecialize` is not firing — start with Chapter 4's failure catalogue, not with the code.

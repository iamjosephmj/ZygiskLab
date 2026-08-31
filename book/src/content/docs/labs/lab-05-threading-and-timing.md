---
title: "Lab 5: Threading and timing"
description: "Perform a main-thread-only action from an injected module, at a moment you chose, with proof of the thread you were on."
sidebar:
  order: 5
status: unverified
---

**Chapter:** 16
**Module:** `modules/05-main-thread/`

## Deliverable

Perform a main-thread-only action from an injected module, at a moment you chose, with proof of the thread you were on.

## Prerequisites

Reference rig: Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5. Use a spare device, not your daily driver.

- Labs 1-4 complete. In particular, Lab 2's deploy discipline is assumed here
  and not restated: you never `cp` over a live `.so`.
- Root over `adb`, with `su -M` available.
- `ANDROID_NDK_HOME` exported.
- Two apps you are willing to launch repeatedly: one to arm for, and at least
  one you will not. The unarmed one is the control.
- [Chapter 16](/ZygiskLab/book/postspecialize/16-threading-and-timing/) read,
  especially the argument for why the module waits for a moment the app reaches
  rather than sleeping for a plausible interval.

## Steps

### Part A - read it before you build it

1. **Read `modules/05-main-thread/jni/main.cpp`.** Find five things and be able
   to say why each is where it is:

   - `logThread()`, and why `gettid() == getpid()` answers the "which thread"
     question without trusting any framework promise.
   - The `clock_gettime(CLOCK_MONOTONIC, &armedAt)` immediately before the hook
     is installed, and why `CLOCK_MONOTONIC` rather than wall-clock time.
   - The `JNINativeMethod` array and the `hookJniNativeMethods()` call in
     `preAppSpecialize` - not `postAppSpecialize`.
   - The `orig_setArgV0 != nullptr` check straight after that call. This is the
     *only* signal of failure the API gives you; the header says so.
   - `my_setArgV0()` calling through to `orig_setArgV0(env, clazz, name)`
     unmodified. The module observes; it does not alter what the app sees.

2. **Predict the trace before you produce it.** Write down, now, what you expect
   `gettid() == getpid()` to report at each of the three call sites. Chapter 16
   argues one answer; committing to a prediction is what makes the log
   informative rather than confirmatory.

### Part B - build, arm, deploy

3. **Build.**

   ```bash
   cd modules/05-main-thread
   export ANDROID_NDK_HOME=/path/to/ndk
   ./build.sh              # -> out/05-main-thread.zip
   ```

4. **Install the zip once, through your root manager.** This creates
   `/data/adb/modules/zygisklab_mainthread/` with the labels your provider
   assigns. `deploy.sh` refuses to run before that directory exists.

5. **Write `target.txt`, and give it the label treatment.** Same contract as
   Labs 3 and 4 - an exact `strcmp` against `args->nice_name`, no prefix match:

   ```bash
   adb shell su -M -c "echo -n com.android.chrome > \
     /data/adb/modules/zygisklab_mainthread/target.txt"
   adb shell su -M -c "chmod 644 /data/adb/modules/zygisklab_mainthread/target.txt"
   adb shell su -M -c "restorecon /data/adb/modules/zygisklab_mainthread/target.txt \
     2>/dev/null || chcon --reference=/data/adb/modules/zygisklab_mainthread/module.prop \
     /data/adb/modules/zygisklab_mainthread/target.txt"
   ```

   Substitute your own target's process name. A `target.txt` the loader's domain
   cannot read is indistinguishable from one that is not there, and the module
   fails closed: it arms for nothing.

6. **Deploy and reboot.**

   ```bash
   ./deploy.sh             # or ./deploy.sh <serial>
   adb reboot
   ```

   `deploy.sh` pushes to a staging path and `mv`s, restores the SELinux label,
   verifies the hash, and exits non-zero on any of the three. It does not reboot
   for you, because zygote holds the old library mapped until it does. This is
   Chapter 7's discipline, and it is the whole reason this lab does not waste an
   afternoon on a module that is listed but never loads.

### Part C - the positive result

7. **Watch, then cold-launch the armed app.**

   ```bash
   adb logcat -c && adb logcat -s ZygiskLab
   ```

   Cold, not resumed from recents - `am force-stop` first if in doubt, so the
   process is genuinely forked again.

8. **Read the three thread-identity lines, in order.** You are looking for a
   `pid=`/`tid=` pair from `preAppSpecialize`, one from `postAppSpecialize`, and
   one from `main-thread action (Process.setArgV0 hook)`. Record all three tids
   verbatim. Do not summarise them yet.

9. **Read the two timing lines.** `postAppSpecialize` reports how long after
   arming it ran; the main-thread action reports the same for itself. The
   interval between them is the answer to "how much later did the moment I chose
   actually arrive". This number is yours - the module's README shows an
   illustrative trace, not a measurement, and nothing in this book has been run
   on the rig.

10. **Repeat the launch several times.** One sample tells you the hook can fire.
    A handful tells you whether it fires every time, and how much the interval
    moves between a cold boot and a settled device.

### Part D - the negative control

11. **Launch an app you did not arm for.** Expect exactly two lines: the
    `preAppSpecialize` thread line and the `not armed` line. Nothing after -
    `DLCLOSE_MODULE_LIBRARY` unloads the library before `postAppSpecialize`
    would have run, so `hookJniNativeMethods()` is never even called on that
    path.

    ```bash
    adb logcat -d -s ZygiskLab | grep 'main-thread action' | sort -u
    ```

    Every line in that output must belong to your armed process. If the control
    ever produces one, the arming check is broken, not the hook.

12. **Break the hook on purpose, once.** Change the method signature in
    `main.cpp` to something that cannot resolve - `"()V"` instead of
    `"(Ljava/lang/String;)V"` - then rebuild, deploy and reboot. Expect the
    `WARN` line saying the main-thread action will not run, the app launching
    normally regardless, and no `main-thread action` line ever appearing.

    This is the step that teaches the API's real failure mode: a null `fnPtr` and
    nothing else. Having watched the module catch it once, you will never again
    debug a silent hook by staring at `logcat` waiting for output that cannot
    come. Restore the signature, rebuild, deploy and reboot before continuing.

## Self-check

Six checks, in two groups. The first two say something ran. The last four say it
ran **on the main thread, at a moment you chose** - which is the deliverable, and
the half that is easy to skip.

**Did something run?**

1. **Did all three thread lines appear for the armed process?**
   `preAppSpecialize`, `postAppSpecialize`, and the `main-thread action` line. If
   the third is missing but the first two are present, check for the
   `could not resolve android.os.Process.setArgV0` warning - that distinguishes
   "the hook was never installed" from "the call site is not where the module
   assumed", and only one of those is your bug.

2. **Did the app behave normally?** The module passes `env`, `clazz` and `name`
   straight through to the real `setArgV0`, so the app should be unaffected. If
   the target misbehaves only when the module is armed, stop and investigate
   before trusting anything else in this lab.

**Did you prove where and when?**

3. **Does the `main-thread action` line report `gettid()==getpid() -> true`?**
   This is the crux. It is the module's evidence that the framework dispatched
   the hooked call on the process's original thread - the one Android treats as
   main - rather than on a binder thread or a runtime worker.

4. **Do all three lines report the same tid?** They should, and that is the
   point: the hook did not move you to a different thread, it moved you to a
   different *moment* on the same one. If the third tid differs from the first
   two, you have found something the chapter did not predict - which is a real
   result, and worth writing up rather than explaining away.

5. **Can you say what the earlier callbacks were missing, given they were on
   that same thread?** Being on the OS main thread is necessary and not
   sufficient. At `preAppSpecialize` and `postAppSpecialize` there is no prepared
   main `Looper`, no `ActivityThread`, no `Instrumentation` and no `Application`
   - so main-thread *work* is not yet possible there, even though the thread is
   right. If you cannot state that difference in your own words, the tid equality
   in check 3 is a number, not a proof.

6. **Is the moment yours, and can you name it?** The action ran inside
   `ActivityThread.handleBindApplication()`'s call to `Process.setArgV0()`, on
   the main `Looper`, before `Application` was created. You did not sleep, you
   did not poll, and you did not race the app - you waited on an event the app
   was always going to reach. If your answer to "when did it run" is a number of
   milliseconds rather than a named point in the app's startup, you have measured
   a delay instead of choosing a moment.

## What this lab does not settle

Three claims stay open, and an honest write-up says so:

- Whether `handleBindApplication()` calls `Process.setArgV0()` at that position
  on *your* build. A firing hook is strong evidence; it is not source.
- Whether the hook's thread is the same one the app's own main-thread code uses
  for everything after. The module does not cross-check against
  `Looper.getMainLooper().getThread()`, because that needs the Java-side
  machinery this design deliberately avoids.
- Whether `hookJniNativeMethods()` behaves as the header documents under Zygisk
  Next 1.4.5 specifically. Step 12 is the only part of this lab that tests it,
  and it tests one failure mode, once.

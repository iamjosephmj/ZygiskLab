---
title: "Lab 2: Deploying without bricking zygote"
description: "A deploy.sh that is safe by construction, plus a deliberate reproduction of the corruption so the reader has seen the crash signature once, on a spare device."
sidebar:
  order: 2
status: unverified
---

**Chapter:** 7
**Module:** `modules/01-hello-zygisk/`

## Deliverable

A deploy.sh that is safe by construction, plus a deliberate reproduction of the corruption so the reader has seen the crash signature once, on a spare device.

## Prerequisites

Reference rig: Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5. Use a spare device, not your daily driver.

- Lab 1 complete: `modules/01-hello-zygisk` installed, scoped to one target app, and printing its three lines under `adb logcat -s ZygiskLab`.
- Root over `adb`, with `su -M` available.
- A factory image downloaded and a rehearsed way into safe mode, per [Chapter 3](/ZygiskLab/book/foundations/03-rig-and-toolchain/).

:::danger
Part B of this lab deliberately corrupts a running zygote. Do it on a **spare
device only** — never a daily driver, never a phone holding anything you have
not backed up. The intended outcome is a per-app crash, but the same write can
take zygote itself down and leave you at the boot animation. If that happens,
you are in Chapter 3's recovery levers: safe mode, `ksud module disable`, a
recovery shell, or a reflash. Do not start Part B until you can perform at
least one of those from memory.
:::

## Steps

### Part A — deploy safely and read the script

1. **Rebuild with a visible change.** In `modules/01-hello-zygisk/jni/main.cpp`,
   change the `onLoad` log string to something you will recognise — append
   `build 2`, say. Then:

   ```bash
   cd modules/01-hello-zygisk
   ./build.sh
   ```

   You now have a `.so` that differs from the one on the device, and a log line
   that tells you which build actually ran. That distinction is the whole lab.

2. **Read `deploy.sh` before you run it.** Open
   `modules/01-hello-zygisk/deploy.sh`. Its header comment is Chapter 7 in
   miniature. Find the four decisions and be able to say why each is there:
   the `adb push` to `/data/local/tmp` rather than to the module directory;
   the `mv` rather than `cp`; the `su -M` rather than `su -c`; the
   `restorecon || chcon --reference` after the move.

3. **Deploy.**

   ```bash
   ./deploy.sh            # or ./deploy.sh <serial> with several devices attached
   ```

   The script prints the on-device hash and the local hash and stops. It does
   not reboot for you — deliberately, because comparing those two lines is a
   step you should perform with your own eyes rather than delegate.

4. **Compare the hashes.** They must match. If they differ, the deploy did not
   land and nothing you observe afterwards is about your code. Check the module
   `id`, the destination file name (`arm64-v8a.so`, the ABI, not the library
   name), and whether the `su -M` command reported an error.

5. **Check the label.**

   ```bash
   adb shell su -M -c 'ls -Z /data/adb/modules/zygisklab_hello/zygisk/'
   ```

   Record what you see. Compare it against the label on `module.prop` in the
   same module, which the installer created and you did not touch. If your `.so`
   is labelled like a `/data/local/tmp` file, `restorecon` did not take — and
   you are about to meet the "listed but silent" failure.

6. **Test without rebooting, and expect nothing to change.**

   ```bash
   adb logcat -c && adb logcat -s ZygiskLab
   ```

   Launch the target app. You should see the **old** `onLoad` string, not
   `build 2`. This is the correct and boring outcome, and it is worth seeing
   once: a perfect deploy changes nothing until the loader runs again.

7. **Reboot, then confirm.**

   ```bash
   adb reboot
   ```

   Wait for boot, clear the log, launch the app again. Now `build 2` appears.
   You have a deploy path whose failure modes are all loud.

### Part B — reproduce the corruption, once

Spare device. Re-read the warning above. Everything from here is intended to
break.

8. **Establish a clean baseline.** Confirm the target app launches and prints
   its three `ZygiskLab` lines, and confirm at least one *other* app — one your
   module is not scoped to — launches normally. Note both. They are the control.

9. **Make a third build.** Change the `onLoad` string again (`build 3`) and add
   a few lines of code to `postAppSpecialize` — anything that shifts the
   library's layout. A build that differs only trivially may not relocate
   enough to fault. Rebuild.

10. **Deploy it the wrong way, on purpose.**

    ```bash
    adb push libs/arm64-v8a/libzygisklab.so /data/local/tmp/bad.so
    adb shell su -M -c 'cp /data/local/tmp/bad.so \
      /data/adb/modules/zygisklab_hello/zygisk/arm64-v8a.so'
    ```

    Do **not** reboot. The `cp` has just rewritten the inode that the running
    zygote holds mapped.

11. **Launch the armed app.** Expect it to die immediately — SIGSEGV during app
    specialization. Capture the evidence while it is fresh:

    ```bash
    adb logcat -b crash
    adb shell su -M -c 'ls -lt /data/tombstones | head'
    ```

    Read the backtrace. The interesting part is *where the fault surfaces*: in
    the provider's library, the code that dlopens and calls into your module —
    not in a frame you wrote. Write down what you see. This is the signature you
    are here to recognise.

12. **Launch the unarmed control app.** On the rig it survives, because your
    module returns early in that process and may never execute the damaged
    pages. Seeing one app die and the other live, from a single cause, is the
    point of Part B: that asymmetry is what makes the failure look app-specific
    when it is not.

13. **Run the diagnostic before you theorise.**

    ```bash
    adb shell su -M -c 'md5sum /data/adb/modules/zygisklab_hello/zygisk/arm64-v8a.so'
    md5sum libs/arm64-v8a/libzygisklab.so
    ```

    They match. The file is exactly what you built. Sit with that for a moment:
    a crashing app, a correct file, and no bug in your source. Everything the
    crash appears to be telling you is false, and this one command is what says
    so.

14. **Recover.** Reboot. The new mapping is made from the new file and the crash
    is gone.

    ```bash
    adb reboot
    ```

    If the device does not come back, go to Chapter 3: safe mode, then
    `adb shell su -c 'ksud module disable zygisklab_hello'`, then a recovery
    shell, then reflash. That path is why this was a spare device.

15. **Redeploy properly and leave the device clean.** Run `./deploy.sh`, compare
    hashes, reboot, confirm `build 3` prints and the app is healthy.

## Self-check

Six checks. The first four say you deployed safely; the last two say you
understand *why* it was safe, which is the difference between a safe deploy and
a lucky one.

1. **Did the hashes match before you rebooted, and did the new build's log line
   appear only after?** Both halves matter. A matching hash before reboot with
   old behaviour after reboot means the file landed but the loader did not pick
   it up — a different problem from a failed deploy, and one you can only
   distinguish because you checked both.

2. **Is the `.so`'s SELinux label the same as the module's other files?** If you
   never ran `ls -Z`, you have not verified this — you have assumed it. A
   mislabelled module loads on some policies and silently does not on others,
   so a working device is not evidence the label is right.

3. **Did you see the crash yourself, with the fault surfacing in the provider's
   library rather than in your own frames?** Reading step 11 is not the same as
   having read a tombstone. The whole value of Part B is that you will recognise
   this signature at speed six months from now.

4. **Did the unarmed control app survive while the armed one died?** If both
   died, your module does work in every process — re-check your scope, and note
   that the corruption reached further than expected. If both survived, the
   `cp` did not disturb pages that process executes; make a larger code change
   and try again rather than concluding the hazard is not real.

5. **Can you state, without looking, what `mv` does that `cp` does not?** The
   answer is one sentence about inodes: `cp` rewrites the existing inode that
   zygote has mapped; `mv` is `rename`, which rebinds the name to a new inode
   and leaves the old one alive for anything still mapping it. If you can only
   say "`mv` is safer", you have memorised the ritual and not the mechanism, and
   the ritual will not transfer when you meet this hazard on a different file
   type in Part IV.

6. **Can you name a way to break this that is not `cp`?** There are several:
   `adb push` straight to the module directory, shell redirection into the
   path, `dd of=`, an on-device editor saving in place. If your mental rule is
   "do not use `cp`" you will eventually use one of the others. The rule is
   *never write to a currently mapped artifact* — and the test of whether you
   hold that rule is whether you can generate the counterexamples yourself.

Once all six hold, `deploy.sh` is the only way you install a module for the rest
of this book, and any crash that follows a deploy gets a `md5sum` before it gets
a theory.

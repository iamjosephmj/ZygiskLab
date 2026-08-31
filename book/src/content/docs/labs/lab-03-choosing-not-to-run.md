---
title: "Lab 3: Choosing not to run"
description: "A module armed for one package, with a measurement showing the unarmed path's cost on other app launches."
sidebar:
  order: 3
status: unverified
---

**Chapter:** 11
**Module:** `modules/03-armed-once/`

## Deliverable

A module armed for one package, with a measurement showing the unarmed path's cost on other app launches.

## Prerequisites

Reference rig: Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5. Use a spare device, not your daily driver.

- Lab 1 and Lab 2 complete: you have a working build toolchain, and
  `deploy.sh` is the only way you install a `.so`.
- Root over `adb`, with `su -M` available.
- `ANDROID_NDK_HOME` exported.
- Two apps you are willing to launch repeatedly: one to arm for, and at least
  one — ideally several — you will not arm for. The unarmed ones are the control
  and they matter as much as the target.

## Steps

### Part A — build and arm

1. **Read `jni/main.cpp` before you build it.** Find these five things and be
   able to say why each is where it is: the `clock_gettime` call as the *first*
   statement of `preAppSpecialize`; the `readTarget` call before the branch; the
   `target[0] != '\0' &&` short-circuit in the `armed` expression; the
   `setOption(zygisk::DLCLOSE_MODULE_LIBRARY)` as the first thing on the unarmed
   path; and the `ReleaseStringUTFChars` on *both* exits. Chapter 11 argues each
   of them.

2. **Build.**

   ```bash
   cd modules/03-armed-once
   export ANDROID_NDK_HOME=/path/to/ndk
   ./build.sh              # -> out/03-armed-once.zip
   ```

3. **Install the zip once, through your root manager.** This is what creates
   `/data/adb/modules/zygisklab_armed/` with the labels this provider assigns.
   `deploy.sh` refuses to run before that directory exists, and says so, rather
   than silently creating a module the loader will not accept.

4. **Write `target.txt` into the module directory.** The module reads its target
   at runtime from a file next to `module.prop`:

   ```bash
   adb shell su -M -c "echo -n com.android.chrome > \
     /data/adb/modules/zygisklab_armed/target.txt"
   ```

   Substitute your own target's process name. `-n` is not load-bearing —
   `main.cpp` trims a trailing newline, `\r` and spaces itself — but be exact
   about the name, because the match is `strcmp`.

5. **Give `target.txt` the same treatment as the `.so`.** Chapter 7's deploy
   discipline applies to *every* file you put in the module directory, not just
   the library. A file created by shell redirection gets the mode and the
   SELinux label that context hands it, not the label the module directory's
   files are supposed to carry, and a file the loader's domain cannot read is
   indistinguishable from a file that is not there.

   ```bash
   adb shell su -M -c "chmod 644 /data/adb/modules/zygisklab_armed/target.txt"
   adb shell su -M -c "restorecon /data/adb/modules/zygisklab_armed/target.txt \
     2>/dev/null || chcon --reference=/data/adb/modules/zygisklab_armed/module.prop \
     /data/adb/modules/zygisklab_armed/target.txt"
   adb shell su -M -c "ls -Z /data/adb/modules/zygisklab_armed/"
   ```

   `module.prop` is the reference label, for the reason Lab 2 established: the
   manager wrote it in place at install time. `target.txt` should end up
   matching it. If it does not, the symptom you will get later is
   `could not open target.txt in module dir; module will not arm` in `logcat` —
   which is at least a loud failure, because the module fails closed.

6. **Deploy the current build and reboot.**

   ```bash
   ./deploy.sh             # or ./deploy.sh <serial>
   adb reboot
   ```

   `deploy.sh` checks the destination, the hash and the label, and exits
   non-zero on any of them. It does not reboot for you; zygote holds the old
   library mapped until it does.

### Part B — the positive result

7. **Watch, then launch the armed app.**

   ```bash
   adb logcat -c && adb logcat -s ZygiskLab
   ```

   Expect two lines for that process: a `preAppSpecialize` line reporting
   `ARMED`, `getuid=0` and the destination `args->uid`, and a
   `postAppSpecialize` line reporting the same pid with `getuid` now equal to
   that destination uid. That pair is the same boundary proof as Lab 1, and it
   is what says the module armed for the right process and survived
   specialization.

### Part C — the measurement

The measurement is the heart of this lab, and one sample is not a measurement.

8. **Collect many unarmed launches.** Launch — cold, not resumed from recents —
   a range of apps you did *not* arm for. Ten distinct launches is a floor;
   twenty is better, and you want a mix of heavy apps and small ones. Force-stop
   between launches so the process is genuinely forked again rather than brought
   forward:

   ```bash
   adb shell am force-stop <package>
   adb shell monkey -p <package> -c android.intent.category.LAUNCHER 1
   ```

9. **Pull the numbers out.**

   ```bash
   adb logcat -d -s ZygiskLab | grep 'not armed' \
     | sed 's/.*cost=\([0-9]*\)us.*/\1/' | sort -n
   ```

   You now have a sorted list of per-launch unarmed-path costs in microseconds.
   Record the whole list, not a mean. Report the median and the range; the
   spread is the finding, because it tells you how much of what you are seeing
   is your module and how much is the device.

10. **Take a second pass after the device has settled.** The first launches
    after a reboot are not like the tenth: page cache is cold, `target.txt` has
    not been read yet, and the CPU governor is doing its own thing. Collect a
    second batch some minutes later and compare the two distributions. If they
    differ substantially, say so in your notes — that difference is a real
    property of the measurement, not an error in it.

11. **Write down what the number does not include.** Before you quote it to
    anyone, including yourself in six months, state the boundary explicitly:
    this delta covers `GetStringUTFChars`, `getModuleDir`, `openat` + `read` +
    `close` on `target.txt`, one `strcmp`, and one `setOption`. It does **not**
    include the provider finding and mapping the `.so`, the C++ runtime's static
    initialisers, `onLoad`, or anything the provider does around the callback.
    The module cannot see any of that from inside itself. It is a lower bound on
    your module's per-launch cost, not the total injection overhead.

### Part D — the negative control

12. **Prove the unarmed processes did not arm.** For each unarmed launch, there
    should be exactly *one* `ZygiskLab` line: the `not armed` line. No
    `postAppSpecialize` line follows it, because `DLCLOSE_MODULE_LIBRARY` took
    the library out of that process before `postAppSpecialize` would have run.
    Count them:

    ```bash
    adb logcat -d -s ZygiskLab | grep -c 'not armed'
    adb logcat -d -s ZygiskLab | grep 'postAppSpecialize' | sort -u
    ```

    Every `postAppSpecialize` line in that output should name your armed
    process and nothing else.

13. **Retarget without rebuilding, to prove the config is live.** Change
    `target.txt` to a different package, redo the label step, reboot, and
    confirm the arming moved. This is what reading configuration at runtime
    bought you, and it is worth exercising once so the tradeoff Chapter 11
    describes is concrete rather than theoretical.

14. **Break it on purpose, once.** Move `target.txt` aside and reboot:

    ```bash
    adb shell su -M -c "mv /data/adb/modules/zygisklab_armed/target.txt \
      /data/adb/modules/zygisklab_armed/target.txt.bak"
    ```

    Expect a `could not open target.txt in module dir; module will not arm`
    warning and no armed process anywhere, including your target. A missing
    config makes the module arm for *nothing*. Confirm that direction yourself;
    a module that fails the other way arms for everything, and you want to have
    seen which one this is. Restore the file and reboot when you are done.

## Self-check

Seven checks, in two groups. The first three say your module armed. The last four
say it *only* armed there — which is the actual deliverable, and the half that is
easy to skip.

**Did it arm?**

1. **Did the armed process produce both lines, with `getuid` changing from 0 to
   `args->uid` across the boundary?** One line without the other means the module
   loaded but something went wrong across specialization; go back to Chapter 11's
   ordering rules and to Lab 1's failure catalogue.

2. **Did you confirm `target.txt`'s label matches `module.prop`'s?** If you
   skipped step 5 and the module armed anyway, you got lucky with the label your
   shell happened to produce. Check it now, so you know whether it is correct or
   merely working.

3. **Can you retarget the module by editing one file and rebooting, with no
   rebuild?** If you rebuilt to change the target, you have a compile-time
   constant with extra steps and you have not exercised the design decision this
   lab is about.

**Did it stay out of everything else?**

4. **Do you have at least ten unarmed samples, from cold launches of more than
   one app, with a median and a range rather than a single number?** One sample
   is an anecdote. If your numbers are all identical you are probably re-reading
   the same cached launch — force-stop first.

5. **Can you state what the number excludes without looking at step 11?** If you
   quote this figure as "the cost of having this module installed", it is wrong
   by everything the provider does before your first statement runs.

6. **Is there exactly one `ZygiskLab` line per unarmed launch, and does no
   `postAppSpecialize` line name any process but your target?** This is the
   negative control and it carries as much weight as the positive result. A
   `postAppSpecialize` line from an unarmed process means the `dlclose` did not
   take effect — check that `setOption` really is on that path
   ([Chapter 10](/ZygiskLab/book/prespecialize/10-setoption-and-flags/) covers
   the preconditions), and see
   [Chapter 20](/ZygiskLab/book/companion/20-where-it-breaks/) for what a given
   provider does and does not honour.

7. **Did you watch it fail closed?** Step 14. Until you have seen the missing
   config produce nothing rather than everything, you are trusting one `&&` in a
   source file you read once.

If checks 1 through 3 hold and 4 through 7 do not, you have a module that works
and an unproven claim about it. That combination is the normal state of a Zygisk
module in the wild, and the rest of this book gets steadily harder to debug the
longer you leave it that way.

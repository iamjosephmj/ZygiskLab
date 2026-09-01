---
title: "Lab 4: Hooking native symbols"
description: "Hook a libc call in one target app, log it, and show a correct non-hooked control process."
sidebar:
  order: 4
status: proven
---

**Chapter:** 14
**Module:** `modules/04-plt-hook/`

## Deliverable

Hook a libc call in one target app, log it, and show a correct non-hooked control process.

The module hooks `open` and registers it against `libandroid_runtime.so`. If you
read an earlier edition, it hooked `openat` against `libc.so`; that could not
work and this lab is what proved it. Chapter 14's
["The mistake this chapter made"](/ZygiskLab/book/postspecialize/14-hooking-native-symbols/#the-mistake-this-chapter-made)
has the reasoning — read it before Part A.

## Prerequisites

Reference rig: Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5. Use a spare device, not your daily driver.

- Labs 1-3 complete: a working toolchain, and `deploy.sh` as the only way you
  install a `.so`.
- Root over `adb`, with `su -M` available.
- `ANDROID_NDK_HOME` exported.
- **Two apps.** One you will arm for — the target — and at least one you will
  not — the control. The control is not optional and it is not a formality; it
  is what turns "I saw output" into "the hook is where I put it".

## Steps

### Part A - read, build, arm

1. **Read `jni/main.cpp` before you build it.** Find five things and be able to
   say why each is where it is: `findTargetLib()` matching on `(dev, inode)`
   rather than on a path, and matching `/libandroid_runtime.so` rather than
   `/libc.so`; `strncpy` into `armedProcessName` happening *before*
   `pltHookCommit()`; the `bool committed` result being logged in both
   directions; the `thread_local int reentryDepth` guard around the `LOGI`
   calls; and the final `return orig_open(path, flags, mode);` passing
   every argument through untouched. Chapter 14 argues each one.

   Read the source comment above `my_open` too. It states why `openat` against
   `libc.so` cannot be hooked at all — libc defines the symbol rather than
   importing it — which is the single most useful thing in this lab.

   **Check the target yourself.** Before trusting the module's choice, confirm
   the import exists:

   ```bash
   adb pull /system/lib64/libandroid_runtime.so .
   llvm-readelf -r libandroid_runtime.so | grep ' open$'
   llvm-readelf -r libandroid_runtime.so | grep ' openat$'
   ```

   One relocation for `open`, none for `openat`. That two-line check is the
   whole of what the earlier edition skipped.

2. **Build.**

   ```bash
   cd modules/04-plt-hook
   export ANDROID_NDK_HOME=/path/to/ndk
   ./build.sh              # -> out/04-plt-hook.zip
   ```

3. **Install the zip once, through your root manager.** This is what creates
   `/data/adb/modules/zygisklab_plthook/` with the labels this provider assigns.
   `deploy.sh` refuses to run before that directory exists.

4. **Write `target.txt`.** Same contract as Lab 3: one line, no trailing
   newline, matched exactly against `args->nice_name`.

   ```bash
   adb shell su -M -c "echo -n <target.package> > \
     /data/adb/modules/zygisklab_plthook/target.txt"
   ```

   If the file is missing, empty, unreadable or matches nothing, the module arms
   for nothing and never touches `open()` anywhere. It fails closed.

5. **Check the label on `target.txt`** against `module.prop`, which the manager
   wrote in place. A mismatch shows up later as
   `could not open target.txt in module dir; module will not arm`.

6. **Deploy and reboot.**

   ```bash
   ./deploy.sh             # or ./deploy.sh <serial>
   adb reboot
   ```

   `deploy.sh` pushes to a staging path and `mv`s, sets the SELinux label explicitly
   and verifies it, and hashes both sides. It does not reboot for you: zygote
   still has the old library mapped until it does. Chapter 7 has the reasoning;
   never `cp` over a live `.so`.

### Part B - the positive result

7. **Watch the log, then cold-launch the target.**

   ```bash
   adb logcat -c && adb logcat -s ZygiskLab
   ```

   ```bash
   adb shell am force-stop <target.package>
   adb shell monkey -p <target.package> -c android.intent.category.LAUNCHER 1
   ```

   Force-stop matters. An app resumed from recents is not forked again, so
   `preAppSpecialize` never runs and you will see nothing.

8. **Read the arming line first, not the hook lines.** You are looking for one
   of these two, and which one you got decides everything after it:

   ```text
   preAppSpecialize: pid=... proc=... ARMED, open() hook committed
   preAppSpecialize: pid=... proc=... ARMED, but pltHookCommit() FAILED - open() is NOT hooked in this process
   ```

   A third possibility is
   `could not locate libandroid_runtime.so in /proc/self/maps`, in which case
   neither `pltHookRegister` nor `pltHookCommit` was called at all.

   If you get the `FAILED` variant, it means the ELF you targeted has no PLT
   entry for the symbol — go back to step 1b, not to the lifecycle.

9. **Then read the hook lines.** Expect `open:` lines carrying the pid, the
   armed process name, a path and the flags, numbered against the cap — and,
   once the app has done any real work, a single line saying further calls are
   suppressed. The first paths on a cold launch are the app's own APK and its
   splits under `/data/app/`, because that is what `libandroid_runtime.so`
   opens during startup. If they look like something else entirely, confirm the
   pid on the `open:` lines matches the pid on the arming line.

10. **Confirm the cap behaved.** Count them:

    ```bash
    adb logcat -d -s ZygiskLab | grep -c 'open: pid='
    adb logcat -d -s ZygiskLab | grep 'further calls suppressed'
    ```

    Twenty logged lines and one suppression line is the designed outcome. Fewer
    than twenty with no suppression line means the app simply did not open that
    many files yet — exercise it and look again.

11. **Confirm the app is undamaged.** Use it for a minute. The replacement
    returns the original's result unchanged, so the app should behave exactly as
    it does with the module absent. Anything else is a bug in your build, not an
    expected cost of hooking.

### Part C - the control

This is the deliverable, not an appendix to it.

12. **Cold-launch an app you did *not* arm for**, with the same logcat running.

    ```bash
    adb shell am force-stop <control.package>
    adb shell monkey -p <control.package> -c android.intent.category.LAUNCHER 1
    ```

13. **Expect exactly one line for that process**, of this shape:

    ```text
    preAppSpecialize: pid=... nice_name=<control> not armed (target=<target>), no hook installed
    ```

    No `open:` lines. No `postAppSpecialize` line either — as in Lab 3,
    `setOption(DLCLOSE_MODULE_LIBRARY)` unloads the library before that
    callback would run. On this path `pltHookRegister` and `pltHookCommit` are
    never called.

14. **Prove it by pid, not by eye.** Take the control's pid and assert nothing
    hooked-looking carries it:

    ```bash
    adb logcat -d -s ZygiskLab | grep 'open:' | grep -o 'pid=[0-9]*' | sort -u
    ```

    Every pid in that output must be the target's. One control pid appearing
    there means the arming logic leaked, not the hook — recheck `target.txt` and
    the exact-match comparison before suspecting the PLT.

15. **Run several controls, including a heavy one.** One control is an anecdote.
    Use a browser or a maps app — something that opens a great many files — so
    that if the hook were process-wide you could not possibly miss it.

16. **Retarget, to prove the scope moves.** Point `target.txt` at what was your
    control, restore the label, reboot, and rerun Parts B and C with the two
    apps swapped. The `open:` lines should follow the configuration. This is
    the cleanest evidence that the scoping is real and not a coincidence of
    which app you happened to launch first.

## Verified result

Run on the reference rig — Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0,
Zygisk Next 1.4.5 — with `com.google.android.deskclock` as the target. One
device, one provider version; these are observed lines, not a specification.

Armed process, cold launch:

```text
preAppSpecialize: pid=3763 proc=com.google.android.deskclock ARMED, open() hook committed
open: pid=3763 proc=com.google.android.deskclock path=/data/app/~~uUs7tT7uUtccf4foUMb4mw==/com.google.android.deskclock-tNvvC8JOu_Q4svF-dzAusA==/base.apk flags=0x80000 [1/20 logged]
open: pid=3763 proc=com.google.android.deskclock path=/data/app/~~uUs7tT7uUtccf4foUMb4mw==/com.google.android.deskclock-tNvvC8JOu_Q4svF-dzAusA==/split_config.xxxhdpi.apk flags=0x80000 [2/20 logged]
```

Negative control, an unarmed app launched with the same logcat running:

```text
not armed (target=com.google.android.deskclock), no hook installed
```

and zero `open:` lines for that process.

For contrast, this is what the earlier `openat`-against-`libc.so` module produced
in every armed process, which is why the module was rewritten:

```text
preAppSpecialize: ARMED, but pltHookCommit() FAILED - openat() is NOT hooked in this process
```

## Self-check

Two groups. The first four say the hook fired. The last four say it fired *only*
where you installed it, which is the actual deliverable and the half that is easy
to skip.

**Did it hook?**

1. **Did you see `ARMED, open() hook committed`?** Not `ARMED` alone — the
   commit result is a separate fact and the module logs it separately. If you
   saw the `FAILED` variant, `open()` is not hooked and nothing below applies;
   go to Chapter 14's decision tree at step 3.

2. **Did `open:` lines appear with the target's pid?** Matching pids are what
   connect the hook lines to the arming line. Lines with a pid you cannot
   account for mean you are reading a different process's output.

3. **Did the suppression line appear after twenty logged calls?** If it did, the
   cap and the atomic counter are working. If you got more than twenty
   `open:` lines, something is wrong with your build.

4. **Did the app behave normally?** The hook returns the original's result
   unchanged. A target that misbehaves only with the module installed means your
   replacement is not the pass-through you think it is.

**Was it scoped?**

5. **Did every control launch produce exactly one line, and was it the
   `not armed` line?** Two lines, or a `postAppSpecialize` line, means the
   unload did not happen where you expected.

6. **Is the set of pids on `open:` lines exactly `{target}`?** You checked
   this mechanically in step 14. Say the number out loud: how many distinct pids
   were there.

7. **Did you run a control that opens a lot of files?** A quiet control proves
   less than a busy one. If your only control was a small app, you have not
   really tested for leakage.

8. **Did retargeting move the hook?** If `open:` lines followed `target.txt`
   to the other app and the first app went silent, the scoping is
   configuration-driven and demonstrated. If they did not move, you are looking at something
   other than what you think.

**What you have not proved.** You have proved the hook sees the calls it sees.
You have not proved it sees *every* file open in the target — Chapter 14's
"What a PLT hook cannot reach" lists the calls that are invisible by
construction, and an app that opens files through a raw syscall or a statically
linked copy will not appear in your log no matter how correct your hook is. Do
not report this lab as "logged every file the app opened".

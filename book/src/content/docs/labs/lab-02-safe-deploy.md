---
title: "Lab 2: Deploying without bricking zygote"
description: "A deploy.sh that is safe by construction, plus a deliberate reproduction of the corruption so the reader has seen the crash signature once, on a spare device."
sidebar:
  order: 2
status: proven
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
   miniature. Find these six decisions and be able to say why each is there:
   the `adb push` to `/data/local/tmp` rather than to the module directory;
   the `mv` rather than `cp`; the `sush` helper's `su -M` rather than plain
   `su -c`; the check that `$DEST_DIR` exists *before* anything is staged;
   the explicit `chcon u:object_r:system_lib_file:s0` after the move, followed
   by reading the label back with `ls -Z` and failing if it does not match; the
   fact that every root step is its own `su -M -c` invocation rather than a
   chain; and the capture-and-compare of both md5 hashes instead of printing
   them for you to eyeball.

3. **Deploy.**

   ```bash
   ./deploy.sh            # or ./deploy.sh <serial> with several devices attached
   ```

   If any of the three checks fails — destination directory missing, hash
   mismatch, label mismatch — the script exits non-zero with an `error:` line
   explaining which one and why, instead of reporting success. If it
   succeeds, it prints the installed path, the confirmed md5 hash, and the
   confirmed SELinux label. It does not reboot for you: the checks above tell
   you the file landed correctly, but only a reboot makes zygote load it, and
   that step is still yours.

4. **Confirm the deploy actually reported success.** Because `deploy.sh` now
   asserts the hash match itself rather than just printing both sides, you no
   longer need to eyeball two hashes — but you should understand what would
   have stopped it. If the script had failed here, the cause would be one of:
   the module `id` in `module.prop` not matching what is actually installed
   on the device, the destination file name being wrong (`arm64-v8a.so`, the
   ABI, not the library name), or the `su -M` command itself erroring. Note
   the md5 the script printed; you will want it later.

5. **See what the label check verified.** `deploy.sh` set the `.so`'s label to
   `u:object_r:system_lib_file:s0` with `chcon`, read it back with `ls -Z`, and
   failed loudly if it did not match. Look at it yourself once, so you
   recognise it later without a script checking for you:

   ```bash
   adb shell su -M -c 'ls -Z /data/adb/modules/zygisklab_hello/zygisk/'
   ```

   Note that `module.prop` next to it carries a *different* label,
   `u:object_r:adb_data_file:s0`, and that is expected — see the verified
   result below and
   [Chapter 7](/ZygiskLab/book/load/07-deploying-safely/) for why the label is
   pinned explicitly rather than restored, and for what a wrong label does and
   does not cause.

6. **Test without rebooting, and expect nothing to change.**

   ```bash
   adb logcat -c && adb logcat -s ZygiskLab
   ```

   Launch the target app. You should see the **old** `onLoad` string, not
   `build 2`. This was observed on the rig — see the verified result below. This is the correct and boring outcome, and it is worth seeing
   once: a perfect deploy changes nothing until the loader runs again.

7. **Reboot, then confirm.**

   ```bash
   adb reboot
   ```

   Wait for boot, clear the log, launch the app again. Now `build 2` appears.
   You have a deploy path whose failure modes are all loud.

## Verified result

Part A was run on the reference rig: Pixel 6 Pro, Android 16, arm64,
KernelSU-Next 3.3.0, Zygisk Next 1.4.5. The build strings used were `BUILD-2`
and `BUILD-3` rather than `build 2` and `build 3`; nothing else differed.

Part B was subsequently run on the same rig, and is recorded at the end of this
section. Its spare-device warning stands unchanged and should be read as
written.

### The mapped-library claim: confirmed

1. `BUILD-2` was deployed by the stage-then-rename path and the device
   rebooted. Newly launched apps logged `BUILD-2`.
2. `BUILD-3` was built and deployed the same way — `adb push`, then `mv`,
   `chmod`, `chcon`, each as its own `su -M -c` invocation — and the hash and
   SELinux label were verified on disk. **No reboot.** Newly launched apps
   still logged `BUILD-2`.
3. The device was rebooted. Newly launched apps logged `BUILD-3`.

That is step 6 and step 7 of this lab, observed. A correct deploy, confirmed
correct on disk by hash, changed nothing until zygote restarted. The old
mapping is what runs, and there is no way to make a newly forked app pick up
the new library short of restarting zygote.

### The SELinux label claim: falsified

This lab and Chapter 7 previously told you that a mislabelled `.so` would be
silently refused by the loader. That was tested and it is wrong. The `.so` was
left carrying `u:object_r:adb_data_file:s0` and the device rebooted; the module
**loaded and ran normally**, producing 69 log lines across app launches,
including `onLoad`, `preAppSpecialize` and `postAppSpecialize`.

The accurate, narrower statement is that the Zygisk API header requires the
*module directory* to be readable by zygote — it names `system_file` context —
because of SELinux restrictions on the descriptor `Api::getModuleDir()` hands
over a socket. That is about directory access through that call, not about
whether the library loads. Module 01 does not call `getModuleDir()`, so this run
says nothing about that case. [Lab
3](/ZygiskLab/labs/lab-03-choosing-not-to-run/) has since settled it on this same
rig: a file inside the module directory carrying `u:object_r:adb_data_file:s0`
could not be read through the descriptor `getModuleDir()` returns, and reverting
the label restored the read. So the header's requirement holds for
module-directory *access*, and only for that; loading is unaffected.

One device, one provider, one version. This disproves a general claim; it does
not prove that no label ever matters.

### Three labels, measured

| Source | Label |
|---|---|
| `restorecon` under `/data/adb/modules` | `u:object_r:adb_data_file:s0` |
| Working modules' `zygisk/*.so` on the device | `u:object_r:system_lib_file:s0` |
| `ksud module install`, freshly staged `.so` | `u:object_r:system_file:s0` |

The middle row was checked across `zygiskcamera`, `zygisk_vector` and
`hma_oss_zygisk`. So `restorecon` is the wrong tool here, and so is
`chcon --reference=module.prop`, because `module.prop` in the live directory is
`adb_data_file` too. `deploy.sh` in all five modules has been corrected to set
`WANT_LABEL="u:object_r:system_lib_file:s0"` explicitly with `chcon` and fail if
the label does not read back, and was re-run on the device afterwards, reporting
a matching hash and the correct label.

### A chained root command failed

Chaining `mv` and `chmod` behind a single `su -M -c` produced
`chmod: Permission denied` immediately after the `mv` succeeded. The identical
`chmod`, issued seconds later as a separate invocation, worked. The cause was
not established. `deploy.sh` now runs each step as its own invocation.

### Part B: the corruption, reproduced

Part B was run on the same rig. A working module was installed and confirmed
running; a second build, whose log string was `CORRUPT-TEST`, was then written
over the live `.so` with `cp` while zygote had it mapped — the exact mistake
this lab is built around.

App spawns began dying immediately. Logcat carried
`Zygote  : Process 8716 exited due to signal 11 (Segmentation fault)` with a
tombstone written, and six such crash lines accumulated as apps kept being
launched. The tombstone header:

```text
Executable: /system/bin/app_process64
Cmdline: zygote64
pid: 8716, tid: 8716, name: main  >>> zygote64 <<<
uid: 0
esr: 0000000082000006 (Instruction Abort Exception 0x20)
signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x000000000002f53c (read)
    x17 0000002b2b678fa8
    pc  000000000002f53c
```

The crashing process is a freshly forked `zygote64` child, still uid 0 — it died
during specialization, before becoming the app. The Java frames say where:
`com.android.internal.os.Zygote.specializeAppProcess`, called from
`Zygote.childMain`, from `Zygote.forkSimpleApps`.

The native backtrace:

```text
#00 pc 000000000002f53c  <unknown>
#01 pc 00000000000fb130  /data/adb/modules/zygisksu/lib64/libzygisk.so
#02 pc 00000000000fb5d0  /data/adb/modules/zygisksu/lib64/libzygisk.so
#03 pc 00000000000fad18  /data/adb/modules/zygisksu/lib64/libzygisk.so
#04 pc 0007fc98          /data/adb/modules/zygisksu/lib64/libzygisk.so
```

Frame #00 is an address in no mapping at all; every named frame belongs to the
provider, and the tombstone additionally notes
`Unreadable libraries: /data/adb/modules/zygisksu/lib64/libzygisk.so`. Nothing
in the trace points at the module that actually caused it. That is step 11,
observed, and it is precisely why this failure reads as a provider bug or as
app-side defences rather than as your own deploy.

Step 13 is the one that matters most. Immediately after the `cp`, the on-disk
hash was `fc02597d052fbe735731a292e510b9c7` — an exact match for the newly built
local file — while app spawns were crashing. The one-step discriminator behaved
exactly as this lab and Chapter 7 claim: a matching hash on a crashing device
means the file is fine and the mapping is stale.

A reboot fully recovered the device: zero crashes afterwards, and the module
loaded and ran normally, logging its new `CORRUPT-TEST` string. The on-disk file
was therefore valid throughout and only the running mapping was damaged. No
recovery mode, no reflash, and `adbd` stayed responsive the whole time, so
control was never lost.

That recovery is reassuring, and it is also one run. The `:::danger` warning
above stands as written: this ran on a device whose owner had accepted the risk,
other devices and other module code can fail worse, and a module that corrupts
zygote during *boot* — rather than crashing app spawns after boot — is a
different and worse situation than the one recorded here.

The armed/unarmed asymmetry of step 12 was **not** measured on this run. Take it
as prediction still.

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

15. **Redeploy properly and leave the device clean.** Run `./deploy.sh` — it
    fails loudly if the destination, hash, or label check does not pass —
    then reboot and confirm `build 3` prints and the app is healthy.

## Self-check

Six checks. The first four say your deploy machinery worked and that you
understand what it checked; the last two say you understand *why* it was
safe, which is the difference between a safe deploy and a lucky one.

1. **Did `deploy.sh` exit successfully, and did the new build's log line
   appear only after you rebooted?** Both halves matter. A successful exit
   (hashes matched, label matched, destination existed) with old behaviour
   after reboot means the file landed but the loader did not pick it up — a
   different problem from a failed deploy, and one you can only distinguish
   because you checked both.

2. **Can you say why the label is set to a literal
   `u:object_r:system_lib_file:s0` rather than derived with `restorecon` or
   copied from `module.prop`?** If your answer is "because the script does
   it," you have trusted the check without understanding it. The real answer
   is measured: both of those derive `adb_data_file`, which is not the label
   any working module's library carries on this device. And you should be able
   to say what the rig showed a wrong label does *not* cause — it did not stop
   the module loading.

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

---
title: "Lab 7: A detection harness"
description: "A table of your own modules against the check matrix, with the specific line of your code responsible for each hit."
sidebar:
  order: 7
status: unverified
---

**Chapter:** 24
**Module:** `modules/07-detection-harness/`

## Deliverable

A table of your own modules against the check matrix, with the specific line of your code responsible for each hit.

## Prerequisites

Reference rig: Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5. Use a spare device, not your daily driver.

## Steps

### Part A — build, install, baseline

1. **Build the harness.** This is the one Gradle project in the repository, and
   the only lab you do not build with `ndk-build`. It has to be an app, because
   it has to inspect a process from inside a process an app actually gets —
   [Chapter 24](/ZygiskLab/book/footprint/24-detection-harness/) argues why.

   ```bash
   cd modules/07-detection-harness
   export JAVA_HOME=/home/joseph/.jdks/temurin-23.0.2   # or another JDK 17+
   ./gradlew assembleDebug
   ```

   Needs an Android SDK; `local.properties` (gitignored) points at it:
   `sdk.dir=/path/to/Android/Sdk`. Output is
   `app/build/outputs/apk/debug/app-debug.apk`.

2. **Read the checks before you trust them.** Nine classes under
   `app/src/main/java/dev/zygisklab/detectionharness/checks/`, plus
   `Signatures.kt`, `CheckResult.kt` and `Report.kt`. Be able to say, for each
   check, what file it reads and what would make it return `COULD_NOT_RUN`. Read
   `Signatures.kt` in particular: `INTERESTING_SUBSTRINGS` is the list
   `MapsCheck` and `MountInfoCheck` grep against, and it already contains
   `zygisklab`, which is why your own modules show up at all.

   Read two of the nine especially closely, because they behave unlike the rest:

   - `ThreadsCheck` enumerates `/proc/self/task` and prints every thread's
     `comm` name as evidence. It deliberately does not classify those names —
     it has no baseline of "normal" ART/Compose/framework thread names — so it
     returns `FOUND` only for a structural anomaly (a listed tid whose `comm`
     cannot be read) and otherwise `NOT_FOUND` with the inventory attached.
     Judging that inventory is your job, not the tool's.
   - `GotIntegrityCheck` reports `COULD_NOT_RUN` **even on a clean device**, and
     that is correct behaviour, not a bug in your build. Read its KDoc before
     you run anything. It can locate where `libc.so` is mapped from and flag an
     impostor mapping under that name as `FOUND`; it cannot resolve what any GOT
     slot actually holds, because that needs `dlsym` or an ELF
     relocation-table parser this pure-Kotlin app does not have. Its evidence
     names exactly what it did not evaluate.
     [Chapter 24](/ZygiskLab/book/footprint/24-detection-harness/) explains why
     that honest failure is the most instructive row in the report.

3. **Install and run it with nothing armed.**

   ```bash
   adb install -r app/build/outputs/apk/debug/app-debug.apk
   adb shell am start -n dev.zygisklab.detectionharness/.MainActivity
   ```

4. **Record the baseline.** Tap "Copy report" or "Share report" and paste the
   whole thing into your lab notes, unedited. Do not summarise it. Everything
   in this run is the device and the root provider, not your module — that is
   the boundary [Chapter 21](/ZygiskLab/book/footprint/21-your-footprint/)
   drew, and this is the run that tells you where it actually falls on your
   rig.

   The predicted shape of a clean baseline is **eight `NOT_FOUND` and one
   `COULD_NOT_RUN`** — the `COULD_NOT_RUN` being `GotIntegrityCheck`, by design.
   Any *other* `COULD_NOT_RUN` is a broken instrument and you must work out why
   before continuing: a check that could not run reads exactly like a clean
   process, and that is the failure mode this harness is built to refuse.

### Part B — one module at a time

Repeat this loop for modules 01, 03, 04, 05 and 06. One module armed at a time;
two at once and you cannot attribute anything.

5. **Arm the module at the harness.** For modules 03 through 06, the arming
   contract from [Chapter 11](/ZygiskLab/book/prespecialize/11-choosing-not-to-run/)
   points at the harness package instead of a real app:

   ```bash
   adb shell su -M -c "echo -n dev.zygisklab.detectionharness > \
     /data/adb/modules/<module-id>/target.txt"
   ```

   Module ids: `zygisklab_armed`, `zygisklab_plthook`, `zygisklab_mainthread`,
   `zygisklab_companion`. Module 01 (`zygisklab_hello`) has no arming file and
   loads into everything in the provider's scope, so there is nothing to point
   at it.

6. **Deploy with Chapter 7's discipline.** Never `cp` over a `.so` that zygote
   may have mapped. Use each module's `deploy.sh`, exactly as
   [Lab 2](/ZygiskLab/labs/lab-02-safe-deploy/) established, and read
   [Chapter 7](/ZygiskLab/book/load/07-deploying-safely/) again if you are
   tempted to skip it.

7. **Reboot, then launch the harness.** Reboot rather than force-stopping: the
   module is loaded at fork from zygote, and you want a process that was
   specialized with the module in place.

8. **Record the report,** the same way as the baseline, labelled with the module
   id, the date and the rig.

9. **Disarm before the next module.** Clear or repoint `target.txt`, reboot, and
   confirm the harness has returned to its baseline before arming the next one.
   A residual `FOUND` from the previous module will otherwise be attributed to
   the next.

10. **Run module 03 twice** — once armed at the harness, once armed at some
    other package. Same module, same build, and the prediction is that
    `MapsCheck` fires in the first case and not the second, because of one
    `setOption(zygisk::DLCLOSE_MODULE_LIBRARY)` in the `if (!armed)` branch.
    This pair is the clearest decision-to-trace demonstration in the lab.

### Part C — the table

11. **Build the deliverable.** One row per module, one column per check, and a
    third element in every non-empty cell: the file and line of your own code
    responsible. Something like:

    | Module | Check | Outcome | Your line |
    |---|---|---|---|
    | 01 | Memory maps | ? | `jni/Android.mk:4` — `LOCAL_MODULE := zygisklab` |
    | 03 (unarmed) | Memory maps | ? | `jni/main.cpp` — `setOption(DLCLOSE_MODULE_LIBRARY)` |

    The outcome column is yours to fill in from the device. The chapter's
    predictions are there to be checked, not copied.

12. **Write down every prediction that was wrong.** Chapter 21's table and
    Chapter 24's per-module predictions are claims made from mechanism, by the
    person who wrote the code, before anyone ran it. Where your device
    disagrees, the device wins. Those rows are the most valuable output of this
    lab; record them explicitly rather than quietly adjusting the table.

13. **Note what the harness cannot see.** Match this against the harness
    README's "What this harness does not measure" section and do not claim more
    than it does:

    - **Module 04's committed PLT hook is unmeasured, not absent.**
      `GotIntegrityCheck` is present, runs, and still cannot resolve a slot
      value; its `COULD_NOT_RUN` evidence is your citation. Do not read that
      row as "no hook found".
    - **Module 05's JNI replacement is unmeasured.** Nothing here compares a
      method's entry point against the class that declared it.
    - **`ThreadsCheck` reports, it does not classify.** A module-spawned thread
      with an ordinary-looking name appears in the inventory and is flagged by
      nothing; only your own reading of the list catches it. Diff the inventory
      against your baseline rather than trusting the outcome field.
    - **Nothing here times anything,** so every behavioural trace at the end of
      Chapter 21 — launch-time deltas, changed error paths, altered ordering —
      is outside the harness's reach entirely.
    - **Nothing here is attested.** Every check is code running on hardware you
      control.

    A row that says "unmeasured" is an honest row; a blank one is not.

## Self-check

Running the harness is the easy half. You have finished this lab when:

- Your baseline run is recorded in full, before any module was armed, and you
  can say which entries in it belong to the root provider or the device rather
  than to anything you wrote.
- You can state, in your own words, what `COULD_NOT_RUN` means for your
  conclusions — that it is a statement about the instrument and not about the
  process, and that it removes a row from your evidence rather than adding a
  clean one. Specifically: you can say why `GotIntegrityCheck` returns it on
  every run including the baseline, what it did evaluate, what it did not, and
  what a boolean-returning version of that check would have told you about
  module 04 instead. Any *other* `COULD_NOT_RUN` in any run is explained —
  which file, which denial, what you would have to change to make it run — and
  none of them is written up as a clean result.
- You can name which of your own modules' traces this harness genuinely cannot
  see, and separate them from the traces it looked for and did not find. At
  minimum: module 04's `openat` PLT hook (no slot resolution), module 05's JNI
  method replacement (no entry-point comparison), any module-spawned thread with
  an unremarkable name (`ThreadsCheck` reports but does not classify), and every
  behavioural or timing trace (nothing here times anything). Saying "the harness
  found nothing" about any of these is the specific error this lab exists to
  prevent.
- For **every** `FOUND` in your table, you can point at the specific line of
  your own code — or, where the trace is not yours, the specific provider
  behaviour — that produced it. Not "the module was loaded": the decision, and
  where you made it.
- For **every** `NOT_FOUND`, you can say which of the three reasons applies:
  the trace was never created, it was created and cleaned up before the check
  ran, or this harness cannot see that class of trace at all. Module 05's
  absent thread, module 03's closed `getModuleDir()` descriptor and module 04's
  PLT hook are one of each, and you should be able to say which is which
  without looking it up.
- You ran module 03 both armed and unarmed and can state the one line of code
  that accounts for the difference.
- You can name at least one prediction from Chapter 21 or Chapter 24 that your
  device contradicted, and say why the prediction was wrong.
- You extended `Signatures.kt` or `FilesystemProbeCheck`'s `PROBE_PATHS` if your
  rig proved them incomplete, and noted in your write-up that you did — the
  results are about a harness you tuned, and the next reader needs to know it.

If you can produce the table but not the third column, you have run a tool. The
third column is the lab.

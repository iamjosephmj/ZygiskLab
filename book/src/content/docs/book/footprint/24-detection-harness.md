---
title: "A detection harness"
description: "Lab 7: an Android app that runs the Chapter 22 checks on itself and scores Labs 1-6's modules against the results."
sidebar:
  order: 4
status: unverified
---

:::caution[Detection and measurement]
This chapter covers detection mechanisms and measurement on systems you
own or are authorised to assess. See [Rules of engagement](/ZygiskLab/book/foundations/02-rules-of-engagement/).
:::

Three chapters of Part VI have been arguments. [Chapter 21](/ZygiskLab/book/footprint/21-your-footprint/)
predicted, from mechanism, what each of your design decisions leaves behind.
[Chapter 22](/ZygiskLab/book/footprint/22-how-an-app-looks-for-you/) described the
code an app runs to look for it, and deferred every concrete path, property name
and line count to "the harness". [Chapter 23](/ZygiskLab/book/footprint/23-existing-answers-surveyed/)
ended each section by telling you to measure rather than trust the claim. This
chapter is where the deferrals come due. `modules/07-detection-harness/` is an
Android application that runs seven self-inspection checks against its own
process and hands you the evidence, and [Lab 7](/ZygiskLab/labs/lab-07-detection-harness/) is you pointing your own modules at
it and writing down what happens.

Everything below is written before the first run. The app compiles with
`./gradlew assembleDebug` and produces an installable debug APK; that has been
verified and nothing else has. Every expected result in this chapter is a
prediction derived from how `/proc/self/*`, `stat(2)` and Android's property
system are documented to behave — not a transcript. Some of those predictions
will be wrong, and the wrong ones are the most valuable thing the lab produces.

## Why it is an app, not a script

Every other module in this repository is a bare `.so` built with `ndk-build`.
This one is the only Gradle project anywhere in `modules/`, and the reason is not
convenience.

The question the harness answers is "what can an app see about itself?" An
answer only counts if it is gathered from inside a process that *is* an app: the
same uid, the same SELinux domain, the same mount namespace, the same set of
descriptors the framework hands a freshly specialized process — and, crucially,
a process the provider considered in scope and injected into. A shell script run
under `adb shell` or `su` has none of that. It inspects a process that the
module was never loaded into, at a privilege level no launched app has, in a
namespace that may not be the app's namespace at all. It would produce output,
and the output would be about something else.

[Chapter 22](/ZygiskLab/book/footprint/22-how-an-app-looks-for-you/) made the
symmetry explicit: a module lives inside the process, so the process can see it.
The harness is that symmetry turned into a program. It reads only its own
`/proc/self/` entries and world-readable device state. It touches no other app's
process, no other app's files, and no other app's data, and it makes no attempt
to defeat, hide from or evade anything it finds. Its manifest asks for no
permissions, and the module README treats a future check needing one as a
red flag worth documenting — because it would mean the harness had started
looking at something other than itself.

## The shape of a check

Seven checks, one class each, under
`app/src/main/java/dev/zygisklab/detectionharness/checks/`. `Report.kt` builds
the fixed list, runs it, and renders the results as the exact plain text the
"Copy report" and "Share report" buttons hand off — what is on screen is what
gets pasted, with nothing summarised away. The whole UI is a scrollable list of
cards, because the app has exactly one job.

The interesting design is not the checks. It is what a check is allowed to
return.

```kotlin
enum class Outcome { FOUND, NOT_FOUND, COULD_NOT_RUN }

data class CheckResult(
    val name: String,
    val outcome: Outcome,
    val evidence: List<String>,
    val description: String,
)

fun interface Check {
    fun run(): CheckResult
}
```

Two decisions are carrying weight here.

**Evidence, not a boolean.** A check that returns `true` has to be trusted. You
cannot argue with it, you cannot tell a real finding from a bad substring match,
and when it fires on a clean device you have no way to work out why. A check that
returns the matching `/proc/self/maps` line, the `readlink` target it followed,
the mount entry it flagged or the property value it read is a check you can
disagree with. Lab 7's deliverable — a line of *your* code behind every hit —
is only possible because each hit arrives carrying the raw text that produced it.

**Three states, not two.** `COULD_NOT_RUN` is a first-class outcome, not a
quietly-folded-in `NOT_FOUND`. A file the app could not read, a syscall SELinux
denied, an exception on an API level where the interface moved — none of those
tell you anything about whether the process is clean, and reporting them as
"nothing found" is how a harness lies to you. The failure mode is specific and
nasty: your evasion looks like it worked, when in fact your measuring instrument
broke. `Report.asText` counts the three separately in its summary line for
exactly this reason. When you write up Lab 7, a `COULD_NOT_RUN` is a result you
must chase down, not a row you may leave blank.

`FilesystemProbeCheck` takes the same idea one level deeper, into the evidence
itself. Rather than `java.io.File.exists()`, which collapses "denied" and "not
there" into the same `false`, it calls `android.system.Os.stat()` and reads
`errno`:

```kotlin
} catch (e: ErrnoException) {
    val label = when (e.errno) {
        OsConstants.ENOENT -> "not reachable (no such path)"
        OsConstants.EACCES, OsConstants.EPERM -> "permission denied (${e.message})"
        else -> "stat failed: errno=${e.errno} (${e.message})"
    }
    evidence.add("$path -> $label")
}
```

Per path, three answers: reachable, not reachable, permission denied. That third
one is a finding in its own right. `EACCES` on `/data/adb/modules` means
something is there and this process is not allowed to see it, which is a
different statement about the device than `ENOENT`, and the naive code cannot
tell them apart.

## The seven checks

| Check | Class | Reads |
|---|---|---|
| Memory maps | `MapsCheck` | `/proc/self/maps`, for module- or provider-shaped paths |
| Open descriptors | `OpenFdCheck` | `/proc/self/fd`, for targets outside the app sandbox |
| Mount table | `MountInfoCheck` | `/proc/self/mountinfo`, for overlay-on-system and manager-named entries |
| Loaded libraries | `LoadedLibrariesCheck` | the distinct `.so` paths in maps, against ordinary locations |
| Tracer | `TracerCheck` | `TracerPid` in `/proc/self/status` |
| Filesystem probes | `FilesystemProbeCheck` | `stat()` on a fixed list of root/provider paths |
| Properties | `PropertiesCheck` | ten properties via `getprop`, against stock-production values |

`MapsCheck` and `MountInfoCheck` grep against `INTERESTING_SUBSTRINGS` in
`Signatures.kt` — `magisk`, `zygisk`, `zygisklab`, `ksu`, `/data/adb/` and a
dozen more. That list is not a signature database in the antivirus sense; it is
short, readable, and there so you can extend it and watch your own module appear.
`LoadedLibrariesCheck` asks a different question from the same file: reduce maps
to the distinct set of `.so` paths, then flag any that do not begin with
`/system/`, `/apex/`, `/vendor/`, `/product/`, `/data/app/` or this app's own
`nativeLibraryDir`. The first check catches a name you recognise; the second
catches a library in a place libraries do not come from, whatever it is called.
Both matter, and Chapter 22's central tension — a short name list catches little
but rarely misfires, a broad structural rule catches more and flags ordinary
phones — is visible in the difference between them.

`PropertiesCheck` deserves one note, because it is the check most likely to
mislead you. It pairs each property with the value a stock, locked, production
build reports (`ro.secure=1`, `ro.debuggable=0`, `ro.build.tags=release-keys`,
`ro.boot.verifiedbootstate=green`, and so on) and reports the values, not a
verdict. `ro.debuggable=1` is entirely ordinary on a userdebug build.
`verifiedbootstate=orange` means the owner unlocked the bootloader, which is
neither root nor a module. A `FOUND` here means "something differs from the
stock-production expectation", and nothing stronger.

## The baseline comes first

Install the harness, arm nothing, launch it, and read the report before you do
anything else.

On an unmodified device — and on a rooted device with none of Labs 1–6 armed at
the harness — the prediction is that every check returns `NOT_FOUND`. No
interesting maps entries, no descriptors outside the sandbox, no overlay mounts,
no unusual libraries, `TracerPid: 0`, every probed path unreachable, every
property matching. `Report.asText` says so in as many words when nothing is
found, because a clean run is not a non-result.

That prediction is doing real work and it will probably not hold cleanly. Your
rig is rooted; KernelSU-Next and Zygisk Next are installed on it; the root
provider is not your module and does not disappear when your module is not armed.
[Chapter 21](/ZygiskLab/book/footprint/21-your-footprint/) drew that boundary in
prose — roughly half of what an app finds is not yours — and the baseline is
where you find out where the line actually falls on your device. Whatever the
unarmed run reports is the provider's footprint plus the device's. Everything
that appears *only* when you arm a module is yours.

Skip the baseline and no later number means anything. A `FOUND` on the mount
check with nothing to compare against tells you a mount exists; a `FOUND` that
was `NOT_FOUND` ten minutes ago tells you which of your decisions put it there.

:::caution
The harness measures the process it runs in. To measure a module against it, that
module's `target.txt` must name `dev.zygisklab.detectionharness` — the arming
contract from [Chapter 11](/ZygiskLab/book/prespecialize/11-choosing-not-to-run/),
pointed at the harness instead of a real app. Module 01 has no arming file and
loads into everything in the provider's scope, so it needs nothing pointed at it.
:::

## Measuring your own modules

Now the experiment Chapter 21 set up. Arm one module, reboot, run the harness,
record, disarm, repeat. One at a time — two modules armed at once and you cannot
attribute a finding to either.

The five modules made deliberately different choices, and the predictions below
follow from those choices rather than from any run.

**Module 01, `zygisklab_hello`.** No arming file, no `DLCLOSE_MODULE_LIBRARY`,
resident in every process the provider injects. `MapsCheck` and
`LoadedLibrariesCheck` should both fire, on a `libzygisklab.so` mapping whose
path is neither in the APK nor in `/system`. Chapter 6 measured that library at
roughly 232KB, essentially all statically linked C++ runtime, so expect a
substantial region rather than a page that could pass for something else. This is
the loudest module in the book and the simplest to explain.

**Module 03, `zygisklab_armed`.** Identical loading, opposite lifecycle. Armed at
the harness it behaves like module 01 for maps purposes. Armed at some other
package, it sets `DLCLOSE_MODULE_LIBRARY` on the unarmed path and the library
should be gone from the harness's maps entirely — the same code, invisible,
because of one `setOption` call. Run it both ways; the pair is the cleanest
demonstration in the lab of a decision producing or not producing a trace. Its
`getModuleDir()` descriptor is opened and `close`d within `readTarget`, so the
prediction for `OpenFdCheck` is nothing: a window, not a standing entry. If the
harness disagrees, that is a finding worth the whole lab.

**Module 04, `zygisklab_plthook`.** A committed PLT hook on `openat`, plus a
resident library on the armed path. Maps and libraries should fire as with 01.
The hook itself should not: none of the seven checks walks GOT slots and compares
addresses against the mapped range of the defining library. That is the honest
gap in this harness, and Chapter 21 named the hook as the trace that survives
everything else. Write it down as unmeasured rather than as absent.

**Module 05, `zygisklab_mainthread`.** Replaces `android.os.Process.setArgV0`
rather than spawning a thread. No check reads `/proc/self/task`, so the thread
you did not create is, again, a trace avoided rather than a trace measured. The
JNI replacement is the ART-level twin of module 04's mismatch and is equally
outside what these seven checks see.

**Module 06, `zygisklab_companion`.** The one with a real chance of moving
`OpenFdCheck`. Its companion socket is opened in `preAppSpecialize` and closed
when the exchange finishes. If the design holds, the harness's own code runs long
after the descriptor is gone and sees nothing. `FilesystemProbeCheck` is
independent of your module: it probes `/data/adb/modules` and friends, and what
it reports is a property of the provider and SELinux, not of module 06.

Two rows to be careful about. The provider's own mounts and files belong to the
provider, so a `FOUND` on the mount or filesystem checks is very likely not
yours — the baseline is what proves that. And `PropertiesCheck` should be
identical across all five modules, because no module in this book sets a
property. If a property moves when you arm a module, something happened that this
book did not predict, and that is worth more than any expected result on the
page.

## Reading a result back to a line

The deliverable is not the table. It is the third column of the table: the line
of your code responsible for each hit.

A `libzygisklab_plthook.so` mapping traces back through the provider's `dlopen`
to the fact that `Android.mk` names a `LOCAL_MODULE` and `build.sh` packages one
`.so` per ABI. A mapping's *size* traces back to `c++_static`. A single exported
symbol instead of your whole API traces back to `-fvisibility=hidden`. An absent
mapping on an unarmed launch traces to one line —
`api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);` — sitting in the `if (!armed)`
branch. An absent module-directory descriptor traces to `close(dirFd);` in
`readTarget`. Legible strings in the library's read-only data trace to `LOG_TAG`
and every `LOGI` you wrote. That mapping, decision to observation, is what makes
the lab worth doing; a table of outcomes without it is a scoreboard.

The same discipline applies to the checks that found nothing, and those are
harder. "Not found" has three possible explanations: the trace was never created,
the trace was created and cleaned up before the check ran, or the check cannot
see that class of trace at all. Modules 04 and 05 are the third case. Module 03's
descriptor is the second. Module 05's absent thread is the first. Being able to
say which, per row, is the difference between having run a harness and having
measured something.

## What this does not settle

Seven checks are not a detection product, and the gap is worth stating plainly
so you do not walk away over-confident.

No check walks relocations, so a committed hook is invisible to it. No check
reads `/proc/self/task`, `/proc/self/smaps`, or the dynamic symbol tables of
loaded objects. No check times anything, so the behavioural traces at the end of
Chapter 21 — launch-time deltas, changed error paths, altered ordering — are
entirely outside its reach, and those are the ones a large app's telemetry is
best placed to see and you are worst placed to measure. Nothing here is attested:
every one of these checks is code running on hardware you control, which is the
distinction [Chapter 22](/ZygiskLab/book/footprint/22-how-an-app-looks-for-you/)
drew between introspective and attested checks and the reason
[Chapter 25](/ZygiskLab/book/footprint/25-the-defensive-chapter/) does not end
with a technique.

And the substrings in `Signatures.kt`, the paths in `FilesystemProbeCheck`, and
the stock-production property values in `PropertiesCheck` are all lists someone
wrote down. Whether they match what Zygisk Next 1.4.5 and KernelSU-Next 3.3.0
actually leave on a Pixel 6 Pro running Android 16 is precisely the sort of claim
this book refuses to assert without a device. Extend the lists when your rig
proves them incomplete, and note in your lab write-up that you did — a harness
you tuned against your own device is a better instrument and a narrower one.

What you get from Lab 7 is smaller than "can my module be detected" and far more
useful: a dated, device-stamped record of which of your own decisions are visible
from inside the process, in evidence you can read and argue with. Chapter 21 gave
you a table of claims made by the person who wrote the code. This chapter replaces
it with results. Where the two disagree, the results win, and the disagreement is
what you learned.

# 07 — Detection Harness

**Lab 7.** A table of your own modules against the check matrix, with the
specific line of your code responsible for each hit.

## Why this is an app, not a script

Every other module in this repository is a bare `.so` built with
`ndk-build` — there is no other Gradle project anywhere in
`modules/`. This one is different on purpose. Chapter 24 asks the question
"what can an app see about itself?", and answering it means running the
checks *from inside a real app process* — the same UID, the same SELinux
domain, the same mount namespace an app actually gets, not the very
different world a root shell sees. A shell script run with `adb shell` or
`su` inspects a process that isn't there and a privilege level no launched
app has; it would measure the wrong thing and teach the wrong lesson. So
this lab ships as an installable Android application: it inspects itself,
as itself.

**This is a measurement tool, not a bypass tool.** Every check below
answers "what can an app see about itself?" and nothing else. It reads its
own `/proc/self/*` entries and other world-readable device state; it does
not touch another app's process, another app's files, or another app's
data, and it makes no attempt to defeat, disable, hide from, or evade
anything it finds. If a future check ever needed a permission beyond what's
already in the manifest, that would itself be worth flagging in this
README, because it would mean the harness had started looking at something
other than its own process.

## What it does

On launch, the app runs nine self-inspection checks against its own
process — the checks Chapter 22 describes an app performing when it looks
for an injected module — and shows a scrollable list of results: one card
per check, its outcome, and the exact evidence behind that outcome. A
"Copy report" / "Share report" pair turns the whole thing into plain text
for pasting into lab notes.

### The checks

Each check is one class under
`app/src/main/java/dev/zygisklab/detectionharness/checks/`, and each
returns evidence, not a boolean — see "Data model" below for why.

| Check | Class | Reads |
|---|---|---|
| Memory maps | `MapsCheck` | `/proc/self/maps`, for mapped-file paths that look module- or provider-related |
| Open file descriptors | `OpenFdCheck` | `/proc/self/fd`, for `readlink` targets outside the app sandbox |
| Mount table | `MountInfoCheck` | `/proc/self/mountinfo`, for overlay-on-system mounts and root-manager-named entries |
| Loaded native libraries | `LoadedLibrariesCheck` | the distinct `.so` paths in `/proc/self/maps`, for libraries outside the ordinary system/vendor/apex/own-app set |
| Tracer presence | `TracerCheck` | `TracerPid` in `/proc/self/status` |
| Filesystem probes | `FilesystemProbeCheck` | `stat()` on a fixed list of well-known root/provider paths |
| System properties | `PropertiesCheck` | a fixed list of properties read via `getprop`, compared against stock-production expectations |
| Threads | `ThreadsCheck` | `/proc/self/task`, reading each thread's name from `/proc/self/task/<tid>/comm` and reporting the inventory |
| GOT integrity (partial) | `GotIntegrityCheck` | `/proc/self/maps`, for where `libc.so` is mapped from — a necessary precondition for GOT-slot verification, not the slot values themselves |

`Report.kt` builds the fixed list of these nine checks, runs them, and
renders the results (`Report.asText`) as the exact text the copy/share
buttons hand off — what's on screen is what gets pasted, nothing
summarized away.

Two of the nine are worth calling out specifically:

- **`ThreadsCheck`** doesn't try to judge any thread name as suspicious —
  it has no stock-baseline list of "normal" ART/Compose/OkHttp thread
  names to compare against the way `PropertiesCheck` has stock property
  values, and guessing at one would mean quietly deciding what counts as
  anomalous on the app author's behalf. It reports the full thread
  inventory as evidence and leaves the judgment to the reader, who knows
  their own app's threads. The one thing it *will* flag as `FOUND` on its
  own is a purely structural anomaly: a `tid` still listed in
  `/proc/self/task` whose own `comm` file can't be read, which shouldn't
  happen for a live thread's own name.
- **`GotIntegrityCheck`** is a deliberately partial implementation of the
  strongest check the book describes (Chapter 22): verifying that a GOT
  slot for an imported libc function points into `libc.so` rather than
  into whatever redirected it. This app can locate *where* `libc.so` is
  mapped from `/proc/self/maps` and flag an impostor mapping named
  `libc.so` outside the expected system/APEX locations — that much is
  pure file I/O. It **cannot** resolve what any specific GOT slot actually
  holds, because that needs either `dlsym()` or a full ELF
  relocation-table parser, neither of which a pure-Kotlin app without
  JNI/NDK code has access to. So the check reports `COULD_NOT_RUN` for the
  core invariant even in the clean case — it never got to look at a slot
  value, so it cannot honestly claim the invariant held — and reserves
  `FOUND` for the one thing it can actually prove: an impostor `libc.so`
  mapping. See the class's own KDoc for the full explanation.

### Data model

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

A check that returns `true` teaches nothing — it has to be trusted. A
check that returns the matching `/proc/self/maps` line, the `readlink`
target it followed, or the mount entry it found lets the reader argue with
the result, which is the entire point of a lab about reading your own
footprint rather than being told a verdict. `COULD_NOT_RUN` is its own
outcome, not a silent `NOT_FOUND` — a probe denied by SELinux or a file
that doesn't exist on this API level says nothing about whether the
process is clean, and folding it into "not found" would misreport that.

The filesystem probe check goes one step further, distinguishing three
outcomes *per path* inside its evidence, not just at the check level:
**reachable**, **not reachable**, and **permission denied** — using
`android.system.Os.stat()` so a real `EACCES`/`EPERM` from the kernel can
be told apart from a real `ENOENT`, instead of collapsing both into
`java.io.File#exists() == false` the way naive code does. A denial is
itself information: it can mean a path is being hidden from this process
by a mount namespace, which is a different finding than the path simply
not existing.

### The baseline matters as much as the findings

On an unmodified device, every check here should come back `NOT_FOUND` —
no interesting maps entries, no stray descriptors, no overlay mounts, no
unexpected libraries, `TracerPid: 0`, every probed path unreachable, every
property matching its stock-production expectation, no thread with an
unreadable `comm` file — with one deliberate exception: `GotIntegrityCheck`
comes back `COULD_NOT_RUN` even on a clean device, because it never
resolves an actual GOT slot value (see "The checks" above) and so can
never honestly claim the underlying invariant held, clean run or not.
`Report.asText` calls out an all-`NOT_FOUND` run explicitly when every
other check comes back that way. A clean run is not a non-result — it's
the baseline that gives a later `FOUND`, once one of this book's own
modules from Labs 1–6 is armed, its meaning. Run the harness unarmed
first and read it before running it armed.

## Build

```bash
export JAVA_HOME=/home/joseph/.jdks/temurin-23.0.2   # or another JDK 17+
cd modules/07-detection-harness
./gradlew assembleDebug   # -> app/build/outputs/apk/debug/app-debug.apk
```

Requires the Android SDK; `local.properties` (gitignored) points at it. If
you don't have one, create it:

```
sdk.dir=/path/to/Android/Sdk
```

**Built and packaged, not yet installed on the reference rig.** This
project compiles cleanly with `./gradlew assembleDebug` and produces a
real, installable debug APK — that's been verified. Nothing about what the
app actually reports on a device has been. See "Reference rig" below.

## Install and run

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n dev.zygisklab.detectionharness/.MainActivity
```

Run it once with none of Labs 1–6 armed and record the baseline (copy or
share the report). Arm one module at a time — following that module's own
README for how — relaunch the harness, and compare. The Lab 7 deliverable
is the table this produces: your modules against the check matrix, with
the specific line of your code responsible for each hit that shows up
(a mapped `.so` path traced back to `dlopen()`, a mount entry traced back
to how the root provider mounts itself, and so on).

## What only a device can confirm

Everything about what this app actually reports is reasoning from how
`/proc/self/*`, `stat()`, and `getprop` are documented to behave on
Android, not an observation:

- That the specific substrings in `checks/Signatures.kt` and the specific
  paths in `FilesystemProbeCheck` actually match what Zygisk Next 1.4.5
  and KernelSU-Next 3.3.0 leave behind on this rig, rather than a slightly
  different set of names or locations this list doesn't cover.
- That the stock-production property values `PropertiesCheck` expects
  (`ro.secure=1`, `ro.debuggable=0`, `ro.build.tags=release-keys`, and so
  on) are in fact what this Pixel 6 Pro reports before anything in this
  book is installed.
- That `TracerPid` reads `0` on an ordinary launch of this app on that
  device, with no debugger or profiler attached by the tooling used to
  install it.
- That none of the checks here cost enough — a handful of file reads and
  one short-lived `getprop` child process — to be noticeable next to a
  real app's own startup work, which is asserted from what the checks do,
  not from a measured frame time.

## What this harness does not measure

Chapter 24 is explicit that a self-inspection harness like this one has
known gaps, and this README says the same rather than overselling what
nine checks against `/proc/self/*` and `getprop` can actually see:

- **GOT/PLT-slot verification is only half done.** `GotIntegrityCheck`
  can locate where `libc.so` is mapped from and flag an impostor mapping
  under that name, but it cannot resolve what any specific GOT slot
  actually holds — that needs `dlsym()` or a full ELF relocation-table
  parser, neither of which a pure-Kotlin app without JNI/NDK code has. The
  strongest check the book describes is therefore not implemented here in
  the form Chapter 22 lays out; a native module with `dlopen`/`dlsym` and
  raw memory access could go further than anything in this app can.
- **No check covers call-timing.** A module that hooks a function by
  patching its GOT slot or splicing its prologue typically adds
  measurable latency to that call. Nothing here times a syscall or a
  library call and compares it against an expected cost — doing that
  usefully needs a controlled, repeated measurement this harness doesn't
  attempt, and a naive one-shot timing would be too noisy to trust.
- **No check reads `/proc/self/exe`'s own on-disk bytes for tampering.**
  This harness inspects the live process's maps, descriptors, mounts, and
  properties; it never compares the running code against a known-good
  copy of the APK's own code, which would be a different kind of check
  (integrity, not injection-detection) and is out of scope here.
- **Thread inspection reports, it does not classify.** `ThreadsCheck`
  hands back the full thread inventory and flags only a structural read
  failure as `FOUND`; it has no baseline of "normal" ART/framework thread
  names to compare against, so an app-spawned or module-spawned thread
  with an innocuous-looking name would not be distinguished from any
  other by this check alone.
- **Every finding here is reasoning about a specific rig, not an
  observation from one yet.** See "What only a device can confirm" above
  and "Reference rig" below — nothing in this app has been run on real
  hardware.

## Reference rig

Written for Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk
Next 1.4.5.

**Not yet run on that rig.** This app compiles and packages cleanly, and
nothing more than that has been established. Every statement above about
what it reports at runtime is reasoning from how `/proc/self/*`,
`stat(2)`, and Android's property system are documented to behave, not an
observation. Treat the expected clean baseline, and every example finding
above, as a prediction to be tested — see "What only a device can confirm".

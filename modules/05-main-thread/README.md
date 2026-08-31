# 05 — Main Thread

**Lab 5.** Threading and timing. Your module's code runs early, and not
necessarily on the thread the app will treat as its main thread. This
module does two things: it proves, in every callback, which OS thread it
actually ran on; then it gets a small, harmless action executed on the
app's *real* main thread, at a moment it chose, without blocking
`preAppSpecialize` or `postAppSpecialize` to get there.

## What it proves

For the configured target (see Configuration below):

```
preAppSpecialize: pid=5678 tid=5678 gettid()==getpid() -> true
preAppSpecialize: pid=5678 proc=com.android.chrome ARMED, Process.setArgV0 hook installed
postAppSpecialize: pid=5678 tid=5678 gettid()==getpid() -> true
postAppSpecialize: pid=5678 nice_name=com.android.chrome getuid=10123, 4.10ms after arming - still waiting for the main-thread action to fire
main-thread action (Process.setArgV0 hook): pid=5678 tid=5678 gettid()==getpid() -> true
main-thread action: 11.83ms after preAppSpecialize armed the hook
```

For every other launch:

```
preAppSpecialize: pid=1234 tid=1234 gettid()==getpid() -> true
preAppSpecialize: pid=1234 nice_name=com.other.app not armed (target=com.android.chrome), no hook installed
```

One `preAppSpecialize` line and one `not armed` line, nothing after -
`setOption(DLCLOSE_MODULE_LIBRARY)` unloads the module before
`postAppSpecialize`, same as Labs 3 and 4, so `hookJniNativeMethods()` is
never even called on this path.

Note that in the armed trace, `tid == pid` (so `gettid() == getpid()`) at
*every single point*, including inside the main-thread action. That is
expected and is explained below — it does not mean the "which thread"
question was pointless to ask.

## Configuration

Same file, same contract as Labs 3 and 4:

```bash
adb shell su -M -c "echo -n com.android.chrome > /data/adb/modules/zygisklab_mainthread/target.txt"
```

If `target.txt` is missing, empty, unreadable, or matches nothing, the
module never arms and never touches `Process.setArgV0`. See
`03-armed-once/README.md` for the exact-match policy and why it isn't a
package-name prefix check.

## The problem this lab is about

`preAppSpecialize` runs with zygote's own privilege, before this process
has any app-specific sandbox restrictions - the process has *just* been
forked, and:

- The `Application` object doesn't exist yet, and won't for a while.
- Whether Android's Java-level main `Looper` has been prepared
  (`Looper.prepareMainLooper()`, called early in
  `ActivityThread.main()`) is not something `preAppSpecialize` can assume
  either way.
- Blocking either `preAppSpecialize` or `postAppSpecialize` to wait for
  either of those - by sleeping, spinning, or joining a thread - delays
  every single app launch this module is armed for. That's the mistake
  Chapters 8 and 11 warn against, and this module does not make it:
  neither callback below does anything but install a hook and return.

So the deliverable splits into two separate questions, answered
separately: which thread is a callback running on right now (answered by
`logThread()`, on every callback), and how do we get code to run on the
main thread later, at a moment we pick, without blocking to wait for it.

## Proving which thread: `gettid()` vs `getpid()`

On Linux, the thread that calls `fork()` becomes the new process's sole
thread, and that thread's tid is set to the new pid at the moment of the
fork - an identity that holds for as long as that thread lives. Zygote
forks each app process from exactly one thread, so immediately after the
fork, `gettid() == getpid()` is true on the thread this module's callbacks
run on, and stays true unless something spawns another thread first (this
module never does).

`logThread()` in `jni/main.cpp` logs both numbers from every callback -
`preAppSpecialize`, `postAppSpecialize`, and the `Process.setArgV0` hook -
so the reader sees the proof directly in logcat rather than trusting a
comment that claims it. This is the cheap, honest half of "which thread
am I on"; the header gives no other way to ask the question, and none is
needed - `/proc/self/task` would show the same answer with far more
ceremony.

**What this does *not* prove**, and why the trace above shows `tid == pid`
everywhere including inside the "main thread" action: tid/pid identity
only tells you this is the process's *original* OS thread - the one
zygote's fork left it with. It says nothing about whether Android's Java
main `Looper` exists yet, or whether `Application` has been created. Those
are the parts covered next, and they're the actual reason this module
waits instead of acting immediately in `preAppSpecialize`, even though
`preAppSpecialize` already satisfies `gettid() == getpid()`.

## Reaching the main thread at a moment we chose

Three approaches were on the table:

1. **Post a `Runnable` to the main `Looper`'s `Handler`, through JNI, once
   the runtime is ready.** This is the obvious answer, and it's what a
   module built alongside a Java/Kotlin component would do. It doesn't
   fit here: `Handler.post(Runnable)` needs a live Java object that
   implements `Runnable`, and this lab is `jni/`-only - `ndk-build`, no
   Gradle, no `.dex`/`.jar` to define that class in. Synthesizing
   bytecode at runtime to work around that (e.g. `DefineClass` with a
   hand-built class file, or an `InMemoryDexClassLoader` fed a hand-built
   dex) is possible in principle, but it's a lab of its own, not a
   thread-and-timing one, and it would make `./build.sh` depend on a
   bytecode assembler this repository doesn't have.
2. **A native approach that observes the main thread's message loop
   directly** - reading `Looper`/`MessageQueue` internals from native
   code to detect when the loop starts pumping. This trades one guess (a
   hook target) for a worse one: `Looper`'s native layout is even less
   stable across Android/ART versions than a documented method name, and
   getting it wrong fails silently instead of logging a clear "could not
   resolve" the way this module's chosen approach does.
3. **Wait for a natural main-thread entry point the app itself will
   reach, and do the work there.** This is what this module does: hook a
   `native`-declared Java method that the framework itself calls, as an
   ordinary part of every app's startup, on the real main thread. No
   Runnable, no bytecode synthesis, no guessing at Looper internals -
   just `Api::hookJniNativeMethods()`, the exact mechanism the header's
   own top-of-file example demonstrates (see `zygisk.hpp`'s
   `logger_entry_max_payload_native` hook) and the one 04-plt-hook's PLT
   hook is the native-symbol sibling of.

### Why `android.os.Process.setArgV0`

`Process.setArgV0(String)` is a `static native` method that sets this
process's `argv[0]` - what `ps` and `/proc/<pid>/cmdline` show. Framework
source calls it from `ActivityThread.handleBindApplication()`, which is
the method the main thread's `Handler` ("H") runs in response to the
`IApplicationThread.bindApplication()` Binder callback from
`ActivityManagerService` - the callback that starts turning a freshly
specialized, still-generic process into *this specific app*. That call:

- Runs on the real main thread, through the real main `Looper`, strictly
  after `Looper.prepareMainLooper()` has already executed in
  `ActivityThread.main()` - unlike `preAppSpecialize`/
  `postAppSpecialize`, which run before any of that Java-level startup.
- Happens early within `handleBindApplication()` - ahead of
  `Instrumentation` and `Application` being created - so the moment this
  module's hook fires is deliberately early: later than "the OS thread
  that will become main" (already true throughout `preAppSpecialize`),
  but earlier than the app having any object graph of its own for this
  module to disturb, exactly the gap the assignment describes.
- Is universal: every app process sets its `argv[0]` this way, exactly
  once, regardless of what the app itself does or doesn't do.

The hook itself is installed in `preAppSpecialize` - `hookJniNativeMethods`
is documented against the pre-specialize calls, and installing early means
it's live for the whole rest of startup, including the call it's waiting
for. Installing it does not run any app code and does not block: it only
rewrites one JNI method table entry, in memory this module already has
access to. The actual work happens later, inside `my_setArgV0()`, entirely
off of `preAppSpecialize`'s call stack - which is *how* this avoids
blocking app launch: the callback that installs the hook returns
immediately, and the hook fires on its own, whenever Android gets there.

**This exact call site — `handleBindApplication()` calling
`Process.setArgV0()` on the main-thread Looper, ahead of `Application`
being created — is asserted from framework source read while writing this
module, not observed on a device.** Nothing in this module has been run
on real hardware. If a future Android version moves, renames, or removes
that call, the hook simply never resolves or never fires - see Failure
handling below - and this README is not claiming otherwise.

## The main-thread action

`my_setArgV0()` does exactly what the assignment asks for and nothing
more: one log line proving `gettid() == getpid()` at the moment it runs,
plus a timestamp showing how many milliseconds elapsed since
`preAppSpecialize` armed the hook (captured via
`clock_gettime(CLOCK_MONOTONIC, ...)`, so it's immune to wall-clock
adjustments). It does not touch `Application`, `Context`, or any app
state. `env`, `clazz`, and `name` are passed straight through to the real
`setArgV0()` unmodified and its return value (`void`) is not altered -
same observe-don't-alter discipline as 04-plt-hook's `openat()` hook: this
module cannot change what argv[0] the app ends up with.

## Failure handling

Two distinct ways this can fail, both handled without crashing the host
app:

- **`target.txt` unreadable/unset/non-matching**: same failure-closed
  behaviour as Labs 3 and 4 - the module logs why it isn't arming and
  unloads via `DLCLOSE_MODULE_LIBRARY`. No hook is ever installed.
- **`hookJniNativeMethods()` can't resolve `android/os/Process`'s
  `setArgV0(Ljava/lang/String;)V`** (wrong class, method, or signature -
  e.g. a future Android version changing it): the header documents this
  as setting `methods[0].fnPtr` to `nullptr` rather than failing loudly.
  This module checks that explicitly after the call and logs a `WARN`
  saying the main-thread action will not run, instead of silently waiting
  forever for a log line that will never come. Android keeps calling the
  *real*, un-replaced `setArgV0()` in this case - the app is completely
  unaffected either way.

Neither failure path touches app state or throws from native code back
into the app; the worst outcome of anything going wrong here is "one log
line never appears."

## Lifecycle and thread safety

This module spawns no threads of its own. The hook fires on whatever
thread Android calls the hooked native method from - which, per the
reasoning above, is the app's main thread, exactly once, for the single
call `handleBindApplication()` makes. There is no persistent background
work, nothing to clean up if the process dies before the hook fires (it
simply never fires, and nothing was holding a resource open), and no
re-entrancy concern the way 04-plt-hook's `openat()` hook has - `setArgV0`
is not itself a logging primitive, so this hook calling `LOGI` inside it
cannot recurse back into itself.

## What only a device can confirm

Everything below is asserted from reading AOSP framework source and the
Zygisk API header, not from running this module:

- That `ActivityThread.handleBindApplication()` still calls
  `Process.setArgV0()`, at that position, on the Pixel 6 Pro / Android 16
  build this book targets.
- That the callback genuinely runs on the main thread via the main
  `Looper`'s `Handler` on that build - i.e. that `logThread()` inside
  `my_setArgV0()` actually logs `gettid() == getpid()` as `true`, and does
  so on the *same* tid the app's own main thread uses for everything
  after (there is no code here that cross-checks against
  `Looper.getMainLooper().getThread()` from the Java side, since doing so
  would need the same Runnable-construction machinery this design
  deliberately avoids).
- The actual elapsed time between arming and the action firing - the
  `%.2fms` figure in the trace above is illustrative, not measured.
- That `hookJniNativeMethods()` behaves as documented (fnPtr null on a
  resolution failure) under Zygisk Next 1.4.5 specifically, rather than
  just under the upstream Zygisk implementation the header describes.
- That none of this trips whatever integrity/attestation checks a real
  target app runs - this module was written for the same reference rig as
  the rest of this book, not audited against any specific app.

## Build

```bash
export ANDROID_NDK_HOME=/home/joseph/Android/Sdk/ndk/29.0.14206865
./build.sh              # -> out/05-main-thread.zip
```

## Install

Flash `out/05-main-thread.zip` in your root manager, set `target.txt` (see
Configuration above), and reboot. For subsequent iterations use
`./deploy.sh` — see `03-armed-once/README.md` / Chapter 7 for why it
pushes and `mv`s instead of `cp`ing over the live `.so`.

## Watch, and run the control

```bash
adb logcat -s ZygiskLab
```

Launch the configured target and confirm all three `gettid()==getpid()`
lines appear, followed by the `main-thread action` lines. Then launch a
**different** app you did not configure as the target — the control. It
should produce exactly one `preAppSpecialize` line, one `not armed` line,
and nothing else: no `postAppSpecialize`, no `Process.setArgV0` hook, no
main-thread action. If the control ever shows a `main-thread action` line,
the arming check is broken, not the hook - re-check `target.txt` before
assuming anything about `setArgV0` leaked scope.

## Reference rig

Written for Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next
1.4.5.

**Not yet run on that rig.** This module compiles and packages cleanly, and
nothing more than that has been established. Every statement here about what
it does at runtime is reasoning from the API header and from AOSP, not an
observation. Treat the expected output as a prediction to be tested. As stated above, this specific module has not
yet been run on that device — see "What only a device can confirm".

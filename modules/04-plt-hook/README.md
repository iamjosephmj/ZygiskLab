# 04 — PLT Hook

**Lab 4.** Hooking native symbols. This module installs a PLT hook on
`openat()` in one configured process, logs a bounded sample of the calls it
intercepts, and calls through to the real function unchanged — then relies
on an unhooked control process to prove the hook is scoped to where you
think it is.

## What it proves

For the configured target:

```
preAppSpecialize: pid=5678 nice_name=com.android.chrome ARMED, openat() hook committed
openat: pid=5678 proc=com.android.chrome path=/data/user/0/com.android.chrome/shared_prefs/foo.xml flags=0x2 [1/20 logged]
openat: pid=5678 proc=com.android.chrome path=/system/framework/framework-res.apk flags=0x0 [2/20 logged]
...
openat: proc=com.android.chrome further calls suppressed after 20 logged calls
postAppSpecialize: pid=5678 nice_name=com.android.chrome getuid=10123
```

For every other launch:

```
preAppSpecialize: pid=1234 nice_name=com.other.app not armed (target=com.android.chrome), no hook installed
```

One line, no `openat:` lines follow it, and no `postAppSpecialize` line
either — same as Lab 3, `setOption(DLCLOSE_MODULE_LIBRARY)` unloads the
module before it gets that far. `pltHookRegister()` and `pltHookCommit()`
are never even called on this path.

## Configuration

Same file, same contract as Lab 3:

```bash
adb shell su -M -c "echo -n com.android.chrome > /data/adb/modules/zygisklab_plthook/target.txt"
```

If `target.txt` is missing, empty, unreadable, or matches nothing, the
module never arms and never touches `openat()` anywhere. See
`03-armed-once/README.md` for the full reasoning on the exact-match
policy and why it isn't a package-name prefix check.

## Why `openat()`

Three requirements had to line up: called reliably by ordinary apps, cheap
enough that logging every call doesn't flood the log or wedge the app, and
safe to intercept without changing behaviour.

Bionic implements the plain `open(2)` libc wrapper itself as a call to
`openat()` with `AT_FDCWD`, and nearly everything that opens a file on
Android — `fopen()`, the classloader reading a dex/jar, SQLite opening a
database, a resource lookup — bottoms out at `openat()`. Hooking that one
symbol observes file opens app-wide instead of only the call sites that
happen to write `open()` literally. `access()` and `stat()` were the other
candidates in this family; they're called far more selectively and don't
give the same coverage.

It's cheap: the replacement does a counter check and, rarely, one log line,
then tail-calls the original. And it's safe by construction — the
replacement below always calls through to `orig_openat()` with the
original arguments and returns exactly what it returns. It cannot change
which file gets opened, with what flags, or what the call returns; this
module observes, it does not alter behaviour.

## Lifecycle: register and commit, both in `preAppSpecialize`

`pltHookRegister()` finds `libc.so` in this process's own
`/proc/self/maps` — by `(dev, inode)`, not by path, since the same file
can be mapped from different paths — and records one
`(dev, inode, symbol) -> newFunc` entry. `pltHookCommit()` is the call
that actually rewrites the PLT/GOT entries for everything registered so
far.

The header's own example (the JNI-method hook in the file-level doc
comment at the top of `zygisk.hpp`) performs the equivalent kind of hook
in `preAppSpecialize` — before the process picks up its sandbox
restrictions, while it still runs with zygote's privilege — and that's
what this module does too: `findLibc()`, `pltHookRegister()`, and
`pltHookCommit()` all happen inside `preAppSpecialize`, nowhere else. Two
things make this the right point rather than an arbitrary one:

- `libc.so` is already mapped at that point — this process was just
  forked from zygote, which linked against libc long before this module's
  `onLoad()` ever ran, so `/proc/self/maps` already has the entry
  `findLibc()` is looking for.
- Registering and committing before specialization means the hook is live
  for the *entire* rest of app startup, including whatever the app's
  `Application`/`attachBaseContext` code does early on — not just the
  tail end after `postAppSpecialize`, which would miss all of that.

The header doesn't state an explicit "callable only in preAppSpecialize"
restriction for `pltHookRegister`/`pltHookCommit` the way it does for
`getModuleDir()` and `exemptFd()` — so this is a considered choice
following the header's own example and the two points above, not a rule
copied verbatim from a comment.

## Checking the commit result

`pltHookCommit()` returns `bool`, and this module does not ignore it. A
hook that silently failed to install is the single most confusing outcome
here: the module loads, arms, logs that it armed — and then nothing about
`openat()` ever shows up, with no indication why. `preAppSpecialize` logs
one of two outcomes explicitly:

```
... ARMED, openat() hook committed
... ARMED, but pltHookCommit() FAILED - openat() is NOT hooked in this process
```

If `findLibc()` can't locate `libc.so` in the maps at all, that's logged
too, and `pltHookRegister`/`pltHookCommit` are never called.

## Re-entrancy

The replacement, `my_openat()`, may run on any thread the target process
has, and it calls `__android_log_print()` from inside a hooked libc
function. On current Android, `__android_log_print` talks to `logd` over a
socket rather than through `openat()`, so in practice this doesn't
recurse. But that's an implementation detail of the current logging
backend, not a guarantee this header makes — a future backend, or a
different log call added here later, that opens a file would turn one
intercepted call into unbounded recursion.

The guard is a `thread_local int reentryDepth`, incremented around the
`LOGI()` calls inside `my_openat()` and checked before logging anything.
If the hook is ever re-entered on the same thread, the inner call skips
logging (it still calls through to `orig_openat()` — only the log line is
skipped). This costs one thread-local increment/decrement per logged
call and makes the recursion failure mode structurally impossible,
independent of what logging happens to do under the hood on any given
Android version.

## Log volume

An app can call `openat()` hundreds of times a second during startup
(asset packs, SQLite, `classes.dex`, shared prefs). Logging every call
unboundedly is the flood this lab warns against, and a fast enough logcat
writer can visibly stall the process it's injected into. A global
`std::atomic<int>` counter caps this at **20 logged calls per process**;
every call past the cap still runs and still calls through to the real
`openat()` — it just stops producing a log line — and the 21st call prints
one line announcing that further calls are suppressed, so the cap itself
is visible in the log rather than a silent cutoff.

## Build

```bash
export ANDROID_NDK_HOME=/home/joseph/Android/Sdk/ndk/29.0.14206865
./build.sh              # -> out/04-plt-hook.zip
```

## Install

Flash `out/04-plt-hook.zip` in your root manager, set `target.txt` (see
above), and reboot. For subsequent iterations use `./deploy.sh` — see
`03-armed-once/README.md` / Chapter 7 for why it pushes and `mv`s instead
of `cp`ing over the live `.so`.

## Watch, and run the control

```bash
adb logcat -s ZygiskLab
```

Launch the configured target and confirm the `openat:` lines appear,
capped at 20, tagged with `proc=<your target>`. Then launch a **different**
app you did *not* configure as the target — the control. It should produce
exactly one `not armed` line and no `openat:` lines at all: proof the hook
fired only in the process it was installed in, not process-wide or
system-wide. If the control process ever shows `openat:` lines, something
is wrong with the arming logic, not the hook itself — re-check
`target.txt` and the exact-match comparison before assuming the hook
leaked.

## Reference rig

Written for Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next
1.4.5.

**Not yet run on that rig.** This module compiles and packages cleanly, and
nothing more than that has been established. Every statement here about what
it does at runtime is reasoning from the API header and from AOSP, not an
observation. Treat the expected output as a prediction to be tested.

# 03 — Armed Once

**Lab 3.** Choosing not to run. `preAppSpecialize` and `postAppSpecialize`
fire for every app the provider injects into — this module arms for exactly
one configured process and gets out, as cheaply as possible, of every other
one.

## What it proves

For a launch that is **not** the configured target:

```
preAppSpecialize: pid=1234 nice_name=com.other.app not armed (target=com.android.chrome), unarmed path cost=42us, dlclose requested
```

One log line. No `postAppSpecialize` line follows it — `setOption(DLCLOSE_MODULE_LIBRARY)`
unloads the module's library from that process before specialization
finishes, so there's nothing left in `com.other.app` to prove anything
further about.

For the configured target:

```
preAppSpecialize: pid=5678 nice_name=com.android.chrome ARMED getuid=0 (current identity) args->uid=10123 (destination)
postAppSpecialize: pid=5678 nice_name=com.android.chrome getuid=10123
```

Same distinction as Lab 1: `getuid()` in `preAppSpecialize` is zygote's own
identity (root, uid 0) because the process hasn't specialized yet;
`args->uid` is not a live read of anything, it's the uid this process is
*about to become*. `postAppSpecialize`'s `getuid()` — now equal to the
`args->uid` we were told to expect — is what actually proves the process
crossed the specialization boundary.

## Configuration: a file, not a compile-time constant

The target process is read at runtime from `target.txt`, a plain-text file
living next to `module.prop` in the module's own directory on the device —
not baked into the `.so`. `Api::getModuleDir()` hands back a file
descriptor for exactly that directory, so `main.cpp` uses `openat()` on it
to read the config; this is the only sanctioned way to reach the module's
own files from `pre[XXX]Specialize`; there's no reason to fall back to a
compile-time constant. The tradeoff: `target.txt` is read from disk on
*every single app launch* the provider hands this module, armed or not, and
that read is folded into the unarmed-path measurement below rather than
optimized away — which is honest, since a real per-process module has no
way to cache the answer across processes anyway.

Set the target:

```bash
adb shell su -M -c "echo -n com.android.chrome > /data/adb/modules/zygisklab_armed/target.txt"
```

`-n` matters less than it looks: `main.cpp` trims a trailing newline itself,
so `echo com.android.chrome > target.txt` (which adds one) works the same
way. If `target.txt` is missing, empty, or unreadable, the module logs a
warning and never arms for anything — it fails closed, not open.

## Package name vs. process name

`args->nice_name` is a **process** name, not a **package** name, and for a
multi-process app they diverge: a background service can run as
`com.example.app:sync` while the main process is plain `com.example.app`.

This module matches `nice_name` **exactly** against the configured target.
That's a deliberate, narrower position, not an oversight: it arms only the
one process named in `target.txt` and stays unarmed — and unloaded — in
every other process of that same app, sub-processes included. The
alternative, treating the target as a package-name prefix so the module
arms for `com.example.app` *and* `com.example.app:sync`, is sometimes the
right call, but it has to be a real token-boundary check (`name == target`
or `name` starts with `"target:"`), not a bare `strncmp` prefix test — a
naive prefix also matches unrelated packages like `com.example.app2`
against a target of `com.example.app`. Chapter 9 works through both
policies in depth; this module embodies the exact-match one because among
the two, it's the one that cannot accidentally over-arm.

## The measurement

`preAppSpecialize` takes a `CLOCK_MONOTONIC` timestamp as its first
statement — before touching JNI or the filesystem — and, on the unarmed
path, a second one immediately before `return`. The delta is logged in
microseconds alongside the `dlclose requested` line.

Read this honestly: it measures this module's own callback cost — reading
`args->nice_name`, opening and reading `target.txt`, one `strcmp`, calling
`setOption()` — not the Zygisk provider's total per-app injection overhead
(loading the module `.so`, calling `onLoad`, whatever the provider itself
does around the callback). It also isn't one number: a single sample is
noise from scheduling, cold page cache, and whatever else the zygote fork
happened to be doing at that moment. Launch a handful of unrelated apps,
collect several `unarmed path cost=` lines from `logcat`, and look at the
spread, not any one value.

## Build

```bash
export ANDROID_NDK_HOME=/home/joseph/Android/Sdk/ndk/29.0.14206865
./build.sh              # -> out/03-armed-once.zip
```

## Install

Flash `out/03-armed-once.zip` in your root manager, set `target.txt` (see
above), and reboot. For subsequent iterations use `./deploy.sh`, which
installs safely — see Chapter 7 for why `cp` over a live module bricks
zygote.

## Watch

```bash
adb logcat -s ZygiskLab
```

Launch the configured target and a handful of other apps in the same
session to see both paths — armed and unarmed — in the same log.

## Reference rig

Written for Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next
1.4.5.

**Not yet run on that rig.** This module compiles and packages cleanly, and
nothing more than that has been established. Every statement here about what
it does at runtime is reasoning from the API header and from AOSP, not an
observation. Treat the expected output as a prediction to be tested.

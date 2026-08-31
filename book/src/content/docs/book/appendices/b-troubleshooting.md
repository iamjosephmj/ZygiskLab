---
title: "Troubleshooting by symptom"
description: "Troubleshooting by symptom, from module not listed to bootloop, with the likely cause of each."
sidebar:
  order: 2
status: unverified
---

Zygisk fails silently. That is the recurring finding of this whole book: a call
into a slot the provider never implemented, a correctly-written call made in the
wrong callback, a `.so` with the wrong SELinux label, and a module that was never
loaded at all are, from outside, the same event — nothing happens and nothing
says why. Chapter 4's silence, Chapter 6's absent ABI, Chapter 7's stale label
and Chapter 10's ignored `setOption` all present identically in your logcat.

So this appendix is not a list of causes. It is a set of **discriminators**: for
each symptom, the one check that tells otherwise-identical causes apart, and the
chapter that explains the mechanism behind it. Nothing here has been run on a
device — every diagnostic below is framed as what to check, not as what was
observed. Where a check depends on your provider rather than on the API, it says
so.

## The manager does not list the module

**What you see.** You install the zip, the manager reports success or a vague
error, and the module is not in the list. Nothing has run.

**Likely causes**, cheapest first:

1. `module.prop` is not at the zip root — the usual cause is compressing the
   folder rather than its contents.
2. A missing required field, or trailing whitespace on `id`.
3. The zip is fine and you flashed an older one.

**The discriminator.** `unzip -l out/<module>.zip`. If `module.prop` appears with
a directory prefix, that is the whole answer and no other check is needed. If it
is at the root, the problem is inside the file, and the manager's own install log
is the next place to read. This failure happens before Zygisk is involved at all,
so it is a packaging problem, never a code problem.

[Hello, Zygisk](/ZygiskLab/book/foundations/04-hello-zygisk/) ·
[The rig and toolchain](/ZygiskLab/book/foundations/03-rig-and-toolchain/)

## Listed, enabled, rebooted — and completely silent

**What you see.** The module is in the manager, Zygisk is on, you rebooted, and
`adb logcat -s ZygiskLab` prints nothing at all. Not from `onLoad`, not from
either specialize callback.

This is the single most overloaded symptom in the book. At least seven distinct
causes produce exactly it.

**Likely causes**, cheapest first:

1. The provider is installed but not *running*. A bare `ls /data/adb/modules`
   lists it either way.
2. Your app is not in the provider's scope list.
3. Wrong ABI: you shipped `arm64-v8a.so` and the process was forked from a
   32-bit zygote.
4. Wrong SELinux label on the `.so` — present, correctly named, hash-identical,
   and unopenable.
5. A `DT_NEEDED` dependency that does not resolve in your linker namespace, so
   `dlopen` fails outright.
6. An API version the provider refuses: `registerModule` returns early and
   `onLoad` is never called.
7. You deployed correctly and did not reboot, so zygote is still running the old
   mapping — or has never mapped anything.

**The discriminators**, in the order that eliminates the most per check:

```bash
adb logcat | grep -i zygisk
```

Widen the filter off your own tag. Your tag comes from your code, and your code
never ran; whatever is knowable is in what the *loader* says about itself. On the
reference rig that means the Zygisk Next module entry and its WebUI, which report
modules that failed to load. The provider's exact log tag is not part of any API
— find it once on your device and reuse it.

```bash
adb shell getprop ro.zygote
```

A value naming both bitnesses means a 32-bit zygote exists. If it does and you
shipped one ABI, and the silence is *partial* — most apps fine, one app silent —
you have already found your bug. Total silence in every process is not this.

```bash
adb shell su -M -c 'ls -Z /data/adb/modules/<id>/zygisk/'
```

Compare the `.so`'s label against `module.prop`'s label in the same directory.
`module.prop` is the right reference because the manager wrote it in place at
install time, so it carries exactly what this provider's policy assigns to this
module's files. A mismatch is Chapter 7's silent refusal, and it is what
`restorecon` after the `mv` exists to prevent.

```bash
readelf -d libzygisklab.so | grep NEEDED
```

Anything outside `libc`, `libm`, `libdl`, `liblog` is a load-time gamble. An
unresolvable one kills the entire module and is indistinguishable, from your
side, from shipping the wrong ABI.

:::caution
"Silent" and "crashing" are different failures needing different investigations,
and the first check is which one you have. No `onLoad` line means the provider
never reached your code. A crash means your code ran and was wrong. Establish
that before theorising.
:::

[How the loader finds you](/ZygiskLab/book/load/06-how-the-loader-finds-you/) ·
[Deploying without bricking zygote](/ZygiskLab/book/load/07-deploying-safely/) ·
[The rig and toolchain](/ZygiskLab/book/foundations/03-rig-and-toolchain/)

## The module loads but does not fire in the process you wanted

**What you see.** `onLoad` prints. `preAppSpecialize` prints for some processes.
The one app you care about produces nothing.

**Likely causes:**

1. The app runs in a process whose `nice_name` is not the package name — a
   `:remote` service, or a child zygote.
2. Your target-matching string is wrong: a typo, or a package that was renamed.
3. The provider does not inject that process at all. Which processes a provider
   covers is a scope-model decision the header does not fix.
4. Wrong ABI for that one app specifically.

**The discriminator.** Log one line per `preAppSpecialize` naming the pid and the
`nice_name` you were handed, unconditionally, before any matching. That line
turns "did not fire" into "fired and the name did not match" or "never fired at
all", which are different bugs with different owners. It is the cheapest
instrument in the book and the only way to answer the provider-scope question on
your own device.

[Reading AppSpecializeArgs](/ZygiskLab/book/prespecialize/09-reading-appspecializeargs/) ·
[Where it breaks](/ZygiskLab/book/companion/20-where-it-breaks/)

## It fires in every process

**What you see.** Hundreds of log lines a minute from apps you never armed.

**Likely causes:**

1. No allowlist configured — Zygisk's default without one is to inject broadly.
2. Your module has no self-scoping logic, so it runs everywhere it is loaded.

**The discriminator.** There is nothing to diagnose; the module is working and
the scope is wrong. Fix it in the manager first, then in code, because a module
that decides for itself does not depend on a manager setting you might forget. A
genuinely global module is also a much larger detection surface.

[Choosing not to run](/ZygiskLab/book/prespecialize/11-choosing-not-to-run/) ·
[How an app looks for you](/ZygiskLab/book/footprint/22-how-an-app-looks-for-you/)

## SIGSEGV during specialization, only in armed apps

**What you see.** The device stays up. The launcher and Settings are fine. The
app your module is scoped to dies instantly on launch, every time, with the fault
surfacing inside the provider's library rather than yours. Apps you did not arm
keep working.

Three readings are natural and all three are wrong: *I broke my module*, *the app
is detecting me*, *the provider is broken*.

**Likely causes:**

1. You deployed with `cp` (or `>`, `dd of=`, `install`, `adb push` straight into
   the module directory, or an editor's save-in-place) over a `.so` that zygote
   has mapped. Same inode, live mapping, corrupted text.
2. A genuine bug in code that only armed processes execute.

The selectivity is not by app identity — it is by *how much of your text a
process executes*, which on your device correlates almost exactly with which apps
you armed.

**The discriminator**, and the model for this whole appendix:

```bash
adb shell su -M -c 'md5sum /data/adb/modules/<id>/zygisk/arm64-v8a.so'
md5sum libs/arm64-v8a/libzygisklab.so
```

| Hashes | Meaning | Action |
|---|---|---|
| Differ | Deploy never landed | Fix the deploy; your code is untested |
| Match | Disk is correct, mapping is stale | Reboot before concluding anything |

A matching hash means "is my code broken?" is *not yet answerable*, because the
running zygote is not executing what is on disk. Reboot and retest: a crash that
survives the reboot is a real bug and the tombstone means what it appears to
mean; one that does not was your deploy. Hash first, reboot second, debug third.

[Deploying without bricking zygote](/ZygiskLab/book/load/07-deploying-safely/)

## The app dies some time after launch, or in an unrelated thread

**What you see.** Not a launch crash. The app runs, then dies later, possibly on
a thread you never touched, possibly at a faulting address belonging to no loaded
module.

**Likely causes:**

1. An uncleared pending JNI exception. With one pending, almost every JNI
   function is unsafe, so the crash lands several calls after the real fault.
2. `DLCLOSE_MODULE_LIBRARY` set after you installed a hook, started a thread,
   registered an `atexit`, or left a vtable pointer behind. Your text is unmapped
   and the pointer into it is called later.
3. Leaked local references growing in a native loop — this presents as steady
   memory growth over hours rather than a crash.

**The discriminator.** A faulting address that symbolicates to *nothing* — no
mapped module in the tombstone's map for that range — points at the `dlclose`
case, because the code that faulted no longer exists. A backtrace with frames in
ART's JNI machinery, arriving at a JNI call that had nothing wrong with it,
points at an exception pending from an earlier call. `ExceptionCheck` after every
call that can throw, and either handle or `ExceptionClear` at the site that
caused it — there is no third option.

[JNI inside a live app](/ZygiskLab/book/postspecialize/13-jni-inside-a-live-app/) ·
[setOption and flags](/ZygiskLab/book/prespecialize/10-setoption-and-flags/)

## `FindClass` fails, or a method ID comes back null

**What you see.** The same helper works from a hook and fails from your worker
thread. Framework classes resolve; the app's own classes do not.

**Likely causes:**

1. Calling from a natively-attached thread, which gets the system classloader,
   not the app's.
2. A dotted name passed where slashed is expected, or the reverse — a
   `ClassNotFoundException` from `loadClass` is the dotted/slashed tell.
3. A wrong signature, or hidden-API restriction, on a member that plainly exists.

**The discriminator.** Change only the *calling context* and retest. If the same
lookup succeeds from a hook on an app thread and fails from your worker, the
classloader is routing, not the name. If it fails from both, the name or
signature is wrong. Hidden-API enforcement is a property of the Android build you
measured it on — write down which one.

[JNI inside a live app](/ZygiskLab/book/postspecialize/13-jni-inside-a-live-app/)

## The hook never fires

**What you see.** The module loads, the arming line prints, `pltHookCommit`
looked fine, and your hook body never logs.

Work down this list in order; each step separates two hypotheses.

1. **Is this the process you armed for?** No arming line naming this pid means
   nothing else matters.
2. **Did the module load at all?** No `preAppSpecialize` line makes this a
   deployment problem, not a hooking problem — go back to the `md5sum` check.
3. **Did `pltHookCommit()` return `true`?** You logged it; read it. `false` means
   the hook was never installed. Check `findLibc()` succeeded first.
4. **Is the original non-null?** A null original after a successful commit means
   the symbol was registered and no matching import was found — that points at
   the symbol name or the target library, not the mechanism.
5. **Is the symbol actually imported by that ELF?** `llvm-readelf --dyn-syms` and
   look for a `UND` entry. Internal calls, inlined calls, statically linked
   copies and raw syscalls have no PLT slot and never had one. This is the step
   where an empty log turns out to be correct behaviour.
6. **Did the call happen before your commit?** Log a timestamp at commit and
   compare with when the app does the work. Library-init opens happen before you.
7. **Is something else hooking the same slot?** Disable other modules and retest.

**The discriminator between "broken hook" and "wrong target".** Step 5. A missing
`UND` import means you hooked an interface the code does not use; you have
intercepted an interface, not a behaviour. With `committed=true`, a non-null
original, a confirmed `UND` import and still no calls — believe the log. The code
path you assumed exists probably does not run in this app.

[Hooking native symbols](/ZygiskLab/book/postspecialize/14-hooking-native-symbols/)

## `setOption` appears to do nothing

**What you see.** You set an option and the behaviour you expected does not
happen. There is no return value, no errno, no log line.

**Likely causes:**

1. `FORCE_DENYLIST_UNMOUNT` called from `postAppSpecialize`, where specialization
   is already over, or from `onLoad`, where the header does not promise it.
2. Checking for the effect in the same callback that set it — the unmount runs
   *during* specialization, later.
3. The provider does not implement the slot at all. The forwarder null-checks and
   returns, so an unimplemented call is byte-for-byte the same experience as a
   successful one.
4. Expecting it to hide your library. It unmounts mounts; your library is mapped.

**The discriminator.** There is no return value to read, so **the only test is
the effect, measured later and from outside** — the mount namespace for one
option, `/proc/self/maps` for the other. Treat `getFlags() == 0` as *no
information*: it is equally the value for "not on the denylist" and for "the slot
is unimplemented". Never make a decision that is only correct if the process is
definitely off the denylist.

[setOption and flags](/ZygiskLab/book/prespecialize/10-setoption-and-flags/) ·
[Where it breaks](/ZygiskLab/book/companion/20-where-it-breaks/)

## The companion never answers

**What you see.** `connectCompanion()` returns `-1`, or the connection succeeds
and the read never returns, or the app hangs at launch.

**Likely causes:**

1. You called it from `postAppSpecialize`. The header restricts it to
   `pre[XXX]Specialize` because of SELinux — the app domain cannot reach the
   daemon's socket. A more permissive provider does not fix this.
2. The provider never implemented the slot. Same `-1`.
3. The daemon is gone or wedged. Same `-1`.
4. You set no socket timeouts, so a wedged companion is an unbounded `read()` on
   the app's launch path, which the user experiences as the app failing to start.

**The discriminator.** `-1` diagnoses nothing by itself — three unrelated causes
return it. Move the call to `preAppSpecialize` and retest: if it succeeds there,
it was timing. If it still returns `-1` there, it is the provider or the daemon,
and the answer is not in your process. And set `SO_RCVTIMEO`/`SO_SNDTIMEO` before
the first byte moves, so "the companion did not answer" is a line in your trace
rather than a mystery about why the app is slow. The injected side must be able
to complete without the companion.

[Companion protocol](/ZygiskLab/book/companion/18-companion-protocol/) ·
[Where it breaks](/ZygiskLab/book/companion/20-where-it-breaks/)

## The companion answered once, then stopped

**What you see.** Early requests succeed; a later one gets nothing. No crash line
anywhere, because the companion is a different process with its own fate.

**Likely causes:**

1. The companion died handling malformed input — a client-supplied length that
   sized an allocation or drove a read.
2. A framing bug: a message boundary inferred rather than length-prefixed, so one
   side lost its place in the stream and every subsequent read is garbage.
3. Concurrent invocation racing on companion-side mutable state. The header warns
   the handler can run on multiple threads.
4. The connection was dropped on a rejected message rather than the rejection
   being reported.

**The discriminator.** Send a deliberately malformed request, then a valid one
**over the same connection**. A companion that survived bad input answers the
second. A companion that died does not, and that is the only signal you get —
nothing announces it. Make the oversized-length case fail from the header alone,
without the client sending the bytes, so the stream stays in sync and the
connection is still usable afterwards.

[Companion protocol](/ZygiskLab/book/companion/18-companion-protocol/)

## The device bootloops

**What you see.** Boot animation forever, or a reboot loop.

**Likely causes:**

1. A native crash in `system_server`, which is also a zygote child — a crash
   there takes the system down.
2. The `cp`-over-a-mapped-`.so` corruption above, but exercised by zygote itself
   because your module does work in `onLoad` or in every process. Same cause as
   the per-app SIGSEGV, louder symptom.
3. Anything in your module that crashes early and unconditionally.

**Recover first, diagnose second.** In increasing severity: KernelSU's safe mode
(volume-down pressed distinctly more than three times after the first boot
screen; Magisk's equivalent is Core Only Mode); `adb`, if `adbd` came up, using
`ksud module disable <id>` and preferring disable over uninstall so you keep the
evidence; a recovery shell, where `touch /data/adb/modules/<id>/disable` is the
non-destructive move; and reflashing.

**The discriminator**, once booted: the newest tombstone.

```bash
adb shell su -c 'ls -lt /data/tombstones | head'
```

A backtrace frame whose mapped file is your `.so` under `/data/adb/modules` is
the evidence it was you. No such frame, and you are looking at something else.
Then apply the hash check before you believe a tombstone that arrived after a
`cp`-style deploy.

[The rig and toolchain](/ZygiskLab/book/foundations/03-rig-and-toolchain/) ·
[Deploying without bricking zygote](/ZygiskLab/book/load/07-deploying-safely/)

## When nothing above fits

Three habits close most of what is left, and all three are the silent-failure
theme stated as practice.

- **Check the return of every call that has one**, and log the moment an expected
  capability is missing, by name. `connectCompanion` and `getModuleDir` return
  `-1`; `exemptFd` and `pltHookCommit` return `false`; `hookJniNativeMethods`
  writes `nullptr` into the `fnPtr` of every entry it could not hook and returns
  nothing at all, so check the pointer before calling through it.
- **Test the effect, not the return value.** A signature can stay stable while
  its meaning drifts under a provider update, and nothing in the type system will
  tell you.
- **Record the provider and version with every result.** A measurement without
  them is not reproducible. The behaviour of this interface is provider-specific
  wherever this book has not said otherwise.

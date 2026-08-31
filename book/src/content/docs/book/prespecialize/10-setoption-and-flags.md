---
title: "`setOption` and the flags"
description: "FORCE_DENYLIST_UNMOUNT and DLCLOSE_MODULE_LIBRARY: what each flag does, and its effect on your footprint."
sidebar:
  order: 3
status: unverified
---

Two options, one query, and no error reporting anywhere in the group. That is the
whole of the configuration surface a Zygisk module has over its own fate:
`setOption(FORCE_DENYLIST_UNMOUNT)`, `setOption(DLCLOSE_MODULE_LIBRARY)`, and
`getFlags()` to read two bits about the process you landed in. The options are
small, they are set in one line each, and both of them change the process in ways
that are irreversible by the time you would notice a mistake. This chapter is
about getting each of them right at the moment it is legal, and about what
neither of them does — which is the part readers most often get wrong, because a
flag named after a denylist sounds like it hides you, and it does not.

The authority is the vendored header at
`modules/01-hello-zygisk/jni/zygisk.hpp`, `ZYGISK_API_VERSION 5`. Where the
header stops short and the answer depends on which root implementation loaded
you, this chapter says so rather than guessing.

## The two enums, exactly as declared

```cpp
enum Option : int {
    FORCE_DENYLIST_UNMOUNT = 0,
    DLCLOSE_MODULE_LIBRARY = 1,
};

enum StateFlag : uint32_t {
    PROCESS_GRANTED_ROOT = (1u << 0),
    PROCESS_ON_DENYLIST  = (1u << 1),
};
```

There are no other options and no other flags in API 5. If you have read code
that sets a third, it is either a different API version or a different framework.
`setOption` takes one option per call — the header states it explicitly — so two
options mean two calls, not a bitwise or. `getFlags()` returns the two `StateFlag`
values bitwise-or'd together, and you test with `&`.

## The silent-failure problem, stated once

Every `Api` method is an inline forwarder that null-checks its table slot:

```cpp
inline void Api::setOption(Option opt) {
    if (tbl->setOption) tbl->setOption(tbl->impl, opt);
}
inline uint32_t Api::getFlags() {
    return tbl->getFlags ? tbl->getFlags(tbl->impl) : 0;
}
```

[Chapter 5](/ZygiskLab/book/load/05-anatomy-of-a-module/) draws out the general
consequence; here is the specific one. `setOption` returns `void`. It cannot tell
you that the provider does not implement it, that you called it from the wrong
callback, that the option was rejected, or that it worked. A call at a wrong
moment is indistinguishable, from inside your module, from a call at the right
one. There is no errno to inspect and no log line you can rely on being yours.

`getFlags()` is worse in a subtler way, because it returns a plausible value on
failure. A missing slot yields `0`, which is also the perfectly ordinary answer
for "not granted root, not on the denylist". You cannot distinguish "the provider
told me nothing" from "the provider told me both bits are clear". Treat a zero
return as *no information*, not as a negative result, and never make a decision
that only makes sense if the process is definitely off the denylist.

:::caution
Both `setOption` calls are fire-and-forget. The only way to know whether either
took effect is to observe the process afterwards — the mount namespace for one,
`/proc/self/maps` for the other. Neither observation is available from inside
the module in the general case, and both are exactly what
[Part VI](/ZygiskLab/book/footprint/21-your-footprint/) measures from outside.
:::

## `FORCE_DENYLIST_UNMOUNT`

The header's comment is short and worth reading literally:

> Force Magisk's denylist unmount routines to run on this process.
>
> Setting this option only makes sense in `preAppSpecialize`.
> The actual unmounting happens during app process specialization.
>
> Set this option to force all Magisk and modules' files to be unmounted from the
> mount namespace of the process, regardless of the denylist enforcement status.

Three separate facts are packed in there.

**What it unmounts.** The root implementation's own mounts, and its modules'
mounts. A systemless-root framework works by bind-mounting files over the read-only
system partitions — module overlays on `/system`, the root binary's own paths,
whatever module files a given implementation stacks into a process's namespace.
This option asks for those to be removed from *this* process's mount namespace
before the app's code runs. That includes your own module's overlay files, if
your module ships any; the routine does not spare its caller.

**What it does not unmount, and does not hide.** Everything that is not one of
those mounts. It does not remove your library from the process — that is the
other option's job, and even that leaves traces. It does not touch anything the
implementation put in the process by means other than mounting: injected code,
the loader's own resident state, file descriptors, threads, properties, the
`su` binary's presence anywhere outside the unmounted paths. It does not alter
`/proc` in any way that conceals the process's own history. And it says nothing
about what a *different* namespace shows — the app can only look at its own.

Calling this a stealth switch is the single most common misreading of the Zygisk
API. It is a namespace-hygiene call: it makes the mounted filesystem the app sees
look like the unmodified one, and it stops there. What remains observable after
it, and how much of an app's detection surface it actually closes, is the subject
of [Your footprint, stage by stage](/ZygiskLab/book/footprint/21-your-footprint/)
and, for the existing tooling built around the same idea,
[Existing answers, surveyed](/ZygiskLab/book/footprint/23-existing-answers-surveyed/).
This chapter deliberately does not pre-empt them.

**When it takes effect.** Not when you call it. The header is explicit: the call
records an intent, and "the actual unmounting happens during app process
specialization" — after your `preAppSpecialize` returns, as part of the
specialization your callback was interposed before. Two consequences follow. You
cannot set it and then verify it in the same callback, because nothing has
happened yet. And you cannot set it from `postAppSpecialize`, because
specialization is already over; the moment it would have acted on has passed.
That call compiles, runs, returns, and does nothing, with no diagnostic — which
is the silent-failure point above, in its most expensive form.

The phrase "regardless of the denylist enforcement status" is the point of the
word *force*. Your module is asking for the unmount routine on this process even
though the provider's own policy would not have run it here.

## `DLCLOSE_MODULE_LIBRARY`

```cpp
// When this option is set, your module's library will be dlclose-ed after post[XXX]Specialize.
// Be aware that after dlclose-ing your module, all of your code will be unmapped from memory.
// YOU MUST NOT ENABLE THIS OPTION AFTER HOOKING ANY FUNCTIONS IN THE PROCESS.
```

This is the option that makes "decide fast and leave nothing behind" possible. A
module that inspects `AppSpecializeArgs`, concludes this is not its target
process, and sets this option is asking the loader to `dlclose` its `.so` after
the specialize pair completes. Your text, your data, your relocations, your
static constructors' results — all of it is unmapped. The process continues; you
are simply no longer part of it.

### What "unmapped" actually means for your code

`dlclose` on the last reference to a shared object runs its destructors and then
removes its mappings from the address space. After that, the address range where
your `preAppSpecialize` used to live either is unmapped — so touching it faults —
or has been reused by a later `mmap` for something entirely unrelated, in which
case a call into it does not fault but executes whatever now occupies those bytes.
Both outcomes are catastrophic and only one of them is legible in a tombstone.

This is why the header's warning is shouted. A PLT hook, a JNI native method
replacement, a signal handler, an `atexit` registration, a thread whose start
routine is your function, a C++ object with a vtable pointer into your library
held by anything that outlives you: every one of these is a pointer into a region
that is about to stop being yours. The crash arrives later, in an unrelated
thread, with a faulting address that belongs to no loaded module — the code that
faults no longer exists, so the stack has nothing to symbolicate. It is the
hardest class of crash to diagnose in this whole book, and it is entirely
preventable by not setting this option when you have left anything behind.

### The rule, stated as a checklist

Before you set `DLCLOSE_MODULE_LIBRARY`, all of the following must be true.

- You have registered **no** hooks of any kind. Not `hookJniNativeMethods`, not
  `pltHookRegister`/`pltHookCommit`, not any third-party hooking library. The
  header's prohibition is absolute and does not carve out "hooks I intend to
  remove later" — there is no unhook call in this API.
- You have started **no** threads that could still be running, and no thread that
  ever will call back into your code.
- Nothing outside your library holds a pointer to anything inside it — no
  callback registered with the runtime, no JNI global reference to a class whose
  natives are yours, no `pthread_atfork` handler, no `atexit`.
- Any work you actually needed to do is **finished**, not merely started. There is
  no "after". Asynchronous anything is disqualifying.

The natural place for this option is therefore the early-exit path of a module
that has decided this process is not interesting. You read the arguments, you
match or fail to match, and if you fail to match you have by construction done
nothing that leaves a pointer behind — so you set the option and let yourself be
removed. [Choosing not to run](/ZygiskLab/book/prespecialize/11-choosing-not-to-run/)
builds exactly that path and measures what it costs the launches that are not
your target.

The other natural reading — "unload once my hooks are installed, so my library
stops showing up" — is precisely the thing the header forbids, and it does not
work. Your hooks point into your library. Unmapping it does not make the hooks
disappear; it makes them dangerous.

:::danger
There is no partial version of this. You cannot unload "most" of your library, and
you cannot arrange for a small trampoline to survive. If any of your code must run
after specialization, this option is not available to you, and the footprint your
resident library leaves is a cost you accept and manage rather than eliminate.
:::

### Timing

The header says the `dlclose` happens "after `post[XXX]Specialize`", so both
`preAppSpecialize` and `postAppSpecialize` are before the deadline, and setting it
from either is within the letter of the header. Setting it in `preAppSpecialize`
is the honest expression of an early-exit decision: you have decided, and the rest
of your callbacks will do nothing anyway. Setting it in `postAppSpecialize` is
legal but tends to mean you are unloading after doing work, which is the case in
which the checklist above is hardest to satisfy.

Note the interaction with the `Api` lifetime rather than fighting it. Zygisk
unloads itself from the specialized process after `post[XXX]Specialize`
regardless of this option; what the option adds is that *your* library goes too.
[How the loader finds you](/ZygiskLab/book/load/06-how-the-loader-finds-you/)
covers the loader side of that.

## The flags, and the provider's denylist

`getFlags()` answers two questions about the current process:
`PROCESS_GRANTED_ROOT` — the user has granted root access to this process — and
`PROCESS_ON_DENYLIST` — this process was added to the denylist.

Here is the distinction that matters, and it is a distinction between two
different actors.

**What your module asks for** is `FORCE_DENYLIST_UNMOUNT`. It is a request, from
your module, on this one process, made at one moment.

**What the provider decides** is everything else: which processes are on the
denylist at all, whether denylist enforcement is on, whether being on the list
implies unmounting or implies not loading Zygisk into the process in the first
place, whether a module scope or exclusion list applies before your code ever
runs, and what its unmount routine actually covers. None of that is in the
header. All of it belongs to the root implementation.

That leads to a consequence worth stating plainly: **`PROCESS_ON_DENYLIST` being
set does not tell you that unmounting has happened or will happen**, and it being
clear does not tell you that it will not. The bit reports list membership as the
provider models it. The header does not promise a relationship between that bit
and the unmount routine, and `FORCE_DENYLIST_UNMOUNT` exists precisely because
the two can diverge — the word "regardless" in its comment says so.

Nor can you assume the reverse direction. If the provider's policy is to not load
modules into denylisted processes at all, your callback never runs there and you
never see the bit set, which looks identical to an empty denylist.

:::caution
Magisk and Zygisk Next are different implementations of this API and this book
does not claim they behave identically here. The reference rig is a Pixel 6 Pro,
Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5, and nothing in this
chapter has been run on it. Provider-specific behaviour — what the denylist means
to each implementation, what its unmount routine covers, whether modules load into
listed processes — is collected in
[Where it breaks](/ZygiskLab/book/companion/20-where-it-breaks/) and surveyed in
[Existing answers, surveyed](/ZygiskLab/book/footprint/23-existing-answers-surveyed/).
Verify on your own rig before you depend on any of it.
:::

Practically, use `getFlags()` as a hint that shapes behaviour you would be
comfortable with either way, never as a precondition for something unsafe. A
reasonable use is deciding whether to bother with expensive work in a process the
user has explicitly granted root to. An unreasonable one is skipping a safety
check because the bit said the process was denylisted.

## What each option does to your footprint

Both options change what an app can observe about itself, in different places.

`FORCE_DENYLIST_UNMOUNT` changes the **mount namespace**. After it runs, the
paths that the root implementation and its modules had bind-mounted are gone from
this process's view, so an app parsing `/proc/self/mountinfo` or stat-ing known
module paths sees a cleaner picture than it otherwise would. It changes nothing
else, and mount-namespace inspection is only one of several things an app looks
at.

`DLCLOSE_MODULE_LIBRARY` changes **`/proc/self/maps`**. Your `.so` stops being a
mapped region with a path, an inode and a device — which is the most direct
evidence of your existence that a process can gather about itself, and the exact
evidence `pltHookRegister`'s own documentation describes using. Unloading removes
that. It does not remove whatever else your having been there caused.

Neither option is a footprint solution and this chapter will not present them as
one. What actually remains after both — in maps, in the namespace, in fds,
in timing, and in the loader's own resident state — is the whole subject of
[Your footprint, stage by stage](/ZygiskLab/book/footprint/21-your-footprint/).
Take the mechanism from here and the measurement from there.

## Failure catalogue

The value of this chapter is the list you come back to. Every entry below
produces no error message.

| What you do | What happens |
|---|---|
| `FORCE_DENYLIST_UNMOUNT` in `postAppSpecialize` | Nothing. Specialization is over. |
| `FORCE_DENYLIST_UNMOUNT` in `onLoad` | Header says it only makes sense in `preAppSpecialize`; do not rely on it. |
| Setting the option, then checking the namespace in the same callback | Unchanged — the unmount runs during specialization, later. |
| Expecting it to hide your library | It unmounts mounts. Your library is mapped, not mounted. |
| `DLCLOSE_MODULE_LIBRARY` after any hook | Deferred crash at an address that no longer belongs to a loaded module. |
| `DLCLOSE_MODULE_LIBRARY` with a live thread of yours | Same, on that thread, at an arbitrary later time. |
| Two options in one call via `|` | Not supported. One option per call. |
| Treating `getFlags() == 0` as "clean process" | Ambiguous: also the value when the slot is unimplemented. |
| Inferring unmount status from `PROCESS_ON_DENYLIST` | Not promised by the header. Provider-specific. |
| Calling either through a stale `Api *` after specialization | Silently does nothing; the pointer stays valid and useless. |

The one thing you cannot learn from the header is what your provider does with
these requests. That is a measurement, it belongs on your own rig, and until you
have made it you should write code that is correct whether or not the option took
effect.

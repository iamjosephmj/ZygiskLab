---
title: "Where it breaks"
description: "exemptFd and connectCompanion failure modes, provider differences, version drift, and designs that look obvious but fail."
sidebar:
  order: 4
status: unverified
---

Everything up to here has described how Zygisk works. This chapter describes
where the description stops being reliable, and it is the one the rest of the
book keeps deferring to. Chapters 1, 5, 9, 10 and Lab 3 all reach a point where
the honest answer is "that depends on your provider and your provider's
version", and each of them sends you here rather than guessing. So this chapter
has to do two things: name the specific places the interface is weaker than it
looks, and model the discipline that makes a module survive them.

That discipline is one sentence. **The header is a description of an interface,
not a promise about an implementation.** Every method on `Api` is an inline
forwarder over a function pointer that a provider may or may not have filled in,
and the forwarder returns a failure value rather than telling you which of half
a dozen things went wrong. There is no version of "it worked on my rig" that is
evidence about anybody else's.

## `exemptFd` has two independent failure modes

`exemptFd` is the API method whose own documentation admits it cannot be
verified. Here is the comment, from `zygisk.hpp`:

```cpp
// Exempt the provided file descriptor from being automatically closed.
//
// This API only make sense in preAppSpecialize; calling this method in any other
// situation is either a no-op (returns true) or an error (returns false).
//
// When false is returned, the provided file descriptor will eventually be closed
// by zygote.
bool exemptFd(int fd);
```

Read the middle sentence again. Calling it in the wrong place is *either* a
no-op that returns `true` *or* an error that returns `false`. The header
declines to say which, and it is not being vague — the two outcomes come from
different code paths inside the provider and both are legal. The consequence is
sharp: **`true` does not prove your descriptor was exempted.** It proves the
provider did not report an error, which is a weaker statement than it appears,
because "did nothing at all and said so cheerfully" is inside the set of things
`true` is documented to cover. There is no follow-up call that asks "is this fd
exempt?". The only proof available is to hold the descriptor across the boundary
and try to use it in `postAppSpecialize` — and a failure then is
indistinguishable from a dozen other reasons a read might fail.

The second failure mode is the one that matters more, and it is not about
ambiguity. It is about the call simply not working.

:::caution[Observed on the reference rig]
On the reference rig — Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0,
Zygisk Next 1.4.5 — `exemptFd` has been observed returning `false`. On that
setup you cannot rely on carrying a file descriptor across specialization at
all. This is the author's observation on one rig with one provider version; it
is not a claim about Zygisk Next in general, and it is certainly not a claim
about Magisk. It is a claim about what you must not assume.
:::

Recall the forwarder from
[Chapter 5](/ZygiskLab/book/load/05-anatomy-of-a-module/):

```cpp
inline bool Api::exemptFd(int fd) {
    return tbl->exemptFd != nullptr && tbl->exemptFd(fd);
}
```

A `false` here has at least three causes you cannot distinguish: the provider
left the slot null, the provider implemented it and refused this descriptor, or
you called it outside `preAppSpecialize`. One return value, three worlds. The
only branch you can safely write is "assume the descriptor will be closed".

The design consequence is the important part, and it is why Part V is shaped the
way it is. If a descriptor cannot be relied on to cross the boundary, then
**there is no reliable post-specialize root channel obtained by smuggling
something through.** You cannot open the companion socket before specialization,
exempt it, and keep talking to root as the app. You cannot open a root-owned log
file, exempt the fd, and append to it afterwards. Both designs are correct in
principle and both rest on `exemptFd` succeeding and staying succeeded, which the
header does not promise and the rig does not deliver. That is precisely why
[Chapter 19](/ZygiskLab/book/companion/19-asymmetry-of-privilege/) builds live
status out of two independent writers joined on the root side, instead of the
obvious single-reporter design. The two-writer architecture is not elegance; it
is what remains after this method is taken away.

:::note
If `exemptFd` returns `true` on your provider and your descriptor really does
survive, that is a fine thing to use — for a *diagnostic*. It is not a fine
thing to make load-bearing, because the next provider update is entitled to take
it back and will not tell you.
:::

## The companion cannot be reached late

`connectCompanion` is unambiguous where `exemptFd` is not, and the sentence to
hold is in the header:

> This API only works in the pre\[XXX\]Specialize methods due to SELinux
> restrictions.

Two things are worth extracting from that. First, the restriction is attributed
to SELinux, not to provider policy. That means it is not a rule someone chose to
enforce and might relax; it is a consequence of the app domain your process
enters at specialization not being permitted to connect to the socket the root
daemon listens on. A more permissive provider does not fix it. Second, the
failure is the usual one: `connectCompanion()` returns `-1`, exactly as it does
when the provider never implemented it, exactly as it does when the daemon is
gone. Same value, no diagnosis.

The design consequence is a timing rule rather than an error-handling rule:
**the decision about whether you need the companion has to be made before you
become the app.** By `postAppSpecialize` it is too late to discover that you
need root. Everything you might want from root — a file only root can read, a
property only root can set, a fact about the system your sandbox cannot see —
must either be fetched pre-specialize, or requested over a socket you opened
pre-specialize and held. And the second half of that sentence brings you
straight back to `exemptFd`, which is why it is worth being blunt: on a provider
where `exemptFd` does not work, the companion is a *pre-specialize-only*
facility. You ask your questions while you are still zygote, or you do not ask
them.

That is not as limiting as it sounds, because `preAppSpecialize` already knows
which process it is in — you have `nice_name`, `uid` and `app_data_dir` from
`AppSpecializeArgs`
([Chapter 9](/ZygiskLab/book/prespecialize/09-reading-appspecializeargs/)). You
have enough to decide. What you do not have is a second chance.

## Provider differences that change your design

[Chapter 1](/ZygiskLab/book/foundations/01-what-zygisk-is/) promised that
"same API" is not "same behaviour" and sent the detail here. Here is the detail,
with a standing caveat that governs the whole section: **this book's rig runs
Zygisk Next on KernelSU-Next, and Magisk's built-in Zygisk has not been measured
for it.** Where the two are described as differing, treat that as a statement
about *categories you must check*, not a table of results you may copy.

Five categories actually change architecture. Everything else is trivia.

**Which processes get injected at all.** A provider decides where your module is
loaded, and providers do not agree. The header does not promise you will be
loaded into `system_server`, and it does not promise that every process of a
package you scoped is a process you land in. This is the question
[Chapter 5](/ZygiskLab/book/load/05-anatomy-of-a-module/) and
[Chapter 9](/ZygiskLab/book/prespecialize/09-reading-appspecializeargs/) both
deferred: whether an app's `:remote` service, its child zygote, or its
`system_server` gets you is a property of the provider's scope model. Design so
that being loaded in an unexpected process is harmless and not being loaded in
an expected one is visible — a log line per `preAppSpecialize` naming the process
is the cheapest instrument in this book, and the only way to answer the question
for your own device.

**Denylist semantics, and who enforces them.** `StateFlag::PROCESS_ON_DENYLIST`
and `Option::FORCE_DENYLIST_UNMOUNT` are named after Magisk's feature, and the
header's own comment for the option says "Force Magisk's denylist unmount
routines to run on this process". A different provider implements something with
the same name that is not necessarily the same thing: what the list means (do
not load, or load and unmount), what its unmount routine covers, and whether a
listed process is one your module runs in at all are three separate decisions
that the interface does not fix.
[Chapter 10](/ZygiskLab/book/prespecialize/10-setoption-and-flags/) sent you here
for exactly this and the answer is the uncomfortable one: you must determine it
on your target provider. Treat a nonzero `getFlags()` bit as a hint you would be
comfortable ignoring, never as a precondition.

**Module directory layout and what the provider manages.** Both families put
modules under `/data/adb/modules/<id>/`, and your `.so` lands in a
`zygisk/<abi>.so` layout the loader knows how to find
([Chapter 6](/ZygiskLab/book/load/06-how-the-loader-finds-you/)). What differs is
everything around it: what the provider writes into that directory itself, how
updates are staged, what an "update" directory means, whether the provider hides
the tree from unprivileged processes, and what SELinux label the directory
carries — which the header explicitly makes your problem, since `getModuleDir`
notes that zygote must be *allowed to read* the module dir for the fd to be
usable over a socket. Do not hard-code paths you derived by looking at one
device, and do not write into your own module directory expecting the provider
to leave it alone.

**Companion behaviour.** The header describes the companion as a root daemon
process, ABI-matched to the caller, whose handler "can run concurrently on
multiple threads". That is the contract. How many companion processes exist, when
they are spawned, how long they live after the last client disconnects, and what
happens to one when the module is updated in place are implementation choices.
Write the companion so it does not care: no assumption of being a singleton
across time, no state that must survive between connections, and locking around
anything global because the header warns you it is concurrent.

**Whether an API is implemented at all — and the silence when it is not.** This
is the category that catches people, because it does not look like a category. A
provider that never implemented a method leaves its `api_table` slot null, every
forwarder null-checks its slot, and you get `-1`, `false`, `0`, or nothing at
all. `setOption`, `hookJniNativeMethods` and `pltHookRegister` return `void`:
on a provider that does not implement them, calling them is *completely
indistinguishable from calling them successfully*. That is the mechanism behind
Lab 3's negative control — if an unarmed process still reaches
`postAppSpecialize`, `DLCLOSE_MODULE_LIBRARY` was not honoured, and no return
value anywhere told you so. The only signal is the behaviour you went looking
for.

### Detecting which provider you are on

The honest answer first: **you cannot always tell cleanly, and you usually should
not try.**

Providers that make an effort to hide from apps are, unavoidably, also hiding
from you — you are running inside an app process, and a check that reliably
identifies your provider from inside an app sandbox is, by construction, a
detection technique for the thing your provider is trying to conceal. That is
[Part VI](/ZygiskLab/book/footprint/21-your-footprint/)'s subject and it is
studied there defensively. It is a bad foundation for a feature.

What you can do soundly, in rough order of preference:

1. **Design so you do not care.** Check the return of every call, log what is
   missing, degrade to something that still makes sense. A module that works on
   any provider that implements what it uses is better than one that branches on
   provider identity, and it is shorter.
2. **Ask from the companion, not from the app.** The companion is root and can
   read `/data/adb/` — the provider's own directories, its version metadata, its
   databases. If you genuinely need provider identity, that is where the
   question is answerable, and the answer crosses to the injected side over the
   socket you already have. This is the same shape as every other privileged
   question in this book.
3. **Record it out of band.** Your installer script runs as root at install time
   and can write what it found into your module's own directory. A fact captured
   then is more trustworthy than one sniffed at runtime, and it costs the
   injected process nothing.
4. **Probe capabilities, not identity.** "Did `exemptFd` return true?" is a
   question with an actionable answer. "Am I on Magisk?" is a question whose
   answer you would only use to guess at the first one.

## Version drift

Your module is compiled against a header. It runs against an implementation that
updates independently, usually without your knowledge, sometimes overnight
because a user tapped update in a module manager. Three things drift.

**The API version.** `entry_impl` calls `registerModule` and returns immediately
if the provider refuses your `ZYGISK_API_VERSION`. `onLoad` is never called, no
callback fires, and your module logs nothing — because it never ran. From your
logcat filter this is identical to shipping the wrong ABI, to a `dlopen` failure,
and to not being installed. Widen the filter and read what the *loader* says
about itself.

**Implemented-ness.** A method that worked can go away, or arrive. The failure is
silent by construction, per the section above.

**Semantics under a stable signature.** The nastiest kind: the call still
returns `true`, and means something slightly different. Nothing in the type
system will tell you. Only a test that checks the *effect* rather than the return
value will.

The discipline that survives all three is the same, and it is the silent-failure
theme of this whole book stated as practice:

- **Check the return of every call that has one.** `connectCompanion` and
  `getModuleDir` return `-1`. `exemptFd` and `pltHookCommit` return `false`.
  `hookJniNativeMethods` writes `nullptr` into a `JNINativeMethod`'s `fnPtr` for
  every entry it could not hook, and has no return value at all — so check the
  `fnPtr` before you ever call through it.
- **Log when an expected capability is missing,** at the moment you notice, with
  the name of the thing that is missing. `getModuleDir() == -1` deserves a line
  that says so.
- **Assert your preconditions where they can still be reported.** A module that
  needs the companion should discover that in `preAppSpecialize`, where it can
  still say something.
- **Prefer a module that announces it cannot work over one that quietly does
  nothing.** A loud refusal is a bug report. A silent no-op is an afternoon.
- **Record the provider and version with every result you write down.** A
  measurement without them is not reproducible and is not worth keeping.

## A catalogue of designs that look obvious and do not work

Each entry is the design, why it looks right, and the mechanism that defeats it.

**Report status from the injected process to a root-owned location.** Looks
right because the module's own directory is where module state belongs and you
know the path. Defeated by the fact that after specialization you are the app:
`/data/adb/` is root-owned and SELinux-restricted, and the provider may have
removed it from your namespace entirely, so you get `ENOENT` rather than
`EACCES` and misdiagnose it. The injected side writes to the app's own cache
directory; something with root collects it
([Chapter 12](/ZygiskLab/book/postspecialize/12-what-changed-at-boundary/)).

**Carry a descriptor across specialization.** Looks right because `exemptFd`
exists for precisely this. Defeated twice over: a `true` return is documented to
include the no-op case, so success is unprovable; and on the reference rig the
call has been observed returning `false` outright. Design as if the descriptor
will be closed.

**Connect to the companion after specialization.** Looks right because the socket
API is still there and the `Api` pointer is still valid. Defeated by SELinux —
the app domain cannot reach the daemon's socket — and reported as `-1`, the same
`-1` you would get if the companion did not exist. Connect before, or not at all.

**Share state between the injected side and the companion through globals.**
Looks right because both halves are in one `.so`, one source file, one build.
Defeated by them being different *processes*: the companion was forked from the
root daemon, not from your app. A global written on one side is invisible on the
other. The socket is the only channel
([Chapter 17](/ZygiskLab/book/companion/17-the-companion-process/)).

**Assume module state survives a fork.** Looks right because it survived on your
test app. Defeated by every app process being a separate fork of zygote with its
own copy of everything: your counters start again, your cached handles refer to
this process only, and two injected apps share your `.so` on disk and nothing
else ([Chapter 5](/ZygiskLab/book/load/05-anatomy-of-a-module/)).

**Assume a PLT hook sees calls internal to a library.** Looks right because you
hooked a file-opening symbol and the library opens files. Defeated by the PLT
only mediating *imports*: a call from a library to its own function, an inlined
call, a statically linked copy, or a raw syscall never touches a PLT stub and
your hook never fires. You have intercepted an interface, not a behaviour. This
book walked into it: the earlier Lab 4 hooked `openat` on the argument that
bionic's `open()` calls it, when that call is internal to libc and crosses no
PLT ([Chapter 14](/ZygiskLab/book/postspecialize/14-hooking-native-symbols/)).

**Assume `setOption` reports failure.** Looks right because a function that
configures something ought to tell you whether it configured it. Defeated by the
signature: it returns `void`, and the forwarder skips the call entirely when the
slot is null. An unimplemented, ignored or invalid `setOption` is byte-for-byte
the same experience as a successful one. The only test is the effect
([Chapter 10](/ZygiskLab/book/prespecialize/10-setoption-and-flags/)).

**Detect your provider from inside the app process.** Looks right because you
just need one branch. Defeated by the provider actively hiding what you would
look for, by the answer changing at the next update, and by the check itself
being a detection technique aimed at the thing protecting you. Ask from the
companion, or design so the branch is unnecessary.

## What remains unknown until you run it

This chapter's own claims deserve the treatment it has been asking you to give
everyone else's. The mechanisms are sourced: the `exemptFd` ambiguity, the
pre-specialize-only restriction on `connectCompanion` and `getModuleDir`, and the
null-check in every forwarder are all in `zygisk.hpp` and are not going to change
under you. The single behavioural observation — `exemptFd` returning `false` — is
the author's, on one rig, on Zygisk Next 1.4.5, and generalises no further than
that sentence.

Everything in the provider-differences section is a list of questions, not a list
of answers. Which of your target's processes you are actually loaded into,
whether `DLCLOSE_MODULE_LIBRARY` is honoured, what the denylist means and what
its unmount covers, whether `exemptFd` works — those are four small experiments
on your device, each producing a fact worth more than this chapter. Write them
down with the provider name, the provider version, and the Android build, because
the fact has a shelf life and those three fields are how you will know it has
expired.

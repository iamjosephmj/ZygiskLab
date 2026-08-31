---
title: "Hooking Java through ART"
description: "ART method hooking surveyed honestly: what it does, why it is fragile, and when to choose it over a native hook."
sidebar:
  order: 4
status: unverified
---

Chapter 14 gave you a hook that works because it targets something specified. The
PLT is part of the ELF ABI; the dynamic linker's behaviour when it resolves an
imported symbol is documented, stable across releases, and shared by every
process on the device. You wrote a hook against a contract.

This chapter is about the case where the thing you want has no such contract.
The behaviour you care about is a Java method. It never crosses a native
boundary you can reach, there is no imported symbol standing between the caller
and the callee, and the only place the call is decided is inside the Android
Runtime's own bookkeeping. To intercept it you have to reach into that
bookkeeping, and the moment you do, you are relying on the internal layout of a
runtime that has never promised you anything.

This chapter is a survey, and deliberately so. It explains the mechanism, walks
the families of approach, points you at the projects that have solved this
properly, and tells you what taking that dependency costs. It does not give you
a working implementation, and you should be suspicious of any short treatment
that claims to. A correct ART hooking library is years of work spread across
every Android release since 5.0, and the honest thing a chapter this length can
do is help you decide whether you want to be in that business at all.

## What an ART hook actually does

Every Java method in a running app is described, inside ART, by a runtime
structure conventionally called `ArtMethod`. It is not a Java object. It is the
runtime's own record of the method: which class declares it, its access flags,
its shorty and signature information, and — the part that matters here — where
its executable code begins.

That last field is the hinge. When ART dispatches a call, it does not consult
the dex bytecode afresh each time. It arranges for control to reach whatever
address the method's entry point currently designates. That address may point at
compiled native code produced ahead of time or by the JIT, or at a generic
interpreter bridge that will walk the bytecode, or at a resolution stub that
will fill in the real answer on first call. The runtime rewrites this field
itself, routinely, as a method moves between those states. It is a mutable
pointer that the runtime treats as authoritative.

An ART hook exploits exactly that. If you can locate a target method's
`ArtMethod` and change where its entry point leads, then every subsequent
dispatch of that method arrives at your code instead. That is the entire idea.
Everything else — how you get an `ArtMethod` from a `jmethodID`, how you build
something that can stand in for a method of arbitrary signature, how you leave a
usable route back to the original implementation — is engineering in service of
that one substitution.

Three consequences follow immediately from the fact that this is a runtime
structure and not an API.

**The layout is not public.** `ArtMethod` lives in ART's own source. It has no
header you can include from the NDK, no stability guarantee, and no
documentation aimed at you. Its fields, their order, and the size of the whole
structure are implementation details of a component that is free to change them.

**It differs between Android versions.** ART is developed continuously. Fields
are added, removed, reordered and repurposed across releases, and the mechanics
around the entry point have changed shape more than once in the runtime's
history. Code that reads a fixed offset out of `ArtMethod` is code written
against one version of one component.

**It can differ between builds of the same version.** This is the part people
underestimate. A vendor ships its own ART build. A structure's layout can be
affected by compile-time configuration, and since Project Mainline, ART itself
is updatable — the runtime on a device is not necessarily the one that shipped
with its platform release. "Android 14" is not a single ABI for this purpose. It
is a family of builds that mostly agree.

:::caution
This is why you will not find `ArtMethod` field offsets in this chapter. Any
specific offset I wrote down would be true for some builds, false for others,
and unverifiable by you without reading ART's source for your exact runtime.
Serious implementations do not hardcode offsets either; they derive them at
runtime, which is a technique discussed below and a large part of why those
projects are as big as they are.
:::

## Why this is the most fragile technique in the book

Set the two hooks side by side.

| | PLT hook (Ch. 14) | ART method hook |
|---|---|---|
| Target | ELF dynamic linkage | ART-internal structure |
| Specified? | Yes, by the ABI | No |
| Stable across releases | Yes | No |
| Varies by vendor build | No | Possibly |
| Breaks on OS update | Rarely | Assume yes |

The PLT hook targets an interface that exists precisely so that independent
pieces of software can agree on how calls are made. Its stability is the whole
point of it. The ART hook targets a structure that exists so the runtime can
manage itself, and whose only consumer is supposed to be the runtime.

Say the consequence plainly: **a module that relies on ART hooking acquires a
maintenance obligation on every Android release, and on every ART mainline
update in between.** Not a risk of breakage — an obligation. Someone has to test
against each new runtime, work out what moved, and ship a fix. If nobody is
going to do that work, the module will silently stop functioning on devices you
never see, and the failure mode is not a clean error. It is a hook that quietly
does not fire, or a process that crashes inside the runtime with a stack that
implicates ART rather than you.

There is a second cost that is easy to miss. ART hooking is a *whole-runtime*
intervention even when your interest is one method. To install a hook safely you
generally have to interact with the runtime's own machinery — its handling of
compiled versus interpreted execution, its assumptions about when method
metadata may change, its threading model. The blast radius of getting it wrong
is not your hook. It is the app.

## The families of approach

Two broad strategies have emerged, and most real implementations combine them.

### Entry-point replacement

Point the target method's entry at code of your own, so that dispatch lands on
you. This is the direct expression of the mechanism described above, and it is
the strategy the Xposed lineage has always been built on in one form or another.

Its appeal is that it works with the runtime's dispatch rather than against it:
you are setting a field the runtime itself sets. The difficulty is everything
around it. You need something to point *at* — a stand-in that can receive a call
with the target's exact signature and calling convention, marshal the arguments,
hand them to your handler, and return correctly. You need a route back to the
original implementation, which usually means preserving the original method's
description before you overwrite anything, so the untouched code can still be
invoked. And you need to satisfy the runtime's own consistency expectations
about a method whose state you have just changed underneath it.

### Inline and trampoline hooking

Patch the compiled code itself: write a jump at the head of the target's native
code so execution diverts before the body runs. This is the same technique as
native inline hooking, applied to code that ART happened to compile.

It reaches things entry-point replacement can miss, because it acts on the
instruction stream rather than on the dispatch record. It is also more invasive.
You are rewriting executable memory, which means instruction cache coherence,
memory protection changes, position-dependent instructions that cannot simply be
relocated, and correctness on every architecture you support. It is exactly as
delicate as native inline hooking, with a runtime that may recompile or move the
code you patched.

### The problem both must solve: inlining

Neither approach helps if the call never happens. ART's optimising compiler
inlines small methods, and an inlined callee has no separate dispatch to
intercept and no separate entry point to redirect — the caller simply contains
the callee's work. YAHFA's README names this directly as a failure mode, and
notes that it shows up when the compiler has hardcoded values it would otherwise
have read from the `ArtMethod` entry point field.

The countermeasure is **deoptimisation**: persuading the runtime to abandon the
optimised compiled form and execute through the interpreter or a non-inlined
path, so a real dispatch to your hooked method occurs again. LSPlant's own
README lists inline deoptimisation as a first-class feature and warns that
deoptimisation may be needed when a "hooked callee [is] not being called because
of inline." If you had not known to look for this, your first ART hook would
appear to install cleanly and never fire, and you would spend a long time
debugging the wrong thing.

## The implementations worth knowing

These projects solve a genuinely hard problem, and the reason they are large is
that the problem is large. Treat each project's own repository as the authority
on how it currently works — the summaries below are drawn from their READMEs as
of this writing, and internal designs move.

**LSPosed** is the Xposed-compatible framework most readers will encounter
first. Its README describes it as a Riru or Zygisk module providing an ART
hooking framework with APIs consistent with the original Xposed, "leveraging
LSPlant hooking framework", with YAHFA and SandHook named as historical
alternatives. Two things are worth noticing. First, LSPosed is a Zygisk module —
the same delivery mechanism this book has been teaching — which means the
techniques in Parts II and III are the substrate it stands on. Second, it
separates the framework from the hooking engine, which is the design lesson to
take away.

**LSPlant** is that engine, extracted. Its README describes it as "an Android
ART hook library, providing Java method hook/unhook and inline deoptimization",
supporting Android 5.0 through 17 (API 21–37) on armeabi-v7a, arm64-v8a, x86,
x86-64 and riscv64. That version span is the honest measure of what this problem
costs: a decade of runtime releases, each of which had to be understood. Its
documented interface has you initialise inside `JNI_OnLoad` so it can prefetch
the runtime symbols it needs, then hook a target method with a hooker object and
callback, receiving a backup method to call the original. It is LGPL-3.0, which
is a licensing fact you must weigh before linking it into anything you ship.

**Pine** describes itself as "a dynamic java method hook framework on ART
runtime, which can intercept almost all java method calls in the current
process", supporting Android 4.4 (ART only) through Android 15 on thumb-2 and
arm64. Its README is unusually candid about limits, and the candour is
instructive: it may not be compatible with some devices or systems, arguments
may be wrong on Android 6.0 arm32/thumb-2, it disables the hidden-API
restriction policy on Android 9.0+, high-concurrency hooking is discouraged in
favour of hooking a synchronised wrapper, and the library must be initialised
before other threads start to avoid a crash tied to an ART bug around
hidden-API policy changes. Read that list again as a description of the
technique rather than of one project. Those are the shapes of problem this
approach has.

**YAHFA** is the earlier, deliberately minimal member of this line — "a hook
framework for Android ART", offering `backupAndHook(target, hook, backup)` with
static hook and backup methods matching the target's signature. Its stated
support runs API 21–30 plus an Android 12 developer preview, with support for
6.0 and below noted as broken after a specific commit. It remains the clearest
small artefact to read if you want to understand the backup-and-replace shape
without a large codebase around it, and its version ceiling illustrates the
maintenance point better than any argument could.

**Whale** is the odd one out and useful for exactly that reason. Its README
describes a cross-platform inline hook framework running on Android, iOS, Linux
and macOS across ARM/THUMB, ARM64, x86 and x86-64, converting PC-relative
instructions to PC-independent ones and carrying a built-in JIT engine to
generate instructions in memory, with Xposed-style method hooking listed among
its Android capabilities. It shows the inline-hooking route reaching Java
methods from underneath, rather than through the runtime's dispatch record.

:::note
Notice how much of what is stated above is a version range. Every one of these
projects publishes one, every range has an upper bound, and the bound is the
date someone last did the work. That is the maintenance obligation made visible.
:::

## The artifact-is-mapped hazard, in Java

Some Java interception routes do not hook at all. They rewrite: patch the dex,
or replace a compiled artifact on disk, and let the runtime load your version.
When you reach for that, you meet a rule this book has already given you.

Chapter 7 was about what happens when you `cp` a new `.so` over a path that
zygote currently has mapped: the copy reuses the inode, the bytes underneath a
live mapping change, and the running process ends up executing a mixture of two
programs. The chapter promised you that this rule would return in a form that
looks nothing like a shared library. This is it.

A dex file, and the compiled artifacts ART derives from it, are mapped by the
app process in exactly the same sense. The runtime opens them and maps them;
pages are demand-faulted from the file for as long as the mapping lives.
Overwrite one of those files in place while the app is running and you have made
the identical mistake. Offsets shift, structures the runtime already parsed no
longer describe what is at those bytes, and during the window in which the file
is truncated but not yet rewritten, faulting a page past its new end raises
SIGBUS. The runtime is executing from an artifact that has been replaced beneath
it, and the crash will implicate ART, not you.

The general rule holds, and so does the mitigation: **never write an artifact
that is currently mapped.** In its Java form:

```bash
adb shell am force-stop <package>
# nothing has the artifact mapped now; replace it
adb shell su -M -c 'mv /data/local/tmp/staged.dex <destination>'
```

Force-stopping is the Java-side equivalent of the reboot in Chapter 7 — it ends
every process holding the mapping, so the next start maps your file rather than
patching over one in use. And `mv` still beats `cp` for the same reason it did
then: `rename(2)` rebinds a name to a different inode instead of writing into
one that something may still hold open.

Two situations, one about a `.so` in zygote and one about a dex in an app, with
nothing in common at the level of what you are doing — and the same rule decides
both. That is what makes it a rule worth having rather than a piece of trivia
about shared libraries.

## Choosing: when this is the right tool

Work down this list. Stop at the first line that fits.

**Do you control the app?** Then instrument it directly. A build flag, a debug
variant, an interface you add on purpose — anything you can put in the source is
stabler than anything you can do from outside it, survives OS updates without
your involvement, and is comprehensible to whoever reads the code next. Reaching
for a runtime hook against code you own is a choice to make your own system
harder to maintain.

**Does the behaviour cross a native boundary?** Then hook there. If the
information you need passes through a `libc` call, a `libbinder` transaction, or
any imported symbol, Chapter 14's PLT hook gets it against a specified interface
that will still be specified in three Android versions. Look hard for such a
chokepoint before concluding there is none; a surprising amount of interesting
Java behaviour eventually calls something native, and the native side is usually
a cleaner observation point anyway.

**Is the behaviour purely Java, in an app you do not control, with no lower
chokepoint?** Then ART hooking earns its cost, and it is the only thing that
does. This is a real category — app-internal logic that never leaves the
runtime — and if that is where you are, the technique exists for you.

If you land there, one more decision. Do not write your own. Use a maintained
library — LSPlant is the obvious candidate, subject to its LGPL-3.0 terms — and
inherit someone else's version-compatibility work rather than starting it from
zero. Writing your own is justified when you have a research reason to
understand the mechanism, not when you have a method to intercept.

And whichever you choose, budget the obligation up front. Pin the Android
versions you claim to support, test on each, and say in your README what happens
outside that set. Every project surveyed above publishes a version range. That
is not modesty. It is the only honest thing you can publish about a technique
built on internals nobody promised would hold still.

:::caution[What is not verified here]
Nothing in this chapter has been run on the rig. The mechanism described is
drawn from public documentation and the surveyed projects' own READMEs, and the
project details are as those READMEs stated them at the time of writing. Before
you rely on any of it, read the source of the library you intend to use, against
the Android version you intend to support.
:::

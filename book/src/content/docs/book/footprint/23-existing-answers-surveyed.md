---
title: "Existing answers, surveyed"
description: "The provider denylist, Shamiko, and Zygisk Next's own footprint choices, each assessed for what it actually removes."
sidebar:
  order: 3
status: unverified
---

:::caution[Detection and measurement]
This chapter covers detection mechanisms and measurement on systems you
own or are authorised to assess. See [Rules of engagement](/ZygiskLab/book/foundations/02-rules-of-engagement/).
:::

Chapter 21 gave you an inventory of what you leave behind and Chapter 22 gave you
the app's side of the same picture. The obvious next question is whether someone
has already solved it, and the ecosystem has an answer ready: a denylist, a flag,
a module everyone recommends, and a provider that markets itself partly on being
harder to spot. This chapter takes each of those seriously enough to say what it
actually addresses.

It is a survey, in the same sense Chapter 15 was a survey of ART hooking
implementations. It describes what published solutions set out to do, in their
own terms, and what they leave untouched. It does not chain them together into a
configuration, it does not tell you how to maximise concealment, and it is not
aimed at any particular app's checks. The value here is diagnostic: when you
observe a trace in Chapter 22's terms, this chapter tells you which of these
things could plausibly be responsible for its absence, and — far more often —
which of them was never going to touch it.

One discipline runs through the whole chapter and you should read it as the
thesis rather than as a disclaimer. **Every claim below is attributed to a
project's own documentation or shipped files, and every one of them has a shelf
life.** These are actively developed projects. A behaviour that was true of a
release last year may be false of the next one, and none of it has been run on
this book's rig. The chapter's real output is not a list of facts. It is a set of
questions you can answer on your own device, which is what Chapter 24 builds a
harness for.

## The provider denylist

Start with the thing whose name causes the most confusion.

Magisk's denylist is a configuration list, manipulated through
`magisk --denylist` — the tool documentation lists the actions plainly: `status`,
`enable`, `disable`, `add PKG [PROC]`, `rm`, `ls`, and `exec` to "Execute
commands in isolated mount namespace and do all unmounts"
(`docs/tools.md`, Magisk repository). A package or process on the list is one the
implementation will treat differently: its own mounts and its modules' mounts are
removed from that process's mount namespace, and with Zygisk enabled, modules are
kept out of it.

That is the whole of it. It is a list of processes a provider will not modify —
or will modify and then back out of — and it is not a hiding mechanism. Magisk's
own FAQ is unusually direct on this point, answering "Why is X app detecting
root?" with: "Magisk no longer handles root hiding. There are plenty of
Magisk/Zygisk modules available that specifically provide these functionalities."
The feature was renamed from MagiskHide to DenyList precisely because the
previous name promised something the mechanism does not deliver.

KernelSU expresses the same idea in the opposite polarity. Its App Profile
documentation describes an "Umount modules" option: "KernelSU provides a
systemless mechanism to modify system partitions, achieved through the mounting
of OverlayFS. However, some apps may be sensitive to this behavior. In this case,
we can unload modules mounted in these apps." The manager exposes "Umount modules
by default", enabled by default, so the same list can be run as a whitelist or a
blacklist depending on which way round you set it. That page also carries a
kernel-version caveat worth noticing: on kernels below 5.10 "this option is
merely a configuration setting, and KernelSU itself doesn't take any action"
unless `path_umount` has been backported. A setting that is on and does nothing
is exactly the failure mode this book keeps warning you about.

**What it removes.** Provider and module mounts from the listed process's mount
namespace. Under Magisk with Zygisk, also the module injection itself for listed
processes.

**What it does not remove.** Everything that is not a mount. Files that exist on
real filesystems rather than being bind-mounted over something. The provider's
own directories under `/data/adb`. Manager app packages. Properties. Anything
that was injected by a mechanism other than mounting. And critically for you:
being on the list is a statement about the *provider's* behaviour, not about
yours — nothing on that list stops a module from making its own noise in a
process it was loaded into.

**How to check.** Ask the provider what it thinks, then ask the process. On
Magisk, `magisk --denylist ls` and `magisk --denylist status` report the
configured list and whether enforcement is on. From the app side, compare
`/proc/<pid>/mountinfo` for a listed process against an unlisted one; Chapter 22
covered reading it and Chapter 24 turns the comparison into a repeatable
measurement. Do not infer the answer from the API. As Chapter 10 established,
`PROCESS_ON_DENYLIST` reports list membership as the provider models it, and the
header promises no relationship between that bit and whether any unmount ran.

:::note
The two families do not mean the same thing by the word. Zygisk Next's README
states directly that "`PROCESS_ON_DENYLIST` cannot be flagged correctly for
isolated processes on Magisk DenyList currently" and that Zygisk Next "only
guarantees the same behavior of Zygisk API, but will NOT ensure Magisk's internal
features". [Where it breaks](/ZygiskLab/book/companion/20-where-it-breaks/) treats
this as a general rule; the denylist is its sharpest instance.
:::

## `FORCE_DENYLIST_UNMOUNT`, with Part VI eyes

Chapter 10 gave you the mechanics: the option is legal only in
`preAppSpecialize`, it records an intent rather than acting, the unmount happens
during specialization, and `setOption` returns `void` so nothing tells you
whether any of that occurred. Read it now for what it changes in the app's view.

The header's comment is the source: "Set this option to force all Magisk and
modules' files to be unmounted from the mount namespace of the process,
regardless of the denylist enforcement status."

**What it removes from the app's view.** One thing: entries in the process's own
mount table. After it runs, `/proc/self/mountinfo` in that process no longer
lists the provider's and modules' bind mounts, and paths that only existed
because something was mounted over them stop resolving. If the app's check was
"parse my mount table and look for unexpected sources", that check is answered.

**What remains visible regardless.** Everything in Chapter 21's inventory that is
not a mount, which is most of it. Your `.so` is *mapped*, not mounted, so it is
still a line in `/proc/self/maps` with a path, a device and an inode — an unmount
does not touch a mapping. Your file descriptors, threads and installed hooks
survive it untouched. Files that live on a real filesystem — the provider's
directories, manager packages, anything under `/data` — are still there to be
stat-ed, because they were never mounted over anything. System properties are
unaffected. And the option cannot reach outside the process: it changes this
namespace, so a check that compares against an expectation derived from somewhere
else is looking at a different picture than the one you changed.

It also removes your own module's overlay files, if your module ships any. The
routine does not spare its caller, which is a functional consideration before it
is a footprint one.

**How to check.** Capture `/proc/self/mountinfo` from a target process with the
option set and from one without, and diff them. That is a two-line experiment and
it is the only thing that distinguishes "the option worked" from "the provider
does not implement `setOption`", which are byte-for-byte identical from inside
your module.

## Shamiko

Shamiko is the module most readers will have been pointed at, and it is worth
describing precisely because it belongs to a different class of problem than
everything above.

The description is its own. The README shipped inside `Shamiko-v1.2.5-414`, by
LSPosed Developers, states: "Shamiko is a Zygisk module to hide Magisk root,
Zygisk itself and Zygisk modules." Its usage section says it "read[s] the
denylist from Magisk for simplicity but it requires denylist enforcement to be
disabled first", and instructs the user to configure the denylist as the set of
processes to hide from while leaving enforcement off. The same README documents a
whitelist mode enabled by creating an empty file at
`/data/adb/shamiko/whitelist`, and is candid about its cost: "Whitelist has
significant performance and memory consumption issue, please use it only for
testing", with root access limited to apps previously granted it.

So the relationship to the denylist is not what it looks like. Shamiko does not
extend the denylist; it borrows the list as a configuration input and does its
own work, which is why enforcement must be off — the provider's routine and
Shamiko's would otherwise both be acting on the same processes.

**The class of problem it addresses** is process-level concealment of a known set
of traces: the ones a rooted device and the Zygisk stack leave in a process and
on the device, plus, per its 1.0 release notes, "some traces introduced by other
modules". Its changelog is a decade-long record of that class being a moving
target — entries for hiding "more trace of Zygisk", a fix for "tmp mount being
detected", and 1.2.1's "Drop dependency on tmpfs workdir and don't rely on any
mounts" all describe removing the module's *own* observable artefacts. The
shipped `service.sh` shows a second, separate strand: it normalises a set of boot
and build system properties toward the values an unmodified device reports. That
is a property-level intervention and it belongs to the same class — making the
device's own answers about itself look ordinary.

**What it does not belong to.** Shamiko is a Zygisk module. It runs on the device,
inside processes, after boot, and every mechanism it has is a local one. That
bounds it absolutely: it operates on what the device can be made to say about
itself, and it has no reach over anything decided elsewhere. The final section of
this chapter is about why that bound matters more than any individual capability.

**What it does not remove, in your terms.** Your module's traces are not its
subject. It addresses traces of the root implementation, of Zygisk, and of
modules as a category; it is not a wrapper that makes an arbitrary module
invisible, and a module that opens a socket, spawns a thread, hooks a symbol and
writes a log is generating evidence that nothing outside your own code is going
to clean up. Chapter 21's point stands: your footprint is a consequence of your
design decisions, and installing something else does not undo them.

**How to check.** Measure the specific trace you care about, twice — with the
module installed and configured, and without. Anything less is inference. And
note the version, the provider, the provider version and the Android build
alongside the result, because the changelog above is proof that the answer moves.

:::caution
The Shamiko claims above come from the README, `module.prop` and `service.sh`
inside the release archive named, and from that project's published release
notes. They describe what the project says it does. Nothing here is a reading of
its native code, and nothing here has been observed on the rig.
:::

## Zygisk Next's own design choices

Zygisk Next is this book's reference provider and describes itself in its README
as a "Standalone implementation of Zygisk, providing Zygisk API support for
KernelSU and a replacement of Magisk's built-in Zygisk". Its requirements are
strict in a way that is itself a footprint decision: "No multiple root
implementation installed", and on Magisk, built-in Zygisk must be turned off. Two
loaders fighting over zygote is both a stability problem and a visibility one.

Beyond that, be careful. The public repository at the time of writing contains a
README, a `docs/` directory and the `zygisk_next_api.h` header — not the
implementation. **You cannot read Zygisk Next's source to learn what it does
about its own footprint**, and so this book will not describe internals it cannot
see. What is publicly sourceable is the release notes, and those state
footprint-relevant work in general terms: 1.4.4 lists "Fixed detection issues"
and "Enhanced security", 1.4.3 and 1.4.2 list "Improved security", and 1.5.0
lists "Adjusted the WebView child process umount mechanism, now managed by Root
solution". That last one is a genuine architectural note — responsibility for
unmounting in a class of child process moved from the loader to the root
implementation — and it is the kind of change that silently alters what an app
sees between two versions of the same product.

The design choice you can verify from the header is the one Chapter 10 already
put in front of you: the API gives a module exactly two options and two flags,
neither of which is a concealment primitive, and the provider does not expose the
rest of its policy to you at all. What the denylist means to it, whether modules
load into listed processes, what its unmount covers — none of that is in the
interface. It is the provider's decision, it is not documented for you, and it is
therefore measurable rather than knowable.

**How to check.** Treat it as a black box and instrument its edges. Log a line in
every `preAppSpecialize` naming the process to learn where you are actually
loaded. Read `getFlags()` and record it alongside the mount table you measured.
Ask the companion, which runs as root, for anything about `/data/adb` you need —
Chapter 20's ordering — rather than sniffing from the app process. Every one of
those answers is specific to one provider version and worth writing down with the
version attached.

## What none of them remove

Here is the conclusion, and it is the most useful thing in the chapter.

Everything surveyed above operates on the same resource: **what the device can be
made to say about itself.** A denylist changes which processes get modified. An
unmount changes a mount table. A hiding module changes what a process can observe
locally, and normalises what the property system reports. These are all edits to
the device's own account of its state, made by software running on that device,
answering questions asked by an app running on that same device.

Platform attestation is not that kind of question, and this is a difference in
kind rather than in difficulty. Google's Play Integrity API documentation
describes verdicts built on "hardware-backed security signals that are highly
resilient to attacks and circumvention", with device trust tiers explicitly
resting on hardware-backed attestation, and it directs developers to have their
backend server handle the result — including being ready for the case where
"Android Platform Key Attestation keys specific for devices are revoked". The
same shape holds for Android's hardware-backed key attestation generally: the
evidence is produced by a component the operating system does not control, and
the party that decides what it means is not on the device.

Follow the consequence through. The question has moved off the device. A verdict
is not a value some process on the phone computed and could have computed
differently; it is a signed statement, produced with hardware-held keys, that
someone else's server evaluates. Local tidying does not participate in that
exchange at any point. It does not matter how clean the mount table is, how few
mapped regions have suspicious paths, or what the properties say, because none of
those are the input.

So: process-level concealment and platform attestation are two different
problems, and no amount of the first addresses the second. Say that to yourself
before you start optimising a footprint. If what you actually need is a passing
attestation verdict, nothing in this chapter — and nothing this book will teach
you — is on the path to it, and time spent tidying local traces is time spent on
the wrong question entirely. If what you need is to understand which of your own
design decisions are observable, and to hold that number down for reasons of
craft, correctness, or a defensive assessment you have been asked to make, then
the survey above tells you which existing pieces touch which traces, and Chapter
24 gives you the harness to find out whether any of it is true on your device
this week.

:::caution[What is not verified here]
Nothing in this chapter has been run on the rig — Pixel 6 Pro, Android 16, arm64,
KernelSU-Next 3.3.0, Zygisk Next 1.4.5. Every project claim is attributed above to
that project's own documentation, shipped files, or release notes as they stood at
the time of writing, and no internal implementation detail is described that could
not be sourced from those. All of these projects change. Read the current source
or documentation of anything you intend to depend on, and measure the specific
trace you care about on your own device before you believe any statement about it,
including the ones here.
:::

---
title: "How an app looks for you"
description: "How an app inspects its own maps, mount namespace, loaded libraries, tracer presence, and filesystem for signs of you."
sidebar:
  order: 2
status: unverified
---

The traces you left, stage by stage — and how apps look for them.

:::caution[Detection and measurement]
This chapter covers detection mechanisms and measurement on systems you
own or are authorised to assess. See [Rules of engagement](/ZygiskLab/book/foundations/02-rules-of-engagement/).
:::

[Chapter 21](/ZygiskLab/book/footprint/21-your-footprint/) inventoried what a
module leaves behind. This chapter is its mirror, written from the other side of
the glass: what an application running as an ordinary app can actually observe
about its own process — what the kernel and the runtime will hand to code under
an app uid, at what cost, and with what reliability.

Listing a dozen checks is easy. Saying for each one what it costs, what it
genuinely catches, and how often it fires on somebody doing nothing of the sort
is the useful part, and every section below answers those three questions. A
check without a false-positive estimate is not a security control; it is a way to
lock out your own users.

Nothing here has been run on the reference rig. The `/proc` interfaces named are
documented Linux kernel interfaces; where a claim depends on Android version,
OEM, or root provider, the prose says so.

## The app's vantage point

An app process is the same process an injected module lives in, and that symmetry
is the whole story. The app cannot inspect *another* process — [Chapter
12](/ZygiskLab/book/postspecialize/12-what-changed-at-boundary/) laid out why an
app uid buys nothing outside its own sandbox, and it constrains the app exactly
as much as it constrains you. But the app can inspect *itself* freely, because a
process may always read its own `/proc/self/` entries, and a module loaded into
that process is inside the thing being inspected.

The checks split into two families. **Introspective** checks read the process and
the device: maps, status, descriptors, mounts, files, properties. They are cheap,
they run offline, and every one of them is evaluated by code running on hardware
the app does not control. **Attested** checks ask something outside the device to
vouch for it. The last section deals with those separately, because collapsing
the two into one list is the most common conceptual error in this area.

## Self-inspection: `maps`, `status`, `fd`

The app opens `/proc/self/maps` and reads it. That is the whole technique, and it
is the single most informative check available to an unprivileged app.

`maps` is one line per virtual memory region, each ending with the pathname of
the file backing it — as resolved *in this process's mount namespace*. Every
shared object the dynamic linker mapped appears there, including one a Zygisk
provider mapped before the app's own code ran. Anonymous regions have no path,
though on recent Android versions the allocator and ART annotate much anonymous
memory with `[anon:...]` names via `prctl(PR_SET_VMA)`, giving the reader more
structure than a plain Linux process offers — how much, on a given release, is
one of the things Chapter 24's harness will show you directly. An app looks for anything unexpected: a mapped
`.so` outside the paths app libraries come from, a path referencing a provider
directory, an executable mapping with no backing file, a name matching a list it
shipped. It can equally look for the *absence* of an expected mapping, or a
library mapped twice.

`/proc/self/status` is a small file of per-process fields. The two that matter
are `TracerPid` (below) and the uid/gid lines, which confirm the process runs as
the uid it expects. Beyond `TracerPid` it carries little detection value.

`/proc/self/fd` is a directory of symlinks, one per open descriptor. An app can
`readlink` every entry and see files, sockets, pipes, and anonymous inodes. A
module that opened something and kept it across specialization appears here as a
descriptor the app never opened — which is why `exemptFd()` and descriptor
hygiene get their own treatment earlier in the book.

**Cost.** Very low. `maps` for an app process typically runs to a few thousand
lines — the exact figure varies with the app and the release, and the harness in
Chapter 24 reports it for your own — so scanning it costs milliseconds and no
permission. `fd` is smaller still, and all three can
be re-read at any time.

**Catches.** Anything mapped or opened under a path the app recognises, plus
anything structurally odd — an executable anonymous mapping, a duplicate library.
The most reliable way to find a module that did not think about naming.

**False positives.** Moderate to high, depending on how the app decides what
counts as unexpected. A device's `maps` is full of vendor libraries, OEM
frameworks, GPU drivers, and platform instrumentation; accessibility services,
enterprise management agents and analytics SDKs add more. Matching on "any path
not in my allowlist" flags ordinary phones constantly. Matching a short list of
known strings has a far lower false-positive rate and catches far less, since
anything renamed is invisible. That trade is the central tension of this chapter.

:::note
Reading your own `maps` is not a detection trick. Lab 4 parses it to obtain the
`(dev, inode)` pair `pltHookRegister` requires, and the linker publishes nowhere
else ([Chapter 14](/ZygiskLab/book/postspecialize/14-hooking-native-symbols/)).
The same file serves both sides because it is simply where Linux describes a
process's address space.
:::

## Mount namespace inspection

`/proc/self/mountinfo` lists every mount visible in the process's namespace, with
mount id, parent id, device, the root of the mount within its filesystem, the
mount point, options, and filesystem type. It is a documented kernel interface
readable by the process itself; `/proc/self/mounts` carries a subset of the same
information in the older `fstab`-like format.

This matters because a root provider's work is largely mount work. Systemless
modification overlays files by mounting over them; hiding removes mounts from a
target's namespace. Either way the mount table of an app process on a modified
device can differ from a stock one — extra mounts over system paths, unusual
filesystem types over directories belonging to the read-only system image, or a
mount whose source device does not match what the app expects for that path.

The catch is "what the app expects". An app has no clean reference copy of a
stock mount table. Android's mount layout varies by version, by OEM, by whether
the device uses dynamic partitions, by how many APEX modules are active, and by
the storage view the framework installed for this app and user. Encoding
expectations means encoding a guess about a hugely variable platform. The subtler
variant ignores contents and looks at *shape* — implausible mount counts,
inconsistent parent relationships, mount ids suggesting churn — which is a weaker
and noisier signal.

**Cost.** Low: one file read and a parse, expensive only in that interpreting it
requires the app to carry expectations.

**Catches.** Overlay-style modification leaving mounts visible in the app's
namespace. Essentially nothing about a module whose only presence is a library
the linker loaded, since loading a library adds no mount.

**False positives.** High — the check most likely to be wrong about a legitimate
user. Custom ROMs, A/B devices mid-update, work profiles, OEM dual-app
containers, and emulators all produce mount tables unlike whatever the developer
saw on their test handset.

## Loaded-library enumeration and hook integrity

There are two distinct checks here and they are often confused.

**Enumeration** asks the runtime, rather than the kernel, what is loaded. On the
Java side an app inspects its own `ClassLoader` and the paths it was constructed
with; on the native side `dl_iterate_phdr` walks the shared objects the dynamic
linker knows about. That is not the same set as `maps`, which shows what is
mapped. A discrepancy between the two — a mapped ELF the linker does not list, or
the reverse — is itself a signal, and a more interesting one than either list
alone, because it does not depend on recognising any name.

**Integrity checking** is the app verifying that its own code still calls what it
should. The concrete case, directly relevant to Part IV, is GOT entries. [Chapter
14](/ZygiskLab/book/postspecialize/14-hooking-native-symbols/) described a PLT
hook as writing one pointer in an importing ELF's Global Offset Table so an
existing indirection lands elsewhere. That is a data change in the app's own
address space, and the app can read it back: walk the library's relocation
entries to find the slot for an imported symbol, read the address in it, and
determine which mapping that address falls into via `maps` or `dl_iterate_phdr`.
If `libandroid_runtime.so`'s slot for `open` does not point into the library
defining `open`,
something rewrote it. The app need not know what the hook is or who installed it,
only that the pointer left the neighbourhood it belongs in.

This is the sharpest check here, because it tests a structural invariant rather
than matching a name — and its limitation matches: it covers only the slots the
app chose to walk, in the libraries it chose to check. Its scope is exactly as
narrow as a PLT hook's, for the same reason. The same idea extends to hashing the
app's own executable regions against a baseline, which detects inline patching
instead; doing that correctly is harder than it sounds, because relocations, JIT
output, and platform instrumentation all move bytes legitimately.

**Cost.** Enumeration is cheap. GOT verification is moderate — parsing ELF
structures in the app's own address space, per library, per symbol. Whole-region
hashing is the expensive end and is noticeable if run often.

**Catches.** GOT verification catches PLT/GOT hooking specifically, including a
hook whose library is named innocuously. It catches no other mechanism — ART
method hooking ([Chapter
15](/ZygiskLab/book/postspecialize/15-hooking-java-through-art/)) leaves the
native GOT untouched.

**False positives.** Low for the GOT check, which is what makes it valuable —
few legitimate reasons exist for a libc import to point outside libc. Not zero:
profilers, sanitiser runtimes, crash-reporting SDKs and platform tracing all
interpose legitimately, and an app shipping such an SDK will flag itself. Code
hashing is far worse.

## `ptrace` and tracer presence

`/proc/self/status` contains a `TracerPid` field: the pid of the process tracing
this one, or `0` if none. One file read and one integer parse is the entirety of
the standard tracer check.

Apps also use the self-trace idiom: call `ptrace(PTRACE_TRACEME, ...)`, or fork
a child that attaches to the parent. A process has at most one tracer, so success
means nothing else is attached and failure suggests something is; the watchdog
variant additionally occupies the slot so a later attach fails.

None of this is relevant to Zygisk in the ordinary case, and saying so plainly
matters. A Zygisk module is loaded into the process by the provider and runs as
part of it. It does not attach as a debugger, and a tracer check will not see it.
The check exists because attach-based instrumentation frameworks *do* show up —
a different threat with a different signature.

**Cost.** Negligible for `TracerPid`. The self-trace variants cost a process and
some care, and can interfere with legitimate crash handling.

**Catches.** A debugger or an attach-based instrumentation tool present at the
moment of the check. Nothing that was loaded into the process rather than
attached to it.

**False positives.** Low in frequency but severe in who they hit: developers
debugging their own device, QA and automation harnesses, attach-based
accessibility tooling, and platform profilers. An app refusing to run under a
debugger refuses to run for the people best placed to report its bugs.

:::caution
`TracerPid` is a snapshot. It is `0` between attaches and it is `0` if the tracer
detached. An app polling it is sampling, and a check that must be true at every
instant cannot be established by reading a file occasionally. This is a general
property of every introspective check in this chapter, and it is the reason
attestation exists.
:::

## Filesystem probes, and the difference between denied and absent

The oldest family of checks: `stat()` or `access()` a list of paths associated
with root, a provider, or its manager app, and treat a hit as evidence.

The list is the problem. There is no canonical set of paths: which directories a
provider uses, where a manager app installs, and which of those are visible from
inside an app's namespace depend on the provider, its version, and its hiding
configuration. Publishing a list here would be inventing facts, and it would be
stale by the time you read it. [Chapter
24](/ZygiskLab/book/footprint/24-detection-harness/) builds the harness that
enumerates what is genuinely reachable on *your* device, which is the only list
worth having.

The mechanism is worth stating precisely, along with one subtlety most treatments
get wrong. **`EACCES` is a different signal from `ENOENT`, and often the stronger
one.** "No such file or directory" honestly reads as the path not being there —
nothing created it, it was never in this namespace, or a provider removed it.
"Permission denied" means the path *exists* and the kernel declined to say more.
The failure to read is itself an existence proof, and an app collapsing both into
"check failed" throws away its better signal.

[Chapter 12](/ZygiskLab/book/postspecialize/12-what-changed-at-boundary/) made
the same point from the module's side: after specialization a path can return
`ENOENT` where it used to resolve, because the namespace changed rather than the
file. Both sides of the glass read the same errno and both must resist
over-reading it — on Android there is a third case, since SELinux denials can
present as either code depending on the operation and the policy, and an app
cannot separate a policy denial from a DAC denial. A related probe checks whether
read-only directories are writable, or whether a root-helper name resolves on
`PATH`; both carry the same namespace caveats.

**Cost.** Very low per path; the total is the length of the list. Hundreds of
`stat` calls are fast, though visible to anything hooking `open` or `stat` — a
mildly amusing symmetry.

**Catches.** Devices where the relevant paths are visible to the app: nobody
configured hiding, or the provider does not hide by default. It catches
unconfigured setups well and configured ones not at all, so its yield falls over
time as providers improve.

**False positives.** Moderate. Custom ROMs ship developer utilities in system
paths, some OEM builds include diagnostic binaries, and userdebug builds
legitimately carry extra tools. A path-name match on a string that also appears
in an unrelated app's files is a real source of noise.

## Property and package checks

`ro.debuggable`, `ro.secure`, `ro.build.type`, `ro.build.tags` and the verified
boot state properties are readable by any app through the system property
interface. Their stock values on a released consumer build are well known, and
values inconsistent with a locked, release-keys device are a signal about the
device's state.

They are a signal about *the device*, not about your module. An unlocked,
test-keys phone may have no modules loaded at all; a phone with pristine
properties may be running a provider that resets them. Properties are trivially
writable by anyone with root, which puts them at the bottom of this chapter's
reliability ordering.

Package checks are the Java-side equivalent: ask `PackageManager` whether a
package is installed, or query for an intent a manager app handles. Package
visibility rules since API 30 mean an app cannot enumerate installed packages
without a permission or explicit `<queries>` entries, so this reduces to checking
a fixed list declared in the manifest — a list that ships inside the APK,
readable by anyone who looks, and outrun by a rename. Again, no list here;
Chapter 24's harness produces the one true for your device.

**Cost.** Very low. Property reads are cheap; package queries are one binder
call each.

**Catches.** Unhidden manager apps and openly non-production device state — the
shallowest family here.

**False positives.** High and unusually consequential. Developer devices,
userdebug builds, custom ROMs on abandoned hardware, managed enterprise fleets,
and devices whose bootloader cannot be relocked all fail these while belonging to
entirely ordinary users. An app blocking on `ro.build.tags` alone blocks a large
population who have never installed a root provider.

## Platform attestation

Everything above is the device examining itself and reporting the result to code
on that same device. Attestation is a structurally different kind of statement.

In an attestation flow, a component the app does not control — backed by the
device's secure hardware and keys provisioned during manufacture — produces a
signed statement about the device or a key resident in it. The app relays that
statement to its own server, which verifies the signature against a chain rooted
in a certificate the vendor trusts and then decides what it means. Android
exposes this in two broad forms: hardware-backed key attestation, where the
keystore issues a certificate chain describing a key and the boot state of the
device holding it, and the platform's integrity APIs, where the app receives a
signed verdict it forwards to a backend for verification.

Three properties follow, and they are why attestation sits apart.

**The evaluation happens off-device.** Every introspective check here is a
function computed in a process the user fully controls, and its result is a value
in memory before it is a decision. Attestation moves the decision to a server,
and the client only carries a signed blob it cannot usefully alter.

**The signer is not the app.** The statement's authority comes from a key the
app's process never holds, so the client cannot manufacture one, only obtain one.

**It says nothing about your process.** An attestation statement speaks to boot
state and to device and key provenance. It is not a report on which libraries are
mapped into one particular app: it is coarser than `maps` and less local, and an
app relying on it alone is asking a different question from the rest of this
chapter. Attestation and self-inspection are complementary precisely because they
fail differently — self-inspection has full local detail and no integrity
guarantee, attestation has an integrity guarantee and almost no local detail.

**Cost.** The highest here by a wide margin: network availability, a backend to
verify, key and certificate handling, latency budgeting, and a considered policy
for when the check cannot complete at all. That last one is a product decision,
not a technical one.

**Catches.** Boot and platform state as a signed assertion, evaluated somewhere
the client cannot reach. Not module presence.

**False positives.** Concentrated and predictable rather than random: devices
lacking the required hardware or platform services, devices whose provisioning
failed or expired, regions where those services are absent, and older or
uncertified hardware. These are populations rather than individuals, which makes
the impact of a hard block easy to underestimate and easy to measure.

This book does not describe how any attestation implementation is circumvented,
and does not detail any product's internals. The mechanism above is what you need
to reason about where the signal sits.

## The honest assessment

Put the table together and the shape is uncomfortable for anybody hoping one
check is decisive.

| Check | Cost | Catches | False positives |
|---|---|---|---|
| `maps` / `fd` scan | very low | named or oddly-shaped mappings | moderate–high |
| `mountinfo` compare | low | overlay-style modification | high |
| Library enumeration | low | linker/kernel disagreement | moderate |
| GOT verification | moderate | PLT/GOT hooks specifically | low |
| `TracerPid` | negligible | attached debuggers only | low, but hits developers |
| Path probes | low | unhidden providers | moderate |
| Properties / packages | very low | open non-production state | high |
| Attestation | high | signed platform state | concentrated by population |

Two conclusions follow.

**The cheap checks catch the careless.** A module shipping under an obvious
name, mounting visibly, leaving descriptors open, on a device with `ro.debuggable`
set, will be found by a fifty-line function. That is a real result and it is most
of what published detection actually does — [Chapter
23](/ZygiskLab/book/footprint/23-existing-answers-surveyed/) surveys the
implementations. It is also the result that decays fastest, because every one of
those traces is a design decision that can be made differently.

**The check with the best false-positive profile is the narrowest.** GOT
verification tests an invariant instead of matching a string, so it does not care
what your library is called — and it sees only the slots the app walked, in the
libraries it chose, against one hooking mechanism. Broad coverage is broad
precisely because its matching is loose, and loose matching is what generates
false positives. That is not an implementation flaw anyone can fix; it is the
shape of the problem.

Which leads to the statement this chapter has to close with, because Chapter 25
builds on it. **Every check above fires on real users doing nothing wrong.**
Custom ROM users on hardware their vendor stopped supporting. Blind and low-vision
users running accessibility services that legitimately interpose. Enterprise
devices under management agents doing exactly what the employer configured.
Developers, QA engineers and security researchers on their own hardware,
including the ones who will report your bugs. Emulators in CI. OEM dual-app
containers. Devices in regions where the platform's services are unavailable.

An app treating any single signal as proof will be wrong about those people, and
wrong silently, because a user locked out by a heuristic is not told which
heuristic. The engineering response is not a longer list of checks. It is scoring
rather than gating, degradation rather than refusal, measurement of how often
each signal fires on the actual install base before it affects anyone, and a
route back for whoever is wrongly flagged. [Chapter
25](/ZygiskLab/book/footprint/25-the-defensive-chapter/) develops that into
advice for app authors.

Before any of that, get numbers. Every rate in this chapter is a judgement, not a
measurement, and it is stated as one. [Chapter
24](/ZygiskLab/book/footprint/24-detection-harness/) builds a harness that runs
these checks on your own device, on a clean one, and on a modified one, and
prints what each returns. That is how you learn which claims above hold on the
hardware in front of you — and which were true on somebody else's phone in a
different year.

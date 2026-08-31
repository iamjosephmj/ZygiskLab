---
title: "Further reading"
description: "Provider source, upstream documentation, and prior art for further reading."
sidebar:
  order: 5
status: unverified
---

Every source below is one this book actually leaned on, grouped by what you are
likely to be trying to do when you need it. For each, the note says what it is
authoritative *for* and what it is not — that scoping is the point of the page,
because the commonest way to waste a day is to read the right document about the
wrong thing.

:::caution[Dated snapshot]
This page describes these projects as they stood on **2026-09-01**. All of them
move: repositories are archived, documentation is rewritten, features change
hands between components. Treat every entry as a pointer to a name, not as a
statement of current fact, and read the project's own current source or
documentation before you depend on anything here. Nothing on this page has been
run on this book's rig.
:::

Projects are named by repository rather than by deep link, deliberately. File
paths inside a repository rot faster than the repository does.

## The API itself

**`zygisk.hpp`** — `topjohnwu/zygisk-module-sample`, file `module/jni/zygisk.hpp`.
The single most important document for a module author, and the one this book
treats as its authority throughout. It is the public Zygisk API header, is
self-documenting, and carries the development guide inline as comments. The copy
vendored at `modules/01-hello-zygisk/jni/zygisk.hpp` in this repository records
its provenance in a comment at the top: fetched from that repository at master,
targeting `ZYGISK_API_VERSION 5`, unmodified below the provenance block.

*Authoritative for*: the module interface — the callbacks, `Api`, `Option`,
`StateFlag`, the companion registration macro, and every guarantee this book
attributes to "the header".

*Not authoritative for*: anything a provider does behind the interface. The
header says `connectCompanion()` works only in `pre[XXX]Specialize` and says
why; it says nothing about companion lifetime, denylist policy, or whether any
particular provider implements `setOption` at all. Note also that the sample
repository was archived in August 2026 and is read-only, so it is a snapshot of
API v5 rather than a living document.

**Zygisk Next** — `Dr-TSNG/ZygiskNext`. This book's reference provider,
describing itself in its README as a standalone implementation of Zygisk
providing Zygisk API support for KernelSU and a replacement for Magisk's
built-in Zygisk. Read its README for the compatibility requirements — no
multiple root implementations, built-in Zygisk off under Magisk — and its
release notes for behavioural changes between versions.

*Authoritative for*: its own stated compatibility rules, its published release
notes, and the `zygisk_next_api.h` header it publishes.

*Not authoritative for* — and this is the entry's real content — **its
implementation, which is not there**. As Chapter 23 established, the public
repository contains a README, a `docs/` directory and that header; the
implementation is not publicly browsable, and the project's own notice states it
is proprietary from v4-0.9.2. You cannot read Zygisk Next's source to learn what
it does. Treat it as a black box and instrument its edges.

**Magisk** — `topjohnwu/Magisk`. The original Zygisk implementation and the
origin of the API header. Its `docs/` directory is the source for this book's
statements about the `magisk` tool and the denylist actions.

*Authoritative for*: Magisk's own tools, installation, and module format, in its
own documentation.

*Not authoritative for*: how Zygisk Next behaves. The two implement the same
module API and diverge freely everywhere else, which is the subject of
[Where it breaks](/ZygiskLab/book/companion/20-where-it-breaks/).

## The platform underneath

**AOSP `frameworks/base`, `core/java/com/android/internal/os/`** — the zygote
itself. `Zygote.java`, `ZygoteInit.java` and `ZygoteConnection.java` are the code
that forks and specializes, and reading them is the only way to see what the
values in `AppSpecializeArgs` actually are on the platform side. Browsable at
`android.googlesource.com` (the interactive code search at `cs.android.com`
indexes the same tree).

*Authoritative for*: what the platform does, on the branch you are reading.

*Not authoritative for*: the device in your hand. Vendors patch, and the branch
you read is not necessarily the build you are running. Confirm on the device.

**Java Native Interface Specification** — Oracle's JNI specification, chapters on
design overview, types and data structures, the function reference, and the
Invocation API. This is where the rules that do *not* change live: reference
lifetimes, the per-thread nature of `JNIEnv`, exception state, type signatures.

*Authoritative for*: JNI semantics as a contract.

*Not authoritative for*: Android's implementation limits and behaviours — local
reference table sizing, CheckJNI, `FindClass` resolution in a process with no
Java frames. Those are the next entry.

**JNI Tips** — Android's `developer.android.com` NDK guide. The Android-specific
companion to the specification, and the source for this book's statements about
attaching and detaching threads, `FindClass` falling back to the system
classloader when no Java frame is on the stack, and reference management. See
[JNI inside a live app](/ZygiskLab/book/postspecialize/13-jni-inside-a-live-app/).

*Authoritative for*: Android's documented JNI behaviour and its stated
requirements, notably that an attached thread must detach before it exits.

*Not authoritative for*: version-specific internals it does not name, and for
anything about injected code specifically — it is written for an app's own JNI
library, which is not what you are.

**Restrictions on non-SDK interfaces** — Android's app-compatibility guide. Its
table of access paths lists JNI explicitly: `GetFieldID` and `GetMethodID`
against a restricted member return `NULL` and throw. Read it before you spend an
afternoon assuming a framework class lost a method.

*Authoritative for*: the restriction model and the documented failure behaviour
per access path.

*Not authoritative for*: which specific members are restricted on your build.
That list is per-release and is not something to hardcode against.

**`proc(5)`** — the Linux man page for the `/proc` filesystem, at `man7.org` or
your own `man` command. The reference for `maps`, `mountinfo`, `status`,
`task/` and `fd/` — that is, for both what you leave behind and what an app
reads to find it. Recent releases of man-pages split the per-process files into
their own pages (`proc_pid_maps(5)`, `proc_pid_mountinfo(5)`,
`proc_pid_status(5)`, `proc_pid_fd(5)`), so if `proc(5)` looks thinner than you
remember, follow its cross-references.

*Authoritative for*: field meanings and formats on Linux.

*Not authoritative for*: what Android's kernel and SELinux policy will actually
let a given process read. Chapters 21 and 22 are about that gap.

## Hooking

Chapter 15 surveys the ART hooking projects, each attributed to its own README.
Go to the repository rather than to any summary, including that chapter's.

**LSPlant** — `LSPosed/LSPlant`. An ART hook library providing Java method
hook/unhook and inline deoptimization, stating support for Android 5.0–17 (API
21–37) across armeabi-v7a, arm64-v8a, x86, x86-64 and riscv64. LGPL-3.0, which
is a licensing decision you must make before you link it. If you have concluded
you genuinely need ART hooking, this is the maintained engine to inherit rather
than to reimplement.

*Authoritative for*: its own API and its own stated version support.

*Not authoritative for*: ART internals in general. Its version range is a record
of work done, not a guarantee about a runtime nobody has tested yet.

**LSPosed** — `LSPosed/LSPosed`. The Xposed-compatible framework, described in
its README as a Riru or Zygisk module providing an ART hooking framework with
APIs consistent with the original Xposed, leveraging LSPlant. Worth reading as a
large, real Zygisk module: the techniques in Parts II and III are its substrate.

*Authoritative for*: the Xposed-compatible API surface it offers modules.

*Not authoritative for*: the hooking mechanism, which it delegates to LSPlant.

**Pine** — `canyie/pine`. A dynamic Java method hook framework on ART, stating
support from Android 4.4 (ART only) through recent releases on thumb-2 and
arm64. Its README is unusually candid about limits — argument correctness on
Android 6.0 arm32, disabling the hidden-API policy on 9.0+, initialisation
ordering relative to other threads — and that candour is the reason to read it
even if you never use it. Those are the shapes of problem the technique has.

**YAHFA** — `PAGalaxyLab/YAHFA`. The earlier, deliberately minimal member of the
line: `backupAndHook(target, hook, backup)` and not much else. Its README states
support through Android 12 DP1 and notes that support for 6.0 and below broke at
a named commit. The clearest small artefact to read if you want the
backup-and-replace shape without a large codebase around it, and its version
ceiling makes the maintenance argument better than prose can.

**Whale** — `asLody/whale`. A cross-platform inline hook framework covering
Android, iOS, Linux and macOS across ARM/THUMB, ARM64, x86 and x86-64, with a
built-in JIT engine for generating instructions in memory. Included because it
shows the inline-hooking route reaching Java methods from underneath rather than
through ART's dispatch record.

*Not authoritative for*: current Android compatibility. Its README's tested
version list stops well short of modern releases; read it for the technique.

**Riru** — `RikkaApps/Riru`. Zygisk's predecessor, injecting into zygote so
modules could run in apps and `system_server`. Archived in January 2024, with a
README directing all users and modules to migrate to Zygisk. Listed for
historical context only: much older writing about "Riru modules" describes a
mechanism that is no longer maintained, and knowing the name saves you following
that trail.

## Footprint and detection

Chapter 23 surveys this ground and every claim in it is attributed. The entries
here are the primary sources it used.

**Magisk's `docs/tools.md` and FAQ** — the source for the denylist actions
(`status`, `enable`, `disable`, `add`, `rm`, `ls`, `exec`) and for the project's
own statement that Magisk no longer handles root hiding.

*Authoritative for*: what the tool does. That FAQ answer is one of the most
load-bearing sentences in Part VI: the denylist is a list of processes a
provider will not modify, not a concealment mechanism.

**KernelSU's App Profile documentation** — `kernelsu.org`, guide section. The
source for the "Umount modules" option, its default, and the caveat that below
kernel 5.10 the option is merely a configuration setting unless `path_umount`
has been backported.

*Authoritative for*: KernelSU's documented behaviour and its kernel-version
caveat.

*Not authoritative for*: KernelSU-Next, which is a separate project
(`KernelSU-Next/KernelSU-Next`) and the one on this book's rig. Where behaviour
matters, check the fork you are running.

**Shamiko** — distributed as release archives from `LSPosed/LSPosed.github.io`,
by LSPosed Developers. Chapter 23's description comes from the README,
`module.prop` and `service.sh` inside a release archive, plus the published
release notes. Read those files yourself; they state the module's scope in its
own words, including the whitelist mode's documented performance cost.

*Authoritative for*: what the project says it does, in its shipped files.

*Not authoritative for*: how it does it. There is no public source repository to
read, so nothing about its internals can be sourced, and this book does not
describe any. It is also not a wrapper that conceals *your* module: its subject
is traces of the root implementation, of Zygisk, and of modules as a category.

**Play Integrity API documentation** — Android's developer documentation. The
source for this book's account of attestation as a class: verdicts built on
hardware-backed signals, evaluated by the app's own backend server rather than
on the device.

*Authoritative for*: the API's own model — what verdicts exist, how a response
is meant to be verified server-side.

*Not authoritative for*: anything about circumventing it, which is not this
book's subject and not a use these pages are cited to support. See
[Rules of engagement](/ZygiskLab/book/foundations/02-rules-of-engagement/) and
[The defensive chapter](/ZygiskLab/book/footprint/25-the-defensive-chapter/).
Chapter 23's conclusion stands: process-level concealment and platform
attestation are different problems, and no amount of the first addresses the
second.

## Root implementations

**Magisk** — `topjohnwu/Magisk`. Systemless root, the module format this book's
modules use, and the original Zygisk. Its documentation is the reference for
module structure, installation scripts, and the boot process.

**KernelSU** — `tiann/KernelSU`, documentation at `kernelsu.org`. A kernel-based
root solution with no Zygisk of its own, which is why Zygisk Next exists.

**KernelSU-Next** — `KernelSU-Next/KernelSU-Next`. The fork on this book's rig
(KernelSU-Next 3.3.0). Its behaviour tracks upstream KernelSU closely but not
identically, and where this book states a rig behaviour it means this fork at
that version.

*Authoritative for*: each project's own installation, module format and
management surface.

*Not authoritative for*: each other. A statement about one root implementation
is not a statement about another, and the rig's combination — Pixel 6 Pro,
Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5 — is one point in a
large space.

:::note
Nothing here is recommended as a product, and nothing here is listed as a route
around any particular service's checks. These are the sources this book read in
order to describe mechanisms accurately. If you find a claim in this book that
one of them contradicts, the source wins.
:::

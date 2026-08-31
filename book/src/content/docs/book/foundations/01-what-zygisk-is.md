---
title: "What Zygisk is, and what it is not"
description: "Zygote, specialization, where Zygisk inserts itself, and how it compares to Xposed, Frida, and LD_PRELOAD."
sidebar:
  order: 1
status: unverified
---

Every app process on Android is a copy of the same process. Not a fresh program
loaded from disk, but a fork of a long-lived daemon that was started at boot,
filled with the runtime and most of the framework, and then left running for the
sole purpose of being copied. Zygisk exists because that daemon is the single
best place in the system to put your code: everything an app will ever run
passes through it, and passes through it before the app's own code has executed
a line.

This chapter is the mental model the rest of the book assumes. It explains what
zygote is, what specialization does to a forked child, exactly where Zygisk
inserts itself in that sequence, and — just as important — what Zygisk is not,
so you do not spend a week looking for an API that was never there.

## Zygote: forking from a warm template

Starting a Java-language process is expensive. You have to create an ART
runtime, initialise the heap, load and verify the core library classes, load the
framework classes the app will inevitably touch, and open the shared resource
tables. Doing that once per app launch would make Android feel unusable on any
hardware.

So Android does it once, at boot. `init` starts the zygote daemon, which creates
the runtime, preloads a large set of framework classes, preloads system
resources and drawables, and preloads a number of shared libraries. Then it
stops, and listens on a socket.

When you tap an icon, `ActivityManagerService` — inside `system_server` — sends a
request down that socket. Zygote forks. The child is an exact copy of an already
warm process: runtime up, classes loaded, resources mapped. Because of
copy-on-write, the child does not even pay for the memory; the preloaded pages
stay shared with the parent until somebody writes to them. That fork is why app
launch is measured in hundreds of milliseconds rather than seconds, and why the
framework's memory cost is paid roughly once rather than once per app.

Two structural consequences matter to you.

First, **every app is downstream of one process.** Not "most apps", not "apps
that opt in". If it runs framework code, it was forked from zygote. On a 64-bit
device you will usually find two zygotes, one per ABI, plus child zygotes spawned
for isolated processes such as the WebView sandbox; the principle is unchanged.
Code placed in zygote reaches all of them.

Second, **the fork happens before the app exists.** At the instant of the fork
the child has no package name, no uid of its own, no app sandbox. It is still, in
every meaningful sense, zygote. The app's `Application` class has not been
constructed, its APK has not been added to the class loader, its native libraries
have not been loaded. If you want to observe or change something before an app
can defend itself, this is the only moment in the system where that is
structurally guaranteed.

:::note
Zygote's other important child is `system_server`, forked once at boot rather
than per app. It hosts most of the platform services. Zygisk gives you the same
before/after entry points there, which is why the API has both
`preAppSpecialize` and `preServerSpecialize`. This book is mostly about app
processes; `system_server` is a much sharper knife.
:::

## Specialization: a process crossing a privilege boundary mid-life

The fork gives you a warm, unprivileged-by-nothing copy of zygote. It is not yet
an app. Turning it into one is called **specialization**, and it is the single
most important concept in this book.

Specialization is the work the forked child does to itself, in the window between
`fork()` returning and the app's entry point being called. Broadly, and with
details that vary by Android version, it:

- **sets the uid and gid**, plus supplementary groups, giving the process the app's
  identity and with it access to the app's data directory and nothing else;
- **applies the SELinux context** derived from the app's seinfo, moving the process
  out of zygote's domain into an app domain;
- **sets the process name** — the `nice_name` you see in `ps` — so the process is
  labelled with its package;
- **enters the app's mount namespace and sets up its storage view**, which
  determines what the process can even see of the filesystem;
- **drops capabilities**, so the child keeps only what an app is allowed to hold;
- applies resource limits, runtime flags and the seccomp filter.

Read that list again with the timing in mind. This is one process, with one pid,
that begins the sequence holding zygote's privilege and ends it inside an app
sandbox. It crosses a privilege boundary in the middle of its own life, without
`exec`, without a new process, without anything that a naive observer would call
a transition at all.

That boundary is the spine of this book. Almost every hard question in Zygisk
module development reduces to *which side of specialization am I on?* Before it,
you have zygote's privilege and zygote's view of the filesystem, but no app: you
do not yet know for certain what the process will become, and any state you leave
behind has to survive the crossing. After it, you are inside the app, with its
uid, its SELinux domain, its mount namespace — you can see the app's world, and
you can no longer reach out of it.

Part III, starting at
[The specialization window](/ZygiskLab/book/prespecialize/08-specialization-window/),
is entirely about the near side. Part IV, starting at
[What changed at the boundary](/ZygiskLab/book/postspecialize/12-what-changed-at-boundary/),
is entirely about the far side. The two halves of the book are the two halves of
this sentence.

## Where Zygisk inserts itself

Zygisk arranges for a provider-supplied component to be present in the zygote
process, and for module code to be invoked around specialization in each forked
child. From your module's point of view the contract is five methods —
`onLoad`, plus a pre/post pair for app specialization and another for
`system_server` — and the upstream header states the crucial point plainly: modules are loaded *after*
zygote has forked the child, so **your code always runs in the app or
`system_server` process, never in the zygote daemon itself**. "Running in
zygote" is a useful shorthand for the privilege you inherit, not a description of
the process you are in.

The four points, in order of a single app launch:

1. `preAppSpecialize(AppSpecializeArgs *args)` — the child has forked and still
   carries zygote's privilege. The arguments that specialization is about to
   apply are handed to you as mutable references: `uid`, `gid`, `gids`,
   `runtime_flags`, `se_info`, `nice_name`, `app_data_dir` and more. You can read
   them, and you can write them.
2. Specialization runs.
3. `postAppSpecialize(const AppSpecializeArgs *args)` — you are now inside the
   app's sandbox, with the app's privilege, before the app's own code runs. Note
   the `const`: the arguments are now history.
4. Zygisk unloads itself from the process. The API handle stops working after
   `post[XXX]Specialize`; anything you want to persist must already be installed.

`preServerSpecialize` and `postServerSpecialize` are the same shape for
`system_server`, with a different argument struct that includes the permitted and
effective capability masks.

The API you get alongside those callbacks is deliberately small: connect to a
root companion process over a socket, get a file descriptor for your module
directory, set a couple of options, read state flags, exempt a file descriptor
from zygote's automatic close, register PLT hooks and commit them, and replace
registered JNI native methods for a class. That is the whole surface as of API
version 5, which is the version this book targets. Appendix A walks it method by
method.

### Who provides it

Zygisk is an *interface* with more than one implementation. That distinction is
easy to miss and expensive to learn late.

**Magisk** ships a built-in Zygisk implementation, toggled in the Magisk app.
This is the original, and the API header is Magisk's.

**Zygisk Next** is a standalone provider distributed as a module, commonly used
on **KernelSU** and KernelSU-Next — which have no Zygisk of their own — and also
usable alongside Magisk. It implements the same module API, so a correctly
written module builds once and loads under either.

The reference rig for this book is a **Pixel 6 Pro, Android 16, arm64,
KernelSU-Next 3.3.0, Zygisk Next 1.4.5**. Where behaviour is known to diverge on
Magisk, the book flags it in a call-out; those divergences are not separately
verified on a Magisk device, and the book says so where it matters.

:::caution
"Same API" is not "same behaviour". Providers differ in how modules are
discovered and loaded, in how denylist or exclusion enforcement interacts with
your process, in what the module directory looks like, and in how aggressively
they hide themselves — which changes what your module can see and what it leaves
behind. Treat provider identity and version as part of your environment, and
record it with every result.
[Where it breaks](/ZygiskLab/book/companion/20-where-it-breaks/) returns to this
in detail.
:::

## What Zygisk is not

**Zygisk is not a hooking framework.**

This is the correction most readers need, and needing it is not a failure of
reading — it is a reasonable expectation formed by Xposed. Xposed and LSPosed
give you a hooking API: name a Java method, supply before/after callbacks, and
the framework handles the mechanics. People arrive at Zygisk expecting the same
thing and go looking for the equivalent call.

It is not there, because that is not what Zygisk does. Zygisk gives you **code
execution in the right process at the right moment**. What you do with that
execution is entirely your problem. The two conveniences in the API —
`hookJniNativeMethods`, which swaps the function pointers in a class's registered
JNI natives, and the PLT hook pair, which rewrites entries in the procedure
linkage table of a mapped ELF — are narrow tools, not a framework. Neither one
hooks an ordinary Java method.

If you want to intercept a Java method, you bring an ART hooking engine yourself
and you deal with the consequences: ART internals are version-specific and not a
stable interface, which is
[Chapter 15](/ZygiskLab/book/postspecialize/15-hooking-java-through-art/)'s whole
subject. If you want to intercept a native function that is called through the
PLT, the built-in registration may be enough; if it is called some other way, you
bring an inline hooking engine. If you want to change behaviour without hooking
at all — by editing specialization arguments, or by placing a file where the app
will read it — that is often the cleaner answer, and this book prefers it where
it works.

The right way to hold it: Zygisk is a *placement* mechanism. It puts your `.so`
in the process you want, at the moment you want, with the privilege that moment
carries. Everything after that is ordinary native engineering.

## How it compares

Zygisk is not the right tool for every job. The honest comparison, on the axes
that actually decide a choice:

| | Injection point | Persists across reboot | Privilege | Detectability | Cannot do |
|---|---|---|---|---|---|
| **Zygisk** | Forked child, around specialization | Yes, while the provider is installed | Zygote's, then the app's | Moderate; leaves loader, memory and namespace traces | Hook Java for you; touch the kernel; escape SELinux |
| **Xposed / LSPosed** | Inside ART, after the runtime is up | Yes | The app's | Well-known signatures; widely probed for | Run before the runtime; do much natively |
| **Frida** | Attach or spawn, agent injected at runtime | No — re-attach after reboot | Whatever the frida-server holds | High while attached: ports, threads, mapped agent | Be always-on unattended; stay quiet |
| **`LD_PRELOAD`** | Dynamic linker, at process start | Only if you can set the environment persistently | The process's own | Low in itself; visible in maps and environment | Reach zygote-forked apps; hook Java; affect statically resolved calls |
| **Repackaging the APK** | Build time, inside the app | Yes — it is the installed app | The app's | Signature no longer matches the original | Survive integrity checks; work on apps you cannot rebuild |

Reading that table fairly:

- **For interactive exploration, Frida is better than Zygisk**, and it is not
  close. Attach, script in JavaScript, change your mind, reload. Use Frida to
  find out what to hook; use Zygisk when you need the change to be there
  unattended on every launch, including the first one after a reboot.
- **For hooking Java methods by name, LSPosed is better than Zygisk**, because
  that is precisely what it was built to do. Reach for Zygisk when you need to be
  earlier than LSPosed can be, when the work is native, or when you need the
  privileged pre-specialization window.
- **For a single app you control, repackaging is simpler than all of them.** No
  root, no provider, no zygote. It fails when the app checks its own signature or
  when you cannot rebuild it.
- **`LD_PRELOAD` is the classic answer and mostly does not apply here**, because
  app processes are forked from an already-running zygote rather than
  `exec`-ed — there is no fresh linker invocation for your preload to ride in on.
  It remains a good tool for native binaries you launch yourself.

Zygisk's genuine advantage is narrow and real: **earliest execution, highest
privilege, unattended, on every app.** When you need that combination, nothing
else on the list offers it. When you do not, use something simpler.

## Honest limits

State these to yourself before you start, not after a week of debugging.

**No kernel access.** Your module is userspace. `pre[XXX]Specialize` runs with
zygote's privilege and `post[XXX]Specialize` runs with the app's; neither is a
kernel context. Root work happens in the separate companion process
([Part V](/ZygiskLab/book/companion/17-the-companion-process/)), and even that is
a root userspace daemon, not a kernel module.

**No SELinux bypass.** SELinux applies to you exactly as it applies to everything
else. Several restrictions in the API exist for this reason and the header says
so outright: `connectCompanion` only works pre-specialization because of SELinux,
and the module directory fd is only usable pre-specialization or in the companion
process, for the same reason plus uid. If your design needs a process to read
something its domain is not allowed to read, the design is wrong, not the
platform.

**Architecture-specific native code.** You are shipping compiled code into
processes of a particular ABI. Modules carry per-ABI libraries, and a device may
run both 32- and 64-bit zygotes; the companion connection is ABI-aware for the
same reason. Any hooking you add on top — PLT rewriting, inline hooking, ART
internals — is more architecture-sensitive still.

**A hard dependency on your provider.** No provider, no module: your code does
not run at all. Provider updates change loading behaviour, hiding behaviour and
occasionally the API surface, and a Magisk or Zygisk Next release can break a
module that worked yesterday. This is the ordinary condition of the work rather
than an unusual hazard, and the book treats it that way.

**A bug here takes the device with it.** Your code runs in a process every app is
forked from, before any of them exist. Crash it badly enough and you get a
bootloop, not a stack trace. That is why
[Deploying without bricking zygote](/ZygiskLab/book/load/07-deploying-safely/)
comes before any of the interesting work.

## Before you continue

You should now be able to answer these without looking anything up. If one of
them is still fuzzy, re-read the section rather than carrying the gap forward —
every later chapter leans on all five.

- Why does Android fork apps from a warm daemon instead of starting them fresh?
- What concrete changes does specialization make to a forked child?
- Which of your module's callbacks run before that boundary, and which after?
- Why is "my code runs in zygote" a shorthand rather than a fact?
- What does Zygisk hand you, and what must you bring yourself?

And one thing this chapter cannot tell you: how any of it behaves on *your*
device, with *your* provider at *your* version. Everything above is mechanism.
The rig comes together in
[The rig and the toolchain](/ZygiskLab/book/foundations/03-rig-and-toolchain/),
and nothing in this book is marked verified until it has run there.

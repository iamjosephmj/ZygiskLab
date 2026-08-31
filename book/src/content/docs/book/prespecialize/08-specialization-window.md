---
title: "The specialization window"
description: "What exists and does not exist in the preAppSpecialize window, and why your code must be nearly free to run."
sidebar:
  order: 1
status: unverified
---

The process you are standing in has no name. It has zygote's uid, zygote's
SELinux domain and zygote's mount namespace, and a heap full of framework
classes that were loaded at boot for an app that did not exist yet. It also has
a pending request — a uid, a `nice_name`, a seinfo string — describing the app it
is about to become. It is not zygote, because zygote is still alive on the other
side of the fork, listening on its socket. It is not the app, because nothing
app-specific has happened to it yet. For the length of your `preAppSpecialize`
callback, it is neither, and you are the only code running in it.

That is the window. Part III is about working inside it, and this chapter is
about its shape: what is there, what is not there, what you are about to lose,
and what it costs the whole device every time your callback returns. Chapters 9
through 11 teach the three things you actually do here — read the arguments,
set the options, and decide not to run. This chapter is the one that tells you
why the third is the most important of them.

## What exists at this moment

Start with what the fork gave you, because it is more than beginners expect and
less than they hope.

**A complete ART runtime, warm.** The heap is initialised, the JIT and GC
threads existed in the parent, the core library classes are loaded and verified,
and the framework classes zygote preloads at boot are resident. This is the
entire reason Android forks apps instead of starting them. None of it is yours,
and none of it is app-specific: what is loaded is what `system_server` decided
every app would probably need, decided once, at boot, years of releases ago.

**Zygote's identity.** Same uid, same gid set, same capability set, same SELinux
context, same mount namespace, same open file descriptors. `fork()` copies all
of it. The child is a byte-for-byte duplicate of the parent's address space at
the moment of the call, with copy-on-write doing the work of not actually
copying it.

**Your module, freshly loaded.** As
[Chapter 5](/ZygiskLab/book/load/05-anatomy-of-a-module/) established, your `.so`
is loaded into the *child*, after the fork — never into the zygote daemon. Your
globals were constructed seconds ago in this process and exist nowhere else.
`onLoad` has already run; you are holding whatever you stashed there.

**The specialization request, mutable.** `AppSpecializeArgs` is not a
description of the process. It is a description of the process the framework
intends to create, handed to you as references before anything acts on it.
That distinction is [Chapter 9](/ZygiskLab/book/prespecialize/09-reading-appspecializeargs/)'s
whole subject.

## What does not exist

Now the absences, because every one of them has been the cause of somebody's
afternoon.

**There is no app classloader.** The only classloader in this process is the one
zygote booted with. It can see the framework and the boot classpath. It cannot
see a single class from the app's APK, because the APK has not been added to any
classloader — that happens later, in the framework code that constructs the
`LoadedApk`. `FindClass` for `android.app.Activity` will resolve. `FindClass` for
anything in the app's own package will not, and it will not resolve in
`postAppSpecialize` either, which is why
[Chapter 13](/ZygiskLab/book/postspecialize/13-jni-inside-a-live-app/) exists as
a separate chapter rather than a paragraph.

**There is no `Application` object.** No `Context`. No `ActivityThread` bound to
a package. No resources, no asset manager for the app, no
`PackageManager` handle that knows who you are. Anything you have ever written
in Android that started with a `Context` starts with something that does not
exist here.

**The app's data directory is not reachable as the app.** The path is *in* your
hands — `args->app_data_dir` is one of the fields — but the process holding it is
still zygote, in zygote's mount namespace, before the app's storage view has
been set up and before its uid has been applied. Having the string is not having
access. What an injected process can actually write, and when, is
[Chapter 12](/ZygiskLab/book/postspecialize/12-what-changed-at-boundary/)'s
table.

**It is not the app's SELinux context.** The seinfo that will produce the app
domain is sitting in `args->se_info` as text. It has not been applied. Every
access-control decision the kernel makes for you right now is made against
zygote's domain, not the app's.

**There is no process name.** `nice_name` is pending. In `ps`, in
`/proc/self/cmdline`, in any log line your provider emits, this process is still
labelled as zygote — or as whatever your provider chose to call it — because the
call that renames it has not happened.

**And you do not know what this will become.** It is probably an app. It might
be a child zygote: the WebView sandbox and isolated-process hosts fork from
zygote and then become zygotes themselves, and `args->is_child_zygote`, when the
pointer is non-null, tells you so. A module that assumes "pre-app specialize
means an app" is wrong in exactly the processes where being wrong is most
awkward.

:::note
Almost every one of these absences is a *timing* fact, not a permission fact.
The classloader, the `Application`, the name — they all arrive later, in the
framework's own code, well after specialization. Being early is the whole value
of this window and the whole source of its constraints, and you do not get to
have one without the other.
:::

## What you have that you are about to lose

If the window were only about being early, you could wait for
`postAppSpecialize`, which is also before the app's code and considerably more
comfortable. People run code here for the other reason: this is the last moment
the process is privileged.

**Root-ish privilege.** Zygote is not a general-purpose root shell, but it holds
a uid and capability set that no app process holds, and it holds them until
specialization drops them. Whatever the process is allowed to do, it is allowed
to do here and not afterwards.

**An unrestricted view of the filesystem.** Zygote's mount namespace, before the
app's storage view is applied and before any denylist unmounting takes effect.
Paths you can open here may be invisible a millisecond later — not permission
denied, *absent*, because the mount that made them visible was replaced.

**A working `connectCompanion()`.** The API header is explicit that this is
pre-specialize only, and the reason it gives is SELinux: after the boundary your
domain is not permitted to reach the companion's socket. The same goes for
`getModuleDir()`. If any part of your design needs root work done, the
connection to root is opened here or it is never opened at all
([Part V](/ZygiskLab/book/companion/17-the-companion-process/)).

**`exemptFd()`.** Specialization closes file descriptors that were not exempted.
This is the only callback where you can ask for one to survive, and
[Chapter 10](/ZygiskLab/book/prespecialize/10-setoption-and-flags/) covers the
awkward part, which is that its return value cannot distinguish success from
no-op.

So the design question that the rest of the book keeps returning to is a
scheduling question: **does this piece of work need privilege, or does it need
the app?** Anything that needs privilege happens here — a socket opened, a
descriptor exempted, a configuration read, a decision made — and its *results*
carry across the boundary as ordinary process state. Anything that needs the
app's libraries, the app's filesystem view or the app's runtime waits. Getting
that split right is most of module architecture.
[Chapter 12](/ZygiskLab/book/postspecialize/12-what-changed-at-boundary/)
tabulates exactly what changes; here, hold the principle.

## What you must not do here

Three prohibitions, each with a mechanism rather than a warning.

### Heavy work delays every app launch

Your callback runs on the critical path of process creation. `system_server`
asked zygote for a process; the user is looking at a launch animation; the
framework is waiting for this child to finish specializing and report back.
Nothing in that chain proceeds until your callback returns.

The cost is not paid once. It is paid for every process the provider injects
into, which by default is a great many of them — every app launch, every
`:remote` process an app spawns, every isolated process, every restart after a
force-stop or a low-memory kill. Ten milliseconds of well-meaning startup work
is ten milliseconds added to every cold start on the device, forever, spread
across processes whose owners have no idea your module exists.

The user does not experience this as "the module is slow". They experience it as
the *phone* being slow, and they will not connect the two. That asymmetry —
diffuse cost, invisible cause — is why this is a discipline question rather than
an optimisation question.

### Threads do not survive the way you expect

This is the one that is genuinely counter-intuitive, so be precise about the
mechanism.

**`fork()` gives the child exactly one thread: the one that called it.** This is
POSIX, not an Android quirk. Every other thread that existed in the parent is
simply absent in the child. What is *not* absent is the memory those threads
were using: their mutexes, their heap state, their half-finished work are all
copied faithfully into the child's address space, in whatever state they were in
at the instant of the fork. A lock held by a thread that no longer exists is a
lock that will never be released. This is why POSIX restricts a forked child of
a multithreaded process to async-signal-safe functions until it `exec`s, and why
`pthread_atfork` handlers exist at all — the runtime and the framework do
substantial work to make zygote's children usable, and they do it for the
threads *they* know about.

You are downstream of all that, and you are about to make it worse in a
different way. A thread you create in `preAppSpecialize` is a thread running
inside a process that is, immediately afterwards, going to have its uid changed,
its capabilities dropped, its SELinux context switched, its mount namespace
replaced and its unexempted file descriptors closed — none of it coordinated
with you. Your thread does not get a notification. It holds whatever it was
holding: a file descriptor that is about to be closed under it, a path that is
about to stop existing, a socket to a companion that its new domain is no longer
allowed to talk to. It keeps its own copy of nothing, because there is only one
address space and the change happens to the whole process.

Add to that the fact, from
[Chapter 5](/ZygiskLab/book/load/05-anatomy-of-a-module/), that the `Api` handle
becomes a set of silent no-ops after `post[XXX]Specialize` — and that with
`DLCLOSE_MODULE_LIBRARY` set your library is unmapped out from under any thread
still executing in it — and the picture is complete. A background thread started
here is a thread whose world is rewritten while it runs.

:::danger
Starting a thread in `preAppSpecialize` is not a race you can win by being
careful with locks. The thing changing underneath the thread is the process's
identity, and there is no synchronisation primitive for that. If you need
concurrency, either do the work in the companion — a separate, stable, root
process ([Chapter 17](/ZygiskLab/book/companion/17-the-companion-process/)) — or
start the thread after the boundary, where at least the ground has stopped
moving ([Chapter 16](/ZygiskLab/book/postspecialize/16-threading-and-timing/)).
:::

### Touching the JVM early destabilises the fork

You are handed a `JNIEnv *` in `onLoad`, and it is tempting to treat that as
permission to use the runtime. Be careful with it, and be careful in a specific
way.

The runtime in this process is mid-transition. It was forked from a
multithreaded parent, so by the rule above it is missing its threads; the
runtime's post-fork setup is designed to bring it back to a usable state for the
framework's own code path, and that path does not include arbitrary work from a
third party at an arbitrary point in it. Class loading can trigger
initialisers. Allocation can trigger collection. Exceptions cross native
boundaries. Each of those is a mechanism that assumes a runtime in a settled
state, being driven by the code the runtime expects to be driven by.

I am deliberately not telling you what the crash looks like, because it is not
one crash. What goes wrong depends on the Android version's runtime, on the
provider, on which operation you attempted and on what the framework happened to
be doing. The honest statement is at the level of mechanism: **the runtime here
is not in a state that expects arbitrary work, and the failures are not reliably
reproducible enough to design around.** Treat the `JNIEnv` in this window as
useful for reading the strings the arguments hand you — and even that has rules,
which is [Chapter 9](/ZygiskLab/book/prespecialize/09-reading-appspecializeargs/)
— and for nothing else. Real JNI work belongs after the boundary, where
[Chapter 13](/ZygiskLab/book/postspecialize/13-jni-inside-a-live-app/) picks it
up.

## The cost model

Now the part that decides how you write everything else in Part III.

Set the scale first. Your callback runs once per specialization. A phone in
ordinary use forks a lot of processes: launches, restarts, background wakeups,
multi-process apps, isolated processes. Call it hundreds a day, conservatively,
and the number does not matter as much as its shape — it is large, it is
recurring, and it is entirely made up of processes you do not care about. A
module armed for one package is not interested in essentially all of them.

So the arithmetic is stark. Your *interesting* path runs a handful of times a
day and can afford to be expensive. Your *uninteresting* path runs constantly
and can afford almost nothing. The total cost your module imposes on the device
is dominated, by orders of magnitude, by the branch where you do nothing.

That gives you three rules, in priority order.

**Decide fast.** The first thing your callback does is answer "is this mine?",
and that answer must be cheap: a comparison against something already in memory,
not a file read, not a socket connection, not a JNI call you could have avoided.
Any work you do before the decision is work you do for every process on the
device. This is why "where the arming configuration lives" is an architecture
question rather than a detail — reading it once and caching it is a different
module from reading it per launch.

**Return.** When the answer is no, return immediately, having allocated nothing
and connected to nothing. Do not connect the companion "just in case". Do not
open the module directory to see what is in it. Do not log — a log line per
process is a measurable cost and a large footprint, and
[Part VI](/ZygiskLab/book/detection/21-what-a-module-leaves-behind/) will have
opinions about it.

**Leave nothing behind.** A no-decision should leave the process
indistinguishable from one your module never touched, as far as you can manage
it. That is partly cost and partly footprint, and the two arguments point the
same way.

There is a fourth rule that is really a corollary: **the interesting path is
allowed to be slow, and you should spend the budget there rather than smearing
it thinly across everything.** A module that spends 30ms setting itself up
properly in the one process it cares about, and 20 microseconds deciding against
every other, is a better citizen than one that spends 2ms everywhere.

[Chapter 11](/ZygiskLab/book/prespecialize/11-choosing-not-to-run/) turns this
into structure — where the decision lives, how the not-interested path is made
the cheapest path, and where the arming configuration comes from — and
[Lab 3](/ZygiskLab/labs/lab-03-choosing-not-to-run/) turns it into evidence: a
module armed for exactly one package, with a measurement of what the unarmed
path costs every other launch on the device. Producing that number yourself is
the point. Until you have measured your own no-op, you are guessing about the
cost you impose on everything you are not interested in.

## Before you continue

Nothing in this chapter has been run on the reference rig — Pixel 6 Pro,
Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5 — and two classes of
claim here deserve to be held loosely until it has.

The first is provider behaviour. *Which* processes your module is loaded into,
and therefore how often your no-op path runs, is decided by Magisk or Zygisk Next
and their scope or denylist settings, not by the API. The cost model's arithmetic
is right in shape and unverified in magnitude on your device.

The second is the runtime. The mechanism above — a forked child with one thread,
a runtime mid-transition, a specialization about to rewrite the process's
identity — is sound, and the specific failures that follow from it vary by
Android version and by provider. Where this chapter declined to give you a crash
signature, that was not caution for its own sake; it is that the signature is not
stable enough to be worth memorising.

What you should carry forward is the shape of the window rather than any number
in it: privileged, nameless, brief, and on the critical path of the entire
device.

---
title: "What changed at the boundary"
description: "The uid, SELinux, mount namespace, and filesystem changes at the postAppSpecialize boundary, and what they rule out."
sidebar:
  order: 1
status: unverified
---

You are the app now. Same pid, same address space, same globals you set in
`onLoad` — and a different process in every way that the kernel cares about. Your
uid is the app's. Your SELinux domain is an app domain. Your mount namespace is
the app's, with the app's storage view applied and whatever the provider chose to
unmount already unmounted. Capabilities zygote held are gone. File descriptors
you did not exempt are closed. None of that was done *to* another process: it was
done by this one, to itself, in the microseconds between your two callbacks.

[Chapter 8](/ZygiskLab/book/prespecialize/08-specialization-window/) described
the near side of that crossing — what you have, and what you are about to lose.
This chapter is its mirror image, and it is short on procedure because there is
no procedure. Nothing here is a technique. It is an inventory of what the process
is now, and an argument about what that inventory forbids. The table below is the
artefact: Chapters 13 through 16 assume it, and the whole of
[Part V](/ZygiskLab/book/companion/17-the-companion-process/) exists because of
it.

## The table

| | `preAppSpecialize` | `postAppSpecialize` |
|---|---|---|
| **uid / gid** | zygote's | the app's, plus its supplementary groups |
| **SELinux** | zygote's domain | an app domain derived from `se_info` |
| **Mount ns** | zygote's, unrestricted | the app's, storage view applied, provider unmounts done |
| **Capabilities** | zygote's set | an app's set — effectively none you can use |
| **Filesystem** | broad read across the device | the app sandbox and world-readable paths |
| **Descriptors** | zygote's, all open | unexempted ones closed |
| **Process name** | still zygote's | the app's `nice_name` |
| **Zygisk API** | fully live | live until this callback returns, then no-ops |

Two rows deserve a note before the rest of the chapter leans on them.

**"Effectively none you can use"** is a deliberate hedge on capabilities. The
kernel's capability model is finer than that, and what a given app process retains
depends on the Android version and on what the framework decides an app needs.
The engineering statement — the one that survives version changes — is that no
capability an app process holds will get you a privileged operation you could not
already do as an ordinary app. Do not design against a specific capability being
present.

**The filesystem row is the one that changes shape.** The other rows are
subtractions: you had a thing, now you have less of it. The filesystem row is a
*substitution*. Paths do not merely become unreadable; some of them stop
existing. Your mount namespace was replaced, so a path that resolved a moment ago
can now return `ENOENT` rather than `EACCES`, and the difference matters when you
are reading errno and drawing conclusions. A missing file is not evidence that
the file is missing.

:::note
The Zygisk API row is the one people trip over most in code review. `connectCompanion()`
and `getModuleDir()` are documented in the header as pre-specialize only, and the
reason given is SELinux and uid — not a policy choice by the provider. Everything
else on the `Api` handle survives until `postAppSpecialize` returns and then
stops, because Zygisk unloads itself from the process. There is no error, no
return code you can branch on. It just stops.
:::

## What an injected app process can actually write

This is the section readers will copy, so read the caveat before the list rather
than after it: **everything below is what this book expects on the reference rig —
Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5 — and none
of it has been run there yet.** Filesystem reachability is decided by SELinux
policy, by uid and gid, by the storage view the framework installed, and by
whatever the provider unmounted, and all four of those vary by Android version,
by OEM and by provider. Treat the list as a starting hypothesis and confirm it on
your own device. A five-line test that writes a file, reads it back, and prints
`errno` on failure is worth more than any table in any book, including this one.

**The app's own cache directory is the one dependable place to write.** After
specialization the process is the app, so `/data/user/0/<pkg>/cache/` — the path
`Context.getCacheDir()` would hand you if you had a `Context` — is owned by your
uid and labelled with the app's data type. You can create files there without a
`Context` and without any framework help, because uid and SELinux are all the
kernel is consulting. Root can read those files back afterwards, which is what
makes the directory useful as an output channel rather than merely as scratch
space. `files/` alongside it is equally reachable; `cache/` is conventional for
module output because the platform is permitted to delete it under storage
pressure, which is the correct semantics for a diagnostic artefact.

Note the `0` in that path. It is the Android user id, not a constant — a work
profile or a secondary user is `10`, `11`, and so on. The app's real data
directory arrives as `args->app_data_dir` in `preAppSpecialize`
([Chapter 9](/ZygiskLab/book/prespecialize/09-reading-appspecializeargs/)), which
is a better source than a path you assembled yourself.

**`/data/adb/...` is not reachable.** This is where module providers keep modules,
their own state and their databases, and it is root-owned and SELinux-restricted
so that exactly this cannot happen. You could not read it as an ordinary app and
you cannot read it as an injected one, because you *are* an ordinary app as far as
the kernel is concerned. The provider's own hiding behaviour may additionally
remove it from your namespace, so you may get "does not exist" rather than
"denied". Neither answer is a bug and neither is worth working around.

**Paths under `/storage/emulated/0` are governed by scoped storage.** Shared
storage stopped being a filesystem question years ago; access runs through the
media provider and the permissions the *app* holds, and an injected module does
not have a permission the host app lacks. Worse, what you see there depends on
the storage view installed during specialization, so the same path can mean
different things in different processes. Writing your module's output to shared
storage looks like the obvious answer and is a reliable way to spend an afternoon
on `EACCES`.

**Also gone, and worth naming:** the module directory, which the header restricts
to the pre-specialize methods and the companion; the companion socket, which
cannot be opened from here for SELinux reasons; and any descriptor you opened
before the boundary and did not pass to `exemptFd()`
([Chapter 10](/ZygiskLab/book/prespecialize/10-setoption-and-flags/)).

:::caution
Do not use a write failure here as a signal about your device's security posture,
and do not use a write *success* as licence to build on it. If you find yourself
able to write somewhere this section says you should not be, you have most likely
found a policy quirk of one Android build rather than a general property — verify
it, write down the build, and design as if it could disappear at the next update.
:::

## Why this table dictates your architecture

Now the consequence, stated bluntly, because it rules out the design almost
everybody reaches for first.

The intuitive shape of a module is: the injected code does the work. It decides
what to do, it does the privileged parts, and it reports what happened to
somewhere central — a log under `/data/adb/`, a status file the module's UI reads,
a socket to a daemon. That design is coherent, it is how you would write the same
feature on a desktop, and it does not survive the boundary. Every one of those
three moves — privileged work, a central write, an outbound connection — is a move
the table has already taken away.

What is left is a split, and it is the same split
[Chapter 8](/ZygiskLab/book/prespecialize/08-specialization-window/) asked you to
hold as a principle:

**Anything requiring privilege happens before specialization, or in the
companion, or not at all.** Before the boundary you have zygote's identity and
you can open the connection to root. After it you have neither, and no amount of
care in the injected code recovers them. The companion is a separate, persistently
root process that your module talks to over a socket opened while you still could
— [Chapter 17](/ZygiskLab/book/companion/17-the-companion-process/) introduces it,
[Chapter 18](/ZygiskLab/book/companion/18-companion-protocol/) covers the protocol,
and [Chapter 19](/ZygiskLab/book/companion/19-asymmetry-of-privilege/) tabulates
the asymmetry directly against this chapter's table.

**Anything the app process produces leaves through a channel the app process is
allowed to use.** In practice that means a file in the app's own directory, read
back later by something with root, or a message written into a socket you opened
before the boundary and kept alive across it. The injected side does not push
status to a central place; it deposits it where it is permitted to, and a
privileged reader collects it.

The consequence that surprises people is what this does to *live status*. A
module manager UI that wants to show "module active in `com.example.app`, 412
frames processed" cannot get that from one reporter. The injected process knows
the interesting facts and cannot write anywhere the UI can read. The companion
can write where the UI reads and knows almost nothing about what is happening
inside the app — it sees connections and requests, not behaviour. So live status
is *assembled*: two independent writers, each recording what it can see, joined on
the root side where both are readable.
[Chapter 19](/ZygiskLab/book/companion/19-asymmetry-of-privilege/) treats that as
a design problem in its own right, including what happens when the two writers
disagree, which they will.

There is a fourth design this rules out that is worth naming because it is
tempting and quiet: **shared state between injected processes.** Two apps you
injected into are two app sandboxes with two uids. They share your `.so` on disk
and nothing else — no memory, no writable common directory, no signals. If your
module needs a fact known in one app to be known in another, the companion is the
only place that fact can live, and it gets there over the socket.

## The JVM is usable now, and the classloader is not what you want

One thing the boundary gave you rather than took away: the runtime is now safe to
use in a way it was not before.

The reason is timing rather than privilege. In `preAppSpecialize` the runtime was
mid-transition — freshly forked from a multithreaded parent, missing every thread
but yours, being reassembled by code that did not expect a third party in the
middle of it. By `postAppSpecialize` that reassembly is done. The process is a
settled app process with a working ART runtime, and it is one the framework is
about to hand to the app's own code. JNI here is ordinary JNI, with ordinary
rules, and [Chapter 13](/ZygiskLab/book/postspecialize/13-jni-inside-a-live-app/)
teaches those rules properly, including the ones about references and pending
exceptions that will bite you regardless of Zygisk.

But there is a catch shaped exactly like the rest of this chapter, and it is
worth seeing now so that Chapter 13 reads as a solution rather than a surprise.
You have a `JNIEnv`. `FindClass` works. `FindClass("android/app/Activity")`
resolves, because the framework is on the boot classpath and always was. And
`FindClass` for any class in the app's own package fails — not because of
permissions, but because the classloader you can reach from a native thread is the
system one, and the app's APK has not been added to it. The app's own classloader
is built later, by framework code that constructs the `LoadedApk`, and it is a
different object that native code has to go and find.

So the last thing the boundary changes is subtler than the rows in the table: you
have gained a usable runtime and inherited a lookup problem. The classes you care
about — the app's, the ones you came here to hook — are precisely the ones the
handle you were given cannot see. That is the problem Chapter 13 solves, and it
is the first real piece of engineering on this side of the boundary.

## Before you continue

Nothing in this chapter has been run on the reference rig, and the two halves of
it deserve different amounts of trust.

The mechanism is solid. Specialization sets uid and gid, applies an SELinux
context from the app's seinfo, enters the app's mount namespace and storage view,
drops capabilities and closes unexempted descriptors. That is what the operation
*is*, it is why the API restricts `connectCompanion()` and `getModuleDir()` to the
pre-specialize methods, and it does not change between builds.

The specifics are exactly as trustworthy as your own device. Which paths resolve,
which writes succeed, which denials show up as `EACCES` and which as `ENOENT` are
decisions made by a policy file and a framework version, and the correct response
to a list of paths in a book is to check it. Write to your app's cache directory
and read it back with root. Try `/data/adb/` and record the errno. Try a path
under `/storage/emulated/0` and see what you get. Those three results, written
down next to your Android version and provider, are worth more than this chapter
and will remain worth more when this chapter is out of date.

---
title: "Your footprint, stage by stage"
description: "The in-process, on-disk, and behavioural traces each earlier part leaves behind, walked back stage by stage."
sidebar:
  order: 1
status: unverified
---

:::caution[Detection and measurement]
This chapter covers detection mechanisms and measurement on systems you
own or are authorised to assess. See [Rules of engagement](/ZygiskLab/book/foundations/02-rules-of-engagement/).
:::

Your footprint is not a chapter's worth of cleanup work waiting at the end of
the project. It is the accumulated shadow of every decision you already made.
You chose to ship one `.so` per ABI, so there is a file with a path and an inode
mapped into the process. You chose static linking, so that mapping is a quarter
of a megabyte rather than a few pages. You chose to read your configuration
through `getModuleDir()`, so for a moment there was a descriptor onto a
root-owned directory in a process that has no business holding one. You chose to
commit a PLT hook, so a GOT slot in a system library now points at an address
that belongs to no system library. You chose a companion, so there was a
connected socket and a deposit in an app cache directory. None of those were
mistakes. Every one of them was the right answer to the problem in front of you
at the time, and every one of them is observable.

That is the thesis of this chapter, and the reason it opens Part VI rather than
closing Part V: the inventory only makes sense as a walk back through the book.
A list of things an app might look for is a checklist, and a checklist teaches
you nothing about the next module you write. A mapping from *decision* to *trace*
teaches you to predict the shadow of a design before you build it.

Two disclaimers first, and they are load-bearing.

**Nothing in this chapter has been measured.** Not on the reference rig — Pixel 6
Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5 — not anywhere. It
is a set of predictions derived from the mechanisms the earlier parts established
and from the API header. [A detection harness](/ZygiskLab/book/footprint/24-detection-harness/)
is where the predictions become numbers on your own device. Read this chapter as
the hypothesis and that one as the experiment.

**Roughly half of what an app can find is not yours.** The module directory
belongs to you. The provider's own directories, its manager package, its daemon
and its resident state belong to the provider, and no amount of care in your
module code removes them. Keeping that boundary sharp matters, because a reader
who confuses the two will spend weeks tuning a build to remove a trace that a
different program put there.

## In-process traces

The most direct evidence a process can gather about itself is the list of what
is mapped into it, and every mechanism in Part II lands here.

**The mapping itself.** Your library is `dlopen`ed, so it appears in
`/proc/self/maps` and `/proc/self/smaps` as a set of regions carrying a path, a
device and an inode, exactly like every other loaded object. This is not obscure
to obtain: it is a file read, in the process's own sandbox, needing no permission
at all. [Chapter 6](/ZygiskLab/book/load/06-how-the-loader-finds-you/) measured
Lab 1's `libzygisklab.so` at about 232KB, essentially all of it statically linked
C++ runtime. So the trace is not one page that might be mistaken for something
else; it is a quarter-megabyte region whose backing path is not in the APK, not
in `/system`, and not in `/apex`.

The size decision and the visibility decision were made together in Chapter 6,
and they push in opposite directions here. `c++_static` is why the mapping is
large. `-fvisibility=hidden` is why it is quiet: the dynamic symbol table of a
correctly built module exports `zygisk_module_entry`, plus
`zygisk_companion_entry` if there is a companion, and nothing else. Chapter 6
gave the correctness reason for that flag — accidental interposition — and named
the second reason without pursuing it. Here it is pursued. An exported symbol
table is readable by anything in the process, without root and without cleverness,
by walking the linker's list of loaded objects and following each one's
`DT_SYMTAB`. A module that leaks a symbol called `hook_openat` has published its
intent in a form that requires no reverse engineering to read.

The same argument applies to string constants, which no visibility flag touches.
Every module in `modules/` defines a `LOG_TAG` and calls `__android_log_print`
with legible messages. Those strings sit in the library's read-only data, in the
process's own address space, findable by a scan. That is a fair price for
debuggable labs, and it is worth knowing you are paying it.

**Descriptors.** `/proc/self/fd` is likewise readable by the process that owns it,
and each entry resolves to a path or a socket. Module 03 opens `target.txt`
through the descriptor `getModuleDir()` returns, and then does something worth
looking at again:

```cpp
int dirFd = api->getModuleDir();
int fd = openat(dirFd, kConfigFile, O_RDONLY);
close(dirFd);
```

[Chapter 11](/ZygiskLab/book/prespecialize/11-choosing-not-to-run/) argued for that
`close` on hygiene grounds. Read it again as a footprint decision: a directory
descriptor onto `/data/adb/modules/<id>/` outliving `preAppSpecialize` would be a
descriptor, in an app-uid process, onto a tree that process cannot open by path
and has no legitimate reason to hold. It survives specialization; it is
enumerable; it names your module in its target. Closing it turns a persistent
trace into a transient one. The same reasoning covers module 06's companion
socket, which is opened in `preAppSpecialize` and closed when the exchange
finishes — a held connected Unix socket to a root daemon would be a standing
entry in `/proc/self/fd` for the life of the process.

Transient is not absent. Both descriptors exist during specialization, and
anything sampling at that moment sees them. What you control is the window.

**Threads.** None of this book's modules spawn one. Module 05 makes that explicit
by borrowing the app's own main thread instead — hooking
`android.os.Process.setArgV0` so its work runs on the Looper the framework was
going to run anyway. That is a timing decision in Chapter 16's terms, and a
footprint decision here, because a thread you create is visible in
`/proc/self/task`, has a name you chose or failed to choose, and has a stack whose
return addresses point into your mapping. [Chapter 14](/ZygiskLab/book/postspecialize/14-hooking-native-symbols/)
forward-referenced exactly this point. Using an existing thread costs you control
over when you run; creating one costs you an entry in a directory the app can list.

**Hooks.** This is the trace that survives everything else, and the reason
`DLCLOSE_MODULE_LIBRARY` is not a footprint solution. Module 04 commits a PLT
hook on `openat`. What that means physically is that a GOT slot in one mapped ELF
now holds an address that does not fall inside the library the symbol resolves
from. A process can walk its own loaded objects, read each relocation, and compare
the stored address against the range where the defining library is mapped. A
mismatch is not evidence of a hook in general — lazy binding, `LD_PRELOAD`-style
interposition and legitimate instrumentation all produce them — but a slot
pointing into an anonymous or unfamiliar region is a specific and cheap thing to
find.

[Chapter 10](/ZygiskLab/book/prespecialize/10-setoption-and-flags/) made the
consequence precise and this chapter states the corollary: unloading your library
does not remove the hook, it orphans it. The GOT slot still points where it
pointed. Now it points into a region that either is unmapped or has been reused,
which is both a crash waiting to happen and a *louder* trace than before — a
pointer into nothing is more anomalous than a pointer into a library with a name.

The same logic applies to module 05's JNI method replacement, one layer up. A
`JNINativeMethod` whose function pointer does not land in any library the app
loaded is the ART-side version of the same mismatch.

## Process-level traces

Above the address space, the process itself carries state that differs from a
clean one.

**The mount namespace.** A systemless root framework works by bind-mounting; that
is the mechanism, and mounts are enumerable through `/proc/self/mountinfo`, again
by the process itself with no permission. `FORCE_DENYLIST_UNMOUNT` asks for the
provider's and its modules' mounts to be removed from this process's namespace
during specialization. Chapter 10 was careful about what that buys and this
chapter is careful about what remains.

What it addresses is a set of mount entries. What it does not address is
everything above: your mapping, your descriptors, your hooks, the provider's
resident state. And there is a second-order point that matters more than the
first. A namespace that has been cleaned is not necessarily a namespace that
looks *untouched*. Comparing an app process's mountinfo against a genuinely clean
device is a comparison of structure — counts, ordering, mount IDs, peer groups,
the presence or absence of entries a stock build would have. Whether a given
provider's unmount routine leaves a structurally ordinary result is a property of
that provider's implementation, not of your module, and it is precisely the kind
of thing the Chapter 24 harness exists to answer on your rig rather than in prose.

**Environment and properties.** Two related surfaces, both readable by the app.
The process environment is inherited through zygote; system properties are a
device-global namespace any process can enumerate. Providers and their managers
may set properties, and Android itself exposes properties whose values differ on
a device with an unlocked bootloader or a modified boot image. This book's modules
set neither an environment variable nor a property, so this row is empty for
*your* code — which is the useful fact. Nothing in the Zygisk API requires you to
publish anything here, so if a property naming your module exists on a device, you
put it there deliberately or your provider did.

:::note
Which properties matter, and what a given Android version exposes, is version- and
device-specific to a degree that makes a list in a book actively misleading.
[How an app looks for you](/ZygiskLab/book/footprint/22-how-an-app-looks-for-you/)
covers the mechanism of property inspection; the values belong to your device.
:::

## On-disk traces

Now the boundary that readers most often get wrong.

**Yours.** Your module directory under the provider's module root, containing at
minimum `module.prop` and `zygisk/<abi>.so`, plus whatever you put beside them.
For modules 03 through 06 that includes `target.txt`, the arming file
[Chapter 11](/ZygiskLab/book/prespecialize/11-choosing-not-to-run/) argued for over
a compile-time constant. That argument was about retargeting without a rebuild,
and [Chapter 19](/ZygiskLab/book/companion/19-asymmetry-of-privilege/) extended it
into a root-side control plane that writes the file. The footprint consequence is
that your module directory contains, in plain text, the package you are interested
in. It is root-owned and an unprivileged app cannot read it — but it is not
encrypted, not obfuscated, and entirely legible to anything that does have root,
including forensic tooling and the device's owner.

Module 06 adds two more of your own: the root-only file its companion reads, and —
under the two-writer status design — a deposit in the target app's cache
directory. That deposit is the one to notice. It lives inside the app's own
sandbox, written by a process wearing the app's uid, which is exactly why the
design works and exactly why it is discoverable: the app can read its own cache.
A file the app did not create, in a directory only the app and root can reach, is
about as legible a trace as this book produces. Chapter 19 accepted that cost
knowingly, because the alternative was no observability at all. Naming it here is
the point of the exercise.

**Not yours.** The provider's own installation tree, its daemon, its
`/data/adb/`-rooted state, and the manager app's package on the device. These are
present because the user rooted the phone, not because you wrote a module. You can
delete every file you own and all of them remain. Whether an app can see any of
them depends on what SELinux and the provider allow an app uid to stat, and that
is provider- and version-specific in ways this book does not attempt to enumerate.
[Existing answers, surveyed](/ZygiskLab/book/footprint/23-existing-answers-surveyed/)
looks at what the published solutions do about this layer; the entry in your
inventory is simply that it exists and that your module code has no authority over
it.

## Behavioural traces

A trace need not be a file, a mapping or a mount. It can be a difference in how
long something took.

Chapter 11 measured the cost of the unarmed path — a timestamp as the first
statement of `preAppSpecialize`, a second one just before returning — and argued
that the uninteresting case is the overwhelmingly common case, so its cost is your
module's real cost. Turn that measurement around and you have this chapter's
point. The same delta the module reports to you, the app can observe about itself.
Anything on the specialization path adds time to every launch of every process in
the provider's injection scope. An app that records its own time from process
start to a known early checkpoint has a number, and the number moves when a module
is present.

Be honest about how weak that signal is in isolation. Launch times vary with
thermal state, storage pressure, whether the app is cold or warm, what else the
device is doing, and the device model. A single measurement proves nothing. A
distribution gathered across many launches, compared against the same app's
distribution on the same hardware without the module, is a different matter — and
it is a comparison a large app's telemetry is in a far better position to make
than you are. This is the mechanism behind "the SDK reported an anomaly" that
Chapter 11 mentioned in passing.

Other behavioural traces follow the same shape. A hooked syscall wrapper that
takes measurably longer than the real one is timing. A hook that changes an error
path — a call that used to fail and now succeeds, or the reverse — is behaviour,
not a file. So is anything that alters ordering: work you do on the main thread
before `Application.onCreate` runs is work the app can notice happened.

## The mapping, in one place

| Decision, and where you made it | Trace it produces |
|---|---|
| Ship a `.so` per ABI (Ch. 6) | Mapped region with path, dev, inode in `/proc/self/maps` |
| `c++_static` (Ch. 4, 6) | ~232KB of it, not a few pages |
| `-fvisibility=hidden` (Ch. 6) | One exported symbol instead of your whole API |
| Log with a tag (all labs) | Legible strings in your library's rodata |
| Stay resident everywhere (Ch. 11) | Observable in every process, not one |
| `getModuleDir()` + `close` (Ch. 11, mod 03) | A module-dir fd, for a window rather than for ever |
| Commit a PLT hook (Ch. 14, mod 04) | A GOT slot pointing outside its defining library |
| `DLCLOSE_MODULE_LIBRARY` after hooking (Ch. 10) | The same slot, now pointing at nothing |
| Replace a JNI method (Ch. 15, mod 05) | A native method whose pointer is in no loaded library |
| Borrow the main thread (Ch. 16, mod 05) | No `/proc/self/task` entry — the trace you avoided |
| `FORCE_DENYLIST_UNMOUNT` (Ch. 10) | A namespace missing entries — cleaner, not necessarily ordinary |
| Companion socket (Ch. 17, mod 06) | A connected socket fd during `preAppSpecialize` |
| Two-writer status (Ch. 19) | A file in the target app's own cache directory |
| Arming file over a constant (Ch. 11) | Your target package, in plain text, on disk |

Read the table as a design tool rather than a scoreboard. Each row is a place
where a different decision would have produced a different shadow, and several of
them are genuine trade-offs with no strictly better side: static linking against
size, resident against capable, a config file against a rebuild, an fd held
against an fd reopened.

## What you do not know yet

Every prediction above is derived from mechanism. None is a measurement, and the
gap between the two is where this part earns its place.

Three things in particular you cannot settle from reading. Whether your provider's
unmount routine leaves a mountinfo that is structurally ordinary or merely
shorter. Whether the descriptors and sockets you believe are transient are in fact
closed before anything in the app's own code could enumerate them. And whether the
launch-time delta your module imposes is large enough to be distinguishable from
noise on your hardware, at your sample size.

[How an app looks for you](/ZygiskLab/book/footprint/22-how-an-app-looks-for-you/)
takes each surface in this chapter and shows the app-side code that reads it.
[A detection harness](/ZygiskLab/book/footprint/24-detection-harness/) builds the
app that runs those checks against Labs 1 through 6 and turns this inventory into
results you can put a date and a device on. Until then, treat the table above as
what it is: a set of claims about your own code, made by the person who wrote it,
and not yet verified by anyone.

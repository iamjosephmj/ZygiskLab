---
title: "Glossary"
description: "Terms used throughout the book: zygote, specialization, denylist, PLT, mount namespace, companion, attestation."
sidebar:
  order: 4
status: unverified
---

Definitions as this book uses them, with a link to the chapter that introduces
each term properly. Several entries exist because two terms are routinely
confused with each other; those entries say so, because the confusion is the
reason they are here. Where a meaning is set by the provider rather than by the
API, the entry says that too — a definition that sounds fixed and is not is
worse than no definition.

## ABI

The instruction set and calling convention a native library is built for:
`arm64-v8a` and `armeabi-v7a` on this book's rig. The loader picks the build
matching the process, so a 32-bit process gets your 32-bit `.so` and a 64-bit
process gets the other one — two different libraries, two different lives, and a
result observed in one says nothing about the other.
[How the loader finds you](/ZygiskLab/book/load/06-how-the-loader-finds-you/)

## `app_data_dir`

The field of `AppSpecializeArgs` holding the app's data directory path. It is
the field that actually names the **package**, identically on every device,
which is why it is the right thing to identify an app by — and why `nice_name`
is not. It is a `jstring` that can be null.
[Reading `AppSpecializeArgs`](/ZygiskLab/book/prespecialize/09-reading-appspecializeargs/)

## `AppSpecializeArgs`

The struct the provider hands your module around app specialization. In
`preAppSpecialize` it is a mutable `AppSpecializeArgs *` whose required fields
are C++ *references* — so a typo turns a read into a write. In
`postAppSpecialize` it is `const`: the same struct, now a record of what
happened rather than a request.
[Reading `AppSpecializeArgs`](/ZygiskLab/book/prespecialize/09-reading-appspecializeargs/)

## Arming

Deciding, in `preAppSpecialize`, whether this process is the one your module
cares about, and doing nothing at all if it is not. The book treats arming as a
design obligation rather than an optimisation: an unarmed module is still loaded
into every process, so the unarmed path is a cost every launch on the device
pays. Arming configuration is read at process start, which means a change takes
effect at the target app's *next* launch.
[Choosing not to run](/ZygiskLab/book/prespecialize/11-choosing-not-to-run/)

## ART (Android Runtime)

The runtime that executes an app's Java and Kotlin code. Since Project Mainline
it is updatable independently of the platform release, so "Android 14" is not a
single ART build for any purpose that touches its internals.
[Hooking Java through ART](/ZygiskLab/book/postspecialize/15-hooking-java-through-art/)

## `ArtMethod`

ART's own internal record of a Java method, including the entry point that
decides where a dispatch lands. It is not a Java object and not an API: no NDK
header, no stability guarantee, layout that varies by Android version and
possibly by vendor build. Every ART hook is a bet on that layout.
[Hooking Java through ART](/ZygiskLab/book/postspecialize/15-hooking-java-through-art/)

## Attestation

A statement about the device or a key in it, produced by a component the
operating system does not control, signed with hardware-held keys, and evaluated
by a server that is not on the device. Attestation is structurally unlike every
other check in Part VI, which is the device examining itself. Do not confuse it
with **hiding**: local tidying is not an input to it at any point.
[How an app looks for you](/ZygiskLab/book/footprint/22-how-an-app-looks-for-you/)

## Companion (root companion)

A **separate** process, running as root, into which the provider's root-side
daemon loads the same shared library you shipped and calls a different entry
point. It is not your module escalating and not privilege kept across the
boundary — the companion was never in the app's lineage at all. Your injected
code can only talk to it, over one socket.
[The companion process](/ZygiskLab/book/companion/17-the-companion-process/)

## Denylist

A provider configuration list of packages or processes the provider will treat
differently — removing its own and its modules' mounts from that process's
namespace, and under Magisk with Zygisk, keeping modules out of it. **It is not
a hiding mechanism**, and Magisk's own FAQ says the project no longer handles
root hiding. Being on the list is a statement about the provider's behaviour,
not about yours. The exact semantics differ between Magisk and Zygisk Next.
[Existing answers, surveyed](/ZygiskLab/book/footprint/23-existing-answers-surveyed/)

## Deoptimisation

Persuading ART to abandon an optimised compiled form so a real dispatch to a
hooked method happens again. Without it, an inlined callee has no dispatch to
intercept, and a correctly installed hook simply never fires.
[Hooking Java through ART](/ZygiskLab/book/postspecialize/15-hooking-java-through-art/)

## `DLCLOSE_MODULE_LIBRARY`

The `setOption` value that asks for your module's library to be `dlclose`-ed
after the post-specialize call. It removes a mapping and leaves behind anything
that outlived the library — including hooks pointing at addresses that are no
longer backed by a loaded library, which is a worse trace than the one you
removed, not a better one.
[`setOption` and the flags](/ZygiskLab/book/prespecialize/10-setoption-and-flags/)

## Footprint

Everything about a process that is observable because your module is in it:
mappings, descriptors, threads, hooked slots, sockets, files, strings. The
book's framing is that a footprint is not an accident but the sum of design
decisions, each of which was probably right.
[Your footprint](/ZygiskLab/book/footprint/21-your-footprint/)

## `FORCE_DENYLIST_UNMOUNT`

The `setOption` value asking for provider and module mounts to be unmounted from
this process's namespace regardless of denylist enforcement. It is legal only in
`preAppSpecialize`, it records an intent rather than acting, and `setOption`
returns `void` — so "it worked" and "the provider does not implement it" are
byte-for-byte identical from inside your module. It touches mounts and nothing
else; your `.so` is *mapped*, not mounted.
[`setOption` and the flags](/ZygiskLab/book/prespecialize/10-setoption-and-flags/)

## GOT (Global Offset Table)

The table of data slots, one per imported symbol, holding the address the PLT
stub jumps to. A PLT hook writes a GOT slot. See also **PLT**.
[Hooking native symbols](/ZygiskLab/book/postspecialize/14-hooking-native-symbols/)

## Hidden API / non-SDK interface

Framework members apps are restricted from using since Android 9. The
restriction covers JNI, not just reflection: `GetFieldID` and `GetMethodID` on a
restricted member return `NULL` and throw, which from your side is
indistinguishable from the member not existing.
[JNI inside a live app](/ZygiskLab/book/postspecialize/13-jni-inside-a-live-app/)

## Injected process

The app process your module was loaded into — app uid, app SELinux domain, app
mount namespace, app sandbox. Distinguish it carefully from the **companion
process**, which runs the same library as root in a different lineage. Almost
every Part V mistake is an assumption that something true on one side is true on
the other.
[The asymmetry of privilege](/ZygiskLab/book/companion/19-asymmetry-of-privilege/)

## Inline hook

Patching the target's compiled instructions so execution diverts before the body
runs, as opposed to redirecting a dispatch record. More reaching and more
invasive: cache coherence, memory protection, and relocation of
position-dependent instructions all become yours to get right.
[Hooking Java through ART](/ZygiskLab/book/postspecialize/15-hooking-java-through-art/)

## Isolated process

A process the framework runs with no app identity of its own, for untrusted
work. It reaches your module through the same specialize callbacks and its
identity fields do not describe an app in the way you expect, so identification
logic written for ordinary app processes misreads it.
[The specialization window](/ZygiskLab/book/prespecialize/08-specialization-window/)

## `JavaVM` / `JNIEnv`

`JNIEnv *` is per-thread state and must never be shared between threads. The
process-wide handle is `JavaVM *`, from which any thread can obtain a correct
env — `GetEnv` for a thread the runtime already knows, `AttachCurrentThread` for
one you created, which must then detach before it exits.
[JNI inside a live app](/ZygiskLab/book/postspecialize/13-jni-inside-a-live-app/)

## Local reference / global reference

A JNI local reference is freed when the native method returns to Java — which
never happens on a worker thread that attaches once and loops, so locals
accumulate with nothing to pop them. Anything you keep past the current call
must be promoted with `NewGlobalRef` and released with `DeleteGlobalRef`. The
two are not interchangeable and confusing them produces either a leak or a
use-after-free.
[JNI inside a live app](/ZygiskLab/book/postspecialize/13-jni-inside-a-live-app/)

## Linker namespace

The dynamic linker's scoping of which libraries a given loaded object may
resolve against. An app's own JNI library sits in a namespace with the APK's
`lib/<abi>/` on its search path, linked to the system namespace through a filter
that admits the public NDK libraries only. Your module is *not* loaded that way,
which changes what you may link against.
[How the loader finds you](/ZygiskLab/book/load/06-how-the-loader-finds-you/)

## Module directory

`/data/adb/modules/<id>/` — **your** module's directory, reachable from inside
the process as a descriptor from `getModuleDir()`. Do not confuse it with the
**provider's** own directories under `/data/adb`, which belong to Magisk or
Zygisk Next and which your injected side generally cannot read at all; that is
the companion's job. The descriptor is itself a footprint if you keep it.
[Anatomy of a module](/ZygiskLab/book/load/05-anatomy-of-a-module/)

## `module.prop`

The metadata file at the root of a module, whose `id=` line is the `<id>` in
every path and command in this book. Renaming it renames the module's directory
and orphans anything that referred to the old name.
[Anatomy of a module](/ZygiskLab/book/load/05-anatomy-of-a-module/)

## Mount namespace

The per-process view of the filesystem. Provider and module mounts exist in some
namespaces and not others, which is why `/proc/<pid>/mountinfo` differs between
two processes on the same device and why a path that resolves for one process
does not resolve for another.
[How an app looks for you](/ZygiskLab/book/footprint/22-how-an-app-looks-for-you/)

## mount-master (`su -M`)

The flag asking for a root shell in the **global** mount namespace — the
unmodified view, where `/data/adb/modules` is the real directory holding real
files rather than something covered by the provider's own mounts. Every command
in this book that modifies module storage runs under it.
[Deploying safely](/ZygiskLab/book/load/07-deploying-safely/)

## `nice_name`

The field of `AppSpecializeArgs` holding the **process** name — not the package
name. They coincide only for a component with no `android:process` attribute,
which is exactly why matching `nice_name` against a package appears to work and
is subtly wrong: `:remote` processes are `com.example.app:remote`, and a
globally named process need not contain the package name at all. Identify the
app by `app_data_dir`; select the process by `nice_name`.
[Reading `AppSpecializeArgs`](/ZygiskLab/book/prespecialize/09-reading-appspecializeargs/)

## PLT (Procedure Linkage Table)

The per-ELF table of stubs used to call imported symbols, paired with the
**GOT**. A PLT hook redirects one importer's view of one symbol, which is why it
is reliable — the ELF ABI specifies it — and why it is narrower than people
expect: it cannot see calls that never go through the table.
[Hooking native symbols](/ZygiskLab/book/postspecialize/14-hooking-native-symbols/)

## `postAppSpecialize`

The callback after specialization. You are now the app: app uid, app SELinux
domain, app sandbox, a live runtime, and no route back to the privileges you had
a moment earlier.
[What changed at the boundary](/ZygiskLab/book/postspecialize/12-what-changed-at-boundary/)

## `preAppSpecialize`

The callback before specialization. You still hold zygote's identity, which is
the only window in which `connectCompanion()` and `setOption` are legal.
Everything that must be decided with privilege is decided here.
[The specialization window](/ZygiskLab/book/prespecialize/08-specialization-window/)

## Provider

The implementation supplying the Zygisk API on a given device — Magisk's
built-in Zygisk, or Zygisk Next as a standalone module, commonly on KernelSU. A
correctly written module builds once for both, but behaviour beyond the header —
what the denylist means, which processes get modules, what an unmount covers —
is the provider's decision and is version-dependent. Record provider identity
and version with every result.
[What Zygisk is](/ZygiskLab/book/foundations/01-what-zygisk-is/)

## SELinux domain / context

The label the kernel enforces policy against. It is what makes
`connectCompanion()` legal only before specialization: an app-domain process is
not permitted to connect to a root daemon's socket, and that restriction lives
outside your code.
[The companion process](/ZygiskLab/book/companion/17-the-companion-process/)

## Specialization

The step in which a freshly forked zygote child is given an app's identity and
confined to its sandbox — uid and gid, SELinux domain, mount view, resource
limits. This book is organised around it, because almost every difficulty in
Zygisk is a question of which side of specialization your code is on.
[The specialization window](/ZygiskLab/book/prespecialize/08-specialization-window/)

## `system_server`

The privileged framework process, also forked from zygote, reaching your module
through its own pre/post pair of callbacks. `onLoad` cannot tell it apart from
an app process, so anything you put in `onLoad` runs there too.
[What Zygisk is](/ZygiskLab/book/foundations/01-what-zygisk-is/)

## Tracer / `TracerPid`

The field in `/proc/self/status` naming the process tracing this one, or `0`.
One file read is the whole of the standard tracer check — and it will not see a
Zygisk module, which is loaded by the provider and runs as part of the process
rather than attaching as a debugger. The check exists because attach-based tools
do show up.
[How an app looks for you](/ZygiskLab/book/footprint/22-how-an-app-looks-for-you/)

## Zygisk

The mechanism by which a provider arranges for module code to run in each
zygote-forked child around specialization. Modules are loaded *after* the fork,
so your code always runs in the child, never in zygote itself.
[What Zygisk is](/ZygiskLab/book/foundations/01-what-zygisk-is/)

## Zygote

The Android daemon every app process is forked from, holding a warm runtime and
preloaded framework classes. Everything your module can do, and everything it
must be careful about, follows from the fact that apps are forked rather than
started fresh.
[What Zygisk is](/ZygiskLab/book/foundations/01-what-zygisk-is/)

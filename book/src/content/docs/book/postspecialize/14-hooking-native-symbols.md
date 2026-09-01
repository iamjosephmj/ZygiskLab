---
title: "Hooking native symbols"
description: "Lab 4: PLT/GOT hooking with pltHookRegister and pltHookCommit, what a PLT hook cannot reach, and debugging a silent hook."
sidebar:
  order: 3
status: proven
---

Everything so far has been about position: getting loaded, arming for the right
process, knowing what the process is on each side of specialization. This is the
first chapter where you change what the app does. The technique is PLT hooking,
and it is the most reliable interception primitive Zygisk gives you, because it
targets something the ELF ABI specifies rather than something a runtime happens
to do this year.

It is also narrower than most people assume the first time they use it. A PLT
hook sees one specific kind of call and is blind to several others, and the
failure mode for that blindness is an empty log rather than an error. Half this
chapter is the mechanism; the other half is the boundary of what it can reach and
how to tell "not hooked" from "not called".

The earlier edition of this chapter got its own worked example wrong, in a way
that the "what a PLT hook cannot reach" section below already predicted. That is
recorded here rather than quietly fixed, because the mistake teaches the
chapter's central point better than the correct answer does. It is
[described in full](#the-mistake-this-chapter-made) once you have the mechanism
to read it with.

## The table in the middle of the call

When a shared library calls a function it does not itself define — `libfoo.so`
calling `open`, which lives in `libc.so` — the compiler cannot emit a direct
branch. The address is not known at compile time, and it is not known at link
time either: it depends on where the dynamic linker maps `libc.so` in this
particular process. So the call is made indirectly, through a table that the
linker fills in.

Each importing ELF carries its own copy of that table. There are two pieces: the
**PLT** (Procedure Linkage Table), a small stub per imported symbol, and the
**GOT** (Global Offset Table), a slot per symbol holding the resolved address the
stub jumps to. The stub is code; the slot is data the linker writes.

```text
  BEFORE
                     libfoo.so                       libc.so
  ┌────────────┐    ┌──────────────┐   ┌──────────┐  ┌───────────────┐
  │ call site  │───▶│ PLT stub for │──▶│ GOT slot │─▶│ open()        │
  │ open(..)   │    │ open         │   │ 0x7f…a10 │  │ real function │
  └────────────┘    └──────────────┘   └──────────┘  └───────────────┘
                                            ▲
                                   dynamic linker wrote
                                   this address at load

  AFTER pltHookCommit()
                     libfoo.so                       your module
  ┌────────────┐    ┌──────────────┐   ┌──────────┐  ┌───────────────┐
  │ call site  │───▶│ PLT stub for │──▶│ GOT slot │─▶│ my_open()     │
  │ open(..)   │    │ open         │   │ 0x7d…c40 │  │               │
  └────────────┘    └──────────────┘   └──────────┘  └──────┬────────┘
                                                            │ tail-call
                                                            ▼
                                                     ┌───────────────┐
                                                     │ open()        │
                                                     │ real function │
                                                     └───────────────┘
```

That is the entire idea. You do not patch `libc.so`. You do not rewrite the first
instructions of `open`. You change one pointer in the caller's table, so that
the indirection which already existed lands somewhere else. The old value is
handed back to you, and you call through it.

Three consequences fall straight out of the diagram, and they explain most of
this chapter:

- The hook is **per importing ELF**, not per function. Patching `libfoo.so`'s
  table does nothing to `libbar.so`'s.
- The hook only affects calls that **go through the table**. A call that resolved
  some other way is untouched.
- Nothing is being executed while you write — you modify data, not instructions —
  which is why this is comparatively safe to do in a live process.

## `pltHookRegister` and `pltHookCommit`

The API is two calls. From `zygisk.hpp`:

```cpp
void pltHookRegister(dev_t dev, ino_t inode, const char *symbol,
                     void *newFunc, void **oldFunc);
bool pltHookCommit();
```

`pltHookRegister` records an intention: for every ELF mapped in this process
whose file identity matches `(dev, inode)`, replace `symbol` with `newFunc`, and
if `oldFunc` is non-null, store the original there. The header describes it in
exactly those terms and nothing more — it records, it does not act.

`pltHookCommit` performs every registration made so far and returns `false` if an
error occurred. That is the whole documented contract.

The split exists for two reasons. The first is batching: registering ten symbols
and committing once is one pass over the mapped ELFs rather than ten. The second
matters more. Writing a GOT slot means finding the page it lives on, making that
page writable, storing, and restoring the protection — and doing that in a
process that has other threads running. That work is delicate, and the API is
shaped so it happens **once**, at a moment you choose, for everything at once,
rather than being smeared across however many registration calls you make. The
`bool` on commit is the single place where the whole batch reports success or
failure.

Note what the identity argument is. Not a path — a `(dev, inode)` pair. The header
is explicit that this pair is what uniquely identifies a mapped file, and the
reason is that the same library can be visible at several paths on Android
(bind mounts, APEX). The path in `/proc/self/maps` is a label; the device and
inode are the identity. Lab 4's module parses the maps once to get them:

```cpp
if (sscanf(line, "%*x-%*x %*4s %*x %lx:%lx %llu %n",
           &devMajor, &devMinor, &inode, &pathOffset) != 3) {
    continue;
}
```

and matches the mapping whose path ends in `/libandroid_runtime.so`, then builds
the pair with
`makedev()`. This is the only place the kernel publishes that pairing, so parsing
`/proc/self/maps` is not a hack here — it is the intended route to the argument
the API asks for.

### Where in the lifecycle

The Lab 4 module does `findTargetLib()`, `pltHookRegister()` and
`pltHookCommit()` all inside `preAppSpecialize`, and nowhere else.

:::note[Measured on the rig, but still not a documented rule]
The header states no lifecycle restriction on `pltHookRegister` or
`pltHookCommit`. Contrast `exemptFd`, where the comment says plainly that the API
"only make sense in preAppSpecialize", and `getModuleDir`, which carries a similar
restriction. For the PLT hooking pair there is no such sentence.

Lab 4 settled the practical question. On the reference rig — Pixel 6 Pro,
Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5 — a correct
registration committed in `preAppSpecialize` returns `true` and the hook fires.
Committing from `postAppSpecialize` was also tried, and it is not the thing that
makes a bad registration succeed: with the wrong target, the commit returned
`false` from both callbacks. So `preAppSpecialize` works, and the reasoning below
stands. What remains undocumented is the *rule*: the header still does not say
this is required or guaranteed, and one rig is not a specification.
:::

The reasoning behind the choice is worth having, because you will make the same
call for other libraries:

- **`libc.so` is already mapped.** This process was forked from zygote seconds
  ago, and zygote linked against libc long before your module existed. There is
  nothing to wait for.
- **`/proc/self/maps` is readable.** Before specialization you still have zygote's
  privilege and zygote's mount namespace — see
  [Chapter 12](/ZygiskLab/book/postspecialize/12-what-changed-at-boundary/) for
  what changes on the far side.
- **Coverage of app startup.** A hook committed before specialization is live for
  the *entire* rest of startup, including the app's `attachBaseContext` and
  `Application.onCreate`. Committing in `postAppSpecialize` would miss whatever
  happened in between, and on Android a great deal happens in between.

## Choosing what to hook

Two questions, in this order: which library **imports** the symbol, and is that
import actually reached at runtime.

The library question is the one people get wrong, because they think of a hook as
attaching to a function. It does not. It attaches to a *caller's* table. The
library that **defines** a symbol is exactly the wrong place to look: a defining
library has no import to rewrite. You are looking for an ELF with an `UND`
reference and a relocation for that symbol — a caller.

Answer it before you write any code, with `llvm-readelf` against the library
itself, pulled from the device:

```bash
adb pull /system/lib64/libandroid_runtime.so .
llvm-readelf -r libandroid_runtime.so | grep ' open$'
```

A relocation naming the symbol means there is a slot to rewrite. No line means
there is nothing to hook in that ELF, and no amount of debugging the hook will
change that. `llvm-readelf --dyn-syms <lib> | grep <symbol>` answers the same
question from the symbol table side; an `UND` entry is the import, a defined
entry is the definition and is not a hook target.

### The mistake this chapter made

The earlier edition of this chapter, and the earlier Lab 4 module, hooked
`openat` and registered it against **`libc.so`**. The argument was coverage:
bionic implements `open()` as a call to `openat()` with `AT_FDCWD`, and nearly
every file-opening API on Android bottoms out at `openat`, so hooking that one
symbol was supposed to observe file opens app-wide.

It cannot work, and it fails twice over.

**`libc.so` is the defining library.** libc *defines* `openat`; it does not
import it. There is no PLT entry in libc for a symbol libc itself provides, so
the registration matched nothing. On the rig, every armed process logged:

```text
preAppSpecialize: ARMED, but pltHookCommit() FAILED - openat() is NOT hooked in this process
```

**The `open()` → `openat()` step is internal to libc.** That is the deeper
error, and this chapter had already stated the rule that forbids it. "What a PLT
hook cannot reach", below, says that a call from a library into its own function
never crosses a PLT. Bionic's `open()` calling `openat()` is precisely that call.
Even hooking `openat` in some *other* ELF would not have caught the traffic the
argument promised, because that traffic never leaves libc.

Nor is `openat` rescued by picking a different ELF. Measured with
`llvm-readelf -r` on libraries pulled from the rig: `libandroid_runtime.so`,
`libutils.so` and `libbase.so` carry **zero** relocations for `openat`. Only
`libc++.so` has one; hooking that committed successfully and intercepted nothing
during an ordinary app launch, because the path is not exercised there.

The lifecycle was never the problem either. Committing in `postAppSpecialize`
was tried with a real registration and also returned `false`. What distinguished
"the API is broken" from "nothing matched" was committing an *empty*
registration list, which returned `true`.

### What the module hooks now

`open`, registered against **`libandroid_runtime.so`**. Three properties make
that the right target:

- **It imports the symbol.** One relocation for `open`, confirmed with
  `llvm-readelf -r`. (`libbase.so` has one too, if you want a second importer.)
- **Every app process loads it.** It is part of the framework every zygote-forked
  app maps, so it is present at `preAppSpecialize` with nothing to wait for.
- **It exercises the symbol during startup.** It opens the app's APK and its
  splits, so the hook has real traffic to intercept on a cold launch rather than
  a slot that is technically correct andsilent.

The trade is honest: this is *not* app-wide file-open coverage. It is
`libandroid_runtime.so`'s calls to `open`, and nothing else. The earlier
edition's claim of app-wide coverage was not achievable by a PLT hook at all —
not with a better target, not with better timing. If you want breadth, you patch
more importers; you do not find one magic symbol.

**Cost and safety.** `open` is a thin syscall wrapper. The replacement does a
bounded amount of work and then tail-calls the original, so the overhead per call
is small and predictable. Compare a hook on something that does real work, where
your instrumentation competes with the function itself.

## Writing a trampoline that is safe on the caller's thread

Your replacement runs on whatever thread made the call, at whatever point in that
thread's work the call happened, possibly on many threads at once. Three
disciplines follow.

**Call through, unchanged.** The last line of the module's replacement is the
whole ethic of an observing hook:

```cpp
    return orig_open(path, flags, mode);
```

Same arguments, result returned unmodified. An observer that quietly alters an
`errno` or drops a flag turns every later measurement into fiction, and the bug
will present as an app failure with no visible connection to your module.

**Handle re-entrancy.** Your logging path must not call the function you hooked.
If it does, one intercepted call becomes unbounded recursion and the process dies
in a way that looks nothing like a logging bug. On current Android
`__android_log_print` reaches `logd` over a socket rather than through `open`,
so in practice this particular pair does not recurse — but that is an
implementation detail of today's logging backend, not a guarantee. The module
makes the failure mode structurally impossible instead of relying on it:

```cpp
static thread_local int reentryDepth = 0;
```

incremented around the log calls and checked before logging anything. Thread-local
is the correct scope: two threads in the hook simultaneously are not re-entrancy,
they are normal, and a global flag would silently drop one of them.

**Assume concurrency.** Anything the replacement touches is shared. The module's
call counter is a `std::atomic<int>`, and the process name it prints is a plain
`char` array written once in `preAppSpecialize` — before `pltHookCommit()`, so
before the hook can possibly fire — rather than a pointer into a JNI string whose
lifetime and `JNIEnv` the hook would then have to reason about. That ordering is
the point: everything the hook body reads is finalised before the hook goes live.

**Bound your output.** An app can call `open` hundreds of times a second during
startup. The module caps logging at 20 calls per process and prints one
`further calls suppressed` line at the cap, so the cutoff is visible rather than
looking like the hook stopped working. Calls past the cap still run and still call
through; only the log line is skipped.

:::caution
Unbounded logging from a hot hook is not just noisy. A fast enough logcat writer
can visibly stall the process you injected into, and you will then be debugging a
performance problem you created. Cap first, raise the cap later if you need to.
:::

## Check the commit result

`pltHookCommit()` returns `bool`. Ignoring it is the single most confusing way
this technique fails: the module loads, arms, logs that it armed, and then nothing
about the hooked symbol ever appears — because the commit failed and your `orig`
pointer is still null. The module logs both outcomes explicitly:

```cpp
        bool committed = api->pltHookCommit();
        if (committed) {
            LOGI("preAppSpecialize: pid=%d proc=%s ARMED, open() hook committed",
                 getpid(), armedProcessName);
        } else {
            LOGW("preAppSpecialize: pid=%d proc=%s ARMED, but pltHookCommit() "
                 "FAILED - open() is NOT hooked in this process", getpid(),
                 armedProcessName);
        }
```

Do this in every module you write. It converts a silent mystery into one line.
Rewriting Lab 4 turned entirely on that one line: without it the wrong target
would have presented as an app that simply never opens files.

### What `false` actually means

The header says only that `pltHookCommit` returns `false` "if an error occurred".
Measured on the rig, the signal is more specific and more useful than that:

| Registration list | `pltHookCommit()` |
| --- | --- |
| Empty | `true` |
| Symbol not imported by the targeted ELF | `false` |
| Symbol imported and patched | `true` |

So `false` does not mean "the API is broken". It means **nothing you asked for
could be applied** — the targeted ELF had no PLT entry for that symbol. That is a
genuine diagnostic, and it points straight at the target rather than at the
mechanism. It is also undocumented: the header describes none of this, and it is
observed on one rig with one provider version, so treat it as a strong hint
rather than a contract.

## What a PLT hook cannot reach

This section matters as much as the mechanism. A reader who believes a PLT hook
sees everything will read an empty log as "this app never opens files", which is
false and expensively so.

**Internal calls.** When `libc.so` calls its own `openat` from inside another
libc function, that call does not go through a PLT — the symbol is local, the
linker resolved it at build time or via a direct branch. You are hooking the
*importer's* table, so calls that never leave the defining library are invisible.
This is not a hypothetical: bionic's `open()` → `openat()` is exactly this call,
and assuming otherwise is what broke the earlier edition of this chapter.

**Inlined code.** If a function was inlined into its caller, or the compiler
replaced a call with the syscall instruction directly, there is no table entry at
all. Static linkage has the same effect: a library that statically links its own
copy of a function calls that copy, not yours.

**Direct syscalls.** Code that issues `svc #0` itself — some anti-tamper layers do
exactly this, and so do some runtimes — bypasses libc entirely. A PLT hook on
`open` cannot see a raw syscall, by construction.

**Calls before your commit.** Anything that ran before `pltHookCommit()` returned
went through the original pointer. This is why the module commits as early as it
can.

**Symbols resolved by another mechanism.** `dlsym` results captured before your
commit, function pointers copied into a struct at init time, C++ vtables, and
`ifunc`-resolved symbols do not re-read the GOT slot on each call. Patching it
afterwards changes nothing for them.

The honest summary: a PLT hook shows you cross-library calls made through dynamic
linkage after the moment you committed. That is a large and useful set on
Android. It is not "everything".

## The deferred-hook problem

Your target library may not be loaded yet. You want to hook `libtarget.so`; at
`preAppSpecialize` it is not in `/proc/self/maps`, because the app will
`System.loadLibrary` it later. Registering against a `(dev, inode)` pair you do
not have is not possible, and committing against a library that is not mapped
patches nothing.

Three approaches, with their real costs:

**Hook the loader.** PLT-hook something on the library-loading path — `dlopen`,
`android_dlopen_ext` — and in your replacement, after calling through, check
whether the library you wanted has now appeared; if so, register and commit a
second batch. This is precise and it is the standard answer, but you are now
running registration work inside a hook on the caller's thread, with all the
re-entrancy discipline above applying twice over, and you must handle the library
being loaded more than once.

**Poll.** Spawn a thread that re-reads `/proc/self/maps` periodically until the
library appears, then hooks. Simple to write and easy to reason about, but it is
a race you can lose: calls made between the load and your next poll are gone, and
a thread you started in a live app is itself a footprint —
[Chapter 21](/ZygiskLab/book/footprint/21-your-footprint/) has the argument.

**Hook later, deliberately.** If the calls you care about happen well after
startup, defer everything to a point where the library is reliably present. You
trade coverage of the early window for simplicity. Sometimes that trade is
correct; make it knowingly rather than by accident.

There is no clean answer here. Every option is a compromise between coverage,
complexity, and how much you are willing to run on someone else's thread. Lab 4
sidesteps it entirely by targeting `libandroid_runtime.so`, which practically
every app process has mapped before your module runs — 125 of the 126 app-uid
processes running on the reference rig had it mapped when this was checked —
which is another reason it is the right first hook. Note the "practically":
one process in that sample did not have it, so if you are hooking a specific
process, confirm rather than assume.

## Debugging a hook that never fires

Work down this list in order. Each step distinguishes two hypotheses; do not skip
to the interesting ones.

**1. Is this the process you armed for?** Check `logcat` for the arming line
naming the pid and process. If the target never armed, nothing else matters. A
mistyped `target.txt`, a package renamed, an app running in a `:remote` process
whose `nice_name` is not the package name — all present as total silence.

**2. Did the module load at all?** No `preAppSpecialize` line means this is a
deployment or loader problem, not a hooking problem. Go back to
[Chapter 7](/ZygiskLab/book/load/07-deploying-safely/): `.so` copied in place
over a mapped file, no reboot after deploy, wrong ABI file name.

**3. Did `pltHookCommit()` return true?** You logged it, so read it. `false`
means the hook was never installed and every question below is moot — and, per
the table above, it means specifically that the ELF you targeted has no PLT entry
for that symbol. Go to step 4. Check that `findTargetLib()` succeeded first — the
module logs that separately.

**4. Does the target ELF actually import the symbol?** This is the step Lab 4
was rewritten around. Pull the library off the device and ask:

```bash
llvm-readelf -r libandroid_runtime.so | grep ' open$'
```

No relocation means there is no slot and there never was one. The most common
version of this error is registering against the library that *defines* the
symbol — `libc.so` for anything in libc — which can never work. Find an importer
instead, or accept that this symbol is not PLT-hookable in this process.

**5. Is `orig_open` non-null?** A null original after a successful commit means
the commit had nothing to do. If the call you care about is internal to the
defining library, statically linked, inlined, or a raw syscall, there is no slot.
This is the step where an empty log turns out to be correct behaviour.

**6. Is the call happening before your commit?** Instrument the other direction:
log a timestamp at commit and compare with when the app does the work you expected
to intercept. If the app opened its file during library init, you were late.

**7. Is something else hooking the same slot?** Another module, or the app's own
protection layer, may have written the slot after you. Disable other modules and
retest before drawing conclusions.

If you reach the end of that list with `committed=true`, a non-null original, a
confirmed relocation for the symbol in the ELF you targeted, and still no calls — believe the log. The most likely
answer at that point is that the code path you assumed exists does not run in this
app.

## What this bought you

You now have interception against a specified interface. The PLT is part of the
ELF ABI; the linker's resolution behaviour is documented and stable across
Android releases, and it does not vary by vendor. A hook written this way keeps
working, which is not something the next technique in this book can promise.
[Chapter 15](/ZygiskLab/book/postspecialize/15-hooking-java-through-art/) is about
what you do when the thing you want has no such contract, and it is worth reading
that chapter with this one's stability in mind — it is the baseline everything
there is measured against.

Take Lab 4's deliverable seriously. Hooking the symbol is the easy half. Proving,
with a control process, that the hook is scoped where you put it is the half that
makes the result mean anything.

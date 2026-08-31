---
title: "Anatomy of a module"
description: "The ModuleBase interface, the Api handle, REGISTER_ZYGISK_MODULE, and the timeline of one app launch."
sidebar:
  order: 1
status: unverified
---

A Zygisk module is one exported C function, one class with five virtual methods,
and a struct of function pointers you may call only while somebody else is on the
stack. That is the whole interface. Everything else in this book — argument
rewriting, PLT hooks, companion processes, ART surgery — is code you write on top
of it, and every one of those techniques fails in the same way when you get the
interface wrong: you call something at a moment it is not valid, and it returns a
useless value instead of an error you can see.

[Chapter 4](/ZygiskLab/book/foundations/04-hello-zygisk/) walked one working
module line by line. This chapter is the other half: the surface as a whole, the
rules about when each part of it is legal, and what state survives what. It is a
reference, and it is meant to be returned to. The authority throughout is the
vendored `zygisk.hpp` at `modules/01-hello-zygisk/jni/zygisk.hpp`, targeting
`ZYGISK_API_VERSION 5`. Where behaviour depends on the provider rather than the
header, this chapter says so and points at
[Where it breaks](/ZygiskLab/book/companion/20-where-it-breaks/).

## `zygisk::ModuleBase`: five callbacks

The header declares exactly five `virtual` methods, all with empty inline
bodies. None is pure virtual, so a module that overrides nothing compiles, loads,
and does nothing at all — which is a useful sanity baseline and a common
accidental outcome.

```cpp
virtual void onLoad(Api *api, JNIEnv *env) {}
virtual void preAppSpecialize(AppSpecializeArgs *args) {}
virtual void postAppSpecialize(const AppSpecializeArgs *args) {}
virtual void preServerSpecialize(ServerSpecializeArgs *args) {}
virtual void postServerSpecialize(const ServerSpecializeArgs *args) {}
```

Note the shape before the detail. The `pre` methods take a non-`const` pointer;
the `post` methods take a `const` one. That single difference in the type system
encodes the entire architecture: before specialization the arguments are a
request you may edit, after specialization they are a record of what happened.

:::note
[Chapter 1](/ZygiskLab/book/foundations/01-what-zygisk-is/) described "four
methods" and Chapter 4's module overrides three. Both are talking about the
callbacks that fire around a boundary. `ModuleBase` has five members: the four
specialization hooks plus `onLoad`, which is not a boundary event at all.
:::

### `onLoad(Api *api, JNIEnv *env)`

Fires as soon as the module is loaded into the target process, from
`entry_impl` immediately after the loader accepts your registration. It fires
once per process, and it is the only callback that receives the `Api *` and the
`JNIEnv *`.

At this moment the process has forked from zygote but no specialization has run,
and — this is the part people get wrong — you do not yet know what the process is
going to become. `onLoad` is called identically for an app process and for
`system_server`. Nothing in its signature tells you which. Any branch on process
identity has to wait for a specialize callback.

What is legal here: store the two pointers, initialise data structures, read
your own module state. What is not: assuming a package name, touching
`AppSpecializeArgs` (you do not have one), or doing anything expensive. This
code is on the critical path of every app launch on the device, and a module
that spends 20ms in `onLoad` has made every cold start 20ms slower for the user,
across every process it is scoped to.

There is no getter to recover `api` or `env` later. If you do not stash them
here, the rest of your module has nothing to call.

### `preAppSpecialize(AppSpecializeArgs *args)`

The privileged window. The header states it plainly: the process has just forked
from zygote, no app-specific specialization has been applied, "the process does
not have any sandbox restrictions and still runs with the same privilege of
zygote".

This is the only callback where the whole `Api` is usable. `connectCompanion()`,
`getModuleDir()` and `exemptFd()` are documented as pre-specialize-only, and
`exemptFd()` narrower still — `preAppSpecialize` specifically. `setOption()` with
`FORCE_DENYLIST_UNMOUNT` is documented as only making sense here. If your module
needs privilege, a socket to root, or a file descriptor that survives, this is
where it happens or it does not happen.

`args` is your view of the pending specialization. Every required field is a
*reference*, not a value:

```cpp
jint &uid;  jint &gid;  jintArray &gids;
jint &runtime_flags;  jobjectArray &rlimits;  jint &mount_external;
jstring &se_info;  jstring &nice_name;
jstring &instruction_set;  jstring &app_data_dir;
```

Writing to `args->uid` writes into the request zygote is about to act on. The
header says so outright — "You can read and overwrite these arguments to change
how the app process will be specialized." What that costs you is
[Chapter 9](/ZygiskLab/book/prespecialize/09-reading-appspecializeargs/)'s
subject; what it costs you *here* is that a stray write to a reference you meant
to read produces a process specialized wrongly, with no error anywhere.

The optional block is a separate hazard:

```cpp
jintArray *const fds_to_ignore;
jboolean *const is_child_zygote;
jboolean *const is_top_app;
jobjectArray *const pkg_data_info_list;
jobjectArray *const whitelisted_data_info_list;
jboolean *const mount_data_dirs;
jboolean *const mount_storage_dirs;
jboolean *const mount_sysprop_overrides;
```

These are pointers, and the header's comment is a one-liner you should treat as
a rule: "Please check whether the pointer is null before de-referencing." They
exist because the underlying framework call gained parameters over Android
versions, and a device whose zygote does not pass one gives you `nullptr`.
Dereferencing without the check is the most common way to crash a module on a
device other than the one you developed on, and a crash here is a crash in a
process the system is waiting on.

`is_child_zygote` deserves naming now. When it is non-null and true, the process
you are in is going to become another zygote — the WebView sandbox and other
isolated-process hosts work this way — not an app. Modules that assume "pre-app
means an app is coming" misbehave in exactly those processes.
[Chapter 11](/ZygiskLab/book/prespecialize/11-choosing-not-to-run/) is about
deciding not to run.

`AppSpecializeArgs` has a deleted default constructor. You cannot make one, copy
one meaningfully, or keep a useful one; treat the pointer as valid only for the
duration of the call.

### `postAppSpecialize(const AppSpecializeArgs *args)`

Same process, same pid, after the boundary. The header: "the process has all
sandbox restrictions enabled for this application... this method runs with the
same privilege of the app's own code."

You have the app's uid, the app's SELinux domain, the app's mount namespace. The
app's own code has still not run — its `Application` has not been constructed —
so this is the last moment that is yours alone, and it is where most hooking
belongs, because the libraries you want to hook are now mapped with the app's
view of the filesystem.

The `Api` is on its last legs here. The header's comment above `struct Api` is
the governing sentence: "All API methods will stop working after
`post[XXX]Specialize` as Zygisk will be unloaded from the specialized process
afterwards." Read that carefully — it says *after*, so calls during
`postAppSpecialize` are within the window, but the privileged subset
(`connectCompanion`, `getModuleDir`, `exemptFd`) is already gone by SELinux and
uid. What remains practically usable is `getFlags()`, `setOption()`,
`hookJniNativeMethods()` and the PLT pair.

The cost of `postAppSpecialize` is that it is the app's own time. Anything slow
here is launch latency the user attributes to the app.

### `preServerSpecialize` / `postServerSpecialize`

The same two moments for `system_server`, with a different argument struct:

```cpp
jint &uid;  jint &gid;  jintArray &gids;  jint &runtime_flags;
jlong &permitted_capabilities;
jlong &effective_capabilities;
```

Six fields, no optional block, no `nice_name` — there is nothing to name,
because there is only ever one `system_server` and it is forked once at boot,
not per launch.

The important fact is negative, and it is the one this section exists for: **a
module that does not check will run inside `system_server` too.** Zygisk calls
whichever callbacks the provider has decided apply to the process it is in. If
you only override the app pair, your module still loads into `system_server` —
`onLoad` fires there — it simply has no specialize behaviour there. If you
override the server pair without meaning to, or put your logic in `onLoad` where
it runs in both, you are now executing inside the process that hosts
`ActivityManagerService`, `PackageManagerService` and most of the platform.

Two consequences follow. First, privilege: `system_server` is not root but holds
capabilities and a domain no app has, which makes it a far more attractive place
to put code and a far more dangerous one to get wrong. Second, blast radius: a
native crash in an app process kills an app, and a native crash in
`system_server` takes the device into a reboot loop. That asymmetry is why
[Deploying without bricking zygote](/ZygiskLab/book/load/07-deploying-safely/)
comes before the interesting work.

:::caution
Whether your module is loaded into `system_server` at all, and whether a
scope or denylist setting affects that, is provider behaviour, not interface
behaviour. The header does not promise it either way. Do not infer your
provider's policy from the fact that a callback did or did not fire once — read
it in [Where it breaks](/ZygiskLab/book/companion/20-where-it-breaks/) and
verify on your rig.
:::

## `zygisk::Api`: the handle

`Api` is a thin struct holding one private pointer to an internal `api_table` of
function pointers. Every public method is an inline forwarder with the same
defensive shape:

```cpp
inline int Api::connectCompanion() {
    return tbl->connectCompanion ? tbl->connectCompanion(tbl->impl) : -1;
}
```

That null check is the single most important implementation detail in the
header. **A provider that does not implement a method leaves its slot null, and
the forwarder returns a failure value rather than crashing.** `connectCompanion`
and `getModuleDir` return `-1`, `getFlags` returns `0`, `exemptFd` and
`pltHookCommit` return `false`, and `setOption`, `hookJniNativeMethods` and
`pltHookRegister` return `void` — they simply do nothing, silently. So does
calling any of them at a moment the provider considers invalid. There is no
exception, no errno you can trust, no distinction in the return value between
"not implemented", "wrong callback" and "genuinely failed". Check every return
value, and treat a zero `getFlags()` as ambiguous rather than informative.

The lifetime is simple and absolute. The handle is a function-local `static`
inside `entry_impl`, so the pointer stays valid and non-null for the life of the
process. Its *usefulness* ends when Zygisk unloads itself after
`post[XXX]Specialize`. A stale `Api *` does not become null and does not fault —
it becomes a set of calls that quietly do nothing. That is a nastier failure
mode than a crash, and it is why a module that starts a thread and calls the API
from it later appears to work in development and does nothing in the field.

The eight operations, with the chapter that owns each:

| Method | Valid in | Owned by |
|---|---|---|
| `connectCompanion()` | `pre[XXX]Specialize` only | [Ch. 17](/ZygiskLab/book/companion/17-the-companion-process/) |
| `getModuleDir()` | `pre[XXX]Specialize` only | [Ch. 17](/ZygiskLab/book/companion/17-the-companion-process/) |
| `setOption(Option)` | see per-option notes | [Ch. 10](/ZygiskLab/book/prespecialize/10-setoption-and-flags/) |
| `getFlags()` | while loaded | [Ch. 10](/ZygiskLab/book/prespecialize/10-setoption-and-flags/) |
| `exemptFd(int)` | `preAppSpecialize` only | [Ch. 10](/ZygiskLab/book/prespecialize/10-setoption-and-flags/) |
| `hookJniNativeMethods(...)` | while loaded | [Ch. 14](/ZygiskLab/book/postspecialize/14-hooking-native-symbols/) |
| `pltHookRegister(...)` | while loaded | [Ch. 14](/ZygiskLab/book/postspecialize/14-hooking-native-symbols/) |
| `pltHookCommit()` | while loaded | [Ch. 14](/ZygiskLab/book/postspecialize/14-hooking-native-symbols/) |

Three notes that belong here rather than in those chapters, because they are
interface rules rather than technique.

`exemptFd` is the only method whose header comment distinguishes its two failure
modes and then refuses to let you tell them apart: calling it outside
`preAppSpecialize` "is either a no-op (returns true) or an error (returns
false)". A `true` therefore does not prove your fd was exempted.

`hookJniNativeMethods` reports per-entry failure by writing `nullptr` into that
`JNINativeMethod`'s `fnPtr`. There is no return value. If you call the "original"
without checking, you call null.

`pltHookRegister` returns `void` and does nothing until `pltHookCommit()`, which
returns `bool` for the whole batch. Registration is not application.

Two enums accompany the handle. `Option` has `FORCE_DENYLIST_UNMOUNT = 0` and
`DLCLOSE_MODULE_LIBRARY = 1`; `StateFlag` has `PROCESS_GRANTED_ROOT = 1u << 0`
and `PROCESS_ON_DENYLIST = 1u << 1`, bitwise-or'd in `getFlags()`'s return.
`setOption` takes one option per call. The header carries a shouted warning on
`DLCLOSE_MODULE_LIBRARY` — "YOU MUST NOT ENABLE THIS OPTION AFTER HOOKING ANY
FUNCTIONS IN THE PROCESS" — for the obvious reason that unmapping your library
leaves every hook pointing at nothing. Chapter 10 has the rest.

## `REGISTER_ZYGISK_MODULE` as a contract

Chapter 4 unpacked the macro from the author's side. From the interface side it
is a three-part handshake.

**The symbol.** The macro defines a function named `zygisk_module_entry`,
declared at the bottom of the header inside `extern "C"` with
`[[gnu::visibility("default")]]`. One fixed, unmangled, exported name. The
loader `dlopen`s your `.so` and looks up that string. Nothing else about your
library is part of the contract, which is why `-fvisibility=hidden` is safe.

**The version.** `entry_impl` constructs a `static module_abi abi(m)` whose
constructor sets `api_version = ZYGISK_API_VERSION` — 5, from the header you
compiled against — and fills a table of plain function pointers with lambdas
forwarding to your virtuals. That indirection exists so the loader never touches
your C++ vtable layout. Your module and the provider agree on a struct of
`long` and function pointers, not on an object model.

**The refusal.** The whole negotiation is one line:

```cpp
if (!table->registerModule(table, &abi)) return;
m->onLoad(&api, env);
```

The loader inspects `api_version` and returns `false` if it cannot support it.
`entry_impl` returns immediately. `onLoad` is never called, no callback ever
fires, and **nothing is logged by your module**, because your module never ran.
A version-mismatched module is indistinguishable, from your logcat filter, from
a module that was never loaded — which is why the failure catalogue in Chapter 4
tells you to widen the filter and read what the loader says about itself. What
the *provider* prints on a mismatch, if anything, is provider-specific.

`REGISTER_ZYGISK_COMPANION(func)` is the parallel macro for the root daemon,
defining `zygisk_companion_entry(int)`. Its header note carries a warning that
belongs in your head from now: "the function can run concurrently on multiple
threads." That is Chapter 17.

## Module state: what survives what

This is the section to come back to.

**Within one process, across callbacks: everything persists.** Your module
instance is a function-local `static` in `entry_impl`, constructed once. Members
you set in `onLoad` are there in `preAppSpecialize` and still there in
`postAppSpecialize`. Globals, statics, heap allocations, open file descriptors —
all ordinary process state, all intact. This is why stashing `api` and `env` in
`onLoad` works.

**Between processes: nothing is shared.** This is the one that produces bugs
that look like magic, so be exact about the mechanism. Each app process is a
`fork()` of zygote. Your module is not loaded into the zygote daemon — the
header is emphatic that modules load *after* the fork — so each process loads
its own copy of your library, runs its own `entry_impl`, constructs its own
`static` module instance. Two apps running simultaneously have two independent
modules with two independent sets of globals that happen to have been
initialised the same way.

The consequence: a counter you increment in `postAppSpecialize` counts launches
of *that process*, which is almost always one. A cache you populate for app A is
not there for app B. A mutex protects nothing across processes. Worse, none of
this fails loudly — it produces a module that appears to work while every
process quietly starts from the same blank slate, and the symptom only appears
when you finally ask a question whose answer depended on accumulation.

If you need state shared across processes, you need something outside the
process: the companion daemon, which is one process for all of them and is
exactly the "share some resources across multiple processes" use case the
header names ([Chapter 17](/ZygiskLab/book/companion/17-the-companion-process/)),
or a file, subject to what an injected process may write
([Chapter 7](/ZygiskLab/book/load/07-deploying-safely/)).

:::caution
Copy-on-write makes the sharing question subtler than it looks in the general
case, but not for your module: because loading happens after the fork, your
module's own globals were never in the parent's address space to begin with.
There is no "shared until written" phase for your state. Every process's copy is
freshly constructed.
:::

**Across the specialization boundary: process state persists, privilege does
not.** Same pid, same heap, same open descriptors — except that zygote closes
file descriptors during specialization unless you asked for an exemption, and
the mount namespace, uid and SELinux domain all change under you. A pointer
stays valid. A path you could read a moment ago may now be invisible.
[Chapter 12](/ZygiskLab/book/postspecialize/12-what-changed-at-boundary/)
catalogues the difference.

**After `post[XXX]Specialize`: your data persists, your API does not.** Zygisk
unloads itself. Hooks you installed stay installed; threads you started keep
running; memory you allocated is still mapped — unless you set
`DLCLOSE_MODULE_LIBRARY`, in which case your library is unmapped and any code of
yours anyone still points at is gone. The `Api` handle stops doing anything.

**Across reboots: nothing.** There is no module-owned persistence in this
interface. Files under your module directory are readable pre-specialization via
`getModuleDir()`, and that is the extent of it.

## One app launch, annotated

The artefact to return to. Everything above is a claim about one of these rows.

```text
  BOOT
   │
   ├─ init starts zygote; runtime up, framework classes and resources preloaded.
   │  Your module is NOT here. Nothing of yours has run.
   │
   └─ zygote listens on its socket.
        │
   ═════╪══════════════ one app launch ═══════════════════════════════
        │
   [1]  │  system_server (AMS) sends a fork request down the socket.
        │
   [2]  fork()  ──────────────────────────────────────────────────────
        │       New pid. Exact copy of zygote: root, zygote's SELinux
        │       domain, zygote's mount namespace. Not an app yet.
        │
   [3]  │  Zygisk loads your .so into the CHILD and calls
        │  zygisk_module_entry -> entry_impl -> registerModule
        │       version mismatch here => silent return, nothing else runs
        │  onLoad(api, env)
        │       You cannot yet tell an app from system_server.
        │
   [4]  │  preAppSpecialize(AppSpecializeArgs *args)
        │       Privilege: ZYGOTE'S.
        │       args fields are mutable references; optional ptrs may be null.
        │       Full Api: connectCompanion, getModuleDir, exemptFd,
        │       setOption(FORCE_DENYLIST_UNMOUNT).
        │
  ══════╪══════════════════════════════════════════════════════════════
  ══ THE PRIVILEGE BOUNDARY — specialization runs (same pid, no exec) ══
  ══   uid/gid set · SELinux context applied · caps dropped           ══
  ══   mount namespace + storage view · nice_name · seccomp · rlimits ══
  ══   unexempted file descriptors closed                             ══
  ══════╪══════════════════════════════════════════════════════════════
        │
   [5]  │  postAppSpecialize(const AppSpecializeArgs *args)
        │       Privilege: THE APP'S. args is now read-only history.
        │       Api: getFlags, setOption, hookJniNativeMethods, PLT pair.
        │       Gone: connectCompanion, getModuleDir, exemptFd.
        │       The app's own code still has not run.
        │
   [6]  │  Zygisk unloads itself. Api calls become silent no-ops.
        │  With DLCLOSE_MODULE_LIBRARY set, your library is unmapped too.
        │
   [7]  │  The app's code runs: Application, then everything else.
        │       Your hooks, threads and allocations are on their own.
        │
  ═══════════════════════════════════════════════════════════════════
```

For `system_server` the shape is identical with `preServerSpecialize` and
`postServerSpecialize` at [4] and [5], `ServerSpecializeArgs` in place of
`AppSpecializeArgs`, and step [1] happening once at boot rather than per launch.

Read as a table, the same thing:

| Step | Your code | Privilege | Api usable |
|---|---|---|---|
| fork | — | zygote's | — |
| load | `onLoad` | zygote's | all |
| pre | `pre*Specialize` | zygote's | all |
| **specialize** | **none — you are not called** | **crosses** | **—** |
| post | `post*Specialize` | the app's | no companion, dir or fd |
| unload | — | the app's | none |
| app runs | hooks only | the app's | none |

## The rules, condensed

Six statements, each of which you can check a design against.

1. If it needs zygote's privilege, root, the module directory or an fd that
   survives, it happens in `pre[XXX]Specialize` or it does not happen.
2. If it needs the app's view of the filesystem or the app's mapped libraries,
   it happens in `post[XXX]Specialize`.
3. Every optional pointer in `AppSpecializeArgs` may be null on some device.
4. Every `Api` call can fail silently. Check returns; a `void` return means you
   cannot check at all.
5. Your globals are per-process and freshly constructed. Cross-process state
   lives in the companion or a file.
6. Overriding nothing does not keep you out of `system_server`. Decide
   deliberately.

What the header cannot tell you is which of these your provider actually
enforces, and at what version. The interface is fixed at API 5; the behaviour
around it belongs to Magisk or Zygisk Next, and this book does not claim to have
measured it on both. Nothing in this chapter has been run on the reference rig —
Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5 — and
until it has, treat the timeline as a reading of the header, which is what it is.

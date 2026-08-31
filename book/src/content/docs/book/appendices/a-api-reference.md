---
title: "API reference"
description: "Every Zygisk API call in one table: signature, valid callback, provider notes, and failure mode."
sidebar:
  order: 1
status: unverified
---

This documents `ZYGISK_API_VERSION 5`, as declared in the vendored
`modules/01-hello-zygisk/jni/zygisk.hpp` — the official Zygisk C++ API header,
unmodified. Everything below is taken from that header and nothing else. Where
the header states a rule, it is quoted as a rule; where the header is silent,
this appendix says so rather than inventing one.

The header is a client-side declaration. What actually implements each call is
the provider's `api_table`, which you never see. A different provider, or a
future API version, may implement a different subset or behave differently at
the same signature. Chapter 20,
[Where it breaks](/ZygiskLab/book/companion/20-where-it-breaks/), is the chapter
on that gap.

Nothing in this appendix has been run on a device.

## The silent-failure property

Every method on `Api` is an inline forwarder over a function pointer in
`internal::api_table`. Each forwarder null-checks its slot first:

```cpp
inline int Api::connectCompanion() {
    return tbl->connectCompanion ? tbl->connectCompanion(tbl->impl) : -1;
}
```

So a call into a slot the provider did not populate does not crash, does not
log, and does not throw. It returns the failure value — or, for the two `void`
methods, does nothing at all. **You cannot distinguish "the provider does not
implement this" from "the provider ran it and it failed" from a return value.**
Treat every return value as the only signal you get, and check it.

## `ModuleBase` callbacks

Your module class inherits `zygisk::ModuleBase` and overrides what it needs.
Every method has an empty default body, so overriding none of them compiles and
does nothing.

| Signature | Header's description |
| --- | --- |
| `virtual void onLoad(Api *api, JNIEnv *env)` | Called as soon as the module is loaded into the target process. A Zygisk API handle is passed as an argument. |
| `virtual void preAppSpecialize(AppSpecializeArgs *args)` | Called before the app process is specialized. Just forked from zygote, no app-specific specialization applied: no sandbox restrictions, still zygote's privilege. |
| `virtual void postAppSpecialize(const AppSpecializeArgs *args)` | Called after the app process is specialized. All sandbox restrictions for this application are enabled; runs with the same privilege as the app's own code. |
| `virtual void preServerSpecialize(ServerSpecializeArgs *args)` | Called before the system server process is specialized. The header says "see `preAppSpecialize(args)` for more info". |
| `virtual void postServerSpecialize(const ServerSpecializeArgs *args)` | Called after the system server process is specialized. Runs with the privilege of system_server. |

Two structural facts follow from the signatures. The `pre` callbacks take a
non-`const` args pointer and the header says you "can read and overwrite these
arguments to change how the app process will be specialized"; the `post`
callbacks take `const`, so they are read-only. And an app process gets the
`App` pair, system_server gets the `Server` pair — the header does not describe
any process receiving both.

The header states one global lifetime rule, immediately above `struct Api`:

> All API methods will stop working after `post[XXX]Specialize` as Zygisk will
> be unloaded from the specialized process afterwards.

Covered in depth by
[Chapter 5, Anatomy of a module](/ZygiskLab/book/load/05-anatomy-of-a-module/),
[Chapter 8, The specialization window](/ZygiskLab/book/prespecialize/08-specialization-window/),
and
[Chapter 12, What changed at the boundary](/ZygiskLab/book/postspecialize/12-what-changed-at-boundary/).

## `Api` methods

### `connectCompanion`

```cpp
int connectCompanion();
```

| | |
| --- | --- |
| **Valid in** | `pre[XXX]Specialize` only. The header states this explicitly: "This API only works in the `pre[XXX]Specialize` methods due to SELinux restrictions." |
| **Returns** | A file descriptor for a socket connected to the socket passed to your companion request handler. |
| **Failure** | `-1`. The header gives `-1` for a failed connection attempt; the forwarder also returns `-1` when the slot is null. Indistinguishable. |
| **Book** | [Chapter 17](/ZygiskLab/book/companion/17-the-companion-process/), [Chapter 18](/ZygiskLab/book/companion/18-companion-protocol/) |

The header adds that the companion is ABI aware: a 32-bit process is connected
to a 32-bit companion, a 64-bit process to a 64-bit one. It also suggests using
the companion to hold resources shared across multiple processes.

### `getModuleDir`

```cpp
int getModuleDir();
```

| | |
| --- | --- |
| **Valid in** | `pre[XXX]Specialize` only, stated explicitly. Additionally, *accessing* the returned directory is only possible in `pre[XXX]Specialize` or in the root companion process (assuming you sent the fd over the socket) — "both restrictions are due to SELinux and UID". |
| **Returns** | A file descriptor for the root folder of the current module. |
| **Failure** | `-1` "if errors occurred", and `-1` from the null-slot path. |
| **Book** | [Chapter 6](/ZygiskLab/book/load/06-how-the-loader-finds-you/), [Chapter 19](/ZygiskLab/book/companion/19-asymmetry-of-privilege/) |

The header adds a deployment requirement: the module should make sure zygote is
allowed to read the module dir — for example, that the dir has `system_file`
context — "due to SELinux restrictions on socket messages".

### `setOption`

```cpp
void setOption(Option opt);
```

| | |
| --- | --- |
| **Valid in** | Not specified by the header at the method. Per-option guidance appears on the enumerators instead (see [Option](#option)). |
| **Returns** | Nothing. |
| **Failure** | **None observable.** `void` return, and the forwarder simply skips the call when the slot is null. A `setOption` that did nothing looks exactly like one that worked. |
| **Book** | [Chapter 10, `setOption` and the flags](/ZygiskLab/book/prespecialize/10-setoption-and-flags/) |

The header notes the method "accepts one single option at a time" — you call it
once per option, you do not OR them together.

### `getFlags`

```cpp
uint32_t getFlags();
```

| | |
| --- | --- |
| **Valid in** | Not specified by the header. |
| **Returns** | Bitwise-OR'd `zygisk::StateFlag` values describing the current process. |
| **Failure** | `0` from the null-slot path. `0` is also a legitimate result — no flags set — so a zero return proves nothing. |
| **Book** | [Chapter 10](/ZygiskLab/book/prespecialize/10-setoption-and-flags/), [Chapter 11, Choosing not to run](/ZygiskLab/book/prespecialize/11-choosing-not-to-run/) |

### `exemptFd`

```cpp
bool exemptFd(int fd);
```

| | |
| --- | --- |
| **Valid in** | The header: "This API only make sense in `preAppSpecialize`; calling this method in any other situation is either a no-op (returns `true`) or an error (returns `false`)." Note that this is `preAppSpecialize` specifically, not `pre[XXX]Specialize`. |
| **Returns** | `bool`. |
| **Failure** | `false` — and "when `false` is returned, the provided file descriptor will eventually be closed by zygote". The null-slot path also returns `false`. |
| **Book** | [Chapter 8](/ZygiskLab/book/prespecialize/08-specialization-window/), [Chapter 19](/ZygiskLab/book/companion/19-asymmetry-of-privilege/) |

:::caution
`true` is not proof of success. The header defines a no-op as also returning
`true`, so a `true` from the wrong callback means "nothing happened" just as
plausibly as "your fd is exempt". Success here is unprovable from the return
value alone.
:::

### `hookJniNativeMethods`

```cpp
void hookJniNativeMethods(JNIEnv *env, const char *className, JNINativeMethod *methods, int numMethods);
```

| | |
| --- | --- |
| **Valid in** | Not specified by the header. The header's own worked example calls it from `preAppSpecialize`. |
| **Returns** | Nothing. |
| **Failure** | Per entry, in-band: "If no matching class, method name, or signature is found, that specific `JNINativeMethod.fnPtr` will be set to `nullptr`." There is no aggregate result, and the whole call is skipped silently when the slot is null. |
| **Book** | [Chapter 13, JNI inside a live app](/ZygiskLab/book/postspecialize/13-jni-inside-a-live-app/), [Chapter 15](/ZygiskLab/book/postspecialize/15-hooking-java-through-art/) |

On success the original function pointer is written back into each
`JNINativeMethod`'s `fnPtr` — that is how you obtain the trampoline target. So
`fnPtr` carries both the result and the error: check it for `nullptr` before
calling through it, per entry, every time.

### `pltHookRegister`

```cpp
void pltHookRegister(dev_t dev, ino_t inode, const char *symbol, void *newFunc, void **oldFunc);
```

| | |
| --- | --- |
| **Valid in** | **The header is silent.** It documents no callback restriction for this call. Do not assume one in either direction. |
| **Returns** | Nothing — registration only. Nothing is applied until `pltHookCommit`. |
| **Failure** | **None observable at this call.** `void` return, silently skipped on a null slot. Any error surfaces later, as a `false` from `pltHookCommit`, or not at all. |
| **Book** | [Chapter 14, Hooking native symbols](/ZygiskLab/book/postspecialize/14-hooking-native-symbols/) |

The header describes matching by the `dev`/`inode` pair, which "uniquely
identifies a file being mapped into memory", as read from `/proc/[PID]/maps`;
for matching ELFs loaded in memory it replaces `symbol` with `newFunc`. If
`oldFunc` is not `nullptr`, the original function pointer is saved to `oldFunc`.

### `pltHookCommit`

```cpp
bool pltHookCommit();
```

| | |
| --- | --- |
| **Valid in** | **The header is silent**, as with `pltHookRegister`. |
| **Returns** | `true` on success — the header says only that it commits all previously registered hooks. |
| **Failure** | `false` "if an error occurred", and `false` from the null-slot path. The header does not say which registrations, if any, were applied when `false` comes back. |
| **Book** | [Chapter 14](/ZygiskLab/book/postspecialize/14-hooking-native-symbols/) |

:::note
The header gives no timing rule for the PLT hook pair. Chapter 14 makes this
point directly, and this appendix will not manufacture a constraint the header
does not state. What the header *does* constrain is the outer lifetime: all API
methods stop working after `post[XXX]Specialize`.
:::

## `AppSpecializeArgs`

`AppSpecializeArgs() = delete;` — you never construct one; you receive a
pointer from the callback. The struct splits into two halves and they have
different contracts.

### Required arguments

"These arguments are guaranteed to exist on all Android versions." They are
C++ references, so they are always valid and always dereferenceable — and
assigning through one in `preAppSpecialize` rewrites the value specialization
will use.

| Field | Type |
| --- | --- |
| `uid` | `jint &` |
| `gid` | `jint &` |
| `gids` | `jintArray &` |
| `runtime_flags` | `jint &` |
| `rlimits` | `jobjectArray &` |
| `mount_external` | `jint &` |
| `se_info` | `jstring &` |
| `nice_name` | `jstring &` |
| `instruction_set` | `jstring &` |
| `app_data_dir` | `jstring &` |

### Optional arguments

"Please check whether the pointer is null before de-referencing." Each is a
`const` pointer to a mutable value: you may write through it, but you may not
repoint it, and you must null-check it first.

| Field | Type |
| --- | --- |
| `fds_to_ignore` | `jintArray *const` |
| `is_child_zygote` | `jboolean *const` |
| `is_top_app` | `jboolean *const` |
| `pkg_data_info_list` | `jobjectArray *const` |
| `whitelisted_data_info_list` | `jobjectArray *const` |
| `mount_data_dirs` | `jboolean *const` |
| `mount_storage_dirs` | `jboolean *const` |
| `mount_sysprop_overrides` | `jboolean *const` |

The header does not say which Android version introduced any given optional
field, nor what a null pointer implies beyond "do not dereference it". Treated
in depth in
[Chapter 9, Reading `AppSpecializeArgs`](/ZygiskLab/book/prespecialize/09-reading-appspecializeargs/).

## `ServerSpecializeArgs`

Also `= delete` on its default constructor. Every field is a reference; the
header marks none of them optional and adds no commentary.

| Field | Type |
| --- | --- |
| `uid` | `jint &` |
| `gid` | `jint &` |
| `gids` | `jintArray &` |
| `runtime_flags` | `jint &` |
| `permitted_capabilities` | `jlong &` |
| `effective_capabilities` | `jlong &` |

## `Option`

```cpp
enum Option : int
```

Passed one at a time to `Api::setOption`.

| Enumerator | Value | Header notes |
| --- | --- | --- |
| `FORCE_DENYLIST_UNMOUNT` | `0` | Force Magisk's denylist unmount routines to run on this process — all Magisk and modules' files unmounted from the process's mount namespace, regardless of denylist enforcement status. "Setting this option only makes sense in `preAppSpecialize`. The actual unmounting happens during app process specialization." |
| `DLCLOSE_MODULE_LIBRARY` | `1` | Your module's library will be `dlclose`-ed after `post[XXX]Specialize`. "Be aware that after `dlclose`-ing your module, all of your code will be unmapped from memory." |

:::danger
The header's own warning, verbatim and in capitals: **"YOU MUST NOT ENABLE THIS
OPTION AFTER HOOKING ANY FUNCTIONS IN THE PROCESS."** A hook trampoline points
into your library; unmap the library and the next call through that trampoline
lands in nothing.
:::

## `StateFlag`

```cpp
enum StateFlag : uint32_t
```

"Bit masks of the return value of `Api::getFlags()`" — test with a bitwise AND.

| Enumerator | Value | Meaning |
| --- | --- | --- |
| `PROCESS_GRANTED_ROOT` | `(1u << 0)` | The user has granted root access to the current process. |
| `PROCESS_ON_DENYLIST` | `(1u << 1)` | The current process was added on the denylist. |

The header defines exactly these two. A provider returning bits outside this
set is returning something the header does not describe.

## Registration macros and entry symbols

### `REGISTER_ZYGISK_MODULE`

```cpp
#define REGISTER_ZYGISK_MODULE(clazz) \
void zygisk_module_entry(zygisk::internal::api_table *table, JNIEnv *env) { \
    zygisk::internal::entry_impl<clazz>(table, env);                        \
}
```

It defines the exported `zygisk_module_entry` function, so it appears once, at
file scope, for one class. `entry_impl<T>` constructs a function-local `static`
`Api`, a function-local `static T` module instance, and a function-local
`static module_abi`, then:

```cpp
if (!table->registerModule(table, &abi)) return;
m->onLoad(&api, env);
```

The failure mode is worth naming: if `registerModule` returns `false`,
`entry_impl` returns and **`onLoad` is never called**. Your module is loaded
into the process and silent. Nothing in the API surface reports this to you.

Because the module instance is `static`, it is default-constructed on first
entry and lives for the life of the mapping — you get one instance, and its
constructor runs before `onLoad`.

### `REGISTER_ZYGISK_COMPANION`

```cpp
#define REGISTER_ZYGISK_COMPANION(func) \
void zygisk_companion_entry(int client) { func(client); }
```

The registered function "runs in a superuser daemon process and handles a root
companion request from your module running in a target process. The function has
to accept an integer value, which is a Unix domain socket that is connected to
the target process."

:::caution
The header's note, in full: "the function can run concurrently on multiple
threads. Be aware of race conditions if you have globally shared resources."
Your handler is not serialised for you. See
[Chapter 18](/ZygiskLab/book/companion/18-companion-protocol/).
:::

### Entry symbols

Both are declared `extern "C"` with default visibility:

```cpp
[[gnu::visibility("default"), maybe_unused]]
void zygisk_module_entry(zygisk::internal::api_table *, JNIEnv *);

[[gnu::visibility("default"), maybe_unused]]
void zygisk_companion_entry(int);
```

These are the two symbols the loader looks up in your `.so`. `maybe_unused`
means a module with no companion is legal — you define only
`zygisk_module_entry`. Covered in
[Chapter 6, How the loader finds you](/ZygiskLab/book/load/06-how-the-loader-finds-you/).

## The ABI handshake

`REGISTER_ZYGISK_MODULE` is the only place the version number enters your
binary. `module_abi`'s constructor stamps it:

```cpp
module_abi(ModuleBase *module) : api_version(ZYGISK_API_VERSION), impl(module) {
```

with `#define ZYGISK_API_VERSION 5` at the top of the header. `api_version` is
the first field of `module_abi`, followed by `impl` and four thunks — one per
specialize callback — each a capturing-free lambda that forwards to your virtual
method. That struct is what you hand the provider via `registerModule`.

So the version you compiled against is a value in your `.so`, and the provider
reads it and decides. The header says nothing about what a provider does with a
version it does not recognise; the observable consequence in the header's own
code is only that `registerModule` may return `false` and `onLoad` never runs.
`api_table` is declared but never defined in your translation unit — its layout
lives on the provider's side, which is precisely why a version mismatch is the
provider's decision and not something you can detect from inside your module.

## Provider notes

The header is written by the Zygisk author and describes the reference
implementation. It names Magisk twice — in `FORCE_DENYLIST_UNMOUNT` and its
description — and mentions no other provider. The reference rig for this book is
a Pixel 6 Pro, Android 16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5, which
is not the implementation the header was written against.

Nothing in this appendix has been measured against any provider. The header is
the contract; whether a given provider honours all of it is exactly the question
[Chapter 20](/ZygiskLab/book/companion/20-where-it-breaks/) asks. Until you have
run a call on your own rig, the honest position is that every table slot might be
null and every one of these failure values might be the one you are looking at.

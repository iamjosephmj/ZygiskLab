---
title: "The companion process"
description: "What the companion process is, REGISTER_ZYGISK_COMPANION, and the single channel it shares with your injected module."
sidebar:
  order: 1
status: unverified
---

[Chapter 12](/ZygiskLab/book/postspecialize/12-what-changed-at-boundary/) ended
with a problem it could not solve. Once specialization has run, your code is the
app: the app's uid, the app's SELinux domain, the app's mount namespace, and a
filesystem in which the only dependable place to write is the app's own cache
directory. Every privileged thing a module might want to do — read a config under
`/data/adb/`, write a status file a manager UI can see, hold a resource that two
injected apps both need — has been ruled out by the time your interesting code
runs.

The companion is the answer, and it is not the answer people expect. It is not a
way to keep privilege across the boundary, and it is not your module escalating.
It is a **second process that was never in the app's lineage at all**, running as
root, into which the framework loads the same shared library you shipped and
calls a different entry point. Your injected module cannot become that process
and cannot borrow from it. It can only talk to it, over one socket, opened during
the one window in which such a socket can be opened.

Everything in Part V follows from that shape. This chapter establishes it. The
authority throughout is the vendored header at
`modules/01-hello-zygisk/jni/zygisk.hpp`, targeting `ZYGISK_API_VERSION 5`, and
this chapter is careful to separate what that header states from what it merely
leaves room for — because the companion's lifecycle is exactly the sort of thing
each provider implements to its own taste.

## What the companion is

The header introduces it in the development guide, immediately after warning you
that your module class never runs privileged:

> Since your module class's code runs with either Zygote's privilege in
> `pre[XXX]Specialize`, or runs in the sandbox of the target process in
> `post[XXX]Specialize`, the code in your class never runs in a true superuser
> environment.
>
> If your module require access to superuser permissions, you can create and
> register a root companion handler function. This function runs in a separate
> root companion daemon process, and an Unix domain socket is provided to allow
> you to perform IPC between your target process and the root companion process.

Three nouns in that passage carry the whole design. *Separate*: it is a distinct
process with its own pid, own address space, own descriptors. *Root*: it is not
subject to the app sandbox, because it was never specialized into one. *Daemon*:
it belongs to the Zygisk implementation's own root-side machinery, not to your
app process — the header calls it "a superuser daemon process" again where the
registration macro is defined.

Draw the lineage explicitly, because this is where the mental model usually goes
wrong. An injected module gets into an app process because zygote forked, and the
Zygisk implementation loaded your library into the child before specialization
([Chapter 5](/ZygiskLab/book/load/05-anatomy-of-a-module/) covers that load). The
companion gets there by an entirely different route: the provider's root-side
daemon runs your library's companion entry point in a process descended from
*it*. The two processes have a common ancestor somewhere far up the tree, in the
same sense that `init` is everybody's ancestor, and no closer relationship than
that.

The consequence readers most often refuse to believe on first hearing: **the
companion is not "your module with more privilege."** It is a different program
that happens to be built from the same object file. It did not run `onLoad`. It
has no `Api` handle, no `JNIEnv`, no `AppSpecializeArgs`, no notion of which app
is launching. It never observed anything your injected side observed. If it knows
a fact, that fact was either compiled into the library or arrived down the socket.

:::note
The one relationship that does hold is the library itself. The same `.so` is
loaded on both sides, so both sides see the same code, the same constants, the
same struct layouts and the same `#define`s. That is genuinely useful: it means
you can define a wire protocol as a shared header and be certain both ends agree
about it, with no versioning problem between them.
[Chapter 18](/ZygiskLab/book/companion/18-companion-protocol/) builds on exactly
that.
:::

## `REGISTER_ZYGISK_COMPANION` and its lifetime

The registration is one macro, and it is almost embarrassingly thin:

```cpp
#define REGISTER_ZYGISK_COMPANION(func) \
void zygisk_companion_entry(int client) { func(client); }
```

That is the entire mechanism. There is no class, no base to inherit, no
registration call at runtime, no handle handed to you. The macro defines a
function with a fixed name, and the header declares that name in an `extern "C"`
block with default visibility:

```cpp
[[gnu::visibility("default"), maybe_unused]]
void zygisk_companion_entry(int);
```

So the contract is a symbol. Your library exports `zygisk_companion_entry`; the
provider's root daemon looks that symbol up and calls it. If your library does
not export it — because you never wrote `REGISTER_ZYGISK_COMPANION`, or because
your visibility flags stripped it — there is nothing to call, and your module
simply has no companion. Note the contrast with
`REGISTER_ZYGISK_MODULE(clazz)`, which expands to `zygisk_module_entry` and goes
through `entry_impl` to register a `module_abi` and receive an `Api *`. The
companion side has none of that machinery. It is a raw C entry point taking one
integer.

The integer is the whole interface. The macro's own comment says what it is:

> The function has to accept an integer value, which is a Unix domain socket
> that is connected to the target process.

And on the other end, `Api::connectCompanion()`:

> Returns a file descriptor to a socket that is connected to the socket passed to
> your module's companion request handler. Returns -1 if the connection attempt
> failed.

The two file descriptors are the two ends of one connected Unix domain socket
pair. The injected side holds one; the handler's `client` argument is the other.

### When it exists relative to your app process

Here the header is precise about one thing and silent about several others, and
it is worth being disciplined about the difference.

**Stated in the header:** `connectCompanion()` "only works in the
`pre[XXX]Specialize` methods due to SELinux restrictions." That is not a policy
choice the provider made to be tidy, and it is not something a future version is
likely to relax — an app-domain process is not permitted to connect to a
root-daemon socket, and the restriction exists on the far side of your code. In
`preAppSpecialize` you still hold zygote's identity
([Chapter 8](/ZygiskLab/book/prespecialize/08-specialization-window/)), and
zygote's identity is what the connection is permitted to.

**Stated in the header:** the companion is ABI aware. "When calling this method
from a 32-bit process, you will be connected to a 32-bit companion process, and
vice versa for 64-bit." On a device that still runs some apps in 32-bit mode,
that means at least two distinct companion processes exist across the system for
one module, one per ABI, with no relationship between them. Anything you cache in
a companion for a 64-bit app is invisible to the 32-bit one.

**Stated in the header, indirectly but usefully:** the companion outlives the
call. `getModuleDir()`'s comment notes that the module directory is accessible
"in the pre[XXX]Specialize methods or in the root companion process (assuming
that you sent the fd over the socket)" — which only makes sense if the companion
is around, holding a descriptor you passed it, doing work while your injected
side is elsewhere.

**Not stated anywhere in the header:** when the companion process is created,
whether it is started lazily on the first `connectCompanion()` or eagerly at boot,
how long it lives after the last client disconnects, whether it is restarted if
it crashes, and what happens to it when the module is disabled or updated. Every
one of those is provider behaviour. Do not design against an answer you have
guessed, and see
[Chapter 20](/ZygiskLab/book/companion/20-where-it-breaks/) for what actually
varies.

:::caution
The single practical rule that falls out of the SELinux restriction: **if you
will ever need the companion for a process, connect in `preAppSpecialize` and
keep the descriptor.** You cannot open the connection later: a *new* connection
is not available on the far side at any price. The cost of connecting
speculatively is one socket per app launch; the cost of not connecting is a
feature you cannot implement.

Carrying that descriptor across the boundary is a different matter, and a much
less settled one. `exemptFd()` is the API that exists for it, but the header
itself notes a `true` return may mean the call was a no-op, and on the reference
rig it has been observed returning `false` outright — so a descriptor you
believe you exempted may not survive.
[Chapter 20](/ZygiskLab/book/companion/20-where-it-breaks/) covers this in full,
and it is the reason Chapter 19's status design uses two independent writers
rather than one held connection. Do not build on the assumption that a socket
crosses the boundary until you have proved it does on your own provider.
:::

## What it inherits, what it shares, and the one channel

This is the section that prevents the most wasted debugging, so it is worth
stating flatly before qualifying anything.

**Your two sides share no memory, no globals, no heap, no static initialiser
results, and no file descriptors except the ones you deliberately pass over the
socket.** They are two processes. Nothing about loading the same library changes
that.

The confusion is understandable, because within a single process a Zygisk module's
state does persist in exactly the way people expect: you stash `api` and `env` in
`onLoad`, set a global in `preAppSpecialize`, and read it in
`postAppSpecialize`, all in the same address space
([Chapter 5](/ZygiskLab/book/load/05-anatomy-of-a-module/)). It is natural to
extend that intuition one hop further, to the companion, and it is wrong. A
counter you increment in the injected side is a counter the companion has never
heard of. A struct you populate in `preAppSpecialize` is not visible to the
handler unless you serialise it and write it down the socket. A `static bool
g_initialised` exists twice, independently, with independent values.

Nor does the companion inherit anything *from your app process*. It is not forked
from it, so there is no copy-on-write snapshot of your state at connection time.
The direction of inheritance runs from the provider's root daemon, whose
environment — working directory, environment variables, namespace, open
descriptors — is the provider's business and not something the header describes.

| | Injected side | Companion side |
|---|---|---|
| **Lineage** | forked from zygote | forked from the root daemon |
| **Privilege** | zygote's, then the app's | root, throughout |
| **Entry point** | `zygisk_module_entry` | `zygisk_companion_entry` |
| **Gets `Api` / `JNIEnv`** | yes | no |
| **Knows the package** | yes, from args | only if told |
| **Globals** | its own copy | its own copy |
| **Shared with the other** | the socket | the socket |

So: the socket is the entire relationship. That is not a limitation to work
around; it is the design, and it is a good one, because it forces every
cross-side dependency to be explicit. Anything both halves need to know is either
compiled into the library — constants, opcodes, struct layouts, paths — or is sent
as bytes. There is no third mechanism, and a design that seems to need one is a
design that has not yet decided which side owns the fact.

The channel is also better than a byte pipe, and Part V leans on this. A Unix
domain socket can carry file descriptors between processes, which is why
`getModuleDir()`'s comment mentions sending the fd over the socket. That turns
the companion into a resource broker: it can open a file no app process could
open and hand the open descriptor across, and the app-side process then holds a
descriptor whose permissions were resolved at open time by a root process. The
header names this as a use case directly — "if you want to share some resources
across multiple processes, hold the resources in the companion process and pass
it over."
[Chapter 18](/ZygiskLab/book/companion/18-companion-protocol/) covers the framing
and the descriptor-passing mechanics;
[Chapter 19](/ZygiskLab/book/companion/19-asymmetry-of-privilege/) works through
what each side can and cannot see, including the two-writer status problem
Chapter 12 raised: the injected side knows what is happening and cannot write
where anyone can read it, the companion can write anywhere and knows almost
nothing.

## When it forks, how many exist, and what that means for state

Now the part where discipline matters most, because it is where a book can most
easily invent a guarantee.

The header makes exactly one statement about concurrency, in the comment directly
above the registration macro:

> NOTE: the function can run concurrently on multiple threads. Be aware of race
> conditions if you have globally shared resources.

Read that carefully, because it says more than it appears to. It tells you that
multiple invocations of your handler can be **live at once**, and that they can be
live **in the same process** — threads share an address space, which is why
globals are a hazard worth warning about. So the companion is not a
one-connection-one-process arrangement in which each client gets a fresh, clean
copy of your state. Your handler is a server handler, and it must be written like
one: no unsynchronised globals, no static buffers reused across calls, no
assumption that you are the only one running.

What the header does **not** say, and what you must therefore not assume:

- **How many companion processes exist.** Beyond the ABI split — one 32-bit, one
  64-bit — the header commits to no number. Whether all connections for one ABI
  land in a single long-lived process, or several exist, is unstated.
- **When the process is created.** Nothing says whether it is spawned at boot,
  on the first `connectCompanion()`, or on some provider-chosen schedule.
- **Whether it is reused across connections.** The threading note is consistent
  with a persistent process serving many connections, but the header stops short
  of promising the process survives between them, or that a later connection
  reaches the same instance as an earlier one.
- **What happens when it dies.** No restart guarantee, no notification to your
  injected side beyond the socket going away, no documented resource limits.

The safe design follows directly, and it costs almost nothing:

**Treat every connection as independent, and keep no cross-connection state you
cannot rebuild.** If your handler needs configuration, read it at the start of the
connection rather than caching it in a global from a previous one. If you keep a
cache anyway — because reading a file per connection is genuinely expensive —
make it a pure optimisation: guarded against concurrent access, and correct to
throw away and rebuild at any moment. State that would be *wrong* to lose is state
you have made the companion's lifecycle responsible for, and the header did not
accept that responsibility.

The corollary is where to put facts that must genuinely persist. Not in companion
memory: on the filesystem, written by the companion, which is root and can write
where it likes. A file under a path the provider gives you survives a companion
restart, survives a reboot if you want it to, and is readable by the tooling that
needs to read it. This is the inverse of the injected side's problem, and it is
the single largest reason the split is worth its awkwardness.

:::danger
Do not let a companion hold the only copy of something a user would miss. If your
module's state lives in a long-running companion's heap and that process is
restarted for a reason the header never promised it would not be, the state is
gone with no error and no event you can observe from the injected side — you will
see a socket that closes, or a new connection that behaves as if it is the first.
Persist anything that matters, and make restart the boring case.
:::

## What remains unknown until you run it

Nothing in this chapter has been run on the reference rig — Pixel 6 Pro, Android
16, arm64, KernelSU-Next 3.3.0, Zygisk Next 1.4.5 — and the two halves of it
deserve different amounts of trust, in the same way Chapter 12's did.

The interface is solid and version-stable. One exported C symbol, one integer
argument that is a connected Unix domain socket, a connection that can only be
made from the pre-specialize methods, ABI-matched companions, handlers that may
run concurrently on threads. Those are in the header, they are the contract every
provider implements, and you can design against them.

The lifecycle is not. When the companion appears, how many there are, how long it
survives idle, whether it comes back after a crash, what it costs you in memory
while it waits — none of that is written down, all of it is the provider's, and
all of it can change with a provider update that does not touch your module at
all. Four measurements are worth more than this section: find the companion's pid
and see when it first appears; watch whether that pid changes across several app
launches; kill it and see whether a subsequent `connectCompanion()` succeeds; and
leave the device idle and see whether it is still there afterwards. Write down the
four answers next to your provider's version.
[Chapter 20](/ZygiskLab/book/companion/20-where-it-breaks/) is where the book
collects the ways those answers differ.

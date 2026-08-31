---
title: "Threading and timing"
description: "Lab 5: getting onto the main thread safely, waiting for app readiness, and proving which thread your code ran on."
sidebar:
  order: 5
status: unverified
---

You are the app now. Everything you lost, you lost. What you have left is a
callback that has already returned, or is about to, and an app that has not
started yet — and almost everything you might want to do to that app has to
happen on a thread you are not currently thinking about, at a moment that has
not arrived.

This chapter is about that gap. Chapters 13, 14 and 15 gave you the mechanisms:
JNI into a live runtime, native symbol interception, Java method interception.
All three assume you know *when* to fire them and *where* — meaning on which
thread. Neither is handed to you. `postAppSpecialize` returns and your module
has no further callback; the app then does all of its actual startup without
telling you. If you want to act inside that startup, you have to pick a moment
the app itself will reach, get onto the right thread when it does, and be able
to prove afterwards that you were where you thought you were.

The module for this chapter is `modules/05-main-thread/`. It does exactly two
things: it logs which OS thread each callback ran on, and it arranges for one
small, harmless action to run on the app's real main thread at a moment it
chose, without blocking either Zygisk callback to get there.

## What actually requires the main thread

"Main thread" in an Android app means one specific thing: the thread that
called `Looper.prepareMainLooper()` and is now sitting in `Looper.loop()`
dispatching messages. `ActivityThread.main()` sets that up, and from then on
that thread is the one the framework treats as the app's.

The list of things that require it is not a style preference, it is enforced:

- **View hierarchy access.** `ViewRootImpl` records the thread that created the
  hierarchy and throws `CalledFromWrongThreadException` if anything else touches
  it. Every `Activity` lifecycle callback, every `View` mutation, every
  `invalidate()` that matters.
- **Lifecycle callbacks themselves.** `Application.onCreate()`,
  `Activity.onCreate()`, `Service` callbacks by default, `BroadcastReceiver`
  callbacks by default — all dispatched by the main `Looper`'s handler. If you
  want to observe or alter them, you observe them on that thread or you do not
  observe them at all.
- **Constructing a `Handler` with no explicit `Looper`.** It binds to the calling
  thread's `Looper`, and on a thread with none prepared it throws.
- **A great deal of framework state that is documented as single-threaded** and
  guarded by nothing at all — no lock, only the convention that only one thread
  ever touches it. Reaching into that from a worker thread does not throw. It
  corrupts, sometimes, later, in the app's code, and it looks like the app's
  bug.

That last category is why this chapter is not optional. The failures are
asymmetric: the thread checks that throw are the *kind* ones. The silent ones
give you a crash report with the app's stack in it and none of yours.

## Your callbacks are on the right thread at the wrong time

Here is the part that surprises people. On Linux, a process's main thread has
`tid == pid`, and that identity is fixed for the life of the thread. Zygote
forks each app process from a single thread, so the process starts with exactly
one thread — the one `fork()` returned on — and that thread's tid *is* the new
pid. Your `preAppSpecialize` and `postAppSpecialize` callbacks run on it.

So you are already on the OS thread that will become the app's main thread. That
sounds like the problem is solved. It is not, because being on the main thread
and being at a moment where main-thread work is meaningful are two different
facts:

- The Java-level main `Looper` has not necessarily been prepared. There is no
  message loop to post to.
- `ActivityThread`, `Instrumentation` and `Application` do not exist yet. There
  is nothing to attach to, hook into, or read from.
- The runtime is mid-transition out of a fork
  ([Chapter 8](/ZygiskLab/book/prespecialize/08-specialization-window/)), and
  arbitrary JNI work in that window is exactly what that chapter told you not
  to do.

The thread is right; the clock is wrong. What you need is a way to be on that
same thread *later*, once the app has assembled itself enough to be worth
touching — and to get there without holding up `postAppSpecialize`, because
every microsecond spent in that callback is charged to the app's launch time.

## Three routes onto the main thread

The module weighed three. They are worth understanding as a set, because the one
it picked is not universally the best — it is the best given a constraint this
lab deliberately imposes.

| Route | What it needs | How it fails |
| --- | --- | --- |
| Post a `Runnable` to the main `Handler` | A Java class implementing `Runnable` | You have no `.dex` to define it in |
| Read `Looper` internals natively | A stable in-memory layout | Silently, on any ART change |
| Hook a `native` method the framework calls on the main thread | A method name and signature that still resolve | Loudly, at install time |

**Posting a `Runnable`** is the obvious answer and the one a module built
alongside a Java or Kotlin component should use. `Handler.post(Runnable)` on the
main `Looper` is the framework's own supported way to say "run this on the main
thread". It needs a live Java object implementing `Runnable`, and the labs in
this book are `jni/`-only: `ndk-build`, no Gradle, no `.dex` or `.jar` in which
to define that class. You can synthesise bytecode at runtime — a hand-built
class file through `DefineClass`, or a hand-built dex through
`InMemoryDexClassLoader` — but that is a lab of its own, and it would make
`./build.sh` depend on a bytecode assembler this repository does not ship. It
is the right answer for a module with a Java side; it is not available here.

**Reading `Looper` internals from native code** — parsing the native
`MessageQueue` or the `Looper` object's fields to detect when the loop starts
pumping — trades one guess for a worse one. A method name and signature are
public API surface that changes visibly and rarely; an object's memory layout is
an implementation detail that changes between ART releases with no announcement.
Worse, getting a layout wrong does not fail cleanly. It reads a plausible number
out of the wrong offset and carries on.

**Hooking a `native` method the framework itself calls on the main thread** is
what the module does. You do not need to construct anything Java-side, you do
not need to guess at layouts, and you get onto the main thread by waiting for
the app to arrive at a point it was always going to reach. The mechanism is
`Api::hookJniNativeMethods()` — the same call the header's own top-of-file
example demonstrates, and the JNI-method sibling of the PLT hook from
[Chapter 14](/ZygiskLab/book/postspecialize/14-hooking-native-symbols/).

### The method it picks, and why

`android.os.Process.setArgV0(String)` is a `static native` method that sets the
process's `argv[0]` — what `ps` and `/proc/<pid>/cmdline` report. AOSP calls it
from `ActivityThread.handleBindApplication()`, which is what the main thread's
`H` handler runs in response to the `bindApplication()` Binder call from
`ActivityManagerService`. That is the call that turns a freshly specialized,
still-generic process into *this specific app*.

Three properties make it a good checkpoint:

- It runs on the real main thread, through the real main `Looper`, strictly
  after `Looper.prepareMainLooper()` has already executed in
  `ActivityThread.main()`.
- It happens early within `handleBindApplication()`, ahead of `Instrumentation`
  and `Application` being created — so it is later than "the OS thread that will
  become main" but earlier than the app having any object graph of its own for
  your module to disturb.
- It is universal and singular: every app process sets its `argv[0]` this way,
  exactly once, regardless of what the app itself does.

:::caution
That call site — `handleBindApplication()` calling `Process.setArgV0()` on the
main-thread `Looper`, ahead of `Application` creation — is read out of framework
source, not observed on a device. Nothing in this module has been run on the
reference rig. If a future Android version moves, renames or removes that call,
the hook never resolves or never fires, and the module says so in `logcat`
rather than pretending otherwise.
:::

## Installing the hook without paying for it

The hook goes in during `preAppSpecialize`:

```cpp
clock_gettime(CLOCK_MONOTONIC, &armedAt);
haveArmedAt = true;

JNINativeMethod methods[] = {
    {"setArgV0", "(Ljava/lang/String;)V", (void *) my_setArgV0},
};
api->hookJniNativeMethods(env, "android/os/Process", methods, 1);
orig_setArgV0 = (SetArgV0Fn) methods[0].fnPtr;
```

This is the whole scheduling mechanism, and the reason it is cheap is that
installing a hook is not the same as doing the work. `hookJniNativeMethods()`
rewrites one JNI method-table entry and returns. No app code runs, nothing
blocks, and `preAppSpecialize` goes on to return in the same handful of
microseconds Lab 3 measured. The action itself happens later, on Android's
schedule, entirely off this call's stack.

It goes in `preAppSpecialize` rather than after the boundary for two reasons:
the header documents the call against the pre-specialize window, and installing
early means the hook is live for the whole of the rest of startup, including the
`handleBindApplication()` call it is waiting for — not just whatever remains
after `postAppSpecialize` returns.

`postAppSpecialize` then deliberately does nothing but log:

```cpp
// Deliberately does nothing further here. This method still runs
// on the critical path of app startup - whatever it does, the app
// waits for - so the main-thread action is not attempted here. It
// was already scheduled, for later, by installing the hook above
// in preAppSpecialize; this method returns immediately either way.
```

### Failure is signalled by a null pointer and nothing else

The header is explicit, and it matters here more than anywhere:

> If no matching class, method name, or signature is found, that specific
> `JNINativeMethod.fnPtr` will be set to `nullptr`.

There is no return value, no exception, no log. A typo'd signature and a
successfully installed hook are indistinguishable at the call site. So the
module checks, every time:

```cpp
if (orig_setArgV0 != nullptr) {
    LOGI("preAppSpecialize: pid=%d proc=%s ARMED, Process.setArgV0 hook installed",
         getpid(), name);
} else {
    LOGW("preAppSpecialize: pid=%d proc=%s ARMED, but hookJniNativeMethods "
         "could not resolve android.os.Process.setArgV0 - main-thread action "
         "will NOT run", getpid(), name);
}
```

If resolution failed, Android keeps calling the real, un-replaced method and the
app is completely unaffected. The only cost is a log line that never appears —
which is precisely why you must log the warning. Waiting for output that can
never come is the most expensive way to debug a hook.

## Waiting for readiness without polling and without racing

The general principle is worth stating on its own, because it outlives this
particular hook: **find a moment the app itself will reach, rather than guessing
a delay.**

The anti-pattern is sleeping and hoping. It looks like `sleep(2)` on a spawned
thread, or a loop that checks every 100ms whether some class has loaded yet, and
it is wrong in both directions at once. Too short and you race the app and lose,
intermittently, on cold boots and slow devices. Too long and you have added
latency to something that was already ready, and you still have no guarantee —
a device under memory pressure will blow through any margin you picked. There is
no delay that is correct, because the thing you are waiting for is not a
duration. It is an event.

Polling is the same mistake with extra cost. It burns CPU on every launch to
observe a transition that the runtime could have told you about for free, and it
still gives you an answer at an arbitrary point *after* the event rather than at
it — which matters when the whole point was to act before `Application` exists.

The alternative in every case is to name a synchronisation point that the app's
own control flow passes through, and attach to it. `Process.setArgV0` is one
such point. Any `native` method the framework calls at a
known stage works the same way, and so does a Java method hooked through the
techniques in [Chapter 15](/ZygiskLab/book/postspecialize/15-hooking-java-through-art/),
or a native symbol from
[Chapter 14](/ZygiskLab/book/postspecialize/14-hooking-native-symbols/). The
choice of point is a judgement about the stage you need; the *shape* of the
solution — wait on an event, not on a clock — does not vary.

This also removes the race. You are not observing a state and hoping it is
still true when you act on it. You are running *inside* the call that
establishes the state, on the thread that establishes it, before it returns.

## Doing slow work without ANRing the host

The corollary is important enough to say plainly: getting onto the main thread
is not permission to stay there.

Everything the main thread does, the user waits for. The framework's ANR
watchdog is the visible enforcement, but long before you trip it you are adding
latency to input dispatch, frame production and lifecycle callbacks, in an app
whose owner has no idea your module exists. `modules/05-main-thread/`'s action
is one log line and a timestamp for exactly this reason: it is the smallest
thing that can demonstrate the mechanism.

If your real work is slow — network, file I/O, parsing, cryptography, anything
with a loop whose bound you cannot state — it does not belong on the main
thread. Do the minimum on the main thread (capture what you can only capture
there, hand it off) and move the rest to a thread you own.

A thread you own after the boundary is legitimate — the ground has stopped
moving, which is exactly what
[Chapter 8](/ZygiskLab/book/prespecialize/08-specialization-window/) said was
missing before it. But it comes with the obligations
[Chapter 13](/ZygiskLab/book/postspecialize/13-jni-inside-a-live-app/)
established, and they are not optional: a `JNIEnv *` is per-thread and must
never be shared, a thread you created with `pthread_create` has no env until it
attaches through the `JavaVM`, and every attach needs a matching detach or you
leak a runtime-visible thread for the life of the process. Re-read that chapter
before you spawn anything; this one does not repeat it.

The module for this lab spawns no threads at all. That is worth noticing rather
than skipping over — the absence of a thread is why it has no attach/detach
discipline, no lifetime question, and nothing to clean up if the process dies
before the hook fires.

## Proving which thread you are on

Every callback in the module calls this:

```cpp
static void logThread(const char *where) {
    pid_t pid = getpid();
    pid_t tid = gettid();
    LOGI("%s: pid=%d tid=%d gettid()==getpid() -> %s",
         where, pid, tid, (pid == tid) ? "true" : "false");
}
```

It works because of the identity established earlier: the process's original
thread — the one the fork left it with — has `tid == pid`, and every other
thread in the process, whether you spawned it, the runtime spawned it, or it is
a binder thread, has a distinct tid. Two syscalls, always available, no runtime
state required, and the answer lands in `logcat` where you can read it rather
than in a comment where you have to believe it.

Be precise about what it proves and what it does not. `gettid() == getpid()`
tells you that you are on the process's original OS thread. It does not tell you
that the Java main `Looper` exists, or that `Application` has been created. In
this module's armed trace all three call sites are expected to report `true` —
`preAppSpecialize`, `postAppSpecialize` and the hook — because all three run on
that same original thread. What separates them is not the thread. It is the
*moment*: the first two run before any of the app's Java-level startup, and the
third runs inside it.

That is why the module also logs a `CLOCK_MONOTONIC` delta from the instant
`preAppSpecialize` armed the hook. The tid equality answers "which thread"; the
delta answers "how much later", and together they are the claim: same thread, a
measurable interval later, inside a call the framework makes on the main
`Looper`.

:::note
The example trace in the module's `README.md` carries millisecond figures. They
are illustrative — the README says so explicitly. Nothing in this module has
been run on the reference rig, and no timing in this book is a measurement until
you take it yourself.
:::

Proof beats assumption here specifically because of the failure mode. A module
that assumes it is on the main thread and is not does not crash at the point of
the mistake. It corrupts single-threaded framework state, or throws
`CalledFromWrongThreadException` out of a call stack that has the app's frames
in it and none of yours, or produces an intermittent glitch on some devices and
not others. You will debug the app. The app is fine. Two syscalls, logged at
every call site, is a very cheap way to never spend that afternoon.

## What only a device can confirm

Three assumptions in this chapter can only be settled by running it, and the
lab exists to settle them:

- That `ActivityThread.handleBindApplication()` still calls
  `Process.setArgV0()`, at that position, on Android 16.
- That the hook genuinely lands on the app's real main thread — that the
  `logThread()` call inside `my_setArgV0()` reports `true`, on the same tid the
  app uses for everything after. The module does not cross-check against
  `Looper.getMainLooper().getThread()`, because doing so would need the same
  Java-side machinery the design deliberately avoids.
- That `hookJniNativeMethods()` behaves as the header documents — `fnPtr` set to
  `nullptr` on a resolution failure — under Zygisk Next 1.4.5 specifically,
  rather than only under the upstream Zygisk implementation the header describes.

:::note[Lab 5]
[Lab 5](/ZygiskLab/labs/lab-05-threading-and-timing/) turns all three into
evidence, or into a clear negative result. Both are worth having; only one of
them is worth writing down as fact.
:::

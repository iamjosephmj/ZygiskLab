---
title: "JNI inside a live app"
description: "Attaching a JNIEnv, solving the classloader problem, reflection helpers, reference hygiene, and exception discipline."
sidebar:
  order: 2
status: unverified
---

[Chapter 12](/ZygiskLab/book/postspecialize/12-what-changed-at-boundary/) left you
with a working JVM and a warning: the runtime is up, the app's own classes are
loaded or loading, and the classloader you can reach from native code is not the
one that can see them. This chapter closes that gap. It is the point where you
stop observing the app process and start reaching into it — pulling a `jclass`
out of the app's own DEX, calling a method on an object you were handed, reading
a field the framework never meant you to read — and it is the chapter where the
mistakes stop being your module's problem and start being the *app's* crash
report.

JNI's rules are precise and they are stable across versions; state them firmly
and obey them. Android's internals are neither. This chapter keeps those two
kinds of claim visibly apart, because the failures that waste days are the ones
where a rule you learned on one release quietly stopped holding on another.

## `JNIEnv` is per-thread; `JavaVM` is what you keep

`postAppSpecialize` hands you nothing, but `onLoad` handed you a `JNIEnv *`, and
Chapter 5 had you stash it. Stash it carefully, because a `JNIEnv *` is not a
handle to the runtime. It is thread-local state, and the JNI specification is
unambiguous that it must not be shared between threads. The pointer you saved in
`onLoad` is valid on the thread that called `onLoad` — which, in the app process,
is the main thread — and using it from a worker thread is undefined behaviour
that CheckJNI will catch and a release build will not.

The process-wide handle is the `JavaVM *`. There is exactly one per process on
Android, and you get it from the env you already have:

```cpp
class MyModule : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        api_ = api;
        env_ = env;                 // valid on THIS thread only
        env->GetJavaVM(&vm_);       // valid everywhere, for the process lifetime
    }
private:
    zygisk::Api *api_ = nullptr;
    JNIEnv *env_ = nullptr;
    JavaVM *vm_ = nullptr;
};
```

From any other thread, the `JavaVM` gives you a correct env. There are two cases
and they are not interchangeable. A thread the runtime already knows about — a
Java thread that has called down into your hook — already has an env, and
`GetEnv` returns it. A thread you created with `pthread_create` has none, and
must attach.

```cpp
// Returns an env for the current thread. *needs_detach is set if we attached.
static JNIEnv *env_for_this_thread(JavaVM *vm, bool *needs_detach) {
    *needs_detach = false;
    JNIEnv *env = nullptr;
    const jint rc = vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
    if (rc == JNI_OK) return env;
    if (rc != JNI_EDETACHED) return nullptr;      // JNI_EVERSION, or worse

    JavaVMAttachArgs args{};
    args.version = JNI_VERSION_1_6;
    args.name    = "zygisklab-worker";            // shows up in traces; be honest
    args.group   = nullptr;
    if (vm->AttachCurrentThread(&env, &args) != JNI_OK) return nullptr;
    *needs_detach = true;
    return env;
}
```

Attaching is the easy half. **A thread you attached must call
`DetachCurrentThread` before it exits.** Android's JNI documentation states this
as a requirement, not a suggestion: an attached thread that dies without
detaching leaves the runtime holding a thread record that will never be reaped,
and the runtime can refuse to shut down cleanly waiting for it. In an ordinary
app that is a leak. In an injected module it is a leak inside somebody else's
process, attributable to them.

The discipline that survives early returns is the same one Chapter 9 used for
strings — put it in a destructor:

```cpp
class ScopedEnv {
public:
    explicit ScopedEnv(JavaVM *vm) : vm_(vm) {
        env_ = env_for_this_thread(vm_, &detach_);
    }
    ~ScopedEnv() { if (detach_ && vm_ != nullptr) vm_->DetachCurrentThread(); }

    ScopedEnv(const ScopedEnv &) = delete;
    ScopedEnv &operator=(const ScopedEnv &) = delete;

    explicit operator bool() const { return env_ != nullptr; }
    JNIEnv *operator->() const { return env_; }
    JNIEnv *get() const { return env_; }

private:
    JavaVM *vm_;
    JNIEnv *env_ = nullptr;
    bool detach_ = false;
};
```

Never detach an env you did not attach. If `GetEnv` succeeded, that thread
belongs to the runtime and detaching it is a bug that will present as the app's
own threads misbehaving. `ScopedEnv` encodes exactly that distinction in
`detach_`.

:::caution
If a thread of yours has a long life and repeatedly does short bursts of JNI
work, attaching and detaching around each burst is wasteful, and holding an
attachment across the whole life is the alternative. Either is correct; what is
not correct is attaching once and letting the thread exit. Where the thread is
started from a library and you do not control its exit path, Android's
documentation points at `pthread_key_create` with a destructor that calls
`DetachCurrentThread`. [Chapter 16](/ZygiskLab/book/postspecialize/16-threading-and-timing/)
returns to thread lifetimes properly.
:::

## `FindClass` finds the wrong classes

Here is the failure that costs people an afternoon. You write this in a worker
thread and it works:

```cpp
jclass str = env->FindClass("java/lang/String");   // fine
```

You write this next to it and it throws `ClassNotFoundException`:

```cpp
jclass thing = env->FindClass("com/example/app/Thing");   // fails
```

Same env, same thread, same process — and the app's `Thing` class is definitely
loaded, because you can see it in a stack trace. The inconsistency is the clue.

`FindClass` does not search the process. It resolves the name through *a*
classloader, and which one it picks is determined by the calling context: the
classloader of the class whose native method is currently on the stack. On a
thread you attached yourself there are no Java frames at all, so there is no such
class, and Android's documentation is explicit about the fallback — "the JavaVM
will start in the 'system' class loader instead of the one associated with your
application, so attempts to find app-specific classes will fail."

The system classloader can see the boot classpath. That is why
`java/lang/String` resolves, and `android/os/Build`, and most of the framework.
It cannot see the app's APK, because the app's DEX files are loaded by a
classloader the framework constructed for that app at startup. Every class you
actually care about — the app's own types, and anything from a library bundled
into the APK — lives behind that loader and is invisible to `FindClass` from a
bare native thread.

Note the sharp edge in the wording: it is the *calling context* that decides, not
the thread's origin. If your code is running inside a JNI hook — a native method
you installed on an app class, or a call that arrived through
`Api::hookJniNativeMethods` — then there *is* a Java frame, and `FindClass` will
use that class's loader, and app classes resolve fine. The same helper function
therefore succeeds when called from a hook and fails when called from your
worker. That is why the bug reproduces intermittently and why the first instinct
is to blame timing.

The rule to internalise: **`FindClass` is only trustworthy for boot-classpath
names, unless you know a Java frame is on the stack.** For everything else, do
not use it.

## Getting a classloader that can see the app

The approach is invariant even though the routes are not: obtain a reference to
a classloader that has the app's DEX, then resolve names through its
`loadClass(String)` method rather than through `FindClass`. `loadClass` takes a
dotted binary name (`com.example.app.Thing`), not the slashed internal form
`FindClass` wants — mixing those up is its own afternoon.

Where you get that loader from depends entirely on how early you are and what
the app has already built, and this is the part of the chapter that is
version- and app-dependent rather than specified.

**From an object you already have.** The most robust route, and the one to
prefer whenever it is available, is to take the loader off something you were
handed. Every `jobject` knows its class, and every class knows its loader:
`GetObjectClass` on the instance, then `Object.getClass().getClassLoader()`, or
`Class.getClassLoader()` directly. If you have hooked a method on an app class,
the `this` you were passed is a perfectly good source of the app's loader, with
no internal APIs involved. This route has no version dependency worth naming
because it uses only public `java.lang` methods.

**From the running application context.** If the app has progressed far enough
to have constructed its `Application` object, that object is a `Context`, and
`Context.getClassLoader()` is public API returning exactly the loader you want.
The dependency here is not on internal APIs but on *timing*: at
`postAppSpecialize` the process has been specialized but the app's
`Application` has not necessarily been created yet, and reaching for it too early
gets you nothing. Chapter 16 is where this timing question gets its own
treatment.

**From framework internals.** There are well-known internal paths to the current
activity thread and through it to the loaded APK and its loader. They work, they
are widely used, and they are exactly the kind of claim this book will not make
flatly. As an example only, and one you must verify against the Android version
you are on: `android.app.ActivityThread.currentActivityThread()` is a static
method returning the process's activity thread, from which the application
context and hence a loader is reachable. Treat the names, the signatures and
the reachability of that path as unverified on any release you have not tested,
and read the next section on hidden-API restrictions before you rely on it —
internal is precisely the category Android restricts.

:::note
The reference rig for this book is a Pixel 6 Pro on Android 16, arm64,
KernelSU-Next 3.3.0, Zygisk Next 1.4.5. Nothing in this section has been run on
it. Where you need a specific internal route, the honest procedure is to try it,
read logcat, and record what you observed on the release you observed it on —
not to inherit a snippet from a blog post written against Android 9.
:::

Once you have the loader, resolution looks like this, and it wants caching:

```cpp
// loader: a jobject that is a java.lang.ClassLoader for the app.
// name:   dotted binary name, e.g. "com.example.app.Thing"
static jclass load_app_class(JNIEnv *env, jobject loader, const char *name) {
    jclass cl_class = env->GetObjectClass(loader);              // local ref
    jmethodID load = env->GetMethodID(
        cl_class, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (load == nullptr) { env->ExceptionClear();
                           env->DeleteLocalRef(cl_class); return nullptr; }

    jstring jname = env->NewStringUTF(name);
    if (jname == nullptr) { env->ExceptionClear();
                            env->DeleteLocalRef(cl_class); return nullptr; }

    auto cls = static_cast<jclass>(env->CallObjectMethod(loader, load, jname));
    if (env->ExceptionCheck()) { env->ExceptionClear(); cls = nullptr; }

    env->DeleteLocalRef(jname);
    env->DeleteLocalRef(cl_class);
    return cls;   // local ref, or nullptr; caller promotes it if it keeps it
}
```

Three things in there are the chapter's real content: every lookup is checked,
every local reference is deleted on every path, and the returned reference is
local — the caller must promote it before storing it. The next two sections are
why.

## A reflection helper you will reuse

Reflection from native code degenerates into unreadable soup unless you decide
once what the shape is. This is that decision, and the rest of the book uses it.

Cache the `jclass` as a **global** reference, cache the method IDs alongside it,
resolve once, and centralise the failure handling so no call site invents its
own.

```cpp
struct JavaRef {
    jclass    cls  = nullptr;   // GLOBAL ref, or nullptr if not resolved
    jmethodID doIt = nullptr;

    // Resolve once. Safe to call again; returns the cached state.
    bool resolve(JNIEnv *env, jobject app_loader) {
        if (cls != nullptr) return true;

        jclass local = load_app_class(env, app_loader, "com.example.app.Thing");
        if (local == nullptr) return false;

        doIt = env->GetMethodID(local, "doIt", "(I)Ljava/lang/String;");
        if (doIt == nullptr) {
            env->ExceptionClear();          // NoSuchMethodError is pending
            env->DeleteLocalRef(local);
            return false;
        }

        cls = static_cast<jclass>(env->NewGlobalRef(local));
        env->DeleteLocalRef(local);         // the local is done either way
        return cls != nullptr;
    }

    void release(JNIEnv *env) {             // if you ever tear down
        if (cls != nullptr) { env->DeleteGlobalRef(cls); cls = nullptr; }
        doIt = nullptr;
    }
};
```

Two properties matter. A `jmethodID` is not a reference and does not need
promoting — but it is only valid while its class is alive, which is exactly what
the global ref on `cls` guarantees. And `GetMethodID` returning `nullptr` leaves
a `NoSuchMethodError` pending; clearing it there, at the site that caused it, is
the difference between a module that fails gracefully and one that detonates
three JNI calls later somewhere unrelated.

For strings, do not write new marshalling code. Chapter 9's
[`ScopedUtf`](/ZygiskLab/book/prespecialize/09-reading-appspecializeargs/) already
does the whole job — null check, pending-exception check, release on every path,
copies deleted — and it is as correct here as it was in the specialization
window.

## Local references, and the leak that will bite you

A local reference is freed when the native method returns to Java. Read that
again with an injected module in mind: **if you never return to Java, they are
never freed.** A worker thread that attaches once and then loops — polling,
periodically inspecting an object, calling a method every few hundred
milliseconds — is one continuous native context. Every `NewStringUTF`, every
`CallObjectMethod` returning an object, every `GetObjectClass`, every
`GetObjectArrayElement` adds an entry to a table nothing will ever pop.

The specification guarantees only that 16 local references can be created; past
that you are relying on the implementation. Android's own guidance is direct:
before Android 8.0 the count was capped at a version-specific limit, and from
8.0 the local reference table is effectively unbounded — which changes the
failure from a prompt abort into unbounded growth, and does not make the bug
acceptable. Under CheckJNI you get warned long before either. Under a release
runtime you get a slow leak in the host app, and the person who notices is a
user.

There are two correct tools, and you should reach for whichever fits the shape:

- **`DeleteLocalRef` as you go.** Right for a handful of references with clear
  lifetimes, as in `load_app_class` above. Delete it the moment you are done,
  not at the end of the function.
- **`PushLocalFrame(n)` / `PopLocalFrame(result)`.** Right for a loop body, or
  any block that creates an unknown number of references. Push a frame at the top
  of each iteration, do the work, pop at the bottom; every local created inside
  is discarded at once. `PopLocalFrame` takes one reference to carry out into the
  enclosing frame — pass `nullptr` if there is nothing to keep.

```cpp
while (running_) {
    if (env->PushLocalFrame(16) != JNI_OK) break;   // OOM; frame not pushed
    do_one_iteration(env);                          // makes as many locals as it likes
    env->PopLocalFrame(nullptr);                    // all of them, gone
    sleep_a_bit();
}
```

The rule that keeps you safe, stated so you can apply it without thinking: **any
reference you keep past the current block becomes a global; any reference you do
not keep gets deleted, and a loop gets a frame.** `NewGlobalRef` for the things
you cache — classes, the app classloader — matched one-for-one with
`DeleteGlobalRef`. Weak globals (`NewWeakGlobalRef`) exist for the case where you
want to observe an object without keeping it alive; they need
`IsSameObject(ref, nullptr)` to test for collection before use, and they are not
what you want for a cached `jclass` you have method IDs for.

## Exception discipline

After any JNI call that can throw, `ExceptionCheck`, and then either handle the
exception or `ExceptionClear` it. There is no third option and no call you can
sneak in first.

The reason is not stylistic. With an exception pending, almost every JNI function
is undefined. The short list you *may* call is the one Android documents:
`DeleteGlobalRef`, `DeleteLocalRef`, `DeleteWeakGlobalRef`, `ExceptionCheck`,
`ExceptionClear`, `ExceptionDescribe`, `ExceptionOccurred`, `MonitorExit`,
`PushLocalFrame`, `PopLocalFrame`, and the `Release*` family. Everything else —
including the innocuous-looking `NewStringUTF` you were about to use to build a
log message — is off limits until you clear.

Note also that a managed exception does not unwind your native frames. Nothing
stops your C++ from carrying on; the return value is simply garbage and the
pending exception is still sitting there. That is why "check the return value" is
not sufficient on its own for calls that can throw and legitimately return
`nullptr`.

In an injected module this matters more than in ordinary JNI, and it is worth
being blunt about why. Ordinary JNI code that leaves an exception pending crashes
its own app, in a build its own developer is debugging, with a stack trace that
points at their own native library. Your module crashes an app you did not write,
on a device you may not own, and the crash lands in the app's crash reporter
attributed to the app. The developer chasing it has no idea your code exists.
Getting this wrong is not a bug in your module; it is damage to somebody else's
product.

Two habits make it cheap. Check immediately after the call, at the call site,
never "later". And when you clear, log what you cleared — `ExceptionDescribe`
writes the trace to logcat and is safe with an exception pending — because a
silently swallowed `ClassNotFoundException` is exactly the symptom you will be
trying to diagnose in the classloader section above.

## Hidden-API restrictions

Since Android 9, apps are restricted from using non-SDK interfaces, and the
restriction is not limited to reflection. Android's documentation lists three
access paths as covered: bytecode references, reflection, and **JNI** — naming
`env->GetFieldID()` and `env->GetMethodID()` specifically. A restricted lookup
through JNI returns `NULL` and throws `NoSuchFieldError` or `NoSuchMethodError`,
which is indistinguishable, from your side, from the class simply not having that
member. If your careful `GetMethodID` check is firing on a framework internal
that you can see in the source, this is the first thing to suspect.

What follows is where the honesty has to be explicit. The enforcement model has
changed across releases — Android 9 introduced it, Android 10 added the
conditionally-blocked `max-target-x` lists, Android 11 pulled test APIs into the
blocklist, and each release since has moved entries between lists. Exemptions
exist for apps signed with the platform key and for system-image apps. **A
Zygisk module is not automatically exempt from anything.** You are running inside
an ordinary app's process, and the enforcement decision is made from the calling
context in that process, not from the privilege your module had ten milliseconds
earlier in zygote.

Whether a particular access from a particular calling context is permitted on
Android 16 — the reference rig — is not something this chapter will assert. The
behaviour depends on the release, on how the runtime determines the caller for a
native access, and on which list the specific member is on in that build. So
determine it empirically:

```bash
# Watch for hidden-API accesses as your module runs. On a debuggable app the
# runtime logs the member, the access kind and the list it is on.
adb logcat | grep -i "Accessing hidden"
```

A line of the shape `Accessing hidden method L…;->…` with `JNI` as the access
kind is the runtime telling you exactly which lookup tripped, and which list the
member sits on. That single log line settles for your device what no
generalisation can. Record the Android build alongside the result, because the
answer belongs to that build.

If you find yourself blocked, the honest first question is whether you need the
internal API at all — the object-derived classloader route earlier in this
chapter uses nothing but public `java.lang` methods and is unaffected by any of
this. Reaching for a bypass because an internal member is on a blocklist is out
of scope for this book, and it is usually the wrong engineering answer anyway:
an approach that depends on a specific internal member of a specific release is
one release from breaking regardless of whether it is enforced.

## The failure catalogue

What these mistakes look like from the outside, so you can recognise them before
you have read your own code twice:

| Symptom | Cause |
|---|---|
| `FindClass` works for framework classes, fails for app classes | Native thread, system classloader |
| Same helper works from a hook, fails from your worker | Calling context decides the loader |
| `ClassNotFoundException` from `loadClass` | Dotted name expected, not slashed |
| `GetMethodID` returns null on a real member | Hidden-API restriction, or a wrong signature |
| Crash several JNI calls after the real fault | Uncleared pending exception |
| Steady memory growth in the app over hours | Locals leaked in a native loop |
| App misbehaves on threads that are not yours | Detached an env you did not attach |
| Runtime complains at process teardown | Attached thread exited without detaching |

Everything in the first two rows and the last two is JNI specification behaviour
and will hold on any release. The classloader *routes*, and every statement about
hidden-API enforcement, belong to whichever Android build you measured them on.
Write down which one that was.

#include <android/log.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <time.h>
#include <cstdio>
#include <cstring>

#include "zygisk.hpp"

#define LOG_TAG "ZygiskLab"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

// --- Arming, reused from Lab 3 / Lab 4 -----------------------------------
//
// Same file, same contract, same failure-closed behaviour as
// 03-armed-once/jni/main.cpp and 04-plt-hook/jni/main.cpp: one line of
// plain text in target.txt, read through Api::getModuleDir(), matched
// exactly (not as a package-name prefix) against args->nice_name. See
// 03-armed-once/README.md for the full reasoning; it is not repeated here.
static constexpr const char *kConfigFile = "target.txt";
static constexpr size_t kConfigMax = 255;

static void readTarget(Api *api, char *out, size_t outSize) {
    out[0] = '\0';
    int dirFd = api->getModuleDir();
    if (dirFd < 0) {
        LOGW("getModuleDir failed; module will not arm");
        return;
    }
    int fd = openat(dirFd, kConfigFile, O_RDONLY);
    close(dirFd);
    if (fd < 0) {
        LOGW("could not open %s in module dir; module will not arm", kConfigFile);
        return;
    }
    ssize_t n = read(fd, out, outSize - 1);
    close(fd);
    if (n <= 0) {
        LOGW("%s is empty or unreadable; module will not arm", kConfigFile);
        out[0] = '\0';
        return;
    }
    if ((size_t) n == outSize - 1) {
        LOGW("%s is longer than %zu bytes and was truncated; module will not arm",
             kConfigFile, outSize - 1);
        out[0] = '\0';
        return;
    }
    out[n] = '\0';
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' ')) {
        out[--n] = '\0';
    }
}

// --- Proving which thread a callback ran on ------------------------------
//
// On Linux, a process's main thread's tid always equals the process's pid
// - that identity is set once, at the thread that calls fork() (or in
// Android's case, the thread zygote uses to fork), and holds for the
// lifetime of that thread. Any other thread - one this module spawns, one
// the runtime spawns, a binder thread - gets its own distinct tid. So
// `gettid() == getpid()` is a cheap, always-available, and honest way to
// answer "was this callback running on the process's original thread?"
// without trusting a comment or a framework promise. Every callback below
// logs both numbers, so the proof is in the log, not asserted in prose.
//
// This is necessary but not sufficient: being on the *OS* main thread
// (tid == pid) does not by itself mean Android's Java-level main Looper
// has been prepared yet, or that Application exists. Those are separate
// facts - see the README for what preAppSpecialize/postAppSpecialize can
// and cannot promise about them, and why this module reaches for a later,
// chosen moment instead of assuming "early" already means "ready".
static void logThread(const char *where) {
    pid_t pid = getpid();
    pid_t tid = gettid();
    LOGI("%s: pid=%d tid=%d gettid()==getpid() -> %s",
         where, pid, tid, (pid == tid) ? "true" : "false");
}

static double msSince(const struct timespec *start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - start->tv_sec) * 1000.0
         + (now.tv_nsec - start->tv_nsec) / 1e6;
}

// --- Reaching the main thread at a moment we choose ----------------------
//
// The problem: preAppSpecialize/postAppSpecialize run early - before this
// process's Java-level main Looper is necessarily prepared, and before
// Application exists - and this module must not block either call, since
// that would delay the app's own launch (Chapters 8 and 11's sin). So the
// action has to run *later*, off of postAppSpecialize's call stack, at a
// point this module picks rather than one it's handed.
//
// Three ways to get there were on the table:
//
//   1. Post a Runnable to the main Looper's Handler via JNI, once the
//      runtime is ready enough to make that call. This is the "obvious"
//      answer, but Handler.post(Runnable) needs a live Java object that
//      implements Runnable, and this module has no .dex/.jar to define
//      one in - jni-only, ndk-build only, no Gradle, per this lab's
//      constraints. Synthesizing bytecode at runtime to work around that
//      is possible in principle but is its own multi-page lab, not a
//      thread-and-timing one.
//   2. A native approach that observes the main thread's message loop
//      directly (e.g. parsing Looper's internals from native code). This
//      trades one guess (a hook target) for a worse one (Looper's memory
//      layout), and breaks across ART/Looper implementation changes more
//      easily than a documented JNI method name does.
//   3. Wait for a natural main-thread entry point the app itself will
//      reach, and do the work there - by hooking a `native` Java method
//      that the framework itself calls, on the main thread, as an
//      ordinary part of every app's startup. Api::hookJniNativeMethods()
//      already does exactly the kind of interception this needs, is
//      already used once in this file's Lab 3/4 lineage's sibling
//      (04-plt-hook uses the PLT equivalent), and needs nothing this
//      module doesn't already have.
//
// This module takes option 3: it hooks the static native method
// `android.os.Process.setArgV0(String)`.
//
// Why that method: android.os.Process.setArgV0() sets this process's
// argv[0] (what `ps` and /proc/<pid>/cmdline show), and framework source
// calls it from ActivityThread.handleBindApplication() - the method the
// main thread's Handler ("H") runs in response to the
// IApplicationThread.bindApplication() Binder callback from
// ActivityManagerService. That callback is what starts turning a freshly
// specialized, generic process into "this specific app": it runs on the
// real main thread, through the real main Looper, strictly after
// Looper.prepareMainLooper() has already executed in ActivityThread.main()
// - and setArgV0() is one of the first things handleBindApplication() does,
// ahead of Instrumentation and Application being created. That makes it a
// deliberately *early* natural main-thread checkpoint: later than "the OS
// thread that will become main" (which preAppSpecialize/postAppSpecialize
// already run on - see logThread() above) but earlier than the app having
// any object graph of its own for this module to disturb.
//
// IMPORTANT - this exact call site (handleBindApplication calling
// Process.setArgV0 on the main-thread Looper, ahead of Application
// creation) is asserted from framework source read while writing this
// module, not observed on a device. Nothing here has been run on real
// hardware. If a future Android version moves or removes that call, this
// hook simply never fires - see the failure handling below - and the
// README says so explicitly rather than claiming this is proven.
using SetArgV0Fn = void (*)(JNIEnv *, jclass, jstring);
static SetArgV0Fn orig_setArgV0 = nullptr;

// Timestamp captured in preAppSpecialize, right before the hook is
// installed, so every later log line can report how long after arming it
// landed. A plain struct (not a pointer into anything JNI-owned) so its
// lifetime is trivially the whole process.
static struct timespec armedAt;
static bool haveArmedAt = false;

// The main-thread action. This runs wherever ActivityThread's Handler
// dispatches handleBindApplication() - the real main thread, on the real
// main Looper, per the reasoning above. What it does is deliberately
// small, visible, and harmless: one log line proving gettid() == getpid()
// at the moment it runs, with a timestamp showing how long after arming
// it took to get here. It does not touch the Application, the Context, or
// any app state - env/clazz/name are passed straight through to the real
// setArgV0() unmodified, so this hook cannot change what the app sees.
static void my_setArgV0(JNIEnv *env, jclass clazz, jstring name) {
    logThread("main-thread action (Process.setArgV0 hook)");
    if (haveArmedAt) {
        LOGI("main-thread action: %.2fms after preAppSpecialize armed the hook",
             msSince(&armedAt));
    }
    // Call through unconditionally, with the original arguments, and
    // return exactly what it returns (void, here) - same discipline as
    // Lab 4's openat() hook: this module observes, it does not alter
    // behaviour. If orig_setArgV0 were somehow null here, this process's
    // argv[0]/cmdline would just silently fail to update; that can't
    // happen in practice because Android only ever calls a *hooked*
    // native method through the replacement hookJniNativeMethods()
    // installed, and installation only completes below once orig_setArgV0
    // was already captured.
    orig_setArgV0(env, clazz, name);
}

// Lab 5: thread and timing. Prove which thread every callback ran on, then
// reach the app's real main thread later, at a moment this module chose,
// without blocking app launch to get there.
class MainThread : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        const char *name = env->GetStringUTFChars(args->nice_name, nullptr);
        logThread("preAppSpecialize");

        char target[kConfigMax + 1];
        readTarget(api, target, sizeof(target));
        bool armed = target[0] != '\0' && strcmp(name, target) == 0;

        if (!armed) {
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
            LOGI("preAppSpecialize: pid=%d nice_name=%s not armed (target=%s), no hook installed",
                 getpid(), name, target[0] ? target : "(unset)");
            env->ReleaseStringUTFChars(args->nice_name, name);
            return;
        }

        // Install the hook here, in preAppSpecialize, for the same reason
        // Lab 4 registers/commits its PLT hook here rather than later:
        // this process still has zygote's privilege and hookJniNativeMethods
        // is documented against this call, and installing before
        // specialization means the hook is live for the whole rest of
        // startup - including the handleBindApplication() call this
        // module is waiting for - not just whatever's left of it after
        // postAppSpecialize returns.
        //
        // Installing the hook does not run any app code and does not
        // block - hookJniNativeMethods() only rewrites a JNI method table
        // entry. The actual main-thread work happens later, inside
        // my_setArgV0(), off of this call's stack entirely.
        clock_gettime(CLOCK_MONOTONIC, &armedAt);
        haveArmedAt = true;

        JNINativeMethod methods[] = {
            {"setArgV0", "(Ljava/lang/String;)V", (void *) my_setArgV0},
        };
        api->hookJniNativeMethods(env, "android/os/Process", methods, 1);
        orig_setArgV0 = (SetArgV0Fn) methods[0].fnPtr;

        // Handle failure honestly. hookJniNativeMethods() sets fnPtr to
        // null if the class, method name, or signature it was given
        // doesn't resolve - e.g. a future Android version renaming or
        // removing Process.setArgV0(). If that happens, my_setArgV0 can
        // never be called (Android still calls the real, un-replaced
        // method), so log that plainly instead of silently waiting
        // forever for a log line that will never come.
        if (orig_setArgV0 != nullptr) {
            LOGI("preAppSpecialize: pid=%d proc=%s ARMED, Process.setArgV0 hook installed",
                 getpid(), name);
        } else {
            LOGW("preAppSpecialize: pid=%d proc=%s ARMED, but hookJniNativeMethods "
                 "could not resolve android.os.Process.setArgV0 - main-thread action "
                 "will NOT run", getpid(), name);
        }

        env->ReleaseStringUTFChars(args->nice_name, name);
    }

    void postAppSpecialize(const AppSpecializeArgs *args) override {
        const char *name = env->GetStringUTFChars(args->nice_name, nullptr);
        logThread("postAppSpecialize");
        if (haveArmedAt) {
            LOGI("postAppSpecialize: pid=%d nice_name=%s getuid=%d, %.2fms after arming - "
                 "still waiting for the main-thread action to fire",
                 getpid(), name, getuid(), msSince(&armedAt));
        } else {
            LOGI("postAppSpecialize: pid=%d nice_name=%s getuid=%d", getpid(), name, getuid());
        }
        // Deliberately does nothing further here. This method still runs
        // on the critical path of app startup - whatever it does, the app
        // waits for - so the main-thread action is not attempted here. It
        // was already scheduled, for later, by installing the hook above
        // in preAppSpecialize; this method returns immediately either way.
        env->ReleaseStringUTFChars(args->nice_name, name);
    }

private:
    Api *api = nullptr;
    JNIEnv *env = nullptr;
};

REGISTER_ZYGISK_MODULE(MainThread)

#include <android/log.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>

#include "zygisk.hpp"

#define LOG_TAG "ZygiskLab"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

// --- Arming, reused from Lab 3 ------------------------------------------
//
// Same file, same contract, same failure-closed behaviour as
// 03-armed-once/jni/main.cpp: one line of plain text in target.txt, read
// through Api::getModuleDir(), matched exactly (not as a package-name
// prefix) against args->nice_name. See that module's README for the full
// reasoning; it is not repeated here.
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

// --- Finding libc.so in this process's own memory map -------------------
//
// pltHookRegister() takes a (dev, inode) pair identifying one mapped ELF,
// not a path - the same file can be mapped from different paths (bind
// mounts, APEX) so the kernel's device/inode identity is what actually
// disambiguates it. /proc/self/maps is the only place that pairing is
// published, so we parse it once, at hook-install time, looking for the
// mapping whose path ends in "/libc.so".
//
// This runs in preAppSpecialize, before this process has any sandbox
// restrictions - /proc/self/maps is readable there the same way it would
// be readable by zygote itself, and libc is already mapped because zygote
// (the process this one was just forked from) linked against it long
// before this module ever loaded.
static bool findTargetLib(dev_t *devOut, ino_t *inodeOut) {
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) {
        LOGW("could not open /proc/self/maps");
        return false;
    }
    char line[512];
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        unsigned long devMajor, devMinor;
        unsigned long long inode;
        int pathOffset = 0;
        // Field layout, per proc(5):
        //   addr-addr perms offset major:minor inode  path
        if (sscanf(line, "%*x-%*x %*4s %*x %lx:%lx %llu %n",
                   &devMajor, &devMinor, &inode, &pathOffset) != 3) {
            continue;
        }
        if (pathOffset <= 0) continue;
        char *path = line + pathOffset;
        // Trim the trailing newline fgets leaves in place.
        size_t len = strlen(path);
        while (len > 0 && (path[len - 1] == '\n' || path[len - 1] == '\r')) {
            path[--len] = '\0';
        }
        if (len == 0) continue;
        static constexpr const char *kSuffix = "/libandroid_runtime.so";
        size_t suffixLen = strlen(kSuffix);
        if (len >= suffixLen && strcmp(path + len - suffixLen, kSuffix) == 0) {
            *devOut = makedev((unsigned) devMajor, (unsigned) devMinor);
            *inodeOut = (ino_t) inode;
            found = true;
            break;
        }
    }
    fclose(f);
    return found;
}

// --- The hook -------------------------------------------------------------
//
// Symbol: openat(2). Two reasons, not one:
//
// 1. Coverage. Bionic implements the plain open(2) libc wrapper itself as
//    a call to openat() with AT_FDCWD, and every higher-level API that
//    eventually opens a file - fopen(), the classloader reading a dex or
//    jar, SQLite opening a database, a resource lookup - bottoms out at
//    openat() on Android. Hooking one symbol here observes file opens
//    app-wide, not just the ones written as literal open() calls.
// 2. Safety and cost. openat() is a thin syscall wrapper: the replacement
//    below does a bounded amount of work (a counter check and, rarely, one
//    log line) and then tail-calls the original with its arguments passed
//    through unchanged. It cannot change which file gets opened, with what
//    flags, or what open() returns - this module observes, it does not
//    alter behaviour.
//
// Why open() and not openat(), which looks like the better target: a PLT
// hook can only rewrite a call that crosses a PLT, and openat() is almost
// never called that way. Bionic implements open() as a call to openat()
// *inside libc*, so that call never crosses a PLT boundary and is
// invisible to this technique. Measured on the reference rig,
// libandroid_runtime.so, libutils.so and libbase.so contain zero
// relocations for openat; only libc++.so has one, and it is not exercised
// during an ordinary app launch. Registering openat against libc.so is
// worse still: libc *defines* the symbol rather than importing it, so
// there is no PLT entry to rewrite and pltHookCommit() returns false.
//
// open() is imported by libandroid_runtime.so, which practically every app
// process loads - 125 of 126 app-uid processes on the reference rig had it
// mapped - and which opens the app's APKs during startup, so the hook has
// something real to intercept. "Practically every" is the honest phrasing:
// one process in that sample did not, so do not assume it without checking
// the process you actually care about. See chapter 14.
using OpenFn = int (*)(const char *, int, mode_t);
static OpenFn orig_open = nullptr;

// Set once, from preAppSpecialize, before the hook can possibly fire -
// the hook only starts intercepting calls after pltHookCommit() returns,
// and armedProcessName is written before that call. A plain array (not a
// pointer into a JNI string) so the hook body never has to worry about the
// JNIEnv or the string's lifetime.
static char armedProcessName[256] = "?";

// Re-entrancy guard. __android_log_print ultimately talks to logd over a
// socket on current Android versions, not through openat(), so in practice
// logging from inside this hook does not recurse into itself. But "in
// practice, on this OS version" is not a guarantee the header gives us,
// and a future liblog backend (or a different logging call added here
// later) that opens a file would turn one intercepted call into infinite
// recursion. A thread_local depth guard makes that failure mode impossible
// regardless of what logging ends up doing under the hood, at the cost of
// one thread-local increment/decrement per call.
static thread_local int reentryDepth = 0;

// Log volume cap. An app can open hundreds of files a second (asset
// packs, SQLite, classes.dex) - logging every single one unboundedly is
// exactly the flood this lab warns against, and on some devices a fast
// enough logcat writer can visibly stall the app it's injected into. Cap
// the number of calls this process will ever log; every call past the cap
// still runs (and still calls through to the real openat()), it just stops
// producing a log line, plus one line announcing the cap was hit.
static constexpr int kLogCap = 20;
static std::atomic<int> loggedCalls{0};

// A note on this signature. openat() is variadic - `int openat(int, const
// char *, int, ...)` - and the mode argument is only actually passed when
// the caller sets O_CREAT or O_TMPFILE. Declaring the replacement with a
// fixed fourth parameter means that for a three-argument call we read
// whatever happens to be in the register the fourth argument would have
// occupied, which is an unspecified value.
//
// That is safe *here*, and only because of what this function does with it:
// the value is passed straight back to the real openat(), which ignores the
// mode entirely unless O_CREAT or O_TMPFILE is set - and in exactly those
// cases the caller did pass a real mode, so the value is the caller's own.
// The garbage is only ever read in the cases where it is also ignored.
//
// It would not be safe if the replacement inspected, logged or acted on
// `mode`. If you adapt this hook and start caring about that argument, read
// it out of a va_list properly instead of taking it as a fixed parameter.
static int my_open(const char *path, int flags, mode_t mode) {
    if (reentryDepth == 0) {
        int seen = loggedCalls.fetch_add(1, std::memory_order_relaxed);
        if (seen < kLogCap) {
            reentryDepth++;
            LOGI("open: pid=%d proc=%s path=%s flags=0x%x [%d/%d logged]",
                 getpid(), armedProcessName, path ? path : "(null)", flags,
                 seen + 1, kLogCap);
            reentryDepth--;
        } else if (seen == kLogCap) {
            reentryDepth++;
            LOGI("open: proc=%s further calls suppressed after %d logged calls",
                 armedProcessName, kLogCap);
            reentryDepth--;
        }
    }
    // Call through unconditionally, with the original arguments, and
    // return exactly what it returns. This is the part that makes the
    // hook an observer instead of a behaviour change.
    return orig_open(path, flags, mode);
}

// Lab 4: hook a libc symbol in one target app, log it, and hand back an
// unmodified return value - then prove, by running an unhooked control
// process alongside it, that the hook is scoped to where it was installed.
class PltHook : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        const char *name = env->GetStringUTFChars(args->nice_name, nullptr);

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

        // Stash the name for the hook body before the hook can possibly
        // fire (i.e. before pltHookCommit() below).
        strncpy(armedProcessName, name, sizeof(armedProcessName) - 1);
        armedProcessName[sizeof(armedProcessName) - 1] = '\0';

        // Lifecycle: both calls happen here, in preAppSpecialize, and
        // nowhere else.
        //
        // pltHookRegister() only records an (dev, inode, symbol) ->
        // newFunc entry; it does not touch memory permissions. The header
        // documents it purely in terms of "ELFs loaded in memory" - it
        // says nothing about being callable post-specialization, but by
        // that point the target library set for this process is already
        // fixed and there is no reason to defer the registration.
        // pltHookCommit() is what actually rewrites the PLT/GOT entries
        // for every hook registered so far, and the header's own example
        // performs the equivalent JNI-method hook in preAppSpecialize -
        // before the process picks up its sandbox restrictions, while it
        // still has zygote's privilege. Registering and committing here,
        // rather than in postAppSpecialize, also means the hook is live
        // for the entire rest of app startup, not just the tail end of it
        // after specialization finishes - so calls made early during
        // Application/attachBaseContext are covered too.
        dev_t dev;
        ino_t inode;
        if (!findTargetLib(&dev, &inode)) {
            LOGW("preAppSpecialize: pid=%d proc=%s could not locate libandroid_runtime.so "
                 "in /proc/self/maps; hook NOT installed", getpid(), armedProcessName);
            env->ReleaseStringUTFChars(args->nice_name, name);
            return;
        }

        api->pltHookRegister(dev, inode, "open", (void *) my_open,
                              (void **) &orig_open);

        // Check and report the result. pltHookCommit() returns bool, and
        // silently ignoring "false" here is the single most confusing way
        // this lab can fail for a reader: the module loads, arms, logs
        // that it armed - and then nothing about openat() ever appears,
        // because the commit failed and orig_openat is still null. Log
        // success or failure explicitly instead of assuming it worked.
        bool committed = api->pltHookCommit();
        if (committed) {
            LOGI("preAppSpecialize: pid=%d proc=%s ARMED, open() hook committed",
                 getpid(), armedProcessName);
        } else {
            LOGW("preAppSpecialize: pid=%d proc=%s ARMED, but pltHookCommit() "
                 "FAILED - open() is NOT hooked in this process", getpid(),
                 armedProcessName);
        }

        env->ReleaseStringUTFChars(args->nice_name, name);
    }

    void postAppSpecialize(const AppSpecializeArgs *args) override {
        const char *name = env->GetStringUTFChars(args->nice_name, nullptr);
        LOGI("postAppSpecialize: pid=%d nice_name=%s getuid=%d", getpid(), name, getuid());
        env->ReleaseStringUTFChars(args->nice_name, name);
    }

private:
    Api *api = nullptr;
    JNIEnv *env = nullptr;
};

REGISTER_ZYGISK_MODULE(PltHook)

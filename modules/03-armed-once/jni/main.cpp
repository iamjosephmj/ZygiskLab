#include <android/log.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <time.h>
#include <cstring>
#include <cstdio>

#include "zygisk.hpp"

#define LOG_TAG "ZygiskLab"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

// The config file name, read relative to the module directory fd that
// Api::getModuleDir() hands back. One line of plain text: the exact
// nice_name (see the note on package vs. process name below) this module
// arms for. Push it with:
//   adb shell su -M -c "echo -n com.android.chrome > \
//     /data/adb/modules/zygisklab_armed/target.txt"
static constexpr const char *kConfigFile = "target.txt";
static constexpr size_t kConfigMax = 255;

// Read the target process name out of the module directory. Only callable
// from pre[XXX]Specialize, same restriction as getModuleDir() itself. On
// any failure (missing file, unreadable, empty) this returns an empty
// string, which cannot equal a real nice_name, so the module simply never
// arms rather than arming for the wrong thing.
static void readTarget(Api *api, char *out, size_t outSize) {
    out[0] = '\0';
    int dirFd = api->getModuleDir();
    if (dirFd < 0) {
        LOGW("getModuleDir failed; module will not arm");
        return;
    }
    int fd = openat(dirFd, kConfigFile, O_RDONLY);
    // Close the module-dir fd as soon as openat has used it. The header does
    // not state who owns this descriptor, so we take ownership: leaking one
    // per app launch would be a descriptor pointing at the module directory,
    // open in every process the provider injects into - which is exactly the
    // kind of trace Chapter 21 teaches you to look for.
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
        // Filled the buffer exactly: the target may have been truncated. A
        // truncated target simply never matches, which looks identical to a
        // correctly configured module that is armed for a process you are not
        // launching. Fail loudly instead.
        LOGW("%s is longer than %zu bytes and was truncated; module will not arm",
             kConfigFile, outSize - 1);
        out[0] = '\0';
        return;
    }
    out[n] = '\0';
    // Trim a trailing newline and any trailing whitespace a text editor
    // left behind, so "echo com.android.chrome > target.txt" (which adds
    // "\n") works the same as "echo -n ...".
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' ')) {
        out[--n] = '\0';
    }
}

// Lab 3: arm for exactly one process, and make the cost of not being that
// process visible.
//
// Every app the provider injects into runs preAppSpecialize and
// postAppSpecialize, whether this module cares about that app or not. The
// module's job in the common case - not our target - is to notice that as
// fast as possible, do nothing else, and get out of the process entirely.
class ArmedOnce : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        // Timestamp first, before touching JNI or the filesystem, so the
        // measurement below covers the module's full cost for this
        // callback - not just the part after some other setup work.
        struct timespec t0;
        clock_gettime(CLOCK_MONOTONIC, &t0);

        const char *name = env->GetStringUTFChars(args->nice_name, nullptr);

        char target[kConfigMax + 1];
        readTarget(api, target, sizeof(target));

        // Package name vs. process name: args->nice_name is the process
        // name, not the package name, and for a multi-process app they
        // differ - a background service can run as "com.example.app:sync".
        // This module matches nice_name EXACTLY against the configured
        // target rather than treating the target as a package-name prefix.
        // That is a deliberate, narrower choice: it arms only the process
        // named in target.txt (typically an app's main process) and stays
        // unarmed - and unloaded - in that same app's other processes. The
        // alternative, prefix-matching "com.example.app" against
        // "com.example.app:sync", is sometimes the right call (you want
        // every process of the app), but it has to be done as a real
        // token boundary check (name == target, or name starts with
        // "target:") - a naive strncmp prefix also matches unrelated
        // packages like "com.example.app2". Chapter 9 works through both
        // policies; this module embodies the exact-match one because it is
        // the one that cannot accidentally over-arm.
        bool armed = target[0] != '\0' && strcmp(name, target) == 0;

        if (!armed) {
            // Not our process. Do the least possible: we already read the
            // name and the config (that cost is real and is what the
            // measurement below reports), so now unload and return.
            // DLCLOSE_MODULE_LIBRARY must be set before we do anything
            // that hooks or otherwise depends on this library staying
            // mapped - we haven't, so it's safe here.
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);

            struct timespec t1;
            clock_gettime(CLOCK_MONOTONIC, &t1);
            long deltaUs = (t1.tv_sec - t0.tv_sec) * 1000000L +
                           (t1.tv_nsec - t0.tv_nsec) / 1000L;
            LOGI("preAppSpecialize: pid=%d nice_name=%s not armed (target=%s), "
                 "unarmed path cost=%ldus, dlclose requested",
                 getpid(), name, target[0] ? target : "(unset)", deltaUs);

            env->ReleaseStringUTFChars(args->nice_name, name);
            return;
        }

        // Our process. Log enough to prove which process this is and that
        // we are about to cross the specialization boundary. getuid()
        // here is zygote's identity (root, uid 0) - the process has not
        // specialized yet. args->uid is not a live read of anything; it is
        // the uid this process is *about to become*. postAppSpecialize
        // logs getuid() again so the transition itself is on the record.
        LOGI("preAppSpecialize: pid=%d nice_name=%s ARMED getuid=%d (current identity) "
             "args->uid=%d (destination)",
             getpid(), name, getuid(), args->uid);

        env->ReleaseStringUTFChars(args->nice_name, name);
    }

    void postAppSpecialize(const AppSpecializeArgs *args) override {
        // Only the armed process gets this far still caring - an unarmed
        // process already dlclose-d its way out of here before
        // postAppSpecialize runs, per DLCLOSE_MODULE_LIBRARY's contract.
        const char *name = env->GetStringUTFChars(args->nice_name, nullptr);
        LOGI("postAppSpecialize: pid=%d nice_name=%s getuid=%d", getpid(), name, getuid());
        env->ReleaseStringUTFChars(args->nice_name, name);
    }

private:
    Api *api = nullptr;
    JNIEnv *env = nullptr;
};

REGISTER_ZYGISK_MODULE(ArmedOnce)

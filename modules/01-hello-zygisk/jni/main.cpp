#include <android/log.h>
#include <unistd.h>
#include <sys/types.h>

#include "zygisk.hpp"

#define LOG_TAG "ZygiskLab"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

// Lab 1: the smallest module that proves it loaded, and proves *where*.
class HelloZygisk : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
        LOGI("onLoad: module loaded into pid=%d", getpid());
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        // Still inside the zygote fork. The process is not an app yet, so
        // getuid() here reports the *current* identity (zygote's, i.e. root,
        // uid 0), not the app's. args->uid is a different thing entirely:
        // it is the specialization *argument* — the uid the process is
        // about to become — not a live read of the process's own state.
        // Logging both makes the distinction explicit: getuid() is where we
        // are, args->uid is where we're headed.
        const char *name = env->GetStringUTFChars(args->nice_name, nullptr);
        LOGI("preAppSpecialize: pid=%d getuid=%d (current identity) args->uid=%d (destination) nice_name=%s",
             getpid(), getuid(), args->uid, name);
        env->ReleaseStringUTFChars(args->nice_name, name);
    }

    void postAppSpecialize(const AppSpecializeArgs *) override {
        // We are the app now. getuid() has moved from 0 (root, in
        // preAppSpecialize) to the app's own uid — the same value
        // preAppSpecialize already told us to expect via args->uid. That
        // getuid() transition, not args->uid, is the actual proof that we
        // crossed the specialization boundary into a real app process.
        LOGI("postAppSpecialize: pid=%d getuid=%d", getpid(), getuid());
    }

private:
    Api *api = nullptr;
    JNIEnv *env = nullptr;
};

REGISTER_ZYGISK_MODULE(HelloZygisk)

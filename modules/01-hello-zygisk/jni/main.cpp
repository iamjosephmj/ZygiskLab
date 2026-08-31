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
        // Still inside the zygote fork. The process is not an app yet.
        const char *name = env->GetStringUTFChars(args->nice_name, nullptr);
        LOGI("preAppSpecialize: pid=%d uid=%d nice_name=%s", getpid(), args->uid, name);
        env->ReleaseStringUTFChars(args->nice_name, name);
    }

    void postAppSpecialize(const AppSpecializeArgs *) override {
        // We are the app now. Compare this pid/uid with the line above.
        LOGI("postAppSpecialize: pid=%d uid=%d", getpid(), getuid());
    }

private:
    Api *api = nullptr;
    JNIEnv *env = nullptr;
};

REGISTER_ZYGISK_MODULE(HelloZygisk)

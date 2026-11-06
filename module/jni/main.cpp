#include "zygisk.hpp"
#include "hooks.h"

#include <android/log.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <string>
#include <vector>

#define LOG_TAG "GLTools"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

static GameConfig g_appConfig;

class GLToolsModule : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        if (!args || !args->nice_name) {
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
            return;
        }
        const char *processName = env->GetStringUTFChars(args->nice_name, nullptr);
        if (!processName) {
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        std::string pkg = processName;
        env->ReleaseStringUTFChars(args->nice_name, processName);

        int fd = api->connectCompanion();
        if (fd >= 0) {
            uint32_t len = static_cast<uint32_t>(pkg.length());
            ssize_t w1 = write(fd, &len, sizeof(len));
            ssize_t w2 = write(fd, pkg.data(), len);
            (void)w1;
            (void)w2;

            loadAppConfigFromCompanion(fd, g_appConfig);
            close(fd);
        }

        if (!g_appConfig.enabled) {
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
        }
    }

    void postAppSpecialize([[maybe_unused]] const zygisk::AppSpecializeArgs *args) override {
        if (g_appConfig.enabled) {
            initHooks(g_appConfig, api, env);
        }
    }

    void preServerSpecialize([[maybe_unused]] zygisk::ServerSpecializeArgs *args) override {
        api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
    }

private:
    zygisk::Api *api = nullptr;
    JNIEnv *env = nullptr;
};

static void gltoolsCompanionHandler(int client) {
    uint32_t len = 0;
    if (read(client, &len, sizeof(len)) != sizeof(len) || len == 0 || len > 256) {
        close(client);
        return;
    }

    std::vector<char> pkgBuf(len + 1, 0);
    if (read(client, pkgBuf.data(), len) != static_cast<ssize_t>(len)) {
        close(client);
        return;
    }

    std::string pkg(pkgBuf.data());
    std::string confPath = "/data/adb/gltools/packages/" + pkg + ".conf";
    int fd = open(confPath.c_str(), O_RDONLY);
    if (fd >= 0) {
        char buf[4096];
        ssize_t n;
        while ((n = read(fd, buf, sizeof(buf))) > 0) {
            ssize_t w = write(client, buf, static_cast<size_t>(n));
            (void)w;
        }
        close(fd);
    }
    close(client);
}

REGISTER_ZYGISK_MODULE(GLToolsModule)
REGISTER_ZYGISK_COMPANION(gltoolsCompanionHandler)

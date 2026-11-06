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
        if (!args || !args->nice_name) return;
        const char *processName = env->GetStringUTFChars(args->nice_name, nullptr);
        if (!processName) return;

        std::string pkg = processName;
        env->ReleaseStringUTFChars(args->nice_name, processName);

        // Fetch configuration from root companion daemon
        int fd = api->connectCompanion();
        if (fd >= 0) {
            uint32_t len = pkg.length();
            write(fd, &len, sizeof(len));
            write(fd, pkg.data(), len);

            loadAppConfigFromCompanion(fd, g_appConfig);
            close(fd);
        }
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *args) override {
        if (g_appConfig.enabled) {
            initHooks(g_appConfig, env);
        }
    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs *args) override {
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
            write(client, buf, n);
        }
        close(fd);
    }
    close(client);
}

REGISTER_ZYGISK_MODULE(GLToolsModule)
REGISTER_ZYGISK_COMPANION(gltoolsCompanionHandler)

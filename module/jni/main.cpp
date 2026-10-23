#include "zygisk.hpp"
#include "hooks.h"
#include <android/log.h>
#include <string>

#define LOG_TAG "ZygiskGLTools"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

class GlToolsZygiskModule {
public:
    void preAppSpecialize(zygisk::AppSpecializeArgs *args) {
        // Read target application nice_name
        if (!args || !args->nice_name) return;
        // JNI strings conversion can be deferred to postAppSpecialize
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *args) {
        if (!args || !args->nice_name) return;

        // Retrieve package name from /proc/self/cmdline
        char cmdline[256] = {0};
        FILE *fp = fopen("/proc/self/cmdline", "r");
        if (fp) {
            fread(cmdline, 1, sizeof(cmdline) - 1, fp);
            fclose(fp);
        }

        if (cmdline[0] == '\0') return;

        GlToolsConfig config;
        if (loadAppConfig(cmdline, config)) {
            LOGI("Found active GLTools configuration for %s! Hooking...", cmdline);
            installGlHooks(m_api, config);
        }
    }

    void setApi(zygisk::Api *api) { m_api = api; }

private:
    zygisk::Api *m_api = nullptr;
};

static GlToolsZygiskModule g_moduleInstance;

extern "C" [[gnu::visibility("default")]]
void zygisk_module_entry(zygisk::Api *api, JNIEnv *env) {
    g_moduleInstance.setApi(api);
    static zygisk::ModuleAbi abi = {
        .api_version = ZYGISK_API_VERSION,
        .impl = reinterpret_cast<zygisk::ModuleImpl *>(&g_moduleInstance),
        .preAppSpecialize = [](zygisk::ModuleImpl *m, zygisk::AppSpecializeArgs *args) {
            reinterpret_cast<GlToolsZygiskModule *>(m)->preAppSpecialize(args);
        },
        .postAppSpecialize = [](zygisk::ModuleImpl *m, const zygisk::AppSpecializeArgs *args) {
            reinterpret_cast<GlToolsZygiskModule *>(m)->postAppSpecialize(args);
        },
        .preServerSpecialize = nullptr,
        .postServerSpecialize = nullptr,
    };
    api->registerModule(&abi);
}

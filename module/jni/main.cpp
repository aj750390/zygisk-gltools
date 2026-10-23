#include "zygisk.hpp"
#include "hooks.h"
#include <android/log.h>
#include <string>

#define LOG_TAG "ZygiskGLTools"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

class GlToolsZygiskModule {
public:
    void preAppSpecialize(zygisk::AppSpecializeArgs *args) {
        m_shouldHook = false;
        if (!args || !m_env) return;

        std::string packageName;

        if (args->nice_name) {
            const char *rawNice = m_env->GetStringUTFChars(args->nice_name, nullptr);
            if (rawNice) {
                packageName = rawNice;
                m_env->ReleaseStringUTFChars(args->nice_name, rawNice);
            }
        }

        if (packageName.empty() && args->app_data_dir) {
            const char *rawDir = m_env->GetStringUTFChars(args->app_data_dir, nullptr);
            if (rawDir) {
                std::string path = rawDir;
                m_env->ReleaseStringUTFChars(args->app_data_dir, rawDir);
                size_t pos = path.rfind('/');
                if (pos != std::string::npos && pos + 1 < path.length()) {
                    packageName = path.substr(pos + 1);
                }
            }
        }

        if (packageName.empty()) return;

        // Reads config as root before privilege drop
        if (loadAppConfig(packageName.c_str(), m_config)) {
            LOGI("Target matched in preAppSpecialize: %s (Renderer: %s)", packageName.c_str(), m_config.renderer.c_str());
            m_shouldHook = true;
        }
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *args) {
        if (!m_shouldHook) return;
        installGlHooks(m_api, m_config);
        spoofJavaBuildProperties(m_env, m_config);
    }

    void setContext(zygisk::Api *api, JNIEnv *env) {
        m_api = api;
        m_env = env;
    }

private:
    zygisk::Api *m_api = nullptr;
    JNIEnv *m_env = nullptr;
    GlToolsConfig m_config;
    bool m_shouldHook = false;
};

static GlToolsZygiskModule g_moduleInstance;

extern "C" [[gnu::visibility("default")]]
void zygisk_module_entry(zygisk::Api *api, JNIEnv *env) {
    g_moduleInstance.setContext(api, env);
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

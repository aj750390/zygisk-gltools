#include "zygisk.hpp"
#include "hooks.h"
#include <android/log.h>
#include <string>

#define LOG_TAG "ZygiskGLTools"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

class GlToolsZygiskModule : public zygisk::ModuleBase {
public:
    void onLoad(zygisk::Api *api, JNIEnv *env) override {
        this->m_api = api;
        this->m_env = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        m_shouldHook = false;
        if (!args || !m_env || !m_api) return;

        std::string packageName;

        // 1. Read package name from nice_name
        if (args->nice_name) {
            const char *rawNice = m_env->GetStringUTFChars(args->nice_name, nullptr);
            if (rawNice) {
                packageName = rawNice;
                m_env->ReleaseStringUTFChars(args->nice_name, rawNice);
            }
        }

        // 2. Fallback to app_data_dir (/data/user/0/<packageName>)
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

        // Strip process suffix if any (e.g. com.example.game:unity -> com.example.game)
        size_t colonPos = packageName.find(':');
        if (colonPos != std::string::npos) {
            packageName = packageName.substr(0, colonPos);
        }

        if (!packageName.empty() && loadAppConfig(packageName.c_str(), m_config)) {
            LOGI("Active profile matched for %s (Renderer: %s, Model: %s)",
                 packageName.c_str(), m_config.renderer.c_str(), m_config.model.c_str());
            m_shouldHook = true;
        } else {
            // Unload module from non-target apps to save memory and prevent interference
            m_api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
        }
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *args) override {
        if (!m_shouldHook) return;
        installGlHooks(m_api, m_config);
        spoofJavaBuildProperties(m_env, m_config);
    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs *args) override {
        // Never inject into system_server; unload immediately
        if (m_api) {
            m_api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
        }
    }

private:
    zygisk::Api *m_api = nullptr;
    JNIEnv *m_env = nullptr;
    GlToolsConfig m_config;
    bool m_shouldHook = false;
};

REGISTER_ZYGISK_MODULE(GlToolsZygiskModule)

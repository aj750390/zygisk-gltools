#include "zygisk.hpp"
#include "hooks.h"
#include <android/log.h>
#include <string>
#include <vector>
#include <unistd.h>

#define LOG_TAG "ZygiskGLTools"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

static bool writeFull(int fd, const void *buf, size_t count) {
    const uint8_t *p = static_cast<const uint8_t *>(buf);
    size_t remaining = count;
    while (remaining > 0) {
        ssize_t n = write(fd, p, remaining);
        if (n <= 0) return false;
        p += n;
        remaining -= n;
    }
    return true;
}

static bool readFull(int fd, void *buf, size_t count) {
    uint8_t *p = static_cast<uint8_t *>(buf);
    size_t remaining = count;
    while (remaining > 0) {
        ssize_t n = read(fd, p, remaining);
        if (n <= 0) return false;
        p += n;
        remaining -= n;
    }
    return true;
}

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

        // Strip process suffix (e.g., com.example.game:unity -> com.example.game)
        size_t colonPos = packageName.find(':');
        if (colonPos != std::string::npos) {
            packageName = packageName.substr(0, colonPos);
        }

        if (packageName.empty()) {
            m_api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        std::string rawConfig;

        // 1. Query Root Companion Daemon over Unix socket (bypasses all SELinux restrictions on /data/adb)
        int fd = m_api->connectCompanion();
        if (fd >= 0) {
            uint32_t pkgLen = static_cast<uint32_t>(packageName.size());
            if (writeFull(fd, &pkgLen, sizeof(pkgLen)) && writeFull(fd, packageName.data(), pkgLen)) {
                uint32_t cfgLen = 0;
                if (readFull(fd, &cfgLen, sizeof(cfgLen)) && cfgLen > 0 && cfgLen < 65536) {
                    std::vector<char> buf(cfgLen);
                    if (readFull(fd, buf.data(), cfgLen)) {
                        rawConfig.assign(buf.data(), cfgLen);
                    }
                }
            }
            close(fd);
        }

        // 2. Fallback to direct file read
        if (rawConfig.empty()) {
            loadAppConfigFromFile(packageName.c_str(), rawConfig);
        }

        if (!rawConfig.empty() && parseConfigContent(rawConfig, packageName.c_str(), m_config)) {
            LOGI("Active profile loaded for %s [GLES: %s | GPU: %s | Model: %s]",
                 packageName.c_str(), m_config.version.c_str(), m_config.renderer.c_str(), m_config.model.c_str());
            m_shouldHook = true;
        } else {
            m_api->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
        }
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *args) override {
        if (!m_shouldHook) return;
        installGlHooks(m_api, m_env, m_config);
        spoofJavaBuildProperties(m_env, m_config);
    }

    void preServerSpecialize(zygisk::ServerSpecializeArgs *args) override {
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

// Root companion handler running in superuser context (u:r:magisk:s0)
static void gltoolsCompanionHandler(int client) {
    uint32_t pkgLen = 0;
    if (!readFull(client, &pkgLen, sizeof(pkgLen)) || pkgLen == 0 || pkgLen > 512) {
        return;
    }
    std::vector<char> pkgBuf(pkgLen + 1, 0);
    if (!readFull(client, pkgBuf.data(), pkgLen)) {
        return;
    }

    std::string rawContent;
    loadAppConfigFromFile(pkgBuf.data(), rawContent);

    uint32_t cfgLen = static_cast<uint32_t>(rawContent.size());
    writeFull(client, &cfgLen, sizeof(cfgLen));
    if (cfgLen > 0) {
        writeFull(client, rawContent.data(), cfgLen);
    }
}

REGISTER_ZYGISK_MODULE(GlToolsZygiskModule)
REGISTER_ZYGISK_COMPANION(gltoolsCompanionHandler)

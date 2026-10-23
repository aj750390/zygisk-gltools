#include "hooks.h"
#include <android/log.h>
#include <dlfcn.h>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <chrono>
#include <thread>

#define LOG_TAG "ZygiskGLTools"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static GlToolsConfig g_config;
static const GLubyte *(*orig_glGetString)(GLenum name) = nullptr;
static const GLubyte *(*orig_glGetStringi)(GLenum name, GLuint index) = nullptr;
static void (*orig_glViewport)(GLint x, GLint y, GLsizei width, GLsizei height) = nullptr;

// Custom glGetString hook
static const GLubyte *hooked_glGetString(GLenum name) {
    if (!g_config.enabled) {
        return orig_glGetString ? orig_glGetString(name) : nullptr;
    }

    switch (name) {
        case GL_RENDERER:
            if (!g_config.renderer.empty()) {
                return reinterpret_cast<const GLubyte *>(g_config.renderer.c_str());
            }
            break;
        case GL_VENDOR:
            if (!g_config.vendor.empty()) {
                return reinterpret_cast<const GLubyte *>(g_config.vendor.c_str());
            }
            break;
        case GL_VERSION:
            if (!g_config.version.empty()) {
                return reinterpret_cast<const GLubyte *>(g_config.version.c_str());
            }
            break;
        case GL_EXTENSIONS:
            if (!g_config.extensions.empty()) {
                return reinterpret_cast<const GLubyte *>(g_config.extensions.c_str());
            }
            break;
        default:
            break;
    }

    return orig_glGetString ? orig_glGetString(name) : nullptr;
}

// Custom glViewport hook for resolution downscaling
static void hooked_glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    if (g_config.enabled && g_config.downscaleFactor > 0.0f && g_config.downscaleFactor < 1.0f) {
        width = static_cast<GLsizei>(width * g_config.downscaleFactor);
        height = static_cast<GLsizei>(height * g_config.downscaleFactor);
        x = static_cast<GLint>(x * g_config.downscaleFactor);
        y = static_cast<GLint>(y * g_config.downscaleFactor);
    }

    if (orig_glViewport) {
        orig_glViewport(x, y, width, height);
    }
}

bool loadAppConfig(const char *packageName, GlToolsConfig &outConfig) {
    std::string configPath = "/data/adb/modules/zygisk_gltools/config/";
    configPath += packageName;
    configPath += ".json";

    std::ifstream file(configPath);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    outConfig.packageName = packageName;
    outConfig.enabled = true;

    // Parse simple key-value JSON
    while (std::getline(file, line)) {
        if (line.find("\"enabled\":") != std::string::npos) {
            outConfig.enabled = (line.find("true") != std::string::npos);
        } else if (line.find("\"vendor\":") != std::string::npos) {
            size_t start = line.find('"', line.find(':') + 1) + 1;
            size_t end = line.rfind('"');
            if (start != std::string::npos && end > start) outConfig.vendor = line.substr(start, end - start);
        } else if (line.find("\"renderer\":") != std::string::npos) {
            size_t start = line.find('"', line.find(':') + 1) + 1;
            size_t end = line.rfind('"');
            if (start != std::string::npos && end > start) outConfig.renderer = line.substr(start, end - start);
        } else if (line.find("\"downscaleFactor\":") != std::string::npos) {
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                try {
                    outConfig.downscaleFactor = std::stof(line.substr(colon + 1));
                } catch (...) {}
            }
        } else if (line.find("\"fpsLimit\":") != std::string::npos) {
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                try {
                    outConfig.fpsLimit = std::stoi(line.substr(colon + 1));
                } catch (...) {}
            }
        }
    }

    return outConfig.enabled;
}

void installGlHooks(zygisk::Api *api, const GlToolsConfig &config) {
    g_config = config;
    LOGI("Installing GLTools hooks for: %s (Renderer: %s, Scale: %.2f)",
         config.packageName.c_str(), config.renderer.c_str(), config.downscaleFactor);

    // Register PLT hooks via Zygisk API for all target GLES libraries
    api->pltHookRegister(".*\\.so$", "glGetString", reinterpret_cast<void *>(hooked_glGetString), reinterpret_cast<void **>(&orig_glGetString));
    api->pltHookRegister(".*\\.so$", "glViewport", reinterpret_cast<void *>(hooked_glViewport), reinterpret_cast<void **>(&orig_glViewport));

    if (api->pltHookCommit()) {
        LOGI("PLT hooks successfully registered and committed!");
    } else {
        LOGE("Failed to commit PLT hooks!");
    }
}

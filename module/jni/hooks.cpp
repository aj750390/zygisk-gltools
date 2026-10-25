#include "hooks.h"
#include <android/log.h>
#include <dlfcn.h>
#include <fstream>
#include <cstring>
#include <cstdio>
#include <set>
#include <utility>
#include <sys/sysmacros.h>
#include <sys/system_properties.h>

#define LOG_TAG "ZygiskGLTools"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static GlToolsConfig g_config;

static const GLubyte *(*orig_glGetString)(GLenum name) = nullptr;
static void (*orig_glViewport)(GLint x, GLint y, GLsizei width, GLsizei height) = nullptr;
static void *(*orig_eglGetProcAddress)(const char *procname) = nullptr;
static void *(*orig_dlsym)(void *handle, const char *symbol) = nullptr;
static int (*orig_system_property_get)(const char *name, char *value) = nullptr;

static const GLubyte *hooked_glGetString(GLenum name) {
    if (g_config.enabled) {
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
    }
    return orig_glGetString ? orig_glGetString(name) : nullptr;
}

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

static void *hooked_eglGetProcAddress(const char *procname) {
    if (g_config.enabled && procname) {
        if (std::strcmp(procname, "glGetString") == 0) {
            return reinterpret_cast<void *>(hooked_glGetString);
        }
        if (std::strcmp(procname, "glViewport") == 0) {
            return reinterpret_cast<void *>(hooked_glViewport);
        }
    }
    return orig_eglGetProcAddress ? orig_eglGetProcAddress(procname) : nullptr;
}

static void *hooked_dlsym(void *handle, const char *symbol) {
    if (g_config.enabled && symbol) {
        if (std::strcmp(symbol, "glGetString") == 0) {
            return reinterpret_cast<void *>(hooked_glGetString);
        }
        if (std::strcmp(symbol, "glViewport") == 0) {
            return reinterpret_cast<void *>(hooked_glViewport);
        }
        if (std::strcmp(symbol, "eglGetProcAddress") == 0) {
            return reinterpret_cast<void *>(hooked_eglGetProcAddress);
        }
    }
    return orig_dlsym ? orig_dlsym(handle, symbol) : nullptr;
}

static int hooked_system_property_get(const char *name, char *value) {
    if (g_config.enabled && name && value) {
        if (std::strcmp(name, "ro.product.model") == 0 && !g_config.model.empty()) {
            std::strncpy(value, g_config.model.c_str(), PROP_VALUE_MAX - 1);
            value[PROP_VALUE_MAX - 1] = '\0';
            return std::strlen(value);
        }
        if (std::strcmp(name, "ro.product.manufacturer") == 0 && !g_config.manufacturer.empty()) {
            std::strncpy(value, g_config.manufacturer.c_str(), PROP_VALUE_MAX - 1);
            value[PROP_VALUE_MAX - 1] = '\0';
            return std::strlen(value);
        }
        if (std::strcmp(name, "ro.product.brand") == 0 && !g_config.brand.empty()) {
            std::strncpy(value, g_config.brand.c_str(), PROP_VALUE_MAX - 1);
            value[PROP_VALUE_MAX - 1] = '\0';
            return std::strlen(value);
        }
        if (std::strcmp(name, "ro.product.device") == 0 && !g_config.device.empty()) {
            std::strncpy(value, g_config.device.c_str(), PROP_VALUE_MAX - 1);
            value[PROP_VALUE_MAX - 1] = '\0';
            return std::strlen(value);
        }
        if (std::strcmp(name, "ro.board.platform") == 0 && !g_config.board.empty()) {
            std::strncpy(value, g_config.board.c_str(), PROP_VALUE_MAX - 1);
            value[PROP_VALUE_MAX - 1] = '\0';
            return std::strlen(value);
        }
        if (std::strcmp(name, "ro.hardware") == 0 && !g_config.hardware.empty()) {
            std::strncpy(value, g_config.hardware.c_str(), PROP_VALUE_MAX - 1);
            value[PROP_VALUE_MAX - 1] = '\0';
            return std::strlen(value);
        }
    }
    return orig_system_property_get ? orig_system_property_get(name, value) : 0;
}

static std::string extractJsonString(const std::string &line) {
    size_t colon = line.find(':');
    if (colon == std::string::npos) return "";
    size_t start = line.find('"', colon);
    if (start == std::string::npos) return "";
    size_t end = line.rfind('"');
    if (end == std::string::npos || end <= start) return "";
    return line.substr(start + 1, end - start - 1);
}

bool loadAppConfig(const char *packageName, GlToolsConfig &outConfig) {
    if (!packageName || packageName[0] == '\0') return false;

    std::string paths[] = {
        std::string("/data/adb/modules/zygisk_gltools/config/") + packageName + ".json",
        std::string("/data/local/tmp/gltools/") + packageName + ".json"
    };

    std::ifstream file;
    for (const auto &path : paths) {
        file.open(path);
        if (file.is_open()) {
            LOGI("Loaded configuration from: %s", path.c_str());
            break;
        }
    }

    if (!file.is_open()) {
        return false;
    }

    outConfig.packageName = packageName;
    outConfig.enabled = true;

    std::string line;
    while (std::getline(file, line)) {
        if (line.find("\"enabled\":") != std::string::npos) {
            outConfig.enabled = (line.find("true") != std::string::npos);
        } else if (line.find("\"vendor\":") != std::string::npos) {
            outConfig.vendor = extractJsonString(line);
        } else if (line.find("\"renderer\":") != std::string::npos) {
            outConfig.renderer = extractJsonString(line);
        } else if (line.find("\"model\":") != std::string::npos) {
            outConfig.model = extractJsonString(line);
        } else if (line.find("\"manufacturer\":") != std::string::npos) {
            outConfig.manufacturer = extractJsonString(line);
        } else if (line.find("\"brand\":") != std::string::npos) {
            outConfig.brand = extractJsonString(line);
        } else if (line.find("\"device\":") != std::string::npos) {
            outConfig.device = extractJsonString(line);
        } else if (line.find("\"board\":") != std::string::npos) {
            outConfig.board = extractJsonString(line);
        } else if (line.find("\"hardware\":") != std::string::npos) {
            outConfig.hardware = extractJsonString(line);
        } else if (line.find("\"downscaleFactor\":") != std::string::npos) {
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                try {
                    outConfig.downscaleFactor = std::stof(line.substr(colon + 1));
                } catch (...) {}
            }
        }
    }

    file.close();
    return outConfig.enabled;
}

void spoofJavaBuildProperties(JNIEnv *env, const GlToolsConfig &config) {
    if (!env || !config.enabled) return;

    jclass buildClass = env->FindClass("android/os/Build");
    if (!buildClass) {
        env->ExceptionClear();
        return;
    }

    auto setField = [&](const char *name, const std::string &val) {
        if (val.empty()) return;
        jfieldID fid = env->GetStaticFieldID(buildClass, name, "Ljava/lang/String;");
        if (fid) {
            jstring jVal = env->NewStringUTF(val.c_str());
            env->SetStaticObjectField(buildClass, fid, jVal);
            env->DeleteLocalRef(jVal);
        } else {
            env->ExceptionClear();
        }
    };

    setField("MODEL", config.model);
    setField("MANUFACTURER", config.manufacturer);
    setField("BRAND", config.brand);
    setField("DEVICE", config.device);
    setField("HARDWARE", config.hardware);
    setField("BOARD", config.board);

    env->DeleteLocalRef(buildClass);
}

void installGlHooks(zygisk::Api *api, const GlToolsConfig &config) {
    if (!api) return;
    g_config = config;

    FILE *fp = std::fopen("/proc/self/maps", "r");
    if (!fp) {
        LOGE("Failed to open /proc/self/maps");
        return;
    }

    std::set<std::pair<dev_t, ino_t>> hookedElfs;
    char line[512];

    while (std::fgets(line, sizeof(line), fp)) {
        uintptr_t start = 0, end = 0, offset = 0;
        unsigned int devMajor = 0, devMinor = 0;
        unsigned long inode = 0;
        char perms[5] = {0};
        int pathPos = 0;

        if (std::sscanf(line, "%lx-%lx %4s %lx %x:%x %lu %n",
                        &start, &end, perms, &offset, &devMajor, &devMinor, &inode, &pathPos) >= 7) {
            if (inode == 0 || pathPos <= 0) continue;
            const char *path = line + pathPos;

            // Hook loaded shared libraries (.so)
            if (std::strstr(path, ".so") != nullptr && std::strstr(path, "zygisk") == nullptr) {
                dev_t dev = makedev(devMajor, devMinor);
                ino_t ino = static_cast<ino_t>(inode);

                if (hookedElfs.insert({dev, ino}).second) {
                    api->pltHookRegister(dev, ino, "glGetString", reinterpret_cast<void *>(hooked_glGetString), reinterpret_cast<void **>(&orig_glGetString));
                    api->pltHookRegister(dev, ino, "glViewport", reinterpret_cast<void *>(hooked_glViewport), reinterpret_cast<void **>(&orig_glViewport));
                    api->pltHookRegister(dev, ino, "eglGetProcAddress", reinterpret_cast<void *>(hooked_eglGetProcAddress), reinterpret_cast<void **>(&orig_eglGetProcAddress));
                    api->pltHookRegister(dev, ino, "dlsym", reinterpret_cast<void *>(hooked_dlsym), reinterpret_cast<void **>(&orig_dlsym));
                    api->pltHookRegister(dev, ino, "__system_property_get", reinterpret_cast<void *>(hooked_system_property_get), reinterpret_cast<void **>(&orig_system_property_get));
                }
            }
        }
    }
    std::fclose(fp);

    if (api->pltHookCommit()) {
        LOGI("Successfully committed PLT hooks across %zu ELF modules for %s",
             hookedElfs.size(), config.packageName.c_str());
    } else {
        LOGE("Failed to commit PLT hooks!");
    }
}

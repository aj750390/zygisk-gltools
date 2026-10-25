#include "hooks.h"
#include <android/log.h>
#include <dlfcn.h>
#include <fstream>
#include <sstream>
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

// Native function pointers
static const GLubyte *(*orig_glGetString)(GLenum name) = nullptr;
static void (*orig_glViewport)(GLint x, GLint y, GLsizei width, GLsizei height) = nullptr;
static void *(*orig_eglGetProcAddress)(const char *procname) = nullptr;
static void *(*orig_dlsym)(void *handle, const char *symbol) = nullptr;
static int (*orig_system_property_get)(const char *name, char *value) = nullptr;

// JNI function pointers
static jstring (*orig_jni_gles20_glGetString)(JNIEnv *env, jclass clazz, jint name) = nullptr;
static jstring (*orig_jni_gles10_glGetString)(JNIEnv *env, jclass clazz, jint name) = nullptr;
static jstring (*orig_jni_glimpl_glGetString)(JNIEnv *env, jobject thiz, jint name) = nullptr;

static const char *getSpoofedGlString(GLenum name) {
    if (!g_config.enabled) return nullptr;
    switch (name) {
        case GL_VENDOR:
            return g_config.vendor.empty() ? nullptr : g_config.vendor.c_str();
        case GL_RENDERER:
            return g_config.renderer.empty() ? nullptr : g_config.renderer.c_str();
        case GL_VERSION:
            return g_config.version.empty() ? nullptr : g_config.version.c_str();
        case GL_EXTENSIONS:
            return g_config.extensions.empty() ? nullptr : g_config.extensions.c_str();
        default:
            return nullptr;
    }
}

// Native glGetString hook
static const GLubyte *hooked_glGetString(GLenum name) {
    const char *spoofed = getSpoofedGlString(name);
    if (spoofed) {
        return reinterpret_cast<const GLubyte *>(spoofed);
    }
    return orig_glGetString ? orig_glGetString(name) : nullptr;
}

// JNI GLES20.glGetString(int) hook
static jstring hooked_jni_gles20_glGetString(JNIEnv *env, jclass clazz, jint name) {
    const char *spoofed = getSpoofedGlString(static_cast<GLenum>(name));
    if (spoofed && env) {
        return env->NewStringUTF(spoofed);
    }
    return orig_jni_gles20_glGetString ? orig_jni_gles20_glGetString(env, clazz, name) : nullptr;
}

// JNI GLES10.glGetString(int) hook
static jstring hooked_jni_gles10_glGetString(JNIEnv *env, jclass clazz, jint name) {
    const char *spoofed = getSpoofedGlString(static_cast<GLenum>(name));
    if (spoofed && env) {
        return env->NewStringUTF(spoofed);
    }
    return orig_jni_gles10_glGetString ? orig_jni_gles10_glGetString(env, clazz, name) : nullptr;
}

// JNI GLImpl.glGetString(int) hook
static jstring hooked_jni_glimpl_glGetString(JNIEnv *env, jobject thiz, jint name) {
    const char *spoofed = getSpoofedGlString(static_cast<GLenum>(name));
    if (spoofed && env) {
        return env->NewStringUTF(spoofed);
    }
    return orig_jni_glimpl_glGetString ? orig_jni_glimpl_glGetString(env, thiz, name) : nullptr;
}

// Native glViewport hook
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

static std::string extractJsonField(const std::string &content, const std::string &key) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = content.find(pattern);
    if (pos == std::string::npos) return "";
    size_t colon = content.find(':', pos + pattern.length());
    if (colon == std::string::npos) return "";
    size_t firstQuote = content.find('"', colon + 1);
    if (firstQuote == std::string::npos) return "";
    size_t secondQuote = content.find('"', firstQuote + 1);
    if (secondQuote == std::string::npos) return "";
    return content.substr(firstQuote + 1, secondQuote - firstQuote - 1);
}

bool parseConfigContent(const std::string &content, const char *packageName, GlToolsConfig &outConfig) {
    if (content.empty()) return false;

    outConfig.packageName = packageName ? packageName : "";
    outConfig.enabled = true;

    // 1. Parse key=value lines if present
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq != std::string::npos && line[0] != '{') {
            std::string k = line.substr(0, eq);
            std::string v = line.substr(eq + 1);
            if (k == "enabled") outConfig.enabled = (v == "true" || v == "1");
            else if (k == "vendor" && !v.empty()) outConfig.vendor = v;
            else if (k == "renderer" && !v.empty()) outConfig.renderer = v;
            else if (k == "version" && !v.empty()) outConfig.version = v;
            else if (k == "model" && !v.empty()) outConfig.model = v;
            else if (k == "manufacturer" && !v.empty()) outConfig.manufacturer = v;
            else if (k == "brand" && !v.empty()) outConfig.brand = v;
            else if (k == "device" && !v.empty()) outConfig.device = v;
            else if (k == "board" && !v.empty()) outConfig.board = v;
            else if (k == "hardware" && !v.empty()) outConfig.hardware = v;
            else if (k == "downscaleFactor") {
                try { outConfig.downscaleFactor = std::stof(v); } catch (...) {}
            }
        }
    }

    // 2. Also support JSON format (single-line or multi-line)
    if (content.find('{') != std::string::npos) {
        size_t enPos = content.find("\"enabled\"");
        if (enPos != std::string::npos) {
            size_t colon = content.find(':', enPos);
            if (colon != std::string::npos) {
                size_t comma = content.find_first_of(",}", colon);
                std::string val = content.substr(colon + 1, comma - colon - 1);
                outConfig.enabled = (val.find("true") != std::string::npos);
            }
        }
        std::string v;
        if (!(v = extractJsonField(content, "vendor")).empty()) outConfig.vendor = v;
        if (!(v = extractJsonField(content, "renderer")).empty()) outConfig.renderer = v;
        if (!(v = extractJsonField(content, "version")).empty()) outConfig.version = v;
        if (!(v = extractJsonField(content, "model")).empty()) outConfig.model = v;
        if (!(v = extractJsonField(content, "manufacturer")).empty()) outConfig.manufacturer = v;
        if (!(v = extractJsonField(content, "brand")).empty()) outConfig.brand = v;
        if (!(v = extractJsonField(content, "device")).empty()) outConfig.device = v;
        if (!(v = extractJsonField(content, "board")).empty()) outConfig.board = v;
        if (!(v = extractJsonField(content, "hardware")).empty()) outConfig.hardware = v;

        size_t dsPos = content.find("\"downscaleFactor\"");
        if (dsPos != std::string::npos) {
            size_t colon = content.find(':', dsPos);
            if (colon != std::string::npos) {
                try { outConfig.downscaleFactor = std::stof(content.substr(colon + 1)); } catch (...) {}
            }
        }
    }

    return outConfig.enabled;
}

bool loadAppConfigFromFile(const char *packageName, std::string &outRawContent) {
    if (!packageName || packageName[0] == '\0') return false;

    std::string paths[] = {
        std::string("/data/adb/modules/zygisk_gltools/config/") + packageName + ".conf",
        std::string("/data/adb/modules/zygisk_gltools/config/") + packageName + ".json",
        std::string("/data/local/tmp/gltools/") + packageName + ".conf",
        std::string("/data/local/tmp/gltools/") + packageName + ".json"
    };

    for (const auto &path : paths) {
        std::ifstream file(path);
        if (file.is_open()) {
            std::ostringstream ss;
            ss << file.rdbuf();
            outRawContent = ss.str();
            if (!outRawContent.empty()) {
                LOGI("Read config file (%zu bytes): %s", outRawContent.size(), path.c_str());
                return true;
            }
        }
    }
    return false;
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

void installGlHooks(zygisk::Api *api, JNIEnv *env, const GlToolsConfig &config) {
    if (!api) return;
    g_config = config;

    // 1. Hook Java JNI OpenGL ES calls (GLES20, GLES10, GLImpl)
    if (env) {
        JNINativeMethod gles20Methods[] = {
            {"glGetString", "(I)Ljava/lang/String;", reinterpret_cast<void *>(hooked_jni_gles20_glGetString)}
        };
        api->hookJniNativeMethods(env, "android/opengl/GLES20", gles20Methods, 1);
        if (gles20Methods[0].fnPtr) {
            orig_jni_gles20_glGetString = reinterpret_cast<jstring (*)(JNIEnv *, jclass, jint)>(gles20Methods[0].fnPtr);
            LOGI("Hooked JNI android/opengl/GLES20->glGetString");
        }

        JNINativeMethod gles10Methods[] = {
            {"glGetString", "(I)Ljava/lang/String;", reinterpret_cast<void *>(hooked_jni_gles10_glGetString)}
        };
        api->hookJniNativeMethods(env, "android/opengl/GLES10", gles10Methods, 1);
        if (gles10Methods[0].fnPtr) {
            orig_jni_gles10_glGetString = reinterpret_cast<jstring (*)(JNIEnv *, jclass, jint)>(gles10Methods[0].fnPtr);
        }

        JNINativeMethod glImplMethods[] = {
            {"glGetString", "(I)Ljava/lang/String;", reinterpret_cast<void *>(hooked_jni_glimpl_glGetString)}
        };
        api->hookJniNativeMethods(env, "com/google/android/gles_jni/GLImpl", glImplMethods, 1);
        if (glImplMethods[0].fnPtr) {
            orig_jni_glimpl_glGetString = reinterpret_cast<jstring (*)(JNIEnv *, jobject, jint)>(glImplMethods[0].fnPtr);
            LOGI("Hooked JNI GLImpl->glGetString");
        }
    }

    // 2. Hook Native PLT symbols across loaded shared libraries
    FILE *fp = std::fopen("/proc/self/maps", "r");
    if (!fp) return;

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
        LOGI("Committed PLT hooks across %zu ELFs for %s [GLES: %s | GPU: %s]",
             hookedElfs.size(), config.packageName.c_str(), config.version.c_str(), config.renderer.c_str());
    }
}

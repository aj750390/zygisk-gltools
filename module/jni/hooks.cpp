#include "hooks.h"
#include <android/log.h>
#include <dlfcn.h>
#include <link.h>
#include <elf.h>
#include <sys/mman.h>
#include <unistd.h>
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

#ifndef GL_SHADING_LANGUAGE_VERSION
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#endif
#ifndef GL_MAJOR_VERSION
#define GL_MAJOR_VERSION 0x821B
#endif
#ifndef GL_MINOR_VERSION
#define GL_MINOR_VERSION 0x821C
#endif
#ifndef EGL_CONTEXT_CLIENT_VERSION
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#endif
#ifndef EGL_NONE
#define EGL_NONE 0x3038
#endif

#if defined(__LP64__)
#define ELFW_R_SYM(r_info) ELF64_R_SYM(r_info)
#else
#define ELFW_R_SYM(r_info) ELF32_R_SYM(r_info)
#endif

static GlToolsConfig g_config;

// Native function pointers
static const GLubyte *(*orig_glGetString)(GLenum name) = nullptr;
static void (*orig_glGetIntegerv)(GLenum pname, GLint *params) = nullptr;
static void (*orig_glViewport)(GLint x, GLint y, GLsizei width, GLsizei height) = nullptr;
static void *(*orig_eglGetProcAddress)(const char *procname) = nullptr;
static void *(*orig_eglCreateContext)(void *dpy, void *config, void *share_context, const int *attrib_list) = nullptr;
static void *(*orig_dlsym)(void *handle, const char *symbol) = nullptr;
static void *(*orig_dlopen)(const char *filename, int flags) = nullptr;
static void *(*orig_android_dlopen_ext)(const char *filename, int flags, const void *extinfo) = nullptr;
static int (*orig_system_property_get)(const char *name, char *value) = nullptr;

// JNI function pointers
static jstring (*orig_jni_gles20_glGetString)(JNIEnv *env, jclass clazz, jint name) = nullptr;
static jstring (*orig_jni_gles10_glGetString)(JNIEnv *env, jclass clazz, jint name) = nullptr;
static jstring (*orig_jni_glimpl_glGetString)(JNIEnv *env, jobject thiz, jint name) = nullptr;
static jstring (*orig_jni_sysprop_get)(JNIEnv *env, jclass clazz, jstring keyJ, jstring defJ) = nullptr;
static jint (*orig_jni_sysprop_get_int)(JNIEnv *env, jclass clazz, jstring keyJ, jint defV) = nullptr;

static void hookAllLoadedElfs();

static const char *getSpoofedGlString(GLenum name) {
    if (!g_config.enabled) return nullptr;
    switch (name) {
        case GL_VENDOR:
            return g_config.vendor.empty() ? nullptr : g_config.vendor.c_str();
        case GL_RENDERER:
            return g_config.renderer.empty() ? nullptr : g_config.renderer.c_str();
        case GL_VERSION:
            return g_config.version.empty() ? nullptr : g_config.version.c_str();
        case GL_SHADING_LANGUAGE_VERSION:
            return (g_config.targetGlMajor == 2)
                ? "OpenGL ES GLSL ES 1.00"
                : "OpenGL ES GLSL ES 3.20";
        case GL_EXTENSIONS:
            return g_config.extensions.empty() ? nullptr : g_config.extensions.c_str();
        default:
            return nullptr;
    }
}

static int getEncodedGlEsVersionInt() {
    return (g_config.targetGlMajor << 16) | (g_config.targetGlMinor & 0xFFFF);
}

// Native glGetString hook
static const GLubyte *hooked_glGetString(GLenum name) {
    const char *spoofed = getSpoofedGlString(name);
    if (spoofed) {
        return reinterpret_cast<const GLubyte *>(spoofed);
    }
    return orig_glGetString ? orig_glGetString(name) : nullptr;
}

// Native glGetIntegerv hook (enforces GL_MAJOR_VERSION and GL_MINOR_VERSION)
static void hooked_glGetIntegerv(GLenum pname, GLint *params) {
    if (orig_glGetIntegerv) {
        orig_glGetIntegerv(pname, params);
    }
    if (g_config.enabled && params) {
        if (pname == GL_MAJOR_VERSION) {
            *params = g_config.targetGlMajor;
        } else if (pname == GL_MINOR_VERSION) {
            *params = g_config.targetGlMinor;
        }
    }
}

// Native eglCreateContext hook (enforces OpenGL ES 2.0 downgrade limit when ES 2.0 is selected)
static void *hooked_eglCreateContext(void *dpy, void *config, void *share_context, const int *attrib_list) {
    if (g_config.enabled && g_config.targetGlMajor <= 2 && attrib_list) {
        for (int i = 0; attrib_list[i] != EGL_NONE; i += 2) {
            if (attrib_list[i] == EGL_CONTEXT_CLIENT_VERSION && attrib_list[i + 1] > g_config.targetGlMajor) {
                LOGI("Blocking OpenGL ES %d.x context creation (enforcing OpenGL ES %d.%d limit)",
                     attrib_list[i + 1], g_config.targetGlMajor, g_config.targetGlMinor);
                return nullptr; // EGL_NO_CONTEXT
            }
        }
    }
    return orig_eglCreateContext ? orig_eglCreateContext(dpy, config, share_context, attrib_list) : nullptr;
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

// JNI SystemProperties.native_get(String, String) hook
static jstring hooked_jni_sysprop_get(JNIEnv *env, jclass clazz, jstring keyJ, jstring defJ) {
    if (g_config.enabled && env && keyJ) {
        const char *key = env->GetStringUTFChars(keyJ, nullptr);
        if (key) {
            std::string k(key);
            env->ReleaseStringUTFChars(keyJ, key);
            if (k == "ro.opengles.version") {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%d", getEncodedGlEsVersionInt());
                return env->NewStringUTF(buf);
            }
            if (k == "ro.product.model" && !g_config.model.empty()) return env->NewStringUTF(g_config.model.c_str());
            if (k == "ro.product.manufacturer" && !g_config.manufacturer.empty()) return env->NewStringUTF(g_config.manufacturer.c_str());
            if (k == "ro.product.brand" && !g_config.brand.empty()) return env->NewStringUTF(g_config.brand.c_str());
            if (k == "ro.product.device" && !g_config.device.empty()) return env->NewStringUTF(g_config.device.c_str());
            if (k == "ro.board.platform" && !g_config.board.empty()) return env->NewStringUTF(g_config.board.c_str());
            if (k == "ro.hardware" && !g_config.hardware.empty()) return env->NewStringUTF(g_config.hardware.c_str());
        }
    }
    return orig_jni_sysprop_get ? orig_jni_sysprop_get(env, clazz, keyJ, defJ) : defJ;
}

// JNI SystemProperties.native_get_int(String, int) hook
static jint hooked_jni_sysprop_get_int(JNIEnv *env, jclass clazz, jstring keyJ, jint defV) {
    if (g_config.enabled && env && keyJ) {
        const char *key = env->GetStringUTFChars(keyJ, nullptr);
        if (key) {
            bool isGlesVer = (std::strcmp(key, "ro.opengles.version") == 0);
            env->ReleaseStringUTFChars(keyJ, key);
            if (isGlesVer) {
                return static_cast<jint>(getEncodedGlEsVersionInt());
            }
        }
    }
    return orig_jni_sysprop_get_int ? orig_jni_sysprop_get_int(env, clazz, keyJ, defV) : defV;
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
        if (std::strcmp(procname, "glGetString") == 0) return reinterpret_cast<void *>(hooked_glGetString);
        if (std::strcmp(procname, "glGetIntegerv") == 0) return reinterpret_cast<void *>(hooked_glGetIntegerv);
        if (std::strcmp(procname, "glViewport") == 0) return reinterpret_cast<void *>(hooked_glViewport);
        if (std::strcmp(procname, "eglCreateContext") == 0) return reinterpret_cast<void *>(hooked_eglCreateContext);
    }
    return orig_eglGetProcAddress ? orig_eglGetProcAddress(procname) : nullptr;
}

static void *hooked_dlsym(void *handle, const char *symbol) {
    if (g_config.enabled && symbol) {
        if (std::strcmp(symbol, "glGetString") == 0) return reinterpret_cast<void *>(hooked_glGetString);
        if (std::strcmp(symbol, "glGetIntegerv") == 0) return reinterpret_cast<void *>(hooked_glGetIntegerv);
        if (std::strcmp(symbol, "glViewport") == 0) return reinterpret_cast<void *>(hooked_glViewport);
        if (std::strcmp(symbol, "eglCreateContext") == 0) return reinterpret_cast<void *>(hooked_eglCreateContext);
        if (std::strcmp(symbol, "eglGetProcAddress") == 0) return reinterpret_cast<void *>(hooked_eglGetProcAddress);
    }
    return orig_dlsym ? orig_dlsym(handle, symbol) : nullptr;
}

// Intercept dynamic library loading (libunity.so, libUE4.so, etc.) and immediately patch new GOT tables
static void *hooked_dlopen(const char *filename, int flags) {
    void *handle = orig_dlopen ? orig_dlopen(filename, flags) : nullptr;
    if (handle && g_config.enabled) {
        hookAllLoadedElfs();
    }
    return handle;
}

static void *hooked_android_dlopen_ext(const char *filename, int flags, const void *extinfo) {
    void *handle = orig_android_dlopen_ext ? orig_android_dlopen_ext(filename, flags, extinfo) : nullptr;
    if (handle && g_config.enabled) {
        hookAllLoadedElfs();
    }
    return handle;
}

static int hooked_system_property_get(const char *name, char *value) {
    if (g_config.enabled && name && value) {
        if (std::strcmp(name, "ro.opengles.version") == 0) {
            std::snprintf(value, PROP_VALUE_MAX, "%d", getEncodedGlEsVersionInt());
            return std::strlen(value);
        }
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

// Runtime ELF GOT/PLT Patcher via dl_iterate_phdr
static inline uintptr_t resolveElfPtr(uintptr_t base, uintptr_t ptr) {
    return (ptr < base) ? (base + ptr) : ptr;
}

static void patchGotAddress(uintptr_t gotAddr, void *newFunc, void **oldFunc) {
    void **slot = reinterpret_cast<void **>(gotAddr);
    if (*slot == newFunc) return;
    size_t pageSize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    uintptr_t pageStart = gotAddr & ~(pageSize - 1);
    if (mprotect(reinterpret_cast<void *>(pageStart), pageSize, PROT_READ | PROT_WRITE) == 0) {
        if (oldFunc && *oldFunc == nullptr && *slot != nullptr) {
            *oldFunc = *slot;
        }
        *slot = newFunc;
    }
}

static void processRelocations(uintptr_t base, const ElfW(Sym) *symtab, const char *strtab,
                               uintptr_t relPtr, size_t relSize, bool isRela) {
    if (!relPtr || !relSize || !symtab || !strtab) return;
    size_t count = isRela ? (relSize / sizeof(ElfW(Rela))) : (relSize / sizeof(ElfW(Rel)));
    for (size_t i = 0; i < count; ++i) {
        uintptr_t r_offset = 0;
        ElfW(Xword) r_info = 0;
        if (isRela) {
            const auto *rela = reinterpret_cast<const ElfW(Rela) *>(relPtr) + i;
            r_offset = rela->r_offset;
            r_info = rela->r_info;
        } else {
            const auto *rel = reinterpret_cast<const ElfW(Rel) *>(relPtr) + i;
            r_offset = rel->r_offset;
            r_info = rel->r_info;
        }
        size_t symIdx = ELFW_R_SYM(r_info);
        if (symIdx == 0) continue;
        const char *symName = strtab + symtab[symIdx].st_name;
        if (!symName || symName[0] == '\0') continue;

        uintptr_t gotEntry = base + r_offset;
        if (std::strcmp(symName, "glGetString") == 0) {
            patchGotAddress(gotEntry, reinterpret_cast<void *>(hooked_glGetString), reinterpret_cast<void **>(&orig_glGetString));
        } else if (std::strcmp(symName, "glGetIntegerv") == 0) {
            patchGotAddress(gotEntry, reinterpret_cast<void *>(hooked_glGetIntegerv), reinterpret_cast<void **>(&orig_glGetIntegerv));
        } else if (std::strcmp(symName, "glViewport") == 0) {
            patchGotAddress(gotEntry, reinterpret_cast<void *>(hooked_glViewport), reinterpret_cast<void **>(&orig_glViewport));
        } else if (std::strcmp(symName, "eglGetProcAddress") == 0) {
            patchGotAddress(gotEntry, reinterpret_cast<void *>(hooked_eglGetProcAddress), reinterpret_cast<void **>(&orig_eglGetProcAddress));
        } else if (std::strcmp(symName, "eglCreateContext") == 0) {
            patchGotAddress(gotEntry, reinterpret_cast<void *>(hooked_eglCreateContext), reinterpret_cast<void **>(&orig_eglCreateContext));
        } else if (std::strcmp(symName, "dlsym") == 0) {
            patchGotAddress(gotEntry, reinterpret_cast<void *>(hooked_dlsym), reinterpret_cast<void **>(&orig_dlsym));
        } else if (std::strcmp(symName, "dlopen") == 0) {
            patchGotAddress(gotEntry, reinterpret_cast<void *>(hooked_dlopen), reinterpret_cast<void **>(&orig_dlopen));
        } else if (std::strcmp(symName, "android_dlopen_ext") == 0) {
            patchGotAddress(gotEntry, reinterpret_cast<void *>(hooked_android_dlopen_ext), reinterpret_cast<void **>(&orig_android_dlopen_ext));
        } else if (std::strcmp(symName, "__system_property_get") == 0) {
            patchGotAddress(gotEntry, reinterpret_cast<void *>(hooked_system_property_get), reinterpret_cast<void **>(&orig_system_property_get));
        }
    }
}

static int phdrCallback(struct dl_phdr_info *info, size_t /*size*/, void * /*data*/) {
    if (!info->dlpi_name || info->dlpi_name[0] == '\0') return 0;
    if (std::strstr(info->dlpi_name, "zygisk") != nullptr ||
        std::strstr(info->dlpi_name, "libc.so") != nullptr ||
        std::strstr(info->dlpi_name, "libdl.so") != nullptr ||
        std::strstr(info->dlpi_name, "ld-android.so") != nullptr) {
        return 0;
    }

    uintptr_t base = info->dlpi_addr;
    const ElfW(Dyn) *dyn = nullptr;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
            dyn = reinterpret_cast<const ElfW(Dyn) *>(base + info->dlpi_phdr[i].p_vaddr);
            break;
        }
    }
    if (!dyn) return 0;

    const ElfW(Sym) *symtab = nullptr;
    const char *strtab = nullptr;
    uintptr_t jmprel = 0, rela = 0, rel = 0;
    size_t pltrelsz = 0, relasz = 0, relsz = 0;
    bool pltIsRela = (sizeof(void *) == 8);

    for (; dyn->d_tag != DT_NULL; ++dyn) {
        switch (dyn->d_tag) {
            case DT_SYMTAB: symtab = reinterpret_cast<const ElfW(Sym) *>(resolveElfPtr(base, dyn->d_un.d_ptr)); break;
            case DT_STRTAB: strtab = reinterpret_cast<const char *>(resolveElfPtr(base, dyn->d_un.d_ptr)); break;
            case DT_JMPREL: jmprel = resolveElfPtr(base, dyn->d_un.d_ptr); break;
            case DT_PLTRELSZ: pltrelsz = dyn->d_un.d_val; break;
            case DT_PLTREL: pltIsRela = (dyn->d_un.d_val == DT_RELA); break;
            case DT_RELA: rela = resolveElfPtr(base, dyn->d_un.d_ptr); break;
            case DT_RELASZ: relasz = dyn->d_un.d_val; break;
            case DT_REL: rel = resolveElfPtr(base, dyn->d_un.d_ptr); break;
            case DT_RELSZ: relsz = dyn->d_un.d_val; break;
        }
    }

    processRelocations(base, symtab, strtab, jmprel, pltrelsz, pltIsRela);
    processRelocations(base, symtab, strtab, rela, relasz, true);
    processRelocations(base, symtab, strtab, rel, relsz, false);
    return 0;
}

static void hookAllLoadedElfs() {
    dl_iterate_phdr(phdrCallback, nullptr);
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

static void parseVersionMajorMinor(const std::string &verStr, int &outMajor, int &outMinor) {
    outMajor = 3;
    outMinor = 2;
    for (size_t i = 0; i + 2 < verStr.size(); ++i) {
        if ((verStr[i] == '2' || verStr[i] == '3') && verStr[i + 1] == '.' &&
            (verStr[i + 2] >= '0' && verStr[i + 2] <= '9')) {
            outMajor = verStr[i] - '0';
            outMinor = verStr[i + 2] - '0';
            return;
        }
    }
}

bool parseConfigContent(const std::string &content, const char *packageName, GlToolsConfig &outConfig) {
    if (content.empty()) return false;

    outConfig.packageName = packageName ? packageName : "";
    outConfig.enabled = true;

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

    parseVersionMajorMinor(outConfig.version, outConfig.targetGlMajor, outConfig.targetGlMinor);
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

    // Resolve default original symbols via dlsym first so they are never null
    orig_glGetString = reinterpret_cast<const GLubyte *(*)(GLenum)>(dlsym(RTLD_DEFAULT, "glGetString"));
    orig_glGetIntegerv = reinterpret_cast<void (*)(GLenum, GLint *)>(dlsym(RTLD_DEFAULT, "glGetIntegerv"));
    orig_glViewport = reinterpret_cast<void (*)(GLint, GLint, GLsizei, GLsizei)>(dlsym(RTLD_DEFAULT, "glViewport"));
    orig_eglGetProcAddress = reinterpret_cast<void *(*)(const char *)>(dlsym(RTLD_DEFAULT, "eglGetProcAddress"));
    orig_eglCreateContext = reinterpret_cast<void *(*)(void *, void *, void *, const int *)>(dlsym(RTLD_DEFAULT, "eglCreateContext"));
    orig_dlsym = reinterpret_cast<void *(*)(void *, const char *)>(dlsym(RTLD_DEFAULT, "dlsym"));
    orig_dlopen = reinterpret_cast<void *(*)(const char *, int)>(dlsym(RTLD_DEFAULT, "dlopen"));
    orig_android_dlopen_ext = reinterpret_cast<void *(*)(const char *, int, const void *)>(dlsym(RTLD_DEFAULT, "android_dlopen_ext"));
    orig_system_property_get = reinterpret_cast<int (*)(const char *, char *)>(dlsym(RTLD_DEFAULT, "__system_property_get"));

    // 1. Hook Java JNI OpenGL ES and SystemProperties methods
    if (env) {
        JNINativeMethod gles20Methods[] = {
            {"glGetString", "(I)Ljava/lang/String;", reinterpret_cast<void *>(hooked_jni_gles20_glGetString)}
        };
        api->hookJniNativeMethods(env, "android/opengl/GLES20", gles20Methods, 1);
        if (gles20Methods[0].fnPtr) {
            orig_jni_gles20_glGetString = reinterpret_cast<jstring (*)(JNIEnv *, jclass, jint)>(gles20Methods[0].fnPtr);
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
        }

        JNINativeMethod sysPropMethods[] = {
            {"native_get", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", reinterpret_cast<void *>(hooked_jni_sysprop_get)},
            {"native_get_int", "(Ljava/lang/String;I)I", reinterpret_cast<void *>(hooked_jni_sysprop_get_int)}
        };
        api->hookJniNativeMethods(env, "android/os/SystemProperties", sysPropMethods, 2);
        if (sysPropMethods[0].fnPtr) {
            orig_jni_sysprop_get = reinterpret_cast<jstring (*)(JNIEnv *, jclass, jstring, jstring)>(sysPropMethods[0].fnPtr);
        }
        if (sysPropMethods[1].fnPtr) {
            orig_jni_sysprop_get_int = reinterpret_cast<jint (*)(JNIEnv *, jclass, jstring, jint)>(sysPropMethods[1].fnPtr);
        }
    }

    // 2. Hook all currently loaded ELFs and register dlopen/android_dlopen_ext hooks for newly loaded game engines
    hookAllLoadedElfs();

    // 3. Also register via Zygisk PLT hook API for preloaded system libraries
    FILE *fp = std::fopen("/proc/self/maps", "r");
    if (fp) {
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
                        api->pltHookRegister(dev, ino, "glGetString", reinterpret_cast<void *>(hooked_glGetString), nullptr);
                        api->pltHookRegister(dev, ino, "glGetIntegerv", reinterpret_cast<void *>(hooked_glGetIntegerv), nullptr);
                        api->pltHookRegister(dev, ino, "glViewport", reinterpret_cast<void *>(hooked_glViewport), nullptr);
                        api->pltHookRegister(dev, ino, "eglGetProcAddress", reinterpret_cast<void *>(hooked_eglGetProcAddress), nullptr);
                        api->pltHookRegister(dev, ino, "eglCreateContext", reinterpret_cast<void *>(hooked_eglCreateContext), nullptr);
                        api->pltHookRegister(dev, ino, "dlsym", reinterpret_cast<void *>(hooked_dlsym), nullptr);
                        api->pltHookRegister(dev, ino, "dlopen", reinterpret_cast<void *>(hooked_dlopen), nullptr);
                        api->pltHookRegister(dev, ino, "android_dlopen_ext", reinterpret_cast<void *>(hooked_android_dlopen_ext), nullptr);
                        api->pltHookRegister(dev, ino, "__system_property_get", reinterpret_cast<void *>(hooked_system_property_get), nullptr);
                    }
                }
            }
        }
        std::fclose(fp);
        api->pltHookCommit();
    }

    LOGI("GLTools hooks installed for %s [Enforced GLES %d.%d (%s) | GPU: %s]",
         config.packageName.c_str(), config.targetGlMajor, config.targetGlMinor,
         config.version.c_str(), config.renderer.c_str());
}

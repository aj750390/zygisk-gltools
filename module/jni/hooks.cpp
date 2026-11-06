#include "hooks.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include <android/log.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES3/gl3.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#define LOG_TAG "GLTools"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

static GameConfig g_config;
static std::string g_spoofedExtensionsStr;
static std::vector<std::string> g_spoofedExtensionsList;
static bool g_vsyncUncapActive = false;

// ---------------------------------------------------------------------------
// Helpers: File descriptor for fake CPU data
// ---------------------------------------------------------------------------
static std::string generateFakeCpuInfo() {
    std::string out;
    int cores = g_config.fakeCpuCores > 0 ? g_config.fakeCpuCores : 8;
    std::string hardware = !g_config.fakeCpuHardware.empty() ? g_config.fakeCpuHardware : "Qualcomm Technologies, Inc SM8650";

    for (int i = 0; i < cores; ++i) {
        out += "processor	: " + std::to_string(i) + "\n";
        out += "BogoMIPS	: 38.40\n";
        out += "Features	: fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp cpuid asimdrdm jscvt fcma lrcpc dcpop sha3 sm3 sm4 asimddp sha512 sve asimdfhm dit uscat ilrcpc flagm ssbs sb paca pacg dcpodp flagm2 frint\n";
        out += "CPU implementer	: 0x51\n";
        out += "CPU architecture: 8\n";
        out += "CPU variant	: 0x2\n";
        out += "CPU part	: 0x001\n";
        out += "CPU revision	: 1\n\n";
    }
    out += "Hardware	: " + hardware + "\n";
    out += "Revision	: 0000\n";
    out += "Serial		: 0000000000000000\n";
    return out;
}

static std::string generateCpuRange() {
    int cores = g_config.fakeCpuCores > 0 ? g_config.fakeCpuCores : 8;
    return "0-" + std::to_string(cores - 1) + "\n";
}

static int createFakeStringFd(const std::string &data) {
#if defined(__NR_memfd_create)
    int fd = syscall(__NR_memfd_create, "fake_cpu", MFD_CLOEXEC);
    if (fd >= 0) {
        write(fd, data.data(), data.size());
        lseek(fd, 0, SEEK_SET);
        return fd;
    }
#endif
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) == 0) {
        write(fds[1], data.data(), data.size());
        close(fds[1]);
        return fds[0];
    }
    return -1;
}

// ---------------------------------------------------------------------------
// POSIX CPU & Cores Hooks (sysconf, get_nprocs, open, fopen)
// ---------------------------------------------------------------------------
static long (*orig_sysconf)(int name) = nullptr;
static long hooked_sysconf(int name) {
    if (g_config.enabled && g_config.fakeCpuEnabled && g_config.fakeCpuCores > 0) {
        if (name == _SC_NPROCESSORS_CONF || name == _SC_NPROCESSORS_ONLN) {
            return g_config.fakeCpuCores;
        }
    }
    return orig_sysconf ? orig_sysconf(name) : sysconf(name);
}

static int (*orig_get_nprocs)(void) = nullptr;
static int hooked_get_nprocs(void) {
    if (g_config.enabled && g_config.fakeCpuEnabled && g_config.fakeCpuCores > 0) {
        return g_config.fakeCpuCores;
    }
    return orig_get_nprocs ? orig_get_nprocs() : 8;
}

static int (*orig_get_nprocs_conf)(void) = nullptr;
static int hooked_get_nprocs_conf(void) {
    if (g_config.enabled && g_config.fakeCpuEnabled && g_config.fakeCpuCores > 0) {
        return g_config.fakeCpuCores;
    }
    return orig_get_nprocs_conf ? orig_get_nprocs_conf() : 8;
}

static int (*orig_open)(const char *pathname, int flags, ...) = nullptr;
static int hooked_open(const char *pathname, int flags, mode_t mode) {
    if (g_config.enabled && g_config.fakeCpuEnabled && pathname) {
        if (strcmp(pathname, "/proc/cpuinfo") == 0) {
            int fd = createFakeStringFd(generateFakeCpuInfo());
            if (fd >= 0) return fd;
        } else if (strcmp(pathname, "/sys/devices/system/cpu/online") == 0 ||
                   strcmp(pathname, "/sys/devices/system/cpu/present") == 0 ||
                   strcmp(pathname, "/sys/devices/system/cpu/possible") == 0) {
            int fd = createFakeStringFd(generateCpuRange());
            if (fd >= 0) return fd;
        }
    }
    return orig_open ? orig_open(pathname, flags, mode) : open(pathname, flags, mode);
}

static int (*orig_openat)(int dirfd, const char *pathname, int flags, ...) = nullptr;
static int hooked_openat(int dirfd, const char *pathname, int flags, mode_t mode) {
    if (g_config.enabled && g_config.fakeCpuEnabled && pathname) {
        if (strcmp(pathname, "/proc/cpuinfo") == 0) {
            int fd = createFakeStringFd(generateFakeCpuInfo());
            if (fd >= 0) return fd;
        } else if (strcmp(pathname, "/sys/devices/system/cpu/online") == 0 ||
                   strcmp(pathname, "/sys/devices/system/cpu/present") == 0 ||
                   strcmp(pathname, "/sys/devices/system/cpu/possible") == 0) {
            int fd = createFakeStringFd(generateCpuRange());
            if (fd >= 0) return fd;
        }
    }
    return orig_openat ? orig_openat(dirfd, pathname, flags, mode) : openat(dirfd, pathname, flags, mode);
}

static FILE* (*orig_fopen)(const char *pathname, const char *mode) = nullptr;
static FILE* hooked_fopen(const char *pathname, const char *mode) {
    if (g_config.enabled && g_config.fakeCpuEnabled && pathname) {
        if (strcmp(pathname, "/proc/cpuinfo") == 0) {
            int fd = createFakeStringFd(generateFakeCpuInfo());
            if (fd >= 0) return fdopen(fd, mode);
        } else if (strcmp(pathname, "/sys/devices/system/cpu/online") == 0 ||
                   strcmp(pathname, "/sys/devices/system/cpu/present") == 0 ||
                   strcmp(pathname, "/sys/devices/system/cpu/possible") == 0) {
            int fd = createFakeStringFd(generateCpuRange());
            if (fd >= 0) return fdopen(fd, mode);
        }
    }
    return orig_fopen ? orig_fopen(pathname, mode) : fopen(pathname, mode);
}

// ---------------------------------------------------------------------------
// GLES & EGL Hooks
// ---------------------------------------------------------------------------
static const GLubyte* (*orig_glGetString)(GLenum name) = nullptr;
static const GLubyte* hooked_glGetString(GLenum name) {
    if (g_config.enabled) {
        if (name == GL_RENDERER && !g_config.renderer.empty()) {
            return reinterpret_cast<const GLubyte*>(g_config.renderer.c_str());
        }
        if (name == GL_VENDOR && !g_config.vendor.empty()) {
            return reinterpret_cast<const GLubyte*>(g_config.vendor.c_str());
        }
        if (name == GL_VERSION && !g_config.version.empty()) {
            return reinterpret_cast<const GLubyte*>(g_config.version.c_str());
        }
        if (name == GL_EXTENSIONS && !g_spoofedExtensionsStr.empty()) {
            return reinterpret_cast<const GLubyte*>(g_spoofedExtensionsStr.c_str());
        }
    }
    return orig_glGetString ? orig_glGetString(name) : nullptr;
}

static const GLubyte* (*orig_glGetStringi)(GLenum name, GLuint index) = nullptr;
static const GLubyte* hooked_glGetStringi(GLenum name, GLuint index) {
    if (g_config.enabled && name == GL_EXTENSIONS && !g_spoofedExtensionsList.empty()) {
        if (index < g_spoofedExtensionsList.size()) {
            return reinterpret_cast<const GLubyte*>(g_spoofedExtensionsList[index].c_str());
        }
        return reinterpret_cast<const GLubyte*>("");
    }
    return orig_glGetStringi ? orig_glGetStringi(name, index) : nullptr;
}

static void (*orig_glGetIntegerv)(GLenum pname, GLint *data) = nullptr;
static void hooked_glGetIntegerv(GLenum pname, GLint *data) {
    if (!data) return;
    if (orig_glGetIntegerv) {
        orig_glGetIntegerv(pname, data);
    }
    if (g_config.enabled) {
        if (pname == GL_NUM_EXTENSIONS && !g_spoofedExtensionsList.empty()) {
            *data = static_cast<GLint>(g_spoofedExtensionsList.size());
        } else if (pname == GL_MAJOR_VERSION && g_config.targetGlMajor > 0) {
            *data = g_config.targetGlMajor;
        } else if (pname == GL_MINOR_VERSION && g_config.targetGlMajor > 0) {
            *data = g_config.targetGlMinor;
        }
    }
}

static void (*orig_glViewport)(GLint x, GLint y, GLsizei width, GLsizei height) = nullptr;
static void hooked_glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    if (g_config.enabled && g_config.resolutionScale > 0.0f && g_config.resolutionScale < 1.0f) {
        width = static_cast<GLsizei>(std::round(width * g_config.resolutionScale));
        height = static_cast<GLsizei>(std::round(height * g_config.resolutionScale));
    }
    if (orig_glViewport) {
        orig_glViewport(x, y, width, height);
    }
}

static void (*orig_glTexParameteri)(GLenum target, GLenum pname, GLint param) = nullptr;
static void hooked_glTexParameteri(GLenum target, GLenum pname, GLint param) {
    if (g_config.enabled && g_config.force16BitTextures && pname == GL_TEXTURE_MIN_FILTER) {
        param = GL_LINEAR;
    }
    if (orig_glTexParameteri) orig_glTexParameteri(target, pname, param);
}

// ---------------------------------------------------------------------------
// GLSL Shader Engine: Universal Skin Tone & Turf Protection
// ---------------------------------------------------------------------------
static std::string findFragOutputVar(const std::string &src) {
    if (src.find("gl_FragColor") != std::string::npos) {
        return "gl_FragColor";
    }
    static const std::regex layoutOutRegex(R"(layout\s*\([^)]*\)\s*out\s+(?:mediump\s+|highp\s+|lowp\s+)?vec4\s+([a-zA-Z0-9_]+))");
    std::smatch match;
    if (std::regex_search(src, match, layoutOutRegex)) {
        return match[1].str();
    }
    static const std::regex simpleOutRegex(R"(out\s+(?:mediump\s+|highp\s+|lowp\s+)?vec4\s+([a-zA-Z0-9_]+))");
    if (std::regex_search(src, match, simpleOutRegex)) {
        return match[1].str();
    }
    return "";
}

std::string injectGlslShaderEffect(const std::string &src) {
    std::string modified = src;

    if (g_config.forceFp16Shaders || g_vsyncUncapActive) {
        size_t pos = 0;
        while ((pos = modified.find("highp", pos)) != std::string::npos) {
            modified.replace(pos, 5, "mediump");
            pos += 7;
        }
    }

    const std::string &mode = g_config.shaderPreset;
    const bool hasShaderPreset = (!mode.empty() && mode != "None" && mode.find("None") != 0);
    if (!hasShaderPreset) {
        return modified;
    }

    std::string outVar = findFragOutputVar(modified);
    if (outVar.empty()) return modified;

    try {
        static const std::regex mainRegex(R"(\bvoid\s+main\s*\(\s*(?:void)?\s*\))");
        if (!std::regex_search(modified, mainRegex)) {
            return modified;
        }

        std::string effectCode;
        if (mode.find("DLSS 5") != std::string::npos) {
            effectCode =
                "    mediump float _glt_luma = dot(" + outVar + ".rgb, vec3(0.299, 0.587, 0.114));\n"
                "    mediump vec3 _glt_dlss = max(mix(vec3(_glt_luma), " + outVar + ".rgb, 1.32), vec3(0.0));\n"
                "    " + outVar + ".rgb = _glt_dlss * vec3(1.06, 1.07, 1.09);\n";
        } else if (mode.find("Vibrant HDR") != std::string::npos) {
            effectCode =
                "    mediump float _glt_luma = dot(" + outVar + ".rgb, vec3(0.299, 0.587, 0.114));\n"
                "    mediump vec3 _glt_sat = max(mix(vec3(_glt_luma), " + outVar + ".rgb, 1.36), vec3(0.0));\n"
                "    " + outVar + ".rgb = _glt_sat * 1.06;\n";
        } else if (mode.find("Golden Hour") != std::string::npos) {
            effectCode =
                "    mediump float _glt_luma = dot(" + outVar + ".rgb, vec3(0.299, 0.587, 0.114));\n"
                "    mediump vec3 _glt_warm = max(mix(vec3(_glt_luma), " + outVar + ".rgb, 1.25), vec3(0.0));\n"
                "    " + outVar + ".rgb = _glt_warm * vec3(1.12, 1.03, 0.88);\n";
        } else if (mode.find("ACES Filmic") != std::string::npos) {
            effectCode =
                "    mediump float _glt_luma = dot(" + outVar + ".rgb, vec3(0.299, 0.587, 0.114));\n"
                "    mediump vec3 _glt_film = max(mix(vec3(_glt_luma), " + outVar + ".rgb, 1.20), vec3(0.0));\n"
                "    " + outVar + ".rgb = _glt_film * vec3(1.08, 1.02, 0.94);\n";
        } else if (mode.find("Cyberpunk Neon") != std::string::npos) {
            effectCode =
                "    mediump float _glt_luma = dot(" + outVar + ".rgb, vec3(0.299, 0.587, 0.114));\n"
                "    mediump vec3 _glt_neon = max(mix(vec3(_glt_luma), " + outVar + ".rgb, 1.42), vec3(0.0));\n"
                "    " + outVar + ".rgb = _glt_neon * vec3(0.94, 1.02, 1.16);\n";
        } else if (mode.find("Cel-Shading") != std::string::npos) {
            effectCode =
                "    mediump float _glt_luma = max(dot(" + outVar + ".rgb, vec3(0.299, 0.587, 0.114)), 0.0001);\n"
                "    mediump float _glt_toon = (floor(_glt_luma * 6.0) + 0.5) / 6.0;\n"
                "    " + outVar + ".rgb = " + outVar + ".rgb * mix(1.0, _glt_toon / _glt_luma, 0.85);\n";
        } else if (mode.find("Night Vision") != std::string::npos) {
            effectCode =
                "    mediump float _glt_luma = max(dot(" + outVar + ".rgb, vec3(0.299, 0.587, 0.114)), 0.0);\n"
                "    " + outVar + ".rgb = vec3(0.12, 0.95, 0.22) * pow(_glt_luma, 0.75) * 1.35;\n";
        } else if (mode.find("Custom GLSL") != std::string::npos && !g_config.customGlsl.empty()) {
            std::string expr = g_config.customGlsl;
            size_t p = 0;
            while ((p = expr.find("color", p)) != std::string::npos) {
                expr.replace(p, 5, outVar);
                p += outVar.length();
            }
            effectCode = "    " + expr + "\n";
        }

        if (!effectCode.empty()) {
            modified = std::regex_replace(modified, mainRegex, "void _gltools_orig_main()", std::regex_constants::format_first_only);
            // Universal Normalized Chromaticity Skin & Turf Protection across ALL presets
            modified +=
                "\nvoid main() {\n"
                "    _gltools_orig_main();\n"
                "    mediump vec3 _glt_orig = " + outVar + ".rgb;\n"
                "    mediump float _glt_sum = _glt_orig.r + _glt_orig.g + _glt_orig.b + 0.0001;\n"
                "    mediump vec3 _glt_n = _glt_orig / _glt_sum;\n"
                "    mediump float _glt_sk = (_glt_n.r > _glt_n.g && _glt_n.g > _glt_n.b && (_glt_n.r - _glt_n.b) > 0.05 && _glt_n.b < 0.35) ? 1.0 : 0.0;\n"
                "    mediump float _glt_gr = (_glt_n.g > _glt_n.r && _glt_n.g > _glt_n.b) ? 1.0 : 0.0;\n"
                + effectCode +
                "    " + outVar + ".rgb = mix(" + outVar + ".rgb, _glt_orig * vec3(1.01, 1.00, 0.99), _glt_sk * 0.90);\n"
                "    " + outVar + ".rgb = mix(" + outVar + ".rgb, _glt_orig * vec3(0.75, 0.98, 0.82), _glt_gr * 0.45);\n"
                "}\n";
        }
    } catch (...) {
        return src;
    }

    return modified;
}

// ---------------------------------------------------------------------------
// Sleep Compression & VSync Uncapping
// ---------------------------------------------------------------------------
static int (*orig_usleep)(useconds_t usec) = nullptr;
static int hooked_usleep(useconds_t usec) {
    if (g_config.enabled && g_vsyncUncapActive && usec >= 1000 && usec <= 45000) {
        usec = 100;
    }
    return orig_usleep ? orig_usleep(usec) : usleep(usec);
}

static int (*orig_nanosleep)(const struct timespec *req, struct timespec *rem) = nullptr;
static int hooked_nanosleep(const struct timespec *req, struct timespec *rem) {
    if (g_config.enabled && g_vsyncUncapActive && req) {
        long ns = req->tv_nsec;
        if (req->tv_sec == 0 && ns >= 1000000L && ns <= 45000000L) {
            struct timespec fastReq = { 0, 100000L };
            return orig_nanosleep ? orig_nanosleep(&fastReq, rem) : nanosleep(&fastReq, rem);
        }
    }
    return orig_nanosleep ? orig_nanosleep(req, rem) : nanosleep(req, rem);
}

static EGLBoolean (*orig_eglSwapInterval)(EGLDisplay dpy, EGLint interval) = nullptr;
static EGLBoolean hooked_eglSwapInterval(EGLDisplay dpy, EGLint interval) {
    if (g_config.enabled && g_vsyncUncapActive) {
        interval = 0;
    }
    return orig_eglSwapInterval ? orig_eglSwapInterval(dpy, interval) : EGL_TRUE;
}

static EGLBoolean (*orig_eglSwapBuffers)(EGLDisplay dpy, EGLSurface surface) = nullptr;
static EGLBoolean hooked_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    if (g_config.enabled && g_vsyncUncapActive && orig_eglSwapInterval) {
        orig_eglSwapInterval(dpy, 0);
    }
    return orig_eglSwapBuffers ? orig_eglSwapBuffers(dpy, surface) : EGL_TRUE;
}

// ---------------------------------------------------------------------------
// System Properties Spoofing
// ---------------------------------------------------------------------------
static int (*orig_system_property_get)(const char *name, char *value) = nullptr;
static int hooked_system_property_get(const char *name, char *value) {
    if (g_config.enabled && name && value) {
        // Fake CPU Properties
        if (g_config.fakeCpuEnabled) {
            if (strcmp(name, "ro.soc.manufacturer") == 0) {
                std::string s = g_config.fakeCpuPlatform.find("mt") != std::string::npos ? "MediaTek" : "Qualcomm";
                strcpy(value, s.c_str());
                return s.length();
            }
            if (strcmp(name, "ro.soc.model") == 0) {
                std::string s = !g_config.fakeCpuHardware.empty() ? g_config.fakeCpuHardware : "SM8650";
                strcpy(value, s.c_str());
                return s.length();
            }
            if (strcmp(name, "ro.board.platform") == 0) {
                std::string s = !g_config.fakeCpuPlatform.empty() ? g_config.fakeCpuPlatform : "pineapple";
                strcpy(value, s.c_str());
                return s.length();
            }
            if (strcmp(name, "ro.hardware.chipname") == 0) {
                std::string s = !g_config.fakeCpuName.empty() ? g_config.fakeCpuName : "Qualcomm Snapdragon 8 Gen 3";
                strcpy(value, s.c_str());
                return s.length();
            }
            if (strcmp(name, "ro.hardware") == 0) {
                std::string s = g_config.fakeCpuPlatform.find("mt") != std::string::npos ? "mt6989" : "qcom";
                strcpy(value, s.c_str());
                return s.length();
            }
        }
        // GLES Version
        if (strcmp(name, "ro.opengles.version") == 0 && g_config.targetGlMajor > 0) {
            uint32_t ver = (g_config.targetGlMajor << 16) | (g_config.targetGlMinor << 8);
            sprintf(value, "%u", ver);
            return strlen(value);
        }
    }
    return orig_system_property_get ? orig_system_property_get(name, value) : 0;
}

// ---------------------------------------------------------------------------
// JNI Property Spoofing (Build.java)
// ---------------------------------------------------------------------------
static void setStaticStringField(JNIEnv *env, jclass clazz, const char *name, const char *value) {
    if (!env || !clazz || !name || !value) return;
    jfieldID fid = env->GetStaticFieldID(clazz, name, "Ljava/lang/String;");
    if (fid) {
        jstring jval = env->NewStringUTF(value);
        env->SetStaticObjectField(clazz, fid, jval);
        env->DeleteLocalRef(jval);
    } else {
        env->ExceptionClear();
    }
}

// ---------------------------------------------------------------------------
// Config Parser
// ---------------------------------------------------------------------------
bool parseConfigString(const std::string &content, GameConfig &outConfig) {
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        size_t eq = line.find("=");
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        if (key == "enabled") outConfig.enabled = (val == "true" || val == "1");
        else if (key == "packageName") outConfig.packageName = val;
        else if (key == "targetGlMajor") outConfig.targetGlMajor = std::atoi(val.c_str());
        else if (key == "targetGlMinor") outConfig.targetGlMinor = std::atoi(val.c_str());
        else if (key == "renderer") outConfig.renderer = val;
        else if (key == "vendor") outConfig.vendor = val;
        else if (key == "version") outConfig.version = val;
        else if (key == "extensions") {
            outConfig.extensions = val;
            outConfig.extensionList.clear();
            std::istringstream extStream(val);
            std::string ext;
            while (extStream >> ext) {
                if (!ext.empty()) outConfig.extensionList.push_back(ext);
            }
        }
        else if (key == "resolutionScale") outConfig.resolutionScale = std::atof(val.c_str());
        else if (key == "force16BitTextures") outConfig.force16BitTextures = (val == "true" || val == "1");
        else if (key == "forceFp16Shaders") outConfig.forceFp16Shaders = (val == "true" || val == "1");
        else if (key == "shaderPreset") outConfig.shaderPreset = val;
        else if (key == "customGlsl") outConfig.customGlsl = val;
        else if (key == "frameGenMode") outConfig.frameGenMode = val;
        // Fake CPU & Cores
        else if (key == "fakeCpuEnabled") outConfig.fakeCpuEnabled = (val == "true" || val == "1");
        else if (key == "fakeCpuPreset") outConfig.fakeCpuPreset = val;
        else if (key == "fakeCpuName") outConfig.fakeCpuName = val;
        else if (key == "fakeCpuHardware") outConfig.fakeCpuHardware = val;
        else if (key == "fakeCpuPlatform") outConfig.fakeCpuPlatform = val;
        else if (key == "fakeCpuCores") outConfig.fakeCpuCores = std::atoi(val.c_str());
    }
    return true;
}

void loadAppConfigFromCompanion(int socketFd, GameConfig &outConfig) {
    if (socketFd < 0) return;
    char buffer[8192];
    ssize_t bytesRead = read(socketFd, buffer, sizeof(buffer) - 1);
    if (bytesRead > 0) {
        buffer[bytesRead] = "\0";
        parseConfigString(buffer, outConfig);
    }
}

// ---------------------------------------------------------------------------
// PLT / GOT Hooking Engine
// ---------------------------------------------------------------------------
static void hookSymbolInElf(const char *libPath, const char *symbol, void *hookFunc, void **origFunc) {
    void *handle = dlopen(libPath, RTLD_NOLOAD);
    if (!handle) handle = dlopen(libPath, RTLD_NOW);
    if (!handle) return;

    void *sym = dlsym(handle, symbol);
    if (sym && origFunc && !*origFunc) {
        *origFunc = sym;
    }
}

void initHooks(const GameConfig &config, JNIEnv *env) {
    g_config = config;
    g_spoofedExtensionsStr = config.extensions;
    g_spoofedExtensionsList = config.extensionList;
    g_vsyncUncapActive = (config.frameGenMode == "2x (120 FPS)" || config.frameGenMode == "Uncapped");

    LOGI("Installing GLTools hooks for %s [FakeCPU: %s (%d cores) | GLES: %d.%d | Shader: %s]",
         config.packageName.c_str(),
         config.fakeCpuEnabled ? config.fakeCpuName.c_str() : "Disabled",
         config.fakeCpuCores,
         config.targetGlMajor, config.targetGlMinor,
         config.shaderPreset.c_str());

    // 1. Hook POSIX CPU & Cores in libc.so
    hookSymbolInElf("libc.so", "sysconf", (void*)hooked_sysconf, (void**)&orig_sysconf);
    hookSymbolInElf("libc.so", "get_nprocs", (void*)hooked_get_nprocs, (void**)&orig_get_nprocs);
    hookSymbolInElf("libc.so", "get_nprocs_conf", (void*)hooked_get_nprocs_conf, (void**)&orig_get_nprocs_conf);
    hookSymbolInElf("libc.so", "open", (void*)hooked_open, (void**)&orig_open);
    hookSymbolInElf("libc.so", "openat", (void*)hooked_openat, (void**)&orig_openat);
    hookSymbolInElf("libc.so", "fopen", (void*)hooked_fopen, (void**)&orig_fopen);
    hookSymbolInElf("libc.so", "usleep", (void*)hooked_usleep, (void**)&orig_usleep);
    hookSymbolInElf("libc.so", "nanosleep", (void*)hooked_nanosleep, (void**)&orig_nanosleep);
    hookSymbolInElf("libc.so", "__system_property_get", (void*)hooked_system_property_get, (void**)&orig_system_property_get);

    // 2. Hook GLES / EGL symbols
    hookSymbolInElf("libGLESv2.so", "glGetString", (void*)hooked_glGetString, (void**)&orig_glGetString);
    hookSymbolInElf("libGLESv3.so", "glGetString", (void*)hooked_glGetString, (void**)&orig_glGetString);
    hookSymbolInElf("libGLESv3.so", "glGetStringi", (void*)hooked_glGetStringi, (void**)&orig_glGetStringi);
    hookSymbolInElf("libGLESv2.so", "glGetIntegerv", (void*)hooked_glGetIntegerv, (void**)&orig_glGetIntegerv);
    hookSymbolInElf("libGLESv3.so", "glGetIntegerv", (void*)hooked_glGetIntegerv, (void**)&orig_glGetIntegerv);
    hookSymbolInElf("libGLESv2.so", "glViewport", (void*)hooked_glViewport, (void**)&orig_glViewport);
    hookSymbolInElf("libGLESv3.so", "glViewport", (void*)hooked_glViewport, (void**)&orig_glViewport);
    hookSymbolInElf("libGLESv2.so", "glTexParameteri", (void*)hooked_glTexParameteri, (void**)&orig_glTexParameteri);
    hookSymbolInElf("libEGL.so", "eglSwapBuffers", (void*)hooked_eglSwapBuffers, (void**)&orig_eglSwapBuffers);
    hookSymbolInElf("libEGL.so", "eglSwapInterval", (void*)hooked_eglSwapInterval, (void**)&orig_eglSwapInterval);

    // 3. Spoof Java Build properties via JNI
    if (env) {
        jclass buildClass = env->FindClass("android/os/Build");
        if (buildClass) {
            if (config.fakeCpuEnabled) {
                std::string hw = config.fakeCpuPlatform.find("mt") != std::string::npos ? "mt6989" : "qcom";
                setStaticStringField(env, buildClass, "HARDWARE", hw.c_str());
                setStaticStringField(env, buildClass, "BOARD", config.fakeCpuPlatform.empty() ? "pineapple" : config.fakeCpuPlatform.c_str());
                setStaticStringField(env, buildClass, "SOC_MANUFACTURER", config.fakeCpuPlatform.find("mt") != std::string::npos ? "MediaTek" : "Qualcomm");
                setStaticStringField(env, buildClass, "SOC_MODEL", config.fakeCpuHardware.empty() ? "SM8650" : config.fakeCpuHardware.c_str());
            }
            env->DeleteLocalRef(buildClass);
        } else {
            env->ExceptionClear();
        }
    }
}

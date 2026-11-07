#include "hooks.h"

#include <dlfcn.h>
#include <elf.h>
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
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
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

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif

#ifndef EGL_CONTEXT_MAJOR_VERSION_KHR
#define EGL_CONTEXT_MAJOR_VERSION_KHR 0x3098
#endif
#ifndef EGL_CONTEXT_CLIENT_VERSION
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#endif
#ifndef EGL_BAD_ATTRIBUTE
#define EGL_BAD_ATTRIBUTE 0x3004
#endif
#ifndef EGL_NO_CONTEXT
#define EGL_NO_CONTEXT ((EGLContext)0)
#endif

#if defined(__LP64__)
typedef Elf64_Ehdr Elf_Ehdr;
typedef Elf64_Phdr Elf_Phdr;
typedef Elf64_Dyn  Elf_Dyn;
typedef Elf64_Sym  Elf_Sym;
typedef Elf64_Rela Elf_Rela;
typedef Elf64_Rel  Elf_Rel;
typedef Elf64_Addr Elf_Addr;
#define ELF_R_SYM(i) ELF64_R_SYM(i)
#else
typedef Elf32_Ehdr Elf_Ehdr;
typedef Elf32_Phdr Elf_Phdr;
typedef Elf32_Dyn  Elf_Dyn;
typedef Elf32_Sym  Elf_Sym;
typedef Elf32_Rela Elf_Rela;
typedef Elf32_Rel  Elf_Rel;
typedef Elf32_Addr Elf_Addr;
#define ELF_R_SYM(i) ELF32_R_SYM(i)
#endif

static GameConfig g_config;
static std::string g_spoofedExtensionsStr;
static std::vector<std::string> g_spoofedExtensionsList;
static bool g_vsyncUncapActive = false;
static std::mutex g_shaderMutex;
static std::unordered_map<GLuint, GLenum> g_shaderTypes;

static void refreshElfHooks();

// ---------------------------------------------------------------------------
// Helpers: Synthetic In-Memory File Descriptors for Fake CPU & Core Emulation
// ---------------------------------------------------------------------------
static std::string generateFakeCpuInfo() {
    std::string out;
    int cores = g_config.fakeCpuCores > 0 ? g_config.fakeCpuCores : 8;
    std::string hardware = !g_config.fakeCpuHardware.empty()
        ? g_config.fakeCpuHardware
        : "Qualcomm Technologies, Inc SM8650";
    std::string modelName = !g_config.fakeCpuName.empty()
        ? g_config.fakeCpuName
        : "Qualcomm Snapdragon 8 Gen 3";

    for (int i = 0; i < cores; ++i) {
        out += "processor\t: " + std::to_string(i) + "\n";
        out += "model name\t: " + modelName + "\n";
        out += "BogoMIPS\t: 38.40\n";
        out += "Features\t: fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp cpuid asimdrdm jscvt fcma lrcpc dcpop sha3 sm3 sm4 asimddp sha512 sve asimdfhm dit uscat ilrcpc flagm ssbs sb paca pacg dcpodp flagm2 frint\n";
        out += "CPU implementer\t: 0x51\n";
        out += "CPU architecture: 8\n";
        out += "CPU variant\t: 0x2\n";
        out += "CPU part\t: 0x001\n";
        out += "CPU revision\t: 1\n\n";
    }
    out += "Hardware\t: " + hardware + "\n";
    out += "Revision\t: 0000\n";
    out += "Serial\t\t: 0000000000000000\n";
    return out;
}

static std::string generateCpuRange() {
    int cores = g_config.fakeCpuCores > 0 ? g_config.fakeCpuCores : 8;
    if (cores == 1) return "0\n";
    return "0-" + std::to_string(cores - 1) + "\n";
}

static int createFakeStringFd(const std::string &data) {
#if defined(__NR_memfd_create)
    int fd = static_cast<int>(syscall(__NR_memfd_create, "fake_cpu", MFD_CLOEXEC));
    if (fd >= 0) {
        ssize_t w = write(fd, data.data(), data.size());
        (void)w;
        lseek(fd, 0, SEEK_SET);
        return fd;
    }
#endif
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) == 0) {
        ssize_t w = write(fds[1], data.data(), data.size());
        (void)w;
        close(fds[1]);
        return fds[0];
    }
    return -1;
}

// ---------------------------------------------------------------------------
// POSIX CPU & Cores Hooks (sysconf, get_nprocs, open, openat, fopen)
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
    if (orig_get_nprocs) return orig_get_nprocs();
    long online = sysconf(_SC_NPROCESSORS_ONLN);
    return online > 0 ? static_cast<int>(online) : 8;
}

static int (*orig_get_nprocs_conf)(void) = nullptr;
static int hooked_get_nprocs_conf(void) {
    if (g_config.enabled && g_config.fakeCpuEnabled && g_config.fakeCpuCores > 0) {
        return g_config.fakeCpuCores;
    }
    if (orig_get_nprocs_conf) return orig_get_nprocs_conf();
    long conf = sysconf(_SC_NPROCESSORS_CONF);
    return conf > 0 ? static_cast<int>(conf) : 8;
}

static int (*orig_open)(const char *pathname, int flags, ...) = nullptr;
static int hooked_open(const char *pathname, int flags, ...) {
    mode_t mode = 0;
    if (flags & (O_CREAT
#ifdef O_TMPFILE
        | O_TMPFILE
#endif
    )) {
        va_list args;
        va_start(args, flags);
        mode = static_cast<mode_t>(va_arg(args, int));
        va_end(args);
    }
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
static int hooked_openat(int dirfd, const char *pathname, int flags, ...) {
    mode_t mode = 0;
    if (flags & (O_CREAT
#ifdef O_TMPFILE
        | O_TMPFILE
#endif
    )) {
        va_list args;
        va_start(args, flags);
        mode = static_cast<mode_t>(va_arg(args, int));
        va_end(args);
    }
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
        x = static_cast<GLint>(std::round(x * g_config.resolutionScale));
        y = static_cast<GLint>(std::round(y * g_config.resolutionScale));
        width = static_cast<GLsizei>(std::round(width * g_config.resolutionScale));
        height = static_cast<GLsizei>(std::round(height * g_config.resolutionScale));
    }
    if (orig_glViewport) {
        orig_glViewport(x, y, width, height);
    }
}

static void (*orig_glTexParameteri)(GLenum target, GLenum pname, GLint param) = nullptr;
static void (*orig_glTexParameterf)(GLenum target, GLenum pname, GLfloat param) = nullptr;

static void hooked_glTexParameteri(GLenum target, GLenum pname, GLint param) {
    if (g_config.enabled && g_config.force16BitTextures && pname == GL_TEXTURE_MIN_FILTER) {
        param = GL_LINEAR;
    }
    if (orig_glTexParameteri) orig_glTexParameteri(target, pname, param);
    if (g_config.enabled && g_config.shaderPreset.find("DLSS 5") != std::string::npos && orig_glTexParameterf) {
        orig_glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, 16.0f);
    }
}

static void hooked_glTexParameterf(GLenum target, GLenum pname, GLfloat param) {
    if (g_config.enabled && g_config.shaderPreset.find("DLSS 5") != std::string::npos && pname == GL_TEXTURE_MAX_ANISOTROPY_EXT) {
        param = 16.0f;
    }
    if (orig_glTexParameterf) orig_glTexParameterf(target, pname, param);
}

// ---------------------------------------------------------------------------
// GLSL Shader Engine: Universal Skin Tone & Turf Protection
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// GLSL Shader Engine: Universal Skin Tone & Turf Protection
// ---------------------------------------------------------------------------
struct FragOutputInfo {
    bool valid = false;
    bool isGles2 = false; // true if gl_FragColor
    std::string originalDecl;
    std::string type;
    std::string varName;
};

static FragOutputInfo findFragOutputVar(const std::string &src) {
    FragOutputInfo info;

    // 1. Skip Depth shaders: depth-only and shadow passes must never have color grading applied
    if (src.find("gl_FragDepth") != std::string::npos ||
        src.find("gl_FragDepthEXT") != std::string::npos) {
        return info;
    }

    // 2. Skip MRT (Multi-Render Target) & G-Buffer passes (e.g. Normals, Specular, Velocity)
    if (src.find("gl_FragData") != std::string::npos) {
        return info;
    }
    static const std::regex mrtRegex(R"(layout\s*\([^)]*location\s*=\s*[1-9])");
    if (std::regex_search(src, mrtRegex)) {
        return info;
    }

    // 3. Search for GLES 3+ output declarations at global scope:
    // Case A: layout(location = 0, ...) out vec4 FragColor;
    static const std::regex layoutOutRegex(R"((layout\s*\([^)]*location\s*=\s*0[^)]*\)\s*out\s+((?:mediump\s+|highp\s+|lowp\s+)?vec4)\s+([a-zA-Z0-9_]+)\s*;))");
    std::smatch match;
    if (std::regex_search(src, match, layoutOutRegex)) {
        info.valid = true;
        info.isGles2 = false;
        info.originalDecl = match[1].str();
        info.type = match[2].str();
        info.varName = match[3].str();
        return info;
    }

    // Case B: out vec4 FragColor; (without layout, must end in semicolon to avoid matching function parameters!)
    static const std::regex simpleOutRegex(R"((\bout\s+((?:mediump\s+|highp\s+|lowp\s+)?vec4)\s+([a-zA-Z0-9_]+)\s*;))");
    if (std::regex_search(src, match, simpleOutRegex)) {
        info.valid = true;
        info.isGles2 = false;
        info.originalDecl = match[1].str();
        info.type = match[2].str();
        info.varName = match[3].str();
        return info;
    }

    // 4. Search for GLES 2.0 output: gl_FragColor (only if not #version 300+)
    if (src.find("gl_FragColor") != std::string::npos &&
        src.find("#version 3") == std::string::npos) {
        info.valid = true;
        info.isGles2 = true;
        info.type = "vec4";
        info.varName = "gl_FragColor";
        return info;
    }

    return info;
}

std::string injectGlslShaderEffect(const std::string &src) {
    std::string modified = src;

    if (g_config.forceFp16Shaders) {
        static const std::regex highpRegex(R"(\bhighp\b)");
        modified = std::regex_replace(modified, highpRegex, "mediump");
    }

    const std::string &mode = g_config.shaderPreset;
    const bool hasShaderPreset = (!mode.empty() && mode != "None" && mode.find("None") != 0);
    if (!hasShaderPreset) {
        return modified;
    }

    FragOutputInfo outInfo = findFragOutputVar(modified);
    if (!outInfo.valid) return modified;

    try {
        // Match the real function definition of main() with opening brace '{'
        static const std::regex mainRegex(R"(\bvoid\s+main\s*\(\s*(?:void)?\s*\)\s*\{)");
        if (!std::regex_search(modified, mainRegex)) {
            return modified;
        }

        std::string targetVar = outInfo.isGles2 ? "_glt_fake_fragcolor" : outInfo.varName;
        std::string effectCode;

        if (mode.find("DLSS 5") != std::string::npos) {
            effectCode =
                "    mediump float _glt_luma = dot(" + targetVar + ".rgb, vec3(0.299, 0.587, 0.114));\n"
                "    mediump vec3 _glt_dlss = max(mix(vec3(_glt_luma), " + targetVar + ".rgb, 1.32), vec3(0.0));\n"
                "    " + targetVar + ".rgb = _glt_dlss * vec3(1.06, 1.07, 1.09);\n";
        } else if (mode.find("Vibrant HDR") != std::string::npos) {
            effectCode =
                "    mediump float _glt_luma = dot(" + targetVar + ".rgb, vec3(0.299, 0.587, 0.114));\n"
                "    mediump vec3 _glt_sat = max(mix(vec3(_glt_luma), " + targetVar + ".rgb, 1.36), vec3(0.0));\n"
                "    " + targetVar + ".rgb = _glt_sat * 1.06;\n";
        } else if (mode.find("Golden Hour") != std::string::npos) {
            effectCode =
                "    mediump float _glt_luma = dot(" + targetVar + ".rgb, vec3(0.299, 0.587, 0.114));\n"
                "    mediump vec3 _glt_warm = max(mix(vec3(_glt_luma), " + targetVar + ".rgb, 1.25), vec3(0.0));\n"
                "    " + targetVar + ".rgb = _glt_warm * vec3(1.12, 1.03, 0.88);\n";
        } else if (mode.find("ACES Filmic") != std::string::npos) {
            effectCode =
                "    mediump float _glt_luma = dot(" + targetVar + ".rgb, vec3(0.299, 0.587, 0.114));\n"
                "    mediump vec3 _glt_film = max(mix(vec3(_glt_luma), " + targetVar + ".rgb, 1.20), vec3(0.0));\n"
                "    " + targetVar + ".rgb = _glt_film * vec3(1.08, 1.02, 0.94);\n";
        } else if (mode.find("Cyberpunk Neon") != std::string::npos) {
            effectCode =
                "    mediump float _glt_luma = dot(" + targetVar + ".rgb, vec3(0.299, 0.587, 0.114));\n"
                "    mediump vec3 _glt_neon = max(mix(vec3(_glt_luma), " + targetVar + ".rgb, 1.42), vec3(0.0));\n"
                "    " + targetVar + ".rgb = _glt_neon * vec3(0.94, 1.02, 1.16);\n";
        } else if (mode.find("Cel-Shading") != std::string::npos) {
            effectCode =
                "    mediump float _glt_luma = max(dot(" + targetVar + ".rgb, vec3(0.299, 0.587, 0.114)), 0.0001);\n"
                "    mediump float _glt_toon = (floor(_glt_luma * 6.0) + 0.5) / 6.0;\n"
                "    " + targetVar + ".rgb = " + targetVar + ".rgb * mix(1.0, _glt_toon / _glt_luma, 0.85);\n";
        } else if (mode.find("Night Vision") != std::string::npos) {
            effectCode =
                "    mediump float _glt_luma = max(dot(" + targetVar + ".rgb, vec3(0.299, 0.587, 0.114)), 0.0);\n"
                "    " + targetVar + ".rgb = vec3(0.12, 0.95, 0.22) * pow(_glt_luma, 0.75) * 1.35;\n";
        } else if (mode.find("Custom GLSL") != std::string::npos && !g_config.customGlsl.empty()) {
            std::string expr = g_config.customGlsl;
            static const std::regex colorRegex(R"(\bcolor\b)");
            expr = std::regex_replace(expr, colorRegex, targetVar);
            effectCode = "    " + expr + "\n";
        }

        if (effectCode.empty()) return modified;

        if (outInfo.isGles2) {
            // GLES 2.0: Redirect gl_FragColor to an in-memory variable so it is NEVER read from hardware register!
            std::string origMainReplacement =
                "vec4 _glt_fake_fragcolor = vec4(0.0, 0.0, 0.0, 1.0);\n"
                "#define gl_FragColor _glt_fake_fragcolor\n"
                "void _gltools_orig_main() {";
            modified = std::regex_replace(modified, mainRegex, origMainReplacement, std::regex_constants::format_first_only);

            modified +=
                "\n#undef gl_FragColor\n"
                "void main() {\n"
                "    _gltools_orig_main();\n"
                "    mediump vec3 _glt_orig = _glt_fake_fragcolor.rgb;\n"
                "    mediump float _glt_sum = _glt_orig.r + _glt_orig.g + _glt_orig.b + 0.0001;\n"
                "    mediump vec3 _glt_n = _glt_orig / _glt_sum;\n"
                "    mediump float _glt_sk = (_glt_n.r > _glt_n.g && _glt_n.g > _glt_n.b && (_glt_n.r - _glt_n.b) > 0.05 && _glt_n.b < 0.35) ? 1.0 : 0.0;\n"
                "    mediump float _glt_gr = (_glt_n.g > _glt_n.r && _glt_n.g > _glt_n.b) ? 1.0 : 0.0;\n"
                + effectCode +
                "    _glt_fake_fragcolor.rgb = mix(_glt_fake_fragcolor.rgb, _glt_orig * vec3(1.01, 1.00, 0.99), _glt_sk * 0.90);\n"
                "    _glt_fake_fragcolor.rgb = mix(_glt_fake_fragcolor.rgb, _glt_orig * vec3(0.75, 0.98, 0.82), _glt_gr * 0.45);\n"
                "    _glt_fake_fragcolor.rgb = clamp(_glt_fake_fragcolor.rgb, 0.0, 1.0);\n"
                "    gl_FragColor = _glt_fake_fragcolor;\n"
                "}\n";
        } else {
            // GLES 3.0+: Replace the write-only 'out' variable declaration with a readable global variable
            // and assign once to '_glt_real_out' at the very end of main()!
            std::string declReplacement;
            if (outInfo.originalDecl.find("layout") != std::string::npos) {
                declReplacement = "layout(location = 0) out " + outInfo.type + " _glt_real_out;\n" +
                                  outInfo.type + " " + outInfo.varName + " = " + outInfo.type + "(0.0, 0.0, 0.0, 1.0);";
            } else {
                declReplacement = "out " + outInfo.type + " _glt_real_out;\n" +
                                  outInfo.type + " " + outInfo.varName + " = " + outInfo.type + "(0.0, 0.0, 0.0, 1.0);";
            }

            size_t declPos = modified.find(outInfo.originalDecl);
            if (declPos != std::string::npos) {
                modified.replace(declPos, outInfo.originalDecl.length(), declReplacement);
            }

            modified = std::regex_replace(modified, mainRegex, "void _gltools_orig_main() {", std::regex_constants::format_first_only);

            modified +=
                "\nvoid main() {\n"
                "    _gltools_orig_main();\n"
                "    mediump vec3 _glt_orig = " + outInfo.varName + ".rgb;\n"
                "    mediump float _glt_sum = _glt_orig.r + _glt_orig.g + _glt_orig.b + 0.0001;\n"
                "    mediump vec3 _glt_n = _glt_orig / _glt_sum;\n"
                "    mediump float _glt_sk = (_glt_n.r > _glt_n.g && _glt_n.g > _glt_n.b && (_glt_n.r - _glt_n.b) > 0.05 && _glt_n.b < 0.35) ? 1.0 : 0.0;\n"
                "    mediump float _glt_gr = (_glt_n.g > _glt_n.r && _glt_n.g > _glt_n.b) ? 1.0 : 0.0;\n"
                + effectCode +
                "    " + outInfo.varName + ".rgb = mix(" + outInfo.varName + ".rgb, _glt_orig * vec3(1.01, 1.00, 0.99), _glt_sk * 0.90);\n"
                "    " + outInfo.varName + ".rgb = mix(" + outInfo.varName + ".rgb, _glt_orig * vec3(0.75, 0.98, 0.82), _glt_gr * 0.45);\n"
                "    " + outInfo.varName + ".rgb = clamp(" + outInfo.varName + ".rgb, 0.0, 1.0);\n"
                "    _glt_real_out = " + outInfo.varName + ";\n"
                "}\n";
        }
    } catch (...) {
        return src;
    }

    return modified;
}

static GLuint (*orig_glCreateShader)(GLenum type) = nullptr;
static GLuint hooked_glCreateShader(GLenum type) {
    GLuint id = orig_glCreateShader ? orig_glCreateShader(type) : 0;
    if (id != 0) {
        std::lock_guard<std::mutex> lock(g_shaderMutex);
        g_shaderTypes[id] = type;
    }
    return id;
}

static void (*orig_glCompileShader)(GLuint shader) = nullptr;
static void hooked_glCompileShader(GLuint shader) {
    if (orig_glCompileShader) {
        orig_glCompileShader(shader);
    }
    if (g_config.enabled) {
        typedef void (*PFN_glGetShaderiv)(GLuint, GLenum, GLint*);
        static PFN_glGetShaderiv p_glGetShaderiv = nullptr;
        if (!p_glGetShaderiv) {
            p_glGetShaderiv = (PFN_glGetShaderiv)dlsym(RTLD_DEFAULT, "glGetShaderiv");
        }
        typedef void (*PFN_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
        static PFN_glGetShaderInfoLog p_glGetShaderInfoLog = nullptr;
        if (!p_glGetShaderInfoLog) {
            p_glGetShaderInfoLog = (PFN_glGetShaderInfoLog)dlsym(RTLD_DEFAULT, "glGetShaderInfoLog");
        }
        if (p_glGetShaderiv && p_glGetShaderInfoLog) {
            GLint status = 0;
            p_glGetShaderiv(shader, 0x8B81 /* GL_COMPILE_STATUS */, &status);
            if (status == 0 /* GL_FALSE */) {
                char infoLog[2048] = {0};
                p_glGetShaderInfoLog(shader, sizeof(infoLog) - 1, nullptr, infoLog);
                LOGE("Shader compilation FAILED: %s", infoLog);
            }
        }
    }
}

static void (*orig_glShaderSource)(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length) = nullptr;
static void hooked_glShaderSource(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length) {
    if (!orig_glShaderSource) return;
    if (!g_config.enabled || !string || count <= 0) {
        orig_glShaderSource(shader, count, string, length);
        return;
    }

    GLenum shaderType = 0;
    {
        std::lock_guard<std::mutex> lock(g_shaderMutex);
        auto it = g_shaderTypes.find(shader);
        if (it != g_shaderTypes.end()) shaderType = it->second;
    }

    if (shaderType == 0) {
        typedef void (*PFN_glGetShaderiv)(GLuint, GLenum, GLint*);
        static PFN_glGetShaderiv p_glGetShaderiv = nullptr;
        if (!p_glGetShaderiv) {
            p_glGetShaderiv = (PFN_glGetShaderiv)dlsym(RTLD_DEFAULT, "glGetShaderiv");
        }
        if (p_glGetShaderiv) {
            GLint type = 0;
            p_glGetShaderiv(shader, 0x8B4F /* GL_SHADER_TYPE */, &type);
            shaderType = static_cast<GLenum>(type);
        }
    }

    if (shaderType != 0x8B30 /* GL_FRAGMENT_SHADER */) {
        orig_glShaderSource(shader, count, string, length);
        return;
    }

    std::string combined;
    for (GLsizei i = 0; i < count; ++i) {
        if (!string[i]) continue;
        if (length && length[i] >= 0) {
            combined.append(string[i], static_cast<size_t>(length[i]));
        } else {
            combined.append(string[i]);
        }
    }

    std::string patched = injectGlslShaderEffect(combined);
    const GLchar *patchedPtr = patched.c_str();
    GLint patchedLen = static_cast<GLint>(patched.size());
    orig_glShaderSource(shader, 1, &patchedPtr, &patchedLen);
}

// ---------------------------------------------------------------------------
// Sleep Compression, VSync Uncapping & EGL Context Spoofing
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

static EGLContext (*orig_eglCreateContext)(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list) = nullptr;
static EGLContext hooked_eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list) {
    if (g_config.enabled && g_config.targetGlMajor == 2 && attrib_list) {
        for (int i = 0; attrib_list[i] != 0x3038 /* EGL_NONE */; i += 2) {
            if (attrib_list[i] == EGL_CONTEXT_CLIENT_VERSION && attrib_list[i + 1] >= 3) {
                return EGL_NO_CONTEXT;
            }
        }
    }
    return orig_eglCreateContext ? orig_eglCreateContext(dpy, config, share_context, attrib_list) : EGL_NO_CONTEXT;
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

static void* (*orig_eglGetProcAddress)(const char *procname) = nullptr;
static void* hooked_eglGetProcAddress(const char *procname) {
    if (g_config.enabled && procname) {
        if (strcmp(procname, "glGetString") == 0) return (void*)hooked_glGetString;
        if (strcmp(procname, "glGetStringi") == 0) return (void*)hooked_glGetStringi;
        if (strcmp(procname, "glGetIntegerv") == 0) return (void*)hooked_glGetIntegerv;
        if (strcmp(procname, "glViewport") == 0) return (void*)hooked_glViewport;
        if (strcmp(procname, "glCreateShader") == 0) return (void*)hooked_glCreateShader;
        if (strcmp(procname, "glCompileShader") == 0) return (void*)hooked_glCompileShader;
        if (strcmp(procname, "glShaderSource") == 0) return (void*)hooked_glShaderSource;
        if (strcmp(procname, "glTexParameteri") == 0) return (void*)hooked_glTexParameteri;
        if (strcmp(procname, "glTexParameterf") == 0) return (void*)hooked_glTexParameterf;
        if (strcmp(procname, "eglCreateContext") == 0) return (void*)hooked_eglCreateContext;
        if (strcmp(procname, "eglSwapBuffers") == 0) return (void*)hooked_eglSwapBuffers;
        if (strcmp(procname, "eglSwapInterval") == 0) return (void*)hooked_eglSwapInterval;
    }
    return orig_eglGetProcAddress ? orig_eglGetProcAddress(procname) : nullptr;
}

// ---------------------------------------------------------------------------
// Dynamic Loader Hooks (dlsym, dlopen, android_dlopen_ext) for Late Engines
// ---------------------------------------------------------------------------
static void* (*orig_dlsym)(void *handle, const char *symbol) = nullptr;
static void* hooked_dlsym(void *handle, const char *symbol) {
    if (g_config.enabled && symbol) {
        if (strcmp(symbol, "glGetString") == 0) return (void*)hooked_glGetString;
        if (strcmp(symbol, "glGetStringi") == 0) return (void*)hooked_glGetStringi;
        if (strcmp(symbol, "glGetIntegerv") == 0) return (void*)hooked_glGetIntegerv;
        if (strcmp(symbol, "glViewport") == 0) return (void*)hooked_glViewport;
        if (strcmp(symbol, "glCreateShader") == 0) return (void*)hooked_glCreateShader;
        if (strcmp(symbol, "glCompileShader") == 0) return (void*)hooked_glCompileShader;
        if (strcmp(symbol, "glShaderSource") == 0) return (void*)hooked_glShaderSource;
        if (strcmp(symbol, "glTexParameteri") == 0) return (void*)hooked_glTexParameteri;
        if (strcmp(symbol, "glTexParameterf") == 0) return (void*)hooked_glTexParameterf;
        if (strcmp(symbol, "eglCreateContext") == 0) return (void*)hooked_eglCreateContext;
        if (strcmp(symbol, "eglSwapBuffers") == 0) return (void*)hooked_eglSwapBuffers;
        if (strcmp(symbol, "eglSwapInterval") == 0) return (void*)hooked_eglSwapInterval;
        if (strcmp(symbol, "eglGetProcAddress") == 0) return (void*)hooked_eglGetProcAddress;
        if (strcmp(symbol, "sysconf") == 0) return (void*)hooked_sysconf;
        if (strcmp(symbol, "get_nprocs") == 0) return (void*)hooked_get_nprocs;
        if (strcmp(symbol, "get_nprocs_conf") == 0) return (void*)hooked_get_nprocs_conf;
    }
    return orig_dlsym ? orig_dlsym(handle, symbol) : nullptr;
}

static void* (*orig_dlopen)(const char *filename, int flags) = nullptr;
static void* hooked_dlopen(const char *filename, int flags) {
    void *res = orig_dlopen ? orig_dlopen(filename, flags) : nullptr;
    if (res && g_config.enabled) {
        refreshElfHooks();
    }
    return res;
}

static void* (*orig_android_dlopen_ext)(const char *filename, int flags, const void *extinfo) = nullptr;
static void* hooked_android_dlopen_ext(const char *filename, int flags, const void *extinfo) {
    void *res = orig_android_dlopen_ext ? orig_android_dlopen_ext(filename, flags, extinfo) : nullptr;
    if (res && g_config.enabled) {
        refreshElfHooks();
    }
    return res;
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
                return static_cast<int>(s.length());
            }
            if (strcmp(name, "ro.soc.model") == 0) {
                std::string s = !g_config.fakeCpuHardware.empty() ? g_config.fakeCpuHardware : "SM8650";
                strcpy(value, s.c_str());
                return static_cast<int>(s.length());
            }
            if (strcmp(name, "ro.board.platform") == 0) {
                std::string s = !g_config.fakeCpuPlatform.empty() ? g_config.fakeCpuPlatform : "pineapple";
                strcpy(value, s.c_str());
                return static_cast<int>(s.length());
            }
            if (strcmp(name, "ro.hardware.chipname") == 0) {
                std::string s = !g_config.fakeCpuName.empty() ? g_config.fakeCpuName : "Qualcomm Snapdragon 8 Gen 3";
                strcpy(value, s.c_str());
                return static_cast<int>(s.length());
            }
            if (strcmp(name, "ro.hardware") == 0) {
                std::string s = g_config.fakeCpuPlatform.find("mt") != std::string::npos ? "mt6989" : "qcom";
                strcpy(value, s.c_str());
                return static_cast<int>(s.length());
            }
        }
        // GLES Version
        if (strcmp(name, "ro.opengles.version") == 0 && g_config.targetGlMajor > 0) {
            uint32_t ver = (static_cast<uint32_t>(g_config.targetGlMajor) << 16) |
                           (static_cast<uint32_t>(g_config.targetGlMinor) << 8);
            sprintf(value, "%u", ver);
            return static_cast<int>(strlen(value));
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
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (!val.empty() && val.back() == '\r') val.pop_back();

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
        else if (key == "resolutionScale") outConfig.resolutionScale = static_cast<float>(std::atof(val.c_str()));
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
    std::string content;
    char buffer[4096];
    ssize_t bytesRead;
    while ((bytesRead = read(socketFd, buffer, sizeof(buffer))) > 0) {
        content.append(buffer, static_cast<size_t>(bytesRead));
    }
    if (!content.empty()) {
        parseConfigString(content, outConfig);
    }
}

// ---------------------------------------------------------------------------
// PLT / GOT Relocation Patcher & Zygisk PLT Hooking Engine
// ---------------------------------------------------------------------------
struct PltHookTarget {
    const char *symbol;
    void *newFunc;
    void **oldFunc;
};

static const PltHookTarget g_hookTargets[] = {
    {"sysconf", (void*)hooked_sysconf, (void**)&orig_sysconf},
    {"get_nprocs", (void*)hooked_get_nprocs, (void**)&orig_get_nprocs},
    {"get_nprocs_conf", (void*)hooked_get_nprocs_conf, (void**)&orig_get_nprocs_conf},
    {"open", (void*)hooked_open, (void**)&orig_open},
    {"openat", (void*)hooked_openat, (void**)&orig_openat},
    {"fopen", (void*)hooked_fopen, (void**)&orig_fopen},
    {"usleep", (void*)hooked_usleep, (void**)&orig_usleep},
    {"nanosleep", (void*)hooked_nanosleep, (void**)&orig_nanosleep},
    {"__system_property_get", (void*)hooked_system_property_get, (void**)&orig_system_property_get},
    {"glGetString", (void*)hooked_glGetString, (void**)&orig_glGetString},
    {"glGetStringi", (void*)hooked_glGetStringi, (void**)&orig_glGetStringi},
    {"glGetIntegerv", (void*)hooked_glGetIntegerv, (void**)&orig_glGetIntegerv},
    {"glViewport", (void*)hooked_glViewport, (void**)&orig_glViewport},
    {"glCreateShader", (void*)hooked_glCreateShader, (void**)&orig_glCreateShader},
    {"glCompileShader", (void*)hooked_glCompileShader, (void**)&orig_glCompileShader},
    {"glShaderSource", (void*)hooked_glShaderSource, (void**)&orig_glShaderSource},
    {"glTexParameteri", (void*)hooked_glTexParameteri, (void**)&orig_glTexParameteri},
    {"glTexParameterf", (void*)hooked_glTexParameterf, (void**)&orig_glTexParameterf},
    {"eglCreateContext", (void*)hooked_eglCreateContext, (void**)&orig_eglCreateContext},
    {"eglSwapBuffers", (void*)hooked_eglSwapBuffers, (void**)&orig_eglSwapBuffers},
    {"eglSwapInterval", (void*)hooked_eglSwapInterval, (void**)&orig_eglSwapInterval},
    {"eglGetProcAddress", (void*)hooked_eglGetProcAddress, (void**)&orig_eglGetProcAddress},
    {"dlsym", (void*)hooked_dlsym, (void**)&orig_dlsym},
    {"dlopen", (void*)hooked_dlopen, (void**)&orig_dlopen},
    {"android_dlopen_ext", (void*)hooked_android_dlopen_ext, (void**)&orig_android_dlopen_ext},
};

static void patchGotSlot(Elf_Addr *slot, void *newFunc, void **oldFunc) {
    if (!slot || *slot == reinterpret_cast<Elf_Addr>(newFunc)) return;
    uintptr_t pageSize = static_cast<uintptr_t>(sysconf(_SC_PAGESIZE));
    uintptr_t pageStart = reinterpret_cast<uintptr_t>(slot) & ~(pageSize - 1);
    if (mprotect(reinterpret_cast<void*>(pageStart), pageSize, PROT_READ | PROT_WRITE) == 0) {
        if (oldFunc && !*oldFunc && *slot != 0) {
            *oldFunc = reinterpret_cast<void*>(*slot);
        }
        *slot = reinterpret_cast<Elf_Addr>(newFunc);
    }
}

static int elfRelocPatchCallback(struct dl_phdr_info *info, [[maybe_unused]] size_t size, [[maybe_unused]] void *data) {
    if (!info->dlpi_name || info->dlpi_name[0] == '\0') return 0;
    if (strstr(info->dlpi_name, "libzygisk_gltools.so") != nullptr ||
        strstr(info->dlpi_name, "ld-android.so") != nullptr ||
        strstr(info->dlpi_name, "linker") != nullptr) {
        return 0;
    }

    const Elf_Dyn *dyn = nullptr;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
            dyn = reinterpret_cast<const Elf_Dyn*>(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
            break;
        }
    }
    if (!dyn) return 0;

    const Elf_Sym *symtab = nullptr;
    const char *strtab = nullptr;
    const void *jmprel = nullptr;
    size_t pltrelsz = 0;
    int pltrelType = 0;

    for (; dyn->d_tag != DT_NULL; ++dyn) {
        switch (dyn->d_tag) {
            case DT_SYMTAB: symtab = reinterpret_cast<const Elf_Sym*>(dyn->d_un.d_ptr > info->dlpi_addr ? dyn->d_un.d_ptr : info->dlpi_addr + dyn->d_un.d_ptr); break;
            case DT_STRTAB: strtab = reinterpret_cast<const char*>(dyn->d_un.d_ptr > info->dlpi_addr ? dyn->d_un.d_ptr : info->dlpi_addr + dyn->d_un.d_ptr); break;
            case DT_JMPREL: jmprel = reinterpret_cast<const void*>(dyn->d_un.d_ptr > info->dlpi_addr ? dyn->d_un.d_ptr : info->dlpi_addr + dyn->d_un.d_ptr); break;
            case DT_PLTRELSZ: pltrelsz = dyn->d_un.d_val; break;
            case DT_PLTREL: pltrelType = static_cast<int>(dyn->d_un.d_val); break;
        }
    }

    if (!symtab || !strtab || !jmprel || pltrelsz == 0) return 0;

    if (pltrelType == DT_RELA) {
        const auto *rela = static_cast<const Elf_Rela*>(jmprel);
        size_t count = pltrelsz / sizeof(Elf_Rela);
        for (size_t i = 0; i < count; ++i) {
            size_t symIdx = ELF_R_SYM(rela[i].r_info);
            if (symIdx == 0) continue;
            const char *symName = strtab + symtab[symIdx].st_name;
            for (const auto &t : g_hookTargets) {
                if (strcmp(symName, t.symbol) == 0) {
                    auto *slot = reinterpret_cast<Elf_Addr*>(info->dlpi_addr + rela[i].r_offset);
                    patchGotSlot(slot, t.newFunc, t.oldFunc);
                    break;
                }
            }
        }
    } else if (pltrelType == DT_REL) {
        const auto *rel = static_cast<const Elf_Rel*>(jmprel);
        size_t count = pltrelsz / sizeof(Elf_Rel);
        for (size_t i = 0; i < count; ++i) {
            size_t symIdx = ELF_R_SYM(rel[i].r_info);
            if (symIdx == 0) continue;
            const char *symName = strtab + symtab[symIdx].st_name;
            for (const auto &t : g_hookTargets) {
                if (strcmp(symName, t.symbol) == 0) {
                    auto *slot = reinterpret_cast<Elf_Addr*>(info->dlpi_addr + rel[i].r_offset);
                    patchGotSlot(slot, t.newFunc, t.oldFunc);
                    break;
                }
            }
        }
    }
    return 0;
}

static void refreshElfHooks() {
    dl_iterate_phdr(elfRelocPatchCallback, nullptr);
}

static int zygiskPhdrCallback(struct dl_phdr_info *info, [[maybe_unused]] size_t size, void *data) {
    if (!info->dlpi_name || info->dlpi_name[0] == '\0') return 0;
    if (strstr(info->dlpi_name, "libzygisk_gltools.so") != nullptr) return 0;

    auto *api = static_cast<zygisk::Api*>(data);
    struct stat st{};
    if (stat(info->dlpi_name, &st) != 0) return 0;

    for (const auto &t : g_hookTargets) {
        api->pltHookRegister(st.st_dev, st.st_ino, t.symbol, t.newFunc, t.oldFunc);
    }
    return 0;
}

static void resolveDefaultOriginals() {
    void *libdl = dlopen("libdl.so", RTLD_NOLOAD);
    if (!libdl) libdl = dlopen("libdl.so", RTLD_NOW);
    if (libdl) {
        if (!orig_dlsym) orig_dlsym = (decltype(orig_dlsym))dlsym(libdl, "dlsym");
        if (!orig_dlopen) orig_dlopen = (decltype(orig_dlopen))dlsym(libdl, "dlopen");
        if (!orig_android_dlopen_ext) orig_android_dlopen_ext = (decltype(orig_android_dlopen_ext))dlsym(libdl, "android_dlopen_ext");
    }

    void *libc = dlopen("libc.so", RTLD_NOLOAD);
    if (!libc) libc = dlopen("libc.so", RTLD_NOW);
    if (libc) {
        if (!orig_sysconf) orig_sysconf = (decltype(orig_sysconf))dlsym(libc, "sysconf");
        if (!orig_get_nprocs) orig_get_nprocs = (decltype(orig_get_nprocs))dlsym(libc, "get_nprocs");
        if (!orig_get_nprocs_conf) orig_get_nprocs_conf = (decltype(orig_get_nprocs_conf))dlsym(libc, "get_nprocs_conf");
        if (!orig_open) orig_open = (decltype(orig_open))dlsym(libc, "open");
        if (!orig_openat) orig_openat = (decltype(orig_openat))dlsym(libc, "openat");
        if (!orig_fopen) orig_fopen = (decltype(orig_fopen))dlsym(libc, "fopen");
        if (!orig_usleep) orig_usleep = (decltype(orig_usleep))dlsym(libc, "usleep");
        if (!orig_nanosleep) orig_nanosleep = (decltype(orig_nanosleep))dlsym(libc, "nanosleep");
        if (!orig_system_property_get) orig_system_property_get = (decltype(orig_system_property_get))dlsym(libc, "__system_property_get");
    }

    void *gles = dlopen("libGLESv2.so", RTLD_NOLOAD);
    if (!gles) gles = dlopen("libGLESv2.so", RTLD_NOW);
    if (gles) {
        if (!orig_glGetString) orig_glGetString = (decltype(orig_glGetString))dlsym(gles, "glGetString");
        if (!orig_glGetStringi) orig_glGetStringi = (decltype(orig_glGetStringi))dlsym(gles, "glGetStringi");
        if (!orig_glGetIntegerv) orig_glGetIntegerv = (decltype(orig_glGetIntegerv))dlsym(gles, "glGetIntegerv");
        if (!orig_glViewport) orig_glViewport = (decltype(orig_glViewport))dlsym(gles, "glViewport");
        if (!orig_glCreateShader) orig_glCreateShader = (decltype(orig_glCreateShader))dlsym(gles, "glCreateShader");
        if (!orig_glCompileShader) orig_glCompileShader = (decltype(orig_glCompileShader))dlsym(gles, "glCompileShader");
        if (!orig_glShaderSource) orig_glShaderSource = (decltype(orig_glShaderSource))dlsym(gles, "glShaderSource");
        if (!orig_glTexParameteri) orig_glTexParameteri = (decltype(orig_glTexParameteri))dlsym(gles, "glTexParameteri");
        if (!orig_glTexParameterf) orig_glTexParameterf = (decltype(orig_glTexParameterf))dlsym(gles, "glTexParameterf");
    }

    void *egl = dlopen("libEGL.so", RTLD_NOLOAD);
    if (!egl) egl = dlopen("libEGL.so", RTLD_NOW);
    if (egl) {
        if (!orig_eglCreateContext) orig_eglCreateContext = (decltype(orig_eglCreateContext))dlsym(egl, "eglCreateContext");
        if (!orig_eglSwapBuffers) orig_eglSwapBuffers = (decltype(orig_eglSwapBuffers))dlsym(egl, "eglSwapBuffers");
        if (!orig_eglSwapInterval) orig_eglSwapInterval = (decltype(orig_eglSwapInterval))dlsym(egl, "eglSwapInterval");
        if (!orig_eglGetProcAddress) orig_eglGetProcAddress = (decltype(orig_eglGetProcAddress))dlsym(egl, "eglGetProcAddress");
    }
}

void initHooks(const GameConfig &config, zygisk::Api *api, JNIEnv *env) {
    g_config = config;
    g_spoofedExtensionsStr = config.extensions;
    g_spoofedExtensionsList = config.extensionList;
    g_vsyncUncapActive = (config.frameGenMode == "2x (120 FPS)" || config.frameGenMode == "Uncapped");

    resolveDefaultOriginals();

    if (api) {
        dl_iterate_phdr(zygiskPhdrCallback, api);
        api->pltHookCommit();
    }
    refreshElfHooks();

    // Spoof Java Build properties via JNI
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

    LOGI("GLTools hooks installed for %s [FakeCPU: %s (%d cores) | GLES: %d.%d | Shader: %s | FrameGen: %s]",
         config.packageName.c_str(),
         config.fakeCpuEnabled ? config.fakeCpuName.c_str() : "Disabled",
         config.fakeCpuCores,
         config.targetGlMajor, config.targetGlMinor,
         config.shaderPreset.c_str(),
         config.frameGenMode.c_str());
}

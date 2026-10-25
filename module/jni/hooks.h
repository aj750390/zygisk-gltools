#pragma once

#include <string>
#include <GLES2/gl2.h>
#include <GLES3/gl3.h>
#include <jni.h>
#include "zygisk.hpp"

struct GlToolsConfig {
    bool enabled = false;
    std::string packageName;

    // OpenGL ES Strings
    std::string vendor = "Qualcomm";
    std::string renderer = "Adreno (TM) 750";
    std::string version = "OpenGL ES 3.2 V@0615.0 (GLTools-Zygisk)";
    std::string extensions = "";

    // System Properties / Device Emulation
    std::string model = "SM-S928B";
    std::string manufacturer = "samsung";
    std::string brand = "samsung";
    std::string device = "e3q";
    std::string board = "pineapple";
    std::string hardware = "qcom";

    // Optimization Settings
    float downscaleFactor = 1.0f;
    int fpsLimit = 0;
    bool optimizeTextures = false;
};

bool parseConfigContent(const std::string &content, const char *packageName, GlToolsConfig &outConfig);
bool loadAppConfigFromFile(const char *packageName, std::string &outRawContent);
void installGlHooks(zygisk::Api *api, JNIEnv *env, const GlToolsConfig &config);
void spoofJavaBuildProperties(JNIEnv *env, const GlToolsConfig &config);

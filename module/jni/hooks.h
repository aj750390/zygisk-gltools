#pragma once

#include <string>
#include <GLES2/gl2.h>
#include <GLES3/gl3.h>
#include "zygisk.hpp"

struct GlToolsConfig {
    bool enabled = false;
    std::string packageName;
    std::string vendor = "Qualcomm";
    std::string renderer = "Adreno (TM) 750";
    std::string version = "OpenGL ES 3.2 V@0615.0";
    std::string extensions = "";
    float downscaleFactor = 1.0f;
    int fpsLimit = 0;
    bool optimizeTextures = false;
};

void installGlHooks(zygisk::Api *api, const GlToolsConfig &config);
bool loadAppConfig(const char *packageName, GlToolsConfig &outConfig);

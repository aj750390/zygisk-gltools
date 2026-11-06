#pragma once

#include <string>
#include <vector>
#include <jni.h>

struct GameConfig {
    bool enabled = false;
    std::string packageName = "";
    int targetGlMajor = 0;
    int targetGlMinor = 0;
    std::string renderer = "";
    std::string vendor = "";
    std::string version = "";
    std::string extensions = "";
    std::vector<std::string> extensionList;
    float resolutionScale = 1.0f;
    bool force16BitTextures = false;
    bool forceFp16Shaders = false;
    std::string shaderPreset = "None";
    std::string customGlsl = "";
    std::string frameGenMode = "Off";

    // Fake CPU & Core Emulation (GLTools feature)
    bool fakeCpuEnabled = false;
    std::string fakeCpuPreset = "None";
    std::string fakeCpuName = "";
    std::string fakeCpuHardware = "";
    std::string fakeCpuPlatform = "";
    int fakeCpuCores = 8;
};

void initHooks(const GameConfig &config, JNIEnv *env);
bool parseConfigString(const std::string &content, GameConfig &outConfig);
void loadAppConfigFromCompanion(int socketFd, GameConfig &outConfig);

# Zygisk GLTools: GPU, CPU & Core Spoofing Engine

A next-generation Zygisk module and companion Android app inspired by classic GLTools, designed for Magisk, KernelSU, and APatch.

## Features
- **CPU & Core Spoofing (Classic GLTools Feature)**:
  - Intercepts POSIX `sysconf(_SC_NPROCESSORS_CONF)` and `sysconf(_SC_NPROCESSORS_ONLN)` to spoof physical and online core counts.
  - Automatically spoofs Java `Runtime.getRuntime().availableProcessors()` for Unreal Engine, Unity, and Java runtime.
  - Intercepts `/proc/cpuinfo` and `/sys/devices/system/cpu/*` (`online`, `present`, `possible`) via in-memory synthetic file descriptors (`memfd_create` / `pipe2`).
  - Spoofs Android system properties: `ro.soc.manufacturer`, `ro.soc.model`, `ro.board.platform`, `ro.hardware.chipname`, and `ro.hardware`.
  - Spoofs `android.os.Build.HARDWARE`, `BOARD`, `SOC_MANUFACTURER`, and `SOC_MODEL` via JNI.
  - Built-in CPU presets: Snapdragon 8 Gen 3 (8 Cores), Snapdragon 8 Gen 2 (8 Cores), Dimensity 9300 (8 Cores), Apple A17 Pro (6 Cores), Tensor G3 (9 Cores), and Custom.
- **GPU Spoofing**:
  - `GL_RENDERER`, `GL_VENDOR`, `GL_VERSION`, and `GL_EXTENSIONS`.
- **GLSL Shader Engine**:
  - Scale-invariant normalized chromaticity facial skin tone protection (`_glt_sk`) and emerald turf balance (`_glt_gr`).
  - Presets: DLSS 5 Ultra, Vibrant HDR, Golden Hour, ACES Filmic, Cyberpunk Neon, Cel-Shading, Night Vision, and Custom GLSL.
- **Frame Rate & Resolution**:
  - Resolution downscaling via `glViewport`.
  - VSync uncapping and sleep compression (`usleep` / `nanosleep`).

## How to Build & Install
1. Push to your GitHub repository `https://github.com/aj750390/zygisk-gltools.git`.
2. GitHub Actions will automatically compile the release APK and multi-ABI `Zygisk_GLTools_v1.0.0.zip`.
3. Flash the zip in Magisk/KernelSU and install the companion APK.

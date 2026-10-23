# Zygisk GLTools (Next-Gen Graphics Optimizer & GPU Spoofer)

Inspired by the legendary **GLTools** app, **Zygisk GLTools** brings custom graphics optimization, GPU spoofing, resolution downscaling, and on-screen telemetry to modern Android devices (Android 10 through 15) using **Zygisk** and **Magisk / KernelSU / APatch**.

---

## 🌟 Features

* **GPU Identity Spoofing**: Intercepts `glGetString(GL_RENDERER)` and `glGetString(GL_VENDOR)` at the process level to emulate flagship chips (e.g. Snapdragon 8 Gen 3 / Adreno 750, Dimensity 9300 / Mali-G720, Apple A17 GPU) and unlock 90/120 FPS or Ultra graphics in games like PUBG Mobile, Genshin Impact, and COD Mobile.
* **Resolution Downscaling**: Scales the OpenGL ES viewport (`glViewport`) down to custom fractions (50% to 100%) to boost framerates on lower-end devices without modifying system resolution.
* **16-bit Texture Optimization**: Forces texture format downsampling to save VRAM and avoid thermal throttling during long gaming sessions.
* **Floating Performance HUD**: Real-time on-screen telemetry pill showing live FPS (via `Choreographer`) and battery thermals.
* **Per-App Customization**: Easily configure individual game profiles through the Material 3 Companion Manager APK.

---

## 🏗️ Architecture

```
┌───────────────────────────────────────────────┐
│           GLTools Companion APK               │
│  (Material 3 UI, Package Scan, Config Editor) │
└──────────────────────┬────────────────────────┘
                       │ Writes JSON profiles via su
                       ▼
┌───────────────────────────────────────────────┐
│     /data/adb/modules/zygisk_gltools/config/  │
└──────────────────────┬────────────────────────┘
                       │ Reads config on app fork
                       ▼
┌───────────────────────────────────────────────┐
│         Zygisk Native Hook (C++20)            │
│  (postAppSpecialize -> PLT hook glGetString)  │
└──────────────────────┬────────────────────────┘
                       │ Modifies GLES calls
                       ▼
┌───────────────────────────────────────────────┐
│           Target 3D Game / OpenGL ES          │
└───────────────────────────────────────────────┘
```

---

## 🚀 How to Build

### Option 1: Build in Android Studio
1. Clone or extract this repository.
2. Open the project root in **Android Studio Hedgehog / Jellyfish / Ladybug**.
3. Install **NDK (Side by side) 26.x** and **CMake 3.22.1+** via SDK Manager (`Tools > SDK Manager > SDK Tools`).
4. Select `Build > Build Bundle(s) / APK(s) > Build APK(s)`.
5. The compiled APK will be located at `app/build/outputs/apk/release/`.

### Option 2: Automated Cloud Build via GitHub Actions
1. Push this repository to GitHub.
2. Navigate to the **Actions** tab.
3. The included `.github/workflows/build.yml` workflow will automatically compile both:
   - The standalone **Companion Manager APK**.
   - The flashable **Magisk / KernelSU Module ZIP** (`Zygisk_GLTools_v1.0.0.zip`).
4. Download the artifacts directly from the workflow run.

---

## 📲 Installation & Usage

1. **Prerequisites**: A rooted device running **Magisk v24+**, **KernelSU**, or **APatch** with **Zygisk enabled**.
2. **Flash Module**: Flash `Zygisk_GLTools_v1.0.0.zip` in Magisk/KernelSU Manager and reboot your device.
3. **Install Companion APK**: Install `GLToolsCompanion.apk` (or let `customize.sh` install it automatically).
4. **Configure Profiles**:
   - Open **GLTools Zygisk**.
   - Select your target game from the list.
   - Choose a GPU preset (e.g. *Adreno 750*) or set a custom resolution scaling factor (e.g. *75%*).
   - Tap **Apply Profile to Zygisk**.
5. Launch your game and enjoy unlocked graphics and boosted framerates!

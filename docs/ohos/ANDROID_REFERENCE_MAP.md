# Android 参考实现映射（Android → OHOS 语义对照）

> Android 是“语义参考”，不是目标平台。禁止 `#define Q_OS_ANDROID` / `__ANDROID__` 伪装。
> 扫描基线：`bcfb7956`，`grep Q_OS_ANDROID|__ANDROID__` 共 **24 个文件**。

## 1. 打包与宿主

| Android | 位置 | 作用 | OHOS 对应策略 |
|---|---|---|---|
| `packaging/android/`（gradle + AndroidManifest + res） | packaging/android | Qt androiddeployqt 宿主工程 | `packaging/ohos/`（hvigor 工程 + module.json5 + Qt runtime so 注入），TASK-003 建立 |
| `tools/android_buildenv.sh` | tools | 下载 `downloads.mixxx.org/dependencies/.../mixxx-deps-2.7-arm64-android-*.zip` 预编译 vcpkg 环境（含 Qt for Android） | OHOS 无现成 deps 包：自建 vcpkg arm64-ohos 环境 + 源码编 Qt；后续可仿照做 `tools/ohos_buildenv.sh` 固化产物 |
| APK 打包目标 `cmake --build . --target apk` | CMake | Qt CMake Android 支持 | OHOS 用 hvigor 打 HAP，CMake 只出 `libmixxx.so` 与 Qt so 集合 |

## 2. 音频（src/soundio/）

| Android | OHOS 策略 |
|---|---|
| `portaudioenumerator.cpp` / `sounddeviceportaudio.cpp` 内 `Q_OS_ANDROID` 块：经 JNI 调 AudioManager 枚举设备/采样率/frames-per-buffer，注册进 PortAudio Oboe backend | 同样的“枚举→注册进 PortAudio”思路，但经 OHAudio API（`OH_AudioRoutingManager` 等）在 **pa_ohos hostapi 内部**完成；`SoundDevicePortAudio` 保持不动 |
| Oboe 低延迟 PCM | OHAudio `OH_AudioRenderer`（等价角色，API 形态不同，不可机翻） |
| `portaudioenumerator.cpp` 中 Android 设备名/channel 适配 | `AudioRouter.qml` 层最终加 `case "HarmonyOS OHAudio"`（P6） |
| CPU affinity / performance hint（Android 特调） | **不照搬**；先用默认调度，profiler 证明需要后接 OHOS QoS API |

## 3. 应用入口与生命周期

| Android | 位置 | OHOS 策略 |
|---|---|---|
| `src/main.cpp:199` `Q_OS_ANDROID` JNI 函数禁用 Qt accessibility（修性能/crash） | src/main.cpp | 平台 quirk，OHOS 不照抄；若 Qt ohos QPA 有同类问题，届时按第一现场处理 |
| QtActivity 宿主生命周期（pause/resume → Mixxx 引擎） | Qt Android 框架层 | Qt ohos QPA + `packaging/ohos` Ability 生命周期桥（P10 `OhosLifecycleBridge`） |
| `src/qml/qmlapplication.cpp/.h`（`Q_OS_ANDROID` 屏幕参数等） | src/qml | 逐条核实其意图（多为移动屏适配），OHOS 走同一移动语义但用 OHOS 能力实现 |

## 4. Controllers（P7/P8 才动）

| Android | 位置 | OHOS 策略 |
|---|---|---|
| `src/controllers/hid/hiddevice.*`、`hidiothread.*`、`hidenumerator.cpp` 内 Android USB 打开/权限 | src/controllers/hid | 建议 P8 先抽 `PlatformUsbDeviceInfo`（vendorId/productId/serial/...）再挂 OHOS USB Kit 实现 |
| `src/controllers/bulk/*` 同上 | src/controllers/bulk | 同上，`-DBULK=OFF` 起步 |
| `controllerscriptenginelegacy.cpp`、`dlgprefcontroller.*` 的 Android 分支 | src/controllers | 多为“Android 不支持 portmidi/某后端”的守卫；OHOS 用自己的守卫宏，不伪装 |
| `packaging/android/src/org/mixxx/UsbPermission.java`（若存在） | Java 桥 | OHOS 权限模型不同（requestPermissionsFromUser / USB Kit），自研 |

## 5. 平台杂项

| 文件 | Android 用途 | OHOS 策略 |
|---|---|---|
| `src/util/screensaver.cpp/.h` | JNI 保持屏幕常亮 | OHOS 窗口 keepScreenOn（P10，经 Qt window 属性或 NAPI） |
| `src/util/desktophelper.cpp` | Android 打开 URL/文件夹适配 | 用 Qt 标准接口 + OHOS OpenLink 能力验证 |
| `src/preferences/configobject.cpp` | Android 存储路径特判 | OHOS 走应用沙箱 `filesDir`，不走 `/storage/emulated/0` |
| `src/waveform/waveformwidgetfactory.cpp` | Android GLES 特判 | 核实是否 GLES2 路径需求，Qt ohos QPA 下按需对齐 |
| `src/mixxxmainwindow.cpp`、`src/coreservices.cpp` | Android 启动/退出特判 | 逐条审阅意图（多为“移动端禁某功能”），按能力而非 OS 重写（P3） |

## 6. Qt 侧 OHOS 已有能力（来自 qtbase dev 分支调研）

- `mkspecs/ohos-clang`、`src/plugins/platforms/ohos` QPA 插件、`cmake/QtHarmonyOSHelpers.cmake`（权限声明机制，类似 Android 的 `qt_internal_add_android_permission`）
- 含义：窗口/输入/GPU/生命周期由 Qt ohos QPA 承担，Mixxx 不需要自写 XComponent 渲染（指导书 ADR-002）

## 7. 完整 Android 特定文件清单（24 个，2026-09-26 扫描）

```
src/main.cpp                              src/controllers/dlgprefcontroller.h
src/coreservices.cpp                      src/controllers/dlgprefcontroller.cpp
src/mixxxmainwindow.cpp                   src/controllers/bulk/bulkenumerator.{cpp,h}
src/qml/qmlapplication.{h,cpp}            src/controllers/bulk/bulkcontroller.{cpp,h}
src/soundio/portaudioenumerator.cpp       src/controllers/hid/hiddevice.{cpp,h}
src/util/desktophelper.cpp                src/controllers/hid/hidcontroller.cpp
src/util/screensaver.{cpp,h}              src/controllers/hid/hidenumerator.cpp
src/waveform/waveformwidgetfactory.cpp    src/controllers/hid/hidiothread.{cpp,h}
src/preferences/configobject.cpp          src/controllers/scripting/legacy/controllerscriptenginelegacy.cpp
```

# Mixxx → HarmonyOS NEXT / OpenHarmony 原生移植：Codex 执行指导书

> 文档用途：直接放入 Mixxx OHOS 移植仓库，作为 Codex / AI Agent 的长期执行约束与阶段路线图。  
> 目标平台：HarmonyOS NEXT / OHOS，覆盖手机、平板、HarmonyOS PC。  
> UI 路线：优先复用 Mixxx 当前 Qt 6 + QML 新界面，并参考 Android 的移动端适配，不从旧 QWidget 桌面界面重新移植。  
> 基线日期：2026-09-26。  
> 上游参考基线：`mixxxdj/mixxx` `main`，本次调研 SHA：`bcfb7956315e64d383c570dcb9e79a06a883e335`。  
> 许可证：Mixxx 顶层为 GPL-2.0-or-later。OHOS 移植代码、构建脚本及发布对应源码需要按 GPL 义务处理。

---

## 0. 给 Codex 的总指令

你正在负责 **Mixxx 的 HarmonyOS NEXT / OHOS 原生移植**。

本项目不是“把 Linux/Windows 桌面版硬搬到手机”，也不是“重新实现一个 DJ 软件”。正确路线是：

1. **最大限度复用 Mixxx 核心：** Engine、Mixer、Deck、Library、Track、Effects、Controller、数据库、音频处理逻辑保持上游架构。
2. **直接沿 Mixxx 当前 Qt 6 + QML 新 UI 路线推进。**
3. **Android 是最重要的移动端参考实现，但不是目标平台。**
4. Android 中的 JNI、Java、Oboe、`Q_OS_ANDROID`、Android Storage/USB Permission 等只能作为“语义参考”，不能机械替换成 OHOS 名称。
5. OHOS 平台功能必须通过清晰的平台边界接入，避免整个仓库遍布 `#ifdef OHOS`。
6. 首要目标是建立一条稳定、可验证的最小运行链：

```text
HAP Start
  -> Qt Harmony QPA
  -> QApplication / QML Engine
  -> Mixxx CoreServices
  -> Main QML
  -> Library / Deck Core
  -> OHAudio output
  -> Load one track
  -> Play / Pause / Seek / Waveform
```

7. 先做“能跑”，再做“完整”，最后做“低延迟、外设、PC 体验和商店发布”。
8. 每个阶段必须有明确的 Gate。Gate 没通过，不允许通过大规模 workaround 跳到下一阶段。
9. 所有修改都必须尽量保持 Windows / Linux / macOS / Android 构建不回归。
10. 不允许为了“先编过”而永久删除、注释或 mock 掉核心逻辑。临时 stub 必须被显式标记，并记录退出条件。

---

# 1. 项目最终目标

## 1.1 产品目标

最终交付一个 **同一套 Mixxx OHOS 核心代码**支持：

- HarmonyOS 手机
- HarmonyOS 平板
- HarmonyOS PC

UI 不按设备型号写三套，而按 **窗口尺寸 + 输入能力 + 当前窗口形态**自适应。

建议产品能力目标：

| 能力 | 手机 | 平板 | PC |
|---|---:|---:|---:|
| 双 Deck 基础播放 | 必须 | 必须 | 必须 |
| 4 Deck | 后续/可选 | 可选 | 必须 |
| Mixer / EQ / Crossfader | 必须 | 必须 | 必须 |
| Library | 必须 | 必须 | 必须 |
| Waveform | 必须 | 必须 | 必须 |
| 文件导入 | 必须 | 必须 | 必须 |
| USB 音频 | 后续 | 必须 | 必须 |
| MIDI Controller | 后续 | 必须 | 必须 |
| HID Controller | 后续 | 后续 | 必须 |
| 键盘快捷键 | 可选 | 可选 | 必须 |
| 鼠标 Hover / 右键 | 不要求 | 可选 | 必须 |
| 可调整窗口 | 否 | 可选 | 必须 |
| 多窗口 | 不要求 | 后续 | 后续 |

---

# 2. 当前 Mixxx 上游现状：必须基于这些事实推进

Codex 开始任何实现前，先重新核对当前 checkout，不允许把以下信息当作永远不变的事实。

## 2.1 当前主线已经有 Android 移动端工作

Mixxx 当前 `main` 已包含：

- `packaging/android/`
- Android Manifest
- Android Qt Activity
- USB Permission Java bridge
- Android AudioManager / Oboe 路径
- Android HID / USB 特判
- QML 新 UI
- Android 专项开发 Epic

因此：

> **OHOS 应该复用 Android 移动端已经验证过的架构思想，而不是从桌面平台重新摸索一遍。**

但是：

> **不要把 `Q_OS_ANDROID` 伪装成 OHOS，更不要靠宏把 OHOS 当 Android 编译。**

---

## 2.2 UI 应以当前 QML 新界面为主线

当前上游已有：

```text
res/qml/
src/qml/
```

当前 `res/qml/main.qml` 已包含移动判断：

```qml
readonly property bool isMobile:
    Qt.platform.os === "android" || Qt.platform.os === "ios"
```

并且移动端使用全屏 `Screen.width / Screen.height`。

`MainWindow.qml` 已经存在基于窗口宽高的响应式行为，例如：

- 窄窗口时 Deck / Mixer 重新布局
- 根据可用高度决定 Library 是否加载
- QML Deck 自身已有 `minWidth` / 动态组件布局

所以：

### 禁止

```text
复制整个 res/qml -> res/qml-ohos
然后长期维护 OHOS 独立 UI fork
```

### 推荐

把现有 QML 的“Android/iOS 二值移动判断”升级为：

```text
LayoutClass = Compact / Medium / Expanded
InputClass  = Touch / Pointer / Hybrid
WindowClass = FullScreen / Freeform
```

让 Android、OHOS 手机、OHOS 平板、OHOS PC 都能共享。

---

## 2.3 Android 音频实现是 OHOS 音频接入的主要参考

Mixxx 当前 Android 音频路线核心位于：

```text
src/soundio/portaudioenumerator.cpp
src/soundio/sounddeviceportaudio.cpp
```

Android 路线大体是：

```text
Mixxx SoundManager
        |
SoundDevicePortAudio
        |
     PortAudio
        |
   Android Oboe
        |
 Android Audio HAL
```

当前 Android 代码通过系统 AudioManager：

- 枚举输入/输出设备
- 获取设备名称
- 获取 ChannelCount
- 获取 SampleRate
- 获取 native frames-per-buffer
- 向 PortAudio Oboe backend 注册设备

然后 **继续复用 Mixxx 原有 SoundDevicePortAudio、回调、Buffer、路由和 Drift 处理逻辑**。

这对 OHOS 非常重要。

### OHOS 推荐架构

优先：

```text
Mixxx SoundManager
        |
SoundDevicePortAudio
        |
     PortAudio
        |
   pa_ohos backend
        |
      OHAudio
```

而不是第一天就写：

```text
Mixxx
  -> 全新 SoundDeviceOhos
  -> 重写 SoundManager 大量逻辑
```

如果 `pa_ohos` 最终证明不适合，再考虑 direct backend。

---

# 3. OHOS 技术路线总图

```text
+-----------------------------------------------------------+
|                 Mixxx shared application                 |
|-----------------------------------------------------------|
| CoreServices / Engine / Mixer / Deck / Library / DB      |
| Effects / Track / Waveform / Controller Mapping          |
+---------------------------+-------------------------------+
                            |
+---------------------------v-------------------------------+
|                      Qt 6 / QML                          |
|  QML UI / Qt Quick / Qt SQL / Qt Network / Qt Core      |
+-------------+------------------+--------------------------+
              |                  |
              |                  +-----------------------+
              |                                          |
+-------------v-------------+             +--------------v-----------+
| Mixxx OHOS platform layer |             | Qt Harmony QPA / Qt RHI |
|---------------------------|             |--------------------------|
| Files / Picker            |             | Window / Input / GPU     |
| Permissions               |             | Touch / Mouse / Keyboard |
| Audio Session             |             +--------------------------+
| Lifecycle                 |
| USB / MIDI bridge         |
| Performance/QoS           |
+-------------+-------------+
              |
+-------------v---------------------------------------------+
|                 HarmonyOS Native APIs                    |
| OHAudio / Audio Routing / Audio Session                  |
| Picker / File / USB / MIDI / Ability / Window           |
+-----------------------------------------------------------+
```

---

# 4. 关键架构决策

## ADR-001：UI 不改为 ArkUI 重写

### 决策

**第一阶段继续使用 Qt Quick / QML。**

### 原因

Mixxx 当前已经在向 QML 新 UI 演进，Android 同样依赖 Qt/QML。

若 OHOS 改成 ArkUI：

- Deck / Library / Mixer 大量 UI 全部需要重写
- QML Proxy / QML Model 接口失去复用价值
- Android 和 OHOS UI 变成两条线
- PC 响应式 UI 再次需要单独开发
- 上游同步成本极高

### ArkTS / ArkUI 的定位

只作为必要的 **平台桥接层**，例如：

- Picker
- 权限
- 特定 Ability 生命周期
- 系统服务
- 某些 Qt 尚未暴露的系统 API

不负责实现 Mixxx 主界面。

---

## ADR-002：优先 Qt Harmony QPA，而不是自己托管 XComponent

首选：

```text
Qt Harmony QPA
 -> QQuickWindow
 -> Qt Scene Graph / RHI
 -> HarmonyOS 图形栈
```

只有在确认 Qt Harmony QPA 存在无法绕过的关键 blocker 时，才评估：

```text
ArkUI XComponent
 -> NativeWindow
 -> 自定义 Qt / Render integration
```

不要一开始就走 XComponent 自绘路线。

---

## ADR-003：OHOS 不是 Android

### Qt C++ 代码

Qt 当前 HarmonyOS 平台宏应优先使用：

```cpp
Q_OS_HARMONY
```

### CMake

Qt Harmony toolchain / Qt CMake 中可识别 `OHOS`。

项目可额外定义一个 Mixxx 自己的统一宏：

```cpp
MIXXX_OS_OHOS
```

或：

```cpp
__OHOS__
```

但必须统一，只保留一种项目级宏。

### 禁止

```cpp
#define Q_OS_ANDROID
#define __ANDROID__
```

来欺骗 Android 路径。

这会导致：

- JNI 被错误启用
- Android Java 类型进入编译
- Android 文件路径泄漏
- Android Permission 假设泄漏
- Oboe 代码错误接入

---

# 5. 仓库目录建议

不要为了“架构好看”大规模搬动上游文件。

尽量采用增量目录：

```text
mixxx/
├─ packaging/
│  ├─ android/
│  └─ ohos/
│     ├─ AppScope/
│     ├─ entry/
│     ├─ build-profile.json5
│     ├─ hvigorfile.ts
│     └─ README.md
│
├─ src/
│  ├─ platform/
│  │  └─ ohos/
│  │     ├─ ohosplatformservices.h/.cpp
│  │     ├─ ohosfilepicker.*
│  │     ├─ ohoslifecycle.*
│  │     ├─ ohosaudiosession.*
│  │     └─ ohospermissions.*
│  │
│  ├─ soundio/
│  │  └─ ohos/
│  │     ├─ ohosaudioenumerator.*
│  │     └─ ...
│  │
│  └─ controllers/
│     └─ ohos/
│        ├─ ohosmidi.*
│        └─ ohosusb.*
│
├─ cmake/
│  └─ ohos/
│     ├─ Dependencies.cmake
│     └─ Packaging.cmake
│
├─ res/qml/
│  ├─ Platform/
│  │  ├─ DeviceProfile.qml
│  │  └─ SafeArea.qml
│  └─ ... existing shared QML ...
│
├─ docs/
│  └─ ohos/
│     ├─ PORTING_STATUS.md
│     ├─ DEPENDENCY_MATRIX.md
│     ├─ DEVICE_TEST_MATRIX.md
│     └─ ARCHITECTURE.md
│
└─ vcpkg-triplets/
   └─ ... only when upstream vcpkg triplet cannot satisfy ...
```

### 注意

UI 应尽可能留在现有 `res/qml/` 中做通用响应式改造。

不要形成：

```text
res/qml/android/
res/qml/ohos/
res/qml/pc/
```

三套长期分叉。

---

# 6. Codex 的强制工作流程

## 6.1 每次任务开始前

Codex 必须：

1. 读取：
   - 本文档
   - `docs/ohos/PORTING_STATUS.md`
   - `docs/ohos/DEPENDENCY_MATRIX.md`
2. 执行：

```bash
git status --short --branch
git log --oneline -10
```

3. 找到本任务对应的 Android / Linux / iOS 实现。
4. 先确认调用链，再改代码。
5. 如果目标 API 不确定，先查 HarmonyOS / Qt 官方 API，不得猜函数名。

---

## 6.2 每个任务结束后

必须更新 `PORTING_STATUS.md`，至少包含：

```markdown
## Task P?.?

Status: PASS / PARTIAL / BLOCKED

### Goal
...

### Changed files
- ...

### Build
命令：...
结果：...

### Device validation
设备：...
结果：...

### Evidence
关键日志：...

### Remaining blocker
...

### Next step
...
```

---

## 6.3 禁止行为

Codex 不得：

- 一次性对几百个文件做无目标的宏替换
- 为了通过编译永久删除功能
- 把 OHOS 伪装成 Linux 或 Android
- 在音频 callback 内增加文件 IO / blocking mutex / 大量日志
- 未真机验证就宣称低延迟完成
- 未验证 API 就编造 HarmonyOS API
- 直接 fork 一套 OHOS UI
- 修改 Engine DSP 来解决平台层问题
- 围绕一个 crash 连续堆多个 fallback，而不找第一现场原因
- 用 `#ifdef Q_OS_HARMONY` 散落在几十个业务模块里解决架构问题

---

# 7. 分阶段实施路线

---

# P0 — Baseline Freeze / 上游基线与依赖盘点

## 目标

建立可复现基线，不写平台功能。

## 工作

### P0.1 固定上游基线

记录：

```text
repo: https://github.com/mixxxdj/mixxx
baseline SHA: bcfb7956315e64d383c570dcb9e79a06a883e335
```

若开始开发时 main 已前进：

- 可以选择新的 SHA
- 但必须在 `PORTING_STATUS.md` 更新
- 不允许长期使用“main 最新”这种不可复现描述

### P0.2 建立分支

建议：

```text
feature/ohos-port
```

或团队已有命名规范。

### P0.3 建立依赖矩阵

新建：

```text
docs/ohos/DEPENDENCY_MATRIX.md
```

至少按：

| Component | Mixxx requirement | OHOS status | Bring-up priority | License | Action |
|---|---|---|---:|---|---|
| Qt6 | Required | Qt Harmony port | P0 | LGPL/GPL | Build/prepare kit |
| PortAudio | Required upstream | no assumed OHOS backend | P1 | MIT-style | port backend |
| SQLite | Required path | likely available via Qt/build | P1 | public domain | verify |
| TagLib | Required/feature | verify | P1 | LGPL/MPL | cross-build |
| FFmpeg | Optional by CMake | verify | P2 | mixed LGPL/GPL | enable later |
| hidapi | `HID` option | verify | P5 | BSD/GPL variants | disable initially |
| libusb | `BULK` option | verify | P5 | LGPL | disable initially |
| RubberBand | feature | verify | P3 | GPL | port later |
| SoundTouch | feature | verify | P3 | LGPL | port later |
| Broadcast | optional | not MVP | P6 | verify | disable |

> Codex 必须先核对 CMake option，再决定能否关闭依赖。不得因为本表写“optional”就直接删代码。

### P0 Gate

- [ ] 上游 SHA 已固定
- [ ] Android 参考路径已记录
- [ ] 依赖矩阵完成
- [ ] 当前桌面主线至少有一个已知可编译基线
- [ ] OHOS SDK / Qt Harmony toolchain 版本已记录

---

# P1 — Qt Harmony + HAP 最小启动

## 目标

先证明：

```text
HAP -> Qt -> QML -> Mixxx executable entry
```

可以成立。

此阶段 **不要求音频、不要求完整 Mixxx Core**。

## P1.1 Qt Harmony 环境

优先按 Qt 官方 HarmonyOS 构建方式准备：

```text
Host Qt
Target Qt for HarmonyOS
arm64-v8a
HarmonyOS SDK
```

Qt HarmonyOS 当前仍处于快速演进阶段，必须把实际 Qt commit/version 固定下来。

记录：

```text
DevEco Studio version
HarmonyOS SDK/API
NDK/native toolchain
Qt host version
Qt Harmony commit/version
CMake
Ninja
vcpkg commit
```

## P1.2 API 基线建议

2026-09 首轮 bring-up 建议：

```text
compileSdk / target: API 26
```

先让当前 SDK 跑通。

待主链稳定后，再评估：

```text
compatibleSdkVersion lower bound
```

不要第一阶段同时解决 SDK 向下兼容。

## P1.3 CMake 平台识别

当前 Mixxx 顶层 CMake 有：

```cmake
elseif(UNIX)
  if(APPLE)
  elseif(ANDROID)
  else()
    ...
  endif()
endif()
```

Codex 必须确认 Harmony toolchain 的：

```cmake
OHOS
CMAKE_SYSTEM_NAME
```

然后加入独立分支，例如概念上：

```cmake
if(OHOS)
    target_compile_definitions(mixxx-lib PUBLIC MIXXX_OS_OHOS)
elseif(ANDROID)
    ...
endif()
```

具体位置根据当前 CMake 结构最小改动实现。

### 不允许

把 OHOS 放进普通 Linux/UNIX fallback 后假装正常。

## P1.4 packaging/ohos

创建 OHOS HAP packaging 最小工程。

目标只需要：

- Ability 启动
- Qt runtime 加载
- `libmixxx.so` / executable 正确加载
- QML splash 能出现
- HAP 可安装 / 启动 / 退出

### P1 Gate

真机日志必须证明：

```text
[OHOS] Ability entered
[OHOS] Qt runtime initialized
[Mixxx] QApplication created
[Mixxx] QML engine created
[Mixxx] main.qml load success
```

允许后续 CoreServices 尚未完整。

---

# P2 — Minimal Mixxx Core 编译闭环

## 目标

让 Mixxx Core 在 OHOS 初始化到 Main QML。

## 原则

第一阶段只启用 MVP 必需组件。

优先关闭可选功能：

```text
Broadcast
Bulk USB
HID
Vinyl Control
额外插件系统
非必要 codecs
Stem
```

**但只能通过现有 CMake option 合法关闭。**

不要修改业务代码“假装没有”。

## P2.1 建议 Feature Preset

建议引入：

```text
MIXXX_OHOS_BRINGUP=ON
```

它只负责设置官方已有 option 的默认组合，例如：

```text
QML=ON
HID=OFF
BULK=OFF
VINYLCONTROL=OFF
BROADCAST=OFF
...
```

不要使该 preset 永远变成阉割版产品配置。

## P2.2 第三方库策略

优先尝试当前 vcpkg 的 OHOS triplet：

```text
arm64-ohos
```

只有缺失时再维护自定义 triplet / port patch。

第三方库 patch 必须放在明确目录，不得直接手改 cache：

```text
cmake/ohos/
ports/ohos/
patches/ohos/
```

### P2 Gate

- [ ] `mixxx-lib` 编译完成
- [ ] QML modules 编译完成
- [ ] HAP 打包完成
- [ ] 真机启动进入 Mixxx QML MainWindow
- [ ] CoreServices 初始化不 crash
- [ ] 可打开 Settings / 基础 Library 空界面

---

# P3 — 手机 / 平板 / PC 响应式 UI

## 目标

一套 QML 自动适配三类窗口。

## 3.1 不要按“设备类型”写死

错误：

```qml
if (Qt.platform.os === "harmonyos") {
    // phone layout
}
```

因为手机、平板、PC 都是 OHOS。

正确：

```text
窗口大小
+ 输入能力
+ 是否支持 hover
+ 当前窗口状态
+ safe area
```

决定 UI。

---

## 3.2 创建统一 DeviceProfile

建议：

```qml
// res/qml/Platform/DeviceProfile.qml
pragma Singleton

QtObject {
    readonly property int Compact: 0
    readonly property int Medium: 1
    readonly property int Expanded: 2

    property int layoutClass
    property bool touchPrimary
    property bool pointerAvailable
    property bool hoverAvailable
    property bool keyboardAvailable
    property bool fullScreen
}
```

实际值不要只靠 OS；由窗口宽度和能力决定。

推荐初始逻辑区间只作为实验起点：

```text
Compact  : < 720 logical px
Medium   : 720 ~ 1199
Expanded : >= 1200
```

这些不是最终产品标准，必须真机调整。

---

## 3.3 Compact：手机

优先横屏 DJ 场景。

建议：

```text
+-----------------------------------+
|           Waveform                |
+-----------------------------------+
| Deck A / Deck B 切换或聚焦 Deck   |
|                                   |
+-----------------------------------+
| EQ / Mixer / Crossfader           |
+-----------------------------------+
| Library / FX / Settings tabs      |
+-----------------------------------+
```

原则：

- 默认 2 Deck
- 一个时刻重点展示 1 Deck
- Deck A/B 可以 swipe / tab / focus 切换
- Crossfader 永远容易访问
- Play / Cue 等触摸目标建议 >= 48 logical px
- Library 可作为全屏 page 或 bottom sheet
- 不要求 hover
- 不显示桌面式菜单栏
- 不能依赖右键

---

## 3.4 Medium：平板

```text
+------------------------------------------------+
| Waveform A                   Waveform B        |
+----------------------+-------------------------+
| Deck A               | Deck B                  |
|                      |                         |
+----------------------+-------------------------+
|             Mixer / EQ                         |
+------------------------------------------------+
|      Collapsible Library / Browser             |
+------------------------------------------------+
```

原则：

- 两 Deck 同时可见
- Mixer 常驻
- Library 可折叠
- 横屏优先
- 允许触控 + 鼠标/键盘混用

---

## 3.5 Expanded：HarmonyOS PC

目标接近桌面 Mixxx 使用效率：

- 2 / 4 Deck
- 完整 Library
- Settings 独立弹窗/大窗口
- Hover 状态
- Keyboard shortcut
- Mouse wheel
- Drag & Drop
- 可调整窗口尺寸
- 非全屏模式

不要因为“OHOS 是移动系统”而在 PC 强制全屏。

---

## 3.6 修改现有 main.qml

当前：

```qml
readonly property bool isMobile:
    Qt.platform.os === "android" || Qt.platform.os === "ios"
```

目标逐步改成：

```qml
readonly property bool compactLayout:
    DeviceProfile.layoutClass === DeviceProfile.Compact
```

并把：

```text
fullscreen
minimumWidth
menuBar
Deck layout
Library mode
```

从 `isMobile` 中解耦。

### P3 Gate

必须至少完成 3 组真机/窗口截图和操作验证：

```text
Compact
Medium
Expanded
```

并记录：

```text
docs/ohos/DEVICE_TEST_MATRIX.md
```

---

# P4 — 音频输出 MVP：OHAudio

这是整个移植最重要的阶段之一。

## 目标

```text
导入一首音乐
-> Deck A load
-> Play
-> Mixxx engine 输出 PCM
-> OHAudio render
-> 扬声器/耳机播放
```

---

## 4.1 不使用 AVPlayer 代替 Mixxx Engine

Mixxx 自己负责：

- 解码
- time stretch
- tempo
- EQ
- mixing
- crossfade
- effects
- waveform timing

所以 HarmonyOS 系统播放器不是 Mixxx 主音频引擎。

OHOS 需要的是：

```text
低延迟 PCM input/output backend
```

OHAudio 正适合承担这个角色。

---

## 4.2 第一优先方案：PortAudio OHOS host backend

目标：

```text
src/soundio/sounddeviceportaudio.cpp
```

尽量保持不改。

新增类似：

```text
PortAudio
  src/hostapi/ohos/
    pa_ohos.c
    pa_ohos.h
```

内部封装：

```text
OH_AudioStreamBuilder
OH_AudioRenderer
OH_AudioCapturer
OH_AudioRoutingManager
```

最终让 PortAudio 枚举出：

```text
HarmonyOS OHAudio
```

作为 Host API。

### 优势

保留：

- SoundManager
- SoundDevicePortAudio
- callback contract
- buffer management
- latency calculations
- channel routing
- existing QML AudioRouter model

---

## 4.3 第二方案：Direct SoundDeviceOhos

只有以下情况再使用：

- PortAudio backend 与 OHAudio callback 模型严重冲突
- 维护成本高于 direct backend
- 无法正确表达多设备/多 stream
- PortAudio abstraction 成为明确性能瓶颈

此时再设计：

```text
SoundDeviceOhos
OhosAudioEnumerator
```

但仍需实现与 SoundManager 一致的契约。

---

## 4.4 实时线程规则

音频回调中禁止：

```text
malloc/new 大对象
文件 IO
SQLite
阻塞 mutex
等待 Future
UI 调用
频繁日志
JSON
NAPI 同步跨线程调用
```

推荐：

- preallocated buffer
- lock-free ring buffer
- atomic counters
- callback 只搬运/转换必要 PCM
- underrun/overrun 用计数器异步上报

---

## 4.5 不复制 Android CPU affinity

Android 当前代码有 CPU affinity / performance hint 处理。

OHOS 不允许直接复制：

```cpp
sched_setaffinity(... Android mask ...)
```

先使用默认调度。

只有 profiler 证明需要时，再研究 HarmonyOS QoS / performance API。

---

## 4.6 音频诊断指标

实现统一 debug stats：

```text
sampleRate
channelCount
framesPerCallback
callbackPeriodUs
callbackCostUs
underrunCount
overrunCount
xrunCount
renderedFrames
routeChangeCount
```

每 5~10 秒异步打印汇总，不在 real-time callback 中打印每帧。

### P4 Gate

至少：

- [ ] Built-in speaker 可播放
- [ ] Wired/Bluetooth route 变化不 crash
- [ ] 44.1 kHz / 48 kHz 至少验证主流路径
- [ ] 连续播放 30 分钟
- [ ] Play/Pause/Seek 正常
- [ ] Deck A/B 混音正常
- [ ] underrun 有可观测计数
- [ ] UI 操作时音频线程不被明显卡死

---

# P5 — 文件系统 / Library / Picker

## 核心原则

不要复制 Android：

```text
/storage/emulated/0/Mixxx
MANAGE_EXTERNAL_STORAGE
WRITE_EXTERNAL_STORAGE
```

OHOS 应遵循：

```text
App Sandbox
System Picker
授权文件访问
```

---

## P5.1 MVP 方案

第一阶段最稳妥：

```text
Picker 选择音乐文件
    |
    +-> 获得系统授权
    |
    +-> 必要时复制到 app-managed music/imports/
    |
    +-> Mixxx 继续使用普通 filesystem path
```

这样能最大限度保留 Mixxx Library / TagLib / decoder 的 path 语义。

缺点是占用额外空间，但适合第一阶段。

---

## P5.2 后续方案

稳定后研究：

```text
URI / FD backed file
persistent permission
stream abstraction
```

如果 Mixxx 各 decoder 能无痛支持 FD / QFile device，再逐步降低复制需求。

不要第一阶段为了“零拷贝文件访问”重构整个 Track / decoder 文件层。

---

## P5.3 PC

PC 可能提供更传统的文件选择和目录能力。

仍然通过统一接口：

```cpp
IPlatformFilePicker
IPlatformFileAccess
```

不要让 Library 层知道“手机/PC”。

### P5 Gate

- [ ] 选择一个音乐文件
- [ ] import
- [ ] DB 保存
- [ ] 重启后仍能找到
- [ ] 扫描 tag
- [ ] waveform analysis
- [ ] 删除/文件失效能正确处理

---

# P6 — Audio Input / Headphone / USB Audio / Routing

MVP 输出稳定后，再进入专业 DJ 音频能力。

需要覆盖：

```text
Main Output
Headphone / PFL
Microphone input
USB audio input/output
Multiple device routing
Device hotplug
```

复用：

```text
SoundManager
Settings/AudioRouter.qml
```

当前 `AudioRouter.qml` 已经对不同 Host API 的设备名称做适配。

OHOS 最终应增加：

```qml
case "HarmonyOS OHAudio":
```

而不是复制 Android 名称。

### P6 Gate

至少验证：

```text
Main -> speaker/headphones
PFL -> second route if platform/device supports
USB audio hotplug
route switch without app crash
```

如果系统不允许某类多路由，应明确记录“平台能力限制”，不要伪装成功。

---

# P7 — MIDI Controller

## 优先路线

HarmonyOS 新版 Audio/MIDI 能力如果已覆盖目标设备，应优先使用系统 MIDI API。

抽象：

```text
HarmonyOS MIDI
   |
OhosMidiEnumerator
   |
OhosMidiDevice
   |
Mixxx ControllerManager
   |
existing mapping / script
```

Mixxx 的 Mapping 层应该保持不动。

---

## P7.1 功能

需要实现：

- USB MIDI enumeration
- BLE MIDI（如果系统 API 支持并稳定）
- hotplug
- MIDI IN
- MIDI OUT
- timestamp / ordering
- mapping persistence

### P7 Gate

用至少一个真实 DJ MIDI Controller 验证：

```text
Play button
Cue
Jog
Fader
LED feedback
reconnect
```

---

# P8 — HID / Raw USB Controller

Android 当前 HID 路径包含：

```text
QJniObject UsbDevice
QJniObject UsbInterface
UsbPermission.java
```

OHOS 必须写自己的平台桥。

不要让：

```cpp
DeviceInfo
```

永久拥有 Android 与 OHOS 两套完全不同成员直到无法维护。

建议重构方向：

```text
PlatformUsbDeviceInfo
  vendorId
  productId
  serial
  manufacturer
  product
  interfaceNumber
  transport
```

Android JNI 和 OHOS USB API 分别负责转换成同一个 detached info。

这样后续上游也更容易接受。

### P8 Gate

- [ ] 枚举一个 HID controller
- [ ] permission
- [ ] open
- [ ] descriptor/report
- [ ] input report
- [ ] output report
- [ ] unplug 不 crash

---

# P9 — Waveform / GPU / Qt Quick 性能

## 原则

先使用 Qt Scene Graph / RHI 当前 OHOS backend。

不要在没有 profiler 证据前：

- 自写 Vulkan renderer
- 自己管理 NativeWindow
- 绕开 QQuickWindow
- 改 Mixxx waveform 算法

---

## P9.1 性能指标

采集：

```text
UI FPS
frame time p50/p95/p99
QML binding cost
scene graph render cost
CPU total
Mixxx audio thread cost
waveform update cost
memory
GPU memory
battery / thermal trend
```

场景：

```text
1 deck playing
2 decks playing
2 decks + waveforms
Library 1k / 10k / 50k tracks
Settings open
track load during playback
fast scroll library
```

---

# P10 — Lifecycle / Background / Audio Session

移动端与 PC 行为必须区分“能力”，而不是平台名。

需要定义：

```text
Foreground active
Background playable
Screen off
Audio interruption
Incoming call / system focus
Bluetooth route change
Ability suspend/resume
Window minimize
PC freeform close/reopen
```

建立：

```text
OhosLifecycleBridge
OhosAudioSession
```

Mixxx core 接收到抽象事件。

不要把 Ability 生命周期逻辑直接写进 Engine。

### P10 Gate

- [ ] 前后台切换不 crash
- [ ] audio focus/interruption 能恢复
- [ ] 路由变化可恢复
- [ ] PC 窗口 minimize/restore 正常
- [ ] 重复进入/退出不会泄漏 audio stream

---

# P11 — Release / Packaging / GPL Compliance

## HAP 发布内容

发布前必须保证：

- HAP
- 对应精确源码 tag
- OHOS build scripts
- Qt / third-party license notices
- Mixxx GPL LICENSE
- 修改说明
- 可复现依赖版本

推荐：

```text
release tag:
  vX.Y-ohos-0.1.0

source:
  exact Git tag

binary:
  HAP generated from same tag
```

不要发布一个无法对应源代码 commit 的 HAP。

---

# 8. 手机 / 平板 / PC UI 的统一设计规则

## 8.1 UI 不等于 Android UI

Android 只是“移动交互参考”。

应保留 Mixxx DJ 工具属性：

- 信息密度高
- 波形优先
- Deck 状态一眼可见
- Play/Cue/Crossfader 必须稳定位置
- 不为了 Material 风格把专业控件过度简化

---

## 8.2 输入模式

定义：

```text
TouchOnly
PointerOnly
Hybrid
```

QML 控件根据能力调整：

| 行为 | Touch | Mouse/Trackpad |
|---|---|---|
| Hover tooltip | 无 | 有 |
| Context menu | long press | right click |
| Fine fader adjust | long press / modifier UI | Shift/Alt + drag |
| Deck focus | tap | click/shortcut |
| Library selection | tap | mouse/keyboard |

---

## 8.3 不要硬编码分辨率

不要：

```qml
if (Screen.width === 2560)
```

使用：

```text
logical window width
available height
safe insets
scale factor
font metrics
```

---

# 9. 音频架构必须保护的 Mixxx 核心边界

Codex 在音频问题上必须先确认问题属于哪层：

```text
A. Decoder
B. Engine / Mixer
C. SoundManager
D. SoundDevicePortAudio
E. PortAudio host backend
F. OHAudio
G. Harmony Audio HAL / device
```

不要因为 F 层问题去改 B 层。

## 调试日志建议

```text
[OHOS-AUDIO][ENUM]
[OHOS-AUDIO][OPEN]
[OHOS-AUDIO][START]
[OHOS-AUDIO][CALLBACK]
[OHOS-AUDIO][XRUN]
[OHOS-AUDIO][ROUTE]
[OHOS-AUDIO][STOP]
```

每条日志带：

```text
stream id
sample rate
channels
frames
state
error code
```

---

# 10. Android → OHOS 映射表

| Android Mixxx 实现 | 含义 | OHOS 对应策略 |
|---|---|---|
| `QtActivityBase` | Qt app host | Qt Harmony app/QPA packaging |
| Android Manifest | 权限/Activity | OHOS module/profile/permissions |
| JNI / `QJniObject` | Java system bridge | Qt Harmony/native/必要 NAPI bridge |
| AudioManager | device list / routing | OHAudio routing/session APIs |
| Oboe | low latency PCM | OHAudio |
| Android USB Manager | USB enumeration/permission | OHOS USB APIs |
| Android MIDI | MIDI | HarmonyOS MIDI APIs |
| `/storage/emulated/0` | external storage | Picker + sandbox/persistent access |
| performance hint | scheduling hint | 先不照搬；后续测量后接 OHOS QoS |
| immersive navigation hide | fullscreen mobile UI | OHOS window/fullscreen/safe area |

> 这张表只表示“功能角色对应”，绝不代表 API 可以一一翻译。

---

# 11. 依赖移植顺序

不要一次 port 全部第三方库。

建议顺序：

```text
Tier 0
  Qt Core / Gui / Qml / Quick
  libc++ / pthread / dl / filesystem basics

Tier 1
  database
  TagLib
  core file/codec prerequisites

Tier 2
  PortAudio + OHAudio
  primary audio decoder path

Tier 3
  FFmpeg
  RubberBand / SoundTouch
  extra codecs

Tier 4
  MIDI
  hidapi
  libusb

Tier 5
  broadcast / plugins / optional advanced functions
```

每 port 一个库，都必须记录：

```text
upstream version
source URL
license
patch list
CMake flags
OHOS ABI
build command
known limitations
```

---

# 12. Build Presets 建议

最终应形成最少三类配置。

## Bring-up

```text
OHOS_BRINGUP
QML ON
Audio minimal
HID OFF
BULK OFF
Vinyl OFF
Broadcast OFF
Tests as possible
```

## Mobile Product

```text
OHOS_MOBILE
QML ON
OHAudio ON
MIDI ON
HID optional
Mobile responsive UI
```

## PC Product

```text
OHOS_PC
QML ON
OHAudio ON
MIDI ON
HID ON
USB ON
keyboard/mouse features ON
4 deck UI ON
```

这些 preset 应以 capability/feature 为核心，不要复制整个 build system。

---

# 13. 测试矩阵

新建：

```text
docs/ohos/DEVICE_TEST_MATRIX.md
```

## 最少维度

| Test | Phone | Tablet | PC |
|---|---|---|---|
| cold start | | | |
| QML render | | | |
| resize/orientation | | | |
| import track | | | |
| library scan | | | |
| Deck A playback | | | |
| Deck A+B | | | |
| seek | | | |
| waveform | | | |
| effects | | | |
| headphone route | | | |
| USB audio | | | |
| MIDI | | | |
| HID | | | |
| suspend/resume | | | |
| 30 min playback | | | |

---

# 14. 自动化测试策略

## Host CI

绝大多数不依赖 OHOS 的测试继续在 Linux host 跑：

```text
Core
Engine
Library
DB
Controller mapping
QML static checks
```

不要把所有测试都强制搬到真机。

## OHOS Device Smoke

目标逐步实现：

```text
build HAP
install via hdc
launch
capture hilog
run scripted smoke action
pull logs
判定 PASS/FAIL
```

设备 smoke 至少监控：

```text
SIGSEGV
SIGABRT
fatal
QML load failure
audio open failure
underrun threshold
ANR/freeze
```

---

# 15. 性能红线

Mixxx 是实时音频软件，不要把“能播放”当作完成。

至少关注：

```text
audio callback deadline
UI frame pacing
track load latency
waveform analysis CPU
large library query latency
memory peak
thermal throttling
```

### 典型错误

```text
QML 动画非常流畅
但 audio callback 每隔 5 秒 underrun
```

这种构建仍然判定 FAIL。

音频优先级高于视觉动画。

---

# 16. 第一批 Codex Task — 建议直接照此执行

以下任务不要合并成一个超大任务。

---

## TASK-001：OHOS Port Baseline Audit

### Prompt

```text
You are starting the Mixxx HarmonyOS/OHOS port.

Read:
- MIXXX_OHOS_CODEX_PORTING_GUIDE.md
- current CMakeLists.txt
- packaging/android/
- src/qml/
- res/qml/
- src/soundio/
- src/controllers/hid/

Do not modify production code yet.

Goals:
1. Record the exact upstream git SHA.
2. Identify every Android-specific code path that may have an OHOS equivalent.
3. Build a dependency matrix for OHOS.
4. Classify dependencies as:
   - mandatory for first QML launch
   - mandatory for first audio playback
   - optional for MVP
   - later controller features
5. Identify CMake feature flags already available upstream.
6. Create:
   docs/ohos/PORTING_STATUS.md
   docs/ohos/DEPENDENCY_MATRIX.md
   docs/ohos/ANDROID_REFERENCE_MAP.md

Constraints:
- Do not invent HarmonyOS APIs.
- Do not patch out missing dependencies.
- Do not add Android compatibility macros.
- No mass source edits.

Output:
- files created
- build blockers
- first recommended compile target
- exact next task
```

### Gate

只产出调查文件，不动核心代码。

---

## TASK-002：建立 OHOS CMake 平台分支

### Prompt

```text
Implement the minimum OHOS platform detection in Mixxx CMake.

Goals:
- Detect the Qt Harmony/OHOS toolchain correctly.
- Introduce one project-level OHOS compile definition.
- Prevent OHOS from entering Android-only JNI/Oboe code.
- Prevent OHOS from accidentally entering an incompatible generic Linux path.
- Preserve Linux/macOS/Windows/Android behavior.

Do not:
- fake ANDROID
- define Q_OS_ANDROID
- add platform conditions to unrelated business code
- port audio yet

Add a CMake configure-time status message showing:
- OHOS detected
- target ABI
- Qt version
- QML status

Run configure and report exact errors.
Update docs/ohos/PORTING_STATUS.md.
```

---

## TASK-003：最小 Qt/QML HAP

### Prompt

```text
Create the minimum packaging/ohos integration needed to produce and install a HAP.

Target path:
HAP launch -> Qt runtime -> QApplication -> QQmlApplicationEngine -> minimal Mixxx QML splash.

Do not enable audio, HID, bulk USB, vinyl control, or broadcast unless required by the build.
Use existing CMake options to disable optional components.

Prefer Qt Harmony QPA/harmonydeployqt integration.
Do not create a custom XComponent renderer unless Qt QPA is proven blocked.

Definition of done:
- HAP built
- HAP installed on a real HarmonyOS device
- app launches
- QML window is visible
- hilog shows the startup stages
- no Java/JNI dependency remains in the OHOS path

Record commands and logs in PORTING_STATUS.md.
```

---

## TASK-004：CoreServices 启动闭环

### Prompt

```text
Continue from the working QML HAP.

Goal:
Reach Mixxx CoreServices initialization and the real res/qml/main.qml on OHOS.

For every failure:
- identify the first failing subsystem
- identify whether it is core logic, dependency, filesystem, platform API, or packaging
- fix the correct layer

Do not create cascading stubs.
If a non-MVP subsystem blocks startup, disable it only through its existing build feature option when possible.

Definition of done:
- real MainWindow.qml loads
- Mixxx Core reports ready
- Settings can open
- app can exit cleanly
```

---

## TASK-005：响应式 DeviceProfile

### Prompt

```text
Refactor the current mobile/desktop QML detection into a reusable responsive DeviceProfile.

The same UI code must support:
- compact phone window
- medium tablet window
- expanded PC window

Use logical window size and input capabilities rather than OS name.
Do not create a separate OHOS QML skin.

Preserve Android behavior as much as possible.

Implement first-pass:
- Compact
- Medium
- Expanded

Make fullscreen behavior independent from 'is mobile'.
Make native/global menu behavior capability-based.

Do not redesign all controls in this task.

Definition of done:
- existing QML runs
- three window classes can be forced for development
- screenshots demonstrate three layouts
- no regressions in QML loading
```

---

## TASK-006：OHAudio 技术探针

### Prompt

```text
Build an isolated OHAudio probe before integrating Mixxx.

Requirements:
- enumerate or report the default output route if API allows
- create an output stream
- generate a sine wave or deterministic PCM buffer
- play continuously
- report sample rate, channels, callback frame count and callback timing
- count underruns/stream errors
- stop and destroy cleanly

No Mixxx Engine changes.
No PortAudio integration yet.

The probe must run on the same device and SDK as the Mixxx HAP.

Write:
docs/ohos/OHAUDIO_PROBE.md

Gate:
Stable PCM output for at least 10 minutes.
```

---

## TASK-007：PortAudio OHOS Backend Spike

### Prompt

```text
Using the validated OHAudio probe, implement a minimal PortAudio OHOS host API spike.

Goal:
Make a PortAudio test application enumerate and open an OHAudio-backed output stream.

Preserve PortAudio callback semantics.
Do not integrate into Mixxx until standalone PortAudio playback works.

Implement only what first playback requires:
- initialize
- enumerate/default output
- open
- start
- callback
- stop
- close
- terminate

Add TODOs for input, hotplug and multiple devices.

Measure callback timing and xrun behavior.
```

---

## TASK-008：Mixxx First Audio Playback

### Prompt

```text
Integrate the validated PortAudio/OHAudio backend into Mixxx.

Goal:
Load one local imported track into Deck 1 and play it through OHAudio.

Prefer keeping SoundDevicePortAudio unchanged.
Changes to SoundManager or Engine must be justified by a demonstrated abstraction mismatch.

Add structured audio logs and xrun counters.

Definition of done:
- load track
- play
- pause
- seek
- waveform remains in sync
- no periodic xrun under normal UI use
- 30-minute playback test passes
```

---

## TASK-009：HarmonyOS Picker + Library Import

### Prompt

```text
Implement OHOS system file selection and a first reliable Mixxx import path.

MVP approach:
- select file with system picker
- obtain authorized access
- if necessary copy into app-managed storage
- pass a normal filesystem path into existing Mixxx track/library code

Do not refactor all Mixxx decoders for URI access in this task.

Validate:
- mp3/flac/wav as available
- metadata scan
- waveform analysis
- DB persistence
- restart and reopen
```

---

## TASK-010：MIDI Platform Adapter

### Prompt

```text
Investigate and implement the HarmonyOS MIDI adapter using current official HarmonyOS APIs.

Map the platform MIDI transport into Mixxx's existing ControllerManager/MIDI abstractions.
Do not modify controller mapping semantics.

Start with:
- enumerate
- open
- input
- output
- hotplug

Then validate one real DJ MIDI controller.
```

---

# 17. Codex 遇到阻塞时的决策树

```text
编译错误？
  |
  +-- upstream code assumes OS API?
  |      -> add a platform adapter
  |
  +-- third-party dependency missing?
  |      -> port dependency / valid feature-disable
  |
  +-- Qt API unavailable?
  |      -> verify Qt Harmony support first
  |
  +-- platform capability genuinely absent?
         -> document limitation; don't fake behavior
```

运行时错误：

```text
startup crash
  -> find first failing subsystem
  -> obtain stack
  -> reproduce minimal
  -> classify ownership layer
  -> fix one layer
```

音频错误：

```text
no sound
  -> Engine produces data?
  -> PortAudio callback runs?
  -> OHAudio callback runs?
  -> frames non-zero?
  -> format correct?
  -> route correct?
```

UI 错误：

```text
layout bad
  -> window metrics?
  -> DeviceProfile class?
  -> QML constraint/minWidth?
  -> safe area?
```

---

# 18. 代码审查 Checklist

每个 OHOS PR / commit review：

## 架构

- [ ] 修改是否发生在正确层？
- [ ] 是否复制了 Android 代码而没有抽象？
- [ ] 是否污染 Mixxx core？
- [ ] 是否可未来 upstream？

## 平台

- [ ] 使用 `Q_OS_HARMONY` / 统一项目宏？
- [ ] 没有伪造 Android？
- [ ] 没有硬编码手机路径？

## 实时音频

- [ ] callback 内无阻塞？
- [ ] callback 内无动态大分配？
- [ ] callback 内无大量日志？
- [ ] xrun 可观测？

## UI

- [ ] Compact/Medium/Expanded 都不明显破坏？
- [ ] Touch 和 Pointer 都可操作？
- [ ] 没有新增固定设备分辨率判断？

## 回归

- [ ] 非 OHOS 平台代码路径没有被无意改变？
- [ ] QML lint / unit test 能跑的已跑？
- [ ] 真机证据已写入进展文档？

---

# 19. 第一阶段成功定义

不要把“能安装 HAP”定义为移植成功。

### Alpha 0 — Boot

```text
HAP
-> Qt
-> QML
-> CoreServices
```

### Alpha 1 — Playback

```text
Import track
-> Deck load
-> OHAudio playback
-> waveform
-> play/pause/seek
```

### Alpha 2 — DJ Core

```text
2 Deck
Mixer
EQ
Crossfader
Library
headphone/basic route
```

### Alpha 3 — Adaptive

```text
Phone
Tablet
PC
```

### Beta 1 — Controller

```text
MIDI
USB/HID
hotplug
```

### Beta 2 — Stability

```text
long playback
large library
background/lifecycle
thermal
memory
```

---

# 20. 推荐优先级

按投入产出比：

```text
P0  Build baseline
P1  HAP + Qt/QML
P2  CoreServices
P3  Basic responsive UI
P4  OHAudio
P5  File/library
P6  2-deck stability
P7  MIDI
P8  USB/HID
P9  advanced audio routing
P10 performance/lifecycle
P11 release
```

不要最开始做：

```text
4 Deck 完整布局
全部 FX
全部 HID
Broadcast
Vinyl
所有 codecs
复杂 PC 多窗口
```

这些会掩盖真正的 bring-up blocker。

---

# 21. 参考源码位置

## Mixxx

主仓库：

```text
https://github.com/mixxxdj/mixxx
```

本次调研基线：

```text
https://github.com/mixxxdj/mixxx/tree/bcfb7956315e64d383c570dcb9e79a06a883e335
```

优先阅读：

```text
CMakeLists.txt
src/main.cpp
src/qml/qmlapplication.cpp
src/qml/qmlapplication.h
res/qml/main.qml
res/qml/MainWindow.qml
res/qml/Deck.qml
res/qml/Library.qml
res/qml/Settings/AudioRouter.qml
src/soundio/portaudioenumerator.cpp
src/soundio/sounddeviceportaudio.cpp
src/soundio/soundmanager.cpp
src/controllers/hid/hiddevice.cpp
src/controllers/hid/hiddevice.h
packaging/android/AndroidManifest.xml
packaging/android/src/org/mixxx/MainActivity.java
packaging/android/src/org/mixxx/UsbPermission.java
```

Android 开发 Epic：

```text
https://github.com/mixxxdj/mixxx/issues/15679
```

Android build wiki：

```text
https://github.com/mixxxdj/mixxx/wiki/Building-for-Android
```

---

# 22. Qt / HarmonyOS 参考

Qt 官方 HarmonyOS 构建 Wiki：

```text
https://wiki.qt.io/Building_Qt6_for_HarmonyOS
```

Qt 关于 HarmonyOS C/C++ / vcpkg 的说明：

```text
https://www.qt.io/blog/building-c/c-libraries-for-harmonyos-with-vcpkg
```

注意：Qt HarmonyOS 支持在 2026 年仍属于快速发展路径。

因此必须：

- pin Qt version/commit
- 不追 main 漂移
- 升级 Qt 时单独做 regression

---

# 23. 最终架构目标

理想状态应是：

```text
                    Mixxx Core
                       |
             +---------+---------+
             |                   |
       Shared QML UI       Shared SoundManager
             |                   |
       DeviceProfile      SoundDevicePortAudio
             |                   |
       Qt Quick/RHI             PortAudio
             |                   |
      Qt Harmony QPA           pa_ohos
             |                   |
       Harmony Window           OHAudio
             |                   |
             +---------+---------+
                       |
                    HarmonyOS

Phone / Tablet / PC
       |
same codebase
       |
responsive layout + capabilities
```

而不是：

```text
Mixxx Desktop
Mixxx Android
Mixxx OHOS Phone
Mixxx OHOS Tablet
Mixxx OHOS PC
```

五套分叉。

---

# 24. 给 Codex 的最终原则

> **Port the platform, not the application.**

Mixxx 已经有成熟的 Engine、Library、Mixer、Controller 和新的 QML UI。

OHOS 移植工作的核心不是“重写 Mixxx”，而是把缺失的平台能力补齐：

```text
Build
Window
Input
Audio
Files
Lifecycle
USB/MIDI
Packaging
```

然后让上层 Mixxx 尽可能不知道它正在运行在 OHOS。

每发现一个问题，先问：

> “这是否真的是 Mixxx core 的问题，还是平台适配层没有满足原有契约？”

默认答案应该优先从平台层验证。

这样才能同时保证：

- 手机上能用
- 平板上好用
- PC 上专业可用
- Android 路线可继续复用
- 后续同步 Mixxx upstream 不至于失控

---

# 25. 建议 Codex 第一句执行指令

将本文放入仓库根目录或：

```text
docs/ohos/MIXXX_OHOS_CODEX_PORTING_GUIDE.md
```

然后向 Codex 下达：

```text
严格阅读 docs/ohos/MIXXX_OHOS_CODEX_PORTING_GUIDE.md。

从 TASK-001 开始，不要直接修改业务代码。
先完成 OHOS port baseline audit、依赖矩阵和 Android reference map。

所有事实以当前 checkout 为准；文档中的上游 SHA 只是本次规划基线。
如果当前源码与文档描述不一致，以源码为准并更新文档。

禁止把 OHOS 伪装成 Android；Android 只作为实现参考。
禁止一次性大面积修改。
每个任务必须更新 PORTING_STATUS.md，并提供构建命令、真机证据和下一步。
```

---

**END**

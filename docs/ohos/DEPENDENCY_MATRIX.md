# Mixxx → OHOS 依赖矩阵

> 规则：任何“关闭”必须通过树内既有 CMake option 实现，不得删代码；`default_option` 型选项在依赖未探测到时自动关闭，无需显式动作。
> 上游事实核对日期：2026-09-26 @ `bcfb7956`。

## Tier 0 — 首次 QML 启动必需

| 组件 | Mixxx 侧 | OHOS 状态 | 获取方式 | 优先级 | 备注 |
|---|---|---|---|---|---|
| Qt Core/Gui/Quick/QuickControls2/Sql/Network/Svg/Concurrent | `find_package(Qt6)` 多处 | 无预编译；qtbase dev 已含 OHOS 后端 | 源码编译（pin dev commit） | P0 | host Linux + cross ohos-clang 两段编译 |
| Qt ShaderTools (qsb) / Qt Tools (host) | qmlcachegen 等 | 同上 | 同上，host 编译 | P0 | 交叉编译 Qt 时必需 |
| C++ 运行时 | libc++_shared | SDK sysroot 自带 musl+libc++ | NDK | P0 | 对齐 Android `c++_shared` 语义 |
| SQLite | Qt Sql 驱动 | Qt 自带 sqlite 插件 | 随 Qt 编译 | P0 | Mixxx 库数据库 |

## Tier 1 — Core 编译硬依赖（链接必须存在）

| 组件 | Mixxx 侧 | OHOS 状态 | 获取方式 | 优先级 | 备注 |
|---|---|---|---|---|---|
| PortAudio | `find_package(PortAudio REQUIRED)`（CMakeLists.txt:3806） | vcpkg `portaudio` 在 arm64-ohos 下可编但无 hostapi（零设备） | vcpkg arm64-ohos | P1 | 先满足链接；真正输出靠 P4 `pa_ohos` hostapi patch |
| TagLib | REQUIRED | 纯 C++ | vcpkg arm64-ohos | P1 | |
| libsndfile / FLAC / Vorbis / OpusFile | 按 option 探测 | 纯 C | vcpkg arm64-ohos | P1 | Opus=OPTION `OPUS` |
| chromaprint | REQUIRED (分析) | 纯 C++ | vcpkg arm64-ohos | P1 | |
| libexif等探测型小库 | optional | - | vcpkg；缺失则对应功能 OFF | P1 | |

## Tier 2 — 首次真实出声（P4）

| 组件 | Mixxx 侧 | OHOS 状态 | 获取方式 | 优先级 | 备注 |
|---|---|---|---|---|---|
| OHAudio (OH_AudioRenderer/Capturer) | — | SDK native `oaudio` | 系统库 `libohaudio.so` | P4 | 通过新增 PortAudio `pa_ohos` hostapi 接入，保持 `SoundDevicePortAudio` 不改 |
| pa_ohos hostapi patch | — | 需自研（vcpkg port overlay） | `ports/ohos/` overlay | P4 | 参考 Android Oboe 在 PortAudio 的接入方式 |

## Tier 3 — 音质/格式增强（P2 后期~P3）

| 组件 | Option | 默认 | OHOS 策略 |
|---|---|---|---|
| FFmpeg | `FFMPEG` | `FFmpeg_FOUND` 探测 | vcpkg arm64-ohos；本机已有 `D:\Git\third_party_ffmpeg` 的 OHOS 编译先例（Ren'Py 项目，仅参考参数） |
| RubberBand | `RUBBERBAND` | ON | vcpkg；若失败可显式 `-DRUBBERBAND=OFF` |
| SoundTouch | 内建 | - | 随源码编 |
| KeyFinder | `KEYFINDER` | ON | vcpkg，失败则 OFF |
| LILV(LV2) | `LILV` | 探测 | 首轮 OFF |

## Tier 4 — Controllers（P7/P8）

| 组件 | Option | 默认 | OHOS 策略 |
|---|---|---|---|
| HID (hidapi) | `HID` | **ON** | bring-up 首轮显式 `-DHID=OFF`（OHOS 上 hidapi 无后端） |
| BULK (libusb) | `BULK` | `LibUSB_FOUND;NOT WIN32` | 首轮 `-DBULK=OFF` |
| PortMidi | `PORTMIDI` | `NOT ANDROID`（OHOS 会误判为 ON） | 显式 `-DPORTMIDI=OFF`；P7 换 HarmonyOS MIDI 适配层 |
| USB 权限桥 | Android Java | - | P8 用 OHOS USB Kit 自研桥 |

## Tier 5 — 可选/暂缓

| 组件 | Option | OHOS 策略 |
|---|---|---|
| Broadcast (shout-idjc) | `BROADCAST` | 首轮 `-DBROADCAST=OFF`（依赖树重） |
| VinylControl | `VINYLCONTROL` | OFF（外设依赖） |
| Stem | `STEM`（随 FFMPEG） | 跟随 FFMPEG |
| PipeWire | `PIPEWIRE` | OHOS 无 PipeWire，探测失败自动 OFF，无需处理 |
| Modplug/WavPack/FAAD/MAD | 各自 option | 探测型，随 vcpkg 可得性 |

## vcpkg 使用约定（OHOS）

- triplet：`arm64-ohos`（上游 community triplet；`VCPKG_CMAKE_SYSTEM_NAME=OHOS`，`VCPKG_ENV_PASSTHROUGH_UNTRACKED=OHOS_SDK_ROOT`）
- 编译前必须 `export OHOS_SDK_ROOT=<sdk>/openharmony`
- patch/overlay 放 `cmake/ohos/ports/`（后续按需建），不得手改 vcpkg cache
- 每成功移植一个库，在本文档追加记录行：版本 / port 修订 / patch 列表 / 已知限制

## 依赖分层与 CMake preset 对应

建议 preset（TASK-002 后补）：`ohos-bringup` = `-DQML=ON -DHID=OFF -DBULK=OFF -DPORTMIDI=OFF -DBROADCAST=OFF -DVINYLCONTROL=OFF -DBUILD_TESTING=OFF -DBUILD_BENCH=OFF`

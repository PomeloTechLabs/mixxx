# Mixxx → HarmonyOS (OHOS) Porting Status

> 维护规则：每个任务结束后必须追加一节 `## Task P?.?`（格式见仓库根 `docs/ohos/MIXXX_OHOS_CODEX_PORTING_GUIDE.md` §6.2）。
> 所有事实以当前 checkout 为准。

## Baseline

| 项 | 值 |
|---|---|
| 上游仓库 | https://github.com/mixxxdj/mixxx |
| 基线 SHA | `bcfb7956315e64d383c570dcb9e79a06a883e335`（main, 2026-09-24, "Merge pull request #16047 from ronso0/lib-date-format-fixes"） |
| 工作分支 | `feature/ohos-port`（HEAD = 基线 SHA） |
| 本地仓库 | `D:\Git\mixxx`（独立 git repo，parent `D:\Git` 为无关的个人工作区仓库） |
| 网络代理 | `http://127.0.0.1:8080`（已写入本仓库 `git config http.proxy`） |

## 环境清单（2026-09-26 实测）

| 组件 | 版本/路径 | 备注 |
|---|---|---|
| HarmonyOS 命令行工具（Windows 侧） | `F:\command-line-tools`，Version 6.1.1.290 | hvigor 6.24.3, ohpm 6.1.2.285 |
| HarmonyOS SDK | `F:\command-line-tools\sdk\default\openharmony`，API 24（Ohos_sdk_public 6.1.1.125） | NDK llvm 在 `native/llvm` |
| Windows DevEco SDK | `C:\Program Files\Huawei\DevEco Studio\sdk\default` | `hdc`: `...\openharmony\toolchains\hdc.exe` |
| Docker 构建镜像 | `winehua-dev:latest`（ubuntu:24.04 + cmake/ninja/node/openjdk-17/clang） | 复用自 VintagePomelo 工程，SDK 以 bind mount 挂 `/apps/harmony` |
| Docker 参考工程 | `F:\VintagePomelo-Workspace`（`prepare-release-container.sh` 展示了 SDK 挂载与 hvigor 用法） | 只参考流程，不共享源码 |
| Qt for OHOS | **无官方预编译包**。qtbase `dev` 分支已含 OHOS 支持（`mkspecs/ohos-clang`、`src/plugins/platforms/ohos` QPA、`cmake/QtHarmonyOSHelpers.cmake`）；6.9.0/6.10.0 tag 均无 → 需源码编译并 pin dev commit | 见 `cmake/ohos/BUILD_QT_OHOS.md` |
| vcpkg | 上游 master 已有 community triplet `arm64-ohos`（要求环境变量 `OHOS_SDK_ROOT`，`VCPKG_CMAKE_SYSTEM_NAME=OHOS`） | 第三方库主通道 |
| 本机 Qt | 无（`C:\Qt`、`F:\Qt` 均不存在） | host Qt 也需从源码编 |

## 参考文档

- `docs/ohos/MIXXX_OHOS_CODEX_PORTING_GUIDE.md` — 总指导书（Codex 执行约束）
- `docs/ohos/DEPENDENCY_MATRIX.md` — 依赖矩阵与分层
- `docs/ohos/ANDROID_REFERENCE_MAP.md` — Android 参考实现映射
- `docs/ohos/BUILD_QT_OHOS.md` — Qt for OHOS 源码编译手册

## Task P0.1 — 上游基线固定 + TASK-001 审计

Status: PASS

### Goal
固定可复现上游基线；完成 OHOS port baseline audit（TASK-001），不修改任何业务代码。

### Changed files
- 新增 `docs/ohos/PORTING_STATUS.md`、`docs/ohos/DEPENDENCY_MATRIX.md`、`docs/ohos/ANDROID_REFERENCE_MAP.md`、`docs/ohos/MIXXX_OHOS_CODEX_PORTING_GUIDE.md`（本批唯一非 CMake 改动均为文档）

### Build
未执行（本任务纯审计，允许）。

### 关键审计结论（2026-09-26，树内核实）
1. 平台检测：顶层 `CMakeLists.txt` 用 `CMAKE_SYSTEM_NAME STREQUAL Android` 走 Android 分支；Apple/WIN32/WSL/Emscripten 均有显式分支；**无任何 OHOS/Harmony 处理** → TASK-002 需新增独立分支。
2. Feature 选项（真实默认值）：
   - `QT6`=ON；`QML`=cmake_dependent_option（Qt ≥ 6.4 时 ON，定义 `MIXXX_USE_QML`）
   - `HID`=ON；`BULK`=default_option(`LibUSB_FOUND;NOT WIN32`)；`BROADCAST`=dependent_option(NOT IOS 时 ON)；`VINYLCONTROL`=default(`NOT MACAPPSTORE`)
   - `FFMPEG`/`STEM`/`LILV`/`OPUS`/`MAD`/`MODPLUG`/`WAVPACK`/`FAAD`/`PORTMIDI`(=`NOT ANDROID`)/`PIPEWIRE` 均为依赖探测型 default_option → OHOS 上未装依赖时自动关闭，符合 bring-up 预期
   - `BUILD_TESTING`/`BUILD_BENCH` 依赖 GTest/benchmark 探测
3. 音频依赖是硬性的：`find_package(PortAudio REQUIRED)`（CMakeLists.txt:3806）→ 即使无音频输出目标，也必须提供可编译的 PortAudio（OHOS 上先用无 hostapi 的 PortAudio 满足链接，`pa_ohos` 后端属 P4）。
4. `src/soundio/` 已存在 PipeWire 独立 enumerator/SoundDevice 的先例，说明 Mixxx 支持 PortAudio 之外的平台设备枚举层，但首选路线仍是 PortAudio hostapi（指导书 ADR）。
5. Android 特定代码面共 **24 个文件**（`grep Q_OS_ANDROID|__ANDROID__`，含 main.cpp/hid/bulk/soundio/qmlapplication 等）→ 全量清单见 ANDROID_REFERENCE_MAP.md。
6. `packaging/` 已有 android/appimage/debian/flatpak/ios/macos/wix 子目录 → `packaging/ohos/` 是惯例位置。
7. `res/qml/main.qml:11` 的 `isMobile` 判定、移动端全屏/尺寸逻辑与指导书描述一致（P3 再改）。
8. `src/main.cpp:199` Android JNI hack（禁用 accessibility）是 platform-quirk 参考，OHOS 不需要照抄。

### Remaining blocker
- Qt for OHOS 无预编译 → P1 关键路径，已启动源码编译（见 Task P1.1）。

### Next step
- TASK-002：CMake OHOS 平台分支。

## Task P1.1 — Qt for OHOS 源码编译（长杆启动）

Status: IN PROGRESS（后台 Docker 编译）

### Goal
产出一套 pin 版本的 Qt host(Linux) + cross(ohos-clang arm64-v8a) 安装树，供后续 Mixxx OHOS configure 使用。

### 方法
- 脚本：`cmake/ohos/build-qt-ohos.sh`，文档 `cmake/ohos/BUILD_QT_OHOS.md`
- 模块：qtbase/qtdeclarative/qtshadertools/qtsvg/qtimageformats/qttools（dev 分支 pin commit，SHA 见脚本顶部）
- 容器：`winehua-dev` 镜像 + `F:\command-line-tools` → `/apps/harmony`（`OHOS_SDK=/apps/harmony/sdk/default/openharmony`）

### Build
命令：见 `cmake/ohos/BUILD_QT_OHOS.md`（Docker 容器内运行）
结果：（待编译日志填充）

### Evidence
日志：`docs/ohos/logs/qt-host.log` / `qt-cross.log`

### Remaining blocker
- qtbase dev OHOS 支持为 2026 新合入代码，未随任何 release tag 发布；若 configure 失败需回读 qtbase dev 的 OHOS CI/文档修正参数。

### Next step
- TASK-002：CMake OHOS 平台分支。

## Task P1.2 — TASK-002：CMake OHOS 平台分支

Status: PASS（CMake 层；Qt/依赖环境缺失为预期 blocker）

### Goal
最小 OHOS 平台检测：独立分支、项目级宏、不落入 Android/Linux 路径、不回归桌面/Android。

### Changed files
- `CMakeLists.txt`（4 处插入，见 git diff；Windows/macOS/Linux/Android 行为零变更）：
  1. `project()` 后新增 OHOS 检测块（`CMAKE_SYSTEM_NAME STREQUAL OHOS` → `OHOS=ON` + arch 打印）
  2. WSL 守卫条件追加 `AND NOT CMAKE_TOOLCHAIN_FILE MATCHES "ohos\.toolchain\.cmake$"`（Docker Desktop 容器内核 uname 含 "microsoft"，属误报场景；仅影响 OHOS 交叉）
  3. install-dir 链新增 `elseif(OHOS)`
  4. compile-definitions 链新增 `elseif(OHOS)`：`__UNIX__ MIXXX_OS_OHOS`（**不定义** `__LINUX__`、不伪装 `__ANDROID__`）
  5. Qt6 段落内新增 `[OHOS]` 特性汇总状态行

### Build（configure probe）
```bash
docker run --rm \
  --mount "type=bind,src=D:/Git/mixxx,dst=/data/src/mixxx" \
  --mount "type=bind,src=F:/command-line-tools,dst=/apps/harmony,readonly" \
  winehua-dev cmake -S /data/src/mixxx -B /tmp/build-ohos -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=/apps/harmony/sdk/default/openharmony/native/build/cmake/ohos.toolchain.cmake \
  -DOHOS_ARCH=arm64-v8a -DCMAKE_BUILD_TYPE=Release
```
结果：按预期在第一个硬依赖处停止。
- `-- [OHOS] target detected: arch=arm64-v8a` ✅
- `-- The C/CXX compiler identification is Clang 15.0.4`（NDK llvm）✅
- `-- Could NOT find ccache`（无害）
- `CMake Warning ... Could not find a package configuration file provided by "Qt6"` →
  `CMake Error at CMakeLists.txt:239 (fatal_error_missing_env)`（`CMakeLists.txt:2400` 调用点）✅ 预期 blocker

### 教训记录
- 工具链文件 `set(CMAKE_SYSTEM_NAME OHOS)` 在 `project()` 之前不可见（Android 靠命令行 cache 变量故无此问题）→ OHOS 检测必须放在 `project()` 之后；WSL 守卫改用 toolchain 文件名匹配（任何时刻可见）。

### Device validation
未涉及。

### Evidence
`[OHOS]` 状态行与 Qt6 缺失错误见本文件 Task P1.2 / 构建日志（重复输出于 probe6-8，2026-09-26）。

### Remaining blocker
- Qt for OHOS（Task P1.1 编译中）与 vcpkg arm64-ohos 依赖（下一任务）。

### Next step
- 等 Qt cross 产物 → vcpkg arm64-ohos 编 Tier1 依赖 → Mixxx OHOS configure 全量推进（TASK-003 前置）。

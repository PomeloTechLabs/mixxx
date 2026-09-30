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
命令：`docs/ohos/BUILD_QT_OHOS.md` §3（winehua-dev 容器，STAGE=all，24 jobs，volume `mixxx-ohos-qt-build`/`mixxx-ohos-qt-out`）
结果：RUNNING（2026-09-26 启动；host qtbase configure 已通过，交叉工具链构建中）

### Evidence
日志：`docs/ohos/logs/qt-ohos-build.log`（不随 git 提交）
pin 校验：5 个模块 HEAD 与 `cmake/ohos/qt-ohos-pins.txt` 一致 ✅
host GL 头：容器内可用（未触发 tools-only 降级）✅

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

## Task P1.3 — vcpkg arm64-ohos Tier-1 依赖

Status: PASS

### Goal
用 vcpkg community triplet `arm64-ohos` 编出 Mixxx REQUIRED 依赖（静态库），验证官方 OHOS 工具链通路。

### Build
命令：`docker run winehua-dev … vcpkg install --triplet arm64-ohos --overlay-ports=/data/src/mixxx/cmake/ohos/ports <ports>`（volume `mixxx-ohos-vcpkg`，commit `6df3b6ad` 记录于 volume `VCPKG_COMMIT.txt`）
结果：**PASS** — 30 个包全部安装（8.3 min），产物在 volume `/data/vcpkg/vcpkg/installed/arm64-ohos/{lib,share}`。

直接安装：zlib libflac mp3lame libogg libvorbis libsndfile libebur128 soundtouch taglib rubberband portaudio fftw3 chromaprint opus opusfile
连带（依赖/feature）：libsamplerate、mpg123、sleef、**FFmpeg 全家桶静态库**（libsndfile external-libs 拉入 avcodec/avformat/avutil/avfilter/avdevice — `FFMPEG` option 有望直接启用）、jack2（portaudio 依赖占位）。

### Changed files
- `cmake/ohos/ports/mp3lame/`（新增 overlay port）

### 关键修复：mp3lame arm64-ohos
第一现场：lame 3.100 自带 GNU `config.sub/config.guess` 过旧，不识别 `aarch64-unknown-linux-ohos` 三元组 → configure 失败。
修复：overlay port 以"上游 4 个 patch 应用后的源码树"为基准生成 config-tools 更新 diff（`add-macos-universal-config.patch` 也改 config 工具，故补丁必须最后应用）；当前 GNU config（1992-2026）实测接受 ohos 三元组。
教训：vcpkg 新版 `vcpkg-make` 会延迟重解压源码（`.tmp`→`.clean`），portfile 中 `file(COPY)` 方案无效，必须走 patch 链。

### Qt 侧并行修复：node-addon-api
Qt for OHOS configure 硬性要求 node-addon-api（QPA 链接 NAPI C++ 头）：`ERROR: Qt for OHOS requires node-api-addon. Set NODE_ADDON_API_ROOT`。
修复：`build-qt-ohos.sh` 自动拉取 nodejs/node-addon-api `v8.9.2`（头文件库，无版本要求）到 volume `/data/out/extras/`，cross stage 传 `-DNODE_ADDON_API_ROOT`。

### Evidence
- `docs/ohos/logs/vcpkg-ohos-deps6.log`（成功轮；deps2-5 为失败迭代）
- `docs/ohos/logs/qt-ohos-cross.log`（node-addon-api 拉取 + `-- OHOS build detected`）

### Remaining blocker
- Qt cross qtbase 编译进行中（cross 阶段从 node-addon-api 修复后续跑）。

### Next step
- Qt cross 完成 → TASK-003 前置：Mixxx OHOS 全量 configure（Qt cross + vcpkg 组合）。

## Task P1.4 — Mixxx OHOS 全量 configure + libmixxx.so 编译

Status: PASS（configure + build 完成；产物待 HAP 打包与真机验证）

### Goal
Mixxx 源码树在 OHOS 工具链下完成全量 CMake configure 并编译出 `libmixxx.so`（HAP 主模块）。

### Build（最终成功形态）
```bash
# 容器：winehua-dev，挂载 mixxx repo / vcpkg volume / qt-out volume / SDK
export OHOS_SDK_ROOT=/apps/harmony/sdk/default/openharmony
cmake -S /data/src/mixxx -B /data/mixxx-build/ohos -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$OHOS_SDK/native/build/cmake/ohos.toolchain.cmake \
  -DOHOS_ARCH=arm64-v8a \
  -DCMAKE_FIND_ROOT_PATH="/data/out/qt-ohos;/data/vcpkg/vcpkg/installed/arm64-ohos" \
  -DQT_HOST_PATH=/data/out/qt-host \
  -DMIXXX_VCPKG_ROOT=/data/vcpkg/vcpkg \
  -DVCPKG_TARGET_TRIPLET=arm64-ohos \
  -DQML=ON -DHID=OFF -DBULK=OFF -DPORTMIDI=OFF -DBROADCAST=OFF \
  -DVINYLCONTROL=OFF -DQTKEYCHAIN=OFF -DENGINEPRIME=OFF -DKEYFINDER=OFF \
  -DFFMPEG=ON -DSTEM=ON \
  -DFFmpeg_AVCODEC_VERSION=61.19.101 -DFFmpeg_AVFORMAT_VERSION=61.7.100 \
  -DFFmpeg_AVUTIL_VERSION=59.39.100 -DFFmpeg_SWRESAMPLE_VERSION=5.3.100 \
  -DFFmpeg_{AVCODEC,AVFORMAT,AVUTIL,SWRESAMPLE}_LIBRARIES=<vcpkg>/usr/lib/lib*.so \
  -DProtobuf_PROTOC_EXECUTABLE=<vcpkg>/packages/protobuf_x64-linux/tools/protobuf/protoc \
  -DCMAKE_BUILD_TYPE=Release
cmake --build /data/mixxx-build/ohos -j 24
```

### 产物（2026-09-27 验证）
- `libmixxx.so`：34,980,144 字节，AArch64 DYN（HAP 模块）✅
- DT_NEEDED：ffmpeg 动态库（用户 7.1.1 预编译 .so）+ libGLESv3 + Qt6 各模块

### 关键修复清单（迭代 configure1-13 / build1-24）
| 层 | 问题 | 修复 |
|---|---|---|
| CMake | WSL 守卫误报（vcpkg triplet 不含 toolchain 名） | triplet 名匹配跳过 |
| CMake | MIXXX_VCPKG_ROOT 校验假设 buildenv 包 | ohos triplet 放行标准 vcpkg checkout |
| CMake | OHOS 编译定义分支在 UNIX 后不可达 | 分支前移（OHOS 在 toolchain 下 UNIX=TRUE） |
| CMake | HAP 模块缺 LIBRARY DESTINATION | install 排除 OHOS（打包由 packaging/ohos） |
| CMake | 桌面 GL 依赖 | OHOS 分支显式链 sysroot GLESv3 + QT_OPENGL_ES_2 |
| CMake | X11/DBus/Upower/QTKEYCHAIN/ENGINEPRIME/KEYFINDER | 桌面栈按 option/条件跳过 |
| CMake | SQLite3 alias 在未找到时崩 | 防御性 target 存在检查 |
| CMake | sleef/sleefdft/mpg123/OpenMP 传递闭包 | OHOS 显式闭包（libmpg123.a + -static-openmp） |
| Qt | 缺 Core5Compat/Multimedia（OHOS 后端） | 补编 qt5compat + qtmultimedia（libohosmediaplugin.so） |
| vcpkg | lame 3.100 config.sub 不识 ohos 三元组 | overlay port 更新 GNU config 工具 |
| vcpkg | freetype→libpng genout 无法处理 multiarch 头 | freetype overlay 去 png |
| FFmpeg | NEON 汇编非 PIC 无法链入 so | **采用用户预编译 FFmpeg 7.1.1 共享库**（F:\MyProject\third_party_ffmpeg，avcodec 61.19.101，满足 ≥58.35.100；vcpkg 9.0.2 的头/库备份在 installed/ffmpeg902-backup/） |
| 源码 | CfgkeyAndShortcut 聚合括号初始化（NDK clang 15） | 显式构造函数 |
| 源码 | desktophelper freedesktop DBus | OHOS 走 QDesktopServices |
| pkg-config | CMake 4 cross 用 sysroot 覆盖 PKG_CONFIG_LIBDIR | 版本号显式传 cache 变量 |

### Evidence
- `docs/ohos/logs/mixxx-ohos-configure1-13.log`、`mixxx-ohos-build1-24.log`
- `[OHOS] Qt6 6.13.0, QML=ON, HID=OFF, BULK=OFF, BROADCAST=OFF, VINYLCONTROL=OFF, FFMPEG=ON, STEM=ON`

### Remaining blocker
- HAP 打包（packaging/ohos）与真机启动验证（TASK-003/004）。
- FindFFmpeg 版本检测依赖 pkg-config（CMake 4 cross 下被 sysroot 接管）→ 长期修法待定（显式版本参数为当前 workaround）。

## Task P1.5 — TASK-003：HAP 壳工程 + 打包（签名待材料）

Status: PARTIAL（HAP 构建 PASS；真机安装阻塞在签名材料配对）

### Goal
HAP 安装 -> Qt QPA -> QApplication -> QML 启动链真机可见。

### Changed files
- `packaging/ohos/`（基于qtbase `src/harmonyos/templates`，bundleName `com.pomelo.mixxx`）
  - `entry/src/main/cpp/{qtmixxxboot.cpp,Main.qml,CMakeLists.txt}`：boot 库（QGuiApplication + QML splash）
  - `build-hap.sh`：boot 编译 + libs 收集（Qt 库/插件/QML + 用户 FFmpeg + libmixxx.so 预留）+ hvigorw assembleHap
  - `scripts/sign-hap.sh`、`scripts/verify-materials.sh`
  - `entry/build-profile.json5`：移除 externalNativeOptions（原生库由 build-hap.sh 预编）
- SDK 版本 `6.1.0(23)` → `6.1.1(24)`（匹配本机 CLT 6.1.1.290）

### Build
命令：`docker run … winehua-dev bash /data/src/mixxx/packaging/ohos/build-hap.sh`
结果：**unsigned HAP 产出成功** `entry-default-unsigned.hap`（252,820,929 B；libs/arm64 219 项 188 MB）
- boot 库 `libqtmixxxboot.so` 交叉编译 + QML 模块打包（qt_add_qml_module）成功
- hvigor：PackingCheck PASS，卡在 SignHap（需匹配材料，见下）

### Device validation
设备：HUAWEI MLR-AL10（平板，arm64-v8a，UDID B4BB7C38…7219），hdc 2.0 正常。
结果：**安装失败（预期）** `install … code:9568322 signature verification failed due to not trusted app source`

### 签名材料配对分析（关键诊断）
HarmonyOS NEXT 要求 **p12 私钥 / .cer 应用证书 / profile 内嵌证书 三者公钥一致**。现有材料三方不匹配：

| 材料 | 证书指纹(SHA256) | 公钥(SHA256) |
|---|---|---|
| `sign/app_debug.p12`（alias `ad`，密码 [REDACTED]） | E19822D2… | 14d310f4… |
| `sign/ad.csr` | — | 14d310f4…（与 p12 一致 ✓） |
| `sign/app_debug.cer` | df21a3c0…（= .ohos/default_g9 证书） | — |
| `sign/mixxx_调试Debug.p7b` 内嵌证书 | 93BB5311… | **5f900b73…（无对应私钥）** |

profile 本身合法（verify-profile PASS；bundle-name=com.pomelo.mixxx；type=debug；device-ids 含本机 UDID ✓），
但它的私钥不在本机。SDK 自带 OpenHarmony 测试根（OpenHarmony.p12/123456）在 HarmonyOS NEXT 商用设备不可用。

### Remaining blocker
**需要与 profile 配对的私钥**。两条获取路径：
- A（推荐）：DevEco Studio 打开 `packaging/ohos` → Project Structure → Signing Configs → 勾选自动生成签名（登录华为账号），DevEco 自动生成/复用证书并申请 com.pomelo.mixxx 的 profile；
- B：AGC 用 `sign/ad.csr` 新建调试证书 → 新 profile → 下载 cer+p7b 替换 `sign/`；
随后 `scripts/verify-materials.sh` 校验三件套 → `scripts/sign-hap.sh` 签名 → hdc install。

## Task P1.6 — 真机启动链打通（签名 + native 加载 + Qt 生命周期）

Status: IN PROGRESS（已越过全部已知崩溃点，待解锁屏幕后做最终可见性验证）

### 签名（已解决）
DevEco Studio 自动签名为 `com.pomelo.mixxx` 生成了自洽三件套（`~/.ohos/config/default_ohos_8KS52…`，alias `debugKey`，
profile type=debug、bundle-name 匹配、device-ids 含本机 UDID）。

之前失败原因：`sign/app_debug.p12`(alias ad, pubkey 14d310f4) / `app_debug.cer`(df21a3c0) / `mixxx_调试Debug.p7b`(dev-cert 5f900b73) 三者互不匹配；
HarmonyOS NEXT 要求 **私钥 = 应用证书 = profile 内嵌证书** 三者公钥一致。

构建/签名命令（Windows 侧 DevEco CLI，密码由 hvigor 解密）：

```
DEVECO_SDK_HOME="C:/Program Files/Huawei/DevEco Studio/sdk" \
JAVA_HOME="C:/Program Files/Huawei/DevEco Studio/jbr" \
node "C:/Program Files/Huawei/DevEco Studio/tools/hvigor/bin/hvigorw.js" \
  --mode module -p module=entry@default -p product=default assembleHap --no-daemon
```

产出 `entry/build/default/outputs/default/entry-default-signed.hap`（SignHap 成功）。
SDK 版本需匹配 DevEco 自带 SDK：`6.1.0(23)`（HarmonyOS 6.0.33 / API 23）。

### 真机运行链修复（按出现顺序）

| 现象 | 根因 | 修复 |
|---|---|---|
| 图标/名称为模板值；点击黑屏 | 启动器缓存 + 文本资源未改 | `app_name`/`QAbility_label` → `Mixxx`；图标用提供的 1024×1024 素材（AppScope app_icon、entry layered background/foreground、startIcon）；卸载重装清缓存 |
| `Load native module failed: @app:…/entry/qohos` | HAP 内库目录名 `libs/arm64/` 不是 ABI 名 | 改为 **`libs/arm64-v8a/`**（运行时映射到 `…/libs/arm64`） |
| 同上 | `libqohos.so` 只在 `platforms/` 子目录 | **复制到 libs 根**：它既是 ArkTS NAPI 入口（`import qpa from 'libqohos.so'`），也是 Qt QPA 插件（`QT_QPA_PLATFORM_PLUGIN_PATH` = libs 根） |
| `Error loading shared library libc++_shared.so` | 未捆绑 C++ 运行时 | 从 NDK 复制 `libc++_shared.so` 进 libs |
| `TypeError: Cannot read property handleAbilityStageOnCreate of undefined` → JS_ERROR 杀进程 | 上述 native 加载失败的直接后果 | 同上（修复后消失） |
| `SIGABRT … makeWindowProxyDataForExistingMainWindowInJsThread`: ability without windowStage | boot 库 5ms 内建窗，竞速 `QAbility.onWindowStageCreate`(55ms) | `Main.qml` `visible:false`，`QTimer::singleShot(1500)` 后再显示 |

### 设备

HUAWEI MLR-AL10（arm64-v8a，UDID B4BB7C38…）。hdc 连接偶发失效（需 `hdc kill && hdc start`）；启动应用需屏幕解锁。

## Task P1.7 — TASK-003 收官：Qt/QML 启动链真机 PASS

Status: **PASS**（P1 Gate 达成：HAP → Qt QPA → QApplication → QML 可见）

### 最终阻塞点与根因（决定性）

`libqohos.so` 在 HAP 内**存在两份**：

- `libs/arm64-v8a/libqohos.so`（ArkTS `import qpa from 'libqohos.so'` 与 Qt QPA 插件路径 `QT_QPA_PLATFORM_PLUGIN_PATH` 都指向 libs 根）
- `libs/arm64-v8a/platforms/libqohos.so`（Qt 插件目录结构中的副本）

两份映射 = **两套独立静态状态（peer 注册表）**：ArkUI XComponent 的 `onAttach` 回调在 NAPI 副本里注册了带 windowStage 的 UI peer，
而 `QOhosWindowProxy::createForExistingMainWindow` 在插件副本里查不到它 → `qFatal`（`makeWindowProxyDataForExistingMainWindowInJsThread`: ability without windowStage）→ SIGABRT。

**修复**：只保留 libs 根的一份（`build-hap.sh` 复制后删除 `platforms/libqohos.so`）。

### 验证证据（2026-09-27）

- 设备：HUAWEI MLR-AL10（API 26 / HarmonyOS 7.0.0.109，arm64-v8a）
- 启动：`aa start -b com.pomelo.mixxx -a QAbility` → `start ability successfully.`
- 进程存活（`pidof` 有值），hilog 中 `makeWindowProxyDataForExistingMainWindow` fatal 计数 = 0
- XComponent surface 创建成功（`handleSurfaceCreated`）
- 截图：`docs/ohos/logs/mixxx_shot.jpeg` — 深色全屏 + 红色描边卡片 + "Mixxx" + "Qt on HarmonyOS - HAP shell OK"

### 本轮还修复（详见 P1.6 表）

- `libs/arm64` → `libs/arm64-v8a`（ABI 目录名）
- 捆绑 `libc++_shared.so`
- QML 窗口延迟显示（`visible:false` + 1.5s 定时）
- 模板占位符（deviceTypes/orientation/description）
- 品牌：名称 Mixxx + 用户提供图标（AppScope/layered/startIcon）
- 页面插桩：`MainWindowNativeNode.ets` 的 XComponent onAttach/onAppear/onDisAppear 打印 hilog

### Next step（TASK-004）

1. `QtAppConstants.APP_LIBRARY_NAME` → `libmixxx.so`，并把 `libmixxx.so` 放入 `entry/libs/arm64-v8a/`；
2. 定位 CoreServices 首个失败点（数据库/设置目录、TagLib、SoundManager）；
3. Mixxx QML（`res/qml`）资源部署进 HAP（含 `/qt/qml` 资源前缀与 QML 导入路径）。

## Task P1.8 — TASK-004 达成：真正的 Mixxx 界面在真机运行

Status: **PASS**（CoreServices 启动闭环 + 完整 Mixxx UI 上屏）

### 关键改动

1. **引导目标切换**：`QtAppConstants.APP_LIBRARY_NAME` → `libmixxx.so`（真正的 Mixxx `main()`），HAP 同时携带 `libmixxx.so`、Qt QML 模块与 Mixxx 资源。
2. **资源部署（决定性）**：HAP 的 `libs/` 目录**只提取 .so**，普通文件在安装时被忽略——实测 `bundleCodeDir/libs/arm64/res` 在设备上不存在，Mixxx 报
   `Critical: Skin directory does not exist`。改为经由 `resources/resfile/res/**` 交付：系统会以**真实文件**形式释放到
   `<bundleCodeDir>/<module>/resources/resfile/res`（实测 `/data/storage/el1/bundle/entry/resources/resfile/res`，9 个子目录齐全）。
3. **路径注入**：`QAbilityStage` 在启动 Qt 前解析资源目录（`resourceDir` 在 ApplicationContext 上为空，因此按候选列表探测并要求
   `skins/` 子目录存在）并通过 `appArgs` 传入 `--resource-path`；`--settings-path` 指向应用沙箱 `filesDir/.mixxx`（数据库/配置落在此处）。
4. **诊断开关**：`QAbilityStage` 把资源候选路径与 Mixxx 自己的 `mixxx.log` 尾部打印到 hilog（P5 完成后移除）。
5. `build-hap.sh`：资源改入 resfile；`STAGE_ONLY=1` 只做暂存，签名交给 DevEco CLI。

### 验证（HUAWEI MLR-AL10，API 26 / HarmonyOS 7.0.0.109）

- CoreServices 正常初始化：数据库 schema 由 0 升级到 v33+（`SchemaManager` 日志）
- Mixxx 自有对话框渲染正常（"no output sound devices" 警告框——音频后端属 P4 范围；菜单栏设置询问框）
- 完整 UI 上屏（截图 `docs/ohos/logs/mixxx_shot10.jpeg`）：
  - 菜单栏 **File | Library | View | Options | Help**
  - 顶部栏：BIG LIBRARY / WAVEFORMS / 4 DECKS / MIXER / EFFECTS / SAMPLERS / MIC-AUX + 时钟 + Buffer% + REC + ON
  - 双 Deck（CUE、热键 1-8、Loop、变速 ±8%、Filter、BPM/SYNC、FX1-2）
  - Mixer（EQ H/M/L 三色环旋钮、MAIN/BAL、HEAD MIX/SPLIT、FX1-4、推子）、滤波与交叉推子
  - MIC 1-4 / AUX 1-4
  - 底部 Library 面板（Preview + Cover Art / Last Played / Album / Artist / Title）

### Remaining（后续阶段）

- 音频输出：PortAudio 无 OHOS hostapi → P4（`pa_ohos` + OHAudio）
- 音乐导入：系统 Picker + Library 扫描 → P5（当前目录选择器为系统文件选择器，可正常弹出）
- 翻译资源未随包（`res/translations` 75MB 暂略）
- `QAbilityStage` 诊断代码与 C++ 侧 `MIXXX_OS_OHOS` 资源分支保留（后者是正式实现，前者待清理）

## Task P1.9 — GLES 着色器 / 媒体路径 / 全屏沉浸（三处用户反馈）

> 本节由 AI Agent 自动生成，用于记录移植过程，后续以人工复核为准。
> This section was written autonomously by an AI Agent to record porting progress.

Status: **代码与打包 PASS；真机验证被锁屏阻塞**（HAP 已安装，`aa start` 返回 10106102）

### 1. 着色器 flood（`Link failed because of missing fragment shader` ×170707）

实测证据链（非推断）：

1. `src/rendergraph/CMakeLists.txt`（Qt ≥ 6）置 `USE_QSHADER_FOR_GL=ON`，OHOS 构建中 `materialshader.cpp`
   的编译命令确认已带 `-DUSE_QSHADER_FOR_GL`；
2. 同一目标生成的 `.qsb` **只含一种 GLSL 变体**——`qsb --dump` 结果为
   `SPIR-V 100 / GLSL 100 es / HLSL 50 / MSL 12`（没有 120/150）；
3. `materialshader.cpp` 却固定按 `QShaderKey(GlslShader, 120)` 取 shader → 取到空串 → 链接期报“缺 fragment shader”；
4. 编译期方案不可行：`QT_OPENGL_ES_2` 由 `target_compile_definitions(mixxx-lib PUBLIC ...)` 提供，
   **不会反向传播**到 `rendergraph_gl`（实测该 TU 的 `DEFINES` 中没有此宏）。

修复：改为**运行期**按当前 context 选变体——用 `QOpenGLContext::currentContext()->isOpenGLES()`
与 `QShader::availableShaders()` 各变体的 `QShaderVersion::GlslEs` 标志比对；无匹配时回落第一个 GLSL
变体并告警。`src/rendergraph/shaders/CMakeLists.txt` 未改（ES 变体本就存在，避免无谓的 qsb 重新生成）。

### 2. 媒体路径固定为 `Download/<包名>/`（不再弹文件夹选择器）

真机日志证据：

- 库指向 `/storage/Users/currentUser/Download/com.vintage.pomelopro/games/music/`（**别的应用**的目录），
  目录可枚举但文件不可读（`Cannot read audio properties from inaccessible/unreadable/invalid file`）；
- `[Recording] Directory /Mixxx/Recordings` + `Failed to create folder ... "Permission denied"`，
  根因是 Qt OHOS 的 `QStandardPaths::MusicLocation` **未实现**：`qstandardpaths_ohos.cpp` 只对接
  `OH_Environment_GetUserDownloadDir/DocumentDir/DesktopDir`（SDK 头文件中没有 Music getter），返回空串。

按 `F:\PomeloWin`（`AppCatalogService.ets`）的做法分三层修复：

| 层 | 改动 |
|---|---|
| ArkTS | `QAbilityStage.resolveMediaRoot()`：`bundleManager.getBundleInfoForSelfSync()` 取包名 → `file://docs/storage/Users/currentUser/Download/<包名>` 经 `fileUri.FileUri(...).path` 解析 → 逐级 `mkdirSync`（docs provider 的递归 mkdir 在父目录不存在时会失败）；失败回落 `filesDir/Music`。结果经 `appArgs` 以 `--media-path` 传入 |
| C++ 选项 | `src/util/cmdlineargs.{h,cpp}` 新增 `--media-path`；`getMediaPath()` 未提供时回落 `QStandardPaths::MusicLocation`，`getMediaPathProvided()` 区分“由启动器提供” |
| C++ 消费 | `browsefeature` 的 Music 快捷根；`dlgprefrecord` 录制目录默认值（并纠正指向包外/不可写的旧值）；`library.cpp` 启动时把该目录注册进库（`addDirectory`，仅在 `--media-path` 存在时生效，桌面行为不变） |

### 3. 全屏沉浸（隐藏系统状态栏）

先前 `setWindowSystemBarEnable([])` 在真机被拒（`{"code":1300002}`），状态栏（截图顶行 `11:11`+电量）始终可见。
按 `F:\PomeloWin` 的成熟写法改为
`setWindowLayoutFullScreen(true)` + `setWindowDecorVisible(false)` + **`setSpecificSystemBarEnabled('status'|'navigationIndicator', false)`**，
并保留启动后 2s / 5s 重试（窗口创建期调用会被拒）。

### 4. 本轮构建与安装

- `cmake --build /data/mixxx-build/ohos`（687 目标）→ `libmixxx.so` 链接 PASS；
- `STAGE_ONLY=1 build-hap.sh`（222 项 / 404M）→ hvigor `assembleHap` **BUILD SUCCESSFUL**；
  期间修掉 2 处 ArkTS 报错 `arkts-no-implicit-return-types`（箭头函数补 `: void`）；
- 卸载重装成功（同时刷新启动器名称/图标缓存）。资源已含 `res/translations/*.qm`，
  P1.8 遗留的“翻译未随包”在此解决。

### Blocker

`aa start` 返回 `10106102 The device screen is locked during the application launch, unlock screen failed`。
`power-shell wakeup` 可点亮屏幕，但钥匙盘需人工解锁，故以下三项**尚未真机验证**：
状态栏是否隐藏、`--media-path` 是否落在 `Download/com.pomelo.mixxx`、着色器 flood 是否归零。

> 本节结束（由 AI Agent 自动生成）。
> End of autonomously AI-generated section.

## Task P1.10 — 解锁后真机调试：音频链路打通 + 媒体目录落地 + 三处根因修复

> 本节由 AI Agent 自动生成，用于记录移植过程，后续以人工复核为准。
> This section was written autonomously by an AI Agent to record porting progress.

Status: **PASS** —— 真机上完整界面 + 中文 + 沉浸全屏 + OHAudio 音频流运行 + `Download/<包名>/` 生效。
（交接文档见 `docs/ohos/HANDOVER_CODEX.md`）

### 1. P1.9 三项验证（解锁后完成）

| 项 | 结果 | 证据 |
|---|---|---|
| 着色器 flood 归零 | ✅ | 新日志 16 KB（原 16 MB），`grep -c 'missing fragment shader'` = **0** |
| 状态栏隐藏（沉浸） | ✅ | `QAbility[startup/retry2s/retry5s] system bars hidden`；截图顶部已无 11:11/电量 |
| `Download/<包名>/` 生效 | ✅ | `MIXXX-DIAG Download provisioned: "/storage/Users/currentUser/Download/com.pomelo.mixxx" ready=true`；`mixxx.log` 出现 `Library - Registering launcher-provided music folder QFileInfo(/storage/Users/currentUser/Download/com.pomelo.mixxx)` |

### 2. 菜单栏消失（用户反馈）→ 根因是"无输出设备"模态框

证据链：`MixxxMainWindow::initialize()` 中 `noOutputDlg()` 是**模态**，位置在 `m_pMenuBar->show()` **之前**，
因此菜单栏（`createMenuBar()` 后处于隐藏态）永远不显示。
触发条件：`SoundManagerConfig::loadDefaults()` 只有 Linux/Windows/iOS/macOS 分支，**OHOS 没有默认 host API** → `m_api` 为空 → 选不到设备 → `getOutputs()` 为空。

修复：`src/soundio/soundmanagerconfig.cpp` 增加 `MIXXX_OS_OHOS` 分支，**选择第一个真正有输出设备的 host API**（不硬编码名字）。
验证：`soundconfig.xml` 现在为
`api="OHOS OHAudio"` + `<SoundDevice deviceIndex="0" name="Speaker"><output channel="0" channel_count="2" index="0" type="Master"/></SoundDevice>`。

另：`[Config] hide_menubar` 的自动隐藏依赖 **Alt 键**（平板无法恢复），`show_menubar_hint` 模态会在新装时干扰 →
种子配置写入 `hide_menubar=false` / `show_menubar_hint=false`（`QAbilityStage.ensureConfigEntries`，逐键插入 `[Config]` 段，不覆盖既有值）。
遗留：该提示框在 seed 之后**仍然弹出**（未根治，机制待查），详见交接文档 §7.2。

### 3. `pa_ohos.c` 两处致命缺陷（崩溃与"Invalid stream pointer"）

1. **结构体布局错误**：`PaOhosStream` 的首成员原为 `hostApi`，但 PortAudio 会把 `PaStream*` 强转回
   `PaUtilStreamRepresentation*` 并校验 `magic`（`PaUtil_ValidateStreamPointer`）→ 返回 `paBadStreamPtr`，
   界面显示 `无法打开 "Speaker" / Invalid stream pointer`。修法：把 `streamRepresentation` 放到首成员（与 ALSA 等 host API 一致）。
2. **`PaUtil_SetNoInput()` 误用导致 SIGSEGV**：其实现为 `bp->hostInputChannels[0][0].data = 0;`，而
   `hostInputChannels` **只在有输入通道时才分配**。纯输出流（本例 `numInputChannels=0`）调用它即 NULL 解引用。
   崩溃证据：`Fault thread: OS_AudioWriteCB`，栈 `PaUtil_SetNoInput+4` ← `ohosRendererOnWriteData` ← `libohaudio.so`，
   `Reason:Signal:SIGSEGV(SEGV_MAPERR)@000000000000000000`。
   修法：`if (stream->bufferProcessor.inputChannelCount > 0)` 守卫。

两处修完 `vcpkg.json` port-version 提到 **23** 并强制重编（`vcpkg remove --recurse` + `install`；注意只改 overlay 源码而不 bump 版本 vcpkg 会直接跳过）。

### 4. `Download/<包名>/` 授权（用户反馈"包名目录没有创建出来"）

真机实测：`fs.mkdirSync("/storage/Users/currentUser/Download/com.pomelo.mixxx")` **失败**——该目录必须先由系统 provisioning 一次。
参考 `F:\PomeloWin`（`AppCatalogService.provisionGamesDirectory`）实现：

- 用 `DocumentViewPicker` + `DocumentPickerMode.DOWNLOAD`（**不是选择器，是授权接口**）静默拿到该目录并写入持久授权；
- 三个硬约束（逐个踩出）：
  1. 构造必须用 `(context, window)` 双参 —— 只给 context 会 `[picker] ParseWindow: not window mode` → `13900042`；
  2. context 必须是 **`UIAbilityContext`**（AbilityStageContext 亦报 `13900042`）；
  3. picker 依赖窗口的 **ArkUI `uiContent`**（系统日志 `get uiContent failed`），而它只在 Qt 窗口加载完成后存在 →
     **picker 不可能在 Qt 启动前执行**。故改为 `onForeground` 之后 2s/6s/12s 重试（成功即停），
     首次启动用沙箱目录兜底，授权持久化后**第二次启动起**使用 `Download/<包名>/`。

### 5. 复验结果

- 安装启动后 **45 秒进程仍存活**（修复前 0.35 秒即 `CPP_CRASH`），`PortAudioOHOS: stream opened` → `stream started` 持续；
- 截图 `docs/ohos/logs/mixxx_v8.jpeg`：完整 LateNight 皮肤（BIG LIBRARY/WAVEFORMS/4 DECKS/MIXER/EFFECTS/SAMPLERS/MIC-AUX）、
  双 Deck、Mixer、中文资料库表头、顶栏状态栏已消失；
- 崩溃日志留存：`docs/ohos/logs/cppcrash.log`。

### Remaining

1. **放歌验证真正出声**（把音频文件放进 `Download/com.pomelo.mixxx/` 再从资料库加载播放）；
2. "是否隐藏菜单栏"提示框未根治（见 §2 遗留）；
3. 首次启动音乐目录仍走沙箱兜底（§4 约束 3 的必然结果）；
4. DPI `ScaleFactor=0.75`（可在 Preferences → Interface 调整）；
5. 资料库导入/扫描实测（P5）；移除 ArkTS 侧 `MIXXX-DIAG`/`MIXXX-LOG` 诊断输出。

### 6. 第二轮着色器问题（音频修好后暴露，已修）

音频打通、`initialize()` 跑完（波形/转盘控件终于创建）后，日志 2 分钟内涨到 1.7 MB / 17,431 条
`Link failed because of missing fragment shader`。定位为 `src/shaders/textureshader.cpp` 的内联 fragment 着色器
（被 `WSpinnyGLSL` 黑胶转盘、`WVuMeterGLSL` VU 表使用）：

- `#version 120`（桌面 GLSL）→ GLES 编译器报 `P0007: Language version '120' unknown`；
- 去掉版本指令后暴露第二层：`uniform float alpha;` 无精度限定符 → `S0032: no default precision defined for variable 'alpha'`
  （GLSL ES 的 fragment shader 中 float 无默认精度）。

两处都改掉（并加注释防止改回）后复验：

| 指标 | 修复前 | 修复后 |
|---|---|---|
| `missing fragment shader` | 17,431 条 / 2 min | **0** |
| `QOpenGLShader::compile` 报错 | 有 | **0** |
| `mixxx.log` 体积 | 1.7 MB | **16 KB** |

可见效果：黑胶转盘与顶栏 Buffer%/VU 表由空白变为正常绘制（截图 `docs/ohos/logs/mixxx_final.jpeg`）。
排查方法：先 `grep 'compile('` 取编译器原文，再对照源码；`*** Problematic Fragment shader source code ***` 会紧随其后打印源码。

> 本节结束（由 AI Agent 自动生成）。
> End of autonomously AI-generated section.

## Task P1.11 — 2026-09-28 续调：Music 自动创建、配置迁移、加载播放修复

> 本节由 AI Agent 自动生成。This section was written autonomously by an AI Agent.

Status: **PASS（本轮加载与目录修复）** — 最终签名 HAP 已覆盖安装，Music 浏览/搜索及 MP3 加载播放通过；实际听音与干净安装另列未验证。

### Goal

按交接文档继续真机调试；应用自动创建用户可见的 Music 目录，并从这里浏览、扫描及搜索音乐，解决加载播放阻塞。

### Changed files

- `packaging/ohos/entry/src/main/ets/qabilitystage/QAbilityStage.ets`：自动创建 `Download/com.pomelo.mixxx/Music`，启动传 `--media-path` 与 `--rescan-library`；沿用窗口就绪后的 DOWNLOAD provisioning；修复种子配置语法并迁移旧值，创建 effects/chains，删除完整日志转存。
- `packaging/ohos/entry/src/main/ets/qability/QAbility.ets`：同步首次目录授权说明。
- `src/coreservices.cpp`、`src/library/library.{h,cpp}`：启动器已提供目标路径时不弹音乐目录选择框，最多等待 30 秒授权/目录可用，延后注册后扫描。
- `src/library/browse/browsefeature.cpp`：旧配置也追加 Music 绝对路径的快捷入口。
- `cmake/ohos/ports/portaudio/ohos/pa_ohos.c`、`vcpkg.json`：后端 **#24** 在 renderer 创建后查询真实参数，填 `PaStreamInfo`；修复实际采样率 0 引发的 EQ/fidlib 退出。
- `src/sources/soundsourceffmpeg.cpp`：仅在负帧预读定位失败时，重试首个可用包；中段定位保持原逻辑。
- `packaging/ohos/build-hap.sh`：补资源根的 `en_US.kbd.cfg`，使默认键盘映射 fallback 可读取。
- `docs/ohos/HANDOVER_CODEX.md`：更新当前路径、根因、部署注意事项与剩余验证。

### Root causes

1. `ConfigObject::parse()` 使用 `键 空格 值`，bool 使用 0/1；旧 `ScaleFactor=0.75` / `hide_menubar=false` / `show_menubar_hint=false` 全部未生效。新配置采用 `ScaleFactor 0.75` / `hide_menubar 0` / `show_menubar_hint 0`，保留已有有效用户设置，读取失败不重写。
2. 加载 WAV 后的 `OS_AudioWriteCB` 崩溃符号化为 `fid_design → EngineFilterBiquad1Peaking → BiquadFullKillEQ → EngineMixer`。后端未填 `streamInfo.sampleRate`，Mixxx 获取 0 并传入 DSP；#24 返回真实 48000 Hz。
3. MP3 能扫描封面和标签，但开始读取时反向定位到首个索引之前，FFmpeg 返回 -1，被 `av_strerror()` 显示为 `Operation not permitted`。文件未丢失；重试 flags=0 可取到首包。
4. 本真机文件管理器有旧名称缓存：Download 列表显示 **com.pomelo.mixxx**，进入后的导航路径仍显示 **label**，两者是同一目录。实际放歌入口：**文件管理 → 我的平板 → Download → com.pomelo.mixxx（导航可能显示 label）→ Music**。

### Build

命令：

```powershell
docker exec -e OHOS_SDK_ROOT=/apps/harmony/sdk/default/openharmony mixxx-ohos-build bash -c 'cd /data/mixxx-build/ohos && cmake --build . -j 24'
docker exec -e STAGE_ONLY=1 -e OHOS_SDK_ROOT=/apps/harmony/sdk/default/openharmony mixxx-ohos-build bash /data/src/mixxx/packaging/ohos/build-hap.sh
$env:DEVECO_SDK_HOME = 'C:/Program Files/Huawei/DevEco Studio/sdk'
$env:JAVA_HOME = 'C:/Program Files/Huawei/DevEco Studio/jbr'
# 工作目录 packaging/ohos
node 'C:/Program Files/Huawei/DevEco Studio/tools/hvigor/bin/hvigorw.js' --mode module -p module=entry@default -p product=default assembleHap --no-daemon
```

结果：PortAudio #24 重编、libmixxx 编译、最终暂存、HAP 打包签名及覆盖安装全部通过；第二次构建确认 `library.cpp` 与 `browsefeature.cpp` 编译，第三次确认 `soundsourceffmpeg.cpp` 编译。Ninja 运行中后来修改的文件可能不进入 dirty 集合，本轮因此补做后续构建，不能只凭链接成功宣称入包。

最终 native 与 staged `libmixxx.so` SHA256 同为 `ac2c42b4ba6090ccb92ab4e56751b715bbb15af10e8f188733b5f3db2db947f1`；HAP 内经 hvigor strip 后的库与构建中间库同为 `4ce8800e0451a49e2e6db63210aad480e1bd6843792e1ecb981d3b22fd435215`。本阶段 signed HAP SHA256：`e9ccb0a633c8434571b946eae8d26ca20c7960cb75812994694afdf78ef8f380`，后续标题栏修复包会替换此产物。

### Device validation

设备：已连接的 MatePad Mini，原有数据保留，使用覆盖安装。

- 菜单提示消失，菜单可见，0.75 缩放生效，布局完整。
- 安装启动前 Music 不存在，启动后应用自行创建；HDC 物理路径 `/mnt/hmdfs/100/account/device_view/local/files/Docs/Download/com.pomelo.mixxx/Music`。
- 原目录五首用户 MP3 成功扫描标签与封面；未搬动用户音乐。Music 中已放入真实文件 `sine-30.wav`；误传形成的两个 `.wav` 同名目录已通过文件管理器移至最近删除。
- #24 日志 `stream opened: 48000 Hz, 2 ch, 4458 frames/host buffer`；WAV 可加载分析，播放进度与 VU 可见，进程未崩溃。实际回调缓存较大，不能宣称低延迟达标。
- 最终包启动扫描发现 Music 中新增 WAV 1 首，曲库共 8 首；最后一个 Music 快捷链接指向正确 Download Music，Browse 显示 `sine-30.wav`。无匹配过滤时列表为空，匹配 `sine` 时一行恢复。
- 用户报错歌曲《动力火车 - 当…mp3》在最终包中成功加载、分析并播放超过 2 分钟，波形、BPM、进度及 VU 正常；随后暂停并点击 overview 跳转成功。扬声器听音尚未验证，用户目前不在家。

### Evidence

本地证据位于忽略目录 `docs/ohos/logs/20260928-continuation/`，原始日志/截图/测试歌曲不公开发布。

- `config-migration-result.log`：调用实际 ArkTS 方法的临时脚本，覆盖新配置、旧种子、用户值、分组隔离、重复项、读取失败、幂等，全部 PASS。
- `config-fixed.jpeg`、`filemanager-label.jpeg`、`music-created.jpeg`、`wav-playing.jpeg`。
- `crash-analysis.log`：采样率 0 引发的 fidlib 退出栈。
- `seek-probe-result.log`：同源 Windows FFmpeg 读取真机失败 MP3，负帧 backward=-1、forward=0；恢复后的首包与顺序读取 PTS/字节相同，中段定位及返回开头成功。
- `seek-probe-wav-result.log`：WAV 对照同样通过。
- `vcpkg-portaudio24.log`、`mixxx-portaudio24.log`、`mixxx-music-final.log`、`mixxx-seek-final.log`、`stage-verified.log`、`hap-verified.log`、`install-verified.log`。
- `dang-playing-final.jpeg`、`music-links-expanded.jpeg`、`music-browsed-final.jpeg`、`music-search-seek.jpeg`、`music-search-empty.jpeg`、`music-search-match.jpeg`、`mixxx-playback-final.log`。
- 裸 ELF PortAudio probe 因生产真机执行权限被拒，**不计为运行 PASS**。

### Remaining blocker

- 用户听音、真实 DJ 设置下的低延迟/稳定性测试仍未确认；电平与进度不等于实际扬声器输出验证。
- 无持久授权的全新安装尚待独立环境复验；本轮不清除用户数据。
- 11:32 通过文件菜单退出后，日志末尾出现 Qt QPA 的 `can't create window without the default Ability instance` fatal；播放期间未发生该错误。退出生命周期需单独追踪，不能据此宣称整条退出路径正常。

### Next step

按用户最新要求修复系统标题栏/顶部空间浪费，见后续 P1.12。所有 commit、push、PR/Issue 与真实 DJ 人工测试由用户执行；本轮保留既有工作区/IDE 改动。

> 本节结束（由 AI Agent 自动生成）。End of autonomously AI-generated section.



## Task P1.12 — 2026-09-28 顶部留白与全屏窗口生命周期

> 本节由 AI Agent 自动生成。This section was written autonomously by an AI Agent.

Status: **PASS（顶部空间与窗口恢复）** — 最终包已签名覆盖安装，顶部留白由 105 像素降为 0；菜单、返回前台及键盘收起复验通过。

### Goal

按用户要求隐藏系统标题栏并消除顶部浪费的空间；用户不在家，暂不进行人工听音确认。

### Changed files

- `src/mixxxmainwindow.cpp`：OHOS 主窗口设为 frameless；Qt 6.9+ 主窗口禁用自动安全区域内容边距。
- `packaging/ohos/entry/src/main/ets/qabilitystage/QAbilityStage.ets`：启动参数追加 `--full-screen`，使 Qt 窗口状态与 ArkTS 沉浸设置一致。
- `packaging/ohos/entry/src/main/ets/qability/QAbility.ets`：创建、恢复、返回前台及固定态键盘高度归零时重施全屏/隐藏栏；后台和销毁时取消延迟任务，销毁时注销键盘监听；捕获异步布局 API 拒绝；5 秒后记录系统避让区和屏幕 cutout 尺寸。

### Diagnosis

- 真机窗口 `mixxx0` 的 `WindowRect` 为 `[0,0,2560,1600]`，ArkUI 主 XComponent 同样覆盖全屏。
- `ContainerModalTitleRow` 的 `FrameRect` 是 `0×0`，标题栏已无高度；截图仍有约 104 像素的整行空白。
- pinned Qt 6.13 的 QWidget 默认启用 `WA_ContentsMarginsRespectsSafeArea`；OHOS QPA 将系统避让区和 cutout 转换为内容边距，即使原生窗口和渲染面覆盖全屏，Qt 内容仍可能内缩。
- 手动切换 Mixxx 全屏未消除留白，因此同时调整 QWidget 内容边距及原生窗口生命周期。
- 仅更新 ArkTS 和 `--full-screen` 的中间包仍留白。系统避让区四边矩形均为 0；屏幕 cutout 为 `(30,25,80,80)`。QPA 按最近边缘折算得到顶部边距 **25+80=105**，与截图完全一致。

### Build

命令沿用 P1.11 的 native build → STAGE_ONLY → Windows hvigor → HDC 覆盖安装流程。原生构建 exit 0，确认 `mixxxmainwindow.cpp.o` 编译并重链；暂存 exit 0，hvigor `BUILD SUCCESSFUL`，HDC 覆盖安装成功。

native 与 staged `libmixxx.so` SHA256 同为 `6dab5173508cc58eebe8d3a163b072a543841fe5d9fb961fe48d79172b9cd3fe`；HAP 内经 strip 的库 SHA256 为 `ee18e5291cd8e6102da65c89d8f3392918be371faa2552c4845593dc2c81014d`。本阶段 signed HAP SHA256 为 `ed4cdb9d5a1e00245139820711809cd1cffc36326d59e4d1df5e9c92204cac3b`，后续 P1.13 包已替换此产物。

### Device validation

- 最终包 11:43:36 启动，PID 58027。主界面覆盖屏幕顶部，旧留白由 105 像素变为 0；文件菜单可打开。
- Home 返回桌面，再 `aa start` 回到原进程，顶部仍无留白，系统栏隐藏。
- 点击搜索框唤起键盘，输入 `sine` 后仅显示 `sine-30`；收起键盘后 2s/5s 重试生效，底部临时输入法工具栏消失，顶部仍无留白。
- 测试结束已清空过滤条件并收起输入法，最终画面恢复 8 首音轨列表（`clean-final.jpeg`），保持应用运行供用户复核。
- 音频流仍成功打开/启动：48000 Hz、2 声道、4458 帧/host buffer。此项不作为实际听音确认。

### Evidence

忽略目录 `docs/ohos/logs/20260928-continuation/`：`top-before.jpeg`、`window-before.log`、`top-layout-before.json`、`window-element-before.log`、`qt-fullscreen.jpeg`、`mixxx-fullscreen-build.log`、`titlebar-audit.json`。

最终证据：`stage-fullscreen.log`、`hap-fullscreen.log`、`install-fullscreen.log`、`fullscreen-artifact-hashes.json`、`window-fullscreen-final.log`、`top-after.jpeg`、`menu-after.jpeg`、`foreground-after.jpeg`、`keyboard-after.jpeg`、`keyboard-settled.jpeg`、`keyboard-bottom.jpeg`、`clean-final.jpeg`、`top-space-measurement.json`、`audio-fullscreen.log`。

### Remaining blocker

- 扬声器听音由用户回家后确认；真实 DJ 延迟与干净安装验证仍保留 P1.11 的限制。
- 全屏绘制现在包含摄像头周边区域；系统截图不包含实体挖孔，HDC 注入触控也不能验证物理遮挡，附近按键需用户现场复核。
- Qt QPA 退出时重建窗口的 fatal 需独立追踪。

### Next step

用户回家后复核听音与摄像头附近按键；退出生命周期问题单独追踪。保留原有应用数据和既有工作区改动；本轮不 commit/push。

> 本节结束（由 AI Agent 自动生成）。End of autonomously AI-generated section.

## Task P1.13 — 2026-09-28 暂停时滚动波形闪烁与 CUE 位置切换

> 本节由 AI Agent 自动生成。This section was written autonomously by an AI Agent.

Status: **PASS（代码回归与真机采样）** — 已构建、签名并覆盖安装；双 Deck 暂停连续 40 张截图波形区域完全一致，未再采到旧画面/CUE 位置切换；播放、定位和前台恢复复验通过。实际屏幕高频观感另待用户复核。

### Goal

修复用户指出的顶部双层滚动波形持续闪烁。用户补充：暂停时也会闪，CUE 标记看起来在两个状态之间切换。用户目前无法听音，优先验证画面、加载与定位。

### Diagnosis

- `VisualPlayPosition::getPlaySlipAtNextVSync()` 只检查最近 3 条位置快照；OHAudio 本机 host buffer 为 4458 帧，Mixxx 的 engine buffer 为 1024 帧，一次 host callback 可生成超过 3 条快照。
- 新一批快照都可能尚未到达 DAC，实际可用位置在更早的记录中。原查询此时返回 false，`WaveformWidgetRenderer` 将位置设为 -1，AllShader 隐藏信号/CUE 子树，只绘制背景。后续刷新又获得有效位置，形成有效画面与背景之间的切换。
- 用 host Qt 编译并调用实际 `visualplayposition.cpp`，模拟批量回调后 10ms/80ms 的交替刷新：暂停位置固定 0.25，原逻辑出现 **20/40 次无效刷新**；扩展查询后为 **0/40**，音轨失效后的查询仍返回 false。
- GL 路径另有两套提交入口：Mixxx 定时直接交换缓冲，`QOpenGLWindow` 自己的重绘也会自动交换。OHOS 改为 Qt 重绘内绘制并提交，避免 Qt 事件消耗预绘制缓冲后 Mixxx 再提交空缓冲。未声称已用逐帧 GPU 跟踪证明这是用户现象的唯一根因。
- 后续双 Deck 暂停采样抓到异常：`wave-dual-paused-before-13.jpeg` 的主界面时间仍为 2:11.29，顶部却短暂返回歌曲开头的预读画面，CUE/Intro 标记也不同；其他 15 张保持当前画面。这证明同一次暂停期间确实存在两种画面，支持旧 GL 缓冲被重提的判断。

### Changed files

- `src/waveform/visualplayposition.cpp`：OHOS 检查最多 8 条历史位置，使用现有 16 条环形缓冲；保留 DAC 时间选择、插值、Slip/Loop 和失效语义。其他平台继续检查 3 条。
- `src/widget/wglwidgetqopengl.cpp`：OHOS 定时交换请求改为 `QOpenGLWindow::update()`。
- `src/waveform/widgets/allshader/waveformwidget.cpp`：OHOS 移除定时器中重复的 GL 绘制，由 Qt `paintGL()` 绘制真实音频波形。
- `src/widget/openglwindow.cpp`：OHOS resize 不再额外绘制/交换；Qt 重绘沿用 `shouldRender()` 的可见性判断，保持 exposed 状态异常时的绘制回退。

### Build and validation

沿用 P1.11 的 native build → STAGE_ONLY → Windows hvigor → HDC 覆盖安装流程，两轮原生构建均 exit 0。第一轮确认 GL 提交相关的 3 个 `.cpp.o` 编译；位置同步修改发生在该轮开始之后，补做第二轮，确认 `visualplayposition.cpp.o` 编译并重链。每轮 `cmake_autogen` 扫描约 11 分钟；不能只凭首轮链接成功声称所有改动均已入包。

暂存 exit 0；hvigor `BUILD SUCCESSFUL in 23 s 845 ms`；HDC `install bundle successfully`。最终 native 与 staged `libmixxx.so` SHA256 同为 `45a9d94861a7efea0e4db2dc3416370b71a63ba880c073ae3fa7023b1617633a`；HAP 内库与 hvigor stripped 库同为 `cbb0c134e36d2425a17c4fb7b77c9f512dcb34b6e4ddf4d1e1ce7ba10b71bea3`。signed HAP SHA256 为 `32018f385df1de04c19a1b03bf228228850999dd7f38ca612de1c21900226a92`。

### Device validation

- 新包 12:33:59 打开音频流，PID 17388，至测试收尾仍存活。空 Deck 连续 10 张截图背景一致，不再出现前包空窗口的黑/灰状态差异。
- 《当》与 `sine-30.wav` 均能加载/分析。MP3 暂停定位到约 2:11.70 并设 CUE，双 Deck 连续 40 张截图的波形区域逐像素完全相同；前包相近位置的 16 张截图中，第 13 张曾短暂显示开头旧画面。
- 两个 Deck 播放后连续 16 张截图均有不同的有效波形画面，进度/VU 可见，证明修改后波形持续更新；随后全部暂停。
- `Shift+F` 返回 CUE 并停止；隐藏/重显 WAVEFORMS，再 Home 返回桌面并恢复原进程，后续 16 张截图波形区域保持一致。顶部仍无整行留白。
- 测试收尾将 MP3 临时 CUE 恢复到 0:00.00，测试 WAV 回到开头附近；两个 Deck/Preview 均停止，输入法未打开，列表仍为 8 首，保持应用运行供用户复核。
- 音频流仍为 48000 Hz、2 声道、4458 帧/host buffer。`no transport` 时间同步警告仍存在，未把本次视觉采样当作 DAC 时间戳精度、低延迟或实际扬声器输出验证。

### Evidence

本地忽略目录 `docs/ohos/logs/20260928-continuation/`：

- `wave-audit.json`、`capture_wave.py`：有界采集与本地证据契约。
- `wave-before-*`、`wave-playing-before-*`、`wave-paused-mid-before-*`、`wave-cue-before-*`、`wave-dual-before.jpeg`：修改前的初始暂停、播放、CUE 与双 Deck 画面；这些早期采样未抓到异常。
- `wave-dual-paused-before-*`、`wave-pause-alternating-before.png`、`wave-wrong-frame-time.png`：后续暂停采样抓到第 13 帧重现开头旧画面，界面时间仍是 2:11.29。低速采样不能统计实际高频闪烁率。
- `wave-loaded.log`：播放期间可见 `no transport` 时间同步警告。
- `position_probe.cpp`、`run_position_probe.sh`、`position-before.log`、`position-after.log`：编译并测试实际位置类，不复制被测算法；测试使用模拟回调时间，未在真机执行此 host 程序。
- `mixxx-wave-build.log`、`mixxx-wave-position-build.log`、`stage-wave.log`、`hap-wave.log`、`install-wave.log`、`wave-artifact-hashes.json`：构建、签名、覆盖安装与入包哈希。
- `wave-empty-after-*`、`wave-dual-paused-after-*`、`wave-playing-after-*`、`wave-foreground-after-*`、`wave-foreground-check.jpeg`、`wave-final.jpeg`、`wave-verification.json`：新包空 Deck、暂停 CUE、播放、前台恢复与测试收尾采样。
- `wave-hilog-after.log`、`wave-playback-after.log`：当前进程音频流与播放/位置日志。

### Remaining verification

实际屏幕的高频观感仍需用户复核，低速 HDC 截图不能覆盖所有显示帧。用户在 2026-09-28 已反馈声音正常、可以播放。音频时间戳精度与 `no transport` 警告、4458 帧 host buffer 的延迟、干净安装及 P1.12 的物理挖孔/退出生命周期限制继续保留。本轮不 commit/push，保留用户音乐和既有工作区改动。

> 本节结束（由 AI Agent 自动生成）。End of autonomously AI-generated section.

## Task P1.14 — 2026-09-28 原版界面缩放、触摸与设置弹窗适配、系统媒体播放

> 本节由 AI Agent 自动生成。This section was written autonomously by an AI Agent.

Status: **PASS（本机已验证范围）** — 保留原版 LateNight 界面；设置窗口、键盘避让、模态弹窗层级和触摸滑动已修复并覆盖安装。14 个设置分类已逐页巡检，常用弹窗及分屏已实测。手机实体、正反横屏的物理旋转和全部罕见弹窗仍需后续验证。

### Goal

按用户最终明确的要求，只压缩原版界面比例，不设计另一套手机界面；普通分屏可用，极小窗口提示放大。解决“选项/设置显示不全、按钮点不到”，统一检查同类窗口；改善触摸滑动。同步落实横屏启动、后台播放与系统媒体胶囊控制、应用名称“旧柚Mixxx”和关于中的个人信息。

### Changed files

- `src/platform/ohos/windowadapter.{h,cpp}`：原版皮肤统一缩放、极小窗口提示、通用 QDialog 滚动/边界适配、触摸滚动、键盘高度处理、模态窗口状态发布。
- `src/platform/ohos/mediabridge.{h,cpp}`、`mediacontroller.{h,cpp}`：GUI 线程媒体控制；线程安全状态桥，NAPI 提供 `readState/sendCommand/setKeyboardHeight/readWindowState`。
- `src/mixxxmainwindow.{h,cpp}`、`CMakeLists.txt`：OHOS 接入原版皮肤适配器、媒体控制器、`Qt6::GuiPrivate` 和 `libmixxxohosmedia.so`。中间实验的自创 compact/adaptive 页面已移除。
- `src/widget/wtracktableview.{h,cpp}`：OHOS 点选时保持整行可见；非编辑状态禁用输入法，避免触摸选曲/滚动时误弹键盘。
- `src/dialog/dlgabout.{h,cpp}`、`src/util/versionstore.cpp`：原作者/授权标签保留，新增可编辑并保存的个人信息页；OHOS 名称为旧柚Mixxx。用户尚未提供个人文案，字段保持空，未杜撰身份信息。
- `packaging/ohos/entry/src/main/ets/{qability/QAbility.ets,media/MixxxMediaSession.ets}`、NAPI 类型、`module.json5`：双方向自动横屏、原生窗口/子窗键盘监听、模态层级恢复、AVSession、连续音频后台任务与权限。
- AppScope 和 entry 各语言 `string.json`：应用显示名同步为旧柚Mixxx，bundle 保持 `com.pomelo.mixxx`。
- `src/waveform/renderers/allshader/waveformrenderbackground.{h,cpp}`：OHOS 按原皮肤颜色绘制背景矩形，补修收尾发现的空碟机背景黑/灰切换。

### Diagnosis and implementation

1. **原版皮肤与极小窗口**：基于原皮肤最小尺寸、原生窗口尺寸和基础 DPR 调整比例，180ms 合并尺寸变化；不改变皮肤控件结构。所需比例小于 50% 时提示放大，放大后恢复原界面。主界面的 GL 子窗不能外套 QScrollArea；实验曾触发 native ancestor/VSync fatal，因此主界面采用整体缩放，滚动容器只用于普通弹窗。
2. **触摸滑动**：父 WWidget 会抢走 viewport 的 Touch 事件；viewport 设置 `WA_NoMousePropagation`。Qt 合成的鼠标移动还会触发歌曲 QDrag，阻断 QScroller；只拦截触屏合成的左键 MouseMove，保留真实鼠标拖拽。QScroller 使用 TouchGesture、逐像素滚动和无 overshoot；拖动释放取消误点击，保留轻触选中。
3. **弹窗边界**：所有带 root layout 的顶层 QDialog 按主窗与屏幕可用区交集、8 逻辑像素边距约束几何；外套滚动容器保留原布局，已有单个 root QScrollArea 的关于窗口直接复用。解除根最小尺寸约束，使超长内容/原按钮可通过滑动到达。保持原 palette 的不透明背景，避免主界面 GL 透出。
4. **模态窗口关闭**：显示后调用 `setWindowFlag(FramelessWindowHint)` 会 hide/show；Qt `QDialogPrivate::setVisible(false)` 会退出 `exec()`，导致颜色编辑器刚打开就销毁。改为在显示前的 Polish 处理 flags，并预处理早于适配器创建的 Preferences；fit 时不再改 flags。
5. **键盘避让**：pinned Qt OHOS 的 `QInputMethod::keyboardRectangle()` 为空，不能依靠 Qt 判断键盘区域。QAbility 监听主窗及子窗，前台每 500ms 扫描子窗，消失/后台/销毁时注销；native 回调排队到 GUI 线程。子窗返回重叠高度，需补 `max(0, mainBottom - subBottom)`；本机 724 → 798 像素。仅缩小 root scroll viewport，保持原生 dialog geometry，避免高度归零触发窗口缩小/恢复振荡。
6. **按钮点不到**：键盘收起的高度 0 延迟 250ms 应用，正高度立即应用。按下按钮时输入法会先隐藏，延迟避免按钮在 press/release 间移动；全屏和分屏 Preferences 的“取消”均一次点击成功。
7. **弹窗被父窗遮挡**：Qt OHOS `raise()` 未实现，`activateWindow()` 不能恢复原生 Z-order。native 发布当前 modal 的平台窗口几何和 revision；ArkTS 根据子窗原生坐标匹配后调用 `raiseToAppTop()`。只在 revision 变化时操作，避开 active popup，异常单独捕获。真机点击颜色编辑器外侧后仍能看到并操作编辑器，Discard 可关闭。
8. **空碟机背景**：最终回归 8 张截图中 2 张空碟机背景变黑，有歌曲的波形/CUE 保持一致。原背景只调用 glClear，现 OHOS 同时绘制使用原背景色的 RGB 矩形，且位于信号子树之外。修后空双 Deck 连续 16 张、加载歌曲并 Home 返回后的 16 张上下区域分别逐像素一致；播放 4 张均正常变化。未用 GPU 跟踪证明驱动根因，不把低速截图当作所有物理显示帧的保证。
9. **媒体与方向**：`auto_rotation_landscape` 加 `AUTO_ROTATION_LANDSCAPE`（preferredOrientation=7），允许正反横屏。AVSession 发布当前歌曲、进度与播放状态，接收暂停/继续/上一首/下一首/seek；暂停记录原播放 Deck，恢复这些 Deck。上一首/下一首沿曲库 model 顺序，支持首尾循环；DJ 引擎混音保持原逻辑。播放时持有 AUDIO_PLAYBACK 连续后台任务，暂停后释放。

### Build

本轮已完成原生构建与暂存。窗口/桥接收尾使用当前 Ninja 生成的实际编译、归档、链接命令重编目标；未手写另一套编译参数。类新增方法与动态 property 不增加实例字段；背景 renderer 和创建它的 waveform widget 均已重编。

最终背景修复的构建/打包命令（node 在 `packaging/ohos` 下执行，沿用 HANDOVER §3 的 DEVECO_SDK_HOME/JAVA_HOME）：

```powershell
docker exec mixxx-ohos-build bash /data/src/mixxx/docs/ohos/logs/20260928-continuation/relink_wave_background.sh
docker exec mixxx-ohos-build cp /data/mixxx-build/ohos/libmixxx.so /data/src/mixxx/packaging/ohos/entry/libs/arm64-v8a/libmixxx.so
node 'C:/Program Files/Huawei/DevEco Studio/tools/hvigor/bin/hvigorw.js' --mode module -p module=entry@default -p product=default assembleHap --no-daemon
```

- 原生重编/重链 exit 0，`hap-wave-background.log`：`BUILD SUCCESSFUL in 16 s 354 ms`；HDC `install -r`：`install bundle successfully`。只覆盖安装，未卸载或清数据。
- 最终包：`packaging/ohos/entry/build/default/outputs/default/entry-default-signed.hap`，SHA256 **`b618542a77e68ffc885281134c91e4aa17ab606cd5334d396a2b98141dfc3ff8`**。
- 最终 native/staged `libmixxx.so`：`c3e297781767eed857675d4d0d5b123812fe5619236842222557a857c8131845`；stripped/packed：`6e982b989086037b77f0e1c4e274cb9e4c663c6a4a32c071d8026c4d035c8fe5`。
- `libmixxxohosmedia.so` native/staged：`ff1f91f9256def0587538e4a3143bc92b55b3e621a7a802c39223107f785ef89`；stripped/packed：`deb7d5f03f70964da15d7caf930e3cfe6940df4a85d881922eb4974f960fb6d6`。
- `hash_adaptive_package.py` 检查源码/目标时间、两库以及 **3246** 个资源；最终输出 `original-skin-artifact-hashes.json`。早期 P1.11/P1.12/P1.13 和本轮中间包哈希均为历史产物。

### Host regression

`run_original_skin_probe.sh` 编译实际 `windowadapter.cpp`，通过 Qt Test 注入真实 touch/mouse/keyboard 事件，未复制适配算法。`dialog-native-raise-host.log` exit 0：

- 原皮肤对象保持；1600×900、900×550 按比例显示，500×300 提示，放大恢复。
- 200 行列表触摸滚动约 842 像素；swipe clicks=0、touch drag attempts=0、tap clicks=1；真实鼠标 drag attempts=1。
- 超大弹窗仍包含原保存按钮，可滚到并点击；键盘上方 viewport 正确避让。
- press 后高度归零，100ms 内按钮位置稳定，release 成功触发 clicked，随后 viewport 恢复。
- 实际 `QDialog::exec()` 保持可见，直到测试显式 reject；适配不再导致即时退出。

### Device validation

实机为 MatePad Mini（2560×1600），最终进程 PID **28040**，不是手机测试。

| 检查范围 | 结果与限制 |
|---|---|
| 14 个设置分类 | 声音硬件、媒体库、控制器、界面、波形、颜色、碟机、混音器、效果、AutoDJ、录制、节拍、音调、归一化逐页截图巡检；原底部操作可见，长页可滑动 |
| 分屏 1271×1600 | 原皮肤 ratio=0.53449 完整；设置颜色页、声音页及长页滑动通过。输入法打开时取消按钮可见且一次点击关闭 |
| 极小悬浮窗约 815×1448 | 提示放大可读；最大化后原界面恢复，未崩溃 |
| 全屏 Preferences + IME | 原确认/取消/应用/帮助可见；一次 Cancel 关闭，无误弹曲库搜索键盘 |
| About | 个人信息空字段及保存入口完整；原作者/授权页可切换，作者长页触摸滑动有效。个人信息测试值已清空 |
| 音轨属性 | 内容、原取消/应用/确认按钮完整；标题字段可编辑，输入法避让时可滚动或收起键盘使用底部按钮。测试 Cancel，未保存测试元数据 |
| 播放列表输入框 | QInputDialog 正常 exec；键盘上方 OK/Cancel 可操作，Cancel 一次关闭，未创建测试列表 |
| ColorPaletteEditor | 正常打开；键盘及原按钮可用；点击窗口外侧后仍可见，Discard 正常关闭。QColorDialog 未实际打开，不计为实测 |
| 原版主面板 | 四碟机、采样器、麦克风/AUX 展开及恢复检查通过，原布局保留 |
| 曲库触摸 | 真机横向滑动列、反向恢复和轻触选曲/加载通过。当前只有 8 首，纵向滚动由 host 200 行回归验证，未声称 8 首真机列表有纵向溢出 |
| 后台媒体 | 此轮先前约 3 分 40 秒后台进度持续增加；原生音频与进程存活，胶囊显示歌曲。窗口桥最终版本再次 Home、系统暂停/继续/下一首/上一首通过，后一原生改动仅背景绘制 |
| 最终背景包 | 空双 Deck 16 张、单 Deck 加载并返回前台后暂停 16 张均稳定；播放 4 张波形正常更新，回 CUE 停止通过 |

静态检查全部 11 个 `.ui` 根 QDialog 均含 root layout；另外核对 C++ 构建的颜色编辑、曲库扫描和曲库导出窗口也有 root layout，统一适配可覆盖这些结构。未声称所有控制器学习、导出、联网标签等罕见弹窗均已逐一真机操作。

收尾：双 Deck/Preview 停止，《当》回开头/CUE（界面约 0:00.01），crossfader 中央，恢复双 Deck 默认面板，无弹窗/输入法，曲库保留 8 首。个人资料为空，用户音乐和标签未改动；保留既有工作区及 IDE 文件变化，未 commit/push/PR/Issue。

### Evidence

均在本地忽略目录 `docs/ohos/logs/20260928-continuation/`，不上传截图、日志或签名材料：

- `original_skin_probe.cpp`、`run_original_skin_probe.sh`、`dialog-native-raise-host.log`、`dialog-layout-audit.json`。
- `verified-prefs-00..13.jpeg`、`verified-prefs-contact.jpg`；`final-about-*`、`final-track-properties*`、`final-playlist-input-*`、`final-native-modal-outside.jpeg`、`final-palette-discarded.jpeg`、`final-help-menu.jpeg`。
- `final-split-sound-{top,swiped}.jpeg`、`final-split-settings-cancelled.jpeg`、`final-split-scale.log`、`final-tiny-readable.jpeg`、`final-tiny-restored.jpeg`、`final-full-prefs-ime-buttons.jpeg`、`final-full-ime-cancelled.jpeg`。
- `final-library-horizontal*.jpeg`、`final-library-tap-loaded.jpeg`；`final-original-four-decks.jpeg`、`final-original-samplers.jpeg`、`final-original-mic-aux.jpeg`。
- `original-background-{0,55,110,165}*`、`original-system-*`、`final-regression-system-*`、`final-regression-capsule.jpeg` 及对应 command/ledger。
- `final-original-paused-*` 是背景修补前的反例，不可作为修复成功截图；`wave-background-empty-*`、`wave-background-loaded-paused-*`、两份 `*-comparison.json` 是修后逐像素对比；`wave-background-playing-*`、`original-final-clean*` 是最终播放/收尾。
- `dialog-native-raise-relink.log`、`hap-dialog-native-raise.log`、`install-dialog-native-raise.log` 是窗口桥中间完成包；最终为 `wave-background-relink.log`、`hap-wave-background.log`、`wave-background-package-check.log`、`install-wave-background.log`、`launch-wave-background.log`、`original-skin-artifact-hashes.json`。

### Remaining blocker

- 手机实体的小屏触控、正反横屏物理旋转、摄像头挖孔附近按键需现场验证；本机 HDC 截图/注入不能替代物理测试。个人文案待用户提供或在关于页自行填写。
- 真实 DJ 配置、低延迟/xrun、`no transport` 时间同步警告、4458 帧 host buffer 及干净无授权首装待继续验证；不清用户数据做首装测试。
- 文件菜单正常退出时的 Qt default Ability fatal 仍需独立追踪；本轮没有以菜单退出复测该问题。

### Next step

后续按用户真实设备反馈推进手机实体布局、物理旋转与实际 DJ 测试，保持原版界面及 OHOS 平台边界。

> 本节结束（由 AI Agent 自动生成）。End of autonomously AI-generated section.

## Task P1.15 — 2026-09-28 设置保存、自由浮窗输入与公共运行日志

> 本节由 AI Agent 自动生成。This section was written autonomously by an AI Agent.

Status: **DONE（本机覆盖安装及操作复验通过；其它实体设备与首次无授权导出仍待验证）**

### Goal

保留原版 Mixxx 界面，使主题/布局设置在系统结束进程后保留，修复自由浮窗的触摸操作，并自动保存可从文件管理器导出的运行日志。

### Changed files

- `src/preferences/dialog/dlgpreferences.cpp`：OHOS 应用/确认后立即保存配置。
- `src/control/control.{h,cpp}`、`src/coreservices.cpp`：快照持久 Control，GUI 线程每 2 秒比较配置变化并保存，进入非 Active 状态立即保存/flush；修正 getAllInstances 清理失效指针的迭代器。
- `src/util/logging.{h,cpp}`：保留私有日志，复制完整启动日志后同步追加公共 mixxx.log；GUI 定时重试导出/flush。
- `packaging/ohos/entry/src/main/ets/common/RuntimeLog.ets`：hilog 包装器，私有/公共 ability.log、启动轮转及大小上限；QAbilityStage、QAbility、MixxxMediaSession 接入。
- `packaging/ohos/entry/src/main/ets/qability/QAbility.ets`：监听窗口状态，浮窗内容避开系统标题栏，其它状态维持沉浸布局；替换/销毁时注销监听。
- `src/mixxxmainwindow.cpp`、`src/platform/ohos/windowadapter.cpp`：主题重载保持原窗口模式，恢复 modal 焦点、原 palette 不透明背景；关闭设置恢复主窗焦点/收起输入法，异步激活时再次检查 popup。
- `cmake/ohos/patches/qt-ohos-touch-window-coordinates.patch`、`qt-ohos-popup-geometry.patch`：保存两项 QPA 修补；`build-qt-ohos.sh` 在 pin 检查后规范目标文件换行，使用 reverse dry-run 幂等应用。
- Qt 本地 `D:/Git/qt6/qtbase/src/plugins/platforms/ohos/qohosinputmethodeventhandler.cpp`、`qohosfloatingwindow.cpp` 已同步；临时全量事件、调用栈和关闭来源诊断已删除。

### Diagnosis and implementation

1. **设置未落盘**：修前 Classic 应用后界面改变，但磁盘没有 Scheme，直接结束进程会恢复 PaleMoon。持久 Control 原本在析构才写配置，而 OHOS 常直接终止进程。修后 Apply/OK 立即保存；GUI 周期读取当前持久值，比较完整配置键值，仅变化且 save 成功时更新快照。实时音频回调无新增 IO/锁。布局操作不足 2 秒立刻强杀仍有丢失窗口。
2. **浮窗坐标不一致**：Scene 对窗口再缩放，XComponent 的 displayPosition 与 Qt 布局不匹配。使用 native local → Qt logical local → Qt global → native global 映射，active touch positions 使用同一结果。
3. **浮窗顶部截获点击**：全屏内容延伸到系统浮窗操作条下。`windowStatusChange` 的 FLOATING 状态使用 `setWindowLayoutFullScreen(false)`，延迟重试也按当前状态处理。
4. **重复触摸**：XComponent 与 non-client 两路送出相同时间、相同目标窗口的事件，但 finger ID 不同；按窗口和 timestamp 去重。菜单选择、分类切换、曲库点选及播放操作恢复。
5. **菜单/下拉框刚显示就关闭**：调用栈定位为 WSI CloseEvent；`QOhosFloatingWindow::handleWindowRectChanged` 将 Preferences 子窗 reason=6/UNDEFINED 的矩形更新误当屏幕旋转。原比较 logical geometry 和 native rect，DPR 下总不同；改为 native size 比较，且旋转判断只作用 MainWindow。保留 DRAG_START 关闭 popup。
6. **换主题后的模态显示/焦点**：OHOS 跳过桌面的 maximize→fullscreen workaround，避免破坏浮窗和模态绘制；StyleChange/PaletteChange 后恢复不透明背景，alpha 已为 255 时不再 setPalette，防循环。关闭 dialog 时无其它可见 modal 才恢复主窗焦点，异步激活再次检查 active popup，避免抢下拉框焦点和误弹键盘。
7. **日志不可导出**：ArkTS 在 Qt 启动前写私有 ability.log，目录授权后复制并同步写公共文件。C++ 按 Music 父目录派生 logs，先复制私有 mixxx.log 的完整启动记录再追加。启动传 `--log-max-file-size 10000000 --log-flush-level info`；每次启动保留 `.1`…`.10` 历史。

### Build

Qt 使用现有 `mixxx-ohos-qt-build`/`mixxx-ohos-qt-out` volumes，按 Ninja 的实际对象/链接命令增量重编 QPA 并同步 `libqohos.so`。本地 runner 为 `relink_qpa_popup.sh`；后续干净重建走 `cmake/ohos/build-qt-ohos.sh`，两个 patch 已各自通过临时副本 reverse → forward → reverse dry-run。

原生、暂存、HAP 签名和覆盖安装沿用交接文档 §3：

```powershell
docker exec mixxx-ohos-build cmake --build /data/mixxx-build/ohos -j 24
docker exec -e STAGE_ONLY=1 -e OHOS_SDK_ROOT=/apps/harmony/sdk/default/openharmony mixxx-ohos-build bash /data/src/mixxx/packaging/ohos/build-hap.sh
$env:DEVECO_SDK_HOME = 'C:/Program Files/Huawei/DevEco Studio/sdk'
$env:JAVA_HOME = 'C:/Program Files/Huawei/DevEco Studio/jbr'
Set-Location D:/Git/mixxx/packaging/ohos
node 'C:/Program Files/Huawei/DevEco Studio/tools/hvigor/bin/hvigorw.js' --mode module -p module=entry@default -p product=default assembleHap --no-daemon
& 'C:/Program Files/Huawei/DevEco Studio/sdk/default/openharmony/toolchains/hdc.exe' -t 5KPBB25818203996 install -r 'D:\Git\mixxx\packaging\ohos\entry\build\default\outputs\default\entry-default-signed.hap'
```

最终 Qt/native build exit 0，hvigor `BUILD SUCCESSFUL in 8 s 935 ms`，HDC `install bundle successfully`。生产包：`packaging/ohos/entry/build/default/outputs/default/entry-default-signed.hap`。

| 产物 | 最终 SHA256 |
|---|---|
| signed HAP | `4f6ece8ac039b36348c69455aff890efff25ec648f5327ccea2cbf0d08e9c703` |
| libmixxx.so native/staged | `b2e0df4965c653cdd8339c6c19de8e6c7b1f950b14e7b9fe54a50e0cda4f0ed9` |
| libmixxx.so stripped/packed | `61eda05b2e711a35dc31ab20cc5d4bc6d501b6696ad2b3287b8b2b645eb53cd6` |
| libqohos.so native/staged | `b27bbd6b312783eee467f8df12cc578fe97ae986d760524e03dc5f3a6133ffc0` |
| libqohos.so stripped/packed | `7d51477d74d0dfe0433e7c3b9f6887c29dce408bc333403bfb2bc6cd56587d46` |
| libmixxxohosmedia.so native/staged | `ff1f91f9256def0587538e4a3143bc92b55b3e621a7a802c39223107f785ef89` |
| libmixxxohosmedia.so stripped/packed | `deb7d5f03f70964da15d7caf930e3cfe6940df4a85d881922eb4974f960fb6d6` |

`persistence-artifact-hashes.json` 记录源码/目标时间、三库 native=staged、stripped=packed 及 **3246** 个资源校验。此前包/hash 是历史证据。

### Validation

实机仍为 MatePad Mini（2560×1600），收尾 PID **32768**。

| 检查 | 结果 |
|---|---|
| 主题落盘/强杀重启 | Classic 应用后 Scheme 存盘，直接结束进程重启仍 Classic；覆盖安装保留。最终恢复 PaleMoon，再在生产包强杀重启，LateNight/PaleMoon、Mixer=1 保持 |
| 持久布局开关 | Mixer 隐藏存为 0，强杀重启仍隐藏；收尾恢复 1。未整份回写测试前配置 |
| 浮窗主体与移动缩小 | 原版界面；点选/加载、播放、暂停、CUE、菜单进入 Preferences、分类切换可用，移动/缩小后点选仍有效 |
| 最终生产包浮窗 popup | 首次触摸颜色下拉保持展开，选择 PaleMoon/Apply 生效，Cancel 一次关闭且无误弹键盘；首次菜单展开并触摸进入设置也通过 |
| 主题重载 | 全屏 Classic Apply 后 Preferences 不透明、按钮可用，Cancel 后无键盘；最终浮窗 PaleMoon Apply/Cancel 同样通过，浮窗模式保留 |
| 公共日志 | 自动创建 `Download/com.pomelo.mixxx/logs`，mixxx.log、ability.log 非空持续更新，启动轮转 `.1`…`.10`；最终重启后约 32 KB/4.5 KB，完整启动记录保留 |
| host 回归 | `persistence-final-dialog-host.log` exit 0：原皮肤对象/缩放/极小提示恢复，触摸滚动和 tap、无 swipe click，超大弹窗原保存按钮可达，键盘避让与 press/release，真实 modal exec 均 PASS |

收尾恢复全屏，《当》加载于 Deck1 开头/CUE（0:00.01），Deck1/2/Preview 停止、crossfader 中央、FX 关闭，无弹窗/键盘，曲库仍 8 首。用户音乐、标签、其它配置及既有 IDE/工作区修改保留；仅覆盖安装，无卸载/清数据，无 commit/push/PR/Issue。

### Evidence and log collection

本地证据在忽略目录 `docs/ohos/logs/20260928-continuation/`：

- 保存：`persistence-applied2.cfg`、`persistence-theme-restarted.jpeg`、`persistence-layout-restarted.{jpeg,cfg}`；最终 `persistence-wrap-config-{before,after}.log`、`persistence-wrap-restarted.jpeg`。
- 浮窗：`persistence-final-float-{interface,playing,paused,cue}.jpeg`、`persistence-final-resized3.jpeg`、`persistence-final-resized-selection.jpeg`；最终 `persistence-final-floating-combo.jpeg`、`persistence-final-floating-palemoon-applied.jpeg`、`persistence-wrap-float-{cancelled,menu,preferences}.jpeg`。
- 主题焦点：`persistence-complete-classic-{applied,cancelled}.jpeg`；早期 `persistence-theme-window-cancelled.jpeg` 是误弹键盘反例，不是最终通过证据。
- 根因：`persistence-nonclient-diag.log`、`persistence-popup-stack-events.log`、`persistence-popup-close-origin.log`。全量/调用栈诊断不在生产代码中。
- 构建：`persistence-popup-final-qt-build.log`、`persistence-final-native-build.log`、`persistence-final-hap-build.log`、`persistence-final-install.log`、`persistence-final-package-check.log`、`persistence-artifact-hashes.json`；补丁 `persistence-final-{qt,popup}-patch-check.log`。
- 最终：`persistence-wrap-maximized.jpeg`、`persistence-wrap-restored-cue.jpeg`、`persistence-wrap-final*`（截图、WindowManager、媒体/音频、hilog、PID）、`persistence-wrap-log-list.log`；host `persistence-final-dialog-host.log`。
- 导出读取：`persistence-wrap-export-{mixxx,ability}.log`，约 40 KB/5.3 KB，首部启动记录与后续 touch/settings/window 内容存在；对应 `*-transfer.log` 是 HDC 传输输出。

用户排查路径：**Download → com.pomelo.mixxx → logs → mixxx.log / ability.log**。复现后取当前两个文件；若已重启，再取对应 `.1` 等历史。shell 物理目录：`/mnt/hmdfs/100/account/device_view/local/files/Docs/Download/com.pomelo.mixxx/logs`。HDC recv 在 Windows 用反斜杠绝对目标路径，不输出整份配置或上传用户日志。

### Remaining verification

- 手机实体、物理正反横屏、摄像头附近触摸仍待现场验证；不能将平板浮窗代替手机实测。
- 同窗同 timestamp 去重仅在本设备的事件顺序通过；其它设备事件重排待验证。浮窗下原生子窗视觉位置不总贴合父窗左上，目前菜单/下拉/原按钮操作通过。
- 无授权干净首装的日志导出待独立环境测试；不卸载/清除用户数据。周期保存不是强杀前零丢失保证，布局更新有最长约 2 秒窗口。
- 真实 DJ 低延迟/xrun、`no transport` 时间同步、4458 帧 host buffer、文件菜单退出的 Qt default Ability fatal 仍是独立待办。

> 本节结束（由 AI Agent 自动生成）。End of autonomously AI-generated section.

## Task P1.16 — 触摸长按拖歌、H/M/L 旋钮闪退及手机安装测试

> 本节由 AI Agent 自动生成。This section was written autonomously by an AI Agent.

Date: 2026-09-28

Status: **IN PROGRESS（修补已打包、平板和手机安装成功；手机系统锁屏，布局实测等待用户解锁）**

### Diagnosis and changes

- `gesture-eq-crash1.log` / `gesture-eq-crash2.log`：QtMainThread SIGABRT，调用链含 `QOhosWindowProxy::setCustomCursor`。公共 history 日志明确报告 `exception from task invoked in JS thread`、`Invalid parameter`。旋钮按下设置的透明 bitmap cursor 被 OHOS 拒绝，Qt 将异常升级为 fatal。
- `src/widget/knobeventhandler.h`：OHOS 不创建/设置透明自定义光标，不执行光标回位及 wheel 光标隐藏 timer；保留原值计算、down/up/reset。WKnob/WKnobComposed 共用处理器，桌面分支保留。
- `src/widget/wtracktableview.{h,cpp}`：曲库 viewport 接入 TapAndHoldGesture；单指按住约 700 ms、尚未滚动且不处于编辑态时，选择命中行并调用原 `DragAndDropHelper::dragTrackLocations`。普通 swipe 保持 QScroller；移动越过阈值、多指、TouchEnd/Cancel 取消长按候选。

### Build and artifact verification

容器 `mixxx-ohos-build`，输出 `/data/mixxx-build/ohos/libmixxx.so`。本轮 `cmake_autogen` 的慢扫描已中止，`gesture-eq-native-build.log` exit 143 不是成功记录。`relink_gesture_eq.py` 使用 Ninja 的实际 deps/compile 命令重编 header 消费者及曲库对象，然后重新归档 `libmixxx-lib.a`、`libmixxx-qml-lib.a` 再链接。曾遗漏归档，已修正；最终 `strings` 存在 `OHOS touch track drag`。

暂存、DevEco 签名打包通过，`gesture-eq-hap-build.log`：BUILD SUCCESSFUL in 9 s 18 ms。生产包：`packaging/ohos/entry/build/default/outputs/default/entry-default-signed.hap`。

| 产物 | SHA256 |
|---|---|
| signed HAP | `d437606958893f1aeab6c188cbc049dcb97f326919647e8dbead2dfeed5a9b4f` |
| libmixxx.so native/staged | `5300ddea4d74a8944f6911ab56ec42a1951e15a484a4aa27a10820f6eaaf3202` |
| libmixxx.so stripped/packed | `f358a533f4b86f2e6143b27f024e31dd7b6758ef9d71c4fca5bf0789bb8b4ad3` |
| libqohos.so native/staged（沿用 P1.15） | `b27bbd6b312783eee467f8df12cc578fe97ae986d760524e03dc5f3a6133ffc0` |

`gesture-eq-artifact-hashes.json` / `gesture-eq-package-check.log` 确认三库 native=staged、stripped=packed 及 **3246** 资源一致。`gesture-eq-native-archive-relink.log` 是修正归档后的最终链接证据，早期包/hash 不能用于验收。

### Validation so far

- host 真旋钮处理器：100 次 adjust/release/reset 及 wheel，值范围、释放稳定和零自定义光标调用通过；`gesture-eq-knob-host.log`。
- host 提取生产 viewportEvent/mousePressEvent，真实 Qt Touch/QScroller/TapAndHold：普通 swipe 滚动且不启动拖歌；hold 恰好一次调用原 helper 并传选中歌曲，均 PASS、exit 0；`gesture-eq-touch-drag-host.log`。初始夹具 hold 点 x=100 落在默认唯一列之外，改为 x=50 后通过，生产代码未因此改动，生成器同步修正。
- 平板日志 20:52:05.996 记录长按启动，随后 dropEventFiles 收到用户 MP3 URI，右 Deck 更新；`gesture-eq-drag2-record.log`、`gesture-eq-drag-right-foreground.jpeg`。用户同时在操作，不能把所有输入算作独立自动测试。
- 平板六个 H/M/L 的计划操作未可靠完成：用户切到其它应用，相关截图不能作为旋钮真机通过证据。用户现用 Deere (64 Samplers)、ScaleFactor=0.75、Scheme 空、Mixer=1；曲库 6 首、Browse 5 首，基线备份 `gesture-eq-user-config-before.cfg`，不恢复 P1.15 的旧主题/歌曲数量。

### Phone install and pending layout test

无线手机 target **`192.168.180.76:44559`**，机型 **VYG-AL00**，系统 **OpenHarmony-7.0.0.105**。明确选择该 TCP Connected target，平板不再注入输入。`phone_device.py` 设置有限 timeout、保存本地命令审计；每次输入重新查询 Mixxx Ability 前台状态。

```powershell
& 'C:/Program Files/Huawei/DevEco Studio/sdk/default/openharmony/toolchains/hdc.exe' -t 192.168.180.76:44559 install -r 'D:\Git\mixxx\packaging\ohos\entry\build\default\outputs\default\entry-default-signed.hap'
& 'C:/Program Files/Huawei/DevEco Studio/sdk/default/openharmony/toolchains/hdc.exe' -t 192.168.180.76:44559 shell 'aa start -a QAbility -b com.pomelo.mixxx'
```

`phone-install.log` 显示 **install bundle successfully**。启动返回 **10106102**：设备锁屏，developer mode 无法自动解锁。`power-shell wakeup` 已执行，只能亮屏；已请求用户手动解锁。尚无手机主界面截图，不能声明横屏、设置 footer、菜单、触摸或六个 H/M/L 通过。

解锁后续测：先截图确认手机实际尺寸及前台；原版横屏缩放/全界面、Preferences 各分类和原按钮/滑动、popup 点选、六个 H/M/L 调整/复位及进程/日志、首次 Music 和 logs 目录授权及长按拖到 Deck。不得沿用平板坐标倍率，截图和依赖截图的输入分开调用。物理正反旋转和摄像头遮挡仍需现场复核。

所有证据在 `docs/ohos/logs/20260928-continuation/`，仅本地保存；无卸载/清数据，无 commit/push/PR/Issue。

> 本节结束（由 AI Agent 自动生成）。End of autonomously AI-generated section.

## Task P1.17 — 平板触摸操作与 Windows 曲库迁移首版

> 本节由 AI Agent 自动生成，供用户审阅。This section was written autonomously by an AI Agent.

Date: 2026-09-29

Status: **DONE（首版交付，授权 USB 平板覆盖安装与所列操作通过；真实音乐、完整演奏语义及大包性能另待验收）**

### Goal and scope

用户已接受布局，本轮继续平板触摸操作和 PC 曲库导入。保留原 Mixxx 主界面、皮肤、歌曲菜单和 CueMenu，新增入口放入原“选项”菜单。按触屏输入来源处理，真实鼠标键盘沿原分支。本轮操作设备仅 USB 平板 `5KPBB25818203996`，2560×1600；手机无本轮新增输入或覆盖安装。

### Changed files and implementation

- `src/widget/wtracktableview.{h,cpp}`：单指长按约 700 ms 进入准备态，原位松手开原歌曲菜单，准备后移动复用 DragAndDropHelper/QDrag；滚动不转拖放，触摸多选复用 selectionModel。
- `src/widget/knobeventhandler.h`：触屏旋钮按上下相对位移调节，速度读取触摸设置；保留原 control 及桌面分支，OHOS 不使用会导致 P1.16 闪退的 custom cursor/回位。
- `src/platform/ohos/touchcompat.h`、`src/widget/wwidget.cpp`、`wpushbutton.cpp`：独立 synthetic Mouse QPointingDevice，派生事件保留真实触屏来源、点和时间戳；按下/移动/释放的 button/buttons 和空点 TouchCancel 成对处理。直接以 TouchScreen device 构造 QMouseEvent 会改写 Qt 持久 point0 并丢后续事件，已通过 host 和平板复现定位；继续经原 QWidget::event 分派。双击复位限旋钮/推子，不为演奏按钮延迟按下。
- `src/widget/whotcuebutton.cpp`：触摸编辑点按开原 CueMenu；短拖根据各原 Hotcue 的 global bounds 确认落点，调用原 Track::swapHotcues；槽位外取消。Qt QDrag 在本设备短距离拖动漏相邻目标，编辑触摸采用上述落点确认。进入编辑时释放残留原按住状态，交换后菜单恢复。鼠标和非编辑拖放仍沿原 QDrag，OHOS drag 使用 pixmap。
- `src/platform/ohos/windowadapter.{h,cpp}`、`src/widget/wmainmenubar.cpp`：原菜单增加旋钮滑动速度、多选、Hotcue 编辑和屏幕边缘模式。速度/留边保存；多选/编辑是临时操作模式。标准 QMessageBox/QProgressDialog/QInputDialog/QFileDialog 保留私有布局，避免搬移和反复 resize 导致窄高增长、按钮不可达。
- `packaging/ohos/entry/src/main/ets/common/SafeDisplay.ets`、`qability/QAbility.ets`、native media bridge 及类型声明：按当前窗口实际边缘、挖孔和圆角计算留边，自动/完全全屏/保守留边供原皮肤及弹窗使用。用户布局反馈接受不等同实体摄像头触控验收。
- `tools/ohos-migration/`：中文 Tkinter 便携 EXE，支持配置目录/cfg/RAR/ZIP；随包 7-Zip 与许可；SQLite 只读+backup 快照。多音乐根映射、单首手工关联、同名不同 location 独立落地、缺失保留；过滤 PC 路径/凭据，携带实际音乐、封面、采样器、效果、控制器文件和可选 analysis。流式 ZIP_STORED/ZIP64、SHA256、进度/取消、空间检查、结束校验；来源配置/数据库/音频不改写。
- `src/platform/ohos/migration{,archive}.{h,cpp}`、`src/mixxxmainwindow.cpp`、`CMakeLists.txt`、`QAbilityStage.ets`：原菜单系统文件选择、数量/空间预览、后台流式校验，导入新档案；SchemaManager 在副本升级数据库，映射实际落地路径并保留 track/location/CUE/列表 ID。新档案保留本机 soundconfig.xml，通过 readAll+QSaveFile 复制，修复设备上 QFile::copy 失败。启动仅接受 id 一致且 state=ready 的 import.json；原菜单切换/还原档案，下次启动生效。
- 完成回调同步删除进度窗，再下一事件循环显示结果，修复结果窗叠在 progress 上。公共导入摘要写出失败记录 warning，不把已经提交成功的档案误报为失败。

设置/DB 各档案独立，公共 `Music` 根共用。导入音频在 `Music/Imported/<档案 ID>`；启动扫描可发现其它批次音频。切换档案不移动这些文件，也不等于撤回公共目录新增歌曲。

### Build and deliverables

容器 `mixxx-ohos-build`，输出 `/data/mixxx-build/ohos/libmixxx.so`。本轮使用忽略目录中的 `relink*.py` 按 Ninja 原编译/归档/链接命令增量构建，最终 `relink_widget.py` 覆盖 wwidget/wpushbutton/whotcue/wtracktable/windowadapter，并明确链接 migration 两对象。普通 build.ninja 尚未完成完整重新配置；未来干净/正常构建需重新生成以包含 CMake 中新增的两个源文件。

最终构建和核验入口（在仓库根目录）：

```powershell
& docs/ohos/logs/20260929-implementation/pc-env/Scripts/python.exe docs/ohos/logs/20260929-implementation/relink_widget.py
docker exec -e STAGE_ONLY=1 -e OHOS_SDK_ROOT=/apps/harmony/sdk/default/openharmony mixxx-ohos-build bash /data/src/mixxx/packaging/ohos/build-hap.sh
$env:DEVECO_SDK_HOME = 'C:/Program Files/Huawei/DevEco Studio/sdk'
$env:JAVA_HOME = 'C:/Program Files/Huawei/DevEco Studio/jbr'
```

在 `packaging/ohos` 执行 DevEco `hvigorw.js --mode module -p module=entry@default -p product=default assembleHap --no-daemon`。`widget-relink-final.log`、`stage-final.log` 成功；`hvigor-final.log`：**BUILD SUCCESSFUL in 18 s 130 ms**。指定平板执行 `hdc -t 5KPBB25818203996 install -r`，成功，保留应用数据。

| 产物 | 大小/最终 SHA256 |
|---|---|
| `dist/ohos/PomeloMixxx-P1.17-unsigned.hap` | 321732851 bytes；`9a814c598699f124d8ad7f7d6d976bd174b603e7f10edfc0e20f7adb377b93f3` |
| `dist/ohos-migration/PomeloMixxxMigration-portable.zip` | 15260251 bytes；`3d758a5d4333001d49eb69fc0746320f4f9c2d270f640132b8c30819246ed672` |
| 平板覆盖安装的 signed HAP | `df75c14e852a936453580b50dfd1e841fa50a11116f1d89c3ecbd01d9db2dd6e` |
| `libmixxx.so` native/staged | `9210be75b5fb0d7ffdd13dbe5eafbe27fe71990a03e613c0d29c576b9c076114` |
| `libmixxx.so` stripped/packed | `2ac00b77353cf5d8f01f27fa2d89d76f9cc26984420ca1d113807e58344751e0` |

`final-artifacts.json` 验证 packed=stripped、含最终 Hotcue touch swap 代码、3246 资源逐字节一致，无用户 DB 入包。收尾另核对容器 native=staged。README 更新后重新打入 PC ZIP，EXE 未改变。

### Validation

| 验收项 | 结果和边界 |
|---|---|
| PC 核心 | 6 tests PASS；便携 EXE 与随包 RAR reader 实际读取用户 Mixxx.rar，GUI 展示 1521 条歌曲、4 个根 |
| 原生 archive reader | 正常解包、SHA 错误、危险路径、无效清单、压缩格式拒绝及取消 PASS；只接受工具输出的 stored ZIP |
| ZIP64 | 稀疏文件中央目录偏移超过 4 GiB，解析及取消 PASS；未进行 12 GB 真实音乐全量导入性能测试 |
| 留边计算 | 左/右挖孔、圆角、中央/靠边浮窗、fallback 和小尺寸数学回归 PASS；实体遮挡需现场复核 |
| 曲库触摸 | 长按原位松手开原菜单；长按后移动将两首同名 WAV 分别一次加载左右 Deck；多选增减；1519 行缺失列表纵向和曲库横向滑动不误拖 |
| H/M/L | 六个旋钮上下相对调节及双击复位，无崩溃；精细/标准/快速可选，精细设置覆盖安装后保留 |
| Hotcue 编辑 | 点按原 CueMenu、改色、相邻 1→2→1 往返交换、交换后再点 1 打开菜单，以及槽位外取消均通过 |
| Touch 事件 host | 真实 Qt Touch 一次 press/move/release，保留输入来源/buttons，空点取消释放 PASS；`widget-touch-final.log` |
| 平板导入 | 两次导入完成，progress 消失；新档案重启加载，schema 39→40、quick_check=ok、无孤立 location、本机 soundconfig 保留 |
| 样本数据 | 原 499 CUE、2439 PlaylistTracks、crates/crate_tracks 全表一致；146 原列表逐行一致。第二档案运行新增 2 个历史相关列表、公共扫描新增 3 条其它测试音频记录 |

测试包**只带两首 4 秒静音 WAV**，文件名相同但 location/落地路径独立；1519 首仍缺失，原 1521 元数据保留。真实音乐未提供，未验证原歌曲身份/音质或真实 CUE 落点。合成音频的摘要和路径验证不能代替实际音乐验收。

普通 CUE 按住预听/松手恢复暂停此前通过。最终新建 Hotcue 原生日志仅记录到 press，编辑模式已记录成对 press/release；**不宣称全部演奏按住语义已完成真机验收**。多指、全部演奏行为由真实 DJ 操作复核，不继续猜测性重写全局事件。

### Restore and original data verification

通过原“选项 → 配置档案／还原 → 本机原始档案”切回，`active-profile` 为零字节。清理脚本先核验测试 profile 的 id/state/packageId/included=2/missing=1519、canonical 路径、预期合成 WAV 数量及 SHA256，只删除两个测试档案及对应四个合成 WAV。没有删除用户真实 MP3 或公共根目录。

恢复截图 `pad-original-restored.png`：原 Deere (64 Samplers)、Effects 布局、音轨(6)，双 Deck 空且暂停，无模态窗。读回原 cfg/DB 后仅本地只读核验，`pad-original-final-validation.json`：

- 原 cfg **逐键及字节完全一致**：Deere (64 Samplers)、Scheme 空、ScaleFactor=0.75、show_mixer=1、show_stem_controls=0。
- `quick_check=ok`；library/track_locations 各 8 条逐行一致，其中 6 首可见；原 12 条 CUE、20 条 PlaylistTracks 及曲目顺序逐行一致；crates/crate_tracks 保持。
- 18 个原非占位列表逐行一致；原 SetlogFeature 启动逻辑重建一个空锁定 historyPlaceholder 并新增一个空历史列表，因此 Playlists 数量 19→20，非曲库迁移丢失。

没有卸载/清数据、提交/push/PR/Issue；工作区原有改动保留。

### User workflow and evidence

PC 解压便携 ZIP → 选择旧目录/cfg/RAR/ZIP → 为各旧音乐根映射实际音乐位置、处理单首疑义 → 默认携带音乐生成 ZIP → 复制到 `Download/com.pomelo.mixxx` → 原“选项 → 导入 Windows 迁移包” → 核对携带/缺失数量并导入 → 关闭并重新打开。原“配置档案／还原”可切回。详见 `tools/ohos-migration/README.md` 和 `TOUCH_AND_WINDOWS_IMPORT_PLAN.md` §0。

本地证据在忽略目录 `docs/ohos/logs/20260929-implementation/`：

- 构建/产物：`widget-relink-final.log`、`stage-final.log`、`hvigor-final.log`、`final-artifacts.json`、`pad-install.log`。
- PC/reader：`pc-tests.log`、`frozen-inspect.json`、`real-sample-test.log`、`native-tests.log`、`large-zip64-test.log`、`safe-display-test.log`。
- 手势：`pad-touch-latest-decks.png`、`pad-multi-select.png`、`pad-multi-toggle.png`、`pad-touch-missing-scrolled.png`、`pad-touch-horizontal-latest.png`、`pad-touch-six-eq.png`、`pad-touch-speed-retained.png`、`pad-touch-hotcue-final-adjacent.png`、`pad-final-edited-hotcue-menu.png`、`pad-hotcue-outside-cancel.png`、`widget-touch-final.log`。
- 导入/还原：`pad-import-final-result.png`、`pad-db-final-validation.json`、`pad-test-cleanup.json`、`pad-original-restored.png`、`pad-original-final-validation.json`。原日志、数据库和包含来源路径的材料只本地保存。

用户可取 `Download/com.pomelo.mixxx/logs/{mixxx.log,ability.log,import-<档案 ID>.json}`；当前运行日志会轮转。切换档案后公共 Music 的其它批次仍可被扫描。

### Remaining work

真实音乐身份/音质与 CUE 落点、12 GB 实际包性能、实体正反横屏与挖孔附近触摸、真实鼠标键盘、多指和 DJ 低延迟仍待验收。尚未实现多卷/补充包、设备端缺失音乐关联、按歌单选音频、合并当前库、自定义皮肤资源迁移和 OHOS 直接读 RAR；控制器文件可携带不等于支持对应硬件。

> 本节结束（由 AI Agent 自动生成）。End of autonomously AI-generated section.

## Task P1.18 — 版本跟随上游与 PC 目录迁移入口

> 本节由 AI Agent 自动生成，供用户审阅。This section was written autonomously by an AI Agent.

Date: 2026-09-29

Status: **DONE（版本回归、直接目录读取、便携工具和 HAP 打包、平板覆盖安装/关于版本核验通过）**

### Request and changes

用户要求程序版本跟随原开源工程，增加最后一段自有迭代号；另明确 PC 迁移的常规入口应直接读取 PC 目录，不要求制作 RAR。

- `packaging/ohos/version.json`：仅维护旧柚 revision，当前为 1。前三段和预发布标记取实际源码的 `CMakeLists.txt`，当前上游 `2.7.0-alpha`，显示 `2.7.0.1-alpha`。
- `cmake/ohos/ConfigureVersion.cmake`、`src/platform/ohos/version.h.in`、`CMakeLists.txt`：OHOS 配置时生成显示版本头，并将 revision 文件登记为重新配置依赖。
- `packaging/ohos/version.cjs`、`hvigorfile.ts`、`AppScope/app.json5`：每次 DevEco/Hvigor 打包自动同步 versionName/versionCode。code 按上游三段、alpha/beta/rc/正式版阶段及旧柚 revision 排序，当前 `207000001`，可以覆盖历史 code=`1000000` 的包；规则见 `VERSIONING.md`。
- `src/util/versionstore.{h,cpp}`、`src/main.cpp`、`src/dialog/dlgabout.cpp`、`src/qml/qmlapplicationproxy.cpp`：新增 applicationVersion，供关于、Qt/QML 显示和启动日志使用。原 version()/versionNumber()/versionSuffix() 及配置升级继续保持上游语义。
- `tools/ohos-migration/migration_gui.py`：自动识别默认配置目录；主要按钮为“读取本机 Mixxx 配置”“选择其他配置目录”，显示识别出的目录。cfg/RAR/ZIP 是已有备份的次要入口。正常直接读配置目录和原有效音乐路径，不要求先压缩；搬过位置的音乐仍可映射。
- 更新 `tools/ohos-migration/README.md`、计划、交接文档，新增 `docs/ohos/VERSIONING.md`。开发任务 P1.xx 保留作历史编号，新 HAP 文件名使用实际显示版本。

### Build and validation

证据目录：`docs/ohos/logs/20260929-version/`。

- `node --test packaging/ohos/version.test.cjs`：2 tests PASS；自有迭代递增、上游版本变动自动跟随、manifest 同步，alpha→beta→rc→正式版→下个 patch 在 revision 重置后 code 仍递增。测试仅创建/清理经路径核验的临时夹具。
- `smoke_pc_directory.py`：直接配置目录自动识别与 GUI 读取 1521 条/499 CUE/146 列表/4 根；重建 frozen EXE 的 `--inspect` 直接读同一目录也通过。前后源 cfg/DB 的 SHA256 不变，未要求或调用备份解压入口。样本来自此前只读提取目录，不代表这台 PC 已安装 Windows Mixxx。
- 便携工具重新执行 `tools/ohos-migration/build.ps1 -Python <pc-env/python.exe>`，`pc-build.log` 成功；更新同一分发 ZIP。
- Native 增量 `docker exec mixxx-ohos-build python3 /data/src/mixxx/docs/ohos/logs/20260929-version/relink_version.py`：重建三个 PCH，再编译 versionstore/about/QML proxy/main，归档并链接；明确保留 migration 两对象，`native-build.log` PASS。正常构建仍应重新生成 CMake/Ninja 文件。
- `build-hap.sh` STAGE_ONLY 暂存，DevEco `hvigorw.js --mode module -p module=entry@default -p product=default assembleHap --no-daemon`：`hvigor.log` 首行记录自动版本同步，**BUILD SUCCESSFUL in 23 s 148 ms**。
- HAP 内 module.json versionName/code 正确，stripped=packed native，包含 UTF-16 显示版本字符串，无用户 DB。容器 native=staged 的 SHA256 同值。
- 仅指定 USB 平板 `5KPBB25818203996` 覆盖安装成功、启动成功；前后截图保留原 Deere 皮肤和音轨(6)。关于显示 `旧柚Mixxx 2.7.0.1-alpha`，启动日志一致。读回 cfg **逐键保持不变**，配置 Version 仍为 `2.7.0-alpha`。关于关闭，双 Deck 空/暂停，无模态窗；手机无本轮操作。

| 产物 | SHA256 |
|---|---|
| `dist/ohos/PomeloMixxx-2.7.0.1-alpha-unsigned.hap`（321732871 bytes） | `78ce9a9a9d9a78a334f15129dc6b6c6d4fd7082c222544d280375c0d5d11ee05` |
| signed HAP（平板安装） | `06aa1052609a2f3c6738637d1356fe10948bc284534774debcf8a3f66a79ba71` |
| `libmixxx.so` native/staged | `cceade504351bf49a8c9ab8b3b40a2e76681b5baabdb2b7a75cfa5837f9fb228` |
| `libmixxx.so` stripped/packed | `20df40442645ba254d323ecb3cbb7abb6641dbada30e53f2c4f3d9d3fb0cab25` |
| `dist/ohos-migration/PomeloMixxxMigration-portable.zip`（15261037 bytes） | `67f10d6de7eaaef971b7bc56f09f0d651bc665d86ba2d8f87ad1acff6e7adedd` |

`artifacts.json`、`pad-validation.json`、`version-tests.log`、`frozen-directory-inspect.json` 保存摘要。截图及受保护的原配置/日志在 `20260929-implementation/pad-version-{before,installed,about,final}.png`、`pad-version-new.{cfg,log}`，仅本地保存。

没有自动同步/提交/push 上游或创建 PR/Issue，没有卸载/清数据。版本在用户实际同步源码后自动跟随，修改 revision 后需要重新编译和暂存 native。P1.17 的触摸和迁移验收边界保持；本轮没有重复全套 DJ 操作或真实音乐大包测试。

> 本节结束（由 AI Agent 自动生成）。End of autonomously AI-generated section.

## Task P1.19 — 预留智慧屏设备支持

> 本节由 AI Agent 自动生成，供用户审阅。This section was written autonomously by an AI Agent.

Date: 2026-09-29

Status: **DONE（智慧屏声明与完整 HAP 打包核验通过，智慧屏真机/遥控器尚未验收）**

用户要求工程包含智慧屏，未来上架可不选该设备范围。根据 HarmonyOS module.json5 的 deviceTypes 定义，智慧屏使用 `tv`。

- `packaging/ohos/entry/src/main/module.json5`：原 `phone/tablet/2in1` 设备声明增加 `tv`。保持原界面及现有鼠标键盘/触摸分支；本轮没有新增遥控器导航实现。
- `packaging/ohos/version.json`：revision 1→2，生成 `2.7.0.2-alpha`、versionCode=`207000002`。原生版本头及库重建，保持显示版本与系统包版本一致；配置升级版本继续为上游 `2.7.0-alpha`。
- 复用 P1.18 `relink_version.py` 重建、归档、链接，`native-build.log` PASS；将新 native 同步到 entry/libs，再执行 DevEco `hvigorw.js --mode module -p module=entry@default -p product=default assembleHap --no-daemon`。`hvigor.log`：**BUILD SUCCESSFUL in 42 s 934 ms**。
- 构建完成后打开 HAP 核验：module.json 的 deviceTypes 四项齐全、pack.info 含 tv；versionName/code 正确；native 含相同显示版本，容器 native=staged、stripped=packed，无用户 DB 入包。

| 产物 | SHA256 |
|---|---|
| `dist/ohos/PomeloMixxx-2.7.0.2-alpha-unsigned.hap`（321732886 bytes） | `b1ae7abbf76c36e874f6f476713797ddcbe5b290e3e8db1524cf0d0730465037` |
| signed HAP（仅构建，本轮未安装） | `051de4fca583ce0169bdf90ea30643a08efdd9ad28b98844358cdad57ab8cacb` |
| `libmixxx.so` native/staged | `cd08f04a83d103e4d9ff202abc12aeb24554757969767d50a5df7d055ee33419` |
| `libmixxx.so` stripped/packed | `54dce0e6e0de8ee16cb1e0483df997fd43eb93362972361b0b13f268993146fb` |

证据：忽略目录 `docs/ohos/logs/20260929-tv/{native-build.log,hvigor.log,artifacts.json}`，验证入口 `validate_artifacts.py`。本轮未安装或操作设备，平板仍为 P1.18 的 `2.7.0.1-alpha`；PC ZIP 未修改。没有提交/push/PR/Issue。

工程设备声明与最终上架发布范围分开选择。智慧屏实体安装、遥控器焦点与按键操作、音频/后台和导入权限需后续独立验收，不能把包内 tv 声明作为这些功能已通过的证据。

> 本节结束（由 AI Agent 自动生成）。End of autonomously AI-generated section.

## Task P1.20 — 系统媒体歌曲封面

> 本节由 AI Agent 自动生成，供用户审阅。This section was written autonomously by an AI Agent.

Date: 2026-09-29

Status: **DONE（源码、构建及平板播放胶囊/展开卡片通过；超级桌面跨设备未实测）**

用户要求后台播放和超级桌面的媒体图像显示歌曲封面。原 ArkTS 会话始终把 startIcon 设置为 AVMetadata.mediaImage，现改为当前歌曲封面，缺失或读取失败回退应用图。

- `src/platform/ohos/mediacontroller.{h,cpp}`：沿用原 CoverInfo 内嵌/外部封面加载器，QtConcurrent 异步读取、缩放至最长边 512、编码 PNG；按封面信息变化触发，以 generation 拒绝迟到结果，换歌清旧图。读取不传 TrackPointer，不修改歌曲封面/标签。
- `mediabridge.{h,cpp}` 与类型声明：状态、封面键和 PNG 原子发布；`readArtwork(key)` 返回匹配键的 ArrayBuffer，限制 2 MiB。进度 JSON 不携带图片，避免周期重复传大数据。
- `MixxxMediaSession.ets`：按封面键解码/更新 metadata，按键与 asset 再检查异步结果；无图/坏图回退。只在变化时解码，成功设置 metadata 后释放旧图；异常释放候选；销毁等待初始化与进行中的同步，释放 PixelMap/ImageSource。同步读取最新播放状态，维持原媒体控制和后台任务逻辑。
- `packaging/ohos/media-session.test.cjs`：5 项行为测试涵盖换歌、同歌封面更新、轮询不重读、迟到图丢弃、状态/图键不匹配重试、坏图回退、setAVMetadata 失败重试及销毁资源释放。使用 TypeScript 转译实际 ArkTS 源码并模拟系统 API；与 2 项版本测试合计 **7 tests PASS**，不是原生/系统服务的替代验收。
- revision=3，版本 **2.7.0.3-alpha**、code **207000003**，上游配置版本仍为 2.7.0-alpha，设备声明 phone/tablet/2in1/tv。

### Build and validation

证据目录 `docs/ohos/logs/20260929-artwork/`。Native 重建、完整 ArkTS/HAP 编译通过，最终 **BUILD SUCCESSFUL in 12 s 231 ms**。首次增量遗漏 MediaController 创建方；最终明确重编 `mixxxmainwindow.cpp`、三个 PCH、媒体及版本对象，再归档/保留 migration 两对象和链接两库。修改类布局需重编消费者，正常构建应完整重新生成 CMake/Ninja，不能只沿旧 commands 重编 controller。

平板 `5KPBB25818203996` 覆盖安装最终签名包，保留原数据：

- 桌面顶部胶囊显示绿色歌曲封面；展开系统卡片显示相同大封面、标题、进度和播放控件。
- 系统暂停、下一首由绿封面切到红封面、继续播放、再下一首经第二红封面切到无封面歌曲通过。无封面回退应用图，未残留上一首。后台绿色歌曲位置从约 60 s→120 s→180 s 持续增长，恢复前台正常。
- 收尾系统暂停、双 Deck 卸载，native asset 为空/position=0；原 Deere (64 Samplers) 界面、原 cfg 逐键/字节相同，8 条歌曲元数据/封面字段和 12 条 CUE 相同。20 条原歌单关联保持，原逻辑新增 4 条本次播放历史；quick_check=ok。
- `pad-validation.json`、`pad-hilog-final.log` 保存结果；`pad-green-capsule-expanded.png`、`pad-green-paused.png`、`pad-red-next.png`、`pad-no-cover.png` 为系统截图；`card-*` 仅裁剪媒体卡片，减少桌面其它内容。应用/配置快照在 implementation 目录 `pad-artwork-*`，只在本地保存。

| 产物 | SHA256 |
|---|---|
| `dist/ohos/PomeloMixxx-2.7.0.3-alpha-unsigned.hap`（321751169 bytes） | `d5d574daba8eefbdd4937942310ff28cb537ea8300ca11ace5eb4a06e358893c` |
| signed HAP（平板最终安装） | `6ecf34fbc1b61e444164f1b817a87642bfd93f61a46564ebb354ad539374ef57` |
| `libmixxx.so` native/staged | `4b702061ddc2cf2049b5f321f0e5cbbdd435f2fb3dd122927e2dda7def28e42c` |
| `libmixxx.so` stripped/packed | `c71fa0c559a2f198da40174cfaa4d87b162bf9af32b1de86d16b17c910c8ed35` |
| `libmixxxohosmedia.so` native/staged | `1595696b46ffcec652824cad59a627178420a5c5dcef337fff6075943ce410cc` |
| `libmixxxohosmedia.so` stripped/packed | `9fdef8aa8acf7070ef3521c499e9e124d48f050adad27840b544fe3af9a15a06` |

`artifacts.json`、`validate_artifacts.py` 核验两库和 manifest，原生显示版本与启动日志一致。PC ZIP 未改，无手机或智慧屏本轮操作，无卸载/清数据或 Git 提交/推送。

超级桌面跨设备投送尚未实测；同一 AVSession.mediaImage 已可供系统读取，但不能据此声称所有远端系统入口都验收。播放卡片中的应用身份小角标由系统保留（AVMetadata.bundleIcon 为只读），没有更改桌面应用图标。原版主界面不变。

本轮自动触摸长按/拖歌未成功复验：有 hold/context/drag 原生日志，但菜单未可见、Drop 落入 library；通过原曲库 Enter 加载完成封面测试。该输入路径需后续单独调查，P1.17 历史通过记录不作为当前自动复验通过。没有为封面任务猜测性改写全局输入。

> 本节结束（由 AI Agent 自动生成）。End of autonomously AI-generated section.

## Task P1.21 — 官方媒体生命周期与暂停闲置释放

> 本节由 AI Agent 自动生成，供用户审阅。This section was written autonomously by an AI Agent.

Date: 2026-09-29

Status: **DONE（23 项回归、构建与平板切歌/短暂暂停、十分钟后台闲置释放、释放后新建卡片恢复及数据保留核验通过）**

### Official guidance and policy

用户要求未播放闲置后合理关闭实况窗，参考官方建议，避免普通暂停即销毁或一直持有。

- OpenHarmony 官方 `avsession-background-scene.md`：播放时保持 AVSession 和 AUDIO_PLAYBACK 长时任务；暂停/停止时及时主动取消长时任务，继续播放时重新申请；结束进程或完全退出播放业务时销毁会话，避免频繁创建/释放。
- 鸿蒙没有统一的“暂停 N 分钟收起实况窗”建议。[Android Media3 MediaSessionService](https://github.com/androidx/media/blob/release/libraries/session/src/main/java/androidx/media3/session/MediaSessionService.java) 的公开默认前台服务宽限为 600000 ms。本项目参考选择十分钟，不能表述为鸿蒙官方时限。
- 首次启动/只加载未播放不创建会话；任一 Deck 播放保持；全部暂停及时取消长时任务，同一会话在十分钟宽限内保留恢复控制；连续暂停到期视为结束本次系统播放业务，STOP→deactivate→destroy，释放歌曲 PixelMap；下次播放创建新会话并重发歌曲、封面和进度。
- 空载、native 不可用、明确系统 stop 或 Ability 销毁释放；暂停定位/歌曲 metadata 更新不延长计时。停止后台任务独立先于销毁，销毁失败不继续占用播放长时任务，保留会话供重试。
- 暂停释放长时任务后系统可能冻结 ArkTS 定时器；清理发生在到期后下一次允许执行时，不保证所有设备后台准点十分钟。没有为等待倒计时保留无业务播放保活，也不循环申请短时任务。完整说明与官方链接见 `MEDIA_SESSION_LIFECYCLE.md`。

### Implementation and regressions

- `MixxxMediaSession.ets`：会话按播放需要创建，串行同步与销毁，连续暂停计时，暂停状态去重，系统 stop 优先；销毁异常保留对象重试；延续 P1.20 的封面原子键、迟到图丢弃和 ImageSource/PixelMap 释放。
- 本机只 deactivate/activate 同一会话后音频能恢复但实况窗不再显示，故十分钟结束后完整销毁，下次新建。该中间试验记录在 `final-avsession-{paused,after-timeout,resumed}.txt` 与 `idle-session-validation.json`；这些旧命名的 final 文件不是最终包验收记录。
- 首个完整释放候选在 next 异步加载间隙把空 asset 当真正卸载，导致会话销毁后后台新建卡片不恢复；`release-hilog.log` 与 `release-card-next.png` 为失败证据，不能作为切歌通过。
- `MediaController::publish()` 新增 `loading`：通过每个播放器已提交的曲目与 PlayerInfo 已加载曲目比较区分加载过渡，覆盖系统切歌与 GUI 加载。类布局本次未再改变；bridge 的初始状态明确 loading=false。ArkTS 保留已有会话/封面，加载最多十五秒，失败/超时释放；暂停加载不重启长时任务、不延长闲置期限。系统 play 先申请任务，给尚在 Qt 队列的命令五秒确认，避免旧暂停快照立即撤销新任务。
- `media-session.test.cjs` 转译实际 ArkTS 并模拟系统 API/时间，21 项媒体测试 + 2 项版本测试 **23 PASS**：十分钟边界、普通暂停复用、超时后新建、初次未播放、定位/metadata 不续期、多 Deck 播放、明确 stop、失败重试、decode/create 与 destroy 并发资源释放，以及 next/previous 异步空加载、加载失败/超时、暂停加载、异步 play 完成/超时、旧快照 stop。

### Build and tablet evidence

证据目录 `docs/ohos/logs/20260929-idle-media/`。revision=4，versionName=`2.7.0.4-alpha`，versionCode=`207000004`，设备声明 phone/tablet/2in1/tv。

- 先重建版本/PCH/创建方和媒体对象，`native-build.log`；切歌修补后 `docker exec mixxx-ohos-build python3 /data/src/mixxx/docs/ohos/logs/20260929-idle-media/relink_transition.py` 重编 controller/bridge、归档时明确保留 migration/migrationarchive，再链接两库，`native-transition-build.log` PASS。正常生产构建应重新生成 CMake/Ninja。
- 两库同步 entry/libs 后，DevEco `hvigorw.js --mode module -p module=entry@default -p product=default assembleHap --no-daemon`，`hvigor-final.log` **BUILD SUCCESSFUL in 12 s 7 ms**。先确认 exit=0，再运行 `validate_artifacts.py`；容器=staged，两库 stripped=packed，HAP 包含相同 native 显示版本，无用户 DB。
- 仅授权 USB 平板 `5KPBB25818203996` 覆盖安装签名包，`pad-install-final.log` 成功，PID 8383。`verified-startup.png`/`verified-loaded.png` 为原 Deere 界面；空载启动及加载但未播放的 AVSession dump 均无本应用会话。
- 13:37:17 开始后台播放，13:38:33 next 绿封面→红封面，13:39:27 previous 红→绿；13:39:46 暂停，13:40:50 系统继续，13:41:52 再暂停。`verified-controls-validation.json` 核验全部控制前后的 session ID 一致，只有一次 activate，没有 retired；`verified-{green,next,previous}-card.png`、`verified-short-{pause,resume}.png` 显示当前封面/进度/控制。13:41:52.011 pause accepted，13:41:52.331 长时任务已取消。
- `watch_idle.py` 仅读系统会话，每 45 秒记录，无输入或应用前台唤醒；13:51:15 仍 active，13:51:52.492 `retired: paused timeout`，距首次暂停状态 **600.160 秒**。系统在边界另发一次 pause，未重置暂停计时；13:52:00 的系统会话记录已无本应用，`verified-expired-system.png` 确认实况窗消失。该本机结果不保证其它设备冻结后的定时器同样准点。
- 返回前台 `verified-expired-pad-paused.png` 仍原歌曲 **1:20.44**；13:53:53 播放创建新 ID，`verified-recreated-{capsule,card}.png` 显示绿封面、当前进度与可用控制，后台进度 80.917→141.589 秒。13:55:24 从新卡片暂停成功并取消任务，随后卸载 Deck1；13:57:18 `retired: empty`，双 Deck 空/暂停，系统 dump 无本应用会话。`verified-lifecycle-validation.json` 确认闲置退出、新 ID 与最终空载释放，无本次 media sync 错误。
- `verified-original-final.png` 为收尾界面；读回本轮最新基线对应的 cfg/DB，`verified-data-validation.json`：cfg **逐字节相同**，8 条歌曲标签/封面字段与路径、**16 条 CUE** 全部相同，原歌单关联保持，quick_check=ok。原逻辑新增 6 条本次播放历史；不恢复上一轮的 12 条 CUE 基线。公共 `Download/com.pomelo.mixxx/logs/{ability.log,mixxx.log}` 仍可读取，启动日志版本一致。

| 产物 | SHA256 |
|---|---|
| `dist/ohos/PomeloMixxx-2.7.0.4-alpha-unsigned.hap`（321764371 bytes） | `b0e3156e4d099468e62127f6ded5f702aff82575db02ad6d5ead26387167a284` |
| signed HAP（平板最终安装） | `3bea008bff25e14ebecdecbaac2dcb9f28db42783f1042b4606dd3cedb212c8d` |
| `libmixxx.so` native/staged | `a6392e1bc55e0091cbd08aa49a6fd6e5a305e4a232e94db4d3d36f11fd8ed2ed` |
| `libmixxx.so` stripped/packed | `6251e0099d3efef07da35eba9d887422b2fce9461e1a8a42cef4676c6cc91ec7` |
| `libmixxxohosmedia.so` native/staged | `1485ac0e4bdb954798f5cc603f5ee1429589cf8957eee45fb04f557217225e76` |
| `libmixxxohosmedia.so` stripped/packed | `f53e012d7f37ddee8d3090268687878b21180d69feded5275cf4c18010b09478` |

PC 工具未改；没有手机/智慧屏/超级桌面跨设备操作或验收，没有卸载/清数据、Git 提交/推送或 PR/Issue。原版界面、用户设置/歌曲/CUE 保留；主题、触摸和迁移历史边界见 P1.17–P1.20。

> 本节结束（由 AI Agent 自动生成）。End of autonomously AI-generated section.

## Task P1.22 — 上架 APP 构建与发布签名

> 本节由 AI Agent 自动生成，供用户审阅。This section was written autonomously by an AI Agent.

Date: 2026-09-29

Status: **PASS（release APP 构建、发布签名与官方验签完成；尚未上传商店）**

### Goal

按用户指定的 `sign/release/` 发布材料与别名 `hv` 生成上架 APP。保留上游版本规则，本次纯构建/签名不增加 revision：`2.7.0.4-alpha` / `207000004`。

### Changed files

- `dist/ohos/PomeloMixxx-2.7.0.4-alpha-release.app`、`PomeloMixxx-2.7.0.4-alpha-release-signed.hap` 与 `SHA256SUMS-release.txt`。
- `docs/ohos/HANDOVER_CODEX.md`、本状态文档；本轮未修改应用功能代码或界面。
- 忽略目录中的 `sign/release/hyperview-release-chain.cer`：原 CER 三张证书顺序为 root→intermediate→leaf，另生成 leaf→intermediate→root 供签名工具读取，原材料保留。忽略的 `docs/ohos/logs/20260929-release/` 保存诊断脚本与验证证据。

### Build

使用 DevEco 自带 Node/JBR/SDK，在 `packaging/ohos` 执行：

```text
node hvigorw.js --mode project -p product=default -p buildMode=release assembleApp --no-daemon
```

结果：exit=0，**BUILD SUCCESSFUL in 1 min 6 s 636 ms**；release ArkTS 混淆构建通过。Hvigor 原默认签名产物只作中间输出，分发包另使用用户发布材料签名。

- `hap-sign-tool.jar verify-profile` 验证原 P7B 签名成功：type=`release`、distribution=`app_gallery`、bundle=`com.pomelo.mixxx`，当前在有效期内。P12 的 `hv` 公钥与 Profile 内 leaf 相同；OpenSSL 证书链验证通过。
- 按本机 Hvigor `SignApp` builder 的官方 `sign-app -mode localSign` 路径，对 project unsigned APP 和独立 unsigned HAP 使用重排的 CER、P12、P7B 与 SHA256withECDSA 签名。密码通过临时环境变量进入 Python，再传给本地签名进程；日志输出脱敏，文件/工程/文档未保存密码。
- APP/HAP `verify-app` 均成功，导出的 Profile `verify-profile` 均通过且与原 P7B 逐字节一致；导出证书集合与发布链相同，leaf SHA256=`25dc629952304fae1f3fa6abcfe8e52169393c86ba3542282905c066b2e75d89`。官方验签输出链的排列顺序不固定，验证不依赖第一张就是 leaf。
- APP 使用标准 PackageApp→SignApp 路径，内含 `entry-default.hap`、`pack.info`、`pac.json`；内嵌 HAP 与官方 unsigned APP 内 HAP 逐字节相同，APP 外层发布签名。独立 HAP 另发布签名，未把默认调试签名 HAP 装入 APP。
- 两包 module.json 核验 versionName=`2.7.0.4-alpha`、code=`207000004`、debug=false、targetAPIVersion=`60100023`、设备 phone/tablet/2in1/tv。两包 native 库哈希一致且匹配 P1.21；不包含用户配置/曲库 DB、P12/JKS 等私钥文件。

| 产物 | bytes | SHA256 |
|---|---:|---|
| release APP | 123755826 | `e7d64a35cbd151cf070cd4cee7e96f5a21a184726dfbe8a253324850ed0b23f4` |
| release signed HAP | 325742873 | `61523ebfda049dca1984ce446bb27c6c65b4c3161e04a0f621c1f99360a959ff` |
| packed `libmixxx.so` | — | `6251e0099d3efef07da35eba9d887422b2fce9461e1a8a42cef4676c6cc91ec7` |
| packed `libmixxxohosmedia.so` | — | `f53e012d7f37ddee8d3090268687878b21180d69feded5275cf4c18010b09478` |

### Device validation

本轮没有操作手机/平板/智慧屏，未安装发布包。平板仍为 P1.21 调试签名包，功能与数据保留验收沿用 P1.21；不能将本轮包验签表述为新增设备或商店审核通过。

### Evidence

`docs/ohos/logs/20260929-release/`：`hvigor-release.log`、`preflight.json`、`artifacts.json`、`sign-{app,hap}.log`、`verify-{app,hap}.log`、`{app,hap}-verified-profile.json`、`verify-{app,hap}-profile.log`。`dist/ohos/SHA256SUMS-release.txt` 为新发布包校验清单，旧 unsigned HAP 与其清单保留。

### Remaining blocker

本地构建/签名目标已完成。商店上传、设备范围选择与审核尚未进行，历史功能验证边界沿用 P1.17–P1.21。

### Next step

用户可将 release APP 上传 AppGallery Connect，按实际需求选择上架设备范围；后续代码改动继续遵循 `VERSIONING.md` 的上游版本加第四段迭代规则。

> 本节结束（由 AI Agent 自动生成）。End of autonomously AI-generated section.

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
| `sign/app_debug.p12`（alias `ad`，密码 Yifengling0） | E19822D2… | 14d310f4… |
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

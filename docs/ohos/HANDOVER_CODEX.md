# Mixxx → HarmonyOS (OHOS) 移植 — 交接文档

> 本文档由 AI Agent 自动整理，用于把当前工作交接给后续开发者/AI 助手。
> This handover document was written autonomously by an AI Agent.
>
> 阅读顺序建议：§1 现状 → §2 环境 → §3 编译 → §6 已攻克的坑（最重要）→ §7 未解决问题。

---

## 1. 当前状态（一句话）

**2026-09-28 最新 P1.16：修复触摸长按拖歌及 H/M/L 旋钮触发的 Qt 自定义光标 fatal。最新生产包已安装到平板和无线连接的 VYG-AL00 手机；手机启动测试目前被系统锁屏阻挡，等待用户解锁。** P1.15 的主题保存、浮窗触摸和公共日志修补保留。
Music 自动创建与扫描/搜索、MP3 加载播放、顶部 105 像素留白消除继续有效。用户已确认听到声音、播放正常。低延迟、真实 DJ 设置、手机实体小屏及正反横屏物理旋转仍未验证；不得将平板分屏当作手机测试。

**暂停时双层滚动波形闪烁/CUE 状态切换已完成修复并装机复验**：旧包抓到时间仍为 2:11、波形却返回开头的异常帧；新包暂停连续 40 张截图波形区域完全一致。实际位置类的批量回调回归从 20/40 次无效刷新降为 0/40，播放、定位和前台恢复也通过。最终部署与验证范围见 P1.13，实际屏幕高频观感仍待用户复核。

P1.14 收尾另抓到**空碟机背景**黑/灰切换（有歌曲的波形/CUE 稳定），已补原皮肤背景色的绘制面。该阶段最终包空双 Deck 16 张、加载并返回前台后暂停 16 张上下区域分别逐像素一致，播放 4 张持续更新。最新包/PID/哈希见 §7；P1.11–P1.14 的包和 PID 仅为历史证据。

| 能力 | 状态 |
|---|---|
| CMake OHOS 平台分支 + 交叉编译 | ✅ |
| Qt 6.13 (dev) for OHOS 全套（qtbase/declarative/shadertools/svg/imageformats/5compat/multimedia） | ✅ 已编译并安装到 volume |
| vcpkg `arm64-ohos` 依赖（30 包） | ✅ |
| `libmixxx.so` 交叉编译 | ✅ |
| HAP 打包 + DevEco 调试签名 + 真机安装 | ✅ |
| Qt/QML 启动链（libqohos QPA） | ✅ |
| 完整 Mixxx UI（菜单栏/顶栏/资源库/双 Deck/Mixer） | ✅ |
| 原版界面缩放 / 极小窗口 | ✅ 普通分屏保留完整原皮肤；比例不足 50% 提示放大，恢复全屏可用；不启用自创手机页面 |
| 设置与常用弹窗 | ✅ 14 设置分类巡检，原操作按钮可达；键盘避让、模态 exec 和外侧触摸层级恢复通过；罕见弹窗未全部实测 |
| 主题与布局保存 | ✅ Preferences 应用/确认立即保存；持久 Control 在 GUI 线程每 2 秒及退后台时保存；换主题、Mixer 开关、强杀重启与覆盖安装通过 |
| 自由浮窗输入 | ✅ 修正 Scene 缩放坐标、重复事件、标题栏覆盖及子窗尺寸更新误关 popup；移动/缩小后的点选、播放/暂停/CUE、菜单/分类/下拉框通过 |
| 公共运行日志 | ✅ `Download/com.pomelo.mixxx/logs/{mixxx.log,ability.log}` 自动创建；补齐启动日志、持续追加、每次启动轮转并保留 10 份历史 |
| 触摸滑动 | ✅ 设置/关于长页、曲库横向滑动及点选通过；曲库纵向由 host 200 行回归验证 |
| 横屏启动 / 双方向横屏 | ✅ manifest 与 preferredOrientation=7 已生效；实体反向旋转另待验证 |
| 后台播放 / 系统媒体胶囊 | ✅ 本机后台进度持续增加；歌曲显示和暂停/继续/上一首/下一首实测通过 |
| 关于个人信息 | ✅ 原作者/授权标签保留，个人信息可编辑保存；尚无用户文案，字段为空 |
| 中文（`res/translations/*.qm` 随包） | ✅ |
| 全屏沉浸（隐藏 OHOS 状态栏/导航栏，消除顶部留白） | ✅ 内容覆盖屏幕顶部，恢复前台/键盘收起复验通过；实体摄像头附近的遮挡与触控另待人工确认 |
| OHAudio 后端设备枚举（`Pa_CountDevices found 2`） | ✅ |
| 音频流真正打开并启动（`stream opened` → `stream started`） | ✅ 真机已验证，进程持续存活不再崩溃 |
| 音乐目录固定为 `Download/<包名>/Music`（自动创建） | ✅ 真机已创建；列表入口是 `我的平板 → Download → com.pomelo.mixxx → Music`，进入后导航路径仍可能显示旧名称 `label` |
| 资源库自动扫描该目录 | ✅ 启动新增扫描 1 首 WAV，共 8 曲；Browse 与搜索匹配/无匹配复验通过 |
| 着色器 / 渲染 | ✅ 两轮修复并复验归零：`materialshader.cpp` 选 GLSL ES 变体 + `textureshader.cpp` 去 `#version 120` / 补 `highp`（§6.1 / §6.4） |

---

## 2. 环境与路径（务必先固化）

### 2.0 Git 仓库与上游联动（2026-09-28 起）

| 项 | 值 |
|---|---|
| 本仓库远程 `origin` | `https://github.com/PomeloTechLabs/mixxx`（**fork**，`forked from mixxxdj/mixxx`） |
| 上游远程 `upstream` | `https://github.com/mixxxdj/mixxx`（原 origin，partial clone `blob:none`） |
| 工作分支 | `feature/ohos-port`（已推送到 origin；fork 的 `main` 保持跟踪上游，**不要**用本地旧基线覆盖它） |
| 未来同步上游 | `git fetch upstream` 后将 `upstream/main` rebase/merge 进 `feature/ohos-port`；或用 GitHub 页面的 *Sync fork* |
| 推送注意 | 网络走代理 `127.0.0.1:8080`（repo 的 `http.proxy` 已配置；gh CLI 需 `HTTPS_PROXY` 环境变量） |
| 敏感文件 | `sign/`（签名私钥）与 `packaging/ohos/.idea/` 已写入 `.git/info/exclude`，**永不入库**；`resfile/` 生成物已 untrack 并进 `.gitignore`（构建时 `build-hap.sh` 重新生成） |

| 项 | 值 |
|---|---|
| 仓库 | `D:\Git\mixxx`（独立 git repo，分支 `feature/ohos-port`，基线 `bcfb7956`） |
| git 代理 | `http://127.0.0.1:8080`（已写入 repo config，clone/fetch 需要） |
| Docker 镜像 | `winehua-dev:latest`（ubuntu 24.04 + cmake/ninja/clang/node/openjdk17） |
| **构建容器** | **`mixxx-ohos-build`**（已常驻，内含 GL 依赖 libegl1/libgl1/libopengl0/gperf） |
| 容器挂载 | `F:/command-line-tools`→`/apps/harmony`；volume `mixxx-ohos-vcpkg`→`/data/vcpkg`；volume `mixxx-ohos-qt-out`→`/data/out`；volume `mixxx-ohos-mixxx-build`→`/data/mixxx-build`；`D:/Git/mixxx`→`/data/src/mixxx` |
| OHOS SDK（容器内） | `OHOS_SDK_ROOT=/apps/harmony/sdk/default/openharmony` |
| DevEco（Windows 侧打包/签名） | `C:\Program Files\Huawei\DevEco Studio`（SDK 规格 `6.1.0(23)`，jbr 作 JAVA_HOME） |
| hdc | `C:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe` |
| 真机 | 序列号 `5KPBB25818203996`（HUAWEI MatePad Mini，2560×1600） |
| 包名 / 应用名 | `com.pomelo.mixxx` / **旧柚Mixxx** |

**重建容器**（如容器丢失）：

```bash
MSYS_NO_PATHCONV=1 docker run -d --name mixxx-ohos-build \
  -v F:/command-line-tools:/apps/harmony \
  -v mixxx-ohos-vcpkg:/data/vcpkg \
  -v mixxx-ohos-qt-out:/data/out \
  -v mixxx-ohos-mixxx-build:/data/mixxx-build \
  -v D:/Git/mixxx:/data/src/mixxx \
  winehua-dev bash -c 'apt-get update -qq && apt-get install -y --no-install-recommends libegl1 libgl1 libopengl0 gperf; sleep infinity'
```

> **Windows Git Bash 必须加 `MSYS_NO_PATHCONV=1`**，否则 `/data/...`、`/data/local/tmp/...` 这类容器/设备路径会被 MSYS 改写成 `C:/Program Files/Git/...`。

---

## 3. 编译与部署流程（固定三步）

```bash
# ── 0) 改 C++ 后：编译 libmixxx.so ───────────────────────────────
MSYS_NO_PATHCONV=1 docker exec mixxx-ohos-build bash -c '
  export OHOS_SDK_ROOT=/apps/harmony/sdk/default/openharmony
  cd /data/mixxx-build/ohos && cmake --build . -j 24 2>&1 | tail -8'

# ── 1) 暂存进 HAP 工程（收集 Qt 库/插件/QML/资源/翻译 + boot 壳）──
MSYS_NO_PATHCONV=1 docker exec mixxx-ohos-build bash -c '
  export STAGE_ONLY=1 OHOS_SDK_ROOT=/apps/harmony/sdk/default/openharmony
  bash /data/src/mixxx/packaging/ohos/build-hap.sh 2>&1 | tail -5'

# ── 2) Windows 侧打包 + 签名（DevEco CLI）────────────────────────
cd D:/Git/mixxx/packaging/ohos && \
DEVECO_SDK_HOME="C:/Program Files/Huawei/DevEco Studio/sdk" \
JAVA_HOME="C:/Program Files/Huawei/DevEco Studio/jbr" \
node "C:/Program Files/Huawei/DevEco Studio/tools/hvigor/bin/hvigorw.js" \
  --mode module -p module=entry@default -p product=default assembleHap --no-daemon

# ── 3) 安装到真机 ────────────────────────────────────────────
HDC="/c/Program Files/Huawei/DevEco Studio/sdk/default/openharmony/toolchains/hdc.exe"
MSYS_NO_PATHCONV=1 "$HDC" -t 5KPBB25818203996 install -r \
  "D:\\Git\\mixxx\\packaging\\ohos\\entry\\build\\default\\outputs\\default\\entry-default-signed.hap"
"$HDC" -t 5KPBB25818203996 shell "aa start -a QAbility -b com.pomelo.mixxx"
```

### 耗时与坑（血泪）

- **`cmake --build` 常常先重跑 configure（约 13 分钟）再编译**，总耗时 15–20 分钟；不要误判为卡死（用 `docker exec mixxx-ohos-build ps -eo etime,comm | grep cmake` 观察）。
- **只改 ArkTS 时不必重编 C++，直接第 2 步**（约 10 秒）。
- **hvigor 的退出码会被管道里的 `tail` 掩盖**：务必 `> log 2>&1; echo $?` 后 grep `BUILD SUCCESSFUL` / `COMPILE RESULT:FAIL`，否则会把失败当成功。
- **hdc 文件传输**：`hdc file recv <设备绝对路径> <相对路径>` —— 本地参数用**相对路径**（hdc 会拼上自己的 cwd）；且需要 `MSYS_NO_PATHCONV=1`。
- **真机锁屏会导致 `aa start` 失败**（`10106102 The device screen is locked...`）。`power-shell wakeup` 只能点亮屏幕，**钥匙盘必须人工解锁**。
- **改 vcpkg overlay port 的源码后必须 bump `port-version`**（当前 portaudio 已是 `#24`），否则 vcpkg 报 "already installed" 直接跳过；必要时先 `./vcpkg remove <port> --triplet arm64-ohos --recurse`。
- Ninja 启动后再修改的源文件可能不在该次 dirty 集合中；必须等待本次构建结束，再构建一次，并确认相应 `.cpp.o` 的编译记录。仅看到链接成功不足以证明改动已入包。

---

## 4. 代码改动地图（关键文件 → 为什么）

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | OHOS 平台分支：`MIXXX_OS_OHOS` 宏、GLESv3 链接、`ohaudio`/`hilog_ndk.z` 链接、安装目录、避开 Linux/Android fallback |
| `cmake/ohos/build-qt-ohos.sh` + `qt-ohos-pins.txt` | Qt for OHOS 两阶段（host+cross）编译脚本与 commit pin；幂等应用下列两个 QPA 补丁 |
| `cmake/ohos/patches/qt-ohos-touch-window-coordinates.patch` | XComponent local → Qt global 映射及同窗同时间重复触摸过滤 |
| `cmake/ohos/patches/qt-ohos-popup-geometry.patch` | 旋转比较使用原生物理尺寸且限定主窗，防 Preferences 更新误关菜单/下拉框 |
| `cmake/ohos/ports/portaudio/**` | **OHAudio 后端**：`ohos/pa_ohos.c`（新写，~600 行）、portfile 补丁、`vcpkg.json` |
| `src/rendergraph/opengl/materialshader.cpp` | 运行期按当前 GL context 选 `.qsb` 里的 GLSL 变体 |
| `src/util/cmdlineargs.{h,cpp}` | 新增 `--media-path`（+ `getMediaPathProvided()`） |
| `src/library/browse/browsefeature.cpp` | Browse 的 Music 快捷根改用 `--media-path` |
| `src/library/library.cpp` | 启动时把 `--media-path` 目录注册进资料库 |
| `src/coreservices.cpp` | 启动器指定目录时跳过首次选目录模态框；GUI 线程自动保存配置、重试公共日志导出及 flush |
| `src/control/control.{h,cpp}` | 持久 Control 当前值快照；修正 getAllInstances 清理失效 weak pointer 时的迭代器 |
| `src/preferences/dialog/dlgpreferences.cpp` | OHOS 应用/确认后立即保存配置 |
| `src/util/logging.{h,cpp}` | 私有日志保留并同步追加公共 mixxx.log，导出前复制完整启动日志 |
| `src/sources/soundsourceffmpeg.cpp` | 开头的负帧预读反向定位失败时，重试首个可用音频包 |
| `src/mixxxmainwindow.cpp` | OHOS 主窗口 frameless，Qt 6.9+ 取消整行安全区域内容边距；皮肤重载保留窗口状态并恢复 modal 焦点 |
| `src/platform/ohos/windowadapter.{h,cpp}` | 原皮肤比例缩放/极小提示，触摸滚动，顶层 QDialog 保留布局并提供可滚动可达的内容与按钮；主题变更恢复不透明背景、关闭后恢复焦点 |
| `src/platform/ohos/mediabridge.{h,cpp}`、`mediacontroller.{h,cpp}` | NAPI 状态/命令/键盘/模态几何桥；媒体命令排队到 GUI 线程，原引擎保持实时边界 |
| `src/widget/wtracktableview.{h,cpp}` | OHOS 保持选中行可见；非编辑状态禁用输入法，防触摸列表误弹键盘 |
| `src/dialog/dlgabout.{h,cpp}`、`src/util/versionstore.cpp` | 关于原标签保留及个人信息保存；OHOS 名称旧柚Mixxx |
| `src/waveform/renderers/allshader/waveformrenderbackground.{h,cpp}` | OHOS 按原皮肤颜色绘制背景矩形，避免空碟机只有 glClear 时背景切换 |
| `src/waveform/visualplayposition.cpp` | OHOS 查询 8 条位置历史，适配一次 OHAudio host callback 生成多条 engine 快照 |
| `src/widget/wglwidgetqopengl.cpp`、`openglwindow.cpp`、`src/waveform/widgets/allshader/waveformwidget.cpp` | OHOS 由 Qt 单次重绘绘制并提交 GL 波形，避免重复绘制/缓冲交换 |
| `src/preferences/dialog/dlgprefrecord.cpp` | 录制目录默认值/纠正旧值 |
| `src/soundio/soundmanagerconfig.cpp` | `loadDefaults()` 增加 `MIXXX_OS_OHOS` 分支（选第一个有输出的 host API） |
| `packaging/ohos/build-hap.sh` | 暂存脚本（libs + `resources/resfile/res/**` + 翻译 + 符号链接实体化） |
| `packaging/ohos/entry/src/main/ets/qabilitystage/QAbilityStage.ets` | 资源目录探测、`--resource-path/--settings-path/--media-path` 注入、配置种子、Download 授权 |
| `packaging/ohos/entry/src/main/ets/qability/QAbility.ets` | 恢复全屏/自动横屏，浮窗内容避开系统标题栏；主窗与子窗键盘监听；按 native modal 几何与 revision 恢复原生层级 |
| `packaging/ohos/entry/src/main/ets/common/RuntimeLog.ets` | Ability/窗口/媒体日志保留 hilog，同时写私有与公共 ability.log，启动轮转 |
| `packaging/ohos/entry/src/main/ets/media/MixxxMediaSession.ets`、`module.json5`、NAPI 类型 | AVSession 与 AUDIO_PLAYBACK 后台任务，播放控制和权限；方向与品牌资源同步 |
| `docs/ohos/PORTING_STATUS.md` | 逐任务记录（P0.1 … P1.15，含证据） |
| `docs/ohos/HANDOVER_CODEX.md` | 本文档 |

---

## 5. 运行期设计

### 5.1 资源与设置目录
- HAP 的 `libs/` **只会解出 `.so`**，普通文件在安装时被忽略 → Mixxx 的 `res/` 必须走 `resources/resfile/res/**`，设备上是**真实文件**位于
  `/data/storage/el1/bundle/entry/resources/resfile/res`。
- `QAbilityStage` 用候选列表探测该路径（要求 `skins/` 子目录存在，因为 `resourceDir` 在 ApplicationContext 上为空且 `accessSync` 会误报 true），
  通过 `appArgs` 传 `--resource-path`；`--settings-path` 指向 `<filesDir>/.mixxx`（实测 `filesDir = /data/storage/el2/base/files`）。

### 5.2 音乐目录 = `Download/<包名>/Music`（自动创建）
- Qt OHOS 的 `QStandardPaths::MusicLocation` **未实现**（`qstandardpaths_ohos.cpp` 只对接 `OH_Environment_GetUserDownloadDir/DocumentDir/DesktopDir`，无 Music），返回空串 →
  Mixxx 曾把录制目录算成 `/Mixxx/Recordings` 并 `Permission denied`。
- 且**用户手选的目录在鸿蒙上不可读**：实测选到别的应用的 `Download/com.vintage.pomelopro/...` 时目录可枚举、文件全部 `Cannot read audio properties from inaccessible/unreadable/invalid file`。
- 正解（参考 `F:\PomeloWin`）：
  1. `bundleManager.getBundleInfoForSelfSync()` 取包名 → `file://docs/storage/Users/currentUser/Download/<包名>` 经 `fileUri.FileUri(...).path` 解析；
  2. **首次必须用 `DocumentViewPicker` + `DocumentPickerMode.DOWNLOAD` 做"授权"**（这不是选择器而是 provisioning 接口，静默返回该目录并写入持久授权）；
  3. 授权后普通 `fs` 直连即可用，逐级 `mkdirSync`。
- **三个硬约束（踩过的坑）**：
  - picker 构造必须用 **`(context, window)` 双参**，只给 context 会 `ParseWindow: not window mode` 直接参数错误；
  - context 必须是 **`UIAbilityContext`**（stage context 会 `13900042`）；
  - picker 需要窗口的 **ArkUI `uiContent`**，而它在 Qt 窗口加载完成后才存在 → **picker 不可能在 Qt 启动前跑**。因此保留 `onForeground` 后 2s/6s/12s 授权重试，启动时直接传目标 `Music` 路径。
- C++ 跳过启动器已指定路径时的音乐目录选择框；`Library` 在 GUI 线程每秒重试目录可用性，最多 30 次，延后注册成功后触发扫描。仅启动器提供 `--media-path` 时生效。
- Browse 即使已有旧快捷入口，也追加 `Music/` 的绝对路径；每次启动用 `--rescan-library` 扫描。全新无授权安装尚待独立环境复验，禁止为此清除用户数据。
- 本真机文件管理器有旧名称缓存：Download 列表显示 **`com.pomelo.mixxx`**，进入后的导航路径仍显示 **`label`**，两者是同一目录。用户放歌位置为 **我的平板 → Download → com.pomelo.mixxx（导航可能显示 label）→ Music**，无需手工创建。物理目录为 `/mnt/hmdfs/100/account/device_view/local/files/Docs/Download/com.pomelo.mixxx/Music`。

### 5.3 全屏沉浸
- `setWindowSystemBarEnable([])` 曾被拒；使用 `setWindowLayoutFullScreen(true)`、`setWindowDecorVisible(false)` 和 **`setSpecificSystemBarEnabled('status'|'navigationIndicator', false)`**。
- Qt 启动参数增加 `--full-screen`，主窗口添加 `Qt::FramelessWindowHint`；Qt 6.9+ 关闭 `WA_ContentsMarginsRespectsSafeArea`，避免 QPA 把摄像头高度扩为整行顶部留白（§6.9）。
- 创建、恢复、返回前台及固定态键盘高度归零时重新应用，并保留 2s/5s 重试；后台和销毁时取消延迟任务、销毁时注销键盘监听。
- 窗口 UIContent 尚未完成时，decor/background API 仍可能报 1300002；后续重试成功。不能将这个错误一概解释为“平板没有标题栏”。
- P1.15 监听 `windowStatusChange`，浮窗使用 `setWindowLayoutFullScreen(false)`，保留系统窗口操作条并让应用内容避开它；其它状态沿用全屏布局，延迟重试也读取当前窗口状态。
- 真机截图顶部旧留白从 **105 像素变为 0**，返回前台和键盘收起后未恢复；实体摄像头区域不会出现在系统截图里，附近按键遮挡/触控需用户现场复核。

### 5.4 原版界面与触摸

- 用户明确禁止重设计主界面；此前实验 compact/adaptive 页面已移除。`WindowAdapter` 保持原皮肤对象，通过 DPR 调整比例（180ms debounce）；最小比例 50%，再小显示放大提示，放大后恢复。
- 主 GL 界面不能外套 QScrollArea，曾触发 native ancestor/VSync fatal。普通 QDialog 可用外层 scroll 保留原布局，范围约束为主窗与屏幕交集，保留原内容/按钮及触摸滑动；关于已有 root scroll 时复用。
- QAbstractScrollArea viewport 禁止鼠标传播，防父 WWidget 抢 Touch。QScroller 使用触摸、逐像素、无 overshoot；只过滤触屏合成的左键移动，防止误触发 QDrag，真实鼠标拖拽保留。滑动释放不点击，轻触选曲仍有效。
- 极小悬浮窗提示、普通分屏、全屏设置按钮已实测；极小窗口不作为完整 DJ 操作布局承诺。
- 浮窗由系统 Scene 再缩放，输入不可直接使用原始 displayPosition；QPA 按 XComponent local 和目标 Qt 窗口映射坐标。non-client 同窗同时间事件去重，子窗尺寸更新不再误当主屏旋转关闭 popup。修补已保存为仓库内 patch，重建 Qt 必须带上。

### 5.5 媒体与关于

- manifest `auto_rotation_landscape`，原生主窗 preferredOrientation=7，支持正反横屏；本机横屏启动已验证，物理旋转尚未测试。
- C++ `MediaController` 读取 PlayerInfo/ControlObject 状态并发布 JSON；NAPI 媒体命令排队到 GUI 线程。ArkTS AVSession 显示曲目/进度，接收暂停、继续、上一首/下一首和 seek；播放时持有 AUDIO_PLAYBACK 连续后台任务，暂停释放。
- 上一首/下一首沿当前曲库 model 顺序并循环；暂停/恢复保存原播放 Deck，保持 Mixxx DJ 混音逻辑。
- 显示名旧柚Mixxx；bundle 与 Music 路径不变。关于保留原作者/授权标签，个人信息页可保存到 `[OHOSAbout]`。用户未给个人文案，保持空值，不编造身份信息。

### 5.6 设置保存

- Preferences 应用/确认后立即写入 `mixxx.cfg`。OHOS 常被系统直接终止，不能只依赖正常退出。
- 初始化完成后，GUI 线程每 2 秒读取标记为持久化的 Control 当前值，写入配置对象，比较完整配置键值；仅变化且保存成功时更新快照。进入非 Active 状态时立即执行同一保存流程并 flush 日志。音频回调无新增 IO/锁。
- 布局开关操作后不足 2 秒立即强杀仍有丢失窗口；应用/确认设置是立即保存。皮肤重载不再执行桌面的 maximize/fullscreen 切换，浮窗状态保持；modal 焦点及不透明背景恢复。
- 本轮最终恢复 `LateNight / PaleMoon`、`show_mixer=1`。不得整份回写测试前配置，避免覆盖期间新增的真实设置。

### 5.7 公共运行日志

- 文件管理器路径：**Download → com.pomelo.mixxx → logs**，与 `Music` 同级；保存 `mixxx.log`（C++/Qt/音频）和 `ability.log`（Ability/窗口/媒体）。每次启动轮转 `.1`…`.10`，单文件约 10 MB 上限。
- ArkTS 在 Qt 初始化前创建私有 ability.log；Download 授权/目录可用后复制启动记录并同步追加公共文件。C++ 先写私有 mixxx.log，GUI 每 2 秒重试公共导出，成功后同步追加/flush，保留完整启动内容。
- `--log-max-file-size 10000000 --log-flush-level info` 随启动参数注入。首次无授权路径尚待独立环境验证，不清用户数据测试。
- 复现问题后提供当前两个文件；如已重启，再提供对应 `.1` 等历史文件。应用日志不能替代系统 faultlog。

---

## 6. 已攻克的坑（症状 → 根因 → 修法）——最有价值的部分

### 6.1 `QOpenGLShader::link: Link failed because of missing fragment shader` ×170,707（日志 16MB、界面闪烁）
- 根因：`qt6_add_shaders` 生成的 `.qsb` **只含 `GLSL 100 es` 一种 GLSL 变体**（`qsb --dump` 可验证），而 `materialshader.cpp` 固定按 `QShaderKey(GlslShader, 120)` 取 → 取到空串 → 链接期报"缺 fragment shader"。
- 注意：**`QT_OPENGL_ES_2` 不会传播到 `rendergraph_gl` 目标**（`target_compile_definitions(mixxx-lib PUBLIC ...)` 不反向传播），所以编译期 `#ifdef` 方案无效。
- 修法：**运行期**用 `QOpenGLContext::currentContext()->isOpenGLES()` 与 `QShader::availableShaders()` 的 `QShaderVersion::GlslEs` 标志比对选变体。
- 验证：新日志 16KB、`missing fragment shader` 计数 **0**。

### 6.2 菜单栏消失（用户报"菜单怎么都看不到了"）
- 根因链：**没有任何输出设备** → `MixxxMainWindow::initialize()` 里 `noOutputDlg()` 是**模态** → 卡在 `m_pMenuBar->show()` **之前** → 菜单栏永远不显示（`createMenuBar()` 之后菜单是隐藏的）。
- 触发条件：`loadDefaults()` 里只有 Linux/Windows/iOS/macOS 分支，**OHOS 没有默认 host API** → `m_api` 为空 → 选不到设备 → 输出为空。
- 修法：`soundmanagerconfig.cpp` 的 `loadDefaults()` 加 `MIXXX_OS_OHOS` 分支，**选第一个真有输出设备的 host API**。
- 验证：`soundconfig.xml` 现在写成
  `<SoundConfig api="OHOS OHAudio"><SoundDevice name="Speaker"><output ... type="Master"/></SoundDevice>`。
- 另：`[Config] hide_menubar` 的自动隐藏功能**依赖 Alt 键**，平板上无法恢复；种子使用 `hide_menubar 0` / `show_menubar_hint 0`，格式根因及迁移见 §6.6。

### 6.3 `pa_ohos.c` 两处致命 bug
1. **结构体布局**：`PaOhosStream` 首成员必须是 `PaUtilStreamRepresentation`（PortAudio 会强转 `PaStream*` 并校验 `magic`）。原先首成员是 `hostApi` → `PaUtil_ValidateStreamPointer()` 返回 `paBadStreamPtr`（显示为 **"Invalid stream pointer"**）。
2. **`PaUtil_SetNoInput()` 误用**：其实现是 `bp->hostInputChannels[0][0].data = 0;`，而 `hostInputChannels` **只在有输入通道时才分配**。纯输出流调用它 → **NULL 解引用 → SIGSEGV**（崩溃线程 `OS_AudioWriteCB`，`PaUtil_SetNoInput+4`）。已加 `if (inputChannelCount > 0)` 守卫。

### 6.4 `#version 120` 导致的第二轮着色器刷屏（修复后音频通了才暴露）
- 现象：音频修好、`initialize()` 跑完（波形/转盘控件终于被创建）后，日志 2 分钟涨到 1.7 MB / 17,431 条
  `QOpenGLShader::link: Link failed because of missing fragment shader`，并伴随
  `QOpenGLShader::compile(Fragment): P0007: Language version '120' unknown, this compiler only supports up to version '320 es'`。
- 根因：`src/shaders/textureshader.cpp:19` 的 **fragment 着色器写死了 `#version 120`**（桌面 GLSL），
  但它被 `WSpinnyGLSL`（黑胶转盘）与 `WVuMeterGLSL`（VU 表）使用 —— GLES 编译器无法识别该版本指令。
  注意该着色器其余部分（`varying` / `gl_FragColor` / `texture2D`）本来就是 **GLSL ES 100 语法**，
  同文件里的顶点着色器**根本没有 `#version`**，可作旁证。
- 修法（**两处，缺一不可**）：
  1. 删掉 `#version 120` 指令（不写版本 = 桌面 GLSL 110 / ES 100，两边都合法）；
  2. `uniform float alpha;` → `uniform highp float alpha;` —— GLSL ES 的 fragment shader **float 没有默认精度**，
     实测报错 `QOpenGLShader::compile(Fragment): 3:4: S0032: no default precision defined for variable 'alpha'`。
     该文件其它变量本来就带 `highp`，此处保持一致。
- 为什么之前没发现：第一次测到"着色器报错归零"时，应用正卡在"无输出设备"模态框里、`setCentralWidget` 还没执行，
  **这些控件根本没被创建**。凡是"修好一个阻塞点后日志才变多"的情况，都要重新数一遍错误。
- 排查手法：`QOpenGLShader::compile(...)` 那一行**紧跟在** Qt 打印的 `*** Problematic Fragment shader source code ***` 之前，
  必须先 `grep 'compile('` 拿到编译器原文，再看源码定位。
- **复验结果**：`missing fragment shader` **0** 条、`QOpenGLShader::compile` 报错 **0** 条、日志 55 秒 **16 KB**（修复前 1.7 MB / 17,431 条）。
  可见效果：**黑胶转盘（`WSpinnyGLSL`）与顶栏 Buffer%/VU 表（`WVuMeterGLSL`）从空白变为正常绘制** ——
  即"界面看起来没问题"不等于这些控件真的工作了，要用日志计数确认。

### 6.5 其它
- `libqohos.so` **只能存在一份**（`libs/` 根目录）。同时存在 `platforms/libqohos.so` 会产生两套静态 peer 注册表 → Qt abort（`makeWindowProxyDataForExistingMainWindowInJsThread`）。
- HAP 里 `libs/` 只有 `.so` 被解出（见 §5.1）。
- vcpkg 构建静默 no-op：改 overlay 源码必须 bump `port-version`。

### 6.6 菜单提示与缩放种子一直不生效
- `ConfigObject::parse()` 只接受 **`键 空格 值`**，旧种子 `键=值` 全部被忽略；bool 读取调用 `toInt()`，只接受 **0/1**。
- `QAbilityStage` 改为 `ScaleFactor 0.75`、`hide_menubar 0`、`show_menubar_hint 0`；仅迁移 `[Config]` 中旧等号格式及 true/false，保留用户有效值，读取失败不重写文件。
- 实际 ArkTS 方法的临时回归脚本覆盖新配置、旧配置、用户值、分组隔离、重复项、读取失败和幂等；真机菜单提示消失，缩放确实生效。

### 6.7 WAV 加载后 EQ 退出：PortAudio 流信息采样率 0
- 崩溃线程 `OS_AudioWriteCB`，`fid_design` 报频率超出 Nyquist 范围，最终 `exit(1)`；并非 DSP 参数本身配置错误。
- `PaUtil_InitializeStreamRepresentation()` 将 `streamInfo.sampleRate` 初始化为 0；OHOS 后端未填，Mixxx 从 `Pa_GetStreamInfo()` 得到 0 后广播给引擎。
- #24 在 renderer 创建后查询实际采样率、回调帧数与延迟，再初始化 buffer processor，填 `streamInfo.structVersion/sampleRate/outputLatency`。真机显示实际 **48000 Hz**，WAV 播放不再崩溃。
- 本机报告 **4458 帧/host buffer**，不能据此声称低延迟 DJ 已达标；延迟及 xrun 仍需实测。

### 6.8 MP3 可扫描但不能加载：负帧预读定位
- 失败文件仍存在，tags/封面可读取。`av_seek_frame()` 返回 -1 被格式化成 `Operation not permitted`，这里是索引定位失败，不是文件权限结论。
- 对不带有效开头定位信息的 MP3，Mixxx 的预读目标位于首个索引之前，FFmpeg generic backward seek 返回 -1。
- `adjustCurrentPosition()` 仅在定位失败且 `seekIndex < 0` 时，对原目标重试 flags=0；可取到首个可用包。中段定位保持原来的 backward seek。
- 使用同源 Windows FFmpeg 对真机失败歌曲复现：backward=-1，forward=0，恢复后的首包与顺序读取的 PTS/字节完全相同，中段定位及回到开头成功。最终真机复验见 P1.11。

### 6.9 系统栏已隐藏但顶部仍有 105 像素空白
- `WindowRect` 和 ArkUI XComponent 已是 `[0,0,2560,1600]`，`ContainerModalTitleRow` 高度为 0；单纯重试隐藏 decor 或切换全屏未消除空白。
- 系统避让区的四个矩形均为 0，但 `display.getCutoutInfo()` 返回 `left=30, top=25, width=80, height=80`。pinned Qt 的 OHOS QPA 按最近窗口边缘折算 cutout，得到顶部边距 **25+80=105**；QWidget 默认尊重该边距，所以整个内容区内缩。
- OHOS 主窗口取消自动安全区域内容边距并设置 frameless，启动传 `--full-screen`；P1.14 普通对话框也在显示前设置 frameless，并保留原布局、增加滚动与屏幕边界适配。
- 中间包仅更新 ArkTS/启动参数时仍留白，最终包含 `mixxxmainwindow.cpp` 的包才消除留白。构建、前台恢复、菜单和键盘复验见 P1.12。

### 6.10 暂停时滚动波形旧画面重现与位置有效性切换
- `VisualPlayPosition` 原先仅检查最近 3 条快照。OHAudio 4458 帧 host buffer 大于 Mixxx 本机 1024 帧 engine buffer，一次回调可连续写入超过 3 条尚未到达 DAC 的位置；此前已有有效位置却因历史查询过短返回 false。
- 上层收到 false 会把位置设为 -1，AllShader 隐藏信号/CUE 子树；下一次有效查询又恢复信号。用实际 C++ 类模拟批量回调与 10ms/80ms 的刷新，固定暂停位置仍出现 20/40 次失效。
- OHOS 扩展为 8 条历史查询，使用现有 16 条环形缓冲，无新增音频线程分配或锁；回归变为 0/40，音轨失效语义仍通过。
- OHOS 的定时交换改为 `QOpenGLWindow::update()`，由 Qt `paintGL()` 和自动 swap 完成单次提交；AllShader 定时 render 和 resize 不再额外绘制/交换。其他平台保留现有路径。
- 双 Deck 暂停后旧包 16 张截图中抓到 1 张异常：主界面时间仍为 2:11.29，顶部波形却返回开头预读画面，CUE/Intro 标记也变了，支持旧 GL 缓冲被重复提交的判断。新包相近场景连续 40 张截图逐像素一致；播放及隐藏/重显波形、前台恢复复验通过。
- 普通 HDC 截图采样率不足以证明所有物理显示帧正常；host 位置回归、真机渲染采样与用户屏幕观感分别报告。音频的 `no transport` 时间同步警告仍存在，最终状态与限制见 P1.13。

---

### 6.11 设置/弹窗显示不全与键盘遮挡

- 顶层固定最小尺寸及窗口 decor 的 frame/client 坐标会把原底部按钮推到边界外。通用适配解除 root 最小约束，外层滚动保留布局，限制在主窗/屏幕交集，沿原 palette 设置不透明背景。
- frameless 必须在 Polish/显示前准备。显示后改 window flag 会 hide/show，Qt `QDialog::exec()` 会被 hide 终止；颜色编辑器曾因此立即销毁。host 实际 exec 保持直到显式关闭，真机编辑器已复验。
- Qt OHOS 的 keyboardRectangle 固定空；监听主窗/子窗 `keyboardHeightChange`，取最大高度，子窗重叠值补主窗和子窗底边之差（本机 724 → 798）。只改变 root scroll viewport，保持 dialog 原生 geometry，防反馈振荡。
- 高度归零延迟 250ms，避免按下按钮先收键盘、布局在释放前移动，导致只收键盘而未点击。全屏/分屏 Preferences Cancel 一次关闭通过。
- 弹窗检查范围、输入法下音轨属性需要滑动或收键盘再操作底部按钮的限制见 P1.14；不要声称所有窗口的 footer 都在 IME 打开时固定可见。

### 6.12 模态层级与空碟机背景

- 点击颜色编辑器外侧会把父 Preferences 原生子窗置顶。Qt OHOS `raise()` 未实现，activateWindow 不能改原生 Z-order；native 发布 modal 平台坐标与 revision，经 `readWindowState()` 到 ArkTS 匹配子窗，调用 `raiseToAppTop()`。
- 只在 revision 变化时恢复，跳过 Qt active popup；单独捕获原生 raise 错误，后台注销扫描。最终窗口桥包点击外侧后编辑器仍可见，Discard 可用。
- 收尾 8 张暂停截图抓到 2 张空碟机区域变黑，有歌曲的波形与 CUE 不变。OHOS 背景在原 glClear 外增加原皮肤色矩形；最后包空双 Deck 16 张、加载并 Home 返回后暂停 16 张上下区域各自一致，播放 4 张正常更新。GPU 驱动根因未做逐帧跟踪。

### 6.13 主题重启恢复默认

- 修前 Classic 应用可见但磁盘无 Scheme，强杀后回 PaleMoon；Mixxx 原持久 Control 在销毁时才写配置，系统直接结束进程会跳过。
- 应用/确认立即保存，加 GUI 周期/后台快照。Classic 落盘并通过强杀重启/覆盖安装，Mixer 隐藏也重启保持；收尾恢复 PaleMoon 和 Mixer 显示，最终生产包再次强杀重启通过。

### 6.14 浮窗点击失效及 popup 立即关闭

- 四个原因：系统 Scene 缩放令 display/local 坐标错位；沉浸内容被浮窗操作条覆盖；同次 touch 经 XComponent 与 non-client 两路重复送出；Preferences 矩形变化被误判为屏幕旋转。
- 最后一项有调用栈/close 来源证据：`DlgPreferencesDlgWindow`，reason=6/UNDEFINED；Qt 原比较 logical geometry 与 native rect，DPR 下总不同。改为 native size 比较，并只对 MainWindow 判断旋转。DRAG_START 的关闭逻辑保留。
- 最终生产包首次点击菜单/下拉框保持展开、触摸菜单进入设置、换主题 Apply/Cancel 通过。临时 NC/WSI、popup 调用栈与 close 来源日志已移除；只保留少量 DOWN/窗口诊断。
- 同窗同 timestamp 去重已在本设备顺序验证，其它设备事件重排尚未验证。原生子窗视觉位置不总贴合父窗左上，当前操作通过，不因此重设计界面。

---

## 7. 当前验证与待办

- **P1.16 最新包与测试入口**：signed HAP SHA256 `d437606958893f1aeab6c188cbc049dcb97f326919647e8dbead2dfeed5a9b4f`；`libmixxx.so` native/staged=`5300ddea4d74a8944f6911ab56ec42a1951e15a484a4aa27a10820f6eaaf3202`，stripped/packed=`f358a533f4b86f2e6143b27f024e31dd7b6758ef9d71c4fca5bf0789bb8b4ad3`。三库及 3246 资源校验见 `gesture-eq-artifact-hashes.json`。下列 P1.15 收尾配置、PID 与包哈希为历史状态，不得恢复覆盖用户新设置。
- **P1.16 旋钮与拖歌**：两次 crash 指向 `QOhosWindowProxy::setCustomCursor`，Invalid parameter 导致 QtMainThread SIGABRT。OHOS 跳过透明自定义光标/光标回位，调音算法保持原版；host 100 次调节/释放/复位及 wheel 通过。曲库普通滑动滚动、单指长按约 700ms 启动原拖放；host swipe/hold 通过，平板日志已记录长按及 Deck 收到 URI。全部六个 H/M/L 的真机操作尚未可靠完成，不能把用户切到其它应用后的注入计为通过。
- **P1.16 手机进度**：无线目标 `192.168.180.76:44559`，机型 VYG-AL00，系统 OpenHarmony-7.0.0.105。`phone-install.log` 显示 install bundle successfully；`phone-start.log` / `phone-pretest-start.log` 返回 10106102，手机锁屏，尚无主界面截图及布局通过结果。已请求用户解锁。只操作手机；用户正在使用平板，不再向平板注入输入。
- **最新用户平板设置**：用户自行改为 Deere (64 Samplers)、ScaleFactor=0.75、Mixer=1、Scheme 空；曲库 6 首、Browse 5 首是本轮观测，不恢复 P1.15 的 LateNight/PaleMoon 或 8 首基线。只读备份 `gesture-eq-user-config-before.cfg`。
- **已验证**：菜单提示和缩放修复、Music 自动创建/扫描/浏览/搜索、5 首用户 MP3 的元数据扫描、WAV 与《当》的加载分析播放、暂停和定位；播放期间进程持续存活。
- **顶部空间复验**：原生与 HAP 构建、签名覆盖安装通过；顶部留白由 105 像素降为 0，菜单可打开，前台恢复与键盘收起后沉浸布局保持。
- **窗口/触摸复验**：P1.14 保留原版界面；14 设置分类、关于、音轨属性、普通输入框、颜色编辑器、帮助菜单、分屏设置和极小提示已验证；host 覆盖纵向触摸、原按钮可达、键盘收起 press/release 和模态 exec。11 个 `.ui` 根 QDialog 均有 root layout，另核对 3 个 C++ 构建窗口；未逐一操作全部罕见弹窗。
- **原版主面板**：四碟机、采样器、MIC/AUX 展开与恢复截图通过。普通分屏 1271×1600 比例 0.53449，极小悬浮 815×1448 提示放大，最大化恢复。
- **媒体复验**：P1.14 本机后台播放进度持续增加约 3 分 40 秒；胶囊显示当前曲目，系统暂停/继续/上一首/下一首通过。P1.15 浮窗播放/暂停/CUE 操作通过。
- **波形复验**：P1.13 历史双 Deck 40 张稳定；P1.14 最终背景包空双 Deck 16 张和加载/前台恢复后的 16 张稳定，播放 4 张更新。测试已停播，《当》回开头/CUE，crossfader 中央，无弹窗/键盘，曲库保留 8 首。
- **P1.15 保存/浮窗/日志**：主题应用、持久布局开关、强杀重启/覆盖安装保留设置；移动/缩小浮窗的列表、播放/暂停/CUE、菜单和分类可用。最终生产包首次颜色下拉展开、PaleMoon 应用/取消、菜单触摸进入设置再次通过，无误弹键盘。公共两份日志非空且轮转更新。
- **最终安装**：收尾 PID **32768**；signed HAP SHA256 **`4f6ece8ac039b36348c69455aff890efff25ec648f5327ccea2cbf0d08e9c703`**，位于 `packaging/ohos/entry/build/default/outputs/default/entry-default-signed.hap`。`libmixxx.so` native/staged=`b2e0df4965c653cdd8339c6c19de8e6c7b1f950b14e7b9fe54a50e0cda4f0ed9`；`libqohos.so` native/staged=`b27bbd6b312783eee467f8df12cc578fe97ae986d760524e03dc5f3a6133ffc0`。三库 native/staged、stripped/packed 及 3246 资源一致，见 `persistence-artifact-hashes.json`。此前 P1.11–P1.14 和 P1.15 中间包均为历史产物。
- **最终证据**：以 `PORTING_STATUS.md` P1.15 为准；日志/截图在 `docs/ohos/logs/20260928-continuation/`，最终 `persistence-wrap-final*`，主题/菜单 `persistence-final-floating-*`、`persistence-wrap-float-*`；构建/安装 `persistence-popup-final-qt-build.log`、`persistence-final-native-build.log`、`persistence-final-hap-build.log`、`persistence-final-install.log`。最终强杀重启后恢复全屏，《当》开头/CUE 0:00.01、全部停止、crossfader 中央、无弹窗/键盘；LateNight/PaleMoon、Mixer=1，仍保留 8 首。
- **用户听音已确认**：2026-09-28 用户反馈声音正常、可以播放；音质、低延迟和真实 DJ 设置另需验证。
- **仍需实体摄像头附近的遮挡/触控复核**：HDC 截图与注入触控不能验证屏幕物理挖孔对按键的影响。
- **仍需手机实体与物理正反横屏旋转**：本机为平板；方向参数 7 已生效，设备翻转与手机触控尚待用户现场测试。个人文案可在关于填写，目前为空。
- **退出生命周期待追踪**：11:32 文件菜单退出后，Qt QPA 报 `can't create window without the default Ability instance` fatal；播放期间未出现该错误，后续包可正常启动。
- **仍需干净安装复验**：无持久授权场景的首次启动等待机制已实现，但本轮未卸载或清除用户数据来验证。
- **已清理**：删除整份 `mixxx.log` 转存至 hilog 的 `dumpMixxxDiag`；保留少量目录授权诊断；补默认键盘映射及 effects/chains 目录。
- 工作区有既有未提交改动和 IDE 文件变化，保留原状；提交、push、PR/Issue 及真实 DJ 测试由人执行。

---

## 8. 诊断手册

| 想看的 | 命令/位置 |
|---|---|
| 应用自己的日志 | `hdc shell "tail -c 3000 /data/app/el2/100/base/com.pomelo.mixxx/files/.mixxx/mixxx.log"` |
| 用户可导出的运行日志 | 文件管理器 `Download/com.pomelo.mixxx/logs/{mixxx.log,ability.log}`；重启前记录在 `.1`…`.10` |
| 公共日志物理目录 | `/mnt/hmdfs/100/account/device_view/local/files/Docs/Download/com.pomelo.mixxx/logs`；`hdc -t 5KPBB25818203996 file recv <远程文件> <Windows绝对路径>` |
| 过滤着色器刷屏 | `hdc shell "grep -v 'missing fragment shader' <log>"`（曾占日志 99.6%） |
| 应用 hilog（我们打的） | `hdc shell "hilog -x -T mixxxohos"` |
| PortAudio/OHAudio 日志 | `hdc shell "hilog -x -T PortAudioOHOS"` |
| 崩溃文件 | `/data/log/faultlog/faultlogger/cppcrash-com.pomelo.mixxx-*.log`（shell 不能 `ls`，但**已知文件名可直接 `hdc file recv`**） |
| 设备截图 | `hdc -t 5KPBB25818203996 shell "snapshot_display -f /data/local/tmp/x.jpeg"` + `hdc -t 5KPBB25818203996 file recv /data/local/tmp/x.jpeg "D:\Git\mixxx\docs\ohos\logs\x.jpeg"`（Windows 使用反斜杠绝对路径） |
| 清 hilog | `hdc shell "hilog -r"` |
| 配置/库文件 | `<filesDir>/.mixxx/{mixxx.cfg,soundconfig.xml,mixxxdb.sqlite,mixxx.log}` |

---

## 9. 纪律

- 上游 `AGENTS.md`：**AI 不得自主创建/更新 Issue、PR，不得回复 PR 评论**；所有提交与沟通必须由人完成。AI 生成的文本需在首尾注明。
- 本项目基线：`bcfb7956315e64d383c570dcb9e79a06a883e335`；任何上游行为改动都要在 `docs/ohos/PORTING_STATUS.md` 追加 `## Task P?.?` 小节（含命令、结果、证据）。
- Android 只作参考，**不得伪造 `Q_OS_ANDROID`**。

> 本文档结束（由 AI Agent 自动生成）。End of autonomously AI-generated document.

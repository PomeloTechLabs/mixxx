# Qt for HarmonyOS (OHOS) 源码编译手册

> 结论（2026-09-26 调研）：Qt **没有** OHOS 预编译包（6.9/6.10/6.11 支持平台页均无 OpenHarmony；
> `doc.qt.io` 无对应页面）。OHOS 支持只存在于 **qtbase/qtdeclarative 等 dev 分支**
> （`mkspecs/ohos-clang`、`src/plugins/platforms/ohos` QPA、`cmake/QtHarmonyOSHelpers.cmake`），
> 因此必须源码编译并 pin dev commit。OpenHarmony SIG 的 gitee/qt 是 Qt 5.15 且已归档，不适用。

## 模块与产物

| 阶段 | 模块 | 产物 | 用途 |
|---|---|---|---|
| host (Linux) | qtbase, qtshadertools, qtdeclarative | moc/rcc、`qsb`、`qmlcachegen` 等 | 交叉编译时调用的宿主工具 |
| cross (OHOS) | qtbase, qtshadertools, qtsvg, qtimageformats, qtdeclarative | Qt6 libs + QPA `qohos` 插件 + sqlite 插件 | Mixxx OHOS 链接的 Qt |

mixxx 不使用 LinguistTools（无 lrelease/lupdate 依赖），故 **不需要 qttools**。

## 版本 Pin

`cmake/ohos/qt-ohos-pins.txt`（2026-09-26 dev HEAD）：

| 模块 | SHA |
|---|---|
| qtbase | `2be39d2fa91aa06df766873aa63458be5923290e` |
| qtdeclarative | `1171fd3c023c6b777ed567d32a159c14ba22b5e5` |
| qtshadertools | `0dbd8c22599c1ca76ec1fac323e6d6cc4ebd20c3` |
| qtsvg | `effc94fe42b41462a7ae961ad3ebd26c6bdcd0a1` |
| qtimageformats | `13878ad93b96aa186cdb0cdfb9125642718dc79e` |

## 步骤

### 1) Host 侧：取源码（Windows Git Bash）

```bash
cd /d/Git
bash /d/Git/mixxx/cmake/ohos/checkout-qt-pins.sh /d/Git/qt6
# 网络直连可用时可: MIXXX_GIT_PROXY= 置空跳过代理（脚本自动回退直连）
```

### 2) 创建持久化 Docker volume

```bash
docker volume create mixxx-ohos-qt-build   # 构建树（增量重跑）
docker volume create mixxx-ohos-qt-out     # 安装树 (qt-host / qt-ohos)
```

### 3) 启动编译（后台）

```bash
mkdir -p /d/Git/mixxx/docs/ohos/logs
docker run --rm -i \
  -e STAGE=all \
  --mount "type=bind,src=D:/Git/mixxx,dst=/data/src/mixxx" \
  --mount "type=bind,src=D:/Git/qt6,dst=/data/qt6" \
  --mount "type=bind,src=F:/command-line-tools,dst=/apps/harmony" \
  --mount "type=volume,src=mixxx-ohos-qt-build,dst=/data/build" \
  --mount "type=volume,src=mixxx-ohos-qt-out,dst=/data/out" \
  winehua-dev bash /data/src/mixxx/cmake/ohos/build-qt-ohos.sh \
  > /d/Git/mixxx/docs/ohos/logs/qt-ohos-build.log 2>&1
```

- `STAGE=host|cross|all` 可分段执行；失败修复后重跑同命令即增量继续。
- `JOBS=$(nproc)` 默认并行度；容器内核数由 Docker VM 决定。
- 预计总时长 2~4 小时（host ≈ 40-60min，cross ≈ 1-2h，视 CPU）。

### 4) 产物

```
qt-host : <volume:out>/qt-host      # Linux 宿主工具
qt-ohos : <volume:out>/qt-ohos      # OHOS 交叉 Qt（CMake 包在此）
```

后续 Mixxx OHOS configure：

```bash
cmake -S . -B build-ohos -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$OHOS_SDK/native/build/cmake/ohos.toolchain.cmake \
  -DOHOS_ARCH=arm64-v8a \
  -DCMAKE_PREFIX_PATH=<out>/qt-ohos \
  -DQT_HOST_PATH=<out>/qt-host \
  -DQML=ON -DHID=OFF -DBULK=OFF -DPORTMIDI=OFF -DBROADCAST=OFF -DVINYLCONTROL=OFF
```

（vcpkg 依赖接入后还需 `-DCMAKE_TOOLCHAIN_FILE` 链 vcpkg 或 `-DCMAKE_FIND_ROOT_PATH` 组合，见 `docs/ohos/DEPENDENCY_MATRIX.md`。）

## 已知风险 / 备忘

1. **fontconfig**：OHOS NDK sysroot 无 fontconfig；Qt dev 的 `FindWrapFontconfig` 仅支持系统查找。
   ohos QPA 链接 `WrapFontconfig::WrapFontconfig`。若 cross qtbase configure 报错，回退方案：
   用 vcpkg `arm64-ohos` 编 fontconfig/expat/freetype，configure 时加
   `-DCMAKE_FIND_ROOT_PATH=<vcpkg installed>/<triplet>`。
2. **NodeAddonApi**：ohos QPA 依赖 `NodeAddonApi::NodeAddonApi`（qtbase `cmake/FindNodeAddonApi.cmake`），
   预期由 Qt 自带 fetch/捆绑逻辑解决；如失败按错误提示补。
3. **host 无 GL 时**：脚本先尝试 `apt-get install libgl1-mesa-dev`（经 `host.docker.internal:8080` 代理）；
   不可用则自动降级为 tools-only（`-DFEATURE_quick=OFF` 等），host 工具仍够用。
4. **CMake 4.x deprecation**：ohos.toolchain.cmake 触发 cmake_minimum_required 弃用警告，可忽略。
5. **API level**：SDK 为 API 24（6.1.1）；Qt dev 对 minApi 的要求以 configure 输出为准，出现
   `OHOS_PLATFORM_LEVEL` 相关错误时显式传 `-DOHOS_PLATFORM_LEVEL=<n>`。
6. Qt dev 分支每日变化：升级 pin 需完整回归（重编 host+cross + Mixxx configure），并在
   `docs/ohos/PORTING_STATUS.md` 记录新 SHA。

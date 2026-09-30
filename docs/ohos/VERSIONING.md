# 旧柚Mixxx 版本与上游同步

> 本文由 AI Agent 自动生成，供用户审阅。This document was written autonomously by an AI Agent.

应用版本格式：`上游主版本.次版本.补丁版本.旧柚迭代号[-上游预发布标记]`。

当前上游为 `2.7.0-alpha`，旧柚迭代号为 `4`，应用显示 **`2.7.0.4-alpha`**（按官方生命周期释放播放长时任务，暂停十分钟宽限后于系统允许执行时结束媒体会话/收起实况窗；短暂暂停复用，闲置结束后继续播放重建，切歌加载过渡保护，保留歌曲封面及智慧屏声明）。首个版本为 `2.7.0.1-alpha`，后续本地修补递增为 `2.7.0.5-alpha`；同步上游 `2.8.0` 后显示 `2.8.0.1`（可在该次同步时将迭代号重置为 1）。`P1.17` 等仅作开发任务编号，后续分发包文件名采用应用版本。

## 唯一来源与生成入口

- 上游版本和 alpha/beta/rc 标记沿用仓库根 `CMakeLists.txt` 的 `project(mixxx VERSION ...)` 与 `MIXXX_VERSION_PRERELEASE`，不复制前三段到独立配置。同步上游时先解决 Git 合并冲突，使用同步后源码的实际版本。
- 旧柚仅维护 `packaging/ohos/version.json` 的整数 `revision`（0–999）。一次正式分发修改递增一次，不在每次本地构建时自动递增。
- CMake 的 `cmake/ohos/ConfigureVersion.cmake` 自动生成 native 显示版本头；修改 revision 会触发重新配置。应用关于、Qt applicationVersion、QML 显示和启动日志使用同一显示版本。
- 根 `packaging/ohos/hvigorfile.ts` 在打包时调用 `version.cjs`，自动刷新 `AppScope/app.json5` 的 `versionName` 和 `versionCode`；命令行与 DevEco 构建均经过该入口。可先运行 `node packaging/ohos/version.cjs` 查看/同步。
- 原 `VersionStore::version()` 和配置升级所用版本仍为上游版本；数据库升级使用原 SchemaManager。自有迭代不会改变上游的配置版本判断。

## 覆盖安装排序

`versionCode = major × 100000000 + minor × 1000000 + patch × 10000 + stage × 1000 + revision`。

stage：alpha=0、beta=1、rc=2、正式版=9。minor/patch 为 0–99，major 为 0–20；超出范围或遇到未知预发布标记会停止打包，需明确更新规则。当前 code 为 `207000004`，大于历史 `1000000`。自有迭代增加、预发布阶段升级和上游补丁升级均保持覆盖安装顺序；向旧上游降级仍可能被系统拒绝。

## 同步与分发步骤

1. 人工同步 `upstream` 的代码并解决冲突；保留 OHOS 平台分支。版本来源是本次实际编译的源码，不自动追逐最新 tag。
2. 递增旧柚 revision；进入新上游版本时可重置。正常生成 CMake 构建文件、编译原生库。
3. 用 `build-hap.sh` 暂存当前原生库和资源，DevEco/Hvigor 打包。修改上游或 revision 后必须重新编译、暂存原生库，不能仅改 manifest。
4. 核验关于/启动日志与 HAP 内 manifest 的显示版本一致，覆盖安装保留用户数据。分发名如 `PomeloMixxx-2.7.0.1-alpha-unsigned.hap`。

回归：`node --test packaging/ohos/version.test.cjs`，涵盖本地迭代、同步后自动跟随、manifest 字段和 alpha→beta→rc→正式版→下个 patch 的升级顺序。不会自动 commit/push 或创建上游 PR。

> 本文结束（由 AI Agent 自动生成）。End of autonomously AI-generated document.

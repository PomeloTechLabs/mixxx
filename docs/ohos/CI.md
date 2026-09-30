# OHOS CI 与分支策略

> 本文档由 AI 助手自主生成。

## 分支模型（推荐并已实施）

| 分支 | 角色 | 规则 |
|---|---|---|
| `main` | **上游镜像**，不落任何 port 提交 | 同步：`git fetch upstream && git push origin upstream/main:main`；将来给上游提 PR 的 feature 分支从这里切 |
| `ohos-port` | **fork 主线**（默认分支），全部 OHOS 工作落这里 | 定期 `git merge main` 吸收上游；CI 在此分支跑构建检查 |
| `rc-N` / `dev-N` | **发布标签**，从 ohos-port 打出 | 触发 CI 构建 + GitHub Release（未签名 HAP）；`rc-*` 占 Latest 徽章，`dev-*` 不占 |

不建议把 port 提合并进 `main`：一旦混入，后续每次同步 upstream 都是冲突地狱。
稳定版不需要单独分支——直接在 ohos-port 上打标签即可；若将来需要_hotfix，
再从某个 tag 切 `release/x.y` 分支。

`feature/ohos-port` 保留未删（历史引用），确认 ohos-port 工作正常后可自行删除。

## CI 组成

| 文件 | 作用 |
|---|---|
| `.github/workflows/ohos-build.yml` | push `ohos-port` → 构建检查；push `rc-*`/`dev-*` 标签或手动 dispatch → 构建 + 发布 GitHub Release（未签名 HAP，命名 `PomeloMixxx-<tag>-arm64-v8a-unsigned.hap`） |
| `.github/workflows/ohos-buildenv.yml` | 手动触发：构建/更新 GHCR 工具链镜像 |
| `packaging/ohos/ci/Dockerfile` | buildenv 镜像配方（OHOS CLI 工具 + Qt for OHOS + vcpkg arm64-ohos） |
| `packaging/ohos/ci/verify_release.py` | 版本闸门：tag 号 ↔ `version.json` revision ↔ `app.json5` ↔ HAP 元数据一致，release 包 `debug=false`，无私密文件 |

CI 里的 HAP 是**未签名**的（覆写空 `signingConfigs` 的 build-profile 跳过 SignHap）；
上架/装机签名仍走本地 `sign_release.py` 流程，密钥不进 CI。

## 一次性引导（必须先做）

1. 准备 command-line-tools zip（华为开发者官网下载 linux-x64 版），放到
   `packaging/ohos/ci/command-line-tools.zip`（已 git-ignore），或取其直链 URL。
2. 本地构建镜像并推到 GHCR（首次 2–4 小时，Qt host+cross 是大头）：
   ```bash
   docker build -f packaging/ohos/ci/Dockerfile -t buildenv-ohos .
   docker tag buildenv-ohos ghcr.io/pomelotechlabs/mixxx/buildenv-ohos:latest
   docker login ghcr.io   # 用 GitHub 用户名 + write:packages 的 PAT
   docker push ghcr.io/pomelotechlabs/mixxx/buildenv-ohos:latest
   ```
   也可用 Actions 手动跑 `OHOS buildenv image`（URL 走 secret
   `OHOS_CMDLINE_TOOLS_URL`）；4 核 runner 可能逼近 6 小时上限，本地大机器更快。
3. 镜像就位后，`ohos-build.yml` 即可用。

## 日常发版流程

```bash
# 1) bump revision（version.cjs 会同步 app.json5），提交
cd packaging/ohos && node version.cjs && cd ../..
git add packaging/ohos/version.json packaging/ohos/AppScope/app.json5
git commit -m "ohos: bump revision to N"
git push origin ohos-port
# 2) 打对应编号的 tag（rc-5 ⇔ 2.7.0.5）并推送
git tag rc-5 && git push origin rc-5
```

CI 校验 tag 号与 revision 一致后才出包；Release 附带 `ci-source.json` /
`ci-artifact.json`（版本、哈希、来源 commit）。需要正式上架时，再在本地按
`docs/ohos/logs/<date>-release/sign_release.py` 流程对同一 tag 的产物做发布签名。

## 已知边界

- 镜像内 Qt pins 变更（`qt-ohos-pins.txt`）或 vcpkg commit 变更需要重跑
  buildenv 镜像；Dockerfile 顶部 `ARG VCPKG_COMMIT` 与 vcpkg volume 的
  `VCPKG_COMMIT.txt` 保持一致。
- 托管 runner 冷构建约 1.5–3 小时（无 ccache，后续可加）。
- GHCR 镜像名必须全小写：`ghcr.io/pomelotechlabs/mixxx/buildenv-ohos`。

> 本文档由 AI 助手自主生成。

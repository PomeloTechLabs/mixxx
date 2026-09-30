# 系统媒体会话与闲置释放策略

> 本文由 AI Agent 自动生成，供用户审阅。This document was written autonomously by an AI Agent.

## 官方依据

- [OpenHarmony 后台播放指南](https://github.com/openharmony/docs/blob/master/zh-cn/application-dev/media/avsession/avsession-background-scene.md)：播放期间保持 AVSession 实例；暂停/停止时主动取消播放长时任务，恢复播放时重新申请；进程结束或完全退出播放业务时再释放 AVSession，避免频繁创建和释放。
- [长时任务指南](https://developer.huawei.com/consumer/cn/doc/harmonyos-guides/continuous-task)：未执行相应业务时，系统可能冻结应用。释放任务与真实音频状态应一致。
- [AVSession API](https://github.com/openharmony/docs/blob/master/zh-cn/application-dev/reference/apis-avsession-kit/arkts-apis-avsession-AVSession.md)：`deactivate()` 禁用会话功能，可通过 `activate()` 恢复；`destroy()` 使会话完全失效。

上述鸿蒙指南没有统一规定“暂停后 N 分钟收起实况窗”。[Android Media3 MediaSessionService](https://github.com/androidx/media/blob/release/libraries/session/src/main/java/androidx/media3/session/MediaSessionService.java) 的公开默认前台服务宽限期为 600000 ms；旧柚Mixxx采用同样的十分钟作为本项目的闲置宽限参数。该参考是 Android 前台服务策略，不能表述为鸿蒙实况窗官方超时标准。

## 当前行为

| 情况 | 行为 |
|---|---|
| 初次启动或只加载歌曲，尚未播放 | 不激活媒体会话，不显示实况窗 |
| 任一 Deck 播放 | 保持有效 AVSession、歌曲信息/封面及 AUDIO_PLAYBACK 长时任务 |
| 切歌正在异步加载 | 保留当前会话/封面，不把加载间隙误报为卸载；最长等待十五秒，失败/超时释放 |
| 全部暂停 | 及时取消播放长时任务，上报一次暂停状态，保留恢复播放控制 |
| 连续暂停十分钟 | 下一次获得执行机会时，上报 STOP、失活并销毁本次系统媒体会话，释放歌曲 PixelMap |
| 宽限期内继续播放 | 清除暂停计时，重新申请播放长时任务 |
| 闲置结束后继续播放 | 创建并激活新会话，重新发布当前歌曲、封面与播放进度 |
| 无歌曲、native 不再可用或系统明确 stop | 销毁会话；保留 Qt 的暂停/位置行为 |
| Ability 销毁 | 等待异步操作，关闭会话与后台任务，释放图片 |

计时基于连续未播放的时间，不被轮询、暂停定位或封面更新重置。暂停状态只有位置、曲目或时长确实变化时才重新上报。Qt 界面、歌曲、CUE 和设置保持原逻辑。

Native 通过各播放器的待加载曲目与 PlayerInfo 的已加载曲目区分加载中和真正空载，覆盖系统上一首/下一首与界面换歌。加载过渡期间只保留已有会话，不在初次加载时创建会话。已暂停的加载不会重新启动后台任务，也不延长十分钟计时。系统继续播放时先申请后台任务，给已接受但尚在 Qt 队列中的命令最多五秒完成；届时仍未播放则取消任务，避免先申请又立即被旧暂停快照取消。

普通短暂暂停继续使用同一个 AVSession。十分钟闲置后视为结束本次系统播放业务；本机实测仅失活后重新激活原 AVSession 虽可继续音频，却未重新出现实况窗；销毁后重新创建可以恢复实况窗。因此长时间闲置结束时完整释放，下一次播放再创建，避免每次暂停都重建，也不长期持有失效的系统显示状态。

## 后台调度与验证边界

暂停后释放长时任务，应用可能被冻结，ArkTS 定时器无法保证准点执行。会话清理在系统允许的下一次执行或恢复前台时完成；不为等待宽限期保留无业务的播放长时任务，也不循环申请短时任务。前一版“两分钟”试验在本机实际于暂停十分钟后得到调度，已记录在 `logs/20260929-idle-media/pad-hilog-extended.log`，不能称为两分钟后台准点通过。

[短时任务指南](https://github.com/openharmony/docs/blob/master/zh-cn/application-dev/task-management/transient-task.md) 规定申请时机为前台或 onBackground，存在单日配额，适合状态保存等有限工作。本实现不使用短时任务维持暂停等待。

回归入口 `packaging/ohos/media-session.test.cjs` 转译实际 ArkTS 源码，以模拟时间验证宽限边界、短暂暂停复用、闲置后重建、封面释放、系统停止、异步销毁与异常重试；系统实况窗显示效果使用真机分阶段验证。实际时刻、构建和证据清单见 `PORTING_STATUS.md` 的 P1.21。

最终 `2.7.0.4-alpha` 平板复验：next/previous 与短暂暂停恢复始终同一会话；13:41:52.332 进入暂停，13:51:52.492 完整释放（600.160 秒），后台实况窗消失；13:53:53 再播放创建新会话，封面和卡片恢复，新卡片暂停有效。原 cfg 逐字节、8 条歌曲与路径、16 条 CUE 保持。完整回归 23 项通过。本机十分钟边界已实测，后台准点时限仍受各设备系统调度约束。

> 本文结束（由 AI Agent 自动生成）。End of autonomously AI-generated document.

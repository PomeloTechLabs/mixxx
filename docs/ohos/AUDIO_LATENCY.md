# OHOS 音频输出延迟：根因与验证

> 本文档由 AI 助手自主生成。
>
> 现象：两首歌混频接入时（推 crossfader / 按播放），出声比操作慢约 0.1s。

## 根因（2026-09-30 实测定位）

设备基线（修复前，NORMAL 模式）hilog：

```
PortAudioOHOS: stream opened: 48000 Hz, 2 ch, 4458 frames/host buffer
```

- **OHAudio `AUDIOSTREAM_LATENCY_MODE_NORMAL`（主因）**：单次写回调块 4458 帧 @48kHz ≈ **92.9ms**，音频服务端几乎缓冲了 100ms 才来拉数据。
- Mixxx 引擎默认 buffer 1024 帧（`AudioBufferSizeIndex::Size20xms`）≈ 21.3ms。
- PortAudio 适配层（user 1024 vs host 4458）再叠少量排队。

合计 ≈ 0.1s+，与体感一致。两个 deck 走同一输出流，彼此不会错拍，慢的是"操作→出声"。

## 修复内容

`cmake/ohos/ports/portaudio/ohos/pa_ohos.c`：

1. renderer 优先以 `AUDIOSTREAM_LATENCY_MODE_FAST` 打开；若设备拒绝（部分设备 FAST 限制采样率/声道），销毁 builder 后回退 NORMAL，不再直接失败。
2. 打开成功后记录 `OH_AudioRenderer_GetLatency(AUDIOSTREAM_LATENCY_TYPE_ALL)` 并经 `PaStreamInfo` 上报；`stream opened` 日志增加 latency mode 与 renderer 延迟（ms）。
3. `cmake/ohos/ports/portaudio/vcpkg.json` port-version 24→26，改 overlay 源码必须 bump，否则 vcpkg 跳过重建。

## 已知副作用与回退：波形前后抽动（已修）

第一版补丁曾把回调的 `outputBufferDacTime` 从"按已播帧数推算"改为
`currentTime + rendererLatency`，结果播放中波形前后抽动。原因：
Mixxx 波形时钟对 DAC 时间戳做合理性检查（与 CPU 计时偏差超过 1/10 buffer
即弃用，见 `SoundDevicePortAudio::updateCallbackEntryToDacTime`）。帧数推算
公式每次回调精确前进整数帧，检查恒自洽；墙钟公式带回调调度抖动，且 FAST
下宿主回调块与引擎 1024 帧 buffer 不成整数倍，时间戳在可信/不可信间反复
横跳，波形随两个时钟源切换来回跳（每跳数百采样，肉眼可见）。

结论：**DAC 时间戳保持帧数推算公式**（自洽、稳定），真实延迟只经
`PaStreamInfo`/日志上报。若将来想让波形做精确延迟补偿，需先让宿主回调
与引擎 buffer 对齐（`OH_AudioStreamBuilder_SetFrameSizeInCallback`），再谈
锚定墙钟，否则必然抖。

## 构建与安装

```bash
# 容器内：改 overlay port 后必须先移除旧包再重配
docker exec mixxx-ohos-build bash -c \
  '/data/vcpkg/vcpkg/vcpkg remove portaudio --triplet arm64-ohos --recurse'
MSYS_NO_PATHCONV=1 docker exec mixxx-ohos-build bash -c \
  'cmake -S /data/src/mixxx -B /data/mixxx-build/ohos && cmake --build /data/mixxx-build/ohos -j 24'
MSYS_NO_PATHCONV=1 docker exec mixxx-ohos-build bash -c \
  'bash /data/src/mixxx/packaging/ohos/build-hap.sh'

# 宿主机：安装（HAP 路径见 build-hap.sh 输出）
MSYS_NO_PATHCONV=1 "C:/Program Files/Huawei/DevEco Studio/sdk/default/openharmony/toolchains/hdc.exe" \
  install -r packaging/ohos/entry/build/default/outputs/default/entry-default-signed.hap
```

## 验证步骤

1. **指标（客观）**：启动应用后抓日志
   ```bash
   hdc shell "hilog -x | grep PortAudioOHOS | tail"
   ```
   - 修复前基线：`4458 frames/host buffer`（≈92.9ms/块）
   - 修复后实测（2026-09-30，5KPBB25818203996）：`240 frames/host buffer,
     latency mode 1, renderer latency 25 ms` —— FAST 生效，可听延迟约从
     115ms 降到 ~46ms（25ms 通路 + 21ms 引擎 buffer）。
   - 若出现 `GenerateRenderer failed in latency mode 1` 后跟 `latency mode 0`，
     说明该设备 FAST 被拒、走了 NORMAL 回退——记录设备型号，延迟不会改善。
2. **听感 A/B（主观）**：内置扬声器或有线耳机（**不要用蓝牙**，蓝牙编解码
   自带 100–250ms，任何端侧改动无效）：加载两轨做混频接入，比较推子/播放
   到出声的跟手程度；可用另一台手机录慢动作对比波形过线与出声帧差。
3. **回归清单**：
   - 长时间播放无爆音/underrun（FAST 模式容错更小，手机 SoC 需确认够快）；
   - 中断：来电、其他应用抢音频焦点后恢复；
   - 暂停/恢复、锁屏、息屏播放；
   - 停止再启动音频（Preferences 里切换声卡）无残留静音。

## 后续调优（若仍不够低）

- Preferences → 声音硬件 → Buffer Size 降到 512 帧（10.7ms），总延迟可再降 ~10ms；
  手机上如出现爆音再退回 1024。
- 若 FAST 下 `frames/host buffer` 仍偏大，可在 builder 上加
  `OH_AudioStreamBuilder_SetFrameSizeInCallback`（API 11+，约束：≥设备单次处理块、
  <内部缓冲容量一半）。

> 本文档由 AI 助手自主生成。

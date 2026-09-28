import common from '@ohos.app.ability.common';
import avSession from '@ohos.multimedia.avsession';
import image from '@ohos.multimedia.image';
import backgroundTaskManager from '@ohos.resourceschedule.backgroundTaskManager';
import wantAgent, { WantAgent } from '@ohos.app.ability.wantAgent';
import { BusinessError } from '@ohos.base';
import hilog from '../common/RuntimeLog';
import mediaBridge from 'libmixxxohosmedia.so';
import { LOG_DOMAIN, LOG_TAG } from '../common/QtAppConstants';

interface MediaState {
  ready: boolean;
  playing: boolean;
  assetId: string;
  title: string;
  artist: string;
  album: string;
  duration: number;
  position: number;
}

export default class MixxxMediaSession {
  private session: avSession.AVSession | null = null;
  private launchAgent: WantAgent | null = null;
  private timer: number = -1;
  private closed: boolean = false;
  private syncing: boolean = false;
  private backgroundRunning: boolean = false;
  private backgroundTransition: Promise<void> | null = null;
  private metadataKey: string = '';
  private lastStateLog: number = 0;
  private lastPlaying: boolean = false;
  private artwork: image.PixelMap | undefined = undefined;
  private initialized: Promise<void>;
  private context: common.UIAbilityContext;

  constructor(context: common.UIAbilityContext) {
    this.context = context;
    this.initialized = this.initialize();
  }

  private async initialize(): Promise<void> {
    try {
      const session = await avSession.createAVSession(this.context, '旧柚Mixxx', 'audio');
      this.session = session;
      this.launchAgent = await wantAgent.getWantAgent({
        wants: [{ bundleName: 'com.pomelo.mixxx', abilityName: 'QAbility' }],
        operationType: wantAgent.OperationType.START_ABILITY,
        requestCode: 0,
        wantAgentFlags: [wantAgent.WantAgentFlags.UPDATE_PRESENT_FLAG]
      });
      await session.setLaunchAbility(this.launchAgent);
      try {
        const bytes = await this.context.resourceManager.getMediaContent($r('app.media.startIcon').id);
        const source = image.createImageSource(bytes.buffer as ArrayBuffer);
        this.artwork = await source.createPixelMap();
        source.release();
      } catch (e) {
        hilog.warn(LOG_DOMAIN, LOG_TAG, 'Mixxx session artwork: %{public}s', JSON.stringify(e));
      }
      session.on('play', (): void => { this.dispatch('play'); });
      session.on('pause', (): void => { this.dispatch('pause'); });
      session.on('stop', (): void => { this.dispatch('stop'); });
      session.on('playNext', (): void => { this.dispatch('next'); });
      session.on('playPrevious', (): void => { this.dispatch('previous'); });
      session.on('seek', (position: number): void => { this.dispatch('seek', position); });
      await session.activate();
      if (!this.closed) {
        this.timer = setInterval((): void => { this.sync(); }, 750);
        this.sync();
      }
      hilog.info(LOG_DOMAIN, LOG_TAG, 'Mixxx AVSession active: %{public}s', session.sessionId);
    } catch (e) {
      hilog.error(LOG_DOMAIN, LOG_TAG, 'Mixxx AVSession initialize: %{public}s', JSON.stringify(e));
    }
  }

  private dispatch(command: string, value: number = 0): void {
    const deliver = (): void => {
      const accepted = mediaBridge.sendCommand(command, value);
      hilog.info(LOG_DOMAIN, LOG_TAG, 'Mixxx media command %{public}s accepted=%{public}s',
        command, String(accepted));
      setTimeout((): void => { this.sync(); }, 300);
    };
    if (command === 'play') {
      this.setBackgroundRunning(true).then(deliver).catch((e: BusinessError): void => {
        hilog.error(LOG_DOMAIN, LOG_TAG, 'Mixxx media resume background: %{public}s', JSON.stringify(e));
        deliver();
      });
    } else {
      deliver();
    }
  }

  private async setBackgroundRunning(playing: boolean): Promise<void> {
    if (!canIUse('SystemCapability.ResourceSchedule.BackgroundTaskManager.ContinuousTask')) {
      return;
    }
    if (this.backgroundTransition !== null) {
      await this.backgroundTransition;
    }
    if (playing === this.backgroundRunning || this.closed || this.launchAgent === null) {
      return;
    }
    const transition = playing
      ? backgroundTaskManager.startBackgroundRunning(this.context,
          backgroundTaskManager.BackgroundMode.AUDIO_PLAYBACK, this.launchAgent)
      : backgroundTaskManager.stopBackgroundRunning(this.context);
    this.backgroundTransition = transition;
    try {
      await transition;
      this.backgroundRunning = playing;
      hilog.info(LOG_DOMAIN, LOG_TAG, 'Mixxx audio background task: %{public}s', String(playing));
    } finally {
      this.backgroundTransition = null;
    }
  }

  public async sync(): Promise<void> {
    if (this.closed || this.syncing || this.session === null) {
      return;
    }
    this.syncing = true;
    let operation: string = 'read native state';
    try {
      const state: MediaState = JSON.parse(mediaBridge.readState()) as MediaState;
      if (!state.ready) {
        return;
      }
      const key = state.assetId + '\n' + state.title + '\n' + state.artist + '\n' + state.album + '\n' + state.duration;
      if (key !== this.metadataKey || state.playing !== this.lastPlaying || Date.now() - this.lastStateLog >= 60000) {
        hilog.info(LOG_DOMAIN, LOG_TAG, 'Mixxx media state asset=%{public}s playing=%{public}s positionMs=%{public}d',
          state.assetId, String(state.playing), Math.round(state.position));
        this.lastStateLog = Date.now();
        this.lastPlaying = state.playing;
      }
      if (key !== this.metadataKey) {
        const metadata: avSession.AVMetadata = {
          assetId: state.assetId || 'mixxx-empty',
          title: state.title || '旧柚Mixxx',
          duration: Math.round(Math.max(0, state.duration))
        };
        if (state.artist.length > 0) {
          metadata.artist = state.artist;
        }
        if (state.album.length > 0) {
          metadata.album = state.album;
        }
        if (this.artwork !== undefined) {
          metadata.mediaImage = this.artwork;
        }
        operation = 'set metadata';
        await this.session.setAVMetadata(metadata);
        this.metadataKey = key;
      }
      operation = 'set playback state';
      await this.session.setAVPlaybackState({
        state: state.playing ? avSession.PlaybackState.PLAYBACK_STATE_PLAY : avSession.PlaybackState.PLAYBACK_STATE_PAUSE,
        speed: 1,
        position: { elapsedTime: Math.round(Math.max(0, state.position)), updateTime: Date.now() },
        duration: Math.round(Math.max(0, state.duration))
      });
      operation = 'set background task';
      await this.setBackgroundRunning(state.playing);
    } catch (e) {
      const err = e as BusinessError;
      hilog.error(LOG_DOMAIN, LOG_TAG, 'Mixxx media sync %{public}s: code=%{public}d message=%{public}s',
        operation, err.code, err.message);
    } finally {
      this.syncing = false;
    }
  }

  public async destroy(): Promise<void> {
    this.closed = true;
    if (this.timer >= 0) {
      clearInterval(this.timer);
      this.timer = -1;
    }
    await this.initialized;
    if (this.backgroundTransition !== null) {
      await this.backgroundTransition.catch((): void => {});
    }
    if (this.backgroundRunning) {
      await backgroundTaskManager.stopBackgroundRunning(this.context).catch((e: BusinessError): void => {
        hilog.warn(LOG_DOMAIN, LOG_TAG, 'Mixxx background stop: %{public}s', JSON.stringify(e));
      });
      this.backgroundRunning = false;
    }
    if (this.session !== null) {
      await this.session.destroy().catch((e: BusinessError): void => {
        hilog.warn(LOG_DOMAIN, LOG_TAG, 'Mixxx session destroy: %{public}s', JSON.stringify(e));
      });
      this.session = null;
    }
    this.artwork?.release();
    this.artwork = undefined;
  }
}

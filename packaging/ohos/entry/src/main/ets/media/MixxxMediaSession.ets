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
  loading: boolean;
  assetId: string;
  title: string;
  artist: string;
  album: string;
  artworkKey: string;
  artworkAvailable: boolean;
  duration: number;
  position: number;
}

const PAUSED_SESSION_TIMEOUT_MS: number = 10 * 60 * 1000;
const LOAD_TRANSITION_TIMEOUT_MS: number = 15 * 1000;
const RESUME_COMMAND_TIMEOUT_MS: number = 5 * 1000;

export default class MixxxMediaSession {
  private session: avSession.AVSession | null = null;
  private sessionActive: boolean = false;
  private sessionConfigured: boolean = false;
  private pausedSince: number = -1;
  private stopRequested: boolean = false;
  private loadingSince: number = -1;
  private resumePendingUntil: number = -1;
  private playbackKey: string = '';
  private launchAgent: WantAgent | null = null;
  private timer: number = -1;
  private closed: boolean = false;
  private syncing: boolean = false;
  private syncOperation: Promise<void> | null = null;
  private backgroundRunning: boolean = false;
  private backgroundTransition: Promise<void> | null = null;
  private metadataKey: string = '';
  private lastStateLog: number = 0;
  private lastPlaying: boolean = false;
  private artwork: image.PixelMap | undefined = undefined;
  private artworkKey: string = '';
  private fallbackArtwork: image.PixelMap | undefined = undefined;
  private initialized: Promise<void>;
  private context: common.UIAbilityContext;

  constructor(context: common.UIAbilityContext) {
    this.context = context;
    this.initialized = this.initialize();
    this.initialized.then((): void => {
      if (!this.closed) {
        this.timer = setInterval((): void => { this.sync(); }, 750);
        this.sync();
      }
    });
  }

  private async initialize(): Promise<void> {
    try {
      this.launchAgent = await wantAgent.getWantAgent({
        wants: [{ bundleName: 'com.pomelo.mixxx', abilityName: 'QAbility' }],
        operationType: wantAgent.OperationType.START_ABILITY,
        requestCode: 0,
        wantAgentFlags: [wantAgent.WantAgentFlags.UPDATE_PRESENT_FLAG]
      });
      try {
        const bytes = await this.context.resourceManager.getMediaContent($r('app.media.startIcon').id);
        this.fallbackArtwork = await this.decodeArtwork(bytes.buffer as ArrayBuffer);
      } catch (e) {
        hilog.warn(LOG_DOMAIN, LOG_TAG, 'Mixxx session artwork: %{public}s', JSON.stringify(e));
      }
    } catch (e) {
      hilog.error(LOG_DOMAIN, LOG_TAG, 'Mixxx AVSession initialize: %{public}s', JSON.stringify(e));
    }
  }

  private async ensureSession(): Promise<avSession.AVSession | null> {
    if (this.closed) {
      return null;
    }
    if (this.session === null) {
      const session = await avSession.createAVSession(this.context, '旧柚Mixxx', 'audio');
      this.session = session;
      if (this.closed) {
        return null;
      }
    }
    const session = this.session;
    if (!this.sessionConfigured) {
      if (this.launchAgent !== null) {
        await session.setLaunchAbility(this.launchAgent);
      }
      session.on('play', (): void => { this.dispatch('play'); });
      session.on('pause', (): void => { this.dispatch('pause'); });
      session.on('stop', (): void => { this.dispatch('stop'); });
      session.on('playNext', (): void => { this.dispatch('next'); });
      session.on('playPrevious', (): void => { this.dispatch('previous'); });
      session.on('seek', (position: number): void => { this.dispatch('seek', position); });
      this.sessionConfigured = true;
    }
    if (!this.closed && !this.sessionActive) {
      await session.activate();
      this.sessionActive = true;
      hilog.info(LOG_DOMAIN, LOG_TAG, 'Mixxx AVSession active: %{public}s', session.sessionId);
    }
    return this.closed ? null : session;
  }

  private dispatch(command: string, value: number = 0): void {
    if (this.closed) {
      return;
    }
    if (command === 'play') {
      this.resumePendingUntil = Date.now() + RESUME_COMMAND_TIMEOUT_MS;
    } else if (command === 'pause' || command === 'stop') {
      this.resumePendingUntil = -1;
    }
    const deliver = (): void => {
      if (this.closed) {
        return;
      }
      const accepted: boolean = mediaBridge.sendCommand(command, value);
      if (accepted && command === 'stop') {
        this.stopRequested = true;
      } else if (accepted && command === 'play') {
        this.stopRequested = false;
      } else if (!accepted && command === 'play') {
        this.resumePendingUntil = -1;
      }
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

  private async decodeArtwork(bytes: ArrayBuffer): Promise<image.PixelMap> {
    const source = image.createImageSource(bytes);
    try {
      return await source.createPixelMap();
    } finally {
      await source.release().catch((e: BusinessError): void => {
        hilog.warn(LOG_DOMAIN, LOG_TAG, 'Mixxx image source release: %{public}s', JSON.stringify(e));
      });
    }
  }

  private async releaseArtwork(artwork: image.PixelMap | undefined): Promise<void> {
    if (artwork !== undefined) {
      await artwork.release().catch((e: BusinessError): void => {
        hilog.warn(LOG_DOMAIN, LOG_TAG, 'Mixxx artwork release: %{public}s', JSON.stringify(e));
      });
    }
  }

  private async updateMetadata(session: avSession.AVSession, state: MediaState, key: string): Promise<void> {
    let artwork = this.artwork;
    let decoded: image.PixelMap | undefined = undefined;
    try {
      if (state.artworkKey !== this.artworkKey) {
        artwork = undefined;
        if (state.artworkAvailable) {
          const bytes: ArrayBuffer = mediaBridge.readArtwork(state.artworkKey);
          if (bytes.byteLength === 0) {
            return;
          }
          try {
            decoded = await this.decodeArtwork(bytes);
            artwork = decoded;
          } catch (e) {
            hilog.warn(LOG_DOMAIN, LOG_TAG, 'Mixxx track artwork decode: %{public}s', JSON.stringify(e));
          }
        }
      }
      const current: MediaState = JSON.parse(mediaBridge.readState()) as MediaState;
      if (this.closed || !current.ready || current.artworkKey !== state.artworkKey || current.assetId !== state.assetId) {
        return;
      }
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
      metadata.mediaImage = artwork === undefined ? this.fallbackArtwork : artwork;
      await session.setAVMetadata(metadata);
      const previous = this.artwork;
      this.artwork = artwork;
      decoded = undefined;
      this.artworkKey = state.artworkKey;
      this.metadataKey = key;
      if (previous !== artwork) {
        await this.releaseArtwork(previous);
      }
      hilog.info(LOG_DOMAIN, LOG_TAG, 'Mixxx media artwork asset=%{public}s source=%{public}s key=%{public}s',
        state.assetId, artwork === undefined ? 'app' : 'track', state.artworkKey);
    } finally {
      await this.releaseArtwork(decoded);
    }
  }

  public async sync(): Promise<void> {
    if (this.closed || this.syncing) {
      return;
    }
    this.syncing = true;
    const operation = this.syncState();
    this.syncOperation = operation;
    try {
      await operation;
    } finally {
      this.syncOperation = null;
      this.syncing = false;
    }
  }

  private async retireSession(reason: string): Promise<void> {
    const session = this.session;
    if (session === null) {
      return;
    }
    if (this.sessionActive) {
      await session.setAVPlaybackState({
        state: avSession.PlaybackState.PLAYBACK_STATE_STOP
      }).catch((e: BusinessError): void => {
        hilog.warn(LOG_DOMAIN, LOG_TAG, 'Mixxx session stop state: %{public}s', JSON.stringify(e));
      });
      await session.deactivate().then((): void => {
        this.sessionActive = false;
      }).catch((e: BusinessError): void => {
        hilog.warn(LOG_DOMAIN, LOG_TAG, 'Mixxx session deactivate: %{public}s', JSON.stringify(e));
      });
    }
    await session.destroy();
    this.session = null;
    this.sessionConfigured = false;
    this.sessionActive = false;
    this.metadataKey = '';
    this.playbackKey = '';
    this.artworkKey = '';
    const artwork = this.artwork;
    this.artwork = undefined;
    await this.releaseArtwork(artwork);
    hilog.info(LOG_DOMAIN, LOG_TAG, 'Mixxx AVSession retired: %{public}s', reason);
  }

  private async applySessionPolicy(state: MediaState): Promise<boolean> {
    if (!state.ready || this.stopRequested) {
      this.pausedSince = -1;
      this.loadingSince = -1;
      this.resumePendingUntil = -1;
      await this.setBackgroundRunning(false);
      await this.retireSession(this.stopRequested ? 'stop' : 'unavailable');
      this.stopRequested = false;
      return false;
    }
    if (state.ready && state.playing) {
      this.pausedSince = -1;
      this.loadingSince = -1;
      this.resumePendingUntil = -1;
      await this.setBackgroundRunning(true);
      return true;
    }
    if (Date.now() < this.resumePendingUntil) {
      return false;
    }
    this.resumePendingUntil = -1;
    if (state.loading) {
      if (this.loadingSince < 0) {
        this.loadingSince = Date.now();
      }
      if (Date.now() - this.loadingSince >= LOAD_TRANSITION_TIMEOUT_MS) {
        await this.setBackgroundRunning(false);
        await this.retireSession('loading timeout');
        return false;
      }
      if (this.session !== null && this.pausedSince < 0) {
        return false;
      }
    } else {
      this.loadingSince = -1;
    }
    if (!state.loading && state.assetId.length === 0 && state.title.length === 0 && state.duration <= 0) {
      this.pausedSince = -1;
      await this.setBackgroundRunning(false);
      await this.retireSession('empty');
      return false;
    }
    if (this.session === null) {
      await this.setBackgroundRunning(false);
      return false;
    }
    if (this.pausedSince < 0) {
      this.pausedSince = Date.now();
    }
    if (Date.now() - this.pausedSince >= PAUSED_SESSION_TIMEOUT_MS) {
      await this.setBackgroundRunning(false);
      await this.retireSession('paused timeout');
      return false;
    }
    await this.setBackgroundRunning(false);
    return !state.loading;
  }

  private async syncState(): Promise<void> {
    let operation: string = 'read native state';
    try {
      await this.initialized;
      if (this.closed) {
        return;
      }
      let state: MediaState = JSON.parse(mediaBridge.readState()) as MediaState;
      operation = 'apply session policy';
      if (!await this.applySessionPolicy(state) || this.closed) {
        return;
      }
      operation = 'ensure session';
      const session = await this.ensureSession();
      if (session === null) {
        return;
      }
      state = JSON.parse(mediaBridge.readState()) as MediaState;
      if (this.closed || !await this.applySessionPolicy(state)) {
        return;
      }
      const key = state.assetId + '\n' + state.title + '\n' + state.artist + '\n' + state.album + '\n' + state.duration + '\n' + state.artworkKey;
      if (key !== this.metadataKey || state.playing !== this.lastPlaying || Date.now() - this.lastStateLog >= 60000) {
        hilog.info(LOG_DOMAIN, LOG_TAG, 'Mixxx media state asset=%{public}s playing=%{public}s positionMs=%{public}d',
          state.assetId, String(state.playing), Math.round(state.position));
        this.lastStateLog = Date.now();
        this.lastPlaying = state.playing;
      }
      if (key !== this.metadataKey) {
        operation = 'set metadata';
        await this.updateMetadata(session, state, key);
        state = JSON.parse(mediaBridge.readState()) as MediaState;
      }
      if (this.closed || !await this.applySessionPolicy(state)) {
        return;
      }
      operation = 'set playback state';
      const position = Math.round(Math.max(0, state.position));
      const duration = Math.round(Math.max(0, state.duration));
      const playbackKey = String(state.playing) + '\n' + state.assetId + '\n' + position + '\n' + duration;
      if (state.playing || playbackKey !== this.playbackKey) {
        await session.setAVPlaybackState({
          state: state.playing ? avSession.PlaybackState.PLAYBACK_STATE_PLAY : avSession.PlaybackState.PLAYBACK_STATE_PAUSE,
          speed: 1,
          position: { elapsedTime: position, updateTime: Date.now() },
          duration: duration
        });
        this.playbackKey = playbackKey;
      }
      operation = 'set background task';
      await this.setBackgroundRunning(state.playing);
    } catch (e) {
      const err = e as BusinessError;
      hilog.error(LOG_DOMAIN, LOG_TAG, 'Mixxx media sync %{public}s: code=%{public}d message=%{public}s',
        operation, err.code, err.message);
    }
  }

  public async destroy(): Promise<void> {
    this.closed = true;
    if (this.timer >= 0) {
      clearInterval(this.timer);
      this.timer = -1;
    }
    await this.initialized;
    if (this.syncOperation !== null) {
      await this.syncOperation;
    }
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
      await this.retireSession('ability destroyed').catch((e: BusinessError): void => {
        hilog.warn(LOG_DOMAIN, LOG_TAG, 'Mixxx session destroy: %{public}s', JSON.stringify(e));
      });
      this.session = null;
    }
    await this.releaseArtwork(this.artwork);
    this.artwork = undefined;
    await this.releaseArtwork(this.fallbackArtwork);
    this.fallbackArtwork = undefined;
  }
}

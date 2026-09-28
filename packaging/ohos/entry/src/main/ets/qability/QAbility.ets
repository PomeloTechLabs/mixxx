import AbilityConstant from '@ohos.app.ability.AbilityConstant';
import QAbilityStage from '../qabilitystage/QAbilityStage';
import UIAbility from '@ohos.app.ability.UIAbility';
import Want from '@ohos.app.ability.Want';
import { BusinessError } from '@ohos.base';
import display from '@ohos.display';
import Window from '@ohos.window'
import hilog from '../common/RuntimeLog';
import qpa from 'libqohos.so';
import mediaBridge from 'libmixxxohosmedia.so';
import MixxxMediaSession from '../media/MixxxMediaSession';
import {LOG_DOMAIN, LOG_TAG} from '../common/QtAppConstants';

interface MixxxDialogRect {
  left?: number;
  top?: number;
  width?: number;
  height?: number;
  revision?: number;
}

export default class QAbility extends UIAbility {

  private localStorage: LocalStorage | null = null;
  private fullscreenTimers: number[] = [];
  private foreground: boolean = false;
  private mediaSession: MixxxMediaSession | null = null;
  private keyboardStage: Window.WindowStage | null = null;
  private keyboardTimer: number = 0;
  private keyboardScanning: boolean = false;
  private keyboardWindows: Map<number, Window.Window> = new Map();
  private keyboardCallbacks: Map<number, (height: number) => void> = new Map();
  private keyboardHeights: Map<number, number> = new Map();
  private mainKeyboardHeight: number = 0;
  private reportedKeyboardHeight: number = 0;
  private raisedDialogRevision: number = 0;
  private readonly windowStatusChanged = (status: Window.WindowStatusType): void => {
    const mainWindow = QAbility.mainWindow;
    if (mainWindow !== null) {
      this.applyWindowLayout(mainWindow, status).catch((err: BusinessError): void => {
        hilog.warn(LOG_DOMAIN, LOG_TAG, 'QAbility window layout: ' + JSON.stringify(err));
      });
    }
  };

  private async applyWindowLayout(mainWindow: Window.Window, status?: Window.WindowStatusType): Promise<void> {
    const currentStatus = status ?? mainWindow.getWindowStatus();
    await mainWindow.setWindowLayoutFullScreen(currentStatus !== Window.WindowStatusType.FLOATING);
    const properties = mainWindow.getWindowProperties();
    hilog.info(LOG_DOMAIN, LOG_TAG, 'QAbility window layout status=' + currentStatus +
      ' window=' + JSON.stringify(properties.windowRect) +
      ' drawable=' + JSON.stringify(properties.drawableRect));
  }
  private readonly keyboardHeightChanged = (height: number): void => {
    this.mainKeyboardHeight = height;
    this.reportKeyboardHeight();
    if (height === 0 && this.foreground) {
      this.scheduleFullscreen();
    }
  };

  private reportKeyboardHeight(): void {
    let height: number = this.mainKeyboardHeight;
    this.keyboardHeights.forEach((value: number): void => { height = Math.max(height, value); });
    if (height !== this.reportedKeyboardHeight) {
      this.reportedKeyboardHeight = height;
      mediaBridge.setKeyboardHeight(height);
    }
  }

  private subWindowKeyboardHeight(subWindow: Window.Window, height: number): number {
    if (height <= 0 || QAbility.mainWindow === null) {
      return 0;
    }
    const mainRect: Window.Rect = QAbility.mainWindow.getWindowProperties().windowRect;
    const subRect: Window.Rect = subWindow.getWindowProperties().windowRect;
    return height + Math.max(0, mainRect.top + mainRect.height - subRect.top - subRect.height);
  }

  private async scanKeyboardWindows(): Promise<void> {
    const stage = this.keyboardStage;
    if (stage === null || !this.foreground || this.keyboardScanning) {
      return;
    }
    this.keyboardScanning = true;
    try {
      const windows: Window.Window[] = await stage.getSubWindow();
      if (stage !== this.keyboardStage || !this.foreground) {
        return;
      }
      const alive: Set<number> = new Set();
      windows.forEach((subWindow: Window.Window): void => {
        const id: number = subWindow.getWindowProperties().id;
        alive.add(id);
        if (!this.keyboardWindows.has(id)) {
          const callback = (height: number): void => {
            this.keyboardHeights.set(id, this.subWindowKeyboardHeight(subWindow, height));
            this.reportKeyboardHeight();
          };
          subWindow.on('keyboardHeightChange', callback);
          this.keyboardWindows.set(id, subWindow);
          this.keyboardCallbacks.set(id, callback);
          const area: Window.AvoidArea = subWindow.getWindowAvoidArea(Window.AvoidAreaType.TYPE_KEYBOARD);
          this.keyboardHeights.set(id, this.subWindowKeyboardHeight(subWindow, area.bottomRect.height));
        }
      });
      this.keyboardWindows.forEach((subWindow: Window.Window, id: number): void => {
        if (!alive.has(id)) {
          try {
            subWindow.off('keyboardHeightChange', this.keyboardCallbacks.get(id));
          } catch (_) {}
          this.keyboardWindows.delete(id);
          this.keyboardCallbacks.delete(id);
          this.keyboardHeights.delete(id);
        }
      });
      this.reportKeyboardHeight();
      const dialog: MixxxDialogRect = JSON.parse(mediaBridge.readWindowState()) as MixxxDialogRect;
      if (dialog.revision !== undefined && dialog.revision !== this.raisedDialogRevision) {
        for (const subWindow of windows) {
          const rect: Window.Rect = subWindow.getWindowProperties().windowRect;
          if (Math.abs(rect.left - (dialog.left ?? -1)) <= 2 &&
              Math.abs(rect.top - (dialog.top ?? -1)) <= 2 &&
              Math.abs(rect.width - (dialog.width ?? 0)) <= 2 &&
              Math.abs(rect.height - (dialog.height ?? 0)) <= 2) {
            try {
              await subWindow.raiseToAppTop();
            } catch (err) {
              hilog.warn(LOG_DOMAIN, LOG_TAG, 'QAbility modal raise: ' + JSON.stringify(err));
            }
            this.raisedDialogRevision = dialog.revision;
            break;
          }
        }
      }
    } catch (err) {
      hilog.warn(LOG_DOMAIN, LOG_TAG, 'QAbility subwindow keyboard observer: ' + JSON.stringify(err));
      this.stopKeyboardObserver();
    } finally {
      this.keyboardScanning = false;
    }
  }

  private startKeyboardObserver(): void {
    if (this.keyboardTimer === 0) {
      this.keyboardTimer = setInterval((): void => { this.scanKeyboardWindows(); }, 500);
    }
    this.scanKeyboardWindows();
  }

  private stopKeyboardObserver(): void {
    if (this.keyboardTimer !== 0) {
      clearInterval(this.keyboardTimer);
      this.keyboardTimer = 0;
    }
    this.keyboardWindows.forEach((subWindow: Window.Window, id: number): void => {
      try {
        subWindow.off('keyboardHeightChange', this.keyboardCallbacks.get(id));
      } catch (_) {}
    });
    this.keyboardWindows.clear();
    this.keyboardCallbacks.clear();
    this.keyboardHeights.clear();
    this.mainKeyboardHeight = 0;
    this.raisedDialogRevision = 0;
    this.reportKeyboardHeight();
  }

  // Kept so media folder provisioning can hand the picker the ability window.
  private static mainWindow: Window.Window | null = null;

  onCreate(want: Want, launchParam: AbilityConstant.LaunchParam) {
    hilog.info(LOG_DOMAIN, LOG_TAG, 'QAbility::onCreate(): want.parameters: ' + JSON.stringify(want.parameters));
    QAbilityStage.initQtAppContextIfNeeded(this.context.getApplicationContext());
    qpa.handleAbilityOnCreate(this, want, launchParam);
    this.mediaSession = new MixxxMediaSession(this.context);
  }

  async onDestroy(): Promise<void> {
    hilog.info(LOG_DOMAIN, LOG_TAG, 'QAbility::onDestroy()');
    this.stopKeyboardObserver();
    this.keyboardStage = null;
    await this.mediaSession?.destroy();
    this.mediaSession = null;
    await qpa.handleAbilityOnDestroy(this);
  }

  onPrepareToTerminate(): boolean {
    hilog.info(LOG_DOMAIN, LOG_TAG, 'QAbility::onPrepareToTerminate()');
    return qpa.handleAbilityOnPrepareToTerminate(this);
  }

  onPrepareToTerminateAsync(): Promise<boolean> {
    hilog.info(LOG_DOMAIN, LOG_TAG, 'QAbility::onPrepareToTerminateAsync()');
    return qpa.handleAbilityOnPrepareToTerminateAsync(this);
  }


  private scheduleFullscreen(): void {
    this.clearFullscreenTimers();
    const apply = (attempt: string): void => {
      const mainWindow = QAbility.mainWindow;
      if (mainWindow !== null) {
        try {
          mainWindow.setPreferredOrientation(Window.Orientation.AUTO_ROTATION_LANDSCAPE)
            .then((): void => {
              if (attempt === 'retry5s') {
                hilog.info(LOG_DOMAIN, LOG_TAG, 'QAbility preferred orientation: %{public}d',
                  mainWindow.getPreferredOrientation());
              }
            }).catch((err: BusinessError): void => {
              hilog.warn(LOG_DOMAIN, LOG_TAG, 'QAbility[' + attempt + '] orientation: ' + JSON.stringify(err));
            });
        } catch (e) {
          hilog.warn(LOG_DOMAIN, LOG_TAG, 'QAbility[' + attempt + '] orientation: ' + JSON.stringify(e));
        }
        try {
          this.applyWindowLayout(mainWindow).catch((err: BusinessError): void => {
            hilog.warn(LOG_DOMAIN, LOG_TAG, 'QAbility[' + attempt + '] layoutFullScreen: ' + JSON.stringify(err));
          });
        } catch (e) {
          hilog.warn(LOG_DOMAIN, LOG_TAG, 'QAbility[' + attempt + '] layoutFullScreen: ' + JSON.stringify(e));
        }
        try {
          mainWindow.setWindowDecorVisible(false);
        } catch (e) {
          hilog.warn(LOG_DOMAIN, LOG_TAG, 'QAbility[' + attempt + '] decor: ' + JSON.stringify(e));
        }
        try {
          mainWindow.setWindowBackgroundColor('#000000');
        } catch (e) {
          hilog.warn(LOG_DOMAIN, LOG_TAG, 'QAbility[' + attempt + '] background: ' + JSON.stringify(e));
        }
        Promise.all([
          mainWindow.setSpecificSystemBarEnabled('status', false),
          mainWindow.setSpecificSystemBarEnabled('navigationIndicator', false)
        ]).then((): void => {
          hilog.info(LOG_DOMAIN, LOG_TAG, 'QAbility[' + attempt + '] system bars hidden');
          if (attempt === 'retry5s') {
            hilog.info(LOG_DOMAIN, LOG_TAG, 'QAbility system avoid area: ' +
              JSON.stringify(mainWindow.getWindowAvoidArea(Window.AvoidAreaType.TYPE_SYSTEM)));
            display.getDefaultDisplaySync().getCutoutInfo().then((info: display.CutoutInfo): void => {
              hilog.info(LOG_DOMAIN, LOG_TAG, 'QAbility display cutouts: ' + JSON.stringify(info.boundingRects));
            }).catch((err: BusinessError): void => {
              hilog.warn(LOG_DOMAIN, LOG_TAG, 'QAbility display cutouts: ' + JSON.stringify(err));
            });
          }
        }).catch((err: BusinessError): void => {
          hilog.warn(LOG_DOMAIN, LOG_TAG, 'QAbility[' + attempt + '] bars: ' + JSON.stringify(err));
        });
      }
    };
    apply('foreground');
    this.fullscreenTimers.push(setTimeout(() => apply('retry2s'), 2000));
    this.fullscreenTimers.push(setTimeout(() => apply('retry5s'), 5000));
  }

  private clearFullscreenTimers(): void {
    this.fullscreenTimers.forEach((timer: number): void => clearTimeout(timer));
    this.fullscreenTimers = [];
  }

  private registerMainWindow(windowStage: Window.WindowStage): void {
    this.stopKeyboardObserver();
    this.keyboardStage = windowStage;
    if (QAbility.mainWindow !== null) {
      QAbility.mainWindow.off('keyboardHeightChange', this.keyboardHeightChanged);
      QAbility.mainWindow.off('windowStatusChange', this.windowStatusChanged);
    }
    QAbility.mainWindow = windowStage.getMainWindowSync();
    QAbility.mainWindow.setPreferredOrientation(Window.Orientation.AUTO_ROTATION_LANDSCAPE)
      .catch((err: BusinessError): void => {
        hilog.warn(LOG_DOMAIN, LOG_TAG, 'QAbility landscape orientation: ' + JSON.stringify(err));
      });
    QAbility.mainWindow.on('keyboardHeightChange', this.keyboardHeightChanged);
    QAbility.mainWindow.on('windowStatusChange', this.windowStatusChanged);
    if (this.foreground) {
      this.startKeyboardObserver();
    }
  }

  onWindowStageCreate(windowStage: Window.WindowStage) {
    hilog.info(LOG_DOMAIN, LOG_TAG, 'QAbility::onWindowStageCreate(): this.launchWant.parameters: ' + JSON.stringify(this.launchWant.parameters));
    try {
      this.registerMainWindow(windowStage);
    } catch (e) {
      hilog.warn(LOG_DOMAIN, LOG_TAG, 'QAbility main window unavailable: ' + JSON.stringify(e));
    }
    qpa.handleAbilityOnWindowStageCreate(this, windowStage);
    this.scheduleFullscreen();
  }

  onWindowStageRestore(windowStage: Window.WindowStage): void {
    this.registerMainWindow(windowStage);
    qpa.handleAbilityOnWindowStageRestore(this, windowStage);
    this.scheduleFullscreen();
  }

  onNewWant(want: Want, launchParam: AbilityConstant.LaunchParam): void {
    hilog.info(LOG_DOMAIN, LOG_TAG, 'QAbility::onNewWant(): want.parameters: ' + JSON.stringify(want.parameters));
    qpa.handleAbilityOnNewWant(this, want, launchParam);
  }

  onForeground() {
    hilog.info(LOG_DOMAIN, LOG_TAG, 'QAbility::onForeground()');
    this.foreground = true;
    this.startKeyboardObserver();
    this.mediaSession?.sync();
    QAbilityStage.scheduleProvisioning(this.context, QAbility.mainWindow);
    qpa.handleAbilityOnForeground(this);
    this.scheduleFullscreen();
  }

  onBackground() {
    hilog.info(LOG_DOMAIN, LOG_TAG, 'QAbility::onBackground()');
    this.foreground = false;
    this.stopKeyboardObserver();
    this.mediaSession?.sync();
    this.clearFullscreenTimers();
    qpa.handleAbilityOnBackground(this);
  }

  onWindowStageDestroy() {
    hilog.info(LOG_DOMAIN, LOG_TAG, 'QAbility::onStageDestroy()');
    mediaBridge.setKeyboardHeight(0);
    this.stopKeyboardObserver();
    this.keyboardStage = null;
    this.clearFullscreenTimers();
    if (QAbility.mainWindow !== null) {
      QAbility.mainWindow.off('keyboardHeightChange', this.keyboardHeightChanged);
      QAbility.mainWindow.off('windowStatusChange', this.windowStatusChanged);
    }
    QAbility.mainWindow = null;
    qpa.handleAbilityOnWindowStageDestroy(this);
  }

  onContinue(wantParam: Record<string, Object>): AbilityConstant.OnContinueResult | Promise<AbilityConstant.OnContinueResult> {
    return qpa.handleAbilityOnContinue(this, wantParam);
  }
}

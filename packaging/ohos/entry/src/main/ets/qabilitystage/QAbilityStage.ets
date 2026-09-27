import AbilityStage from '@ohos.app.ability.AbilityStage';
import QAbility from '../qability/QAbility';
import QChildProcess from '../process/QChildProcess';
import QtUtils from '../qability/QtUtils';
import Want from '@ohos.app.ability.Want';
import common from '@ohos.app.ability.common';
import util from '@ohos.util';
import hilog from '@ohos.hilog';
import fs from '@ohos.file.fs';
import qpa from 'libqohos.so';
import {APP_LIBRARY_NAME, LOG_DOMAIN, LOG_TAG} from '../common/QtAppConstants';

export default class QAbilityStage extends AbilityStage {

  // setting "appArgs" overrides arguments from initial Want object
  private static appArgs?: Array<string>;

  private static setupQtApplicationCalled: boolean = false;

  private static initQtAppContextImpl(appContext: common.ApplicationContext, abilityClassName: string): void {
    if (!QAbilityStage.setupQtApplicationCalled) {
      hilog.info(LOG_DOMAIN, LOG_TAG, 'QAbilityStage::initQtAppContextImpl: init');
      QAbilityStage.setupQtApplicationCalled = true;
      QAbilityStage.dumpMixxxDiag(appContext);
      if (!QAbilityStage.appArgs) {
        // Mixxx needs to find its bundled resources (res/ next to the app
        // library inside the HAP) and a writable settings directory inside
        // the app sandbox.
        QAbilityStage.appArgs = [
          '--resource-path', QAbilityStage.resolveMixxxResourceDir(appContext),
          '--settings-path', appContext.filesDir + '/.mixxx',
        ];
        hilog.info(LOG_DOMAIN, LOG_TAG,
          'QAbilityStage::initQtAppContextImpl: appArgs=' + JSON.stringify(QAbilityStage.appArgs));
      }
      qpa.setupQtApplication({
        appContext: appContext,
        modulesFactories: QtUtils.getModulesFactoriesMapForQt(),
        appName: APP_LIBRARY_NAME,
        appArgs: QAbilityStage.appArgs,
        abilityClassName: abilityClassName,
        _unusedQChildProcess: new QChildProcess(),
      });
    } else {
      hilog.info(LOG_DOMAIN, LOG_TAG, 'QAbilityStage::initQtAppContextImpl: already initialized');
    }
  }

  public static initQtAppContextIfNeeded(appContext: common.ApplicationContext): void {
    QAbilityStage.initQtAppContextImpl(appContext, QAbility.name);
  }

  // Locate the bundled Mixxx resource directory. resources/resfile/ is
  // delivered as real files per module; resourceDir can be empty on the
  // application context, hence the fallbacks.
  private static resolveMixxxResourceDir(appContext: common.ApplicationContext): string {
    const candidates: string[] = [
      appContext.resourceDir + '/res',
      appContext.bundleCodeDir + '/entry/resources/resfile/res',
      appContext.bundleCodeDir + '/resources/resfile/res',
    ];
    for (let i = 0; i < candidates.length; i++) {
      try {
        // require a known sub-directory: accessSync alone reports bogus hits
        if (fs.accessSync(candidates[i]) && fs.accessSync(candidates[i] + '/skins')) {
          return candidates[i];
        }
      } catch (e) {
        // try next
      }
    }
    return '';
  }

  // TEMPORARY diagnostic: report resource dir presence and Mixxx's own log.
  private static dumpMixxxDiag(appContext: common.ApplicationContext): void {
    try {
      const candidates: string[] = [
        appContext.resourceDir + '/res',
        appContext.bundleCodeDir + '/resources/resfile/res',
        appContext.bundleCodeDir + '/entry/resources/resfile/res',
        appContext.bundleCodeDir + '/res',
        appContext.filesDir + '/res',
      ];
      hilog.info(LOG_DOMAIN, LOG_TAG, 'MIXXX-DIAG resourceDir="' + appContext.resourceDir +
        '" bundleCodeDir="' + appContext.bundleCodeDir + '" filesDir="' + appContext.filesDir + '"');
      for (let i = 0; i < candidates.length; i++) {
        const c = candidates[i];
        let exists = false;
        let n = 0;
        try {
          exists = fs.accessSync(c);
          if (exists) { n = fs.listFileSync(c).length; }
        } catch (e) {
          exists = false;
        }
        hilog.info(LOG_DOMAIN, LOG_TAG, 'MIXXX-DIAG candidate[' + i + '] exists=' + exists + ' entries=' + n + ' ' + c);
      }
      const top = fs.listFileSync(appContext.bundleCodeDir);
      hilog.info(LOG_DOMAIN, LOG_TAG, 'MIXXX-DIAG bundleCodeDir entries=' + top.join(','));
    } catch (e) {
      hilog.error(LOG_DOMAIN, LOG_TAG, 'MIXXX-DIAG check failed: ' + JSON.stringify(e));
    }
    try {
      const logPath = appContext.filesDir + '/.mixxx/mixxx.log';
      if (fs.accessSync(logPath)) {
        const stat = fs.statSync(logPath);
        const size = stat.size as number;
        const readLen = size > 4000 ? 4000 : size;
        const buf = new ArrayBuffer(readLen);
        const file = fs.openSync(logPath, fs.OpenMode.READ_ONLY);
        fs.readSync(file.fd, buf, { offset: size - readLen, length: readLen });
        fs.closeSync(file);
        const text = new util.TextDecoder('utf-8').decodeToString(new Uint8Array(buf));
        const lines = text.split(String.fromCharCode(10));
        for (let i = 0; i < lines.length; i++) {
          if (lines[i].length > 0) {
            hilog.info(LOG_DOMAIN, LOG_TAG, 'MIXXX-LOG| ' + lines[i].substring(0, 220));
          }
        }
      } else {
        hilog.info(LOG_DOMAIN, LOG_TAG, 'MIXXX-DIAG no mixxx.log at ' + logPath);
      }
    } catch (e) {
      hilog.error(LOG_DOMAIN, LOG_TAG, 'MIXXX-DIAG log read failed: ' + JSON.stringify(e));
    }
  }

  onCreate(): void {
    hilog.info(LOG_DOMAIN, LOG_TAG, 'QAbilityStage::onCreate()');
    qpa.handleAbilityStageOnCreate(this);
  }

  onNewProcessRequest(want: Want): string {
    hilog.info(LOG_DOMAIN, LOG_TAG, 'QAbilityStage::onNewProcessRequest: want.parameters: ' + JSON.stringify(want.parameters));
    QAbilityStage.initQtAppContextIfNeeded(this.context.getApplicationContext());
    let processKey: string = qpa.handleAbilityStageOnNewProcessRequest(this, want);
    hilog.info(LOG_DOMAIN, LOG_TAG, 'QAbilityStage::onNewProcessRequest: processKey: "' + processKey + '"');
    return processKey;
  }

  onAcceptWant(want: Want): string {
    hilog.info(LOG_DOMAIN, LOG_TAG, 'QAbilityStage::onAcceptWant: want.parameters: ' + JSON.stringify(want.parameters));
    QAbilityStage.initQtAppContextIfNeeded(this.context.getApplicationContext());
    let instanceKey: string = qpa.handleAbilityStageOnAcceptWant(this, want);
    hilog.info(LOG_DOMAIN, LOG_TAG, 'QAbilityStage::onAcceptWant: instanceKey: "' + instanceKey + '"');
    return instanceKey;
  }

  onDestroy() {
    hilog.info(LOG_DOMAIN, LOG_TAG, 'QAbilityStage::onDestroy()');
    qpa.handleAbilityStageOnDestroy(this);
  }
}

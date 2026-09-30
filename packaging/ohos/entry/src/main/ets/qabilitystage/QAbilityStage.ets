import AbilityStage from '@ohos.app.ability.AbilityStage';
import QAbility from '../qability/QAbility';
import QChildProcess from '../process/QChildProcess';
import QtUtils from '../qability/QtUtils';
import Want from '@ohos.app.ability.Want';
import bundleManager from '@ohos.bundle.bundleManager';
import common from '@ohos.app.ability.common';
import { BusinessError } from '@ohos.base';
import fileUri from '@ohos.file.fileuri';
import picker from '@ohos.file.picker';
import util from '@ohos.util';
import window from '@ohos.window';
import hilog from '../common/RuntimeLog';
import fs from '@ohos.file.fs';
import qpa from 'libqohos.so';
import {APP_LIBRARY_NAME, LOG_DOMAIN, LOG_TAG} from '../common/QtAppConstants';

interface ImportedProfile {
  id: string;
  state: string;
}

export default class QAbilityStage extends AbilityStage {

  // setting "appArgs" overrides arguments from initial Want object
  private static appArgs?: Array<string>;

  private static setupQtApplicationCalled: boolean = false;
  private static provisioningScheduled: boolean = false;

  private static initQtAppContextImpl(appContext: common.ApplicationContext, abilityClassName: string): void {
    if (!QAbilityStage.setupQtApplicationCalled) {
      hilog.info(LOG_DOMAIN, LOG_TAG, 'QAbilityStage::initQtAppContextImpl: init');
      QAbilityStage.setupQtApplicationCalled = true;
      hilog.initialize(appContext.filesDir);
      const settingsDir = QAbilityStage.resolveSettingsDir(appContext.filesDir);
      QAbilityStage.seedMixxxSettings(appContext.filesDir, settingsDir);
      if (!QAbilityStage.appArgs) {
        // Mixxx needs to find its bundled resources (res/ next to the app
        // library inside the HAP) and a writable settings directory inside
        // the app sandbox.
        const args: Array<string> = [
          '--resource-path', QAbilityStage.resolveMixxxResourceDir(appContext),
          '--settings-path', settingsDir,
          '--full-screen',
          '--rescan-library',
          '--log-max-file-size', '10000000',
          '--log-flush-level', 'info',
        ];
        const mediaRoot = QAbilityStage.resolveMediaRoot(appContext);
        if (mediaRoot.length > 0) {
          args.push('--media-path', mediaRoot);
        }
        QAbilityStage.appArgs = args;
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


  // The app-scoped Download directory: Download/<bundleName>/. HarmonyOS
  // grants every app access to its own folder there without a permission
  // prompt, which makes it the only place Mixxx may actually read music from,
  // and it stays visible to the user through the system file manager.
  private static resolveDownloadRoot(): string {
    try {
      const bundleInfo = bundleManager.getBundleInfoForSelfSync(
        bundleManager.BundleFlag.GET_BUNDLE_INFO_WITH_APPLICATION);
      // The file://docs/ authority is the media library provider; resolving
      // the path through FileUri yields the location the sandbox may open.
      const uri = 'file://docs/storage/Users/currentUser/Download/' + bundleInfo.name;
      const path = new fileUri.FileUri(uri).path;
      hilog.info(LOG_DOMAIN, LOG_TAG, 'MIXXX-DIAG download root="' + path + '"');
      return path;
    } catch (e) {
      hilog.error(LOG_DOMAIN, LOG_TAG, 'MIXXX-DIAG download root failed: ' + JSON.stringify(e));
      return '';
    }
  }

  // The docs-backed provider creates app-scoped children with a direct mkdir,
  // but its recursive form fails while the parent does not exist yet, so each
  // level is created on its own.
  private static ensureDirectory(path: string): boolean {
    if (path.length === 0) {
      return false;
    }
    try {
      if (fs.accessSync(path) && fs.statSync(path).isDirectory()) {
        return true;
      }
    } catch (e) {
      hilog.info(LOG_DOMAIN, LOG_TAG, 'MIXXX-DIAG "' + path + '" not accessible: ' + JSON.stringify(e));
    }
    try {
      fs.mkdirSync(path);
    } catch (e) {
      hilog.warn(LOG_DOMAIN, LOG_TAG, 'MIXXX-DIAG mkdir "' + path + '" failed: ' + JSON.stringify(e));
    }
    try {
      return fs.accessSync(path) && fs.statSync(path).isDirectory();
    } catch (e) {
      hilog.warn(LOG_DOMAIN, LOG_TAG, 'MIXXX-DIAG "' + path + '" still inaccessible: ' + JSON.stringify(e));
      return false;
    }
  }

  // The app-scoped Download directory is not writable until the system has
  // provisioned it once per install. DOWNLOAD picker mode is that provisioning
  // API rather than a chooser: it returns Download/<bundleName> and installs
  // the persistent access grant, which is what makes plain fs access work
  // afterwards. Only a UIAbilityContext is accepted here.
  // Returns true once the app-scoped Download directory is usable.
  public static async provisionMediaRoot(
      context: common.UIAbilityContext, mainWindow: window.Window | null): Promise<boolean> {
    const downloadRoot = QAbilityStage.resolveDownloadRoot();
    if (downloadRoot.length === 0) {
      return false;
    }
    if (QAbilityStage.ensureDirectory(downloadRoot) &&
        QAbilityStage.ensureDirectory(downloadRoot + '/Music')) {
      hilog.info(LOG_DOMAIN, LOG_TAG, 'MIXXX-DIAG Music dir already usable');
      hilog.exportTo(downloadRoot + '/logs');
      return true;
    }
    try {
      // The picker needs the ability window: built from the context alone it
      // logs "ParseWindow: not window mode" and refuses the request.
      const documentPicker = mainWindow !== null
        ? new picker.DocumentViewPicker(context, mainWindow)
        : new picker.DocumentViewPicker(context);
      const options = new picker.DocumentSaveOptions();
      options.pickerMode = picker.DocumentPickerMode.DOWNLOAD;
      const uris = await documentPicker.save(options);
      if (uris.length === 0 || !uris[0]) {
        hilog.warn(LOG_DOMAIN, LOG_TAG, 'MIXXX-DIAG Download provisioning returned nothing');
        return false;
      }
      const provisioned = new fileUri.FileUri(uris[0]).path;
      const ready = QAbilityStage.ensureDirectory(provisioned) &&
        QAbilityStage.ensureDirectory(provisioned + '/Music');
      if (ready) {
        hilog.exportTo(provisioned + '/logs');
        QAbilityStage.ensureDirectory(provisioned + '/Music/Mixxx');
        QAbilityStage.ensureDirectory(provisioned + '/Music/Mixxx/Recordings');
      }
      hilog.info(LOG_DOMAIN, LOG_TAG,
        'MIXXX-DIAG Download provisioned: "' + provisioned + '" ready=' + ready);
      return ready;
    } catch (e) {
      hilog.warn(LOG_DOMAIN, LOG_TAG, 'MIXXX-DIAG Download provisioning failed: ' + JSON.stringify(e));
      return false;
    }
  }

  // The picker resolves the window's ArkUI content, which exists only after the
  // Qt window has finished loading, so the first attempt cannot succeed: retry
  // a few times with growing delay and stop at the first success.
  private static readonly provisionDelays: Array<number> = [2000, 6000, 12000];

  public static scheduleProvisioning(
      context: common.UIAbilityContext, mainWindow: window.Window | null): void {
    if (QAbilityStage.provisioningScheduled) {
      return;
    }
    QAbilityStage.provisioningScheduled = true;
    const attempt = (index: number): void => {
      if (index >= QAbilityStage.provisionDelays.length) {
        hilog.error(LOG_DOMAIN, LOG_TAG, 'MIXXX-DIAG Download provisioning gave up');
        QAbilityStage.provisioningScheduled = false;
        return;
      }
      setTimeout((): void => {
        QAbilityStage.provisionMediaRoot(context, mainWindow)
          .then((ready: boolean): void => {
            if (!ready) {
              attempt(index + 1);
            }
          })
          .catch((err: BusinessError): void => {
            hilog.error(LOG_DOMAIN, LOG_TAG, 'MIXXX-DIAG provisioning rejected: ' + JSON.stringify(err));
            attempt(index + 1);
          });
      }, QAbilityStage.provisionDelays[index]);
    };
    attempt(0);
  }

  private static resolveMediaRoot(appContext: common.ApplicationContext): string {
    const downloadRoot = QAbilityStage.resolveDownloadRoot();
    if (downloadRoot.length > 0) {
      const musicRoot = downloadRoot + '/Music';
      const ready = QAbilityStage.ensureDirectory(downloadRoot) &&
        QAbilityStage.ensureDirectory(musicRoot);
      if (ready) {
        hilog.exportTo(downloadRoot + '/logs');
      }
      hilog.info(LOG_DOMAIN, LOG_TAG,
        'MIXXX-DIAG media root="' + musicRoot + '" ready=' + ready);
      return musicRoot;
    }
    // Fall back to the app sandbox: always writable, but the user cannot drop
    // files into it from outside the app.
    const fallback = appContext.filesDir + '/Music';
    if (QAbilityStage.ensureDirectory(fallback)) {
      hilog.warn(LOG_DOMAIN, LOG_TAG,
        'MIXXX-DIAG download root unusable, falling back to "' + fallback + '"');
      return fallback;
    }
    hilog.error(LOG_DOMAIN, LOG_TAG, 'MIXXX-DIAG no usable media root');
    return '';
  }

  // Mixxx reads its scale factor and sound configuration from
  // <settings>/mixxx.cfg. A fresh minimal config makes Mixxx pick sensible
  // defaults (0.75 scale so the 1920x1080 layout fits the tablet) and lets
  // the sound manager auto-select the OHAudio host API on startup.
  // Mixxx writes a setting only once something has changed it, so the values
  // the port depends on are inserted into the [Config] section when missing.
  // Existing values are never overwritten, which keeps Preferences authoritative.
  private static ensureConfigEntries(settingsDir: string, entries: Array<string>): void {
    const cfgPath = settingsDir + '/mixxx.cfg';
    const separator = String.fromCharCode(10);
    let text = '';
    try {
      if (fs.accessSync(cfgPath)) {
        const stat = fs.statSync(cfgPath);
        const size = stat.size as number;
        const buf = new ArrayBuffer(size);
        const f = fs.openSync(cfgPath, fs.OpenMode.READ_ONLY);
        fs.readSync(f.fd, buf, { offset: 0, length: size });
        fs.closeSync(f);
        text = new util.TextDecoder('utf-8').decodeToString(new Uint8Array(buf));
      }
    } catch (e) {
      hilog.warn(LOG_DOMAIN, LOG_TAG, 'MIXXX-DIAG cfg read failed: ' + JSON.stringify(e));
      return;
    }
    const lines: Array<string> = text.length > 0 ? text.split(separator) : ['[Config]'];
    let header = -1;
    for (let i = 0; i < lines.length; i++) {
      if (lines[i].trim() === '[Config]') {
        header = i;
        break;
      }
    }
    if (header < 0) {
      lines.unshift('[Config]');
      header = 0;
    }
    let sectionEnd = lines.length;
    for (let i = header + 1; i < lines.length; i++) {
      if (lines[i].trim().startsWith('[')) {
        sectionEnd = i;
        break;
      }
    }
    let changed = false;
    const missing: Array<string> = [];
    for (let i = 0; i < entries.length; i++) {
      const split = entries[i].indexOf(' ');
      const key = entries[i].substring(0, split);
      const defaultValue = entries[i].substring(split + 1);
      let foundLine = -1;
      let legacyLine = -1;
      for (let j = header + 1; j < sectionEnd; j++) {
        const line = lines[j].trim();
        if (line.split(/\s+/)[0] === key) {
          foundLine = j;
        }
        if (line.startsWith(key + '=')) {
          legacyLine = j;
        }
      }
      if (foundLine < 0 && legacyLine < 0) {
        missing.push(entries[i]);
        continue;
      }
      if (foundLine >= 0 && legacyLine >= 0) {
        lines[legacyLine] = '';
        changed = true;
      }
      const index = foundLine >= 0 ? foundLine : legacyLine;
      const line = lines[index].trim();
      let value = line.substring(key.length + (foundLine >= 0 ? 0 : 1)).trim();
      if ((defaultValue === '0' || defaultValue === '1') &&
          (value === 'false' || value === 'true')) {
        value = value === 'true' ? '1' : '0';
      }
      const normalized = key + ' ' + value;
      if (line !== normalized) {
        lines[index] = normalized;
        changed = true;
      }
    }
    if (missing.length === 0 && !changed) {
      return;
    }
    for (let i = missing.length - 1; i >= 0; i--) {
      lines.splice(header + 1, 0, missing[i]);
    }
    try {
      const f = fs.openSync(cfgPath, fs.OpenMode.CREATE | fs.OpenMode.READ_WRITE | fs.OpenMode.TRUNC);
      fs.writeSync(f.fd, lines.join(separator) + separator);
      fs.closeSync(f);
      hilog.info(LOG_DOMAIN, LOG_TAG, 'MIXXX-DIAG seeded cfg entries: ' + missing.join(','));
    } catch (e) {
      hilog.error(LOG_DOMAIN, LOG_TAG, 'MIXXX-DIAG cfg write failed: ' + JSON.stringify(e));
    }
  }

  private static resolveSettingsDir(filesDir: string): string {
    try {
      const id = fs.readTextSync(filesDir + '/active-profile').trim();
      if (/^[a-f0-9]{32}$/.test(id)) {
        const candidate = filesDir + '/profiles/' + id;
        if (fs.accessSync(candidate + '/mixxx.cfg') && fs.accessSync(candidate + '/import.json')) {
          const report: ImportedProfile = JSON.parse(fs.readTextSync(candidate + '/import.json')) as ImportedProfile;
          if (report.state === 'ready' && report.id === id) {
            hilog.info(LOG_DOMAIN, LOG_TAG, 'QAbilityStage selected imported profile ' + id);
            return candidate;
          }
        }
      }
    } catch (_) {}
    return filesDir + '/.mixxx';
  }

  private static seedMixxxSettings(filesDir: string, settingsDir: string): void {
    try {
      if (!fs.accessSync(settingsDir)) {
        fs.mkdirSync(settingsDir);
      }
    } catch (e) {
      hilog.error(LOG_DOMAIN, LOG_TAG, 'MIXXX-DIAG mkdir settings failed: ' + JSON.stringify(e));
    }
    QAbilityStage.ensureDirectory(settingsDir + '/effects');
    QAbilityStage.ensureDirectory(settingsDir + '/effects/chains');
    QAbilityStage.ensureConfigEntries(settingsDir, [
      'ScaleFactor 0.75',
      'hide_menubar 0',
      'show_menubar_hint 0',
    ]);
    // App-owned music folder: Mixxx can scan it without any picker/permission.
    try {
      const musicDir = filesDir + '/Music';
      if (!fs.accessSync(musicDir)) {
        fs.mkdirSync(musicDir);
        hilog.info(LOG_DOMAIN, LOG_TAG, 'MIXXX-DIAG created ' + musicDir);
      }
    } catch (e) {
      hilog.error(LOG_DOMAIN, LOG_TAG, 'MIXXX-DIAG music dir failed: ' + JSON.stringify(e));
    }
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

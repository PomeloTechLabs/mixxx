import hilog from '@ohos.hilog';
import fs from '@ohos.file.fs';

export default class RuntimeLog {
  private static path: string = '';
  private static publicPath: string = '';
  private static readonly maxSize: number = 10000000;

  private static rotate(path: string): void {
    for (let i = 9; i >= 0; i--) {
      const current = i === 0 ? path : path + '.' + i;
      const next = path + '.' + (i + 1);
      if (fs.accessSync(current)) {
        if (fs.accessSync(next)) {
          fs.unlinkSync(next);
        }
        fs.renameSync(current, next);
      }
    }
  }

  public static initialize(filesDir: string): void {
    if (RuntimeLog.path.length > 0) {
      return;
    }
    try {
      const directory = filesDir + '/.mixxx';
      if (!fs.accessSync(directory)) {
        fs.mkdirSync(directory);
      }
      const path = directory + '/ability.log';
      RuntimeLog.rotate(path);
      const file = fs.openSync(path, fs.OpenMode.CREATE | fs.OpenMode.READ_WRITE | fs.OpenMode.TRUNC);
      fs.closeSync(file);
      RuntimeLog.path = path;
      RuntimeLog.write('I', 'Runtime log started ' + new Date().toISOString());
    } catch (err) {
      hilog.error(0, 'mixxxohos', 'Runtime log initialization failed: %{public}s', JSON.stringify(err));
    }
  }

  public static exportTo(directory: string): void {
    if (RuntimeLog.path.length === 0 || RuntimeLog.publicPath.length > 0) {
      return;
    }
    try {
      if (!fs.accessSync(directory)) {
        fs.mkdirSync(directory);
      }
      const path = directory + '/ability.log';
      RuntimeLog.rotate(path);
      fs.copyFileSync(RuntimeLog.path, path);
      RuntimeLog.publicPath = path;
      RuntimeLog.write('I', 'Runtime log exported to ' + directory);
    } catch (err) {
      hilog.warn(0, 'mixxxohos', 'Runtime log export failed: %{public}s', JSON.stringify(err));
    }
  }

  private static append(path: string, line: string): void {
    if (path.length === 0 || fs.statSync(path).size >= RuntimeLog.maxSize) {
      return;
    }
    const file = fs.openSync(path, fs.OpenMode.READ_WRITE | fs.OpenMode.APPEND);
    try {
      fs.writeSync(file.fd, line);
    } finally {
      fs.closeSync(file);
    }
  }

  private static write(level: string, message: string, args: Array<string | number> = []): void {
    const line = new Date().toISOString() + ' ' + level + ' ' + message +
      (args.length > 0 ? ' ' + args.join(' ') : '') + '\n';
    try {
      RuntimeLog.append(RuntimeLog.path, line);
      RuntimeLog.append(RuntimeLog.publicPath, line);
    } catch (err) {
      hilog.warn(0, 'mixxxohos', 'Runtime log write failed: %{public}s', JSON.stringify(err));
    }
  }

  public static info(domain: number, tag: string, message: string, ...args: Array<string | number>): void {
    hilog.info(domain, tag, message, ...args);
    RuntimeLog.write('I', message, args);
  }

  public static warn(domain: number, tag: string, message: string, ...args: Array<string | number>): void {
    hilog.warn(domain, tag, message, ...args);
    RuntimeLog.write('W', message, args);
  }

  public static error(domain: number, tag: string, message: string, ...args: Array<string | number>): void {
    hilog.error(domain, tag, message, ...args);
    RuntimeLog.write('E', message, args);
  }
}

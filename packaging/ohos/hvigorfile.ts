import { appTasks } from '@ohos/hvigor-ohos-plugin';

const { syncVersion } = require('./version.cjs');
const version = syncVersion();
console.info(`[version] ${version.versionName} (${version.versionCode}), upstream ${version.upstreamVersion}`);

export default {
    system: appTasks,  /* Built-in plugin of Hvigor. It cannot be modified. */
    plugins:[]         /* Custom plugin to extend the functionality of Hvigor. */
}

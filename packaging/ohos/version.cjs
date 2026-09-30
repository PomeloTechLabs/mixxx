const fs = require('node:fs');
const path = require('node:path');

function readVersion(repositoryRoot) {
    const cmake = fs.readFileSync(path.join(repositoryRoot, 'CMakeLists.txt'), 'utf8');
    const project = cmake.match(/^\s*project\(mixxx\s+VERSION\s+(\d+)\.(\d+)\.(\d+)\s/m);
    const prerelease = cmake.match(/^\s*set\(MIXXX_VERSION_PRERELEASE\s+"([^"]*)"\)/m);
    if (!project || !prerelease) {
        throw new Error('Cannot read upstream Mixxx version from CMakeLists.txt');
    }
    const { revision } = JSON.parse(fs.readFileSync(path.join(repositoryRoot, 'packaging/ohos/version.json'), 'utf8'));
    const [major, minor, patch] = project.slice(1).map(Number);
    const suffix = prerelease[1];
    const stages = { alpha: 0, beta: 1, rc: 2, '': 9 };
    if (!Number.isInteger(revision) || revision < 0 || revision > 999 ||
            major > 20 || minor > 99 || patch > 99 || !Object.hasOwn(stages, suffix)) {
        throw new Error('OHOS version components out of range or unsupported upstream prerelease');
    }
    const upstreamNumber = `${major}.${minor}.${patch}`;
    const ending = suffix ? `-${suffix}` : '';
    const versionName = `${upstreamNumber}.${revision}${ending}`;
    const versionCode = major * 100000000 + minor * 1000000 + patch * 10000 + stages[suffix] * 1000 + revision;
    return { upstreamVersion: upstreamNumber + ending, revision, versionName, versionCode };
}

function syncVersion(repositoryRoot = path.resolve(__dirname, '../..')) {
    const version = readVersion(repositoryRoot);
    const manifestPath = path.join(repositoryRoot, 'packaging/ohos/AppScope/app.json5');
    const original = fs.readFileSync(manifestPath, 'utf8');
    const matchesCode = original.match(/"versionCode"\s*:\s*\d+/g) || [];
    const matchesName = original.match(/"versionName"\s*:\s*"[^"]*"/g) || [];
    if (matchesCode.length !== 1 || matchesName.length !== 1) {
        throw new Error('Expected one versionName and versionCode in app.json5');
    }
    const updated = original
        .replace(/("versionCode"\s*:\s*)\d+/, (_, prefix) => `${prefix}${version.versionCode}`)
        .replace(/("versionName"\s*:\s*)"[^"]*"/, (_, prefix) => `${prefix}"${version.versionName}"`);
    if (updated !== original) fs.writeFileSync(manifestPath, updated, 'utf8');
    return version;
}

module.exports = { readVersion, syncVersion };
if (require.main === module) {
    console.log(JSON.stringify(syncVersion(), null, 2));
}

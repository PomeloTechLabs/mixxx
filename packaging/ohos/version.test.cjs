const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { test } = require('node:test');
const { readVersion, syncVersion } = require('./version.cjs');

function removeFixture(root) {
    const resolved = path.resolve(root);
    assert.equal(path.dirname(resolved), path.resolve(os.tmpdir()));
    assert(path.basename(resolved).startsWith('mixxx-version-'));
    fs.rmSync(resolved, { recursive: true, force: true });
}

test('upstream version and local revision drive the manifest', () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'mixxx-version-'));
    try {
        fs.mkdirSync(path.join(root, 'packaging/ohos/AppScope'), { recursive: true });
        const cmake = path.join(root, 'CMakeLists.txt');
        const revision = path.join(root, 'packaging/ohos/version.json');
        const manifest = path.join(root, 'packaging/ohos/AppScope/app.json5');
        fs.writeFileSync(cmake, 'project(mixxx VERSION 2.7.0 LANGUAGES C CXX)\nset(MIXXX_VERSION_PRERELEASE "alpha")\n');
        fs.writeFileSync(revision, '{"revision":1}');
        fs.writeFileSync(manifest, '{"app":{"bundleName":"com.pomelo.mixxx","versionCode":1000000,"versionName":"1.0.0"}}');
        const initial = syncVersion(root);
        assert.equal(initial.versionName, '2.7.0.1-alpha');
        assert.equal(initial.upstreamVersion, '2.7.0-alpha');
        assert(initial.versionCode > 1000000);
        fs.writeFileSync(revision, '{"revision":2}');
        const next = syncVersion(root);
        assert.equal(next.versionName, '2.7.0.2-alpha');
        assert(next.versionCode > initial.versionCode);
        fs.writeFileSync(cmake, 'project(mixxx VERSION 2.8.0 LANGUAGES C CXX)\nset(MIXXX_VERSION_PRERELEASE "")\n');
        fs.writeFileSync(revision, '{"revision":1}');
        const upstream = syncVersion(root);
        assert.equal(upstream.versionName, '2.8.0.1');
        assert(upstream.versionCode > next.versionCode);
        assert.equal(JSON.parse(fs.readFileSync(manifest)).app.bundleName, 'com.pomelo.mixxx');
        assert.equal(JSON.parse(fs.readFileSync(manifest)).app.versionName, upstream.versionName);
        fs.writeFileSync(revision, '{"revision":1000}');
        assert.throws(() => readVersion(root), /out of range/);
    } finally {
        removeFixture(root);
    }
});

test('alpha, beta, rc and stable upgrades stay ordered when revisions reset', () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'mixxx-version-stages-'));
    try {
        fs.mkdirSync(path.join(root, 'packaging/ohos'), { recursive: true });
        let last = 0;
        for (const [version, suffix, revision] of [
                ['2.7.0', 'alpha', 999], ['2.7.0', 'beta', 0], ['2.7.0', 'rc', 0],
                ['2.7.0', '', 999], ['2.7.1', 'alpha', 0]]) {
            fs.writeFileSync(path.join(root, 'CMakeLists.txt'), `project(mixxx VERSION ${version} LANGUAGES C CXX)\nset(MIXXX_VERSION_PRERELEASE "${suffix}")\n`);
            fs.writeFileSync(path.join(root, 'packaging/ohos/version.json'), JSON.stringify({ revision }));
            const code = readVersion(root).versionCode;
            assert(code > last && code <= 2147483647);
            last = code;
        }
    } finally {
        removeFixture(root);
    }
});

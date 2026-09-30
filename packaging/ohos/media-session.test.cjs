const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { test } = require('node:test');
const ts = require(process.env.MIXXX_TYPESCRIPT_PATH || 'typescript');

const source = fs.readFileSync(path.join(__dirname, 'entry/src/main/ets/media/MixxxMediaSession.ets'), 'utf8');
const compiled = ts.transpileModule(source, {
  compilerOptions: { target: ts.ScriptTarget.ES2020, module: ts.ModuleKind.CommonJS, esModuleInterop: true }
}).outputText;

function deferred() {
  let resolve;
  let reject;
  const promise = new Promise((ok, fail) => { resolve = ok; reject = fail; });
  return { promise, resolve, reject };
}

async function fixture() {
  let state = { ready: false, playing: false, assetId: '', title: '', duration: 0 };
  let now = 1000000;
  const maps = [];
  const metadata = [];
  const playback = [];
  const decodes = [];
  const reads = [];
  let pendingDecode;
  let rejectMetadata = false;
  let mismatch = false;
  let pendingCreate;
  let failDestroy = false;
  let failDeactivate = false;
  let deferredCommands = false;
  const sessions = [];
  const commands = [];
  const background = [];
  let releasedSources = 0;
  const makeSession = () => {
    const session = {
      sessionId: String(sessions.length), active: false, destroyed: false, events: {}, activations: 0,
      on(name, handler) { this.events[name] = handler; }, async setLaunchAbility() {},
      async activate() { assert.equal(this.destroyed, false); this.active = true; this.activations++; },
      async deactivate() {
        if (failDeactivate) { throw { code: 4, message: 'temporary deactivate failure' }; }
        this.active = false;
      },
      async setAVMetadata(value) {
        assert.equal(this.destroyed, false);
        if (rejectMetadata) { throw { code: 1, message: 'temporary metadata failure' }; }
        metadata.push(value);
      },
      async setAVPlaybackState(value) { assert.equal(this.destroyed, false); playback.push(value); },
      async destroy() {
        if (failDestroy) { throw { code: 3, message: 'temporary destroy failure' }; }
        this.destroyed = true; this.active = false;
      }
    };
    sessions.push(session);
    return session;
  };
  const makeMap = (label) => {
    const map = { label, released: 0, async release() { this.released++; } };
    maps.push(map);
    return map;
  };
  const modules = {
    '@ohos.multimedia.avsession': {
      async createAVSession() {
        const session = makeSession();
        if (pendingCreate) {
          const gate = pendingCreate;
          pendingCreate = undefined;
          await gate.promise;
        }
        return session;
      },
      PlaybackState: { PLAYBACK_STATE_PLAY: 1, PLAYBACK_STATE_PAUSE: 2, PLAYBACK_STATE_STOP: 6 }
    },
    '@ohos.multimedia.image': {
      createImageSource(bytes) {
        const label = new Uint8Array(bytes)[0];
        return {
          async createPixelMap() {
            decodes.push(label);
            if (pendingDecode) {
              const gate = pendingDecode;
              pendingDecode = undefined;
              return gate.promise;
            }
            if (label === 255) { throw { code: 2, message: 'invalid image' }; }
            return makeMap(label);
          },
          async release() { releasedSources++; }
        };
      }
    },
    '@ohos.resourceschedule.backgroundTaskManager': {
      BackgroundMode: { AUDIO_PLAYBACK: 1 },
      async startBackgroundRunning() { background.push(true); },
      async stopBackgroundRunning() { background.push(false); }

    },
    '@ohos.app.ability.wantAgent': {
      async getWantAgent() { return {}; },
      OperationType: { START_ABILITY: 0 }, WantAgentFlags: { UPDATE_PRESENT_FLAG: 0 }
    },
    '../common/RuntimeLog': { info() {}, warn() {}, error() {} },
    '../common/QtAppConstants': { LOG_DOMAIN: 0, LOG_TAG: 'test' },
    'libmixxxohosmedia.so': {
      readState() { return JSON.stringify(state); },
      readArtwork(key) {
        reads.push(key);
        return new Uint8Array(mismatch || key !== state.artworkKey ? [] : [state.image]).buffer;
      },
      sendCommand(command) {
        commands.push(command);
        if (!deferredCommands) {
          if (command === 'stop' || command === 'pause') { state.playing = false; }
          if (command === 'play') { state.playing = true; }
        }
        return true;
      }
    }
  };
  const sandbox = {
    exports: {}, require(name) { assert.ok(modules[name], name); return modules[name]; },
    Date: { now() { return now; } },
    setInterval() { return 1; }, clearInterval() {}, setTimeout() {},
    canIUse() { return true; }, $r() { return { id: 1 }; }
  };
  vm.runInNewContext(compiled, sandbox, { filename: 'MixxxMediaSession.js' });
  const context = { resourceManager: { async getMediaContent() { return new Uint8Array([0]); } } };
  const media = new sandbox.exports.default(context);
  await media.initialized;
  if (media.syncOperation) { await media.syncOperation; }
  return {
    media, maps, metadata, playback, decodes, reads, makeMap, sessions, commands, background,
    get releasedSources() { return releasedSources; },
    get destroyed() { return sessions.every(session => session.destroyed); },
    track(assetId, artworkKey, image) {
      state = { ready: true, playing: true, loading: false, assetId, artworkKey, image, artworkAvailable: image !== undefined,
        title: assetId, artist: '', album: '', duration: 60000, position: 1000 };
    },
    delayDecode() { pendingDecode = deferred(); return pendingDecode; },
    delayCreate() { pendingCreate = deferred(); return pendingCreate; },
    advance(ms) { now += ms; },
    patch(value) { Object.assign(state, value); },
    empty() { state = { ready: true, playing: false, loading: false, assetId: '', title: '', duration: 0 }; },
    deferCommands(value) { deferredCommands = value; },
    failDestroy(value) { failDestroy = value; },
    failDeactivate(value) { failDeactivate = value; },
    failMetadata(value) { rejectMetadata = value; },
    mismatch(value) { mismatch = value; }
  };
}

async function until(condition) {
  for (let i = 0; i < 100; i++) {
    if (condition()) { return; }
    await Promise.resolve();
  }
  assert.fail('async operation did not reach the expected gate');
}

test('covers follow songs, update on the same song and fall back without decoding on position polls', async () => {
  const f = await fixture();
  f.track('A', '1', 1);
  await f.media.sync();
  const first = f.metadata.at(-1).mediaImage;
  assert.equal(first.label, 1);
  await f.media.sync();
  assert.equal(f.metadata.length, 1);
  assert.deepEqual(f.reads, ['1']);
  f.track('A', '2', 2);
  await f.media.sync();
  assert.equal(f.metadata.at(-1).mediaImage.label, 2);
  assert.equal(first.released, 1);
  f.track('B', '3');
  await f.media.sync();
  assert.equal(f.metadata.at(-1).assetId, 'B');
  assert.equal(f.metadata.at(-1).mediaImage.label, 0);
  await f.media.destroy();
  assert.ok(f.maps.every(map => map.released === 1));
  assert.equal(f.releasedSources, f.decodes.length);
});

test('a song changed during decoding never publishes the late cover', async () => {
  const f = await fixture();
  f.track('A', '1', 1);
  const gate = f.delayDecode();
  const operation = f.media.sync();
  await until(() => f.decodes.includes(1));
  f.track('B', '2', 2);
  const late = f.makeMap(1);
  gate.resolve(late);
  await operation;
  assert.equal(f.metadata.length, 0);
  assert.equal(late.released, 1);
  await f.media.sync();
  assert.equal(f.metadata.at(-1).assetId, 'B');
  assert.equal(f.metadata.at(-1).mediaImage.label, 2);
  await f.media.destroy();
});

test('snapshot mismatch retries; unreadable artwork clears the old cover and keeps playback updates', async () => {
  const f = await fixture();
  f.track('A', '1', 1);
  f.mismatch(true);
  await f.media.sync();
  assert.equal(f.metadata.length, 0);
  assert.equal(f.playback.length, 1);
  f.mismatch(false);
  await f.media.sync();
  const old = f.metadata.at(-1).mediaImage;
  f.track('B', '2', 255);
  await f.media.sync();
  assert.equal(f.metadata.at(-1).mediaImage.label, 0);
  assert.equal(old.released, 1);
  const count = f.decodes.length;
  await f.media.sync();
  assert.equal(f.decodes.length, count);
  await f.media.destroy();
});

test('failed metadata releases the candidate and retries while retaining the published cover', async () => {
  const f = await fixture();
  f.track('A', '1', 1);
  await f.media.sync();
  const old = f.metadata.at(-1).mediaImage;
  f.track('B', '2', 2);
  f.failMetadata(true);
  await f.media.sync();
  assert.equal(old.released, 0);
  assert.equal(f.maps.at(-1).released, 1);
  f.failMetadata(false);
  await f.media.sync();
  assert.equal(f.metadata.at(-1).assetId, 'B');
  assert.equal(old.released, 1);
  await f.media.destroy();
  assert.ok(f.maps.every(map => map.released === 1));
});

test('destroy waits for an in-flight decode and releases all images without publishing after close', async () => {
  const f = await fixture();
  f.track('A', '1', 1);
  const gate = f.delayDecode();
  const sync = f.media.sync();
  await until(() => f.decodes.includes(1));
  const closing = f.media.destroy();
  const late = f.makeMap(1);
  gate.resolve(late);
  await Promise.all([sync, closing]);
  assert.equal(f.metadata.length, 0);
  assert.deepEqual(f.playback.map(state => state.state), [6]);
  assert.equal(f.destroyed, true);
  assert.ok(f.maps.every(map => map.released === 1));
  assert.equal(f.releasedSources, f.decodes.length);
});

test('startup and loading a paused song never create a live media session', async () => {
  const f = await fixture();
  assert.equal(f.sessions.length, 0);
  f.track('A', '1', 1);
  f.patch({ playing: false });
  await f.media.sync();
  f.advance(300000);
  await f.media.sync();
  assert.equal(f.sessions.length, 0);
  assert.deepEqual(f.background, []);
  await f.media.destroy();
});

test('pause keeps controls for ten minutes then deactivates and destroys the idle session', async () => {
  const f = await fixture();
  f.track('A', '1', 1);
  await f.media.sync();
  const cover = f.metadata.at(-1).mediaImage;
  f.patch({ playing: false });
  await f.media.sync();
  const pausedCount = f.playback.length;
  for (let i = 0; i < 799; i++) {
    f.advance(750);
    await f.media.sync();
  }
  assert.equal(f.sessions[0].active, true);
  assert.equal(f.playback.length, pausedCount);
  f.advance(750);
  await f.media.sync();
  assert.equal(f.playback.at(-1).state, 6);
  assert.equal(f.sessions[0].destroyed, true);
  assert.equal(f.sessions[0].active, false);
  assert.equal(cover.released, 1);
  assert.deepEqual(f.background, [true, false]);
  await f.media.destroy();
  assert.ok(f.maps.every(map => map.released === 1));
});

test('paused seeking and song metadata updates do not restart the idle deadline', async () => {
  const f = await fixture();
  f.track('A', '1', 1);
  await f.media.sync();
  f.patch({ playing: false });
  await f.media.sync();
  f.advance(599000);
  f.track('B', '2', 2);
  f.patch({ playing: false, position: 9000 });
  await f.media.sync();
  assert.equal(f.metadata.at(-1).assetId, 'B');
  assert.equal(f.playback.at(-1).position.elapsedTime, 9000);
  f.advance(1000);
  await f.media.sync();
  assert.equal(f.sessions[0].active, false);
  await f.media.destroy();
});

test('resume within the grace period cancels the deadline and the next pause starts a new one', async () => {
  const f = await fixture();
  f.track('A', '1', 1);
  await f.media.sync();
  f.patch({ playing: false });
  await f.media.sync();
  f.advance(599999);
  f.sessions[0].events.play();
  await until(() => f.commands.includes('play'));
  await f.media.sync();
  f.advance(600000);
  await f.media.sync();
  assert.equal(f.sessions[0].active, true);
  assert.equal(f.sessions.length, 1);
  f.patch({ playing: false });
  await f.media.sync();
  f.advance(599999);
  await f.media.sync();
  assert.equal(f.sessions[0].destroyed, false);
  f.advance(1);
  await f.media.sync();
  assert.equal(f.sessions[0].active, false);
  await f.media.destroy();
});

test('playing after expiry creates a fresh session with the current song and cover', async () => {
  const f = await fixture();
  f.track('A', '1', 1);
  await f.media.sync();
  f.patch({ playing: false });
  await f.media.sync();
  f.advance(600000);
  await f.media.sync();
  f.track('B', '2', 2);
  await f.media.sync();
  assert.equal(f.sessions.length, 2);
  assert.equal(f.sessions[0].destroyed, true);
  assert.equal(f.sessions[1].active, true);
  assert.equal(f.metadata.at(-1).assetId, 'B');
  assert.equal(f.metadata.at(-1).mediaImage.label, 2);
  assert.ok(f.sessions[1].events.pause && f.sessions[1].events.playNext);
  assert.equal(f.playback.at(-1).state, 1);
  assert.deepEqual(f.background, [true, false, true]);
  await f.media.destroy();
  assert.ok(f.maps.every(map => map.released === 1));
});

test('empty decks, unavailable native state and an explicit system stop retire immediately', async () => {
  for (const cause of ['empty', 'not-ready', 'stop']) {
    const f = await fixture();
    f.track('A', '1', 1);
    await f.media.sync();
    if (cause === 'empty') { f.empty(); }
    if (cause === 'not-ready') { f.patch({ ready: false }); }
    if (cause === 'stop') { f.sessions[0].events.stop(); }
    await f.media.sync();
    assert.equal(f.sessions[0].destroyed, true, cause);
    assert.equal(f.playback.at(-1).state, 6);
    assert.deepEqual(f.background, [true, false]);
    await f.media.destroy();
  }
});

test('aggregate playback from another deck keeps the session active beyond the timeout', async () => {
  const f = await fixture();
  f.track('A', '1', 1);
  await f.media.sync();
  f.advance(1200000);
  f.track('B', '2', 2);
  await f.media.sync();
  assert.equal(f.sessions[0].active, true);
  assert.equal(f.playback.at(-1).state, 1);
  assert.deepEqual(f.background, [true]);
  await f.media.destroy();
});

test('next and previous retain the session, cover and background task through asynchronous empty loading', async () => {
  for (const [event, command] of [['playNext', 'next'], ['playPrevious', 'previous']]) {
    const f = await fixture();
    f.track('A', '1', 1);
    await f.media.sync();
    const cover = f.metadata.at(-1).mediaImage;
    const count = f.playback.length;
    f.sessions[0].events[event]();
    assert.equal(f.commands.at(-1), command);
    f.empty();
    f.patch({ loading: true });
    await f.media.sync();
    f.advance(750);
    await f.media.sync();
    assert.equal(f.sessions[0].active, true);
    assert.equal(f.sessions[0].destroyed, false);
    assert.equal(f.metadata.at(-1).assetId, 'A');
    assert.equal(cover.released, 0);
    assert.equal(f.playback.length, count);
    assert.deepEqual(f.background, [true]);
    f.track('B', '2', 2);
    await f.media.sync();
    assert.equal(f.sessions.length, 1);
    assert.equal(f.metadata.at(-1).assetId, 'B');
    assert.equal(f.metadata.at(-1).mediaImage.label, 2);
    assert.equal(cover.released, 1);
    assert.deepEqual(f.background, [true]);
    await f.media.destroy();
  }
});

test('failed or stalled loading releases the retained session and task within a bounded transition', async () => {
  for (const failed of [true, false]) {
    const f = await fixture();
    f.track('A', '1', 1);
    await f.media.sync();
    f.empty();
    f.patch({ loading: true });
    await f.media.sync();
    f.advance(14999);
    await f.media.sync();
    assert.equal(f.sessions[0].destroyed, false);
    if (failed) { f.patch({ loading: false }); }
    else { f.advance(1); }
    await f.media.sync();
    assert.equal(f.sessions[0].destroyed, true);
    assert.deepEqual(f.background, [true, false]);
    await f.media.sync();
    assert.equal(f.sessions.length, 1);
    await f.media.destroy();
  }
});

test('loading while paused neither extends the idle deadline nor starts a background task', async () => {
  const f = await fixture();
  f.track('A', '1', 1);
  await f.media.sync();
  f.patch({ playing: false });
  await f.media.sync();
  f.advance(599999);
  f.empty();
  f.patch({ loading: true });
  await f.media.sync();
  assert.equal(f.sessions[0].destroyed, false);
  f.advance(1);
  await f.media.sync();
  assert.equal(f.sessions[0].destroyed, true);
  assert.deepEqual(f.background, [true, false]);
  await f.media.destroy();
});

test('system resume keeps its task while the native command is queued, and bounds an unfulfilled resume', async () => {
  for (const resume of [true, false]) {
    const f = await fixture();
    f.track('A', '1', 1);
    await f.media.sync();
    f.patch({ playing: false });
    await f.media.sync();
    f.deferCommands(true);
    f.sessions[0].events.play();
    await until(() => f.commands.includes('play'));
    await f.media.sync();
    f.advance(750);
    await f.media.sync();
    assert.deepEqual(f.background, [true, false, true]);
    if (resume) { f.patch({ playing: true }); }
    else { f.advance(4250); }
    await f.media.sync();
    assert.equal(f.sessions.length, 1);
    assert.equal(f.sessions[0].active, true);
    assert.deepEqual(f.background, resume ? [true, false, true] : [true, false, true, false]);
    await f.media.destroy();
  }
});

test('an explicit stop wins over a stale playing or loading snapshot', async () => {
  for (const loading of [true, false]) {
    const f = await fixture();
    f.track('A', '1', 1);
    await f.media.sync();
    f.patch({ loading });
    f.deferCommands(true);
    f.sessions[0].events.stop();
    await f.media.sync();
    assert.equal(f.sessions[0].destroyed, true);
    assert.equal(f.playback.at(-1).state, 6);
    assert.deepEqual(f.background, [true, false]);
    await f.media.destroy();
  }
});

test('failed stop destruction retries without losing the reusable session', async () => {
  const f = await fixture();
  f.track('A', '1', 1);
  await f.media.sync();
  f.failDestroy(true);
  f.sessions[0].events.stop();
  await f.media.sync();
  assert.equal(f.sessions[0].active, false);
  assert.equal(f.sessions[0].destroyed, false);
  assert.deepEqual(f.background, [true, false]);
  f.failDestroy(false);
  await f.media.sync();
  assert.equal(f.sessions[0].destroyed, true);
  assert.equal(f.sessions.length, 1);
  await f.media.destroy();
  assert.ok(f.maps.every(map => map.released === 1));
});

test('a failed idle destruction retries or reactivates the retained session on resume', async () => {
  for (const resume of [false, true]) {
    const f = await fixture();
    f.track('A', '1', 1);
    await f.media.sync();
    f.patch({ playing: false });
    await f.media.sync();
    f.advance(600000);
    f.failDestroy(true);
    await f.media.sync();
    assert.equal(f.sessions[0].active, false);
    assert.equal(f.sessions[0].destroyed, false);
    f.failDestroy(false);
    if (resume) { f.patch({ playing: true }); }
    await f.media.sync();
    assert.equal(f.sessions[0].active, resume);
    assert.equal(f.sessions[0].destroyed, !resume);
    await f.media.destroy();
    assert.ok(f.maps.every(map => map.released === 1));
  }
});

test('destroy waits for session creation and never activates a late session', async () => {
  const f = await fixture();
  f.track('A', '1', 1);
  const gate = f.delayCreate();
  const sync = f.media.sync();
  await until(() => f.sessions.length === 1);
  const closing = f.media.destroy();
  gate.resolve();
  await Promise.all([sync, closing]);
  assert.equal(f.sessions[0].activations, 0);
  assert.equal(f.sessions[0].destroyed, true);
  assert.equal(f.metadata.length, 0);
  assert.ok(f.maps.every(map => map.released === 1));
});

test('expiry during artwork decoding still retires and releases the late image', async () => {
  const f = await fixture();
  f.track('A', '1', 1);
  await f.media.sync();
  f.patch({ playing: false });
  await f.media.sync();
  f.advance(599000);
  f.track('B', '2', 2);
  f.patch({ playing: false });
  const gate = f.delayDecode();
  const sync = f.media.sync();
  await until(() => f.decodes.includes(2));
  f.advance(1000);
  gate.resolve(f.makeMap(2));
  await sync;
  assert.equal(f.sessions[0].active, false);
  assert.equal(f.sessions[0].destroyed, true);
  assert.ok(f.maps.filter(map => map.label !== 0).every(map => map.released === 1));
  await f.media.destroy();
});


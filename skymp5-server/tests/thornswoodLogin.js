'use strict';
// Thornswood #1320. The offline login asks the gamemode (mp.onThornswoodLogin)
// and challenges the connection with a nonce; the join pass is proved by a mac
// over that nonce under the pass's own key, so a pass seen on the wire proves
// nothing on another connection. With no hook, the login is exactly as before.
//
// Runs the real ts/systems/login.ts and ts/systems/thornswoodLogin.ts, and a
// stand-in gamemode hook that checks a proof the way Thornswood's gamemode
// does: the pass's signature under passKey, its key derived from passKey and
// the pass, and the mac over the nonce this connection was sent.
//
//     node tests/thornswoodLogin.js
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const ts = require('typescript');

function compile(file) {
  const source = fs.readFileSync(path.join(__dirname, '../ts/systems', file), 'utf8');
  return ts.transpileModule(source, { compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2022 } }).outputText;
}
const loginJs = compile('login.ts');
const thornswoodJs = compile('thornswoodLogin.ts');

function loadThornswood() {
  const exports = {};
  vm.runInNewContext(thornswoodJs, { exports, require() { throw new Error('thornswoodLogin.ts needs nothing'); } });
  return exports;
}

function loadLogin(logged) {
  const exports = {};
  const sandbox = {
    exports, global: { fetch: async () => { throw new Error('no fetch offline'); } },
    console: { log: (...a) => logged.push(a.join(' ')), error: (...a) => logged.push('ERROR ' + a.join(' ')) },
    require(name) {
      if (name === '../settings') return { Settings: { get: async () => ({}) } };
      if (name === 'fetch-retry') return { default: (f) => f };
      if (name === './metricsSystem') return { loginsCounter: { inc() { } }, loginErrorsCounter: { inc() { } } };
      if (name === './thornswoodLogin') return loadThornswood();
      if (name === 'crypto') return crypto;
      throw new Error('Unexpected dependency ' + name);
    }
  };
  vm.createContext(sandbox);
  vm.runInContext(loginJs, sandbox);
  return exports;
}

// The pass and its key, as the site makes them (site/server.js makePass and
// the key beside it).
const PASS_KEY = 'test-pass-key-not-a-secret';
const b64u = (b) => Buffer.from(b).toString('base64').replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
function makePass(o) {
  const body = b64u(JSON.stringify(o));
  return 'jp1.' + body + '.' + b64u(crypto.createHmac('sha256', PASS_KEY).update('jp1.' + body).digest());
}
const passKeyOf = (pass) => b64u(crypto.createHmac('sha256', PASS_KEY).update('jpk1.' + pass.split('.')[2]).digest());
const macOf = (key, nonce) => b64u(crypto.createHmac('sha256', key).update('jlp1.' + nonce).digest());

// A gamemode hook like Thornswood's. mode 'watch': hand over at the login,
// record what the proof said. mode 'require': wait for a good proof.
function gamemode(mode) {
  const seen = [];
  const hook = (e) => {
    seen.push(e);
    if (!e.proof) return mode === 'watch' ? e.profileId : -1;
    const parts = String(e.proof.pass).split('.');
    let ok = parts.length === 3 && parts[0] === 'jp1';
    let p = 0;
    if (ok) {
      const want = b64u(crypto.createHmac('sha256', PASS_KEY).update('jp1.' + parts[1]).digest());
      ok = want === parts[2];
    }
    if (ok) {
      const o = JSON.parse(Buffer.from(parts[1], 'base64').toString('utf8'));
      p = Number(o.p);
      ok = o.x > Date.now();
    }
    if (ok) ok = macOf(passKeyOf(e.proof.pass), e.nonce) === e.proof.mac;
    e.verdict = ok ? 'good' : 'bad';
    if (mode === 'watch') return -1;
    return ok ? p : 0;
  };
  return { hook, seen };
}

function rig(opts) {
  const logged = [];
  const { Login } = loadLogin(logged);
  const system = new Login((...a) => logged.push(a.join(' ')), 10, null, 7777, 'key', opts.offline !== false);
  const sent = [];
  const spawned = [];
  const guids = { 1: 'guid-a', 2: 'guid-b' };
  const svr = {
    getUserIp: () => '203.0.113.7',
    getUserGuid: (u) => guids[u],
    isConnected: () => true,
    sendCustomPacket: (u, json) => sent.push([u, JSON.parse(json)]),
  };
  if (opts.hook) svr.onThornswoodLogin = opts.hook;
  const ctx = { svr, gm: { emit: (name, userId, profileId) => { if (name === 'spawnAllowed') spawned.push([userId, profileId]); } } };
  system.settingsObject = {};
  const login = (u, profileId) => system.customPacket(u, 'loginWithSkympIo', { gameData: { profileId } }, ctx);
  const proof = (u, content) => system.customPacket(u, 'thornswoodLoginProof', content, ctx);
  const nonceFor = (u) => { const s = sent.filter((x) => x[0] === u && x[1].customPacketType === 'thornswoodLoginChallenge'); return s.length ? s[s.length - 1][1].n : null; };
  return { system, ctx, sent, spawned, logged, guids, login, proof, nonceFor };
}

let failed = 0;
function check(name, fn) {
  try { fn(); console.log('PASS  ' + name); }
  catch (e) { failed++; console.log('FAIL  ' + name + '\n      ' + (e && e.message)); }
}

const now = Date.now();
const pass9 = makePass({ p: 9, s: 'main', v: '1.2.3', d: '111', i: now, x: now + 600000 });
const key9 = passKeyOf(pass9);

check('no hook: the login hands over the number it was given, and no challenge is sent', () => {
  const r = rig({});
  r.login(1, 7);
  assert.deepEqual(r.spawned, [[1, 7]]);
  assert.equal(r.sent.length, 0);
});

check('no hook: a proof packet is ignored', () => {
  const r = rig({});
  r.proof(1, { pass: pass9, mac: 'x' });
  assert.equal(r.spawned.length, 0);
});

check('watching: handed over at the login, the challenge carries a fresh nonce', () => {
  const g = gamemode('watch');
  const r = rig({ hook: g.hook });
  r.login(1, 9);
  assert.deepEqual(r.spawned, [[1, 9]]);
  const n = r.nonceFor(1);
  assert.match(n, /^[A-Za-z0-9_-]{16,128}$/);
  const r2 = rig({ hook: g.hook });
  r2.login(1, 9);
  assert.notEqual(r2.nonceFor(1), n);
});

check('watching: the proof reaches the gamemode and nobody is handed over twice', () => {
  const g = gamemode('watch');
  const r = rig({ hook: g.hook });
  r.login(1, 9);
  r.proof(1, { pass: pass9, mac: macOf(key9, r.nonceFor(1)) });
  assert.equal(g.seen.length, 2);
  assert.equal(g.seen[1].verdict, 'good');
  assert.deepEqual(r.spawned, [[1, 9]]);
});

check('requiring: nothing is handed over until the proof', () => {
  const g = gamemode('require');
  const r = rig({ hook: g.hook });
  r.login(1, 9);
  assert.equal(r.spawned.length, 0);
  r.proof(1, { pass: pass9, mac: macOf(key9, r.nonceFor(1)) });
  assert.deepEqual(r.spawned, [[1, 9]]);
});

check('requiring: the character handed over is the pass\'s, not the number asked for', () => {
  const g = gamemode('require');
  const r = rig({ hook: g.hook });
  r.login(1, 816507994);
  r.proof(1, { pass: pass9, mac: macOf(key9, r.nonceFor(1)) });
  assert.deepEqual(r.spawned, [[1, 9]]);
});

check('a pass and mac seen on the wire prove nothing on another connection', () => {
  const g = gamemode('require');
  const r = rig({ hook: g.hook });
  r.login(1, 9);
  const seenOnWire = { pass: pass9, mac: macOf(key9, r.nonceFor(1)) };
  r.proof(1, seenOnWire);
  r.login(2, 9);
  r.proof(2, seenOnWire);
  assert.deepEqual(r.spawned, [[1, 9]]);
  assert.equal(g.seen[g.seen.length - 1].verdict, 'bad');
});

check('a pass without its key proves nothing', () => {
  const g = gamemode('require');
  const r = rig({ hook: g.hook });
  r.login(1, 9);
  r.proof(1, { pass: pass9, mac: macOf('a-guessed-key-of-some-length', r.nonceFor(1)) });
  assert.equal(r.spawned.length, 0);
});

check('a second proof on the same connection is not passed on', () => {
  const g = gamemode('require');
  const r = rig({ hook: g.hook });
  r.login(1, 9);
  r.proof(1, { pass: pass9, mac: 'wrong' });
  const asked = g.seen.length;
  r.proof(1, { pass: pass9, mac: macOf(key9, r.nonceFor(1)) });
  assert.equal(g.seen.length, asked);
  assert.equal(r.spawned.length, 0);
});

check('a proof with no challenge is not passed on', () => {
  const g = gamemode('require');
  const r = rig({ hook: g.hook });
  r.proof(1, { pass: pass9, mac: 'x' });
  assert.equal(g.seen.length, 0);
});

check('a proof from another connection on the same slot is not passed on', () => {
  const g = gamemode('require');
  const r = rig({ hook: g.hook });
  r.login(1, 9);
  const n = r.nonceFor(1);
  r.guids[1] = 'guid-new';
  r.proof(1, { pass: pass9, mac: macOf(key9, n) });
  assert.equal(g.seen.length, 1);
  assert.equal(r.spawned.length, 0);
});

check('a disconnect forgets the challenge', () => {
  const g = gamemode('require');
  const r = rig({ hook: g.hook });
  r.login(1, 9);
  const n = r.nonceFor(1);
  r.system.disconnect(1);
  r.proof(1, { pass: pass9, mac: macOf(key9, n) });
  assert.equal(r.spawned.length, 0);
});

check('the gamemode refusing hands nobody over', () => {
  const r = rig({ hook: () => 0 });
  r.login(1, 9);
  assert.equal(r.spawned.length, 0);
});

check('a hook that throws hands nobody over, and says so', () => {
  const r = rig({ hook: () => { throw new Error('broken'); } });
  r.login(1, 9);
  assert.equal(r.spawned.length, 0);
  assert.ok(r.logged.some((l) => /threw/.test(l)));
});

check('a proof that is not a pass and a mac reaches the gamemode as nothing', () => {
  const g = gamemode('require');
  const r = rig({ hook: g.hook });
  r.login(1, 9);
  r.proof(1, { pass: 'x'.repeat(5000), mac: 'y' });
  assert.equal(g.seen[1].proof.pass, '');
  assert.equal(r.spawned.length, 0);
});

check('a second login on the same connection sends no second challenge', () => {
  const g = gamemode('watch');
  const r = rig({ hook: g.hook });
  r.login(1, 9);
  r.login(1, 9);
  assert.equal(r.sent.length, 1);
  assert.deepEqual(r.spawned, [[1, 9]]);
});

check('online mode: a proof packet is ignored', () => {
  const g = gamemode('require');
  const r = rig({ hook: g.hook, offline: false });
  r.proof(1, { pass: pass9, mac: 'x' });
  assert.equal(g.seen.length, 0);
});

if (failed) { console.log(failed + ' failed'); process.exit(1); }
console.log('all passed');

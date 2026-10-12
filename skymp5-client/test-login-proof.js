// test-login-proof.js
//
//     node skymp5-client/test-login-proof.js
//
// Thornswood #1320. When the server challenges a login with a nonce
// (thornswoodLoginChallenge), the client answers with the join pass and
// HMAC-SHA256 of "jlp1." + nonce under the pass's key, both of which
// Thornswood's front plugin puts in storage.thornswoodJoinPass. The key never
// goes in the packet.
//
// WHAT THIS RUNS. The real features/thornswoodLoginProof.ts and
// services/services/authService.ts, their TypeScript types taken off (node's
// module.stripTypeScriptTypes, Node 22.13 or newer, or the typescript
// package), over a stand-in controller that records every message sent.
//
// What it checks:
//   1. the login is the same as before: profileId, and no pass in it;
//   2. a challenge with no pass in storage sends nothing, on that tick or later;
//   3. the tick the pass is in storage, the proof goes, once, with the right mac;
//   4. a challenge with the pass already in storage is answered at once;
//   5. the key is never in anything sent;
//   6. a challenge with no nonce, or a pass that is not one, sends nothing;
//   7. a new connection forgets the last one's challenge.
//
// AUTH=<file> runs it against another authService.ts; against the code before
// the change, 5 fail (every case that needs an answer).
'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const ROOT = __dirname;
const AUTH = process.env.AUTH || path.join(ROOT, 'src', 'services', 'services', 'authService.ts');
const PROOF = path.join(ROOT, 'src', 'features', 'thornswoodLoginProof.ts');

let pass = 0;
let fail = 0;
function yes(what, cond) {
  if (cond) { pass++; console.log('PASS  ' + what); }
  else { fail++; console.log('FAIL  ' + what); }
}

function stripTypes(src) {
  const mod = require('module');
  if (typeof mod.stripTypeScriptTypes === 'function') {
    const warn = process.emitWarning;
    process.emitWarning = function () { };
    try { return mod.stripTypeScriptTypes(src, { mode: 'transform' }); }
    finally { process.emitWarning = warn; }
  }
  let ts = null;
  try { ts = require('typescript'); } catch (e) { ts = null; }
  if (ts && typeof ts.transpileModule === 'function') {
    return ts.transpileModule(src, {
      compilerOptions: { target: ts.ScriptTarget.ES2019, module: ts.ModuleKind.ESNext },
    }).outputText;
  }
  throw new Error('needs Node 22.13 or newer, or the typescript package (yarn install here)');
}

// The loader of test-connection-denied.js, with export var and as const.
function load(file, stubs) {
  let js = stripTypes(fs.readFileSync(file, 'utf8').split('\r').join(''));
  js = js.replace(/^import\s+\{([^}]*)\}\s+from\s+["']([^"']+)["'];?/gm, function (m, names, from) {
    const list = names.split(',').map(function (n) { return n.trim(); }).filter(Boolean)
      .map(function (n) { return n.replace(/^type\s+/, '').replace(/\s+as\s+/, ': '); });
    return 'const { ' + list.join(', ') + ' } = __stub(' + JSON.stringify(from) + ');';
  });
  js = js.replace(/^import\s+\*\s+as\s+(\w+)\s+from\s+["']([^"']+)["'];?/gm, function (m, name, from) {
    return 'const ' + name + ' = __stub(' + JSON.stringify(from) + ');';
  });
  const exported = [];
  js = js.replace(/^export\s+(class|const|let|var|function)\s+(\w+)/gm, function (m, kind, name) {
    exported.push(name);
    return kind + ' ' + name;
  });
  js += '\nreturn { ' + exported.join(', ') + ' };';
  return new Function('__stub', js)(function (from) {
    if (!(from in stubs)) return {};
    return stubs[from];
  });
}

const proofModule = load(PROOF, { crypto: crypto });

class ClientListener { }
const MsgType = { CustomPacket: 'customPacket' };
const auth = load(AUTH, {
  crypto: crypto,
  fs: { readFileSync: function () { throw new Error('no locale file here'); } },
  '../../features/authModel': { authGameDataStorageKey: 'authGameData' },
  '../../lib/functionInfo': { FunctionInfo: class { getText() { return ''; } } },
  './clientListener': { ClientListener: ClientListener },
  skyrimPlatform: { browser: { executeJavaScript: function () { } }, Menu: {} },
  './timersService': { TimersService: class { } },
  '../../logging': { logTrace: function () { }, logError: function () { } },
  './networkingService': { NetworkingService: class { } },
  '../../messages': { MsgType: MsgType },
  './settingsService': { SettingsService: class { } },
  '../../features/thornswoodLoginProof': proofModule,
});

function rig() {
  const sent = [];
  const handlers = {};
  const ticks = [];
  const sp = {
    storage: { authGameData: { local: { profileId: 9 } } },
    settings: { 'skymp5-client': { gameData: {} } },
    browser: { executeJavaScript: function () { }, setVisible: function () { }, setFocused: function () { } },
    Game: { disablePlayerControls: function () { } },
  };
  const controller = {
    emitter: {
      on: function (ev, fn) { handlers[ev] = fn; },
      emit: function (ev, e) { if (ev === 'sendMessage') sent.push(JSON.parse(e.message.contentJsonDump)); },
    },
    on: function (ev, fn) { if (ev === 'tick') ticks.push(fn); },
    once: function () { },
    lookupListener: function () { return { close: function () { }, reconnect: function () { } }; },
  };
  const service = new auth.AuthService(sp, controller);
  return {
    sp, sent,
    accepted: function () { handlers.connectionAccepted(); },
    packet: function (o) { handlers.customPacketMessage({ message: { contentJsonDump: JSON.stringify(o) } }); },
    tick: function (n) { for (let i = 0; i < (n || 1); i++) ticks.forEach(function (f) { f(); }); },
    proofs: function () { return sent.filter(function (m) { return m.customPacketType === 'thornswoodLoginProof'; }); },
  };
}

const b64u = function (b) { return Buffer.from(b).toString('base64').replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, ''); };
const PASS = 'jp1.eyJwIjo5fQ.' + b64u(crypto.randomBytes(32));
const KEY = b64u(crypto.randomBytes(32));
const NONCE = b64u(crypto.randomBytes(24));
const MAC = b64u(crypto.createHmac('sha256', KEY).update('jlp1.' + NONCE).digest());

// 1.
const a = rig();
a.accepted();
yes('1. the login is the same as before: the profile, and nothing else in gameData',
  a.sent.length === 1 && a.sent[0].customPacketType === 'loginWithSkympIo' &&
  JSON.stringify(a.sent[0].gameData) === JSON.stringify({ profileId: 9 }));

// 2. and 3.
a.packet({ customPacketType: 'thornswoodLoginChallenge', n: NONCE });
a.tick(30);
yes('2. a challenge with no pass in storage sends nothing, then or on later ticks', a.proofs().length === 0);
a.sp.storage.thornswoodJoinPass = { pass: PASS, key: KEY };
a.tick(1);
yes('3a. the tick the pass is in storage, the proof goes', a.proofs().length === 1);
const p = a.proofs()[0] || {};
yes('3b. with the pass', p.pass === PASS);
yes('3c. and the mac over this nonce under the pass\'s key', p.mac === MAC);
a.tick(30);
yes('3d. once', a.proofs().length === 1);
yes('5. the key is never in anything sent', a.sent.every(function (m) { return JSON.stringify(m).indexOf(KEY) < 0; }));

// 4.
const b = rig();
b.sp.storage.thornswoodJoinPass = { pass: PASS, key: KEY };
b.accepted();
b.packet({ customPacketType: 'thornswoodLoginChallenge', n: NONCE });
yes('4. a challenge with the pass already in storage is answered at once', b.proofs().length === 1 && b.proofs()[0].mac === MAC);

// 6.
const c = rig();
c.sp.storage.thornswoodJoinPass = { pass: PASS, key: KEY };
c.accepted();
c.packet({ customPacketType: 'thornswoodLoginChallenge' });
c.packet({ customPacketType: 'thornswoodLoginChallenge', n: 'short' });
c.tick(5);
yes('6a. a challenge with no nonce, or one that is not a nonce, sends nothing', c.proofs().length === 0);
const d = rig();
d.sp.storage.thornswoodJoinPass = { pass: 'not a pass', key: KEY };
d.accepted();
d.packet({ customPacketType: 'thornswoodLoginChallenge', n: NONCE });
d.tick(5);
yes('6b. a pass in storage that is not one sends nothing', d.proofs().length === 0);

// 7.
const e = rig();
e.accepted();
e.packet({ customPacketType: 'thornswoodLoginChallenge', n: NONCE });
e.accepted();
e.sp.storage.thornswoodJoinPass = { pass: PASS, key: KEY };
e.tick(5);
yes('7. a new connection forgets the last one\'s challenge', e.proofs().length === 0);

// The module on its own.
yes('the mac is HMAC-SHA256 of "jlp1." and the nonce, base64url', proofModule.thornswoodLoginMac(KEY, NONCE) === MAC);
yes('readThornswoodJoinPass refuses a missing key', proofModule.readThornswoodJoinPass({ pass: PASS }) === null);

console.log('');
console.log(pass + ' passed, ' + fail + ' failed');
process.exit(fail ? 1 : 0);

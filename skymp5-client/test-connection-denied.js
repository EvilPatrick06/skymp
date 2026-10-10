// test-connection-denied.js
//
//     node skymp5-client/test-connection-denied.js
//
// Thornswood #1893. networkingService.ts answered every connectionDenied
// packet with reconnect(): a refusal at the engine's door ("No free incoming
// connections", "IP recently connected", "Already connected", "Incompatible
// protocol version", "Banned") was answered with a new connection at once,
// refused again, in a loop with no wait for as long as the game was open.
//
// WHAT THIS RUNS. The real networkingService.ts, its TypeScript types taken
// off (node's module.stripTypeScriptTypes, Node 22.13 or newer, or the
// typescript package), over a stand-in mpClientPlugin that counts every
// createClient and destroyClient, and a stand-in controller with tick.
//
// What it checks:
//   1. a denial is told to the client's listeners (connectionDenied event);
//   2. it opens no new connection, on that tick or on many ticks after;
//   3. the connection is closed (destroyClient) on the next tick, not inside
//      mpClientPlugin.tick's own callback;
//   4. the reason is left in storage.thornswoodConnectionDenied for the
//      game's plugins, with when;
//   5. connect() after it (a person picking a character) connects once;
//   6. connectionFailed and disconnect still reconnect as before.
//
// NETWORKING=<file> runs it against another copy; against the code before
// the change the denial cases fail.
'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = __dirname;
const NETWORKING = process.env.NETWORKING ||
  path.join(ROOT, 'src', 'services', 'services', 'networkingService.ts');

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

// Same loader as test-update-churn.js: every import comes from stubs.
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
  js = js.replace(/^export\s+(class|const|let|function)\s+(\w+)/gm, function (m, kind, name) {
    exported.push(name);
    return kind + ' ' + name;
  });
  js += '\nreturn { ' + exported.join(', ') + ' };';
  return new Function('__stub', js)(function (from) { return stubs[from] || {}; });
}

class ClientListener { }
const stubs = {
  '../../logging': { logTrace: function () { }, logError: function () { } },
  '../../lib/errors': { NeverError: class extends Error { } },
  '../../messages': { MsgType: {} },
  './clientListener': { ClientListener: ClientListener },
  './remoteServer': { RemoteServer: class { } },
};
const NetworkingService = load(NETWORKING, stubs).NetworkingService;

// One client with a game's worth of plumbing around it. next is the packet the
// engine hands the next tick (then nothing).
function rig() {
  const w = { created: [], destroyed: 0, events: [], next: [], inCallback: false, destroyedInCallback: 0 };
  const tickFns = [];
  const onceFns = [];
  const sp = {
    storage: {},
    decodeUtf8: function () { return ''; },
    mpClientPlugin: {
      createClient: function (h, p) { w.created.push(h + ':' + p); },
      destroyClient: function () { w.destroyed++; if (w.inCallback) w.destroyedInCallback++; },
      isConnected: function () { return false; },
      send: function () { },
      tick: function (cb) {
        const due = w.next.splice(0);
        w.inCallback = true;
        try { due.forEach(function (p) { cb(p[0], null, p[1]); }); }
        finally { w.inCallback = false; }
      },
    },
  };
  const controller = {
    on: function (ev, fn) { if (ev === 'tick') tickFns.push(fn); },
    once: function (ev, fn) { if (ev === 'tick') onceFns.push(fn); },
    emitter: {
      on: function () { },
      emit: function (ev, e) { w.events.push([ev, e]); },
    },
    lookupListener: function () { return null; },
  };
  w.service = new NetworkingService(sp, controller);
  w.sp = sp;
  w.tick = function (n) {
    for (let i = 0; i < (n || 1); i++) {
      const once = onceFns.splice(0);
      tickFns.forEach(function (f) { f(); });
      once.forEach(function (f) { f(); });
    }
  };
  return w;
}

// 1-4. A denial.
const REASON = 'IP recently connected';
const d = rig();
d.service.connect('203.0.113.9', 7777);
yes('connect() opens one connection', d.created.length === 1);
d.next.push(['connectionDenied', REASON]);
d.tick(1);
yes('the denial is told to the client (connectionDenied, with the reason)',
  d.events.some(function (e) { return e[0] === 'connectionDenied' && e[1] && e[1].error === REASON; }));
yes('the denial opens no new connection on its tick', d.created.length === 1);
d.tick(1);
yes('the connection is closed by the tick after the denial', d.destroyed >= 1);
yes('and not from inside mpClientPlugin.tick\'s own callback', d.destroyedInCallback === 0);
d.tick(600);
yes('600 ticks later it still has opened no new connection (' + d.created.length + ' in all)', d.created.length === 1);
const left = d.sp.storage.thornswoodConnectionDenied;
yes('the reason is left in storage.thornswoodConnectionDenied for the game\'s plugins',
  !!left && left.error === REASON);
yes('with when it came', !!left && typeof left.at === 'number' && left.at > 0);

// A run of denials, the old loop's shape: still nothing opened.
for (let i = 0; i < 20; i++) { d.next.push(['connectionDenied', 'No free incoming connections']); d.tick(1); }
yes('twenty denials in a row open nothing (' + d.created.length + ' in all)', d.created.length === 1);
yes('and storage holds the latest reason',
  (d.sp.storage.thornswoodConnectionDenied || {}).error === 'No free incoming connections');

// 5. Picking a character connects again.
d.service.connect('203.0.113.9', 7777);
yes('connect() after a denial (a person picking) connects once', d.created.length === 2);
d.tick(5);
yes('and nothing more follows it', d.created.length === 2);

// 6. Unchanged: a failed attempt or a dropped line is still retried.
const f = rig();
f.service.connect('203.0.113.9', 7777);
f.next.push(['connectionFailed', '']);
f.tick(1);
yes('connectionFailed still reconnects, as before', f.created.length === 2);
const x = rig();
x.service.connect('203.0.113.9', 7777);
x.next.push(['disconnect', '']);
x.tick(1);
yes('disconnect still reconnects, as before', x.created.length === 2);
yes('neither leaves a denial in storage',
  f.sp.storage.thornswoodConnectionDenied === undefined && x.sp.storage.thornswoodConnectionDenied === undefined);

console.log('');
console.log(pass + ' passed, ' + fail + ' failed');
process.exit(fail ? 1 : 0);

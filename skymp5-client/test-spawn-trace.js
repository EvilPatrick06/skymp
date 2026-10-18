// test-spawn-trace.js
//
//     node skymp5-client/test-spawn-trace.js
//
// Thornswood #1287. Guards and Stormcloaks piled up on people in central
// Whiterun. A neighbour is made by player.placeAtMe, so it is born on the
// person, and SpawnProcess moves it, enables it, resurrects it and puts it
// back, each step asynchronous. Nothing said where the actor stood between
// those steps, or whether a hosting client sent that place to the server.
// spawnTrace.ts writes one line per step to thornswood-spawn.log, and one per
// hosted movement sent while a reference is still spawning.
//
// What this holds:
//   A. The logging changes nothing: SpawnProcess makes the same engine calls,
//      in the same order, with the same arguments, and calls back the same
//      way, as the code before it (the list below is what f49b5f4f does).
//   B. The lines: start, moved, enabled, resurrected (keyframed for a
//      non-actor), settled, in that order, each with the fields a capture
//      needs; the reset at the person after resurrect is visible as dPlayer=0.
//   C. A reference that goes away mid spawn writes "gone-<step>" and, as
//      before, nothing is called back.
//   D. hosted-send is written only while the reference is spawning.
//   E. A read that throws costs a field, never the spawn.
//   F. The file is emptied once per game start, kept across a hot reload,
//      and stops at the cap with one line.
//
// WHAT THIS RUNS. The real spawnTrace.ts and spawnProcess.ts with their types
// stripped, over stand-ins for skyrimPlatform, the file system and the
// client's other modules. Read only.
'use strict';

const fs = require('fs');
const path = require('path');

const SRC = path.join(__dirname, 'src', 'view');
const TRACE = process.env.SPAWNTRACE || path.join(SRC, 'spawnTrace.ts');
const PROCESS = process.env.SPAWNPROCESS || path.join(SRC, 'spawnProcess.ts');

let pass = 0;
let fail = 0;
function yes(what, cond) {
  if (cond) { pass++; console.log('PASS  ' + what); } else { fail++; console.log('FAIL  ' + what); }
}

function stripTypes(src) {
  const mod = require('module');
  if (typeof mod.stripTypeScriptTypes === 'function') {
    const warn = process.emitWarning;
    process.emitWarning = function () { };
    try { return mod.stripTypeScriptTypes(src, { mode: 'transform' }); } finally { process.emitWarning = warn; }
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

function load(file, stubs) {
  let js = stripTypes(fs.readFileSync(file, 'utf8').split('\r').join(''));
  js = js.replace(/^import\s+\{([^}]*)\}\s+from\s+["']([^"']+)["'];?/gm, function (m, names, from) {
    const list = names.split(',').map(function (n) { return n.trim(); }).filter(Boolean)
      .map(function (n) { return n.replace(/^type\s+/, '').replace(/\s+as\s+/, ': '); });
    return 'const { ' + list.join(', ') + ' } = __stub(' + JSON.stringify(from) + ');';
  });
  const exported = [];
  js = js.replace(/^export\s+(class|const|let|function)\s+(\w+)/gm, function (m, kind, name) {
    exported.push(name);
    return kind + ' ' + name;
  });
  js += '\nreturn { ' + exported.join(', ') + ' };';
  return new Function('__stub', js)(function (from) {
    if (!(from in stubs)) throw new Error(path.basename(file) + ' imports ' + from + ', which this harness does not stand in for');
    return stubs[from];
  });
}

// ---- The stand-ins -------------------------------------------------------

const PLAYER = [1000, 2000, 50];      // where the person stands
const TARGET = [1800, 2600, 60];      // where the server says the NPC is

// The file system as the plugin sees it through require('fs').
const files = { writes: 0, text: {} };
const fakeFs = {
  writeFileSync(p, s) { files.writes++; files.text[p] = String(s); },
  appendFileSync(p, s) {
    if (files.failAppend) throw new Error('disk full');
    files.text[p] = (files.text[p] || '') + s;
  },
};
globalThis.require = function (id) {
  if (id === 'fs') return fakeFs;
  throw new Error('no module ' + id);
};

let env = null;   // the current world; the stubs read it at call time
const storage = {};

function newWorld() {
  const w = { calls: [], refs: new Map(), throwOnPos: false };
  w.player = {
    getPositionX() { return PLAYER[0]; }, getPositionY() { return PLAYER[1]; }, getPositionZ() { return PLAYER[2]; },
  };
  w.make = function (id, isActor) {
    // placeAtMe: born on the person, disabled.
    const r = {
      id: id, pos: PLAYER.slice(), disabled: true, dead: false, isActor: !!isActor,
      getFormID() { return id; },
      getPositionX() { if (w.throwOnPos) throw new Error('engine says no'); return this.pos[0]; },
      getPositionY() { return this.pos[1]; },
      getPositionZ() { return this.pos[2]; },
      isDisabled() { return this.disabled; },
      is3DLoaded() { return true; },
      getBaseObject() { return { getFormID() { return 0x13bbd; } }; },
      setPosition(x, y, z) { w.calls.push(['setPosition', id, x, y, z]); this.pos = [x, y, z]; return Promise.resolve(); },
      enable(fade) { w.calls.push(['enable', id, fade]); this.disabled = false; return Promise.resolve(); },
      setMotionType(t, b) { w.calls.push(['setMotionType', id, t, b]); return Promise.resolve(); },
    };
    if (isActor) {
      r.isDead = function () { return r.dead; };
      // A reset returns a placeAtMe actor to where it was made: the person.
      r.resurrect = function () { w.calls.push(['resurrect', id]); r.pos = PLAYER.slice(); return Promise.resolve(); };
    }
    w.refs.set(id, r);
    return r;
  };
  return w;
}

const sp = {
  Game: {
    getFormEx(id) { return env.refs.get(id) || null; },
    getPlayer() { return env.player; },
  },
  ObjectReference: { from(f) { return f || null; } },
  Actor: { from(f) { return f && f.isActor ? f : null; } },
  MotionType: { Keyframed: 4 },
  printConsole() { },
  storage: storage,
};

const trace = load(TRACE, { skyrimPlatform: sp });
const { SpawnProcess } = load(PROCESS, {
  skyrimPlatform: sp,
  '../sync/appearance': { applyTints(ac, app) { env.calls.push(['applyTints', ac.id, app]); } },
  '../sync/movement': {},
  '../extensions/objectReferenceEx': { ObjectReferenceEx: { dealWithRef(r) { env.calls.push(['dealWithRef', r.id]); } } },
  './spawnTrace': trace,
});

function flush() { return new Promise((ok) => setImmediate(ok)); }
async function settleAll() { for (let i = 0; i < 20; i++) await flush(); }
function logText() { return files.text[trace.SPAWN_LOG_FILE] || ''; }
function lines() { return logText().split('\n').filter(Boolean); }
function linesFor(local) { return lines().filter((l) => l.indexOf(' local=' + local.toString(16) + ' ') !== -1 || l.endsWith(' local=' + local.toString(16))); }
function field(line, k) { const m = new RegExp(' ' + k + '=(\\S+)').exec(line); return m ? m[1] : undefined; }
function stage(line) { return line.split(' ')[2]; }

(async function () {
  // ---- A and B: an actor, the whole spawn --------------------------------
  env = newWorld();
  const A = 0xff000a01;
  env.make(A, true);
  storage.hosted = [0x1000a01];
  trace.traceSpawnChosen({ remote: 0x1000a01, local: A, base: 0x13bbd, method: 'placeAtMe', reason: 'no-reference',
    target: TARGET, hostedByOther: false, isDead: false });
  let called = 0;
  new SpawnProcess({ name: 'Guard' }, TARGET, A, () => { called++; });
  await settleAll();

  const want = [
    ['setPosition', A].concat(TARGET),
    ['applyTints', A, { name: 'Guard' }],
    ['enable', A, false],
    ['resurrect', A],
    ['setPosition', A].concat(TARGET),
  ];
  yes('A1 the same engine calls, in the same order, as before the logging: ' + JSON.stringify(env.calls),
      JSON.stringify(env.calls) === JSON.stringify(want));
  yes('A2 the callback runs once, after the put-back', called === 1);

  const la = linesFor(A);
  yes('B1 chosen, start, moved, enabled, resurrected, settled, in that order: ' + la.map(stage).join(','),
      la.map(stage).join(',') === 'chosen,start,moved,enabled,resurrected,settled');
  yes('B2 every line is tagged and clocked', la.every((l) => /^\[spawn\] \d\d:\d\d:\d\d\.\d\d\d /.test(l)));
  const chosen = la[0];
  yes('B3 chosen names the remote, base, method, reason, target and the person\'s distance to it',
      field(chosen, 'remote') === '1000a01' && field(chosen, 'base') === '13bbd' && field(chosen, 'method') === 'placeAtMe' &&
      field(chosen, 'reason') === 'no-reference' && field(chosen, 'target') === '1800,2600,60' &&
      field(chosen, 'dPlayerTarget') === String(Math.round(Math.hypot(800, 600, 10))) &&
      field(chosen, 'hosted') === '1' && field(chosen, 'hostedByOther') === '0');
  const st = {};
  la.forEach((l) => { st[stage(l)] = l; });
  yes('B4 start: still on the person, disabled', field(st.start, 'dPlayer') === '0' && field(st.start, 'disabled') === '1');
  yes('B5 moved: at the target, still disabled', field(st.moved, 'dTarget') === '0' && field(st.moved, 'disabled') === '1');
  yes('B6 enabled: at the target, enabled', field(st.enabled, 'dTarget') === '0' && field(st.enabled, 'disabled') === '0');
  yes('B7 resurrected: the reset shows, enabled and back on the person',
      field(st.resurrected, 'dPlayer') === '0' && field(st.resurrected, 'disabled') === '0' &&
      Number(field(st.resurrected, 'dTarget')) > 0);
  yes('B8 settled: put back at the target', field(st.settled, 'dTarget') === '0');
  yes('B9 every step line carries remote, method, dead, loaded3d, hosted and ms',
      la.slice(1).every((l) => field(l, 'remote') === '1000a01' && field(l, 'method') === 'placeAtMe' &&
        field(l, 'dead') === '0' && field(l, 'loaded3d') === '1' && field(l, 'hosted') === '1' && /^\d+$/.test(field(l, 'ms') || '')));
  yes('B10 after settled the reference is no longer spawning', !trace.spawnIsSpawning(A));

  // ---- B, a non-actor ----------------------------------------------------
  env = newWorld();
  const K = 0xff000b02;
  env.make(K, false);
  new SpawnProcess(null, TARGET, K, () => { called++; });
  await settleAll();
  yes('B11 a non-actor: same calls as before (move, enable, dealWithRef, keyframed, put back): ' + JSON.stringify(env.calls),
      JSON.stringify(env.calls) === JSON.stringify([
        ['setPosition', K].concat(TARGET), ['enable', K, false], ['dealWithRef', K], ['setMotionType', K, 4, true], ['setPosition', K].concat(TARGET)]));
  yes('B12 its lines say keyframed, and a spawn nobody announced has method -',
      linesFor(K).map(stage).join(',') === 'start,moved,enabled,keyframed,settled' && field(linesFor(K)[0], 'method') === '-');

  // ---- C: gone mid spawn ---------------------------------------------------
  env = newWorld();
  const G = 0xff000c03;
  const g = env.make(G, true);
  const realEnable = g.enable;
  g.enable = function (f) { const p = realEnable.call(g, f); env.refs.delete(G); return p; };
  let gCalled = 0;
  new SpawnProcess(null, TARGET, G, () => { gCalled++; });
  await settleAll();
  yes('C1 gone after the enable: no resurrect, no put-back, as before', JSON.stringify(env.calls.map((c) => c[0])) === '["setPosition","enable"]');
  yes('C2 and nothing is called back', gCalled === 0);
  yes('C3 the last line says gone-enabled', linesFor(G).map(stage).join(',') === 'start,moved,gone-enabled');
  yes('C4 and it is no longer spawning', !trace.spawnIsSpawning(G));
  const N = 0xff000c04;
  new SpawnProcess(null, TARGET, N, () => { gCalled++; });
  await settleAll();
  yes('C5 a reference that is not there at all: gone-start and no call', stage(linesFor(N)[0]) === 'gone-start' && gCalled === 0);

  // ---- D: hosted-send ------------------------------------------------------
  env = newWorld();
  const H = 0xff000d05;
  env.make(H, true);
  trace.traceSpawnChosen({ remote: 0x1000d05, local: H, base: 1, method: 'placeAtMe', reason: 'no-reference',
    target: TARGET, hostedByOther: false, isDead: false });
  trace.traceHostedSend(H, 0x1000d05, PLAYER.slice());
  const hs = linesFor(H).filter((l) => stage(l) === 'hosted-send');
  yes('D1 a hosted send while spawning is written, with the place sent and how far it is from the target and the person',
      hs.length === 1 && field(hs[0], 'sent') === '1000,2000,50' && field(hs[0], 'dPlayer') === '0' &&
      field(hs[0], 'dTarget') === String(Math.round(Math.hypot(800, 600, 10))));
  new SpawnProcess(null, TARGET, H, () => { });
  await settleAll();
  const before = lines().length;
  trace.traceHostedSend(H, 0x1000d05, TARGET.slice());
  trace.traceHostedSend(0xff00ffff, 0x100ffff, TARGET.slice());
  yes('D2 after settled, and for a reference never spawned, nothing is written', lines().length === before);

  // ---- E: a read that throws -------------------------------------------------
  env = newWorld();
  const E = 0xff000e06;
  env.make(E, true);
  env.throwOnPos = true;
  let eCalled = 0;
  new SpawnProcess(null, TARGET, E, () => { eCalled++; });
  await settleAll();
  yes('E1 a position read that throws still spawns and calls back', eCalled === 1 && env.calls.filter((c) => c[0] === 'setPosition').length === 2);
  yes('E2 and its lines say pos=-', linesFor(E).length === 5 && linesFor(E).every((l) => field(l, 'pos') === '-'));
  files.failAppend = true;
  env.throwOnPos = false;
  const F1 = 0xff000e07;
  env.make(F1, true);
  let fCalled = 0;
  new SpawnProcess(null, TARGET, F1, () => { fCalled++; });
  await settleAll();
  files.failAppend = false;
  yes('E3 a file that will not take the line costs the line, never the spawn', fCalled === 1);

  // ---- F: the file -------------------------------------------------------------
  yes('F1 emptied once this game', files.writes === 1);
  trace.traceRespawnNeeded('cell', 0x1000a01, A);
  yes('F2 respawn-needed is written with where it came from', stage(lines()[lines().length - 1]) === 'respawn-needed' &&
      field(lines()[lines().length - 1], 'where') === 'cell');
  const reloaded = load(TRACE, { skyrimPlatform: sp });   // a plugin hot reload: new module, same storage
  reloaded.spawnTraceWrite('[spawn] after reload');
  yes('F3 a hot reload does not empty it again', files.writes === 1 && logText().indexOf('after reload') !== -1);
  storage.thornswoodSpawnLogLines = trace.SPAWN_LOG_MAX_LINES - 1;
  trace.spawnTraceWrite('[spawn] last one');
  trace.spawnTraceWrite('[spawn] one too many');
  trace.spawnTraceWrite('[spawn] and another');
  const tail = lines().slice(-2);
  yes('F4 at the cap: the last line, one line saying so, then nothing',
      tail[0] === '[spawn] last one' && /cap reached at 20000 lines/.test(tail[1]) && logText().indexOf('one too many') === -1);
  yes('F5 the file is under Data/Platform and not Platform/Plugins', trace.SPAWN_LOG_FILE === 'Data/Platform/thornswood-spawn.log');

  console.log('\n' + pass + ' passed, ' + fail + ' failed');
  process.exit(fail ? 1 : 0);
})().catch((e) => { console.log('FAIL  the harness threw: ' + (e && e.stack)); process.exit(1); });

// test-kept-npc-adopt.js
//
//     node skymp5-client/test-kept-npc-adopt.js
//
// Thornswood #2483. The server sends every actor below 0xff000000 with
// 0x100000000 added (PartOne.cpp), so a kept, plugin-placed NPC reaches the
// client as 0x1xxxxxxxx. formView.ts sent all of those down the spawn branch:
// player.placeAtMe made a second reference and only that one was protected
// from the world cleaner, which deletes one random actor a frame. The
// plugin's own copy stood at its authored spot until it was picked, so a kept
// trader or animal stood twice.
//
// The fix: take the placed reference (refrId - 0x100000000) as the view's
// reference when it is loaded, enabled and of the same base; spawn only when it
// is not. On destroy an adopted reference gives back its protection and is
// switched off, never deleted as if this view had spawned it.
//
// WHAT THIS RUNS. The real formView.ts with its types stripped, over stand-ins
// for skyrimPlatform and the client's modules. Run it against the code before
// the change with FORMVIEW=<that file>: the adoption cases fail there.
'use strict';

const fs = require('fs');
const path = require('path');

const FORMVIEW = process.env.FORMVIEW || path.join(__dirname, 'src', 'view', 'formView.ts');

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
  return new Function('__stub', js)(function (from) { return stubs[from] || {}; });
}

const NPC = 43;
const KEPT = 0x0001a67c;          // a trader placed by a plugin
const WIRE = KEPT + 0x100000000;  // how the server sends it
const BASE = 0x00013bbd;

function rig() {
  const w = { once: [], wc: [], deleted: [], disabled: [], spawnedAt: [], spawnProcess: [], forms: {}, nextId: 0xff000100 };
  const baseForm = { getFormID: function () { return BASE; }, getType: function () { return NPC; } };
  w.base = baseForm;
  function actor(id, base) {
    const a = {
      actor: true, id: id, disabledFlag: false, deletedFlag: false,
      getFormID: function () { return id; },
      getBaseObject: function () { return base; },
      isDeleted: function () { return a.deletedFlag; },
      isDisabled: function () { return a.disabledFlag; },
      disableNoWait: function () { a.disabledFlag = true; w.disabled.push(id); },
      delete: function () { a.deletedFlag = true; w.deleted.push(id); },
      getRace: function () { return null; },
      setActorValue: function () { }, setDisplayName: function () { }, setAngle: function () { },
      startDeferredKill: function () { },
    };
    w.forms[id] = a;
    return a;
  }
  w.actor = actor;
  const player = actor(0x14, null);
  player.placeAtMe = function (b) { const id = w.nextId++; w.spawnedAt.push(id); return actor(id, b); };
  w.forms[BASE] = baseForm;
  const sp = {
    once: function (ev, fn) { w.once.push(fn); return {}; },
    printConsole: function () { },
    destroyText: function () { },
    Game: { getPlayer: function () { return player; }, getFormEx: function (id) { return w.forms[id] || null; } },
    ObjectReference: { from: function (f) { return f || null; } },
    Actor: { from: function (f) { return f && f.actor ? f : null; } },
    TESModPlatform: { setWeaponDrawnMode: function () { } },
    FormType: { NPC: NPC },
    storage: {},
    Utility: {},
  };
  const cleaner = { modWcProtection: function (id, mod) { w.wc.push([id, mod]); } };
  const controller = { lookupListener: function () { return cleaner; }, emitter: { emit: function () { } } };
  const stubs = {
    skyrimPlatform: sp,
    './spawnProcess': { SpawnProcess: function (app, pos, id) { w.spawnProcess.push(id); } },
    // The spawn log (Thornswood #1287) only reads; stood in so it writes nothing here.
    './spawnTrace': { traceSpawnChosen: function () { }, traceRespawnNeeded: function () { } },
    '../extensions/objectReferenceEx': { ObjectReferenceEx: { getWorldOrCell: function () { return 0x3c; }, getPos: function () { return [0, 0, 0]; }, dealWithRef: function () { } } },
    '../services/spApiInteractor': { SpApiInteractor: { getControllerInstance: function () { return controller; } } },
  };
  w.FormView = load(FORMVIEW, stubs).FormView;
  w.model = function () { return { refrId: WIRE, baseId: BASE, movement: { worldOrCell: 0x3c, pos: [1, 2, 3], rot: [0, 0, 0] }, numAppearanceChanges: 0 }; };
  w.runOnce = function () { w.once.splice(0).forEach(function (fn) { fn(); }); };
  return w;
}

function run(what, fn) {
  try { fn(); } catch (e) { fail++; console.log('FAIL  ' + what + ': ' + (e && e.stack || e)); }
}

run('kept NPC whose placed copy is loaded', function () {
  const w = rig();
  const placed = w.actor(KEPT, w.base);
  const view = new w.FormView(WIRE);
  view.update(w.model());
  yes('a kept NPC with its placed copy loaded makes no second reference (placeAtMe calls: ' + w.spawnedAt.length + ')', w.spawnedAt.length === 0);
  yes('the view takes the placed reference as its own', view.getLocalRefrId() === KEPT);
  yes('the placed reference is protected from the world cleaner', w.wc.some(function (x) { return x[0] === KEPT && x[1] === 1; }));
  yes('the placed reference goes through the spawn process (position, enable, settle)', w.spawnProcess.length === 1 && w.spawnProcess[0] === KEPT);
  view.update(w.model());
  yes('a second update keeps it, no respawn', w.spawnedAt.length === 0 && view.getLocalRefrId() === KEPT);

  view.destroy();
  yes('destroy gives back its protection at once', w.wc.some(function (x) { return x[0] === KEPT && x[1] === -1; }));
  w.runOnce();
  yes('and switches it off next frame', placed.disabledFlag === true);
  yes('and never deletes the plugin\'s reference', w.deleted.indexOf(KEPT) === -1);
});

run('destroyed and taken again in the same frame', function () {
  const w = rig();
  const placed = w.actor(KEPT, w.base);
  const view = new w.FormView(WIRE);
  view.update(w.model());
  view.destroy();
  view.refrId = 0;
  view.update(w.model());
  w.runOnce();
  yes('a reference taken again before the next frame is not switched off', placed.disabledFlag === false && view.getLocalRefrId() === KEPT);
  const plus = w.wc.filter(function (x) { return x[0] === KEPT && x[1] === 1; }).length;
  const minus = w.wc.filter(function (x) { return x[0] === KEPT && x[1] === -1; }).length;
  yes('its protection balances to one (+' + plus + ' -' + minus + ')', plus - minus === 1);
});

run('placed copy not usable', function () {
  const w = rig();
  const view = new w.FormView(WIRE);
  view.update(w.model());
  yes('no placed copy loaded: spawns one as before', w.spawnedAt.length === 1 && view.getLocalRefrId() === w.spawnedAt[0]);

  const w2 = rig();
  const p2 = w2.actor(KEPT, w2.base);
  p2.disabledFlag = true;
  const v2 = new w2.FormView(WIRE);
  v2.update(w2.model());
  yes('placed copy already cleaned (disabled): spawns as before', w2.spawnedAt.length === 1);

  const w3 = rig();
  w3.actor(KEPT, { getFormID: function () { return 0x999; }, getType: function () { return NPC; } });
  const v3 = new w3.FormView(WIRE);
  v3.update(w3.model());
  yes('placed copy of another base (leveled): spawns as before', w3.spawnedAt.length === 1);
});

run('ids that are not kept NPCs', function () {
  const w = rig();
  const F = w.FormView;
  yes('findKeptPlacedRef exists', typeof F.findKeptPlacedRef === 'function');
  w.actor(KEPT, w.base);
  yes('a 0xff... id (spawned player or NPC) never adopts', F.findKeptPlacedRef(0xff000a01, w.base) === null);
  yes('a bare placed id (no offset) never adopts', F.findKeptPlacedRef(KEPT, w.base) === null);
  yes('the player (0x14 + offset) never adopts', F.findKeptPlacedRef(0x14 + 0x100000000, w.base) === null);
  yes('the kept NPC id adopts its placed copy', F.findKeptPlacedRef(WIRE, w.base) === w.forms[KEPT]);
});

console.log('');
console.log(pass + ' passed, ' + fail + ' failed');
process.exit(fail ? 1 : 0);
// test-update-churn.js
//
//     node skymp5-client/test-update-churn.js
//
// Thornswood #1032. A dev session's skyrim-platform.log at debug, 28 September
// (20:33 to 20:39), had 4,553 "Subscribed to event update" lines and 4,512
// "Unsubscribed" ones: about 12 a second, all session, at 12 to 13 frames a
// second outdoors. That is a once('update') on every frame.
//
// Two callers, both fixed:
//
// 1. FormView.destroy() asked for once('update') on every call. update()
//    calls destroy() on every frame for a form in another worldOrCell, and
//    from the second frame on the view is empty, so the callback did nothing.
//    destroy() now asks for the next update only when the view holds a
//    reference it spawned (0xff...), the one case with something to delete.
// 2. MagicSyncService.onUpdate() wrapped its send in once('update') although
//    it already runs in update: twice a second while a spell or a staff is in
//    hand. It sends directly now.
//
// WHAT THIS RUNS. The real formView.ts and magicSyncService.ts, with their
// TypeScript types taken off (node's own module.stripTypeScriptTypes, Node 22.13
// or newer, or the typescript package when it is installed), over stand-ins for
// skyrimPlatform and the client's other modules that count every once().
//
// Run it against the code before the change with FORMVIEW=<that file> and
// MAGICSYNC=<that file>: the churn cases fail there.
'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = __dirname;
const FORMVIEW = process.env.FORMVIEW || path.join(ROOT, 'src', 'view', 'formView.ts');
const MAGICSYNC = process.env.MAGICSYNC ||
  path.join(ROOT, 'src', 'services', 'services', 'magicSyncService.ts');

let pass = 0;
let fail = 0;
function yes(what, cond) {
  if (cond) {
    pass++;
    console.log('PASS  ' + what);
  } else {
    fail++;
    console.log('FAIL  ' + what);
  }
}

// TypeScript to JavaScript, types only. Nothing here needs a bundler.
function stripTypes(src) {
  const mod = require('module');
  if (typeof mod.stripTypeScriptTypes === 'function') {
    const warn = process.emitWarning;
    process.emitWarning = function () { };
    try {
      return mod.stripTypeScriptTypes(src, { mode: 'transform' });
    } finally {
      process.emitWarning = warn;
    }
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

// Load one source file as a function of its imports. Every import is taken
// from stubs[module path]; a name a stub does not have is undefined, which is
// what a type-only import is once the types are gone.
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

// ---------------------------------------------------------------- FormView

function formViewRig() {
  const w = {
    once: [], deleted: [], wc: [], drawn: [], texts: [], logs: [],
    playerWorld: 0x3c,
    forms: {},
  };
  const player = { id: 0x14 };
  const sp = {
    once: function (ev, fn) { w.once.push({ ev: ev, fn: fn }); return {}; },
    printConsole: function () { w.logs.push(Array.prototype.join.call(arguments, ' ')); },
    destroyText: function (id) { w.texts.push(id); },
    Game: {
      getPlayer: function () { return player; },
      getFormEx: function (id) { return w.forms[id] || null; },
    },
    ObjectReference: { from: function (f) { return f || null; } },
    Actor: { from: function (f) { return f && f.actor ? f : null; } },
    TESModPlatform: { setWeaponDrawnMode: function (ac, mode) { w.drawn.push([ac.id, mode]); } },
    storage: {},
    Utility: {},
  };
  const cleaner = { modWcProtection: function (id, mod) { w.wc.push([id, mod]); } };
  const controller = {
    lookupListener: function () { return cleaner; },
    emitter: { emit: function () { } },
  };
  const stubs = {
    skyrimPlatform: sp,
    '../extensions/objectReferenceEx': {
      ObjectReferenceEx: { getWorldOrCell: function () { return w.playerWorld; } },
    },
    '../services/spApiInteractor': {
      SpApiInteractor: { getControllerInstance: function () { return controller; } },
    },
  };
  w.FormView = load(FORMVIEW, stubs).FormView;
  w.spawned = function (id) {
    const refr = { id: id, actor: true, delete: function () { w.deleted.push(id); } };
    w.forms[id] = refr;
    return refr;
  };
  w.runOnce = function () {
    const due = w.once.splice(0);
    due.forEach(function (o) { o.fn(); });
    return due.length;
  };
  return w;
}

function elsewhere(remoteId) {
  return {
    refrId: remoteId,
    movement: { worldOrCell: 0x1a26f, pos: [0, 0, 0], rot: [0, 0, 0] },
  };
}

{
  const w = formViewRig();
  yes('formView.ts loads and has FormView', typeof w.FormView === 'function');

  // THE BUG. A form in another worldOrCell, never spawned here, for 600 frames
  // (about 50 seconds at 12 frames a second).
  const view = new w.FormView(0xff000a01);
  const model = elsewhere(0xff000a01);
  let threw = '';
  try {
    for (let i = 0; i < 600; i++) { view.update(model); }
  } catch (e) { threw = String(e && e.stack || e); }
  yes('600 frames of a form in another worldOrCell run without an error' + (threw ? ': ' + threw : ''),
      threw === '');
  yes('and ask for the next update 0 times (was 600, once a frame), asked ' + w.once.length,
      w.once.length === 0);
}

{
  // A view that spawned its actor, then the form goes to another worldOrCell.
  const w = formViewRig();
  const view = new w.FormView(0xff000a02);
  w.spawned(0xff000b02);
  view.refrId = 0xff000b02;
  view.textNameId = 7;
  const model = elsewhere(0xff000a02);
  for (let i = 0; i < 600; i++) { view.update(model); }
  yes('a spawned actor whose form leaves for another worldOrCell asks for the next update exactly once, asked ' +
      w.once.length, w.once.length === 1 && w.once[0].ev === 'update');
  yes('its name over the head is taken down at once', w.texts.length === 1 && w.texts[0] === 7);
  yes('nothing is deleted before that update', w.deleted.length === 0);
  w.runOnce();
  yes('on that update the spawned reference is deleted', w.deleted.length === 1 && w.deleted[0] === 0xff000b02);
  yes('and its world cleaner protection is given back',
      w.wc.length === 1 && w.wc[0][0] === 0xff000b02 && w.wc[0][1] === -1);
  yes('and its drawn weapon mode is cleared',
      w.drawn.length === 1 && w.drawn[0][0] === 0xff000b02 && w.drawn[0][1] === -1);
}

{
  // destroy() on its own.
  const w = formViewRig();
  const empty = new w.FormView(0xff000a03);
  for (let i = 0; i < 50; i++) { empty.destroy(); }
  yes('destroy() on a view that holds nothing, 50 times, asks for no update', w.once.length === 0);

  const fromFile = new w.FormView(0xff000a04);
  fromFile.refrId = 0x0001a2b3; // a reference out of the game's own files
  fromFile.destroy();
  yes('destroy() on a reference out of the game\'s files asks for no update (nothing of ours to delete)',
      w.once.length === 0);

  const ours = new w.FormView(0xff000a05);
  w.spawned(0xff000b05);
  ours.refrId = 0xff000b05;
  ours.destroy();
  yes('destroy() on a reference this view spawned asks for one update', w.once.length === 1);
  w.runOnce();
  yes('which deletes it', w.deleted.length === 1 && w.deleted[0] === 0xff000b05);
}

// ------------------------------------------------------------ MagicSync

function magicRig() {
  const w = { once: [], sent: [], handlers: {}, now: 1000000 };
  const player = {
    getFormID: function () { return 0x14; },
    getEquippedSpell: function (slot) { return slot === 0 ? { id: 0x12fd0 } : null; },
    getEquippedItemType: function () { return 0; },
  };
  const sp = {
    Game: { getPlayer: function () { return player; } },
    getAnimationVariablesFromActor: function () {
      return { booleans: new ArrayBuffer(2), floats: new ArrayBuffer(4), integers: new ArrayBuffer(4) };
    },
    SpellType: { Left: 0, Right: 1, Voise: 2, Instant: 3 },
    SlotType: { Left: 0, Right: 1 },
    EquippedItemType: { Staff: 8 },
  };
  const controller = {
    on: function (ev, fn) { (w.handlers[ev] = w.handlers[ev] || []).push(fn); },
    once: function (ev, fn) { w.once.push({ ev: ev, fn: fn }); },
    emitter: { emit: function (ev, e) { if (ev === 'sendMessage') { w.sent.push(e.message); } } },
  };
  const stubs = {
    skyrimPlatform: sp,
    './clientListener': { ClientListener: function ClientListener() { } },
    '../../messages': { MsgType: { UpdateAnimVariables: 24, SpellCast: 23 } },
    '../../view/worldViewMisc': {
      localIdToRemoteId: function (id) { return id; },
      remoteIdToLocalId: function (id) { return id; },
    },
    '../../logging': { logTrace: function () { } },
  };
  const MagicSyncService = load(MAGICSYNC, stubs).MagicSyncService;
  const spHooks = { hooks: { sendAnimationEvent: { add: function () { } } } };
  new MagicSyncService(spHooks, controller);
  w.frames = function (seconds, fps) {
    const realNow = Date.now;
    Date.now = function () { return w.now; };
    try {
      const n = Math.round(seconds * fps);
      for (let i = 0; i < n; i++) {
        w.now += 1000 / fps;
        // An update: the once() callbacks asked for before it, then the handlers.
        const due = w.once.splice(0);
        due.forEach(function (o) { o.fn(); });
        (w.handlers.update || []).forEach(function (f) { f(); });
        w.asked = (w.asked || 0) + w.once.length;
      }
    } finally {
      Date.now = realNow;
    }
  };
  return w;
}

{
  const w = magicRig();
  w.frames(10, 12);
  yes('a spell in hand for 10 seconds at 12 frames a second asks for the next update 0 times (was 20), asked ' +
      (w.asked || 0), (w.asked || 0) === 0);
  const vars = w.sent.filter(function (m) { return m.t === 24; });
  yes('and still sends its animation variables twice a second (19 or 20 in 10 s), sent ' + vars.length,
      vars.length >= 19 && vars.length <= 20);
  yes('each one names the person and carries the variables',
      vars.length > 0 && vars[0].data.actorRemoteId === 0x14 &&
      Array.isArray(vars[0].data.actorAnimationVariables.booleans));
}

console.log('');
console.log(pass + ' passed, ' + fail + ' failed');
process.exit(fail === 0 ? 0 : 1);

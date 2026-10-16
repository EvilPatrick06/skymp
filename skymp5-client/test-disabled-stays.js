// test-disabled-stays.js
//
//     node skymp5-client/test-disabled-stays.js
//
// Thornswood #2328. A reference from a master file that the server holds
// disabled (the three chopping blocks of #376) came back on every time the
// server streamed it again to a game that already had it disabled.
// remoteServer.ts applied harvested, then disabled: for anything that is not a
// tree or flora the harvested step reads isDisabled() as harvested, so with
// isHarvested false it called enable(true); Enable is latent, so the disabled
// step still read isDisabled() true, did nothing, and the queued enable ran.
//
// WHAT THIS RUNS. The real view/modelApplyUtils.ts, its TypeScript types taken
// off (node's module.stripTypeScriptTypes, Node 22.13 or newer, or the
// typescript package), over a stand-in reference whose enable and disable are
// latent: each is queued and takes effect when the stand-in Papyrus VM runs,
// after the call returns, the way Skyrim Platform runs a latent native
// (CallNative.cpp, VmCall). A stream is what onCreateActorMessage does with
// the two flags: ModelApplyUtils.applyModelIsHarvestedAndDisabled, or, in a
// copy of the file from before the change (MODEL=<file>), the old order,
// applyModelIsHarvested and then applyModelIsDisabled.
//
// What it checks:
//   1. a chopping block the server holds disabled is disabled on its first stream;
//   2. and stays disabled on the second and third;
//   3. a harvested item stays disabled on its second stream;
//   4. a reference the server holds neither harvested nor disabled, disabled
//      here, is enabled, with one enable;
//   5. a tree or flora keeps its harvested flag apart from its disabled one;
//   6. no stream of a reference that is not a tree or flora queues more than
//      one enable or disable;
//   7. remoteServer.ts uses the one decision on a stream and on an update.
//
// Against the code before the change (MODEL=<that copy>), 2, 3, 4 and 6 fail:
// the second streams come back on, and a reference turned on gets two enables.
// Against the code before #2382, 5c, 5d, 5e, 5f and 5g fail: the flora redraw
// disables and enables, and a harvest asks a random NPC to activate it.
'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = __dirname;
const MODEL = process.env.MODEL || path.join(ROOT, 'src', 'view', 'modelApplyUtils.ts');
const REMOTE = path.join(ROOT, 'src', 'services', 'services', 'remoteServer.ts');

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

function load(file, stubs) {
  let js = stripTypes(fs.readFileSync(file, 'utf8').split('\r').join(''));
  js = js.replace(/^import\s+\{([^}]*)\}\s+from\s+["']([^"']+)["'];?/gm, function (m, names, from) {
    const list = names.split(',').map(function (n) { return n.trim(); }).filter(Boolean)
      .map(function (n) { return n.replace(/^type\s+/, '').replace(/\s+as\s+/, ': '); });
    return 'const { ' + list.join(', ') + ' } = __stub(' + JSON.stringify(from) + ');';
  });
  const exported = [];
  js = js.replace(/^export\s+(class|const|let|var|function)\s+(\w+)/gm, function (m, kind, name) {
    exported.push(name);
    return kind + ' ' + name;
  });
  js += '\nreturn { ' + exported.join(', ') + ' };';
  return new Function('__stub', js)(function (from) { return stubs[from] || {}; });
}

// The stand-in game: a Papyrus VM queue and references.
const FormType = { Tree: 38, Flora: 39, Furniture: 40, Misc: 32 };
const vm = [];
const refs = {};
function runVm() { while (vm.length) { vm.shift()(); } }
function latent(fn) {
  const thens = [];
  vm.push(function () { fn(); thens.forEach(function (t) { t(); }); });
  return { then: function (t) { thens.push(t); return this; } };
}
function makeRef(id, type) {
  const r = {
    id: id, type: type, disabled: false, harvested: false, deleted: false, calls: [],
    getFormID: function () { return id; },
    getBaseObject: function () { return { getType: function () { return type; } }; },
    isDisabled: function () { return r.disabled; },
    isHarvested: function () { return r.harvested; },
    setHarvested: function (h) { r.harvested = h; },
    enable: function () { r.calls.push('enable'); return latent(function () { r.disabled = false; }); },
    disable: function () { r.calls.push('disable'); return latent(function () { r.disabled = true; }); },
    delete: function () { r.deleted = true; },
    activate: function () { },
    getPositionX: function () { return 0; }, getPositionY: function () { return 0; }, getPositionZ: function () { return 0; },
  };
  refs[id] = r;
  return r;
}
const Game = { getFormEx: function (id) { return refs[id] || null; }, findRandomActor: function () { return null; } };
const ObjectReference = { from: function (f) { return f; } };

const M = load(MODEL, {
  skyrimPlatform: { ObjectReference: ObjectReference, Actor: {}, Game: Game, FormType: FormType, TextureSet: {}, NetImmerse: {} },
  '../sync/inventory': { applyInventory: function () { } },
  '../logging': { logError: function () { }, logTrace: function () { } },
}).ModelApplyUtils;
const combined = typeof M.applyModelIsHarvestedAndDisabled === 'function';
console.log(combined ? 'the code with the one decision' : 'the code before the change: harvested, then disabled');

// One stream, as onCreateActorMessage applies the two flags, then the VM runs.
function stream(r, harvested, disabled) {
  const before = r.calls.length;
  if (combined) {
    M.applyModelIsHarvestedAndDisabled(r, harvested, disabled);
  } else {
    M.applyModelIsHarvested(r, harvested);
    M.applyModelIsDisabled(r, disabled);
  }
  const queued = r.calls.length - before;
  runVm();
  return queued;
}

let most = 0;
// Counted for what is not a tree or flora, the references this change is about.
function s(r, h, d) { const q = stream(r, h, d); if (r.type !== FormType.Flora && r.type !== FormType.Tree && q > most) most = q; }

// 1 and 2. A chopping block (furniture) the server holds disabled.
const block = makeRef(0xd6ae9, FormType.Furniture);
s(block, false, true);
yes('1. a chopping block the server holds disabled is disabled on its first stream', block.disabled === true);
s(block, false, true);
yes('2a. and stays disabled on its second stream', block.disabled === true);
s(block, false, true);
yes('2b. and its third', block.disabled === true);

// 3. A harvested item (misc, picked up).
const item = makeRef(0x1000, FormType.Misc);
s(item, true, false);
yes('3a. a harvested item is disabled on its first stream', item.disabled === true);
s(item, true, false);
yes('3b. and stays disabled on its second', item.disabled === true);

// 4. Neither, but disabled here.
const back = makeRef(0x2000, FormType.Furniture);
back.disabled = true;
const before4 = back.calls.length;
s(back, false, false);
yes('4. a reference the server holds neither harvested nor disabled is enabled here, with one enable',
  back.disabled === false && back.calls.slice(before4).join(',') === 'enable');

// 5. Flora.
const flower = makeRef(0x3000, FormType.Flora);
s(flower, true, false);
yes('5a. a flora\'s harvested flag is its own', flower.harvested === true && flower.disabled === false);
const bush = makeRef(0x3001, FormType.Flora);
stream(bush, false, true);
yes('5b. and its disabled flag is its own', bush.harvested === false && bush.disabled === true);
// 5c to 5h, Thornswood #2382: a tree or flora is shown harvested or not by its
// flag alone. SKSE's SetHarvested queues the engine's harvest model update, so
// nothing is disabled and enabled again to draw it, and no NPC is asked to pick it.
const held = makeRef(0x3002, FormType.Flora);
held.harvested = true;
stream(held, true, true);
const held0 = held.calls.length;
stream(held, false, true);
yes('5c. a flora the server holds disabled, made unharvested in the same stream, stays disabled',
  held.disabled === true && held.harvested === false);
yes('5d. ...and that queued no enable', held.calls.slice(held0).indexOf('enable') === -1);
const pick = makeRef(0x3003, FormType.Flora);
const pick0 = pick.calls.length;
stream(pick, true, false);
yes('5e. a flora made harvested has its flag set, stays enabled, and queues no enable or disable',
  pick.harvested === true && pick.disabled === false && pick.calls.length === pick0);
const pick1 = pick.calls.length;
stream(pick, false, false);
yes('5f. made unharvested again: the flag is cleared, with no enable or disable',
  pick.harvested === false && pick.disabled === false && pick.calls.length === pick1);
let activated = 0, searched = 0;
const tree = makeRef(0x3004, FormType.Tree);
tree.activate = function () { activated++; };
const findWas = Game.findRandomActor;
Game.findRandomActor = function () { searched++; return { getFormID: function () { return 0x5000; } }; };
const tree0 = tree.calls.length;
M.applyModelIsHarvested(tree, true);
runVm();
Game.findRandomActor = findWas;
yes('5g. formView\'s path (applyModelIsHarvested alone) on a tree: harvested by its flag, no NPC asked, nothing activated',
  tree.harvested === true && activated === 0 && searched === 0 && tree.calls.length === tree0);
const again = tree.calls.length;
M.applyModelIsHarvested(tree, true);
runVm();
yes('5h. and asked again with the same flag, nothing at all', tree.calls.length === again && tree.harvested === true);

// 6.
yes('6. no stream of anything but a tree or flora queued more than one enable or disable (' + most + ' at most)', most <= 1);

// 7. The wiring, read out of remoteServer.ts.
const remote = fs.readFileSync(REMOTE, 'utf8');
const create = remote.slice(remote.indexOf('private onCreateActorMessage('), remote.indexOf('private onCreateActorMessage(') + 6000);
yes('7a. a stream applies the two flags as one decision, and no longer one after the other',
  /applyModelIsHarvestedAndDisabled\(refr, flags\.harvested, flags\.disabled\)/.test(create) &&
  !/applyModelIsHarvested\(\s*refr,\s*!!msg\.props\['isHarvested'\]/.test(create) &&
  !/applyModelIsDisabled\(refr, !!\(msg\.props\['isDisabled'\]/.test(create));
const update = remote.slice(remote.indexOf('private onUpdatePropertyMessage('), remote.indexOf('private onUpdatePropertyMessage(') + 2500);
yes('7b. an update to either flag applies both, with the other as the server last sent it',
  /masterRefFlags\.get\(refrId\)/.test(update) && /applyModelIsHarvestedAndDisabled\(refr, flags\.harvested, flags\.disabled\)/.test(update));

console.log('');
console.log(pass + ' passed, ' + fail + ' failed');
process.exit(fail ? 1 : 0);

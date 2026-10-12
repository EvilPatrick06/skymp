// test-stagger-relay.js
//
//     node skymp5-client/test-stagger-relay.js
//
// Thornswood #489. A person staggered in their own game sends "staggerStart"
// for their character like any other animation event, and every other machine
// plays it on their actor with Debug.sendAnimationEvent (sync/animation.ts,
// applyAnimation). deathService.ts blanked "staggerStart" for every actor from
// 0xff000000 up, so the event that came over the network was dropped on
// arrival and nobody else saw the stagger.
//
// WHAT THIS RUNS. The real sync/animation.ts and services/deathService.ts,
// their TypeScript types taken off (node's module.stripTypeScriptTypes, Node
// 22.13 or newer, or the typescript package), over a stand-in Skyrim Platform
// whose sendAnimationEvent hooks behave as the fork's do: handlers run in the
// order they were added, each only when its id range and its exact event name
// match the name as the handlers before it left it (Handler::Matches,
// Hook::HandleEnter), and Debug.sendAnimationEvent does not send the event
// inside the call but queues it for the game thread (CallNative.cpp,
// isSendAnimEvent), so the hooks run when the queue is run.
//
// What it checks:
//   1. a stagger that came over the network plays on that actor;
//   2. a stagger this game raises on somebody else's actor is still blanked;
//   3. one network stagger lets one through, not the next local one;
//   4. two network staggers queued before the game thread runs both play;
//   5. a network stagger for one actor does not let another actor's through;
//   6. your own character's stagger is not touched;
//   7. killmoves on somebody else's actor are still blanked, from the
//      network or not.
//
// ANIMATION=<file> and DEATH=<file> run it against other copies; against the
// code before the change, cases 1, 3a, 4 and 5b fail.
'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = __dirname;
const ANIMATION = process.env.ANIMATION || path.join(ROOT, 'src', 'sync', 'animation.ts');
const DEATH = process.env.DEATH || path.join(ROOT, 'src', 'services', 'services', 'deathService.ts');

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

// The loader of test-connection-denied.js, with export var for the enum.
function load(file, stubs) {
  let js = stripTypes(fs.readFileSync(file, 'utf8').split('\r').join(''));
  js = js.replace(/^import\s+\{([^}]*)\}\s+from\s+["']([^"']+)["'];?/gm, function (m, names, from) {
    const list = names.split(',').map(function (n) { return n.trim(); }).filter(Boolean)
      .map(function (n) { return n.replace(/^type\s+/, '').replace(/\s+as\s+/, ': '); })
      .filter(function (n) { return !/^\/\//.test(n); });
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
  return new Function('__stub', js)(function (from) { return stubs[from] || {}; });
}

// A stand-in Skyrim Platform: the hooks and the game thread queue.
const handlers = [];
const gameThread = [];
const played = [];   // [refrId, name as the game got it]
const hooks = {
  sendAnimationEvent: {
    add: function (h, minId, maxId, pattern) { handlers.push({ h: h, minId: minId, maxId: maxId, pattern: pattern }); },
  },
};
function matches(x, selfId, name) {
  if (x.minId !== undefined && selfId < x.minId) return false;
  if (x.maxId !== undefined && selfId > x.maxId) return false;
  if (x.pattern === undefined) return true;
  if (x.pattern.endsWith('*')) return name.startsWith(x.pattern.slice(0, -1));
  return name === x.pattern;
}
// What the game does with one event: every handler's enter, then the result.
function gameSends(selfId, name) {
  if (name === '') return;
  const ctx = { selfId: selfId, animEventName: name };
  handlers.forEach(function (x) {
    if (!matches(x, selfId, ctx.animEventName)) return;
    x.h.enter(ctx);
  });
  played.push([selfId, ctx.animEventName]);
  return ctx.animEventName;
}
function runGameThread() {
  gameThread.splice(0).forEach(function (f) { f(); });
}
function refr(id) {
  return { getFormID: function () { return id; }, is3DLoaded: function () { return true; } };
}
const Debug = {
  sendAnimationEvent: function (r, name) {
    const id = r.getFormID();
    gameThread.push(function () { gameSends(id, name); });
  },
};
const skyrimPlatform = {
  Debug: Debug,
  hooks: hooks,
  Actor: { from: function () { return null; } },
  ObjectReference: { from: function () { return null; } },
  printConsole: function () { },
  Utility: { wait: function () { return { then: function () { } }; } },
  Game: { getFormEx: function () { return null; }, getPlayer: function () { return null; } },
  storage: {},
  setCollision: function () { },
};

const animation = load(ANIMATION, {
  skyrimPlatform: skyrimPlatform,
  './movement': {},
  './movementApply': { applyWeapDrawn: function () { } },
});

class ClientListener { }
const death = load(DEATH, {
  skyrimPlatform: skyrimPlatform,
  '../events/applyDeathStateEvent': {},
  './clientListener': { ClientListener: ClientListener },
  '../../lib/errors': { RespawnNeededError: class extends Error { } },
  '../../sync/animation': animation,
  './ragdollService': { RagdollService: class { } },
});
const sp = { hooks: hooks, Game: skyrimPlatform.Game, Debug: Debug, Utility: skyrimPlatform.Utility };
const controller = { once: function () { }, emitter: { on: function () { } }, lookupListener: function () { return null; } };
new death.DeathService(sp, controller);

// The order the client sets these up in: DeathService is a service the
// controller makes, setupHooks runs in SkympClient after them.
if (typeof animation.setupHooks === 'function') animation.setupHooks();

const B = 0xff000a01;   // somebody else's character, as this machine knows it
const C = 0xff000a02;   // somebody else again
let changes = 0;
function fromNetwork(id, name) {
  const state = { lastNumChanges: 0, useAnimOverrides: false };
  changes++;
  animation.applyAnimation(refr(id), { animEventName: name, numChanges: changes }, state);
}
function last() { return played[played.length - 1]; }

// 1. From the network: plays.
fromNetwork(B, 'staggerStart');
yes('the call is queued, not sent inside Debug.sendAnimationEvent (the stand-in is the fork\'s shape)',
  gameThread.length === 1 && played.length === 0);
runGameThread();
yes('1. a stagger that came over the network plays on that actor', last()[0] === B && last()[1] === 'staggerStart');

// 2. Raised by this game on somebody else's actor: blanked.
gameSends(B, 'staggerStart');
yes('2. a stagger this game raises on somebody else\'s actor is still blanked', last()[0] === B && last()[1] === '');

// 3. One through for one from the network.
fromNetwork(B, 'staggerStart');
runGameThread();
yes('3a. a second stagger from the network plays', last()[1] === 'staggerStart');
gameSends(B, 'staggerStart');
yes('3b. and the local one after it is blanked, the count used up', last()[1] === '');

// 4. Two queued before the game thread runs.
fromNetwork(B, 'staggerStart');
fromNetwork(B, 'staggerStart');
runGameThread();
const lastTwo = played.slice(-2).map(function (p) { return p[1]; });
yes('4. two network staggers queued before the game thread runs both play', lastTwo[0] === 'staggerStart' && lastTwo[1] === 'staggerStart');
gameSends(B, 'staggerStart');
yes('4b. and none is left over for a local one', last()[1] === '');

// 5. Per actor.
fromNetwork(B, 'staggerStart');
gameSends(C, 'staggerStart');
yes('5. a network stagger for one actor does not let another actor\'s local one through', last()[0] === C && last()[1] === '');
runGameThread();
yes('5b. and still plays on the actor it came for', last()[0] === B && last()[1] === 'staggerStart');

// 6. Your own character.
gameSends(0x14, 'staggerStart');
yes('6. your own character\'s stagger is not touched', last()[0] === 0x14 && last()[1] === 'staggerStart');

// 7. Killmoves.
fromNetwork(B, 'KillMove1HMDecapSlash');
runGameThread();
yes('7a. a killmove on somebody else\'s actor from the network is still blanked', last()[1] === '');
gameSends(B, 'KillMoveDualWield');
yes('7b. and one this game raises', last()[1] === '');

console.log('');
console.log(pass + ' passed, ' + fail + ' failed');
process.exit(fail ? 1 : 0);

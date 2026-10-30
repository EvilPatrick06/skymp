// test-craft-work.js
//
//     node skymp5-client/test-craft-work.js
//
// Thornswood #3161. Patrick, 22 Oct 2026: crafting takes small 1 s, medium
// 5 s, large 15 s, with an animation, and moving away stops it. The game puts
// the made item in the pack the moment craft is pressed, so the client's
// craftService takes it straight back out and puts it in again when the
// server says the craft was made (the "craftMade" packet), and says when the
// person gets up from the bench (the "craftLeave" packet) so the server stops
// what it is still working. CraftService::OnCraftItem in the server has the
// other half.
//
// WHAT THIS RUNS. The real src/services/services/craftService.ts, its
// TypeScript types taken off (node's module.stripTypeScriptTypes, Node 22.13
// or newer, or the typescript package), over a stand-in player and menu whose
// removeItem and addItem fire containerChanged the way the game does, after
// the call returns.
//
// What it checks:
//   1. a craft at a bench sends one CraftItem with its inputs and result;
//   2. the made item leaves the pack at once, silently;
//   3. taking it out is not read as the input of a next craft;
//   4. craftMade puts back what the server made, base and count, while the
//      menu is open, and that is not read as a craft either;
//   5. a second craft after that sends only its own inputs;
//   6. closing the menu with a craft still being worked sends craftLeave,
//      once;
//   7. closing it with every craft made sends nothing;
//   8. craftMade after the menu has closed changes nothing here, since the
//      server's own inventory is applied then.
//
// Against the code before the change (CRAFT=<that copy>), 2, 4, 6 and 7 fail:
// the item stays in the pack, and nothing is put back or told to the server.
// 3, 5 and 8 pass there because it never hides or puts back anything.
'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = __dirname;
const CRAFT = process.env.CRAFT || path.join(ROOT, 'src', 'services', 'services', 'craftService.ts');

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

// The stand-in game. Events the game fires wait in a queue, as Skyrim
// Platform hands a containerChanged to its handlers after the change.
const PLAYER = 0x14;
const FORGE = 0x1ad6e;
const queue = [];
const handlers = {};
const emitted = [];
const packetHandlers = [];
let menuOpen = false;
const calls = [];

function fire(name, e) { (handlers[name] || []).forEach(function (h) { h(e); }); }
function run() { while (queue.length) { const ev = queue.shift(); fire('containerChanged', ev); } }
function form(id) { return { getFormID: function () { return id; } }; }
function moved(baseId, count, from, to) {
  queue.push({
    oldContainer: from ? form(from) : null, newContainer: to ? form(to) : null,
    baseObj: form(baseId), numItems: count,
  });
}
const player = {
  getFurnitureReference: function () { return menuOpen ? form(FORGE) : null; },
  removeItem: function (f, count, silent) { calls.push(['removeItem', f.getFormID(), count, silent]); moved(f.getFormID(), count, PLAYER, 0); },
  addItem: function (f, count, silent) { calls.push(['addItem', f.getFormID(), count, silent]); moved(f.getFormID(), count, 0, PLAYER); },
};
const sp = {
  Game: { getPlayer: function () { return player; }, getFormEx: function (id) { return form(id); } },
  Ui: { isMenuOpen: function (name) { return name === 'Crafting Menu' && menuOpen; } },
};
const controller = {
  on: function (name, h) { (handlers[name] = handlers[name] || []).push(h); },
  emitter: {
    on: function (name, h) { if (name === 'customPacketMessage') { packetHandlers.push(h); } },
    emit: function (name, e) { if (name === 'sendMessage') { emitted.push(e.message); } },
  },
};

class ClientListener { }
const M = load(CRAFT, {
  './clientListener': { ClientListener: ClientListener },
  '../../view/worldViewMisc': { localIdToRemoteId: function (id) { return id; } },
  '../../messages': { MsgType: { CustomPacket: 1, CraftItem: 13 } },
  '../../logging': { logTrace: function () { }, logError: function () { } },
  skyrimPlatform: {},
});
new M.CraftService(sp, controller);

function openMenu() { menuOpen = true; fire('menuOpen', { name: 'Crafting Menu' }); }
function closeMenu() { fire('menuClose', { name: 'Crafting Menu' }); menuOpen = false; run(); }
function serverSays(content) {
  packetHandlers.forEach(function (h) { h({ message: { t: 1, contentJsonDump: JSON.stringify(content) } }); });
  run();
}
// A craft as the game makes one: the inputs leave the pack, the item arrives.
function craft(inputs, result, count) {
  inputs.forEach(function (i) { moved(i[0], i[1], PLAYER, 0); });
  moved(result, count, 0, PLAYER);
  run();
}
function sent(t) { return emitted.filter(function (m) { return m.t === t; }); }
function leaves() { return sent(1).filter(function (m) { return JSON.parse(m.contentJsonDump).customPacketType === 'craftLeave'; }); }

const IRON = 0x5ace4, STRIPS = 0x800e4, STEEL = 0x5ace5, WARHAMMER = 0x1398a, DAGGER = 0x1397e;

openMenu();
craft([[IRON, 1], [STRIPS, 3], [STEEL, 4]], WARHAMMER, 1);
const first = sent(13);
yes('1. a craft sends one CraftItem with its inputs and result',
    first.length === 1 && first[0].data.workbench === FORGE && first[0].data.resultObjectId === WARHAMMER &&
    JSON.stringify(first[0].data.craftInputObjects.entries) ===
      JSON.stringify([{ baseId: IRON, count: 1 }, { baseId: STRIPS, count: 3 }, { baseId: STEEL, count: 4 }]));
yes('2. the made item leaves the pack at once, silently',
    calls.length === 1 && calls[0][0] === 'removeItem' && calls[0][1] === WARHAMMER && calls[0][2] === 1 && calls[0][3] === true);

craft([[IRON, 1]], DAGGER, 1);
const second = sent(13);
yes('3. taking it out is not read as the input of the next craft',
    second.length === 2 && JSON.stringify(second[1].data.craftInputObjects.entries) === JSON.stringify([{ baseId: IRON, count: 1 }]));

serverSays({ customPacketType: 'craftMade', baseId: WARHAMMER, count: 1 });
const put = calls.filter(function (c) { return c[0] === 'addItem'; });
yes('4. craftMade puts back what the server made, and it is not read as a craft',
    put.length === 1 && put[0][1] === WARHAMMER && put[0][2] === 1 && put[0][3] === true && sent(13).length === 2);

craft([[STEEL, 2]], DAGGER, 1);
const third = sent(13);
yes('5. a craft after that sends only its own inputs',
    third.length === 3 && JSON.stringify(third[2].data.craftInputObjects.entries) === JSON.stringify([{ baseId: STEEL, count: 2 }]));

// The dagger (second) and the dagger (third) are still being worked.
closeMenu();
yes('6. closing the menu with a craft still being worked sends craftLeave, once', leaves().length === 1);

openMenu();
craft([[IRON, 1]], DAGGER, 1);
serverSays({ customPacketType: 'craftMade', baseId: DAGGER, count: 1 });
closeMenu();
yes('7. closing it with every craft made sends nothing', leaves().length === 1);

const before = calls.length;
serverSays({ customPacketType: 'craftMade', baseId: DAGGER, count: 1 });
yes('8. craftMade after the menu has closed changes nothing here', calls.length === before);

console.log('');
console.log(pass + ' passed, ' + fail + ' failed');
process.exit(fail ? 1 : 0);

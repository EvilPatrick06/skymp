// test-player-record-spells.js
//
//     node skymp5-client/test-player-record-spells.js
//
// Thornswood #1688. The local player is the Player record (0x7), and that
// record's spell list is what a new game starts with: Flames and Healing, and
// the PCHealRateCombat ability. sync/spell.ts removeAllSpells walked only the
// spells added to the actor (Actor.getNthSpell), so when a person spawned and
// the client rebuilt their spells from the server's list, the two from the
// record stayed in the magic menu and could be cast, whatever the server said.
//
// WHAT THIS RUNS. The real src/sync/spell.ts, its TypeScript types taken off
// (node's module.stripTypeScriptTypes, Node 22.13 or newer, or the typescript
// package), over a stand-in player whose base record carries Flames, Healing
// and the ability, and a stand-in NPC on another base.
//
// What it checks:
//   1. removeAllSpells on the player takes Flames and Healing off;
//   2. it leaves the record's ability on;
//   3. it still takes off every spell added to the player, once each;
//   4. learnSpells afterwards puts back exactly the server's list, so a mage
//      the server granted Flames has it again;
//   5. an actor on any other base keeps its base spells;
//   6. remoteServer.ts still clears the spells before it learns the list.
//
// Against the code before the change (SPELL=<that copy>), 1 fails.
'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = __dirname;
const SPELL = process.env.SPELL || path.join(ROOT, 'src', 'sync', 'spell.ts');
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

// The stand-in game. Casting types: 0 constant effect, 1 fire and forget,
// 2 concentration (MagicEffect.GetCastingType).
const forms = {};
function effect(castingType) { return { getCastingType: function () { return castingType; } }; }
function spell(id, name, castingTypes) {
  const s = {
    getFormID: function () { return id; },
    getName: function () { return name; },
    getNumEffects: function () { return castingTypes.length; },
    getNthEffectMagicEffect: function (i) { return effect(castingTypes[i]); },
  };
  forms[id] = s;
  return s;
}
const FLAMES = spell(0x12fcd, 'Flames', [2]);
const HEALING = spell(0x12fcc, 'Healing', [2]);
const HEAL_RATE = spell(0x1031d3, 'PCHealRateCombat', [0]);
const SPARKS = spell(0x2dd2a, 'Sparks', [2]);
const OAKFLESH = spell(0x5ad5c, 'Oakflesh', [1]);

// An actor as the game keeps one: the spells added to it, and its base
// record's spell list. RemoveSpell on the player takes a spell off either,
// the way player.removespell does (the Player record is the player's alone).
function makeActor(baseId, baseSpells, added) {
  const base = {
    spells: baseSpells.slice(),
    getFormID: function () { return baseId; },
    getSpellCount: function () { return base.spells.length; },
    getNthSpell: function (i) { return base.spells[i] || null; },
  };
  const a = {
    base: base,
    added: added.slice(),
    removes: [],
    getBaseObject: function () { return base; },
    getSpellCount: function () { return a.added.length; },
    getNthSpell: function (i) { return a.added[i] || null; },
    removeSpell: function (s) {
      a.removes.push(s.getFormID());
      let i = a.added.indexOf(s);
      if (i !== -1) { a.added.splice(i, 1); return true; }
      i = base.spells.indexOf(s);
      if (i !== -1) { base.spells.splice(i, 1); return true; }
      return false;
    },
    addSpell: function (s) { if (a.added.indexOf(s) === -1) { a.added.push(s); } return true; },
    spellIds: function () {
      return base.spells.concat(a.added).map(function (s) { return s.getFormID(); }).sort();
    },
  };
  return a;
}

const sp = {
  Actor: {},
  ActorBase: { from: function (f) { return f; } },
  Spell: { from: function (f) { return f; } },
  Game: { getFormEx: function (id) { return forms[id] || null; } },
  printConsole: function () { },
};
const M = load(SPELL, { skyrimPlatform: sp });

// 1-3. A person who learned Sparks and Oakflesh on the server, spawning.
const player = makeActor(0x7, [FLAMES, HEALING, HEAL_RATE], [SPARKS, OAKFLESH]);
M.removeAllSpells(player);
const left = player.spellIds();
yes('removeAllSpells takes Flames off the player', left.indexOf(0x12fcd) === -1);
yes('removeAllSpells takes Healing off the player', left.indexOf(0x12fcc) === -1);
yes('the Player record ability stays', left.length === 1 && left[0] === 0x1031d3);
yes('every added spell came off, each once',
    player.added.length === 0 &&
    player.removes.filter(function (id) { return id === 0x2dd2a; }).length === 1 &&
    player.removes.filter(function (id) { return id === 0x5ad5c; }).length === 1);

// 4. The server's list goes back on: a mage it granted Flames.
M.learnSpells(player, [0x12fcd, 0x2dd2a]);
yes('learnSpells puts back exactly the server list',
    JSON.stringify(player.spellIds()) === JSON.stringify([0x12fcd, 0x1031d3, 0x2dd2a].sort()));

// 5. Any other base keeps its spell list.
const npc = makeActor(0x10abac, [FLAMES, HEALING], []);
M.removeAllSpells(npc);
yes('an actor on another base keeps its base spells',
    JSON.stringify(npc.spellIds()) === JSON.stringify([0x12fcc, 0x12fcd]));

// 6. The spawn path still clears before it learns.
const remote = fs.readFileSync(REMOTE, 'utf8');
const clear = remote.indexOf('removeAllSpells(player);');
const learn = remote.indexOf('learnSpells(player, learnedSpells);');
yes('remoteServer.ts clears the spells, then learns the server list', clear !== -1 && learn > clear);

console.log('\n' + pass + ' passed, ' + fail + ' failed');
process.exit(fail ? 1 : 0);

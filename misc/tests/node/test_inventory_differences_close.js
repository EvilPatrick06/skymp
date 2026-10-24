'use strict';
// Thornswood #2146. The client stopped applying an inventory difference that
// came back three times in a row instead of finding why applying did not
// close it. This loads the real inventory.ts over a stand-in game inventory
// and checks the two causes are gone:
//   - a tempered piece's health from the server (a float, so 1.2 arrives as
//     1.2000000476837158) equals the game's reading of it (1.2);
//   - a piece is taken away by the game's own list (removeInventoryItems),
//     so a worn piece tempered at a bench, which the server keeps plain, is
//     the one taken away, and the next pass finds nothing to do;
// and that the give-up counter is out of remoteServer.ts.
// INVENTORY=<file> runs it against another inventory.ts (the one before the
// change fails the health and take-away cases).
const fs = require('node:fs'), path = require('node:path'), vm = require('node:vm');
const ts = require('../../../skymp5-client/node_modules/typescript');
const file = process.env.INVENTORY ||
  path.join(__dirname, '../../../skymp5-client/src/sync/inventory.ts');
const js = ts.transpileModule(fs.readFileSync(file, 'utf8'), {
  compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2018 },
}).outputText;

let pass = 0, fail = 0;
function yes(what, cond) {
  if (cond) { pass++; console.log('PASS  ' + what); } else { fail++; console.log('FAIL  ' + what); }
}

const SWORD = 0x12eb7, BOOTS = 0x12e4b, GOLD = 0xf;
const NAMES = { [SWORD]: 'Iron Sword', [BOOTS]: 'Boots', [GOLD]: 'Gold' };
const FormType = { Misc: 32, Potion: 46, Ingredient: 30, Weapon: 41, Armor: 26 };
const BASE = 7;

// The game: the character's extra lists (0x14), each { baseId, count, name,
// health, worn }, and units with no list of their own in the base container
// (base) and in the changes (loose). A removal by a list the game does not
// hold (addItemEx with a negative count) takes plain units first and never a
// worn list: one choice the game can make, and the one that kept the boots.
function makeGame() {
  const game = { lists: [], base: {}, loose: {}, calls: [], wornState: null, console: [] };
  class Form {
    constructor(id) { this.id = id; }
    getFormID() { return this.id; }
    getName() { return NAMES[this.id] || ''; }
    getType() { return this.id === GOLD ? FormType.Misc : this.id === BOOTS ? FormType.Armor : FormType.Weapon; }
  }
  class ObjectReference extends Form {
    getBaseObject() { return new Form(BASE); }
    removeAllItems() { game.calls.push(['removeAllItems']); }
  }
  const pc = new ObjectReference(0x14);
  const takePlain = (baseId, n) => {
    for (const where of [game.loose, game.base]) {
      const k = Math.min(n, where[baseId] || 0);
      where[baseId] = (where[baseId] || 0) - k;
      n -= k;
    }
    return n;
  };
  const sp = {
    Form, ObjectReference, FormType,
    Game: { getFormEx: (id) => (NAMES[id] ? new Form(id) : null) },
    Ammo: { from: () => null }, Actor: { from: (r) => r && Object.assign(r, { queueNiNodeUpdate() {} }) },
    Enchantment: { from: () => null }, Potion: { from: () => null },
    printConsole: (...a) => { game.console.push(a.join(' ')); },
    storage: { basesResetExists: true, basesReset: new Set([BASE]) },
    getContainer: (id) => (id === BASE
      ? Object.keys(game.base).filter((k) => game.base[k] > 0).map((k) => ({ baseId: Number(k), count: game.base[k] }))
      : []),
    getExtraContainerChanges: () => {
      const byBase = new Map();
      const entry = (baseId) => {
        if (!byBase.has(baseId)) byBase.set(baseId, { baseId, countDelta: 0, extendDataList: [] });
        return byBase.get(baseId);
      };
      for (const l of game.lists) {
        const e = entry(l.baseId);
        e.countDelta += l.count;
        const extras = [];
        if (l.count !== 1) extras.push({ type: 'Count', count: l.count });
        if (l.name) extras.push({ type: 'TextDisplayData', name: l.name });
        if (l.health) extras.push({ type: 'Health', health: l.health });
        if (l.worn) extras.push({ type: 'Worn' });
        e.extendDataList.push(extras);
      }
      for (const k of Object.keys(game.loose)) entry(Number(k)).countDelta += game.loose[k];
      return [...byBase.values()];
    },
    renameInventoryItems: () => false,
    removeInventoryItems: (refrId, baseId, listCount, indexes, counts, plain) => {
      game.calls.push(['remove', baseId, indexes.slice(), counts.slice(), plain]);
      if (game.arrives) { game.lists.push(game.arrives); game.arrives = null; }
      const lists = game.lists.filter((l) => l.baseId === baseId);
      if (lists.length !== listCount) return false;
      indexes.forEach((i, k) => { lists[i].count -= counts[k]; });
      game.lists = game.lists.filter((l) => l.count > 0);
      // what the base container held and is gone is a negative delta
      game.loose[baseId] = (game.loose[baseId] || 0) - plain;
      return true;
    },
    TESModPlatform: {
      resetContainer() {},
      pushWornState(worn, wornLeft) { game.wornState = { worn, wornLeft }; },
      addItemEx(refr, f, count, health, ench, maxCharge, rm, charge, name) {
        const worn = !!(game.wornState && game.wornState.worn);
        game.wornState = null;
        game.calls.push(['addItemEx', f.getFormID(), count, name, health, worn]);
        if (count > 0) {
          // the game keeps a float: 1.2000000476837158 is 1.2 to it
          const l = { baseId: f.getFormID(), count, name };
          if (health > 1) l.health = Math.fround(health);
          if (worn) l.worn = true;
          game.lists.push(l);
          return;
        }
        let left = takePlain(f.getFormID(), -count);
        for (const l of game.lists) {
          if (left && !l.worn && l.baseId === f.getFormID() && (l.name || '') === name &&
              Math.fround(l.health || 1) === Math.fround(health || 1)) {
            const n = Math.min(left, l.count); l.count -= n; left -= n;
          }
        }
        game.lists = game.lists.filter((l) => l.count > 0);
      },
    },
  };
  const sandbox = { exports: {}, require: (m) => (m === 'skyrimPlatform' ? sp : {}) };
  sandbox.module = { exports: sandbox.exports };
  vm.runInNewContext(js, sandbox);
  game.inv = sandbox.exports;
  game.pc = pc;
  game.apply = (entries) => game.inv.applyInventory(pc, { entries }, false, true);
  game.diff = (entries) => game.inv.getDiff({ entries }, game.inv.getInventory(pc), true).entries;
  return game;
}

// A fine sword the server holds: 1.1 arrives as 1.100000023841858.
{
  const g = makeGame();
  const server = [{ baseId: SWORD, count: 1, health: 1.100000023841858 }];
  g.lists = [{ baseId: SWORD, count: 1, name: 'Iron Sword (Fine)', health: Math.fround(1.1) }];
  yes('the server\'s 1.100000023841858 is the game\'s fine sword, so there is no difference',
    g.diff(server).length === 0);
  g.apply(server);
  yes('and nothing is taken away or added for it', g.calls.length === 0);
}

// A legendary piece whose health is 1.6 to the game and 1.600000023841858 to the server.
{
  const g = makeGame();
  const server = [{ baseId: SWORD, count: 1, name: 'loxsword', health: 1.600000023841858 }];
  g.lists = [{ baseId: SWORD, count: 1, name: 'loxsword (Legendary)', health: Math.fround(1.6), worn: true }];
  yes('a legendary "loxsword" from the server is the game\'s "loxsword (Legendary)"', g.diff(server).length === 0);
}

// A sword the server holds tempered and the game holds plain: one add, then nothing.
{
  const g = makeGame();
  const server = [{ baseId: SWORD, count: 1, health: 1.2000000476837158 }];
  g.lists = [{ baseId: SWORD, count: 1, name: 'Iron Sword' }];
  g.apply(server);
  const adds = g.calls.filter((c) => c[0] === 'addItemEx');
  yes('a plain sword the server holds tempered is taken away by its own list and added tempered',
    adds.length === 1 && adds[0][2] === 1 && g.calls.some((c) => c[0] === 'remove' && c[2][0] === 0));
  g.calls.length = 0;
  g.apply(server);
  yes('and the next pass finds nothing to do (it used to add it again every pass)', g.calls.length === 0);
}

// The bench: boots tempered in the game while worn, kept plain by the server.
{
  const g = makeGame();
  const server = [{ baseId: BOOTS, count: 1 }];
  g.lists = [{ baseId: BOOTS, count: 1, name: 'Boots (Fine)', health: Math.fround(1.1), worn: true }];
  g.apply(server);
  const removes = g.calls.filter((c) => c[0] === 'remove');
  const adds = g.calls.filter((c) => c[0] === 'addItemEx');
  yes('the worn tempered boots are taken away by their own list',
    removes.length === 1 && JSON.stringify(removes[0].slice(1)) === JSON.stringify([BOOTS, [0], [1], 0]) &&
    !g.calls.some((c) => c[0] === 'addItemEx' && c[2] < 0));
  yes('the plain boots the server holds are added once', adds.length === 1 && adds[0][2] === 1);
  yes('and the game holds just the plain boots', g.lists.length === 1 && !g.lists[0].health);
  for (let pass = 0; pass < 3; ++pass) {
    g.calls.length = 0;
    g.apply(server);
  }
  yes('three passes later nothing has been added again', g.calls.length === 0);
}

// Two equal pieces, one worn, the server holds one: the one on the body stays.
{
  const g = makeGame();
  g.lists = [{ baseId: SWORD, count: 1, name: 'Iron Sword', worn: true }, { baseId: SWORD, count: 1, name: 'Iron Sword' }];
  g.apply([{ baseId: SWORD, count: 1 }]);
  const r = g.calls.find((c) => c[0] === 'remove');
  yes('of two equal swords, the one not worn is taken away', !!r && JSON.stringify(r[2]) === '[1]' &&
    g.lists.length === 1 && g.lists[0].worn);
}

// Gold in the base container, none on the server: plain units, no list.
{
  const g = makeGame();
  g.base[GOLD] = 40;
  g.apply([]);
  const r = g.calls.find((c) => c[0] === 'remove');
  yes('40 gold with no list of their own are taken away as plain units', !!r && r[1] === GOLD && r[2].length === 0 && r[4] === 40);
}

// The lists changed between the read and the removal: nothing is taken, and it says so.
{
  const g = makeGame();
  g.lists = [{ baseId: SWORD, count: 1, name: 'Iron Sword', health: Math.fround(1.2) }];
  g.arrives = { baseId: SWORD, count: 1, name: 'Iron Sword' };
  g.apply([]);
  yes('a removal whose lists changed takes nothing and says so in the log',
    g.lists.length === 2 && g.console.some((l) => /refused to take away 12eb7/.test(l)));
  g.calls.length = 0;
  g.apply([]);
  yes('and the next pass, reading the lists again, takes both away',
    g.lists.length === 0 && g.calls.filter((c) => c[0] === 'remove').length === 1);
}

// The give-up counter is gone from remoteServer.ts.
{
  const src = fs.readFileSync(path.join(__dirname,
    '../../../skymp5-client/src/services/services/remoteServer.ts'), 'utf8');
  yes('remoteServer.ts no longer leaves a difference alone after three tries',
    !/PC_INV_GIVE_UP_AFTER|pcInvStuckSig|pcInvStuckFor/.test(src));
  yes('and it writes a difference that an apply did not close to the log', /is still there, applying it again/.test(src));
}

console.log('\n' + pass + ' passed, ' + fail + ' failed');
if (fail) process.exitCode = 1;

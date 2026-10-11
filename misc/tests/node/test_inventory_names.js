'use strict';
// Thornswood #1651. A piece's new condition does not reach its holder's
// inventory until the next login.
//
// A Thornswood piece carries its condition on the end of its name, "Iron
// Sword (85%)" (game/gamemode.js, the durability block, #1384). sync/
// inventory.ts compared names after dropping any trailing "(...)" from both,
// so "Iron Sword (85%)" equalled "Iron Sword (84%)" and a plain "Iron Sword",
// and applyInventory never applied the server's rename. This loads the real
// inventory.ts over a stand-in game inventory and checks:
//   - names compare exactly, except the level the game adds to a tempered
//     piece ("loxsword (Legendary)") and no name meaning the base's own name;
//   - a rename reaches the game as a rename (renameInventoryItems), so a
//     worn piece stays worn, and is applied once, not every pass;
//   - when the game cannot rename in place, the change is applied the old
//     way, by taking away and adding.
// INVENTORY=<file> runs it against another inventory.ts (the one before the
// change fails the name cases).
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

const SWORD = 0x12eb7, DAGGER = 0x139b5, LOX = 0x1a0f0, GOLD = 0xf;
const NAMES = { [SWORD]: 'Iron Sword', [DAGGER]: 'Steel Dagger', [LOX]: 'Steel Sword', [GOLD]: 'Gold' };
const FormType = { Misc: 32, Potion: 46, Ingredient: 30, Weapon: 41 };

// The game: the character's extra lists (0x14), each { baseId, count, name, health, worn }.
function makeGame() {
  const game = { lists: [], calls: [], renameWorks: true, wornState: null };
  class Form {
    constructor(id) { this.id = id; }
    getFormID() { return this.id; }
    getName() { return NAMES[this.id] || ''; }
    getType() { return this.id === GOLD ? FormType.Misc : FormType.Weapon; }
  }
  class ObjectReference extends Form {
    getBaseObject() { return new Form(7); }
    removeAllItems() { game.calls.push(['removeAllItems']); }
  }
  const pc = new ObjectReference(0x14);
  const sp = {
    Form, ObjectReference, FormType,
    Game: { getFormEx: (id) => (NAMES[id] ? new Form(id) : null) },
    Ammo: { from: () => null }, Actor: { from: (r) => r && Object.assign(r, { queueNiNodeUpdate() {} }) },
    Enchantment: { from: () => null }, Potion: { from: () => null },
    printConsole: () => {},
    storage: { basesResetExists: true, basesReset: new Set([7]) },
    getContainer: () => [],
    getExtraContainerChanges: () => {
      const byBase = new Map();
      for (const l of game.lists) {
        if (!byBase.has(l.baseId)) byBase.set(l.baseId, { baseId: l.baseId, countDelta: 0, extendDataList: [] });
        const e = byBase.get(l.baseId);
        e.countDelta += l.count;
        const extras = [];
        if (l.count !== 1) extras.push({ type: 'Count', count: l.count });
        if (l.name) extras.push({ type: 'TextDisplayData', name: l.name });
        if (l.health) extras.push({ type: 'Health', health: l.health });
        if (l.worn) extras.push({ type: 'Worn' });
        e.extendDataList.push(extras);
      }
      return [...byBase.values()];
    },
    renameInventoryItems: (refrId, baseId, from, to, count) => {
      game.calls.push(['rename', refrId, baseId, from, to, count]);
      if (!game.renameWorks) return false;
      const lists = game.lists.filter((l) => l.baseId === baseId && (l.name || '') === from)
        .sort((a, b) => (b.worn ? 1 : 0) - (a.worn ? 1 : 0));
      const chosen = [];
      let left = count;
      for (const l of lists) { if (l.count <= left) { chosen.push(l); left -= l.count; } if (!left) break; }
      if (left) return false;
      for (const l of chosen) l.name = to;
      return true;
    },
    TESModPlatform: {
      resetContainer() {},
      pushWornState(worn, wornLeft) { game.wornState = { worn, wornLeft }; },
      addItemEx(refr, f, count, health, ench, maxCharge, rm, charge, name) {
        const worn = !!(game.wornState && game.wornState.worn);
        game.wornState = null;
        game.calls.push(['addItemEx', f.getFormID(), count, name, worn]);
        if (count > 0) {
          const l = { baseId: f.getFormID(), count, name };
          if (health > 1) l.health = health;
          if (worn) l.worn = true;
          game.lists.push(l);
        } else {
          let left = -count;
          for (const l of game.lists) {
            if (left && l.baseId === f.getFormID() && (l.name || '') === name && (l.health || 1) === (health || 1)) {
              const n = Math.min(left, l.count); l.count -= n; left -= n;
            }
          }
          game.lists = game.lists.filter((l) => l.count > 0);
        }
      },
    },
  };
  const sandbox = { exports: {}, require: (m) => (m === 'skyrimPlatform' ? sp : {}) };
  sandbox.module = { exports: sandbox.exports };
  vm.runInNewContext(js, sandbox);
  game.inv = sandbox.exports;
  game.pc = pc;
  game.apply = (entries) => game.inv.applyInventory(pc, { entries }, false, true);
  return game;
}

// Names.
{
  const { inv } = makeGame();
  const eq = (a, b) => inv.inventoryEntriesEqual(Object.assign({ baseId: SWORD, count: 1 }, a), Object.assign({ baseId: SWORD, count: 1 }, b));
  yes('"Iron Sword (85%)" is not "Iron Sword (84%)"', !eq({ name: 'Iron Sword (85%)' }, { name: 'Iron Sword (84%)' }));
  yes('"Iron Sword (85%)" is not a plain Iron Sword (no name of its own)', !eq({ name: 'Iron Sword (85%)' }, {}));
  yes('"Iron Sword (85%)" is not "Iron Sword", the name applyInventory writes on a plain one', !eq({ name: 'Iron Sword (85%)' }, { name: 'Iron Sword' }));
  yes('a piece with no name is the one the game calls "Iron Sword"', eq({}, { name: 'Iron Sword' }));
  yes('"Iron Sword (85%)" is "Iron Sword (85%)"', eq({ name: 'Iron Sword (85%)' }, { name: 'Iron Sword (85%)' }));
  yes('a legendary "loxsword" is the game\'s "loxsword (Legendary)"', eq({ name: 'loxsword', health: 1.6 }, { name: 'loxsword (Legendary)', health: 1.6 }));
  yes('a fine piece with no name is the game\'s "Iron Sword (Fine)"', eq({ health: 1.1 }, { name: 'Iron Sword (Fine)', health: 1.1 }));
  yes('a fine "Iron Sword (85%)" is the game\'s "Iron Sword (85%) (Fine)"', eq({ name: 'Iron Sword (85%)', health: 1.1 }, { name: 'Iron Sword (85%) (Fine)', health: 1.1 }));
  yes('a fine "Iron Sword (84%)" is not the game\'s "Iron Sword (85%) (Fine)"', !eq({ name: 'Iron Sword (84%)', health: 1.1 }, { name: 'Iron Sword (85%) (Fine)', health: 1.1 }));
  yes('a piece that is not tempered gets no level added, so "Iron Sword (Fine)" is not a plain one', !eq({}, { name: 'Iron Sword (Fine)' }));
  const except = inv.inventoryEntriesEqualExceptName || (() => false);
  yes('inventoryEntriesEqualExceptName finds the same piece under its new name',
    except({ baseId: SWORD, count: 1, name: 'Iron Sword (85%)', worn: true }, { baseId: SWORD, count: 1, name: 'Iron Sword (84%)' }, true));
  yes('and not a different piece', !!inv.inventoryEntriesEqualExceptName && !except({ baseId: SWORD, count: 1, name: 'a', health: 1.2 }, { baseId: SWORD, count: 1, name: 'b' }, true));
}

// A hit on a worn sword: 85% to 84%.
{
  const g = makeGame();
  if (typeof g.inv.inventoryEntriesEqualExceptName === 'function') {
    g.lists = [{ baseId: SWORD, count: 1, name: 'Iron Sword (85%)', worn: true }, { baseId: GOLD, count: 40 }];
    g.apply([{ baseId: SWORD, count: 1, name: 'Iron Sword (84%)' }, { baseId: GOLD, count: 40 }]);
    const renames = g.calls.filter((c) => c[0] === 'rename');
    yes('a worn sword renamed by the server is renamed in the game, once, asked ' + renames.length,
      renames.length === 1 && renames[0][1] === 0x14 && renames[0][2] === SWORD &&
      renames[0][3] === 'Iron Sword (85%)' && renames[0][4] === 'Iron Sword (84%)' && renames[0][5] === 1);
    yes('nothing is taken away or added for it', !g.calls.some((c) => c[0] === 'addItemEx'));
    yes('it is still worn and now reads 84%', g.lists.some((l) => l.baseId === SWORD && l.worn && l.name === 'Iron Sword (84%)'));
    g.calls.length = 0;
    g.apply([{ baseId: SWORD, count: 1, name: 'Iron Sword (84%)' }, { baseId: GOLD, count: 40 }]);
    yes('the next pass finds nothing to do', g.calls.length === 0);
  } else {
    yes('a worn sword renamed by the server is renamed in the game (this inventory.ts cannot)', false);
  }
}

// The first hit on a sound piece: plain to 99%, and a repair back to 100%.
{
  const g = makeGame();
  g.lists = [{ baseId: SWORD, count: 1, name: 'Iron Sword', worn: true }];
  g.apply([{ baseId: SWORD, count: 1, name: 'Iron Sword (99%)' }]);
  yes('a sound sword hit for the first time is renamed "Iron Sword (99%)" and stays worn',
    g.lists.length === 1 && g.lists[0].worn && g.lists[0].name === 'Iron Sword (99%)' && !g.calls.some((c) => c[0] === 'addItemEx'));
  g.calls.length = 0;
  g.apply([{ baseId: SWORD, count: 1 }]);
  const r = g.calls.find((c) => c[0] === 'rename');
  yes('a repair to 100% (no name on the server) renames it back to the base\'s name',
    !!r && r[3] === 'Iron Sword (99%)' && r[4] === 'Iron Sword' && g.lists[0].worn && g.lists[0].name === 'Iron Sword');
}

// Two swords, the worn one hit.
{
  const g = makeGame();
  g.lists = [{ baseId: SWORD, count: 1, name: 'Iron Sword' }, { baseId: SWORD, count: 1, name: 'Iron Sword', worn: true }];
  g.apply([{ baseId: SWORD, count: 1 }, { baseId: SWORD, count: 1, name: 'Iron Sword (99%)' }]);
  const r = g.calls.find((c) => c[0] === 'rename');
  yes('of two swords, the rename asks for the name the game spells ("Iron Sword", not the server\'s none)', !!r && r[3] === 'Iron Sword');
  yes('and the worn one is the one renamed', g.lists.some((l) => l.worn && l.name === 'Iron Sword (99%)') &&
    g.lists.some((l) => !l.worn && l.name === 'Iron Sword') && !g.calls.some((c) => c[0] === 'addItemEx'));
}

// The game cannot rename in place: the old way.
{
  const g = makeGame();
  g.lists = [{ baseId: DAGGER, count: 3, name: 'Steel Dagger (60%)' }];
  g.apply([{ baseId: DAGGER, count: 2, name: 'Steel Dagger (60%)' }, { baseId: DAGGER, count: 1, name: 'Steel Dagger (55%)' }]);
  const adds = g.calls.filter((c) => c[0] === 'addItemEx');
  yes('one of a stack of three renamed: no list holds exactly one, so one is taken away and one added',
    g.calls.some((c) => c[0] === 'rename') && adds.length === 2 &&
    adds.some((c) => c[2] === -1 && c[3] === 'Steel Dagger (60%)') && adds.some((c) => c[2] === 1 && c[3] === 'Steel Dagger (55%)'));
  g.calls.length = 0;
  g.apply([{ baseId: DAGGER, count: 2, name: 'Steel Dagger (60%)' }, { baseId: DAGGER, count: 1, name: 'Steel Dagger (55%)' }]);
  yes('and then nothing is left to do', g.calls.length === 0);
}

// What getRealName was written for still holds.
{
  const g = makeGame();
  g.lists = [{ baseId: LOX, count: 1, name: 'loxsword (Legendary)', health: 1.6, worn: true }];
  g.apply([{ baseId: LOX, count: 1, name: 'loxsword', health: 1.6 }]);
  yes('a legendary "loxsword" the game shows as "loxsword (Legendary)" is left alone', g.calls.length === 0);
  g.lists = [{ baseId: SWORD, count: 1, name: 'Iron Sword (Fine)', health: 1.1 }];
  g.apply([{ baseId: SWORD, count: 1, health: 1.1 }]);
  yes('a fine sword with no name the game shows as "Iron Sword (Fine)" is left alone', g.calls.length === 0);
  g.lists = [{ baseId: SWORD, count: 1, name: 'Iron Sword' }];
  g.apply([{ baseId: SWORD, count: 1 }]);
  yes('a plain sword the game names "Iron Sword" is left alone', g.calls.length === 0);
}

console.log('\n' + pass + ' passed, ' + fail + ' failed');
if (fail) process.exitCode = 1;

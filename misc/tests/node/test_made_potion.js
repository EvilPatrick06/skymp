'use strict';
// Thornswood #1649. A brewed potion is a form only the brewing session has.
//
// The game makes a brewed bottle at the lab as a runtime form (above
// 0xff000000). The server kept that id, so in the next session, or on any
// other client, Game.getFormEx found nothing under it (or another form the
// game had made) and the bottle was gone or wrong. Now the server keeps the
// bottle by its contents (Inventory::Made) under an id in 0xff800000 and up,
// and sync/inventory.ts builds a potion with that id (createMadePotion).
// This loads the real inventory.ts over a stand-in game and checks:
//   - readMadePotion reads what is in a bottle the game brewed, for the
//     craft message, and nothing for a plugin's potion or one already built;
//   - a bottle the server sends by its contents is built once, under the
//     server's id, and added; the next pass finds nothing to do;
//   - the brewing session's own runtime bottle gives way to the server's;
//   - a bottle the server no longer holds is taken away (#2146 takes units
//     away by the game's lists; a bottle's contents are its form's, so its
//     units with no list are its plain units);
//   - a new session (nothing built yet) builds it again from the server;
//   - two contents under one id, or no createMadePotion, build nothing and
//     do not throw.
// INVENTORY=<file> runs it against another inventory.ts (the one before the
// change fails the build and add cases).
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

const FormType = { Misc: 32, Potion: 46, Ingredient: 30, Weapon: 41 };
const RESTORE_HEALTH = 0x3eb15, FORTIFY_HEALTH = 0x3eaf3, MINOR_HEALING = 0x3eadd;
const BREWED = 0xff000a01; // what the brewing game called its bottle
const MADE = {
  name: 'Potion of Restore Health', poison: false,
  effects: [
    { effectId: RESTORE_HEALTH, magnitude: 23.45, area: 0, duration: 0 },
    { effectId: FORTIFY_HEALTH, magnitude: 18.2, area: 0, duration: 60 },
  ],
};
const H = 0xff9a3c11; // the server's id for MADE (any id in the made range)

// forms: id -> { name, type, effects?, poison? }
function makeGame({ native = true } = {}) {
  const game = { lists: [], calls: [], forms: new Map(), console: [] };
  game.forms.set(MINOR_HEALING, { name: 'Potion of Minor Healing', type: FormType.Potion,
    effects: [{ effectId: RESTORE_HEALTH, magnitude: 25, area: 0, duration: 0 }], poison: false });
  game.forms.set(BREWED, { name: MADE.name, type: FormType.Potion, poison: false,
    effects: [{ effectId: RESTORE_HEALTH, magnitude: 23.4512, area: 0, duration: 0 },
              { effectId: FORTIFY_HEALTH, magnitude: 18.2, area: 0, duration: 60 }] });
  class Form {
    constructor(id) { this.id = id; }
    getFormID() { return this.id; }
    getName() { const f = game.forms.get(this.id); return f ? f.name : ''; }
    getType() { const f = game.forms.get(this.id); return f ? f.type : FormType.Misc; }
  }
  class MagicEffect extends Form {}
  const Potion = {
    from(form) {
      if (!form) return null;
      const f = game.forms.get(form.getFormID());
      if (!f || f.type !== FormType.Potion) return null;
      return {
        getNumEffects: () => f.effects.length,
        getNthEffectMagicEffect: (i) => new MagicEffect(f.effects[i].effectId),
        getNthEffectMagnitude: (i) => f.effects[i].magnitude,
        getNthEffectArea: (i) => f.effects[i].area,
        getNthEffectDuration: (i) => f.effects[i].duration,
        isPoison: () => !!f.poison,
      };
    },
  };
  class ObjectReference extends Form {
    getBaseObject() { return new Form(7); }
    removeAllItems() {}
  }
  const pc = new ObjectReference(0x14);
  const sp = {
    Form, ObjectReference, FormType, Potion,
    Game: { getFormEx: (id) => (game.forms.has(id) ? new Form(id) : null) },
    Ammo: { from: () => null }, Actor: { from: (r) => r && Object.assign(r, { queueNiNodeUpdate() {} }) },
    Enchantment: { from: () => null },
    printConsole: (...a) => game.console.push(a.join(' ')),
    storage: { basesResetExists: true, basesReset: new Set([7]) },
    getContainer: () => [],
    getExtraContainerChanges: () => {
      const byBase = new Map();
      for (const l of game.lists) {
        if (!byBase.has(l.baseId)) byBase.set(l.baseId, { baseId: l.baseId, countDelta: 0, extendDataList: [] });
        byBase.get(l.baseId).countDelta += l.count;
      }
      return [...byBase.values()];
    },
    renameInventoryItems: () => false,
    // Thornswood #2146. Every unit here has no list of its own (the changes
    // count them), so only plain units can be taken away.
    removeInventoryItems: (refrId, baseId, listCount, indexes, counts, plain) => {
      game.calls.push(['remove', baseId, indexes.slice(), counts.slice(), plain]);
      if (listCount !== 0 || indexes.length) return false;
      const l = game.lists.find((x) => x.baseId === baseId);
      if (!l || l.count < plain) return false;
      l.count -= plain;
      game.lists = game.lists.filter((x) => x.count > 0);
      return true;
    },
    TESModPlatform: {
      resetContainer() {},
      pushWornState() {},
      addItemEx(refr, f, count) {
        game.calls.push(['addItemEx', f.getFormID(), count]);
        const l = game.lists.find((x) => x.baseId === f.getFormID());
        if (l) l.count += count; else game.lists.push({ baseId: f.getFormID(), count });
        game.lists = game.lists.filter((x) => x.count > 0);
      },
    },
  };
  if (native) {
    sp.createMadePotion = (formId, name, poison, ids, mags, areas, durs) => {
      game.calls.push(['createMadePotion', formId, name, poison, ids, mags, areas, durs]);
      if (game.forms.has(formId)) return game.forms.get(formId).type === FormType.Potion;
      game.forms.set(formId, { name, type: FormType.Potion, poison,
        effects: ids.map((id, i) => ({ effectId: id, magnitude: mags[i], area: areas[i], duration: durs[i] })) });
      return true;
    };
  }
  const sandbox = { exports: {}, require: (m) => (m === 'skyrimPlatform' ? sp : {}) };
  sandbox.module = { exports: sandbox.exports };
  vm.runInNewContext(js, sandbox);
  game.inv = sandbox.exports;
  game.sp = sp;
  game.pc = pc;
  game.apply = (entries) => game.inv.applyInventory(pc, { entries }, false, true);
  game.count = (id) => game.lists.filter((l) => l.baseId === id).reduce((n, l) => n + l.count, 0);
  return game;
}

const made = () => JSON.parse(JSON.stringify(MADE));

// What the craft message carries.
{
  const g = makeGame();
  const read = g.inv.readMadePotion ? (id) => g.inv.readMadePotion(g.sp.Game.getFormEx(id)) : () => undefined;
  const brewed = read(BREWED);
  yes('readMadePotion reads the effects of a bottle the game brewed', !!brewed &&
    brewed.effects.length === 2 && brewed.effects[0].effectId === RESTORE_HEALTH &&
    brewed.effects[1].effectId === FORTIFY_HEALTH && brewed.effects[1].duration === 60);
  yes('magnitudes go to two decimals, as the server keeps them', !!brewed && brewed.effects[0].magnitude === 23.45);
  yes('with the name the game gave it and that it is not a poison', !!brewed && brewed.name === MADE.name && brewed.poison === false);
  yes('a plugin\'s potion is not a brew', read(MINOR_HEALING) === undefined);
  yes('nothing at all is not a brew', !!g.inv.readMadePotion && g.inv.readMadePotion(null) === undefined);
}

// A new session: the server sends two bottles by their contents.
{
  const g = makeGame();
  g.apply([{ baseId: H, count: 2, made: made() }]);
  const built = g.calls.filter((c) => c[0] === 'createMadePotion');
  yes('the bottle is built once, under the server\'s id', built.length === 1 && built[0][1] === H);
  yes('from its contents: effects, magnitudes, areas, durations, name',
    built.length === 1 && JSON.stringify(built[0].slice(2)) === JSON.stringify([
      MADE.name, false, [RESTORE_HEALTH, FORTIFY_HEALTH], [23.45, 18.2], [0, 0], [0, 60]]));
  yes('and both bottles are in the pack', g.count(H) === 2);
  yes('no "Bad form ID"', !g.console.some((l) => /Bad form ID/.test(l)));

  g.calls.length = 0;
  g.apply([{ baseId: H, count: 2, made: made() }]);
  yes('the next pass finds nothing to do', g.calls.length === 0);

  const read = g.inv.readMadePotion ? g.inv.readMadePotion(g.sp.Game.getFormEx(H)) : 'x';
  yes('a bottle built from the server is not read back as a new brew', read === undefined);

  const inv = g.inv.getInventory(g.pc).entries;
  yes('the pack reads back as the server holds it, contents and all',
    inv.length === 1 && inv[0].baseId === H && inv[0].count === 2 && JSON.stringify(inv[0].made) === JSON.stringify(MADE));
}

// The brewing session: the game's own bottle gives way to the server's.
{
  const g = makeGame();
  g.lists.push({ baseId: BREWED, count: 1 });
  g.apply([{ baseId: H, count: 1, made: made() }]);
  yes('the runtime bottle only that session had is taken out', g.count(BREWED) === 0);
  yes('and the server\'s bottle is in its place', g.count(H) === 1);
}

// A bottle drunk or given away on the server: the game gives it up too.
{
  const g = makeGame();
  g.apply([{ baseId: H, count: 2, made: made() }]);
  g.calls.length = 0;
  g.apply([{ baseId: H, count: 1, made: made() }]);
  const removes = g.calls.filter((c) => c[0] === 'remove');
  yes('a bottle the server no longer holds is taken away as a plain unit',
    removes.length === 1 && removes[0][1] === H && removes[0][2].length === 0 && removes[0][4] === 1);
  yes('and the game holds the one bottle the server does', g.count(H) === 1);
  yes('with nothing left over to take away', !g.console.some((l) => /fewer than the difference/.test(l)));
  g.calls.length = 0;
  g.apply([{ baseId: H, count: 1, made: made() }]);
  yes('and the next pass finds nothing to do', g.calls.length === 0);
}

// The same id with other contents in one session: left out, not thrown.
{
  const g = makeGame();
  g.apply([{ baseId: H, count: 1, made: made() }]);
  const other = made(); other.effects[0].magnitude = 99;
  let threw = false, ok = true;
  try { ok = g.inv.ensureMadePotion(H, other); } catch (e) { threw = true; }
  yes('another brew under an id already built is refused, without throwing', !threw && ok === false);
}

// A Skyrim Platform without createMadePotion.
{
  const g = makeGame({ native: false });
  let threw = false;
  try { g.apply([{ baseId: H, count: 1, made: made() }]); } catch (e) { threw = true; }
  yes('without createMadePotion nothing is built and nothing throws', !threw && g.count(H) === 0);
}

console.log(`${pass} passed, ${fail} failed`);
process.exitCode = fail ? 1 : 0;

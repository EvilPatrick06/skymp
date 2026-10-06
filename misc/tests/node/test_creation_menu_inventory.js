'use strict';
// Run the actual client handlers with delayed native work and paused adds.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const ts = require('../../../skymp5-client/node_modules/typescript');
const source = fs.readFileSync(path.join(__dirname, '../../../skymp5-client/src/services/services/remoteServer.ts'), 'utf8');
const js = ts.transpileModule(source, {compilerOptions: {module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2018}}).outputText;
const invSource = fs.readFileSync(path.join(__dirname, '../../../skymp5-client/src/sync/inventory.ts'), 'utf8');
const invSandbox = {exports: {}, require: () => ({Game: {getFormEx: id => ({getName: () => 'base' + id})}})};
vm.runInNewContext(ts.transpileModule(invSource, {compilerOptions: {module: ts.ModuleKind.CommonJS}}).outputText, invSandbox);
const diff = invSandbox.exports.getDiff;
const dual = {entries: [{baseId: 200, count: 1, worn: true}, {baseId: 200, count: 1, wornLeft: true}]};
assert.equal(diff(dual, dual, true).entries.length, 0, 'the actual diff must settle identical dual-wield inventories');
const distinct = {entries: [{baseId: 200, count: 1, name: 'First', chargePercent: 10, worn: true}, {baseId: 200, count: 1, name: 'Second', chargePercent: 90, wornLeft: true}]};
assert.equal(diff(distinct, {entries: []}, true).entries.length, 2, 'names and charge must survive normalization');

function fixture(save = false, options = {}) {
  let frame = 0, loadedAt = 20, paused = false, opens = 0, removals = 0;
  let initialInventory, firstMoveInventory, baseResets = 0, appearanceApplies = 0, moveCalls = 0;
  const items = new Map(), worn = new Set(), jobs = [], onceHandlers = {update: [], tick: []}, onHandlers = {update: []};
  const hands = new Map(); let equipFailures = options.equipFailures || 0;
  let rejectedAdds = options.rejectedAdds || 0, positionX = options.pos?.[0] ?? 1, cellId = options.sourceCell || 9;
  let partialThrow = !!options.partialThrow;
  const spellWrites = []; let basePrepared = !!options.basePrepared;
  if (options.initialWorn) for (const id of options.initialWorn) worn.add(id);
  const world = {forms: [], playerCharacterFormIdx: -1, playerCharacterRefrId: 0};
  const forms = id => ({id, getFormID: () => id, getName: () => 'base' + id});
  const actor = {
    is3DLoaded: () => frame >= loadedAt, getFormID: () => 0x14,
    getPositionX: () => positionX, getPositionY: () => options.pos?.[1] ?? 2, getPositionZ: () => 3,
    getParentCell: () => forms(cellId), getWorldSpace: () => options.exterior ? forms(9) : null,
    getRace: () => forms(123),
    getItemCount: f => items.get(f.id) || 0, isEquipped: f => worn.has(f.id),
    equipItem: f => {if (equipFailures-- > 0) throw Error('native equip temporarily unavailable');jobs.push({at: frame + 2, run: () => { if (items.has(f.id)) {worn.add(f.id); if (f.id === 200) hands.set(1, f.id);} }});},
    equipItemEx: (f, slot) => jobs.push({at: frame + 2, run: () => {if(items.has(f.id)){hands.set(slot, f.id);worn.add(f.id);}}}),
    getEquippedWeapon: left => hands.has(left ? 2 : 1) ? forms(hands.get(left ? 2 : 1)) : null,
    queueNiNodeUpdate() {}, unequipItem(f) {jobs.push({at: frame + 2, run: () => {worn.delete(f.id);for(const [slot,id] of hands) if(id === f.id) hands.delete(slot);}});},
    unequipItemEx(f, slot) {jobs.push({at: frame + 2, run: () => {if(hands.get(slot) === f.id) hands.delete(slot);}});}, setActorValue() {}
  };
  const inventory = () => options.actualEntries ? ({entries: options.actualEntries}) : ({entries: [...items].flatMap(([baseId, count]) => {
    if (baseId === 200) {
      const entries = [];
      if (hands.get(1) === baseId) {entries.push({baseId, count: 1, worn: true});count--;}
      if (hands.get(2) === baseId) {entries.push({baseId, count: 1, wornLeft: true});count--;}
      if (count) entries.push({baseId, count});
      return entries;
    }
    return [{baseId, count, ...(worn.has(baseId) ? {worn: true} : {})}];
  })});
  const applyInventory = (_actor, expected) => {
    if (paused || rejectedAdds-- > 0) return; // Native rejects before enqueueing, not at execution.
    for (const e of diff(expected, inventory(), true).entries) {
      jobs.push({at: frame + (options.nativeDelay || 2), run: () => {
        const count = (items.get(e.baseId) || 0) + e.count; if (count > 0) items.set(e.baseId, count); else {items.delete(e.baseId);worn.delete(e.baseId);}
      }});
      if (partialThrow) {partialThrow = false; throw Error('later native entry failed after first addition queued');}
    }
  };
  const sp = {
    getExteriorCellCoordinates: id => options.coordinates === null ? undefined : options.coordinates?.[id] || (id === 9 ? [0, 0] : [1, 0]),
    getInventoryQueueFence() {
      let finished = false;
      const at = Math.max(frame + 1, ...jobs.map(job => job.at + 1));
      jobs.push({at, run: () => {finished = true;}});
      return () => finished;
    },
    storage: {worldModel: world}, Actor: {from: x => x}, Armor: {from: x => x}, Ammo: {from: () => null},
    Cell: {from: x => x}, WorldSpace: {from: () => null}, Weapon: {from: f => f?.id === 200 ? f : null},
    Game: {getPlayer: () => actor, getFormEx: forms, getModCount: () => 0, getLightModCount: () => 0, showRaceMenu: () => {paused = true; opens++;}},
    Ui: {isMenuOpen: name => name === 'RaceSex Menu' && paused},
    TESModPlatform: {moveRefrToPosition() {if (!firstMoveInventory) firstMoveInventory = inventory(); if (options.moveCell) { if (options.transitionDelay) { if (++moveCalls === 1) jobs.push({at: frame + options.transitionDelay, run: () => {cellId = options.moveCell;}}); } else if (options.moveDelay && ++moveCalls === 1) loadedAt = frame + options.moveDelay; else cellId = options.moveCell; }}},
    Utility: {wait: seconds => new Promise(resolve => jobs.push({at: frame + Math.max(1, Math.ceil(seconds * 10)), run: resolve}))},
    once: (event, fn) => onceHandlers[event].push(fn), on: (event, fn) => { (onHandlers[event] ||= []).push(fn); },
    printConsole() {}
  };
  vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../../../skyrim-platform/src/platform_se/skyrim_platform/assets/storageProxy.js'), 'utf8'))(sp);
  sp.storage.worldModel = world;
  const listeners = new Map();
  const controller = {emitter: {on: (name, fn) => listeners.set(name, fn), emit() {}}, lookupListener: () => ({
    getTime: () => ({newGameHourValue: 12}), loadGame: (...args) => {initialInventory = args[6]; loadedAt = frame + 20;
      if (options.nativePreload && initialInventory) for (const e of initialInventory.entries) {items.set(e.baseId, (items.get(e.baseId) || 0) + e.count); if (e.worn) worn.add(e.baseId);}}
  })};
  class IdManager { allocateIdFor(id) {return id;} getId(id) {return id;} }
  sp.storage.idManager = new IdManager();
  const sandbox = {exports: {}, Promise, Map, Set, Date: {now: () => frame * 100}, __thornswoodInWorld: !save,
    require: name => {
      if (name === 'skyrimPlatform') return sp;
      if (name.endsWith('/clientListener')) return {ClientListener: class {}};
      if (name.endsWith('/idManager')) return {IdManager};
      if (name.endsWith('/inventory')) return {acceptPreloadedInventoryBase() {basePrepared = true;}, resetInventoryBase() {baseResets++;if(basePrepared) return false; basePrepared = true; jobs.push({at: frame + (options.nativeDelay || 2), run: () => {items.clear();worn.clear();hands.clear();}});return true;}, applyInventory, getInventory: inventory, getDiff: diff, inventoryEntriesEqual: invSandbox.exports.inventoryEntriesEqual};
      if (name.endsWith('/equipment')) return {isBadMenuShown: () => false, syncSpellEquipment() {}, SpellType: {}, applyEquipment: () => {removals++; items.clear(); worn.clear();}};
      if (name.endsWith('/appearance')) return {applyAppearanceToPlayer() {appearanceApplies++;worn.clear();}};
      if (name.endsWith('/spell')) return {removeAllSpells() {spellWrites.push('remove');}, learnSpells(_actor, ids) {spellWrites.push(...ids);}};
      if (name.endsWith('/logging')) return {logTrace(...args) {if (process.env.CREATION_DEBUG) console.log(...args.slice(1));}, logError(...args) {if (process.env.CREATION_DEBUG) console.log(...args.slice(1));}};
      if (name.endsWith('/messages')) return {MsgType: {SetRaceMenuOpen: 1, SetInventory: 2}};
      return new Proxy({}, {get: () => class {}});
    }};
  vm.runInNewContext(js, sandbox);
  const remote = new sandbox.exports.RemoteServer(sp, controller);
  const emit = (name, message) => listeners.get(name)({message});
  // Thornswood #1560: the server writes a new character's equipment record
  // when it makes the character, so a creation packet carries the worn clothes
  // in the record as well as in the pack. A returning character's record is
  // whatever its game last reported.
  const packEntries = () => options.entries || [{baseId: 100, count: 1, worn: true}, {baseId: 101, count: 1, worn: true}];
  const wornOf = entries => entries.filter(e => e.worn || e.wornLeft).map(e => ({...e}));
  const spawn = (initialMenu = false, idx = 0) => emit('createActorMessage', {idx, refrId: 0xff000015 + idx, isMe: true,
    transform: {worldOrCell: 9, pos: options.pos || [1, 2, 3], rot: [0, 0, 0]}, customPropsJsonDumps: [],
    props: {learnedSpells: options.spells, isRaceMenuOpen: initialMenu, inventory: {entries: packEntries()}},
    appearance: options.appearance,
    equipment: {inv: {entries: options.equipment || (options.returning ? [] : wornOf(packEntries()))}, numChanges: 0}});
  const runEvent = event => {const callbacks = onceHandlers[event].splice(0); for (const fn of [...(onHandlers[event] || []), ...callbacks]) fn();};
  const advance = async (n, updates = true) => {
    for (let i = 0; i < n; i++) {
      frame++; runEvent('tick'); if (updates) runEvent('update');
      const due = jobs.filter(j => j.at <= frame); for (const j of due) jobs.splice(jobs.indexOf(j), 1);
      for (const j of due) j.run();
      for (let k = 0; k < 8; k++) await Promise.resolve();
    }
  };
  return {spawn, emit, advance, move: x => {positionX = x;}, changeCell: id => {cellId = id;}, recreate: () => new sandbox.exports.RemoteServer(sp, controller), get diagnostic() {return typeof sp.storage.ownerOutfitDiagnostic === 'string' ? JSON.parse(sp.storage.ownerOutfitDiagnostic) : undefined;}, get baseResets() {return baseResets;}, get appearanceApplies() {return appearanceApplies;}, get initialInventory() {return initialInventory;}, get firstMoveInventory() {return firstMoveInventory;}, get opens() {return opens;}, items, worn, hands, spellWrites, get settling() {return sp.storage.ownerInventorySettling === true;}, get removals() {return removals;}};
}

(async () => {
  const rejected = fixture(true, {rejectedAdds: 1}); rejected.spawn(true); await rejected.advance(300);
  assert.equal(rejected.opens, 1, 'a silently rejected native add must retry after its queue completes');
  assert.equal(rejected.items.get(100), 1); assert.equal(rejected.items.get(101), 1);
  const partial = fixture(true, {partialThrow: true, nativeDelay: 20}); partial.spawn(true); await partial.advance(400);
  assert.equal(partial.opens, 1); assert.equal(partial.items.get(100), 1); assert.equal(partial.items.get(101), 1);
  const replacementOptions = {basePrepared: true, nativeDelay: 20};
  const replacement = fixture(true, replacementOptions); replacement.spawn(true); await replacement.advance(43);
  replacementOptions.entries = [{baseId: 102, count: 1, worn: true}]; replacement.items.set(102, 1); replacement.worn.add(102);
  replacement.spawn(true, 1); await replacement.advance(400);
  assert.equal(replacement.opens, 1, 'only the current owner can open creation after old queued work drains');
  assert.equal(replacement.items.size, 1); assert.equal(replacement.items.get(102), 1);
  const moving = fixture(true); moving.spawn(true); await moving.advance(30); moving.move(400); await moving.advance(300);
  assert.equal(moving.opens, 1, 'moving inside the spawn cell after arrival cannot strand creation');
  const wrongCell = fixture(true); wrongCell.spawn(true); await wrongCell.advance(30); wrongCell.changeCell(10); await wrongCell.advance(1300);
  assert.equal(wrongCell.opens, 0, 'a different cell is still not the authoritative spawn');
  const borderMove = fixture(false, {exterior: true, sourceCell: 10, moveCell: 9});
  borderMove.spawn(true); await borderMove.advance(1500);
  assert.equal(borderMove.opens, 1, 'preparing in a nearby exterior cell cannot latch that source cell across the move');
  assert.equal(borderMove.settling, false);
  const delayedBorderMove = fixture(false, {exterior: true, sourceCell: 10, moveCell: 9, moveDelay: 15});
  delayedBorderMove.spawn(true); await delayedBorderMove.advance(1700);
  assert.equal(delayedBorderMove.opens, 1, 'an asynchronous exterior transition must refresh the source latch after the actual move');
  const pendingBorderMove = fixture(false, {exterior: true, sourceCell: 10, moveCell: 9, transitionDelay: 100});
  pendingBorderMove.spawn(true); await pendingBorderMove.advance(100);
  assert.equal(pendingBorderMove.opens, 0, 'the still-loaded source exterior cell cannot open creation while movement is pending');
  await pendingBorderMove.advance(300); assert.equal(pendingBorderMove.opens, 1, 'the destination exterior cell releases creation');
  const negativeGrid = fixture(true, {exterior: true, pos: [-1, -4097, 3], coordinates: {9: [-1, -2]}});
  negativeGrid.spawn(true); await negativeGrid.advance(3, false); await negativeGrid.advance(300);
  assert.equal(negativeGrid.opens, 1, 'negative world positions use floor division for exterior cells');
  const absentGrid = fixture(true, {exterior: true, coordinates: null});
  absentGrid.spawn(true); await absentGrid.advance(3, false); await absentGrid.advance(1500);
  assert.equal(absentGrid.opens, 0, 'missing engine cell coordinates cannot fail open');
  const beforeMove = fixture(); beforeMove.spawn(true); await beforeMove.advance(300);
  assert.equal(beforeMove.firstMoveInventory.entries.filter(e => e.worn).length, 2, 'move path must equip both clothes before entering the room');
  const populated = fixture(true, {nativePreload: true}); populated.spawn(true); await populated.advance(3, false); await populated.advance(300);
  assert.equal(populated.opens, 1); assert.equal(populated.baseResets, 0, 'both reset paths must respect the native-preloaded base');
  assert.equal(populated.items.get(100), 1); assert.equal(populated.items.get(101), 1); assert.equal(populated.worn.size, 2);
  for (const synthetic of [false, true]) {
    const face = fixture(synthetic, {appearance: {name: 'Adventurer'}, nativePreload: true}); face.spawn(true); if (synthetic) await face.advance(3, false); await face.advance(300);
    assert.equal(face.opens, 1); assert.equal(face.appearanceApplies, 1, 'full appearance must be applied once on either route');
    assert.equal(face.worn.size, 2, 'appearance-induced unequipping must settle before creation');
    if (!synthetic) assert.equal(face.firstMoveInventory.entries.filter(e => e.worn).length, 2);
  }
  const preload = fixture(true); preload.spawn(true); await preload.advance(3, false);
  assert.ok(preload.initialInventory, 'initial load must receive inventory before room loading');
  assert.deepEqual(JSON.parse(JSON.stringify(preload.initialInventory)), {entries: [{baseId: 100, count: 1, worn: true}, {baseId: 101, count: 1, worn: true}]}, 'creation inventory must reach loadGame before the room is loaded');
  const exterior = fixture(true, {exterior: true}); exterior.spawn(true); await exterior.advance(30);
  exterior.move(4000); exterior.changeCell(10); await exterior.advance(300);
  assert.equal(exterior.opens, 0, 'crossing an exterior cell cannot open creation in the same worldspace');
  for (const initialMenu of [false, true]) {
    const f = fixture(); f.spawn(initialMenu); if (!initialMenu) f.emit('setRaceMenuOpenMessage', {open: true});
    await f.advance(10);
    assert.equal(f.opens, 0, 'the face menu must wait for loading and native inventory');
    await f.advance(90);
    assert.equal(f.opens, 1); assert.equal(f.worn.size, 2, 'both original pieces are equipped before pausing');
    assert.equal(f.diagnostic.stage, 'before-face-menu');
    assert.equal(f.diagnostic.actual.filter(entry => entry.worn).length, 2, 'capture actual engine worn state for connected diagnostics');
    assert.equal(f.items.get(100), 1, 'queued work must not duplicate the shirt');
    assert.equal(f.items.get(101), 1); assert.equal(f.removals, 0, 'dressing from the record must not remove the starter inventory');
  }
  const save = fixture(true); save.spawn(); save.emit('setRaceMenuOpenMessage', {open: true});
  await save.advance(2, false); await save.advance(100);
  assert.equal(save.opens, 1); assert.equal(save.worn.size, 2, 'the synthetic-save branch must settle the outfit too');
  for (const event of ['connectionAccepted', 'connectionDisconnect', 'connectionFailed', 'connectionDenied']) {
    const stale = fixture(); stale.spawn(); stale.emit('setRaceMenuOpenMessage', {open: true});
    await stale.advance(3); stale.emit(event); await stale.advance(100);
    assert.equal(stale.opens, 0, 'a disconnected spawn cannot open the menu later: ' + event);
  }
  const cancelled = fixture(); cancelled.spawn(); cancelled.emit('setRaceMenuOpenMessage', {open: true});
  cancelled.emit('setRaceMenuOpenMessage', {open: false}); await cancelled.advance(100);
  assert.equal(cancelled.opens, 0, 'a cancelled menu request cannot reopen');
  assert.equal(cancelled.settling, false, 'cancelled creation releases background syncing after settlement');
  const lateCancel = fixture(); lateCancel.spawn(); await lateCancel.advance(100);
  lateCancel.emit('setRaceMenuOpenMessage', {open: true}); await lateCancel.advance(1);
  lateCancel.emit('setRaceMenuOpenMessage', {open: false}); await lateCancel.advance(100);
  assert.equal(lateCancel.opens, 0); assert.equal(lateCancel.settling, false, 'a cancelled settled menu wait cannot leak its guard');
  const early = fixture(); early.emit('setRaceMenuOpenMessage', {open: true}); await early.advance(10);
  assert.equal(early.opens, 0, 'an out-of-order request waits for its actor'); early.spawn(); await early.advance(100);
  assert.equal(early.opens, 1); assert.equal(early.worn.size, 2);
  const changed = fixture(); changed.spawn(true);
  for (let frame = 0; frame < 100 && changed.diagnostic?.stage !== 'outfit-ready'; ++frame) await changed.advance(1);
  assert.equal(changed.opens, 0, 'the revision change is exercised before the menu opens');
  changed.emit('setInventoryMessage', {inventory: {entries: [{baseId: 102, count: 1, worn: true}]}});
  await changed.advance(100); assert.equal(changed.opens, 1); assert(changed.worn.has(102)); assert.equal(changed.items.size, 1);
  const transient = fixture(false, {equipFailures: 1}); transient.spawn(true); await transient.advance(100);
  assert.equal(transient.opens, 1); assert.equal(transient.worn.size, 2, 'a transient native exception must recover');
  for (const entries of [[{baseId: 200, count: 1, wornLeft: true}], dual.entries]) {
    const weapons = fixture(false, {entries}); weapons.items.set(200, entries.length); weapons.spawn(true); await weapons.advance(100);
    assert.equal(weapons.opens, 1); assert.equal(weapons.hands.get(2), 200, 'restore the actual left hand');
    if (entries.length === 2) assert.equal(weapons.hands.get(1), 200, 'restore both hands for the same base');
  }
  const oldSpells = fixture(false, {spells: [7]}); oldSpells.spawn(); await oldSpells.advance(3);
  oldSpells.emit('connectionDisconnect'); await oldSpells.advance(100);
  assert.equal(oldSpells.spellWrites.length, 0, 'cancelled delayed work must not replace another spellbook');
  const newSpells = fixture(false, {spells: [7]}); newSpells.spawn(); await newSpells.advance(100);
  assert.deepEqual(newSpells.spellWrites, ['remove', 7], 'current owner spells still load');
  newSpells.emit('setInventoryMessage', {inventory: {entries: [{baseId: 102, count: 1}]}});
  await newSpells.advance(100);
  assert.equal(newSpells.items.get(102), 1, 'background inventory resumes with the real storage proxy after owner settlement');
  const plainWorn = [{baseId: 100, count: 1, worn: true}, {baseId: 100, count: 1, health: 2}];
  const wrongVariant = fixture(false, {entries: [{baseId: 100, count: 1}, {baseId: 100, count: 1, health: 2, worn: true}], actualEntries: plainWorn, initialWorn: [100], basePrepared: true});
  wrongVariant.items.set(100, 2); wrongVariant.spawn(true); await wrongVariant.advance(1300);
  assert.equal(wrongVariant.opens, 0, 'an ambiguous wrong variant must never be accepted as ready');
  assert.deepEqual(plainWorn, [{baseId: 100, count: 1, worn: true}, {baseId: 100, count: 1, health: 2}], 'variant metadata remains intact');
  const queued = fixture(false, {nativeDelay: 20}); queued.spawn(true); await queued.advance(300);
  assert.equal(queued.opens, 1); assert.equal(queued.items.get(100), 1); assert.equal(queued.items.get(101), 1);
  assert.equal(queued.worn.size, 2, 'no pending batch may remove the outfit after opening');
  const retained = fixture(false, {nativeDelay: 20}); retained.items.set(100, 1); retained.spawn(true); await retained.advance(300);
  assert.equal(retained.opens, 1); assert.equal(retained.items.get(100), 1); assert.equal(retained.items.get(101), 1); assert.equal(retained.worn.size, 2, 'base reset must settle before differences are queued');
  const saved = fixture(false, {returning: true, entries: [{baseId: 200, count: 1}], equipment: [{baseId: 200, count: 1, wornLeft: true}]});
  saved.spawn(); saved.emit('setRaceMenuOpenMessage', {open: true}); await saved.advance(100);
  assert.equal(saved.opens, 1); assert.equal(saved.hands.get(2), 200, 'saved equipment overlays ownership inventory');
  const loadingEcho = fixture(false, {returning: true, entries: [{baseId: 100, count: 1}], equipment: [{baseId: 100, count: 1, worn: true}]});
  loadingEcho.spawn(); await loadingEcho.advance(10);
  loadingEcho.emit('updateEquipmentMessage', {idx: 0, data: {inv: {entries: [{baseId: 100, count: 1}]}, numChanges: 1}});
  await loadingEcho.advance(100);
  assert(loadingEcho.worn.has(100), 'a transient loading echo must not replace the saved outfit being restored');
  const unequipped = fixture(false, {returning: true, basePrepared: true, initialWorn: [100], entries: [{baseId: 100, count: 1, worn: true}]});
  unequipped.items.set(100, 1);
  unequipped.spawn(); unequipped.emit('setRaceMenuOpenMessage', {open: true}); await unequipped.advance(100);
  assert.equal(unequipped.opens, 1); assert.equal(unequipped.worn.size, 0, 'empty saved equipment respects deliberate unequipping');
  const swapped = fixture(false, {returning: true, basePrepared: true, entries: [{baseId: 200, count: 1}], equipment: [{baseId: 200, count: 1, worn: true}]});
  swapped.items.set(200, 1); swapped.hands.set(2, 200); swapped.spawn(); swapped.emit('setRaceMenuOpenMessage', {open: true}); await swapped.advance(100);
  assert.equal(swapped.opens, 1); assert.equal(swapped.hands.get(1), 200); assert.equal(swapped.hands.has(2), false, 'saved single-hand gear clears its stale other hand');
  const lostState = fixture(); lostState.spawn(); await lostState.advance(100); lostState.worn.clear(); lostState.recreate();
  lostState.emit('setRaceMenuOpenMessage', {open: true}); await lostState.advance(100);
  assert.equal(lostState.opens, 0, 'retained model without readiness evidence must not fail open');
  // The record decides what is worn, at creation too: a pack that marks the
  // clothes worn beside a record that does not is dressed from the record.
  const recordDecides = fixture(false, {equipment: []}); recordDecides.spawn(true); await recordDecides.advance(300);
  assert.equal(recordDecides.opens, 1); assert.equal(recordDecides.worn.size, 0, 'the equipment record, not the pack, says what is worn');
  assert.equal(recordDecides.items.get(100), 1, 'and the pack still holds the clothes');
  console.log('PASS real creation handlers: delayed inventory, asynchronous equip, both spawn paths, cancellation and early request');
})().catch(e => { console.error(e); process.exitCode = 1; });

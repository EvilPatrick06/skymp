// Real-addon check. Pass the native module and a settings file whose loadOrder
// points at installed game data. No game, saved world or connected users needed.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const addon = require(require('node:path').resolve(process.argv[2]));
const installed = JSON.parse(fs.readFileSync(process.argv[3], 'utf8'));
const mode = process.argv[4] || 'all';
assert(['all', 'disabled', 'interior', 'gates-off'].includes(mode));
const interiorPolicy = Object.fromEntries(installed.loadOrder.map(file =>
  [require('node:path').basename(file), {
    spawnInInterior: true, spawnInExterior: false
  }]));
const server = new addon.ScampServer(JSON.stringify({
  port: 0, maxPlayers: 2, offlineMode: true, name: 'npc-preload-test',
  dataDir: require('node:path').dirname(installed.loadOrder[0]),
  loadOrder: installed.loadOrder, npcEnabled: mode !== 'disabled',
  npcAllowEssential: mode !== 'gates-off',
  npcAllowCrimeFaction: mode !== 'gates-off',
  npcSettings: mode === 'interior' ? interiorPolicy : {}, logLevel: 'error'
}));
assert.equal(typeof server.loadNpcBatch, 'function',
  'the native server must load placed NPCs without a connected human');
for (const args of [[-1, 1], [0.5, 1], [NaN, 1], [Infinity, 1],
  [4294967296, 1], ['0', 1], [0, 0], [0, 129], [0, -1],
  [0, 1.5], [0, NaN], [0, Infinity], [0, '1'], [0]]) {
  assert.throws(() => server.loadNpcBatch(...args),
    `invalid cursor/limit must fail: ${String(args)}`);
}
const started = performance.now();
const rssBefore = process.memoryUsage().rss;
const first = server.loadNpcBatch(0, 1);
const discoveryMs = performance.now() - started;
assert(first.total > 0, 'the real load order contains placed NPC candidates');
assert.equal(first.nextCursor, 1);
assert(first.actorIds.length <= 1);
assert.throws(() => server.loadNpcBatch(first.total + 1, 1));
assert.deepEqual(server.loadNpcBatch(first.total, 1), {
  nextCursor: first.total, total: first.total, actorIds: []
});
let cursor = 0;
const seen = new Set();
let total = first.total;
let worstBatchMs = 0;
while (cursor < total) {
  const before = performance.now();
  const batch = server.loadNpcBatch(cursor, 32);
  worstBatchMs = Math.max(worstBatchMs, performance.now() - before);
  assert.equal(batch.total, total);
  assert.equal(batch.nextCursor, Math.min(cursor + 32, total));
  assert(batch.actorIds.length <= 32);
  for (const id of batch.actorIds) {
    assert(!seen.has(id), `duplicate placed identity: ${id.toString(16)}`);
    seen.add(id);
  }
  cursor = batch.nextCursor;
}
const expected = {
  all: [true, true, true, true], disabled: [false, false, false, false],
  // The winning Nazeem placement starts inside WhiterunDrunkenHuntsman,
  // verified from the real load order, rather than in the outdoor market.
  interior: [false, true, true, true], 'gates-off': [true, false, false, false]
}[mode];
if (mode === 'disabled') assert.equal(seen.size, 0);
for (const [i, id] of [0x10ebaf, 0x1a672, 0x1a66e, 0x1a6a4].entries()) {
  assert.equal(seen.has(id), expected[i],
    `${mode} respects inclusion for Whiterun NPC ${id.toString(16)}`);
  if (!expected[i]) continue;
  const idx = server.get(id, 'idx');
  const pos = server.get(id, 'pos');
  for (let retry = 0; retry < total; retry += 128) {
    server.loadNpcBatch(retry, 128);
  }
  assert.equal(server.get(id, 'idx'), idx, 'reloading keeps the same identity');
  assert.deepEqual(server.get(id, 'pos'), pos, 'reloading keeps its transform');
}
assert(!seen.has(0x14), 'the vanilla human reference is not an NPC');
console.log(JSON.stringify({ result: 'passed', mode, candidates: total,
  loaded: seen.size, discoveryMs, worstBatchMs,
  rssBeforeMiB: rssBefore / 1048576,
  rssAfterMiB: process.memoryUsage().rss / 1048576 }));
process.exit(0);

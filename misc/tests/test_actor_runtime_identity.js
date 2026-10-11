const assert = require('node:assert/strict');

const main = () => {
  const id = mp.createActor(0, [0, 0, 0], 0, 0x3c, 42);
  const name = 'runtimeIdentityQuest';
  mp.makeProperty(name, {isVisibleByOwner:false, isVisibleByNeighbors:false, updateOwner:'', updateNeighbor:''});
  const before = {entries:[{baseId:15, count:40}]};
  const after = {entries:[{baseId:15, count:50}]};
  mp.set(id, 'inventory', before);
  const initial = mp.getActorRuntimeIdentity(id);
  const livingBody = mp.getNpcAIState(id).runtimeLifeIdentity;
  assert.match(livingBody, /^[1-9][0-9]{0,19}$/);
  assert.match(initial, /^[1-9][0-9]{0,19}$/);
  assert.equal(mp.getActorRuntimeIdentity(id), initial);
  mp.set(id, 'isDead', true);
  assert.notEqual(mp.getActorRuntimeIdentity(id), initial);
  assert.equal(mp.getNpcAIState(id).runtimeLifeIdentity,livingBody);
  mp.set(id, 'isDead', false);
  const current = mp.getActorRuntimeIdentity(id);
  assert.notEqual(current, initial);
  assert.notEqual(mp.getNpcAIState(id).runtimeLifeIdentity,livingBody);
  const change = {name, expected:null, replacement:{done:true}, expectedLifeGeneration:0,
    expectedRuntimeIdentity:initial};
  assert.equal(mp.compareAndSetInventoryAndProperty(id, before, 'null', after, 1, 42, change), false);
  assert.deepEqual(mp.get(id, 'inventory'), before);
  assert.equal(mp.getInventoryReceipt(id), 'null');
  for (const invalid of ['', '0', '01', '-1', '1.5', ' 1', '18446744073709551616', 1]) {
    assert.throws(() => mp.compareAndSetInventoryAndProperty(id, before, 'null', after, 1, 42,
      {...change, expectedRuntimeIdentity:invalid}));
    assert.deepEqual(mp.get(id, 'inventory'), before);
    assert.equal(mp.getInventoryReceipt(id), 'null');
  }
  for (const boundary of ['expected','replacement','inventory']) {
    for (const mutation of ['replace','delete']) {
      const victim = mp.createActor(0, [0,0,0], 0, 0x3c, 42);
      mp.set(victim, 'inventory', before);
      const fenced = {name, expected:null, replacement:{done:true}, expectedLifeGeneration:0,
        expectedRuntimeIdentity:mp.getActorRuntimeIdentity(victim)};
      const callback = {toJSON() {
        mp.set(victim, 'isDead', true); mp.set(victim, 'isDead', false);
        if (mutation === 'replace') fenced.expectedRuntimeIdentity=mp.getActorRuntimeIdentity(victim);
        else delete fenced.expectedRuntimeIdentity;
        return boundary === 'inventory' ? before : boundary === 'expected' ? null : {done:true};
      }};
      if (boundary !== 'inventory') fenced[boundary]=callback;
      assert.equal(mp.compareAndSetInventoryAndProperty(victim,
        boundary === 'inventory' ? callback : before, 'null', after, 1, 42, fenced), false,
        boundary+' serialization cannot '+mutation+' the entry ownership fence');
      assert.deepEqual(mp.get(victim, 'inventory'), before);
      assert.equal(mp.getInventoryReceipt(victim), 'null');
      assert.equal(mp.get(victim, name) == null, true);
    }
  }
  change.expectedRuntimeIdentity = current;
  change.replacement = {toJSON() {
    mp.destroyActor(id);
    assert.equal(mp.createActor(id, [0, 0, 0], 0, 0x3c, 42), id);
    mp.set(id, 'inventory', before);
    return {done:true};
  }};
  assert.equal(mp.compareAndSetInventoryAndProperty(id, before, 'null', after, 1, 42, change), false);
  assert.notEqual(mp.getActorRuntimeIdentity(id), current);
  assert.deepEqual(mp.get(id, 'inventory'), before);
  assert.equal(mp.getInventoryReceipt(id), 'null');
  assert.equal(mp.get(id, name) == null, true);
  change.expectedRuntimeIdentity = mp.getActorRuntimeIdentity(id);
  change.replacement = {done:true};
  assert.equal(mp.compareAndSetInventoryAndProperty(id, before, 'null', after, 1, 42, change), true);
  assert.deepEqual(mp.get(id, 'inventory'), after);
  assert.deepEqual(mp.get(id, name), {done:true});
};

try { main(); console.log('Test passed!'); process.exit(0); }
catch (error) { console.error(error); console.log('Test failed!'); process.exit(1); }

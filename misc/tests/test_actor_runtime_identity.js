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
  for(const trap of ['has','get'])for(const mutation of ['replace','delete']) {
    const victim=mp.createActor(0,[0,0,0],0,0x3c,42);mp.set(victim,'inventory',before);
    const fenced={name,expected:null,replacement:{done:true},expectedLifeGeneration:0,
      expectedRuntimeIdentity:mp.getActorRuntimeIdentity(victim)};
    let changed=false;
    function rotate(target,key) {
      if(key!=='expectedRuntimeIdentity' || changed)return;
      changed=true;mp.set(victim,'isDead',true);mp.set(victim,'isDead',false);
      if(mutation==='replace')target[key]=mp.getActorRuntimeIdentity(victim);else delete target[key];
    }
    const input=new Proxy(fenced,{
      has(target,key){if(trap==='has')rotate(target,key);return Reflect.has(target,key);},
      get(target,key,receiver){if(trap==='get')rotate(target,key);return Reflect.get(target,key,receiver);}
    });
    // A get trap deleting its fence produces invalid input, also a refusal.
    let committed=false;
    try{committed=mp.compareAndSetInventoryAndProperty(victim,before,'null',after,1,42,input);}
    catch(error){assert.equal(trap==='get' && mutation==='delete',true);}
    assert.equal(changed,true);
    assert.equal(committed,false,trap+' cannot '+mutation+' the actor entry session');
    assert.deepEqual(mp.get(victim,'inventory'),before);
    assert.equal(mp.getInventoryReceipt(victim),'null');
    assert.equal(mp.get(victim,name)==null,true);
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
  const receipt=mp.getInventoryReceipt(id),inventory=mp.get(id,'inventory');
  let saved;
  try{saved=mp.getSavedInventoryReceipt(id);}catch(error){assert.match(error.message,/acknowledgement is unavailable/);}
  assert.notEqual(saved,receipt,'inventory save has not been acknowledged');
  let state={done:true};
  for(let kills=1;kills<=3;kills++) {
    const next={kills};
    assert.equal(mp.compareAndSetActorProperty(id,42,{name,expected:state,replacement:next,
      expectedLifeGeneration:0,expectedRuntimeIdentity:mp.getActorRuntimeIdentity(id)}),true);
    assert.deepEqual(mp.get(id,name),next);state=next;
    assert.equal(mp.getInventoryReceipt(id),receipt);assert.deepEqual(mp.get(id,'inventory'),inventory);
  }
  assert.equal(mp.compareAndSetActorProperty(id,42,{name,expected:{kills:2},replacement:{kills:4},
    expectedLifeGeneration:0,expectedRuntimeIdentity:mp.getActorRuntimeIdentity(id)}),false);
  for(const boundary of ['expected','replacement']) {
    const token=mp.getActorRuntimeIdentity(id);
    const input={name,expected:state,replacement:{kills:4},expectedLifeGeneration:0,expectedRuntimeIdentity:token};
    input[boundary]={toJSON(){mp.set(id,'isDead',true);mp.set(id,'isDead',false);
      input.expectedRuntimeIdentity=mp.getActorRuntimeIdentity(id);return boundary==='expected'?state:{kills:4};}};
    assert.equal(mp.compareAndSetActorProperty(id,42,input),false,'metadata serialization cannot replace the entry session');
    assert.deepEqual(mp.get(id,name),state);assert.equal(mp.getInventoryReceipt(id),receipt);
  }
  const metadata = () => ({name,expected:state,replacement:{kills:4},expectedLifeGeneration:0,
    expectedRuntimeIdentity:mp.getActorRuntimeIdentity(id)});
  for(const invalid of ['', '0', '01', '1\n', '18446744073709551616', undefined, 1]) {
    assert.throws(()=>mp.compareAndSetActorProperty(id,42,{...metadata(),expectedRuntimeIdentity:invalid}));
  }
  assert.equal(mp.compareAndSetActorProperty(id,43,metadata()),false);
  assert.equal(mp.compareAndSetActorProperty(id,42,{...metadata(),expectedLifeGeneration:1}),false);
  for(const reserved of ['_skympNpcLifeGeneration','_skympServerControlled','_skympServerRespawnAt']) {
    mp.makeProperty(reserved,{isVisibleByOwner:false,isVisibleByNeighbors:false,updateOwner:'',updateNeighbor:''});
    assert.throws(()=>mp.compareAndSetActorProperty(id,42,{...metadata(),name:reserved}));
  }
  mp.makeProperty('visibleMetadata',{isVisibleByOwner:true,isVisibleByNeighbors:false,updateOwner:'',updateNeighbor:''});
  for(const invalidName of ['visibleMetadata','unregisteredMetadata','inventory'])
    assert.throws(()=>mp.compareAndSetActorProperty(id,42,{...metadata(),name:invalidName}));
  for(const mutation of ['replace','delete']) {
    let changed=false;
    const input=new Proxy(metadata(),{get(target,key,receiver) {
      if(key==='expectedRuntimeIdentity' && !changed) {
        changed=true;mp.set(id,'isDead',true);mp.set(id,'isDead',false);
        if(mutation==='replace')target[key]=mp.getActorRuntimeIdentity(id);else delete target[key];
      }
      return Reflect.get(target,key,receiver);
    }});
    let committed=false;
    try{committed=mp.compareAndSetActorProperty(id,42,input);}catch(error){assert.equal(mutation,'delete');}
    assert.equal(changed,true);assert.equal(committed,false);
  }
  const replacement=metadata();
  replacement.replacement={toJSON(){mp.destroyActor(id);mp.createActor(id,[0,0,0],0,0x3c,42);
    mp.set(id,'inventory',inventory);mp.set(id,name,state);
    replacement.expectedRuntimeIdentity=mp.getActorRuntimeIdentity(id);return {kills:4};}};
  assert.equal(mp.compareAndSetActorProperty(id,42,replacement),false);
  assert.deepEqual(mp.get(id,name),state);assert.deepEqual(mp.get(id,'inventory'),inventory);
};

try { main(); console.log('Test passed!'); process.exit(0); }
catch (error) { console.error(error); console.log('Test failed!'); process.exit(1); }

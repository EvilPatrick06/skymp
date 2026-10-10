'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const addon=require(path.resolve(process.argv[2]));
for(const method of ['compareAndSetInventory','compareAndSetInventoryAndProperty','getInventoryReceipt','getSavedInventoryReceipt'])
  assert.equal(typeof addon.ScampServer.prototype[method],'function',method+' must be exposed to the server gamemode');
const installed=JSON.parse(fs.readFileSync(process.argv[3],'utf8'));
assert.ok(Array.isArray(installed.loadOrder) && installed.loadOrder.length > 0,'Use the verified installed plugin order');
const fixture=fs.mkdtempSync(path.join(require('node:os').tmpdir(),'tw-receipt-native-'));
process.chdir(fixture);
const dataDir=path.join(fixture,'data');fs.mkdirSync(path.join(dataDir,'scripts'),{recursive:true});
const server=new addon.ScampServer(JSON.stringify({port:0,maxPlayers:2,offlineMode:true,
  name:'inventory-receipt-check',dataDir,loadOrder:installed.loadOrder,
  databaseDriver:'file',databaseName:path.join(fixture,'world'),logLevel:'error'}));
server._setSelf(server);
const human=server.createActor(0x7,[1,1,1],0,0x3c,42);
const before={entries:[{baseId:15,count:40},{baseId:0x12eb7,count:2,name:'Retained goods',health:0.75}]};
server.set(human,'inventory',before);
assert.equal(server.getNpcAIState(human).profileId,42,'Merchant authentication must receive the real human profile');
assert.equal(server.getInventoryReceipt(human),'null');
assert.throws(()=>server.getSavedInventoryReceipt(human),/unavailable/);
server.attachSaveStorage();
assert.equal(server.getSavedInventoryReceipt(human),'null');
const after={entries:[{baseId:15,count:30},before.entries[1]]};
const compare=(id,expected,receipt,replacement,sequence)=>
  server.compareAndSetInventory(id,expected,receipt,replacement,sequence,42);
assert.throws(()=>server.compareAndSetInventory(human,before,'null',after,1,43),/profile/,
  'the persisted intent profile must match atomically before inventory mutation');
for(const badId of [human+0.5,-1,2**32,NaN,'1'])
  assert.throws(()=>server.getInventoryReceipt(badId),/formId|integer/);
for(const sequence of [0,-1,1.5,NaN,Infinity,Number.MAX_SAFE_INTEGER+1,'1'])
  assert.throws(()=>compare(human,before,'null',after,sequence),/sequence/);
for(const replacement of [null,{entries:[{baseId:15,count:-1}]},
  {entries:[{baseId:15,count:2**32}]},{entries:[{baseId:15,count:1,unsupported:true}]}])
  assert.throws(()=>compare(human,before,'null',replacement,1),/inventory|Inventory/);
for(const entry of [{baseId:15,count:1e300},{baseId:1e300,count:1},
  {baseId:15,count:1,soul:1e300},{baseId:15,count:1,poisonCount:1.5},
  {baseId:15,count:1,health:1e300}])
  assert.throws(()=>compare(human,before,'null',{entries:[entry]},1),/Inventory.*range/);
assert.deepEqual(server.get(human,'inventory'),before,'invalid boundary values never mutate inventory');
assert.equal(compare(human,{entries:[]},'null',after,1),false);
assert.equal(compare(human,before,'null',after,1),true);
const receipt=server.getInventoryReceipt(human);
assert.equal(JSON.parse(receipt).sequence,1);assert.equal(JSON.parse(receipt).profile,42);
assert.deepEqual(server.get(human,'inventory'),after,'one full inventory mutation preserves metadata');
assert.equal(server.getSavedInventoryReceipt(human),'null','live state is not a save acknowledgement');
assert.equal(compare(human,before,'null',after,2),false,'a replay cannot debit twice');
assert.throws(()=>server.set(human,'_inventoryReceipt',{sequence:999}),/receipt|reserved/i);
const property='thornswoodQuests';
const register=(name,owner=false,neighbors=false)=>server.makeProperty(name,{
  isVisibleByOwner:owner,isVisibleByNeighbors:neighbors,updateOwner:'',updateNeighbor:''});
const next={v:1,open:{},cool:{wolf:123456}};
const commit=change=>server.compareAndSetInventoryAndProperty(human,after,receipt,before,2,42,{expectedLifeGeneration:0,...change});
for(const name of ['missing','_inventoryReceipt'])
  assert.throws(()=>commit({name,expected:null,replacement:next}),/hidden custom property/);
register('inventory');
assert.throws(()=>commit({name:'inventory',expected:null,replacement:next}),/hidden custom property/);
register('shownOwner',true);register('shownNeighbor',false,true);
for(const name of ['shownOwner','shownNeighbor'])
  assert.throws(()=>commit({name,expected:null,replacement:next}),/hidden custom property/);
register(property);
for(const entry of [{baseId:0x00ffffff,count:1},{baseId:0x3c,count:1},
  {baseId:15,count:1,enchantmentId:0x00ffffff},{baseId:15,count:1,poisonId:15}]) {
  assert.throws(()=>server.compareAndSetInventoryAndProperty(human,after,receipt,{entries:[entry]},2,42,
    {name:property,expected:null,replacement:next,expectedLifeGeneration:0}),/transaction.*form/i);
  assert.deepEqual(server.get(human,'inventory'),after);
  assert.equal(server.get(human,property),null);
  assert.equal(server.getInventoryReceipt(human),receipt);
}
assert.throws(()=>commit({name:property,expected:null,replacement:Array(3000).fill(1e-7)}),/transaction property/,
  'the normalized stored JSON must also fit the property bound');
for(const life of [-1,0.5,Number.MAX_SAFE_INTEGER+1,undefined])
  assert.throws(()=>commit({name:property,expected:null,replacement:next,expectedLifeGeneration:life}),/lifeGeneration/i);
register('_skympNpcLifeGeneration');
assert.equal(commit({name:property,expected:null,replacement:{toJSON(){
  server.set(human,'_skympNpcLifeGeneration',1);return next;
}}}),false,'serialization cannot commit against a replacement actor life');
assert.deepEqual(server.get(human,'inventory'),after);
assert.equal(server.getInventoryReceipt(human),receipt);
server.set(human,'_skympNpcLifeGeneration',0);
assert.throws(()=>server.compareAndSetInventoryAndProperty(human,after,receipt,before,2,43,
  {name:property,expected:null,replacement:next,expectedLifeGeneration:0}),/profile/);
assert.throws(()=>commit({name:property,expected:null,replacement:'x'.repeat(16384)}),/transaction property/);
assert.equal(commit({name:property,expected:{},replacement:next}),false);
// JSON serialization may reload the gamemode. Eligibility must be checked
// against the registry after getters/toJSON have finished running.
assert.throws(()=>commit({name:property,expected:null,replacement:{toJSON(){server.clear();return next;}}}),/hidden custom property/);
register(property);
assert.throws(()=>commit({name:property,expected:null,replacement:{toJSON(){server.clear();register(property,true);return next;}}}),/hidden custom property/);
assert.deepEqual(server.get(human,'inventory'),after);
assert.equal(server.getInventoryReceipt(human),receipt);
server.clear();register(property);
assert.equal(commit({name:property,expected:null,replacement:next}),true);
assert.deepEqual(server.get(human,property),next);
assert.deepEqual(server.get(human,'inventory'),before);
const questReceipt=server.getInventoryReceipt(human);
assert.equal(JSON.parse(questReceipt).sequence,2);
const deadline=Date.now()+10000;
(async()=>{
  while(server.getSavedInventoryReceipt(human)!==questReceipt && Date.now()<deadline) {
    server.tick();await new Promise(resolve=>setTimeout(resolve,20));
  }
  assert.equal(server.getSavedInventoryReceipt(human),questReceipt,'real FileDatabase completion acknowledges the exact receipt');
  assert.deepEqual(server.get(human,property),next);
  console.log(JSON.stringify({passed:true,plugins:installed.loadOrder.length,profile:42,sequence:2,metadataPreserved:true,replayRefused:true,saved:true,privateQuest:true}));
  process.exit(0);
})().catch(error=>{console.error(error);process.exit(1);});

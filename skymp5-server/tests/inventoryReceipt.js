'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const addon=require(path.resolve(process.argv[2]));
for(const method of ['compareAndSetInventory','getInventoryReceipt','getSavedInventoryReceipt'])
  assert.equal(typeof addon.ScampServer.prototype[method],'function',method+' must be exposed to the server gamemode');
const installed=JSON.parse(fs.readFileSync(process.argv[3],'utf8'));
assert.equal(installed.loadOrder.length,48,'Use the verified installed plugins');
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
const deadline=Date.now()+10000;
(async()=>{
  while(server.getSavedInventoryReceipt(human)!==receipt && Date.now()<deadline) {
    server.tick();await new Promise(resolve=>setTimeout(resolve,20));
  }
  assert.equal(server.getSavedInventoryReceipt(human),receipt,'real FileDatabase completion acknowledges the exact receipt');
  console.log(JSON.stringify({passed:true,plugins:48,profile:42,sequence:1,metadataPreserved:true,replayRefused:true,saved:true}));
  process.exit(0);
})().catch(error=>{console.error(error);process.exit(1);});

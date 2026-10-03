'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const ts=require('typescript');
const source=fs.readFileSync(process.env.SPAWN||path.join(__dirname,'../ts/systems/spawn.ts'),'utf8');
const compiled=ts.transpileModule(source,{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2022}}).outputText;
async function fixture(existing,withHook=true){
  const calls=[],exports={},sandbox={exports,console:{log(){}},require(name){
    if(name==='../settings')return {Settings:{get:async()=>({startPoints:[{pos:[0,0,0],angleZ:0,worldOrCell:1}]})}};
    throw new Error('Unexpected dependency '+name);
  }};
  if(withHook)sandbox.__thornswoodChargen={created(id){calls.push(['clothes',id]);}};
  vm.createContext(sandbox);vm.runInContext(compiled,sandbox);
  let listener;
  const svr={getActorsByProfileId:()=>existing?[7]:[],createActor:()=>7,
    setUserActor:(user,id)=>calls.push(['packet',id]),setRaceMenuOpen:(id)=>calls.push(['face',id]),
    setEnabled(){},get:()=>({raceId:1}),set(){}};
  await new exports.Spawn(()=>{}).initAsync({svr,gm:{on(name,fn){listener=fn;}}});
  listener(1,42,[]);return calls;
}
(async()=>{
  assert.deepEqual(await fixture(false),[['clothes',7],['packet',7],['face',7]],'a fresh character is dressed before the face menu');
  assert.deepEqual(await fixture(true),[['packet',7]],'a returning character receives no new outfit');
  assert.deepEqual(await fixture(false,false),[['packet',7],['face',7]],'other gamemodes remain supported');
  console.log('PASS creation outfit before spawn and face menu; returning outfits preserved');
})().catch(error=>{console.error(error);process.exitCode=1;});

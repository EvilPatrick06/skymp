"use strict";
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const ts=require('../../../skymp5-client/node_modules/typescript');
const source=fs.readFileSync(path.join(__dirname,'../../../skymp5-client/src/services/services/loadGameService.ts'),'utf8');
const inv={entries:[{baseId:114202,count:1,worn:true},{baseId:114203,count:1,worn:true},{baseId:15,count:40}]};
const js=ts.transpileModule(source,{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2018}}).outputText;
function service(refuse) {
 const calls=[], errors=[], sp={loadGame(...args){calls.push(args);if(refuse) throw Error(refuse);}}, controller={on(){}};
 const sandbox={exports:{},require:m=>m.endsWith('logging')?{logError:(...a)=>errors.push(a)}:{ClientListener:class{}}};
 sandbox.globalThis=sandbox;
 vm.runInNewContext(js,sandbox);
 return {svc:new sandbox.exports.LoadGameService(sp,controller),calls,errors,sandbox};
}
const face={name:'Adventurer'};
{
 const {svc,calls}=service('');
 svc.loadGame([1,2,3],[0,0,0],9,face,['Skyrim.esm'],{hours:12,minutes:0,seconds:0},inv);
 assert.equal(calls.length,1); assert.equal(calls[0][3],face); assert.equal(calls[0][6],inv);
 calls.length=0; svc.loadGame([1,2,3],[0,0,0],9,face,['Skyrim.esm'],{hours:12,minutes:0,seconds:0});
 assert.equal(calls[0][6],undefined,'returning six-argument calls retain their established path');
}
{
 // Thornswood #1994: a save the native refuses is not loaded again without the face.
 const why='Form 0xFE029827 names light plugin 41 and the game listed 30';
 const {svc,calls,errors,sandbox}=service(why);
 assert.throws(()=>svc.loadGame([1,2,3],[0,0,0],9,face,['Skyrim.esm'],{hours:12,minutes:0,seconds:0},inv),{message:why});
 assert.equal(calls.length,1,'no second load without the appearance');
 assert.equal(sandbox.__thornswoodLoadRefused,why,'the front is handed the reason');
 assert.equal(errors.length,1); assert.ok(errors[0].includes(why),'the log names the form');
 assert.equal(svc._isCausedBySkyrimPlatform,false,'a refused load leaves no load expected');
}
console.log('PASS actual load service: the starting pack reaches the load, and a refused save stops it and says why');

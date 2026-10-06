"use strict";
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const ts=require('../../../skymp5-client/node_modules/typescript');
const source=fs.readFileSync(path.join(__dirname,'../../../skymp5-client/src/services/services/loadGameService.ts'),'utf8');
const inv={entries:[{baseId:114202,count:1,worn:true},{baseId:114203,count:1,worn:true},{baseId:15,count:40}]};
for(const fallback of [false,true]) {
 const calls=[], sp={loadGame(...args){calls.push(args);if(fallback && calls.length===1) throw Error('headpart failed');}}, controller={on(){}};
 const sandbox={exports:{},require:()=>({ClientListener:class{}})};
 vm.runInNewContext(ts.transpileModule(source,{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2018}}).outputText,sandbox);
 const service=new sandbox.exports.LoadGameService(sp,controller),face={name:'Adventurer'};
 service.loadGame([1,2,3],[0,0,0],9,face,['Skyrim.esm'],{hours:12,minutes:0,seconds:0},inv);
 assert.equal(calls.length,fallback?2:1);for(const call of calls) assert.equal(call[6],inv);
 if(fallback) assert.equal(calls[1][3],undefined);
 calls.length=0; service.loadGame([1,2,3],[0,0,0],9,face,['Skyrim.esm'],{hours:12,minutes:0,seconds:0}); for(const call of calls) assert.equal(call[6],undefined, 'returning six-argument calls retain their established path');
}
console.log('PASS actual load service: initial inventory survives appearance fallback');

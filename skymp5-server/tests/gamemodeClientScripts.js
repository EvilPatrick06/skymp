'use strict';
// ts/gamemodeClientScripts.ts over a stand-in server (Thornswood #3196): what
// a gamemode gives connected games to run, by digest, and whether a reload
// changed it.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),Module=require('node:module');
const ts=require('typescript');
const file=path.join(__dirname,'../ts/gamemodeClientScripts.ts');
const out=ts.transpileModule(fs.readFileSync(file,'utf8'),{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2022}}).outputText;
const m=new Module(file);m.filename=file;m.paths=Module._nodeModulePaths(path.dirname(file));m._compile(out,file);
const {recordClientScripts}=m.exports;

let failed=0;
function check(name,fn){try{fn();console.log('PASS  '+name);}catch(e){failed++;console.log('FAIL  '+name+': '+e.message);}}

// The native server's three methods, as far as this needs: a property made
// twice is refused, as the engine refuses it, and clear forgets them all.
function server(){
  const made=new Set(),calls=[];
  const s={
    makeProperty(name,info){if(this!==s)throw new Error('called on another object');if(made.has('p'+name))throw new Error('already made');made.add('p'+name);calls.push(['property',name,info]);},
    makeEventSource(name,body){if(this!==s)throw new Error('called on another object');made.add('e'+name);calls.push(['event',name,body]);},
    clear(){made.clear();calls.push(['clear']);},
  };
  return {s,calls};
}
// A gamemode's load: clear, then what it makes.
function load(r,s,props,events={}){
  r.clear();
  for(const [n,info] of Object.entries(props))s.makeProperty(n,info);
  for(const [n,body] of Object.entries(events))s.makeEventSource(n,body);
  return r.loaded();
}
const prop=(owner,neighbor='',o=true,n=false)=>({isVisibleByOwner:o,isVisibleByNeighbors:n,updateOwner:owner,updateNeighbor:neighbor});

check('the first load is not a reload, and says what it registered',()=>{
  const {s,calls}=server(),r=recordClientScripts(s);
  const a=load(r,s,{x:prop('return 1')},{onHit:'ctx.sendEvent()'});
  assert.equal(a.reload,false);assert.equal(a.clientScriptsChanged,false);assert.equal(a.scripts,2);
  assert.match(a.digest,/^[0-9a-f]{12}$/);
  assert.deepEqual(calls.map(c=>c[0]),['clear','property','event'],'the server itself still gets every call');
});
check('a reload with the same scripts says unchanged, in any order',()=>{
  const {s}=server(),r=recordClientScripts(s);
  const a=load(r,s,{x:prop('return 1'),y:prop('return 2')});
  const b=load(r,s,{y:prop('return 2'),x:prop('return 1')});
  assert.equal(b.reload,true);assert.equal(b.clientScriptsChanged,false);assert.equal(b.digest,a.digest);
});
check('a reload that changes one update script says changed',()=>{
  const {s}=server(),r=recordClientScripts(s);
  load(r,s,{x:prop('return 1')});
  const b=load(r,s,{x:prop('return 2')});
  assert.equal(b.clientScriptsChanged,true);
});
check('so does one that changes who sees a property, adds one, or changes an event source',()=>{
  for(const [first,second,ev1,ev2] of [
    [{x:prop('a')},{x:prop('a','',true,true)},{},{}],
    [{x:prop('a')},{x:prop('a'),z:prop('b')},{},{}],
    [{x:prop('a')},{x:prop('a')},{e:'one'},{e:'two'}],
  ]){
    const {s}=server(),r=recordClientScripts(s);
    load(r,s,first,ev1);
    assert.equal(load(r,s,second,ev2).clientScriptsChanged,true,JSON.stringify([first,second,ev1,ev2]));
  }
});
check('a property the server refuses is not counted',()=>{
  const {s}=server(),r=recordClientScripts(s);
  r.clear();s.makeProperty('x',prop('a'));
  assert.throws(()=>s.makeProperty('x',prop('b')),/already made/);
  const a=r.loaded();
  const {s:s2}=server(),r2=recordClientScripts(s2);
  assert.equal(load(r2,s2,{x:prop('a')}).digest,a.digest,'the refused second script left the record as the first');
});
check('clear forgets the last load, so a load that registers less is a change',()=>{
  const {s}=server(),r=recordClientScripts(s);
  load(r,s,{x:prop('a'),y:prop('b')});
  const b=load(r,s,{x:prop('a')});
  assert.equal(b.scripts,1);assert.equal(b.clientScriptsChanged,true);
});

// The real server is a node-addon-api ObjectWrap: its methods are on the
// prototype with napi_default attributes, so read-only. An assignment to the
// instance cannot shadow a read-only inherited property; in the engine's
// bundle (not strict) it did nothing at all, and the first load on dev with
// #105 logged "client scripts e3b0c44298fc (0)" for a gamemode that makes
// dozens of properties at load.
function nativeServer(){
  const made=[];
  function Native(){}
  for(const [k,fn] of Object.entries({
    makeProperty(name){made.push('property '+name);},
    makeEventSource(name){made.push('event '+name);},
    clear(){made.length=0;},
  }))Object.defineProperty(Native.prototype,k,{value:fn,writable:false,enumerable:false,configurable:false});
  return {s:new Native(),made};
}
check('a server whose methods are read-only on its prototype, as a native one is, is recorded',()=>{
  const {s,made}=nativeServer(),r=recordClientScripts(s);
  const a=load(r,s,{x:prop('return 1'),y:prop('return 2')},{onHit:'ctx.sendEvent()'});
  assert.equal(a.scripts,3);
  assert.deepEqual(made,['property x','property y','event onHit'],'the server itself still gets every call');
  assert.equal(load(r,s,{x:prop('return 9'),y:prop('return 2')},{onHit:'ctx.sendEvent()'}).clientScriptsChanged,true);
});
check('and a gamemode calling them through the global mp, as the engine sets it, is recorded too',()=>{
  const {s}=nativeServer(),r=recordClientScripts(s);
  const mp=s;
  r.clear();mp.makeProperty('x',prop('a'));
  assert.equal(r.loaded().scripts,1);
});

if(failed){console.log(failed+' failed');process.exit(1);}

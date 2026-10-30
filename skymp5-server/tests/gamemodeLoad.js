'use strict';
// ts/gamemodeLoad.ts and ts/gamemodeClientScripts.ts together, on real
// gamemode files (Thornswood #3196): what the server says after each load,
// which a deploy that reloads the gamemode waits for, and the gamemode's hook.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),os=require('node:os'),Module=require('node:module');
const ts=require('typescript');
function load(file,map){
  const out=ts.transpileModule(fs.readFileSync(file,'utf8'),{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2022,esModuleInterop:true}}).outputText;
  const m=new Module(file);m.filename=file;m.paths=Module._nodeModulePaths(path.dirname(file));
  const real=m.require.bind(m);m.require=(name)=>name in map?map[name]:real(name);
  m._compile(out,file);return m.exports;
}
const root=path.join(__dirname,'..');
const scripts=load(path.join(root,'ts/gamemodeClientScripts.ts'),{});
const loader=load(path.join(root,'ts/gamemodeLoad.ts'),{'./gamemodeClientScripts':scripts});

const dir=fs.mkdtempSync(path.join(os.tmpdir(),'gamemode-load-'));
const gm=path.join(dir,'gamemode.js');
const lines=[];
const log=console.log,err=console.error;
console.log=(...a)=>lines.push(a.join(' '));console.error=(...a)=>lines.push('ERR '+a.join(' '));
const server={made:new Set(),makeProperty(n){if(this.made.has(n))throw new Error('already made');this.made.add(n);},makeEventSource(){},clear(){this.made.clear();}};
const recorder=scripts.recordClientScripts(server);
globalThis.mp=server;
const hooks=[];
// A gamemode: one property with this script, a hook unless told not to, and a
// throw half way when asked.
function write(script,{hook=true,throwAt=false}={}){
  fs.writeFileSync(gm,
    `mp.makeProperty('p',{isVisibleByOwner:true,updateOwner:${JSON.stringify(script)},updateNeighbor:''});\n`+
    (throwAt?`throw new Error('half way');\n`:'')+
    (hook?`globalThis.__thornswoodGamemodeLoaded=(l)=>globalThis.__hooks.push(Object.assign({script:${JSON.stringify(script)}},l));\n`:''));
}
globalThis.__hooks=hooks;
function run(){lines.length=0;loader.requireUncached(gm,recorder,server);return lines.slice();}

let failed=0;
function check(name,fn){try{fn();log('PASS  '+name);}catch(e){failed++;log('FAIL  '+name+': '+e.message);}}

check('the first load says its client scripts, and the gamemode hears it is not a reload',()=>{
  write('a');const out=run();
  assert.ok(out.some(l=>/^\[thornswood\] gamemode client scripts [0-9a-f]{12} \(1\), the first load in this process$/.test(l)),out.join('\n'));
  assert.deepEqual(hooks.pop(),{script:'a',reload:false,clientScriptsChanged:false});
});
check('the same file again loads nothing and says nothing',()=>{
  const out=run();assert.deepEqual(out,[]);assert.equal(hooks.length,0);
});
check('a reload that keeps the scripts says unchanged',()=>{
  fs.appendFileSync(gm,'// a server-side change\n');const out=run();
  assert.ok(out.some(l=>/, unchanged since the last load$/.test(l)),out.join('\n'));
  assert.deepEqual(hooks.pop(),{script:'a',reload:true,clientScriptsChanged:false});
});
check('a reload that changes a script says changed, and so does the hook',()=>{
  write('b');const out=run();
  assert.ok(out.some(l=>/, changed since the last load$/.test(l)),out.join('\n'));
  assert.deepEqual(hooks.pop(),{script:'b',reload:true,clientScriptsChanged:true});
});
check('a gamemode that throws while loading says it did not load, and no hook answers',()=>{
  write('c',{throwAt:true});const out=run();
  assert.ok(out.some(l=>l==='[thornswood] gamemode did not load; the error is above'),out.join('\n'));
  assert.ok(out.some(l=>/^ERR .*half way/.test(l)),'the stack is still printed');
  assert.ok(!out.some(l=>/gamemode client scripts/.test(l)));
  assert.equal(hooks.length,0,'the hook b left was taken away before the load');
});
check('after a failed load, putting the last good one back is compared with that one',()=>{
  write('b');const out=run();
  assert.ok(out.some(l=>/, unchanged since the last load$/.test(l)),out.join('\n'));
  assert.deepEqual(hooks.pop(),{script:'b',reload:true,clientScriptsChanged:false});
});
check('a gamemode with no hook is loaded and nothing earlier answers for it',()=>{
  write('d',{hook:false});const out=run();
  assert.ok(out.some(l=>/, changed since the last load$/.test(l)),out.join('\n'));
  assert.equal(hooks.length,0);
});

console.log=log;console.error=err;
fs.rmSync(dir,{recursive:true,force:true});
if(failed){log(failed+' failed');process.exit(1);}

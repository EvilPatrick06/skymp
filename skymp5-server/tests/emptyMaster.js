'use strict';
// Thornswood #1788: an offline build writes "master": "" into
// server-settings.json. Loads ts/settings.ts on that file and checks the empty
// master is kept, then runs ts/systems/masterClient.ts with it and checks it
// says "No master server specified" and builds no endpoint. Needs typescript
// from node_modules (yarn install first).
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),os=require('node:os'),Module=require('node:module');
const ts=require('typescript');
const root=path.join(__dirname,'..');

function load(file,map){
  const out=ts.transpileModule(fs.readFileSync(file,'utf8'),{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2022,esModuleInterop:true}}).outputText;
  const m=new Module(file);m.filename=file;m.paths=Module._nodeModulePaths(path.dirname(file));
  const real=m.require.bind(m);m.require=(name)=>name in map?map[name]:real(name);
  m._compile(out,file);return m.exports;
}
// Only what loadSettings touches is real; the GitHub and argument parts are not reached.
const merge=(a,b)=>{for(const k of Object.keys(b)){const v=b[k];a[k]=v&&typeof v==='object'&&!Array.isArray(v)&&a[k]&&typeof a[k]==='object'?merge(a[k],v):v;}return a;};
const stubs={
  '@octokit/rest':{Octokit:class{}},
  '@octokit/request-error':{RequestError:class extends Error{}},
  argparse:{ArgumentParser:class{parse_args(){return {};}}},
  lodash:{__esModule:true,default:{merge}},
};

async function settingsIn(json){
  const dir=fs.mkdtempSync(path.join(os.tmpdir(),'settings-master-'));
  const was=process.cwd();
  try{
    fs.writeFileSync(path.join(dir,'server-settings.json'),JSON.stringify(json));
    process.chdir(dir);
    const {Settings}=load(path.join(root,'ts/settings.ts'),stubs);
    return await Settings.get();
  }finally{process.chdir(was);fs.rmSync(dir,{recursive:true,force:true});}
}

async function masterLog(master){
  const {MasterClient}=load(path.join(root,'ts/systems/masterClient.ts'),{axios:{__esModule:true,default:{}}});
  const lines=[];
  const c=new MasterClient((...a)=>lines.push(a.join(' ')),7777,master,100,'test',null,5000,true);
  await c.initAsync();
  return {lines,endpoint:c.endpoint};
}

(async()=>{
  const s=await settingsIn({master:'',offlineMode:true,port:7777});
  assert.equal(s.master,'','an empty master is kept, not replaced by the public default');
  const off=await masterLog(s.master);
  assert.deepEqual(off.lines,['No master server specified']);
  assert.equal(off.endpoint,undefined,'no endpoint is built');
  console.log('ok   "master": "" is kept and logs No master server specified, no endpoint');

  const unset=await settingsIn({offlineMode:true});
  assert.equal(unset.master,'https://gateway.skymp.net','an unset master keeps the default');
  const set=await settingsIn({master:'https://example.test'});
  assert.equal(set.master,'https://example.test','a set master is used');
  const on=await masterLog(set.master);
  assert.equal(on.endpoint,'https://example.test/api/servers/null');
  console.log('ok   an unset master keeps the default and a set one is used');
})().catch((e)=>{console.error(e);process.exitCode=1;});

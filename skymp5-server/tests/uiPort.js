'use strict';
// Runs ts/ui.ts the way the server does, on the real koa stack from
// node_modules (yarn install first), and calls the UI port over HTTP.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),os=require('node:os'),http=require('node:http'),Module=require('node:module');
const ts=require('typescript');
const root=path.join(__dirname,'..');

// Uploads land in the temp folder; give this run its own, so a file left
// behind is one this run caused.
const tmp=fs.mkdtempSync(path.join(os.tmpdir(),'ui-port-'));
process.env.TMPDIR=process.env.TEMP=process.env.TMP=tmp;

function load(file,map){
  const out=ts.transpileModule(fs.readFileSync(file,'utf8'),{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2022,esModuleInterop:true}}).outputText;
  const m=new Module(file);m.filename=file;m.paths=Module._nodeModulePaths(path.dirname(file));
  const real=m.require.bind(m);m.require=(name)=>name in map?map[name]:real(name);
  m._compile(out,file);return m.exports;
}
const metrics=load(path.join(root,'ts/systems/metricsSystem.ts'),{});
const servers=[];
const ui=load(path.join(root,'ts/ui.ts'),{
  './systems/metricsSystem':metrics,
  // No UI dev server: main() takes the path every real server takes.
  axios:{__esModule:true,default:()=>Promise.reject(new Error('no dev server'))},
  http:{...http,createServer:(...a)=>{const s=http.createServer(...a);servers.push(s);return s;}},
});

const calls=[];
ui.setServer({onHttpRpcRunAttempt:(name,payload)=>{calls.push([name,payload]);return {ran:name};}});

function freePort(){return new Promise(r=>{const s=http.createServer().listen(0,()=>{const p=s.address().port;s.close(()=>r(p));});});}
let uiPort;
function send(method,p,{headers={},body}={}){
  return new Promise((res,rej)=>{
    const r=http.request({host:'127.0.0.1',port:uiPort,method,path:p,headers},x=>{let d='';x.on('data',c=>d+=c);x.on('end',()=>res({status:x.statusCode,body:d}));});
    r.on('error',rej);if(body)r.write(body);r.end();
  });
}
const boundary='----uiport';
const multipart=`--${boundary}\r\nContent-Disposition: form-data; name="payload"; filename="big.bin"\r\nContent-Type: application/octet-stream\r\n\r\n${'A'.repeat(1<<20)}\r\n--${boundary}--\r\n`;
const asMultipart={headers:{'Content-Type':'multipart/form-data; boundary='+boundary},body:multipart};
const asJson=(o)=>({headers:{'Content-Type':'application/json'},body:JSON.stringify(o)});
const leftInTmp=()=>fs.readdirSync(tmp);

let failed=0;
async function check(name,fn){
  try{await fn();console.log('PASS  '+name);}
  catch(e){failed++;console.log('FAIL  '+name+'\n      '+(e&&e.message));}
}

(async()=>{
  uiPort=await freePort();
  ui.main({port:uiPort-1,allSettings:{}});
  // main() listens once its dev server probe has failed; wait for that listen.
  await new Promise((r)=>{const t=()=>servers.length&&servers[0].listening?r():setImmediate(t);t();});

  await check('a multipart upload to a path nobody serves leaves nothing in the temp folder',async()=>{
    const r=await send('POST','/no-such-route',asMultipart);
    assert.equal(r.status,404);
    assert.deepEqual(leftInTmp(),[]);
  });
  await check('a multipart upload to the RPC route is not parsed and leaves nothing in the temp folder',async()=>{
    calls.length=0;
    await send('POST','/rpc/Echo',asMultipart);
    assert.deepEqual(leftInTmp(),[]);
    assert.deepEqual(calls,[['Echo',undefined]]);
  });
  await check('a JSON RPC still reaches the handler with its payload',async()=>{
    calls.length=0;
    const r=await send('POST','/rpc/Echo',asJson({payload:{a:1}}));
    assert.equal(r.status,200);
    assert.deepEqual(JSON.parse(r.body),{ran:'Echo'});
    assert.deepEqual(calls,[['Echo',{a:1}]]);
  });

  for(const s of servers)s.close();
  fs.rmSync(tmp,{recursive:true,force:true});
  if(failed){console.log(failed+' failed');process.exit(1);}
  process.exit(0);
})().catch(e=>{console.error(e);process.exit(1);});

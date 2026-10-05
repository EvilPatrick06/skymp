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
const calls=[];

// One UI port as main() starts it. devServer true takes the path a server
// takes when a UI dev server answers on 1234: a proxy on the UI port in front
// of the app on a port of its own.
function freePort(){return new Promise(r=>{const s=http.createServer().listen(0,()=>{const p=s.address().port;s.close(()=>r(p));});});}
async function start(devServer){
  const mine=[];
  const ui=load(path.join(root,'ts/ui.ts'),{
    './systems/metricsSystem':metrics,
    axios:{__esModule:true,default:()=>devServer?Promise.resolve({}):Promise.reject(new Error('no dev server'))},
    http:{...http,createServer:(...a)=>{const s=http.createServer(...a);mine.push(s);servers.push(s);return s;}},
  });
  ui.setServer({onHttpRpcRunAttempt:(name,payload)=>{calls.push([name,payload]);return {ran:name};}});
  const port=await freePort();
  ui.main({port:port-1,allSettings:{}});
  // main() listens once its dev server probe is answered; wait for those listens.
  const want=devServer?2:1;
  await new Promise((r)=>{const t=()=>mine.length===want&&mine.every(s=>s.listening)?r():setImmediate(t);t();});
  return port;
}

// The address a caller from another machine has: this machine's own
// non-loopback address, so the server sees a socket that is not loopback.
const outside=Object.values(os.networkInterfaces()).flat().find(a=>a&&a.family==='IPv4'&&!a.internal);
let uiPort;
function send(method,p,{headers={},body,from='127.0.0.1',port=uiPort}={}){
  return new Promise((res,rej)=>{
    const r=http.request({host:from,port,method,path:p,headers},x=>{let d='';x.on('data',c=>d+=c);x.on('end',()=>res({status:x.statusCode,body:d}));});
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
  uiPort=await start(false);

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

  await check('this machine has an address that is not loopback to call from',async()=>{
    assert.ok(outside,'no non-internal IPv4 interface, so a caller from another machine cannot be tried');
  });
  const series=async(name)=>(await metrics.register.metrics()).split('\n').filter(l=>l.includes('rpcClassName="'+name+'"'));
  if(outside){
    const from=outside.address;
    await check('an RPC from another machine is refused before it runs or counts',async()=>{
      calls.length=0;
      const r=await send('POST','/rpc/FromOutside',{...asJson({payload:1}),from});
      assert.equal(r.status,403);
      assert.deepEqual(calls,[]);
      assert.deepEqual(await series('FromOutside'),[]);
    });
    await check('the route name in other letter case is refused the same way',async()=>{
      calls.length=0;
      const r=await send('POST','/RPC/FromOutside',{...asJson({payload:1}),from});
      assert.equal(r.status,403);
      assert.deepEqual(calls,[]);
    });
    await check('a multipart upload from another machine to the RPC route is refused unread',async()=>{
      const r=await send('POST','/rpc/FromOutside',{...asMultipart,from});
      assert.equal(r.status,403);
      assert.deepEqual(leftInTmp(),[]);
    });
    await check('the rest of the UI port still answers another machine',async()=>{
      const r=await send('GET','/no-such-file.json',{from});
      assert.equal(r.status,404);
    });
    const proxied=await start(true);
    await check('behind the UI dev server proxy an RPC from another machine is refused',async()=>{
      calls.length=0;
      const r=await send('POST','/rpc/FromOutside',{...asJson({payload:1}),from,port:proxied});
      assert.equal(r.status,403);
      assert.deepEqual(calls,[]);
    });
    await check('behind the UI dev server proxy an RPC from this machine still runs',async()=>{
      calls.length=0;
      const r=await send('POST','/rpc/Echo',{...asJson({payload:2}),port:proxied});
      assert.equal(r.status,200);
      assert.deepEqual(calls,[['Echo',2]]);
    });
  }

  for(const s of servers)s.close();
  fs.rmSync(tmp,{recursive:true,force:true});
  if(failed){console.log(failed+' failed');process.exit(1);}
  process.exit(0);
})().catch(e=>{console.error(e);process.exit(1);});

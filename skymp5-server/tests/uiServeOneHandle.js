'use strict';
// The resources port serves each file from the one handle it opened
// (Thornswood #2740, ts/serveData.ts). Runs ts/ui.ts the way the server does,
// on the real koa stack from node_modules (yarn install first), against a
// data folder of its own, and asks it over HTTP.
//
// THE RACE. koa-static, which this replaced, stat'ed a file for its
// Content-Length and then opened it again by name to send it, so a file
// replaced between the two went out with one file's length and the other's
// bytes (38 of 891 answers bad on #2740 with online.json replaced every
// 20 ms). Here the file is replaced the moment the port has first touched it
// by name, whatever that touch is: the answer must still be one file whole,
// its bytes and its length. Run against the old ui.ts it fails (UI_TS names
// another ui.ts to load; its own node_modules are used).
//
// Also koa-static's behaviour as serve("data") had it: GET and HEAD only, 404
// for nothing there, nothing under a name starting with a dot, nothing outside
// the folder, index.html for a folder, a .gz beside the file, the type from
// the extension, Cache-Control max-age=0.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),os=require('node:os'),http=require('node:http'),Module=require('node:module'),zlib=require('node:zlib');
const ts=require('typescript');
const root=path.join(__dirname,'..');
const uiFile=process.env.UI_TS?path.resolve(process.env.UI_TS):path.join(root,'ts/ui.ts');

// The race: the first time the port touches the watched file by name (a
// stat, an open, an access, through any of fs's forms), the file is replaced
// at once by another of a different length, the way a whole-file write
// replaces it: the name goes (node's unlink, a POSIX delete on Windows, so a
// handle already open keeps the old file) and a new file takes it.
let watched=null,replaceWith=null,touches=0;
const realUnlink=fs.unlinkSync.bind(fs),realWrite=fs.writeFileSync.bind(fs);
function touched(p){
  if(watched===null||typeof p!=='string'||path.resolve(p)!==watched)return;
  touches++;
  if(replaceWith!==null){const next=replaceWith;replaceWith=null;realUnlink(watched);realWrite(watched,next);}
}
for(const name of ['stat','open','access','lstat']){
  const real=fs[name].bind(fs);
  fs[name]=function(p,...rest){const cb=rest[rest.length-1];
    if(typeof cb==='function'){rest[rest.length-1]=function(...a){touched(p);return cb.apply(this,a);};}
    return real(p,...rest);};
  const realP=fs.promises[name].bind(fs.promises);
  fs.promises[name]=async function(p,...rest){const r=await realP(p,...rest);touched(p);return r;};
}

function load(file,map){
  const out=ts.transpileModule(fs.readFileSync(file,'utf8'),{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2022,esModuleInterop:true}}).outputText;
  const m=new Module(file);m.filename=file;m.paths=Module._nodeModulePaths(path.dirname(file));
  const real=m.require.bind(m);m.require=(name)=>name in map?map[name]:real(name);
  m._compile(out,file);return m.exports;
}
const tsDir=path.dirname(uiFile);
const map={
  './systems/metricsSystem':load(path.join(tsDir,'systems/metricsSystem.ts'),{}),
  axios:{__esModule:true,default:()=>Promise.reject(new Error('no dev server'))},
};
if(fs.existsSync(path.join(tsDir,'serveData.ts')))map['./serveData']=load(path.join(tsDir,'serveData.ts'),{});

const box=fs.mkdtempSync(path.join(os.tmpdir(),'ui-one-handle-'));
const data=path.join(box,'data');
const put=(rel,bytes)=>{const f=path.join(data,rel);fs.mkdirSync(path.dirname(f),{recursive:true});realWrite(f,bytes);return f;};
const servers=[];
function freePort(){return new Promise(r=>{const s=http.createServer().listen(0,'127.0.0.1',()=>{const p=s.address().port;s.close(()=>r(p));});});}
function ask(port,method,p,headers={}){
  return new Promise((res,rej)=>{
    const r=http.request({host:'127.0.0.1',port,method,path:p,headers},x=>{const parts=[];x.on('data',c=>parts.push(c));
      x.on('end',()=>res({status:x.statusCode,headers:x.headers,body:Buffer.concat(parts),complete:x.complete}));
      x.on('aborted',()=>res({status:x.statusCode,headers:x.headers,body:Buffer.concat(parts),complete:false}));});
    r.on('error',e=>res({status:0,error:e.message,headers:{},body:Buffer.alloc(0),complete:false}));r.end();
  });
}

let failed=0,passed=0;
async function check(name,fn){
  try{await fn();passed++;console.log('PASS  '+name);}
  catch(e){failed++;console.log('FAIL  '+name+'\n      '+(e&&e.message));}
}

(async()=>{
  fs.mkdirSync(data);
  process.chdir(box);   // ui.ts serves "data" from the working folder, as the server does
  const ui=load(uiFile,{...map,http:{...http,createServer:(...a)=>{const s=http.createServer(...a);servers.push(s);return s;}}});
  ui.setServer({onHttpRpcRunAttempt:()=>({}),getPrometheusMetrics:()=>''});
  const port=await freePort();
  ui.main({port:port-1,allSettings:{uiListenHost:'127.0.0.1'}});
  await new Promise(r=>{const t=()=>servers.length&&servers.every(s=>s.listening)?r():setImmediate(t);t();});

  const online=put('online.json','{"now":3,"at":1}');
  await check('a file is answered whole, with its length and type',async()=>{
    const r=await ask(port,'GET','/online.json');
    assert.equal(r.status,200);
    assert.equal(r.body.toString(),'{"now":3,"at":1}');
    assert.equal(r.headers['content-length'],'16');
    assert.match(r.headers['content-type'],/^application\/json/);
    assert.equal(r.headers['cache-control'],'max-age=0');
    assert.ok(r.headers['last-modified']);
  });
  await check('HEAD says the length and sends nothing',async()=>{
    const r=await ask(port,'HEAD','/online.json');
    assert.equal(r.status,200);
    assert.equal(r.headers['content-length'],'16');
    assert.equal(r.body.length,0);
  });
  await check('a file further down is answered',async()=>{
    put('api/servers/thornswood/manifest.json','{"mods":[]}');
    const r=await ask(port,'GET','/api/servers/thornswood/manifest.json');
    assert.equal(r.status,200);assert.equal(r.body.toString(),'{"mods":[]}');
  });
  await check('an empty file is answered empty',async()=>{
    put('empty.json','');
    const r=await ask(port,'GET','/empty.json');
    assert.equal(r.status,200);assert.equal(r.headers['content-length'],'0');assert.equal(r.body.length,0);
  });
  await check('a file that is not there is 404',async()=>{
    assert.equal((await ask(port,'GET','/no-such.json')).status,404);
  });
  await check('nothing under a name that starts with a dot, so a whole write\'s new file is never served',async()=>{
    put('.online.json.whole-1-0','{"now":9}');
    assert.equal((await ask(port,'GET','/.online.json.whole-1-0')).status,404);
    put('.hidden/x.json','{}');
    assert.equal((await ask(port,'GET','/.hidden/x.json')).status,404);
  });
  await check('nothing outside the folder',async()=>{
    realWrite(path.join(box,'server-settings.json'),'{"secret":1}');
    const r=await ask(port,'GET','/..%2fserver-settings.json');
    assert.ok(r.status===403||r.status===404,'answered '+r.status);
    assert.ok(!r.body.toString().includes('secret'));
  });
  await check('a path that does not decode is 400',async()=>{
    assert.equal((await ask(port,'GET','/%E0%A4%A')).status,400);
  });
  await check('a folder answers its index.html, with or without the slash',async()=>{
    put('site/index.html','<p>hi</p>');
    for(const p of ['/site/','/site']){
      const r=await ask(port,'GET',p);
      assert.equal(r.status,200,p);assert.equal(r.body.toString(),'<p>hi</p>',p);
      assert.match(r.headers['content-type'],/^text\/html/,p);
    }
    fs.mkdirSync(path.join(data,'bare'));
    assert.equal((await ask(port,'GET','/bare/')).status,404);
  });
  await check('a .gz beside the file is answered to a caller that takes gzip, typed as the file',async()=>{
    put('big.json','{"a":1}');
    realWrite(path.join(data,'big.json.gz'),zlib.gzipSync('{"a":1}'));
    const r=await ask(port,'GET','/big.json',{'Accept-Encoding':'gzip'});
    assert.equal(r.headers['content-encoding'],'gzip');
    assert.match(r.headers['content-type'],/^application\/json/);
    assert.equal(zlib.gunzipSync(r.body).toString(),'{"a":1}');
    const plain=await ask(port,'GET','/big.json',{'Accept-Encoding':'identity'});
    assert.equal(plain.headers['content-encoding'],undefined);assert.equal(plain.body.toString(),'{"a":1}');
  });
  await check('only GET and HEAD are served',async()=>{
    assert.equal((await ask(port,'POST','/online.json')).status,404);
  });
  await check('the routes in front still refuse scripts and plugins',async()=>{
    put('scripts/x.pex','x');put('x.esp','x');
    assert.equal((await ask(port,'GET','/scripts/x.pex')).status,403);
    assert.equal((await ask(port,'GET','/x.esp')).status,403);
  });

  // The race, both ways round: a file replaced by a longer one, and by a
  // shorter one, the moment the port first touches it.
  const before='{"now":3,"at":1}';
  for(const [name,after] of [['longer','{"now":12,"at":1760000000000,"restarting":false,"max":30,"line":0,"full":false}'],['shorter','{"now":0}']]){
    await check('a file replaced by a '+name+' one as it is answered goes out as one file, whole',async()=>{
      realWrite(online,before);
      watched=path.resolve(online);replaceWith=after;touches=0;
      const r=await ask(port,'GET','/online.json');
      watched=null;
      assert.ok(touches>=1,'the port never touched the file by name');
      assert.equal(replaceWith,null,'the file was not replaced');
      assert.equal(r.status,200);
      assert.ok(r.complete,'the answer stopped short of its length');
      const got=r.body.toString();
      assert.equal(String(Buffer.byteLength(got)),r.headers['content-length'],
        'Content-Length '+r.headers['content-length']+' for '+Buffer.byteLength(got)+' bytes: '+JSON.stringify(got));
      assert.ok(got===before||got===after,'neither file: '+JSON.stringify(got));
      JSON.parse(got);
      assert.equal(fs.readFileSync(online,'utf8'),after);
    });
  }
  await check('and the next ask gets the new file',async()=>{
    const r=await ask(port,'GET','/online.json');
    assert.equal(r.body.toString(),'{"now":0}');assert.equal(r.headers['content-length'],'9');
  });

  for(const s of servers)s.close();
  process.chdir(os.tmpdir());
  fs.rmSync(box,{recursive:true,force:true});
  console.log(passed+' passed, '+failed+' failed');
  process.exit(failed?1:0);
})().catch(e=>{console.error(e);process.exit(1);});

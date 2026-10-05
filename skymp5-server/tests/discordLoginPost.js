'use strict';
// The login line the server posts to a guild's eventLogChannelId: it names
// the slot, actor, profile and Discord account, and never the address the
// person connected from.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const ts=require('typescript');
const source=fs.readFileSync(path.join(__dirname,'../ts/systems/login.ts'),'utf8');
const compiled=ts.transpileModule(source,{compilerOptions:{module:ts.ModuleKind.CommonJS,target:ts.ScriptTarget.ES2022}}).outputText;
const IP='203.0.113.7';

async function login(guilds,roles){
  const posts=[],logged=[];
  let allowed,failed;const spawned=new Promise((r,j)=>{allowed=r;failed=j;});
  const fetch=async(url,opts={})=>{
    if(url.includes('/api/servers/'))return {ok:true,status:200,json:async()=>({user:{id:5,discordId:'111'}})};
    if(url.includes('/members/'))return {ok:true,status:200,json:async()=>({roles})};
    if(url.includes('/messages')){posts.push(JSON.parse(opts.body).content);return {ok:true,status:200,json:async()=>({})};}
    throw new Error('unexpected fetch '+url);
  };
  const exports={};
  const sandbox={exports,global:{fetch},console:{log:(...a)=>logged.push(a.join(' ')),error:(...a)=>failed(new Error(a.map(x=>x&&x.stack||String(x)).join(' ')))},require(name){
    if(name==='../settings')return {Settings:{get:async()=>({discordAuth:{botToken:'bot',guilds}})}};
    if(name==='fetch-retry')return {default:(f)=>f};
    if(name==='./metricsSystem')return {loginsCounter:{inc(){}},loginErrorsCounter:{inc(){}}};
    throw new Error('Unexpected dependency '+name);
  }};
  vm.createContext(sandbox);vm.runInContext(compiled,sandbox);
  const system=new exports.Login(()=>{},10,'https://master.invalid',7777,'key',false);
  const ctx={svr:{getUserIp:()=>IP,getUserGuid:()=>'g1',isConnected:()=>true,sendCustomPacket(){},getActorsByProfileId:()=>[0xff000abc],get:()=>null},
    gm:{emit:(name)=>{if(name==='spawnAllowed')allowed();}}};
  await system.initAsync(ctx);
  system.customPacket(3,'loginWithSkympIo',{gameData:{session:'s'}},ctx);
  await spawned;
  return {posts,logged:logged.filter(l=>l.startsWith('Server Login'))};
}

let failed=0;
async function check(name,fn){
  try{await fn();console.log('PASS  '+name);}
  catch(e){failed++;console.log('FAIL  '+name+'\n      '+(e&&e.message));}
}
(async()=>{
  const {posts,logged}=await login([{guildId:'g',eventLogChannelId:'c'}],[]);
  await check('the login is posted to the event log channel once',async()=>{assert.equal(posts.length,1);});
  await check('the post names the slot, the actor, the profile and the Discord account',async()=>{
    assert.match(posts[0],/Server Slot 3/);
    assert.match(posts[0],/Actor ID ff000abc/);
    assert.match(posts[0],/Master API 5/);
    assert.match(posts[0],/<@111>/);
  });
  await check('the post carries no address',async()=>{
    assert.ok(!posts[0].includes(IP),posts[0]);
    assert.doesNotMatch(posts[0],/\bIP\b/);
  });
  await check('the same line in the server log carries no address',async()=>{
    assert.equal(logged.length,1);
    assert.ok(!logged[0].includes(IP),logged[0]);
  });
  const two=await login([{guildId:'g',eventLogChannelId:'c',hideIpRoleId:'r'}],['r']);
  await check('a guild that still names hideIpRoleId posts the same line, with no address',async()=>{
    assert.equal(two.posts.length,1);
    assert.ok(!two.posts[0].includes(IP));
    assert.doesNotMatch(two.posts[0],/\bIP\b/);
  });
  if(failed){console.log(failed+' failed');process.exit(1);}
})().catch(e=>{console.error(e);process.exit(1);});

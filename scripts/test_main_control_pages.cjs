// Offline tests only: no device requests or Wi-Fi changes.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const path=require('node:path'),root=path.resolve(__dirname,'..');
const html=fs.readFileSync(path.join(root,'web/main-control/index.html'),'utf8');
const js=html.match(/<script>([\s\S]*?)<\/script>/)[1];
const sw=fs.readFileSync(path.join(root,'web/main-control/sw.js'),'utf8');
const ids=[...html.matchAll(/\bid="([^"]+)"/g)].map(m=>m[1]);
assert.equal(new Set(ids).size,ids.length);
for(const marker of ['tab-status','tab-experiment','tab-sensors','tab-clock','tab-logs','sprStatus','gnssRtkMessage','magState','connectDevice','parentSetSave','parentSetStatus'])assert(ids.includes(marker));
assert(!/(?<![\w.])fetch\(/.test(js));
assert(!js.includes("tick();startPhone('time');"));
const wrapper=js.slice(js.indexOf('let deviceConnected='),js.indexOf("const el="));
const handler=js.slice(js.indexOf("el('connectDevice').onclick="),js.indexOf("if('serviceWorker' in navigator)"));
const nodes={connectDevice:{},message:{}},calls=[];let role='P_MAIN',shares=0;
const ctx={Error,Promise,AbortSignal,el:id=>nodes[id],setBusy:()=>{},startPhone:()=>{shares++},window:{fetch:async(url)=>{calls.push(url);return {ok:true,json:async()=>({role})}}}};
vm.createContext(ctx);vm.runInContext(wrapper+handler,ctx);
(async()=>{
 await assert.rejects(vm.runInContext("deviceFetch('/api/status')",ctx));assert.equal(calls.length,0);
 await nodes.connectDevice.onclick();assert.equal(shares,1);assert.equal(calls[0],'http://192.168.4.1/api/status');
 await vm.runInContext("deviceFetch('/api/spresense/stop',{method:'POST'})",ctx);assert.equal(calls.at(-1),'http://192.168.4.1/api/spresense/stop');
 await vm.runInContext("deviceFetch('/api/parent-calibration/save',{method:'POST'})",ctx);assert.equal(calls.at(-1),'http://192.168.4.1/api/parent-calibration/save');
 role='C_MAIN';await nodes.connectDevice.onclick();await assert.rejects(vm.runInContext("deviceFetch('/api/status')",ctx));assert.equal(shares,1);
 const events={},deleted=[],stored=[];let online=true;
 const cache={addAll:async()=>{},put:async(req)=>stored.push(req.url),match:async()=>({offline:true})};
 const sctx={URL,Promise,Error,Response,AbortSignal,self:{location:new URL('https://ry32767.github.io/P_MAIN_experiment/main-control/sw.js'),clients:{claim:async()=>{}},skipWaiting:async()=>{},addEventListener:(name,fn)=>events[name]=fn},caches:{open:async()=>cache,keys:async()=>['aquabeacon-main-control-v6','unrelated'],delete:async key=>deleted.push(key)},fetch:async()=>{if(!online)throw Error('offline');return {ok:true,clone(){return this},online:true}}};
 vm.createContext(sctx);vm.runInContext(sw,sctx);
 let waiting=[];events.activate({waitUntil:p=>waiting.push(p)});await Promise.all(waiting);assert.deepEqual(deleted,['aquabeacon-main-control-v6']);
 async function request(url){let result;waiting=[];events.fetch({request:{method:'GET',url},waitUntil:p=>waiting.push(p),respondWith:p=>result=p});const response=await result;await Promise.all(waiting);return response;}
 assert((await request('https://ry32767.github.io/P_MAIN_experiment/main-control/')).online);
 online=false;assert((await request('https://ry32767.github.io/P_MAIN_experiment/main-control/')).offline);
 assert.equal(await request('http://192.168.4.1/api/status'),undefined);
 console.log('PASS Pages: five tabs, request routing, connect/role guards, clock sharing after connect, fresh online/offline cache, scoped cleanup');
})().catch(e=>{console.error(e);process.exitCode=1});

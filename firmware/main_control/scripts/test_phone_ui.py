from pathlib import Path
import re,subprocess
r=Path(__file__).resolve().parents[2]
blocks=[]
node=r'C:\Program Files\nodejs\node.exe'
for path in [r/'AquaBeacon_MAIN_firmware/web/index.html',r/'P_MAIN_pages_publish/web/main-control/index.html']:
 s=path.read_text(encoding='utf-8');blocks.append(s.split('// PHONE_CLIENT_BEGIN')[1].split('// PHONE_CLIENT_END')[0])
 for script in re.findall(r'<script>(.*?)</script>',s,re.S):subprocess.run([node,'--check'],input=script,text=True,encoding='utf-8',check=True)
assert blocks[0]==blocks[1]
test=blocks[0]+"""
const a=require('node:assert/strict');
a.deepEqual(phoneClockSample(1800000000000,1800000000100,100),{epoch:1800000000050,rtt:100});
a.throws(()=>phoneClockSample(1000,3000,100));a.throws(()=>phoneClockSample(1000,3000,2000));
const p={coords:{latitude:0,longitude:0,accuracy:12.3},timestamp:10000};
a.deepEqual(phonePositionValues(p,11000),[0,0,12300,1000]);
a.throws(()=>phonePositionValues(p,70000));a.throws(()=>phonePositionValues(p,9000));
a.throws(()=>phonePositionValues({...p,coords:{latitude:91,longitude:0,accuracy:1}},11000));
console.log('PASS phone clock midpoint, clock jumps, slow link, coordinate scaling and stale fixes');
"""
subprocess.run([node],input=test,text=True,encoding='utf-8',check=True)

client=(r/'AquaBeacon_MAIN_firmware/web/index.html').read_text(encoding='utf-8').split('// PHONE_CLIENT_BEGIN')[1].split('async function tick(){')[0]
flow=r"""
const vm=require('node:vm'),assert=require('node:assert/strict');
const source=SOURCE;
async function run(geo){
 const calls=[],elements={};let busy=false;
 const ctx={console,Date,Number,Math,Error,Promise,performance:{now:()=>Date.now()},document:{hidden:false},window:{isSecureContext:true},navigator:{geolocation:{getCurrentPosition:geo}},setTimeout:()=>1,clearTimeout:()=>{},el:id=>elements[id]??=( {} ),post:async path=>{calls.push(path);return {session:42,token:1}},setBusy:v=>{ctx.busy=v},busy};
 vm.createContext(ctx);vm.runInContext(source,ctx);
 await vm.runInContext("startPhone('position');phoneWork",ctx);
 return {ctx,calls,elements};
}
(async()=>{
 let r=await run(ok=>ok({coords:{latitude:0,longitude:0,accuracy:10},timestamp:Date.now()}));
 assert(r.calls.some(x=>x.startsWith('/api/phone/time/42/1/')));assert(r.calls.some(x=>x.startsWith('/api/phone/position/42/0/0/10000/')));
 await r.elements.phoneStop.onclick();assert.equal(r.calls.at(-1),'/api/phone/clear');
 r=await run((ok,fail)=>fail({code:1}));assert(r.calls.some(x=>x.startsWith('/api/phone/time/')));assert(!r.calls.some(x=>x.startsWith('/api/phone/position/')));assert.equal(vm.runInContext('phoneMode',r.ctx),'time');
 console.log('PASS phone sharing flow, permission denial time fallback and stop clearing');
})().catch(e=>{console.error(e);process.exitCode=1});
"""
import json
subprocess.run([node],input=flow.replace('SOURCE',json.dumps(client)),text=True,encoding='utf-8',check=True)

"""Exercise production body workflow and warning before any experiment mutation."""
from pathlib import Path
import re,json,subprocess
r=Path(__file__).resolve().parents[1];html=(r/'web/index.html').read_text(encoding='utf-8')
block=html.split('// BODY_CALIBRATION_BEGIN')[1].split('// BODY_CALIBRATION_END')[0]
command=re.search(r'^const cmd=async.*$',html,re.M).group()
test=r'''
const vm=require('node:vm'),assert=require('node:assert/strict');
const elements={},timers=[],calls=[];let ready=false,cameraReady=false;
const el=id=>elements[id]??=({value:id==='bodyTarget'?'p':'1',files:[],querySelector(){return {}},replaceChildren(...v){this.children=v},showModal(){this.open=true},close(){this.open=false}});
const ctx={console,el,ArrayBuffer,DataView,Uint8Array,Date,JSON,Number,Math,TextDecoder,busy:false,bodyLastPoll:0,overviewChildConnected:true,selectedTab:'sensors',
 document:{createElement:()=>({})},clearTimeout:()=>{},setTimeout:f=>{timers.push(f);return timers.length},
 text:r=>JSON.stringify(r.data),post:async path=>{calls.push(path);return {data:{a:0,m:31,p:0,c:1,g:0,v:0,t:0,r:ready?1:0,b:1,k:cameraReady?1:0,o:31,q:0,s:0,d:0}}},
 selectTab:k=>{calls.push('tab:'+k)},stopBoth:async()=>calls.push('stop'),both:async(op,arg)=>calls.push(['both',op,arg]),setBusy:v=>{ctx.busy=v}};
vm.createContext(ctx);vm.runInContext(COMMAND,ctx);vm.runInContext(SOURCE,ctx);
const tick=()=>new Promise(resolve=>setImmediate(resolve));
(async()=>{
 let p=vm.runInContext('startExperiment(true)',ctx);await tick();assert(el('calibrationWarning').open);assert(!calls.includes('stop'));el('calibrationWarningBack').onclick();await assert.rejects(p);assert(!calls.includes('stop'));
 calls.length=0;p=vm.runInContext('startExperiment(true)',ctx);await tick();el('calibrationWarningContinue').onclick();await p;assert(calls.includes('stop'));assert.deepEqual(calls.filter(Array.isArray).at(-1),['both',3,0x43414c21]);
 calls.length=0;ready=true;await vm.runInContext('startExperiment(false)',ctx);assert(!el('calibrationWarning').open);assert.deepEqual(calls.filter(Array.isArray).at(-1),['both',3,0]);
 vm.runInContext("renderBody({a:1,m:3,p:2,c:1,g:0,v:0,t:0,r:0,b:1},'p')",ctx);assert(el('bodyStart').disabled);assert(!el('bodyPhaseEnd').disabled);assert(el('bodyFinish').disabled);assert.match(el('bodyState').textContent,/解析前/);
 timers.at(-1)();assert.match(el('bodyState').textContent,/状態不明/);assert(el('bodyStart').disabled);
 el('bodyTarget').value='c';ctx.overviewChildConnected=false;el('bodyTarget').onchange();assert(el('bodyStart').disabled);el('bodyTarget').value='p';
 calls.length=0;el('bodySolution').files=[{size:100,text:async()=>JSON.stringify({format:'AquaBeaconBodySolution1',capture:1,boot:1,imu:1,mag:1,axes:3,camera:0,imu_q:[2,0,0,0],mag_q:[1,0,0,0],camera_scale:1,camera_offset_us:0})}];await el('bodyImport').onclick();assert.equal(calls.length,0);assert.match(el('bodyMessage').textContent,/軸変換が不正/);
 calls.length=0;el('bodySolution').files=[{size:100,text:async()=>JSON.stringify({format:'AquaBeaconBodySolution1',capture:1,boot:1,imu:1,mag:1,axes:3,camera:1,imu_q:[1,0,0,0],mag_q:[1,0,0,0],camera_scale:1,camera_offset_us:1792886400000000})}];await el('bodyImport').onclick();assert(calls.some(p=>p.includes('/api/control/p/21/')));
 calls.length=0;ready=true;await assert.rejects(vm.runInContext('startRangeChecked()',ctx),/省略不可/);assert(!calls.some(p=>p.includes('/range/')));cameraReady=true;calls.length=0;ready=false;p=vm.runInContext('startRangeChecked()',ctx);await tick();el('calibrationWarning').oncancel({preventDefault(){}});await assert.rejects(p);assert(!calls.some(p=>p.includes('/range/')));
 vm.runInContext("renderBody({a:1,m:0,p:5,c:2,g:1,v:3,r:0,o:16,s:12,d:1,q:1},'p')",ctx);assert.match(el('bodyMotionGuide').textContent,/②/);assert(el('bodyPhaseEnd').disabled);vm.runInContext("renderBody({a:1,m:0,p:5,c:2,g:1,v:3,r:0,o:16,s:31,d:3,q:2},'p')",ctx);assert(!el('bodyPhaseEnd').disabled);vm.runInContext("renderBody({a:1,m:16,p:0,c:2,g:1,v:3,r:0,o:16,s:31,d:3,q:2},'p')",ctx);assert(!el('bodyFinish').disabled);
 console.log('PASS actual body UI: preflight before stop/create, cancel/Esc, explicit override, ready path, phase controls, expiry and invalid solution');
})().catch(e=>{console.error(e);process.exitCode=1});
'''
subprocess.run(['node'],input=test.replace('COMMAND',json.dumps(command)).replace('SOURCE',json.dumps(block)),text=True,encoding='utf-8',check=True)

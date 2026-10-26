"""Offline state tests only: no devices, network requests, or Wi-Fi changes."""
from pathlib import Path
import subprocess,re
r=Path(__file__).resolve().parents[1]
html=(r/'web/index.html').read_text(encoding='utf-8')
js=re.search(r'<script>(.*?)</script>',html,re.S).group(1)
subprocess.run(['node','--check'],input=js,text=True,encoding='utf-8',check=True)
marker='let sprExpiry,sprRequested=0,sprAllowDisable=false;'
block=js.split(marker)[1].split('async function tick(){')[0]
source=marker+block
test=r'''
const assert=require('node:assert/strict'),vm=require('node:vm');
const elements={},buttons=['stop','start','reset','calibration/start','calibration/save','calibration/end','calibration/clear','calibration/pose/0'].map(op=>({dataset:{spr:op},disabled:true}));
let expired,posted=[],bodies=[],selected=[];
const ctx={String,console,el:id=>elements[id]??=({replaceChildren(...v){this.children=v},scrollIntoView(){}}),
 document:{querySelectorAll:()=>buttons,createElement:()=>({})},clearTimeout:()=>{},setTimeout:f=>{expired=f;return 1},
 selectTab:k=>selected.push(k),post:async(p,body)=>{posted.push(p);bodies.push(body);return {command_id:9}}};
vm.createContext(ctx);vm.runInContext(SOURCE,ctx);
const render=s=>{ctx.s=s;vm.runInContext('renderSprLink(s)',ctx)};
(async()=>{
 render(null);assert(buttons.every(b=>b.disabled));assert.match(elements.sprStatus.textContent,/不明/);
 let s={link_fresh:true,recording:true,time_valid:true,last_operation:0,last_result:0,command_pending:false};
 render(s);assert(!buttons[0].disabled);assert(buttons[2].disabled);
 await buttons[0].onclick();assert.equal(posted.at(-1),'/api/spresense/stop');assert.match(elements.sprMessage.textContent,/応答を待/);
 render({...s,command_id:9,command_result:5});assert.match(elements.sprMessage.textContent,/受付/);
 render({...s,recording:false,last_operation:2,last_result:5});assert(buttons[2].disabled);
 render({...s,recording:false,last_operation:2,last_result:0,command_id:9,command_result:5});assert(!buttons[2].disabled);
 render({...s,recording:false,calibration:1,calibration_mask:0,calibration_pose:255});assert(buttons[3].disabled);assert(buttons[4].disabled);assert(!buttons[5].disabled);assert(!buttons[7].disabled);

 render({...s,imu_ok:true,recording:false,calibration:3,calibration_mask:3,calibration_pose:2,calibration_progress:0,calibration_diagnostics:true,calibration_rejection:16|32,calibration_rejections:19});
 assert.match(elements.sprCalibrationAdvice.textContent,/上向きでない/);assert.match(elements.sprCalibrationAdvice.textContent,/振動/);assert(elements.sprCalibrationChecks.children.some(e=>e.textContent==='やり直し中'));
 render({...s,imu_ok:true,recording:false,calibration:2,calibration_mask:3,calibration_pose:2,calibration_progress:42});assert.equal(elements.sprCalibrationProgress.value,42);assert.equal(elements.sprCalibrationProgress.hidden,false);
 render({...s,imu_ok:true,recording:false,calibration:3,calibration_mask:3,calibration_pose:2});assert.match(elements.sprCalibrationAdvice.textContent,/詳細診断は未取得/);
 render({...s,recording:false,calibration:4,calibration_mask:63,calibration_pose:255});assert(!buttons[4].disabled);assert(buttons[7].disabled);assert(buttons[1].disabled);
 assert.match(elements.sprCalibrationAdvice.textContent,/全6姿勢/);
 render({...s,imu_ok:false,recording:false,calibration:2,calibration_progress:80,calibration_pose:0});assert(elements.sprCalibrationProgress.hidden);assert.match(elements.sprCalibrationAdvice.textContent,/最新取得を確認できません/);

 render({...s,recording:false,calibration:5,calibration_applied:true,calibration_generation:3});assert(buttons[6].disabled);assert(!buttons[1].disabled);assert(!buttons[5].disabled);assert(!elements.sprNext.disabled);
 assert(elements.sprCalibrationChecks.children.some(x=>x.textContent==='保存済み補正を使用'));
 let before=posted.length;await buttons[6].onclick();assert.equal(posted.length,before);assert.match(elements.sprMessage.textContent,/確認/);
 elements.sprDisableConfirm.checked=true;elements.sprDisableConfirm.onchange();assert(!buttons[6].disabled);await buttons[6].onclick();assert.equal(posted.at(-1),'/api/spresense/calibration/clear');assert.equal(bodies.at(-1),'confirm_disable=1');assert.equal(elements.sprDisableConfirm.checked,false);
 render({...s,recording:false,calibration:5,calibration_applied:true,calibration_generation:3});before=posted.length;elements.sprNext.onclick();assert.equal(posted.length,before);assert.equal(selected.at(-1),'sensors');assert.equal(elements.magPanel.open,true);assert.equal(elements.sprCalibrationPanel.open,false);
 render({...s,sd_ok:true,sd_diagnostics:true,sd_state:'RECORDING',sd_rows:120,sd_synced_rows:100,sd_errors:0,sd_failure:'NONE',sd_detail:0,sd_attempts:0,sd_recoveries:0});
 assert.match(elements.sprOverview.textContent,/記録中/);assert(elements.sprOverviewDetails.children.some(x=>x.textContent===100));
 render({...s,recording:false,sd_ok:false,sd_diagnostics:true,sd_state:'ERROR',sd_failure:'MOUNT',sd_detail:2});
 assert.match(elements.sprOverview.textContent,/異常/);assert(elements.sprDetails.children.some(x=>String(x.textContent).includes('SD未認識')));
 render({...s,command_id:9,command_result:6});assert.match(elements.sprMessage.textContent,/実行結果不明/);
 await buttons[1].onclick();assert.equal(posted.at(-1),'/api/spresense/start');
 expired();assert.match(elements.sprCalibrationAdvice.textContent,/判定不明/);assert.equal(elements.sprCalibrationChecks.children.length,0);assert(elements.sprCalibrationProgress.hidden);assert.match(elements.sprOverview.textContent,/不明/);assert(buttons.every(b=>b.disabled));assert.match(elements.sprStatus.textContent,/不明/);
 console.log('PASS Spresense offline UI: stale disabling, stop completion before reset, acceptance versus execution, uncertain timeout');
})().catch(e=>{console.error(e);process.exit(1)});
'''.replace('SOURCE',repr(source))
subprocess.run(['node'],input=test,text=True,encoding='utf-8',check=True)

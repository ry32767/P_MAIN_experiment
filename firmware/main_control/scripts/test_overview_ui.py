"""Offline console overview and SD controls; no hardware or Wi-Fi access."""
from pathlib import Path
from html.parser import HTMLParser
import re,json,subprocess
r=Path(__file__).resolve().parents[1]
html=(r/'web/index.html').read_text(encoding='utf-8')
class Structure(HTMLParser):
 def __init__(self):super().__init__();self.details={};self.ids=set()
 def handle_starttag(self,tag,attrs):
  a=dict(attrs)
  if 'id' in a:self.ids.add(a['id'])
  if tag=='details' and 'id' in a:self.details[a['id']]=a
p=Structure();p.feed(html)
for key in ('parentCard','sprCard','childCard','parentSetPanel','sprCalibrationPanel','magPanel','bnoPanel','rtkPanel','utcPanel','gnssTimePanel'):
 assert 'open' not in p.details[key],key
assert 'id="ps_' not in html
for key in ('gnssPpkStatus','gnssNetwork','gnssRtkStatus','gnssStationDetails','phoneStatus','utcStatus','spresense','sprOverviewDetails','parent','child'):
 assert key in p.ids,key
block=html.split('// BATTERY_ESTIMATE_BEGIN')[1].split('// BATTERY_ESTIMATE_END')[0]+'\n'+html.split('// DEVICE_OVERVIEW_BEGIN')[1].split('// DEVICE_OVERVIEW_END')[0]
test=r'''
const assert=require('node:assert/strict'),vm=require('node:vm'),elements={};let expiry;
const controls=['recoverParentSd','stopParentSd','recoverChildSd','stopChildSd','calStart'].map(action=>({dataset:{action,requiresChild:action.includes('Child')||action==='calStart'?'1':'0'},disabled:true}));
const el=id=>elements[id]??=({className:'',textContent:'',open:false});
const ctx={el,console,busy:false,clearTimeout:()=>{},setTimeout:f=>{expiry=f;return 1},document:{querySelectorAll:()=>controls}};
vm.createContext(ctx);vm.runInContext(SOURCE,ctx);
vm.runInContext('renderDeviceOverview(null)',ctx);for(const key of ['parent','spr','child']){assert(el(key+'Card').className.includes('attention'));assert(el(key+'SdCard').className.includes('attention'));}
const h={flags:275,bus_v:12,sd_errors:0,dropped:0,imu_gaps:0};
let sample={local:{...h},remote:{...h,flags:23},link_age_ms:999999,sd_reason:'NONE',sd_recovery:{enabled:true},spresense:{link_fresh:true,recording:true,sd_ok:true,imu_ok:true,sd_state:'RECORDING',sd_errors:0,imu_errors:0,imu_gaps:0},gnss:{rtk_solution:'float',latitude:35,longitude:139},utc:{valid:true,source:'gps_pps_candidate',rtc_valid:true},ppk:{recording:true},rtk:{connected:true,mount:'BASE'}};
const render=()=>{ctx.sample=sample;vm.runInContext('renderDeviceOverview(sample)',ctx)};
render();assert.equal(el('parentState').textContent,'応答あり');assert.equal(el('sprStorage').textContent,'SD：保存中');assert.match(el('childState').textContent,/未接続/);assert(el('childCard').className.includes('attention'));assert(el('childSdCard').className.includes('attention'));assert(controls[2].disabled&&controls[3].disabled&&controls[4].disabled);assert(!controls[0].disabled);
el('parentCard').open=true;for(let i=0;i<20;i++)render();assert.equal(el('parentCard').open,true);
sample.local.flags&=~2;sample.sd_reason='STOPPED';render();assert.match(el('parentStorage').textContent,/停止中/);assert(!el('parentCard').className.includes('attention'));
sample.sd_reason='WRITE';sample.local.sd_errors=2;render();assert(el('parentCard').className.includes('attention'));assert(el('parentSdCard').className.includes('attention'));assert.match(el('parentIssue').textContent,/SDエラー累計 2/);
sample.local={...h,bus_v:10.5};sample.sd_reason='NONE';render();assert.match(el('parentIssue').textContent,/低電圧/);
sample.spresense={...sample.spresense,recording:false,sd_ok:false,sd_state:'ERROR',sd_errors:1};render();assert(el('sprCard').className.includes('attention'));assert.match(el('sprIssue').textContent,/SD 異常/);
sample.spresense={...sample.spresense,imu_errors:3,imu_gaps:4,crc_errors:5,calibration_restore_error:1};render();assert.match(el('sprIssue').textContent,/他\d+件/);assert.match(el('sprIssueDetails').textContent,/IMUエラー累計 3/);assert.match(el('sprIssueDetails').textContent,/校正エラー/);
sample.local={...h};sample.link_age_ms=0;render();assert(!controls[2].disabled);assert.equal(el('childState').textContent,'応答あり');assert(!el('childCard').className.includes('attention'));sample.remote.flags|=8;render();assert.match(el('childIssue').textContent,/漏水検知/);
sample.link_age_ms=3000;render();assert(controls[2].disabled);assert.equal(el('childState').textContent,'通信異常');assert(el('childCard').className.includes('attention'));
assert.match(el('clockOverview').textContent,/RTK Float/);assert.match(el('clockOverview').textContent,/GPS PPS/);assert.match(el('clockOverview').textContent,/35.000000/);assert.match(el('rtkCompact').textContent,/BASE/);
ctx.busy=true;sample.link_age_ms=0;render();assert(controls.every(b=>b.disabled));ctx.busy=false;
expiry();assert.match(el('parentState').textContent,/通信異常/);assert.match(el('clockOverview').textContent,/不明/);assert(controls.every(b=>b.disabled));
console.log('PASS device overview: disconnected child, intentional stop, low battery, faults, stale expiry, disabled controls and preserved disclosure');
'''
subprocess.run(['node'],input=test.replace('SOURCE',json.dumps(block)),text=True,encoding='utf-8',check=True)
actions=re.search(r'const actions=(\{.*?\});\n',html,re.S).group(1)
dispatch=r'''
const assert=require('node:assert/strict'),calls=[];
const cmd=async(t,op)=>calls.push([t,op]);
const startRangeChecked=()=>{},startCalibration=()=>{},calibrationCommand=()=>{},applyAcquisition=()=>{},stopBoth=()=>{},both=()=>{},post=()=>{},gain=()=>{},fileList=()=>{};
const actions=SOURCE;
(async()=>{for(const key of ['recoverParentSd','stopParentSd','recoverChildSd','stopChildSd'])await actions[key]();assert.deepEqual(calls,[['p',3],['p',2],['c',3],['c',2]]);console.log('PASS actual independent parent/child SD action dispatch');})().catch(e=>{console.error(e);process.exitCode=1});
'''
subprocess.run(['node'],input=dispatch.replace('SOURCE',actions),text=True,encoding='utf-8',check=True)
print('PASS collapsed panels, retained diagnostics and removed position inputs')

"""Exercise the compact parent calibration UI without hardware."""
from pathlib import Path
import subprocess,json
r=Path(__file__).resolve().parents[1]
html=(r/'web/index.html').read_text(encoding='utf-8')
assert 'data-parent-field' not in html and 'id="ps_' not in html
block=html.split('// PARENT_SET_BEGIN')[1].split('// PARENT_SET_END')[0]
test=r'''
const vm=require('node:vm'),assert=require('node:assert/strict');
const elements={},calls=[];let expiry;
const el=id=>elements[id]??=({disabled:false,textContent:'',className:''});
let sample={loaded:true,generation:3,calibrations_match:true,can_save:true,imu_calibration:7,mag_calibration:8};
const ctx={console,el,URLSearchParams,AbortSignal,clearTimeout:()=>{},setTimeout:f=>{expiry=f;return 1},
 fetch:async()=>({ok:true,json:async()=>sample}),post:async(path,body)=>{calls.push([path,Object.fromEntries(new URLSearchParams(body))]);return {message:'saved'}}};
vm.createContext(ctx);vm.runInContext(SOURCE,ctx);
(async()=>{
 ctx.sample=sample;vm.runInContext('renderParentSet(sample)',ctx);assert(!el('parentSetSave').disabled);assert.match(el('parentSetCompact').textContent,/保存番号 3/);assert.doesNotMatch(el('parentSetStatus').textContent,/CAD|配置/);
 expiry();assert(el('parentSetSave').disabled);assert.match(el('parentSetStatus').textContent,/不明/);
 await el('parentSetLoad').onclick();assert(!el('parentSetSave').disabled);
 await el('parentSetSave').onclick();assert.deepEqual(calls,[['/api/parent-calibration/save',{reference_only:'1'}]]);assert(!Object.keys(elements).some(k=>k.startsWith('ps_')));
 ctx.post=async()=>{throw Error('SD失敗')};await el('parentSetSave').onclick();assert.match(el('parentSetMessage').textContent,/SD失敗/);assert.equal(el('parentSetMessage').className,'bad');
 ctx.fetch=async()=>({ok:false});await el('parentSetLoad').onclick();assert(el('parentSetSave').disabled);
 console.log('PASS compact parent-set UI: no geometry inputs/payload, calibration reference save, refresh, failure and expiry');
})().catch(e=>{console.error(e);process.exitCode=1});
'''
subprocess.run(['node'],input=test.replace('SOURCE',json.dumps(block)),text=True,encoding='utf-8',check=True)

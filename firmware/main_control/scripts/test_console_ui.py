"""Offline console tests. No device access, Wi-Fi changes or repository publication."""
from pathlib import Path
import re, subprocess, json
from html.parser import HTMLParser
r=Path(__file__).resolve().parents[1]
html=(r/'web/index.html').read_text(encoding='utf-8')
js=re.search(r'<script>(.*?)</script>',html,re.S).group(1)
subprocess.run(['node','--check'],input=js,text=True,encoding='utf-8',check=True)
class Structure(HTMLParser):
    def __init__(self): super().__init__();self.ids=[];self.tabs=[];self.panels=[];self.inputs=[];self.labels=[]
    def handle_starttag(self,tag,attrs):
        a=dict(attrs)
        if 'id' in a:self.ids.append(a['id'])
        if a.get('role')=='tab':self.tabs.append(a)
        if a.get('role')=='tabpanel':self.panels.append(a)
        if tag in ('input','select','textarea'):self.inputs.append(a)
        if tag=='label':self.labels.append(a.get('for'))
p=Structure();p.feed(html)
assert len(p.ids)==len(set(p.ids)), 'duplicate element IDs'
assert len(p.tabs)==len(p.panels)==5
assert sum(x['aria-selected']=='true' for x in p.tabs)==1
assert sum('hidden' not in x for x in p.panels)==1
for tab,panel in zip(p.tabs,p.panels):
    assert tab['aria-controls']==panel['id'] and panel['aria-labelledby']==tab['id']
for a in p.inputs:assert a['id'] in p.labels
blocks='\n'.join(js.split('// '+name+'_BEGIN')[1].split('// '+name+'_END')[0] for name in ['CONSOLE_NAV','BNO_CAL_CLIENT'])
test=r'''
const vm=require('node:vm'),assert=require('node:assert/strict');
const elements={},calls=[],timers=[],tabs=['status','experiment','sensors','clock','logs'].map(key=>({id:'tab-'+key,setAttribute(k,v){this[k]=v},focus(){this.focused=true}}));
const panels=tabs.map(t=>({id:t.id.replace('tab-','panel-')}));
const ctx={console,JSON,String,document:{querySelectorAll:s=>s==='[role="tab"]'?tabs:panels,createElement:()=>({})},el:id=>elements[id]??=({replaceChildren(...v){this.children=v}}),clearTimeout:()=>{},setTimeout:f=>{timers.push(f);return timers.length},busy:false,setBusy:v=>{ctx.busy=v},text:r=>r.data,cmd:async(t,op)=>{calls.push([t,op]);return {data:JSON.stringify({active:1,saved:0,a:2,g:3,m:2,ready:0,err:0})}},rangeStop:async()=>calls.push(['rangeStop'])};
vm.createContext(ctx);vm.runInContext(SOURCE,ctx);
(async()=>{
 vm.runInContext("selectTab('sensors',true)",ctx);assert.equal(tabs[2]['aria-selected'],'true');assert.equal(panels.filter(p=>!p.hidden).length,1);assert(tabs[2].focused);
 tabs[2].onkeydown({key:'End',preventDefault(){}});assert.equal(tabs[4]['aria-selected'],'true');
 tabs[4].onkeydown({key:'ArrowRight',preventDefault(){}});assert.equal(tabs[0]['aria-selected'],'true');
 await vm.runInContext('startCalibration()',ctx);assert.deepEqual(calls,[['rangeStop'],['c',2],['c',13]]);
 assert.match(elements.calState.textContent,/校正中/);assert.equal(elements.calAccuracy.children.length,6);
 assert.match(elements.calAdvice.textContent,/5秒/);
 ctx.probe={active:1,saved:0,a:1,g:0,m:-1,ready:0,err:0};vm.runInContext('renderCalibration(probe)',ctx);assert.match(elements.calAdvice.textContent,/姿勢を変えて静止/);assert.match(elements.calAdvice.textContent,/台の上で静止/);assert.match(elements.calAdvice.textContent,/更新未確認/);
 ctx.probe={active:1,saved:0,a:2,g:3,m:2,ready:1,err:0};vm.runInContext('renderCalibration(probe)',ctx);assert.match(elements.calAdvice.textContent,/継続条件を通過/);

 timers.at(-1)();assert.match(elements.calState.textContent,/不明/);assert.equal(elements.calAccuracy.children.length,0);
 calls.length=0;ctx.cmd=async()=>{throw Error('timeout')};await assert.rejects(vm.runInContext('calibrationCommand(15)',ctx));assert.match(elements.calState.textContent,/不明/);
 await vm.runInContext('pollCalibration(false)',ctx);assert.equal(ctx.busy,false);
 ctx.cmd=async(t,op)=>{calls.push([t,op]);throw Error('stop failed')};await assert.rejects(vm.runInContext('startCalibration()',ctx));assert.deepEqual(calls,[['rangeStop'],['c',2]]);
 console.log('PASS tab keyboard navigation, stop-before-calibrate, stop failure, accuracy rendering, expiration and communication failure');
})().catch(e=>{console.error(e);process.exitCode=1});
'''
subprocess.run(['node'],input=test.replace('SOURCE',json.dumps(blocks)),text=True,encoding='utf-8',check=True)
mag_block=js.split('// MAG_CALIBRATION_BEGIN')[1].split('// MAG_CALIBRATION_END')[0]
mag_test=r'''
const vm=require('node:vm'),assert=require('node:assert/strict'),elements={},calls=[];
const sample={present:true,fresh:true,active:true,ready:false,saved:false,enabled:false,saturated:false,error:'',samples:200,elapsed_s:20,octants:Array(8).fill(10),span_ut:[80,90,100],radius_cv:.03,saturated_samples:0,rejected:0,raw_ut:[20,30,40],corrected_ut:[20,30,40],offset_ut:[0,0,0],scale:[1,1,1],file:''};
let magExpired;const ctx={console,AbortSignal,clearTimeout:()=>{},setTimeout:f=>{magExpired=f;return 1},document:{createElement:()=>({})},el:id=>elements[id]??=({replaceChildren(...v){this.children=v}}),fetch:async(path,opt)=>{calls.push([path,opt]);return {ok:true,json:async()=>({ok:true,calibration:sample})}}};
vm.createContext(ctx);vm.runInContext(SOURCE,ctx);ctx.sample=sample;
(async()=>{
 vm.runInContext('renderMag(sample)',ctx);assert(elements.magSave.disabled);assert(elements.magStart.disabled);assert(elements.magReset.disabled);assert.equal(elements.magProgress.children.length,20);

 Object.assign(sample,{samples:10,elapsed_s:2,octants:Array(8).fill(0),span_ut:[2,3,4],radius_cv:0,fit_evaluated:false,radius_match:false});
 vm.runInContext('renderMag(sample)',ctx);assert.match(elements.magAdvice.textContent,/測定時間/);assert.match(elements.magAdvice.textContent,/サンプル数/);assert(elements.magChecks.children.some(e=>String(e.textContent).includes('変化幅が揃ってから評価')));
 Object.assign(sample,{samples:200,elapsed_s:20,octants:Array(8).fill(10),span_ut:[80,90,100],radius_cv:.03,fit_evaluated:true,radius_match:true});
 sample.saturated_samples=1;vm.runInContext('renderMag(sample)',ctx);assert.match(elements.magAdvice.textContent,/終了・再開始/);sample.saturated_samples=0;
 sample.ready=true;vm.runInContext('renderMag(sample)',ctx);assert.equal(elements.magSave.disabled,false);
 assert.match(elements.magAdvice.textContent,/保存条件を通過/);
 magExpired();assert.match(elements.magAdvice.textContent,/判定不明/);assert.equal(elements.magChecks.children.length,0);

 sample.saved=true;vm.runInContext('renderMag(sample)',ctx);assert(elements.magSave.disabled);assert.match(elements.magState.textContent,/保存・適用済み/);
 sample.active=false;sample.loaded=true;sample.enabled=true;sample.saved=false;vm.runInContext('renderMag(sample)',ctx);assert.match(elements.magCompact.textContent,/省略可/);assert.match(elements.magAdvice.textContent,/省略できます/);
 vm.runInContext('renderMag(null)',ctx);assert(elements.magSave.disabled);assert.equal(elements.magProgress.children.length,0);assert.match(elements.magState.textContent,/通信途絶/);
 await elements.magStart.onclick();assert.equal(calls[0][0],'/api/mag/start');assert.equal(calls[0][1].method,'POST');assert.equal(calls[0][1].headers['X-Aqua-Control'],'1');
 ctx.fetch=async()=>({ok:false,json:async()=>({ok:false,calibration:{...sample,error:'回転不足'}})});await elements.magSave.onclick();assert(elements.magSave.disabled);assert.equal(elements.magState.textContent,'回転不足');
 ctx.fetch=async()=>{throw Error('通信失敗')};await elements.magEnd.onclick();assert(elements.magSave.disabled);assert.equal(elements.magState.textContent,'通信失敗');
 console.log('PASS parent magnetic console readiness, saved state, communication loss, POST controls and failed save');
})().catch(e=>{console.error(e);process.exitCode=1});
'''
subprocess.run(['node'],input=mag_test.replace('SOURCE',json.dumps(mag_block)),text=True,encoding='utf-8',check=True)
def luminance(h):
    v=[int(h[i:i+2],16)/255 for i in (1,3,5)]
    v=[x/12.92 if x<=.04045 else ((x+.055)/1.055)**2.4 for x in v]
    return sum(x*y for x,y in zip(v,(.2126,.7152,.0722)))
colors=dict(re.findall(r'--([\w-]+):(#[0-9a-f]{6})',html))
for fg,bg in [('ink','panel'),('ink','paper'),('muted','panel'),('muted','paper'),('accent','panel'),('danger','panel'),('good','panel'),('ink','danger-surface'),('muted','danger-surface'),('danger','danger-surface')]:
    a,b=sorted([luminance(colors[fg]),luminance(colors[bg])]);ratio=(b+.05)/(a+.05)
    assert ratio>=4.5,(fg,bg,ratio)
    print(f'Contrast {fg}/{bg}: {ratio:.2f}:1')
print('PASS unique IDs, five paired tabs, form labels, syntax and text contrast')

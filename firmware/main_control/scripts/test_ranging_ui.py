"""Offline JS syntax and ranging display checks. Never opens a device or changes Wi-Fi."""
from pathlib import Path
import re, subprocess
r=Path(__file__).resolve().parents[1]
html=(r/'web/index.html').read_text(encoding='utf-8')
js=re.search(r'<script>(.*?)</script>',html,re.S).group(1)
out=r/'reports/raw/ranging_web.js';out.write_text(js,encoding='utf-8')
subprocess.run(['node','--check',str(out)],check=True)
expr=re.search(r"el\('range'\).textContent=(.*?);el\('timing'\)",js).group(1)
test="const assert=require('node:assert/strict');const fmt=(v,u)=>v===null?'—':v.toFixed(4)+u;const display=s=>"+expr+";\n"
test+="assert.match(display({ranging:true,range_state:2,range_m:0.03,sync_ok:false}),/0.0300 m/);\n"
test+="assert.match(display({ranging:true,range_state:2,range_m:0}),/0.0000 m/);\n"
test+="assert.match(display({ranging:true,range_state:4,range_m:null}),/取得異常/);\n"
test+="assert.equal(display({ranging:false,range_state:3,range_m:1}),'測距停止中');console.log('PASS offline Web syntax, close/zero range, acquisition error and stopped display');\n"
subprocess.run(['node'],input=test,text=True,encoding='utf-8',check=True)

apply=re.search(r'async function applyAcquisition\(\)\{.*?\n\}',js,re.S).group(0)
render=re.search(r'function renderAcquisition\(s\)\{.*?\n\}',js,re.S).group(0)
controls=re.search(r'<select id="frequency">(.*?)</select>',html,re.S).group(1)
assert re.findall(r'value="(\d+)"',controls)==['10000','50000','100000','200000']
test="const assert=require('node:assert/strict');const elements={rangingRate:{value:'4'},acousticPattern:{value:'1'},frequency:{value:'10000'},observationSeconds:{value:'3'},observationEvery:{value:'30'},acquisitionStatus:{},observationStatus:{}};const el=id=>elements[id];let calls=[],acquisitionLoaded=false;const rangeStop=async()=>calls.push('stop');const post=async p=>calls.push(p);\n"+apply+'\n'+render+"\n"
test+="(async()=>{for(const hz of [10000,50000,100000,200000]){calls=[];elements.frequency.value=String(hz);await applyAcquisition();assert.deepEqual(calls,['stop','/api/range/config/'+hz+'/3/30/4/1']);}for(const rate of ['0','5','1.5']){elements.rangingRate.value=rate;await assert.rejects(applyAcquisition());}elements.rangingRate.value='4';for(const pattern of ['-1','2']){elements.acousticPattern.value=pattern;await assert.rejects(applyAcquisition());}elements.acousticPattern.value='1';calls=[];elements.frequency.value='20001';await assert.rejects(applyAcquisition());assert.deepEqual(calls,[]);renderAcquisition({frequency_hz:50000,observation_seconds:3,observation_every:30,log_format:'AQBIN001',link_age_ms:0,remote:{observation_active:true,observation_samples:99,observation_lost:7}});assert.match(elements.observationStatus.textContent,/欠損 7/);renderAcquisition(null);assert.match(elements.observationStatus.textContent,/不明/);console.log('PASS Web frequency choices, stop-before-config, invalid settings and observation loss/stale display');})().catch(e=>{console.error(e);process.exit(1)});\n"
subprocess.run(['node'],input=test,text=True,encoding='utf-8',check=True)

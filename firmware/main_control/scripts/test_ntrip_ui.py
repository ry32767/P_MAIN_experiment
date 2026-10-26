"""Offline RTK form checks. No device access or Wi-Fi changes."""
from pathlib import Path
import re, subprocess
root=Path(__file__).resolve().parents[1]
html=(root/'web/index.html').read_text(encoding='utf-8')
for script in re.findall(r'<script>(.*?)</script>',html,re.S):
    subprocess.run(['node','--check'],input=script,text=True,encoding='utf-8',check=True)
block=html.split('// GNSS_RTK_BEGIN')[1].split('// GNSS_RTK_END')[0]
harness=r'''
const assert=require('node:assert/strict'),vm=require('node:vm');
const elements={};const el=id=>elements[id]??=({value:'',checked:true,textContent:'',placeholder:'',options:[],children:[],replaceCount:0,scrollTop:0,addEventListener(event,fn){const old=this[event];this[event]=(...args)=>{old?.(...args);fn(...args)}},replaceChildren(...v){this.children=v;this.options=v;this.replaceCount++;this.scrollTop=0}});
let calls=[],failAction='',config={host:'saved.example',port:2101,mount:'saved-mount',ssid:'saved-wifi',has_auth:true,has_wifi_password:true,boot_checked:true,message:'saved'};
let busy=false,stopFails=false,holdPost=null,throwName='';
const ctx=vm.createContext({busy,setBusy:v=>{ctx.busy=v},rangeStop:async()=>{calls.push(['rangeStop'])},cmd:async(t,op)=>{calls.push([t,op]);if(stopFails)throw Error('stop failed')},el,URLSearchParams,AbortSignal,Date,console,document:{createElement:()=>({})},fetch:async(url,opt)=>{if(!opt?.method)return {ok:true,json:async()=>({...config})};if(throwName)throw Object.assign(new Error('Fetch Aborted'),{name:throwName});if(holdPost)await holdPost;let action=url.split('/').pop();calls.push([action,Object.fromEntries(opt.body)]);return {ok:action!==failAction,json:async()=>({ok:action!==failAction,message:action===failAction?'failed '+action:'done '+action})};}});
'''
checks=r'''
(async()=>{
 await vm.runInContext('loadGnssConfig(true)',ctx);assert.equal(el('gnssHost').value,'saved.example');assert.equal(el('gnssPass').value,'');assert.match(el('gnssWifiPass').placeholder,/登録済み/);
 el('gnssHost').value='field.example';el('gnssHost').input();await vm.runInContext('loadGnssConfig(true)',ctx);assert.equal(el('gnssHost').value,'field.example');
 el('gnssWifiPass').value='new-wifi-secret';el('gnssSsid').value='field-wifi';el('gnssGakuenPreset').onclick();assert.equal(el('gnssHost').value,'ntrip1.bizstation.jp');assert.equal(el('gnssMount').value,'3C722D56');assert.equal(el('gnssKeepAuth').checked,false);assert.equal(el('gnssWifiPass').value,'new-wifi-secret');
 el('gnssPass').value='new-service-secret';await el('gnssSave').onclick();assert.deepEqual(calls.map(c=>c[0]),['rangeStop','p','config']);assert.equal(calls[2][1].wifi_password,'new-wifi-secret');assert.equal(calls[2][1].keep_auth,'0');assert.equal(calls[2][1].save,'1');assert.equal(el('gnssPass').value,'');assert.equal(el('gnssWifiPass').value,'');assert.equal(el('gnssKeepAuth').checked,true);
 calls=[];failAction='config';await el('gnssSave').onclick();assert.deepEqual(calls.map(c=>c[0]),['rangeStop','p','config']);assert.match(el('gnssRtkMessage').textContent,/failed config/);
 calls=[];failAction='';stopFails=true;await el('gnssSave').onclick();assert.deepEqual(calls.map(c=>c[0]),['rangeStop','p']);assert.match(el('gnssRtkMessage').textContent,/stop failed/);stopFails=false;
 calls=[];await el('gnssConfigure').onclick();assert.deepEqual(calls.map(c=>c[0]),['config']);assert.equal(calls[0][1].save,'0');
 failAction='';await el('gnssLoad').onclick();assert.equal(el('gnssHost').value,'saved.example');assert.equal(el('gnssMount').value,'saved-mount');
 el('gnssStation').value='other-mount';el('gnssStation').onchange();assert.equal(el('gnssMount').value,'other-mount');
 await el('gnssAuto').onclick();config.boot_checked=true;
 vm.runInContext("renderGnssRtk({rtk:{table_ready:true,auto_pending:false,mount:'auto-mount'}})",ctx);assert.equal(el('gnssMount').value,'auto-mount');
 vm.runInContext("renderGnssRtk({network:{sta_connected:true,sta_ip:'172.20.10.2',ap_ip:'192.168.4.1'}})",ctx);assert.match(el('gnssNetwork').textContent,/http:\/\/172\.20\.10\.2\//);
 calls=[];let release;holdPost=new Promise(resolve=>release=resolve);const pending=el('gnssTable').onclick();assert.equal(ctx.busy,true);await el('gnssTable').onclick();assert.match(el('gnssRtkMessage').textContent,/処理中/);release();await pending;holdPost=null;assert.deepEqual(calls.map(c=>c[0]),['table']);assert.equal(ctx.busy,false);
 throwName='AbortError';await el('gnssTable').onclick();assert.match(el('gnssRtkMessage').textContent,/応答を時間内/);assert.equal(ctx.busy,false);throwName='';
 vm.runInContext("renderGnssRtk({network:{wifi_configured:true,sta_connected:false,sta_status:4,wifi_attempts:2}})",ctx);assert.match(el('gnssNetwork').textContent,/接続失敗/);assert.match(el('gnssNetwork').textContent,/30秒/);
 vm.runInContext("renderGnssRtk({gnss:{diagnostics:{gps_l5_enabled:null,tracked_gps_l5:null,dgnss_mode:2,rtcm_status_fresh:false}}})",ctx);
 let displayed=el('gnssPpkStatus').children.map(x=>x.textContent).join(' ');assert.match(displayed,/不明 \/ 不明信号/);assert.match(displayed,/Floatのみ/);assert.match(displayed,/未取得／更新途絶/);
 vm.runInContext("renderGnssRtk({gnss:{diagnostics:{gps_l5_enabled:true,tracked_gps_l5:3,dgnss_mode:3,rtcm_status_fresh:true,rtcm_used:7,rtcm_unused:2,rtcm_unknown:1}},ppk:{queue_bytes:120,queue_high_water_bytes:240,enqueue_dropped_rawx:0,enqueue_dropped_sfrbx:1,enqueue_dropped_other:0}})",ctx);
 displayed=el('gnssPpkStatus').children.map(x=>x.textContent).join(' ');assert.match(displayed,/有効 \/ 3信号/);assert.match(displayed,/FIXを試行/);assert.match(displayed,/7件採用 \/ 2件不採用/);assert.match(displayed,/120 \/ 240 byte/);
 // The picker must not rebuild on periodic status updates or silently use a
 // different mount from the one typed/selected by the operator.
 let tableCalls=0;const rtk={host:'caster.example',port:2101,table_ready:true,table_generation:1,mount:'BASE_A',connected:true,gps_msm_fresh:true,gps_l1_seen:true,gps_l5_seen:true};
 const stations=[{mount:'BASE_A',name:'神戸テスト局',lat:35,lon:139,systems:'GPS+GAL',network:'Field',format:'RTCM 3.2',details:'1075(1)',gga:true},{mount:'BASE_B',name:'大阪テスト局',lat:34,lon:135,systems:'GPS',network:'Other'}];
 ctx.fetch=async(url,opt)=>{if(opt?.method){calls.push([url.split('/').pop(),Object.fromEntries(opt.body)]);return {ok:true,json:async()=>({ok:true,message:'done'})};}if(url.endsWith('/stations')){tableCalls++;return {ok:true,json:async()=>({...rtk,stations})}}return {ok:true,json:async()=>({...config,last_available:true,last_host:rtk.host,last_port:rtk.port,last_mount:'BASE_A',last_saved:true,last_message:'保存済み'})};};
 el('gnssHost').value=rtk.host;el('gnssPort').value='2101';el('gnssMount').value='BASE_B';
 ctx.rtk=rtk;ctx.position={latitude:35,longitude:139};
 await vm.runInContext('gnssLiveRtk=rtk;gnssLivePosition=position;loadGnssStations(rtk)',ctx);
 assert.equal(tableCalls,1);assert.equal(el('gnssStation').value,'BASE_B');assert.match(el('gnssStation').options[1].textContent,/神戸.*BASE_A/);
 const rebuilds=el('gnssStation').replaceCount;el('gnssStation').scrollTop=180;
 for(let i=0;i<20;i++)vm.runInContext('renderGnssRtk({rtk,gnss:position})',ctx);
 assert.equal(tableCalls,1);assert.equal(el('gnssStation').replaceCount,rebuilds);assert.equal(el('gnssStation').scrollTop,180);
 el('gnssStationSearch').value='ＧＰＳ 神戸';el('gnssStationSearch').input();assert.equal(el('gnssStation').options.length,2);assert.equal(el('gnssMount').value,'BASE_B');assert.equal(el('gnssStation').value,'');
 el('gnssStation').value='BASE_A';el('gnssStation').onchange();assert.equal(el('gnssMount').value,'BASE_A');
 displayed=el('gnssStationDetails').children.map(x=>x.textContent).join(' ');assert.match(displayed,/神戸テスト局/);assert.match(displayed,/約 0.0 km/);assert.match(displayed,/1075/);assert.match(displayed,/判定できません/);
 el('gnssMount').value='DIRECT';el('gnssMount').input();assert.equal(el('gnssStation').value,'');calls=[];await el('gnssSelect').onclick();assert.equal(calls[0][1].mount,'DIRECT');
 el('gnssStationSearch').value='missing';el('gnssStationSearch').input();assert.equal(el('gnssStation').options.length,1);assert.match(el('gnssStationCount').textContent,/0 \/ 2局/);assert.equal(el('gnssMount').value,'DIRECT');
 el('gnssHost').value='unapplied.example';calls=[];await el('gnssSelect').onclick();assert.equal(calls.length,0);assert.match(el('gnssRtkMessage').textContent,/先に接続設定/);el('gnssHost').value=rtk.host;
 vm.runInContext('renderGnssRtk({rtk,gnss:{}})',ctx);displayed=el('gnssActiveStation').textContent;assert.match(displayed,/L5：信号情報を受信/);
 el('gnssMount').value='BASE_A';vm.runInContext('renderStationDetails()',ctx);assert.match(el('gnssStationDetails').children.map(x=>x.textContent).join(' '),/測位待ち/);
 await vm.runInContext('loadGnssConfig()',ctx);assert.match(el('gnssHistoryState').textContent,/BASE_A/);calls=[];await el('gnssReconnect').onclick();assert.equal(calls[0][0],'reconnect');assert.equal(el('gnssPass').value,'');
 rtk.table_generation=2;await vm.runInContext('loadGnssStations(rtk)',ctx);assert.equal(tableCalls,2);assert.equal(el('gnssStationSearch').value,'missing');
 const stableOptions=el('gnssStation').replaceCount;let releaseTable;const originalFetch=ctx.fetch;
 ctx.fetch=async()=>({ok:true,json:()=>new Promise(resolve=>releaseTable=resolve)});rtk.table_generation=3;
 const oldRequest=vm.runInContext('loadGnssStations(rtk)',ctx);await new Promise(setImmediate);
 vm.runInContext("gnssLiveRtk={...rtk,host:'new-caster.example'}",ctx);releaseTable({...rtk,stations:[{mount:'STALE'}]});await oldRequest;assert.equal(el('gnssStation').replaceCount,stableOptions);ctx.fetch=originalFetch;
 console.log('PASS offline NTRIP form and picker: search normalization/multiword/empty, mount identity, stable periodic DOM/scroll, metadata/distance/freshness, unapplied caster guard and successful history reconnect');
})().catch(e=>{console.error(e);process.exitCode=1});
'''
subprocess.run(['node'],input=harness+'\nvm.runInContext('+__import__('json').dumps(block)+',ctx);\n'+checks,text=True,encoding='utf-8',check=True)

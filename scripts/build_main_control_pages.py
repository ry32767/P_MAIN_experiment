"""Generate the public console from the integrated firmware's canonical HTML."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
source = root / 'firmware/main_control/web/index.html'
target = root / 'web/main-control/index.html'
html = source.read_text(encoding='utf-8')
assert html.count('<script>') == 1
header = '''<p class="note">画面更新：2026-10-27</p><p>GITHUB PAGES · 親子実験コントローラー</p><p class="note">インターネット接続中にこの画面を開き、機体のWi-Fi「AquaBeacon-MAIN」に接続してから「親機に接続」を押してください。通信は端末から親機へ直接行います。</p><div class="actions"><button id="connectDevice" class="primary">親機に接続</button><a href="http://192.168.4.1/" target="_blank" rel="noopener">機体内の操作画面を開く</a></div><p id="offlineReady" class="note">オフライン用の画面を準備中…</p><p class="note">対応するChromeでは「ローカルネットワークへのアクセス」を許可してください。接続できない場合は機体内画面を使えます。これはインターネット越しの遠隔操作ではありません。</p>'''
html = html.replace('<header>', '<header>' + header, 1)
html = re.sub(r'(?<![\w.])fetch\(', 'deviceFetch(', html)
wrapper = r"""let deviceConnected=false;
function deviceFetch(path,options){if(!deviceConnected)return Promise.reject(Error('先に親機へ接続してください'));if(typeof path!=='string'||!path.startsWith('/api/')||/[\\\\\r\n]/.test(path))return Promise.reject(Error('無効な操作先です'));return window.fetch('http://192.168.4.1'+path,options)}
"""
html = html.replace('<script>', '<script>' + wrapper, 1)
html = html.replace('async function tick(){', "async function tick(){if(!deviceConnected){el('connection').textContent='未接続 · 機体Wi-Fiに接続後、親機に接続を押してください';setTimeout(tick,1000);return;}", 1)
ending = """tick();el('connectDevice').onclick=async()=>{el('connectDevice').disabled=true;deviceConnected=true;try{const r=await deviceFetch('/api/status',{signal:AbortSignal.timeout(8000)});if(!r.ok)throw Error('親機から正常な応答がありません');const s=await r.json();if(s.role!=='P_MAIN')throw Error('親機を確認できません');setBusy(false);el('message').textContent='親機に接続しました';startPhone('time');}catch(e){deviceConnected=false;el('message').textContent='接続できません。機体Wi-Fi、ローカルネットワークの許可を確認するか、機体内画面を開いてください。';}finally{el('connectDevice').disabled=false}};
if('serviceWorker' in navigator){navigator.serviceWorker.register('./sw.js',{updateViaCache:'none'}).then(()=>navigator.serviceWorker.ready).then(()=>el('offlineReady').textContent='画面を保存しました。機体Wi-Fiへ切り替えた後もこのページを使用できます。').catch(()=>el('offlineReady').textContent='オフライン保存ができません。このページを開いたまま機体Wi-Fiへ接続してください。')}
"""
assert html.count("tick();startPhone('time');") == 1
html = html.replace("tick();startPhone('time');", ending)
target.write_text(html, encoding='utf-8')
print('Generated web/main-control/index.html')

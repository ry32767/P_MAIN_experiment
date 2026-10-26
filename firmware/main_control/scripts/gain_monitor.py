"""USB live ENV monitor; localhost only. Each run stops TX and restores child logging."""
from pathlib import Path
import json,time,threading,serial,serial.tools.list_ports
from http.server import ThreadingHTTPServer,BaseHTTPRequestHandler
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'reports/raw/gain_live.json'
state={'phase':'待機中','points':[],'shots':[],'elapsed':0,'error':None}
lock=threading.Lock()
def measure():
 global state
 ports={p.serial_number:p.device for p in serial.tools.list_ports.comports()}
 handles={}; buffers={}; statuses={}
 def read():
  for role,s in handles.items():
   buffers[role]+=s.read(s.in_waiting or 1)
   while b'\n' in buffers[role]:
    line,buffers[role]=buffers[role].split(b'\n',1)
    try:v=json.loads(line)
    except (ValueError,UnicodeError):continue
    if v.get('role')==role:statuses[role]=v
    elif v.get('rx_plot'):
     v['t']=max(0,time.monotonic()-start);state['points'].append(v)
 def wait(seconds):
  until=time.monotonic()+seconds
  while time.monotonic()<until:read();time.sleep(.002)
 start=time.monotonic()
 try:
  for role,uid in [('P_MAIN','A444A41EE9334705'),('C_MAIN','A521D21EB1DCFB44')]:
   handles[role]=serial.Serial(ports[uid],115200,timeout=.002);buffers[role]=b'';handles[role].write(b'h')
  wait(3)
  assert set(statuses)==set(handles),'親子の識別に失敗'
  handles['P_MAIN'].write(b'X');handles['C_MAIN'].write(b'q');wait(4)
  assert statuses['C_MAIN']['sd_reason']=='STOPPED','SD停止確認に失敗'
  state['phase']='準備中（5秒後に開始）';wait(5)
  start=time.monotonic();state['phase']='計測中';handles['C_MAIN'].write(b'm');handles['P_MAIN'].write(b'T')
  last=None
  while time.monotonic()-start<60:
   read();state['elapsed']=time.monotonic()-start
   p=statuses.get('P_MAIN',{});state['parent']=p;state['child']=statuses.get('C_MAIN',{})
   key=(p.get('shot_seq'),p.get('range_state'))
   if key!=last:
    state['shots'].append({'t':state['elapsed'],'seq':key[0],'state':key[1]});last=key
   time.sleep(.002)
  assert len(state['points'])>200,'高速受信データが不足しています'
  state['phase']='送信停止・SD記録再開を確認中'
 except Exception as e:state['error']=str(e);state['phase']='エラー'
 finally:
  if 'P_MAIN' in handles:handles['P_MAIN'].write(b'X')
  if 'C_MAIN' in handles:handles['C_MAIN'].write(b'n');wait(.3);handles['C_MAIN'].write(b's')
  wait(3)
  state['final']=statuses
  for s in handles.values():s.close()
  if not state['error']:
   if statuses.get('P_MAIN',{}).get('ranging') is False and statuses.get('C_MAIN',{}).get('sd_reason')=='NONE':
    state['phase']='完了（送信停止・SD記録再開）'
   else:state['phase']='エラー';state['error']='終了後の送信停止・SD再開を確認できませんでした'
  OUT.write_text(json.dumps(state,ensure_ascii=False),encoding='utf-8')
  OUT.with_name('gain_live_'+time.strftime('%Y%m%d_%H%M%S')+'.json').write_text(json.dumps(state,ensure_ascii=False),encoding='utf-8')
class Handler(BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def do_GET(self):
  if self.path=='/api/state':data=json.dumps(state,ensure_ascii=False).encode()
  else:data=(ROOT/'web/gain_monitor.html').read_bytes()
  self.send_response(200);self.send_header('Content-Type','application/json' if self.path=='/api/state' else 'text/html; charset=utf-8');self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(data)
 def do_POST(self):
  if self.path=='/start' and state['phase'] in ('待機中','完了（送信停止・SD記録再開）','エラー'):
   state.update(phase='接続確認中',points=[],shots=[],elapsed=0,error=None)
   threading.Thread(target=measure,daemon=True).start()
  self.send_response(204);self.end_headers()
if __name__=='__main__':
 if OUT.exists():
  state=json.loads(OUT.read_text(encoding='utf-8'))
  if state['phase'] not in ('完了（送信停止・SD記録再開）','エラー'):state['phase']='待機中'
 print('http://127.0.0.1:8777',flush=True)
 ThreadingHTTPServer(('127.0.0.1',8777),Handler).serve_forever()

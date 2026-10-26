"""Observe child synchronization loss/recovery while deploying the parent build."""
import serial,time,json,threading,subprocess,sys
from pathlib import Path
r=Path(__file__).resolve().parents[1]
records=[];done=threading.Event()
def observe():
 with serial.Serial('COM3',115200,timeout=.1) as s:
  s.write(b'h');buf=b''
  while not done.is_set():
   buf+=s.read(8192)
   while b'\n' in buf:
    line,buf=buf.split(b'\n',1)
    try:v=json.loads(line)
    except ValueError:continue
    if v.get('role')=='C_MAIN':records.append(v)
t=threading.Thread(target=observe);t.start()
try:
 time.sleep(3)
 with (r/'reports/raw/health_flash_parent.txt').open('w',encoding='utf-8') as out:
  subprocess.run([sys.executable,str(r/'scripts/flash.py'),'parent'],stdout=out,stderr=subprocess.STDOUT,check=True)
 time.sleep(12)
finally:done.set();t.join()
(r/'reports/raw/parent_restart_child.json').write_text(json.dumps(records),encoding='utf-8')
states=[v['sync_ok'] for v in records]
print({'observations':len(states),'initial':states[0] if states else None,'lost':False in states,'final':states[-1] if states else None})
assert states and states[-1] and False in states,'Did not observe loss and recovery'

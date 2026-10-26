from pathlib import Path
import serial,time,concurrent.futures,json,argparse
p=argparse.ArgumentParser();p.add_argument('--seconds',type=float,default=15);p.add_argument('--name',default='capture');a=p.parse_args()
r=Path(__file__).resolve().parents[1]/'reports/raw';r.mkdir(exist_ok=True)
def read(port):
 data=b''
 with serial.Serial(port,115200,timeout=.1) as s:
  s.write(b'h');end=time.monotonic()+a.seconds
  while time.monotonic()<end:data+=s.read(8192)
 (r/f'{a.name}_{port}.txt').write_bytes(data)
 vals=[]
 for line in data.decode(errors='replace').splitlines():
  try:
   v=json.loads(line)
   if isinstance(v,dict) and 'role' in v:vals.append(v)
  except (ValueError,TypeError):pass
 if vals:
  v=vals[-1];print(json.dumps({'port':port,'reports':len(vals),'first':vals[0],'last':v},ensure_ascii=False))
 else: print(port, 'NO STATUS', '\n'.join(x for x in data.decode(errors='replace').splitlines()[-5:] if 'password' not in x.lower()))
with concurrent.futures.ThreadPoolExecutor() as ex:list(ex.map(read,['COM3','COM6']))

from pathlib import Path
import argparse,serial,time,sys,json
p=argparse.ArgumentParser();p.add_argument('--port',required=True);p.add_argument('--out',required=True);p.add_argument('--health',action='store_true');p.add_argument('--raw',action='store_true');p.add_argument('--gnss',choices=['ubx','rtcm','meta']);a=p.parse_args()
assert not Path(a.out).exists(),'Output exists; refusing overwrite'
with serial.Serial(a.port,115200,timeout=.5) as s:
 s.write(b'q');end=time.monotonic()+15;confirmed=False
 while time.monotonic()<end:
  s.write(b'h');line=s.readline()
  try:
   status=json.loads(line)
   if status.get('sd_reason')=='STOPPED':confirmed=True;break
  except ValueError:pass
 assert confirmed,'SD stop was not acknowledged'
 s.write({'ubx':b'u','rtcm':b'o','meta':b'k'}[a.gnss] if a.gnss else (b'i' if a.raw else (b'e' if a.health else b'd')))
 end=time.monotonic()+15
 while time.monotonic()<end:
  line=s.readline()
  if line.startswith(b'# DUMP_BEGIN bytes='):
   size=int(line.split(b'=')[1]);break
 else:raise RuntimeError('No length-framed dump header; update firmware')
 data=bytearray();end=time.monotonic()+max(120,size/8000+30)
 while len(data)<size and time.monotonic()<end:
  data.extend(s.read(min(16384,size-len(data))))
 assert len(data)==size,'Truncated binary dump'
 assert s.readline().strip()==b'# DUMP_END','Missing dump completion marker'
 Path(a.out).write_bytes(data)
 print(json.dumps({'file':a.out,'bytes':len(data)}))

from pathlib import Path
import argparse,serial,time,sys,json
p=argparse.ArgumentParser();p.add_argument('--port',required=True);p.add_argument('--out',required=True);p.add_argument('--health',action='store_true');p.add_argument('--raw',action='store_true');a=p.parse_args()
with serial.Serial(a.port,115200,timeout=.5) as s:
 s.write(b'q');end=time.monotonic()+15;confirmed=False
 while time.monotonic()<end:
  s.write(b'h');line=s.readline()
  try:
   status=json.loads(line)
   if status.get('sd_reason')=='STOPPED':confirmed=True;break
  except ValueError:pass
 assert confirmed,'SD stop was not acknowledged'
 s.write(b'i' if a.raw else (b'e' if a.health else b'd'))
 received=bytearray();end=time.monotonic()+120
 while time.monotonic()<end:
  received.extend(s.read(16384))
  if b'# DUMP_END' in received:break
 else:raise RuntimeError('Dump timed out')
 start=received.find(b'# DUMP_BEGIN');assert start>=0,'No dump marker'
 start=received.index(b'\n',start)+1;end=received.index(b'# DUMP_END',start)
 data=received[start:end];assert data,'Empty dump'
 Path(a.out).write_bytes(data)
 print(json.dumps({'file':a.out,'bytes':len(data)}))

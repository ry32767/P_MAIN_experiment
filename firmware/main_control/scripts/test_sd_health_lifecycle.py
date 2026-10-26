from pathlib import Path
import serial,serial.tools.list_ports,time,json
r=Path(__file__).resolve().parents[1]
out=r/'reports/2026-09-20_002_sd_health_lifecycle/evidence';out.mkdir(parents=True,exist_ok=True)
expected={'COM3':'A521D21EB1DCFB44','COM6':'A444A41EE9334705'}
ports={p.device:p.serial_number for p in serial.tools.list_ports.comports()}
assert all(ports.get(p)==v for p,v in expected.items()),'USB identity changed'
log=[];buffers={p:bytearray() for p in expected};latest={}
with serial.Serial('COM3',115200,timeout=0) as child,serial.Serial('COM6',115200,timeout=0) as parent:
 devices={'COM3':child,'COM6':parent}
 def observe(seconds):
  end=time.monotonic()+seconds
  while time.monotonic()<end:
   for port,s in devices.items():
    buffers[port].extend(s.read(16384))
    while b'\n' in buffers[port]:
     line,_,rest=buffers[port].partition(b'\n');buffers[port]=bytearray(rest)
     try:
      v=json.loads(line)
      if 'role' in v:latest[port]=v;log.append({'host_time':time.time(),'port':port,'status':v})
     except ValueError:pass
   time.sleep(.01)
  return json.loads(json.dumps(latest))
 child.write(b'h');parent.write(b'h');before=observe(3)
 assert before['COM3']['role']=='C_MAIN' and before['COM6']['role']=='P_MAIN'
 assert before['COM3']['local']['flags']&2 and before['COM6']['local']['flags']&2
 try:
  child.write(b'q');stopped=observe(4)
  assert stopped['COM3']['sd_reason']=='STOPPED'
  assert not stopped['COM3']['local']['flags']&2
  assert not stopped['COM6']['remote']['flags']&2
  assert stopped['COM6']['local']['flags']&2
  assert stopped['COM3']['local']['rows']==stopped['COM3']['local']['durable_rows']
 finally:
  child.write(b's')
 resumed=observe(5)
 assert resumed['COM3']['local']['flags']&2 and resumed['COM6']['remote']['flags']&2
 assert resumed['COM3']['log_file']!=before['COM3']['log_file']
 assert resumed['COM3']['raw_file']!=before['COM3']['raw_file']
 assert resumed['COM3']['local']['sd_errors']==before['COM3']['local']['sd_errors']
 assert resumed['COM6']['remote']['rows']>0
 assert not resumed['COM6']['ranging']
result={'result':'PASS','before':before,'stopped':stopped,'resumed':resumed}
(out/'lifecycle.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
(out/'observations.jsonl').write_text('\n'.join(json.dumps(x,ensure_ascii=False) for x in log),encoding='utf-8')
print(json.dumps({'result':'PASS','old_file':before['COM3']['log_file'],'new_file':resumed['COM3']['log_file'],'stopped_durable_rows':stopped['COM3']['local']['durable_rows'],'parent_remote_resumed_rows':resumed['COM6']['remote']['rows']}))

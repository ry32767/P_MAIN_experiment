"""Start new parent/child SD files without rebooting or erasing previous trials."""
import argparse,json,time,serial,serial.tools.list_ports
from pathlib import Path

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--allow-uncalibrated',action='store_true');args=parser.parse_args()
 ports={p.serial_number:p.device for p in serial.tools.list_ports.comports()}
 devices={};buffers={};latest={};identified=False;stopping=False
 def wait_for(predicate,seconds=15):
  end=time.monotonic()+seconds
  while time.monotonic()<end:
   for role,s in devices.items():
    buffers[role]+=s.read(s.in_waiting or 1)
    while b'\n' in buffers[role]:
     line,buffers[role]=buffers[role].split(b'\n',1)
     try:v=json.loads(line)
     except ValueError:continue
     if v.get('role')==role:latest[role]=v
   if predicate():return
  raise RuntimeError('Device state confirmation timed out')
 try:
  for role,uid in [('P_MAIN','A444A41EE9334705'),('C_MAIN','A521D21EB1DCFB44')]:
   devices[role]=serial.Serial(ports[uid],115200,timeout=.05);buffers[role]=b'';devices[role].write(b'h')
  wait_for(lambda:len(latest)==2);identified=True
  missing=[role for role,v in latest.items() if v.get('body_calibration',{}).get('r')!=1]
  if missing:
   print('WARNING: individual/body-axis calibration incomplete or unknown: '+', '.join(missing))
   if not args.allow_uncalibrated:raise RuntimeError('No files changed. Complete calibration or explicitly pass --allow-uncalibrated.')
  before={role:{k:v[k] for k in ('log_file','health_file','raw_file')} for role,v in latest.items()}
  stopping=True
  for s in devices.values():s.write(b'q')
  wait_for(lambda:all(v['sd_reason']=='STOPPED' for v in latest.values()))
  for s in devices.values():s.write(b's')
  wait_for(lambda:all(v['sd_reason']=='NONE' and v['log_file']!=before[role]['log_file'] and v['health_file']!=before[role]['health_file'] for role,v in latest.items()))
  result={role:{'before':before[role],'after':{k:v[k] for k in ('log_file','health_file','raw_file')}} for role,v in latest.items()}
  folder=Path(__file__).resolve().parents[1]/'reports/raw';folder.mkdir(exist_ok=True)
  (folder/('new_experiment_'+time.strftime('%Y%m%d_%H%M%S')+'.json')).write_text(json.dumps(result,indent=2),encoding='utf-8')
  print(json.dumps(result,indent=2))
  print('Recording prepared. Start each camera and complete mandatory 30-second movement capture on each device before ranging; --allow-uncalibrated cannot skip it.')
 finally:
  # s is idempotent while saving; recover any device left stopped by an error.
  if identified and stopping:
   for s in devices.values():s.write(b's')
  for s in devices.values():s.close()

if __name__=='__main__':main()

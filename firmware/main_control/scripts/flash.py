from pathlib import Path
import serial,serial.tools.list_ports,subprocess,time,json,argparse,shutil
p=argparse.ArgumentParser();p.add_argument('environment',choices=['parent','child50','child100']);a=p.parse_args()
r=Path(__file__).resolve().parents[1];parent=a.environment=='parent'
expected='A444A41EE9334705' if parent else 'A521D21EB1DCFB44';role='P_MAIN' if parent else 'C_MAIN'
ports=[x.device for x in serial.tools.list_ports.comports() if x.serial_number==expected];assert len(ports)==1,'Expected USB identity not uniquely connected'
port=ports[0];identified=False
with serial.Serial(port,115200,timeout=.2) as s:
 s.write(b'h');end=time.monotonic()+3
 while time.monotonic()<end:
  try:
   v=json.loads(s.readline());identified=v.get('role')==role
   if identified:break
  except ValueError:pass
 assert identified,'Firmware role mismatch'
 s.write(b'Xq');end=time.monotonic()+15;stopped=False
 while time.monotonic()<end:
  try:
   v=json.loads(s.readline());stopped=v.get('sd_reason') in ('STOPPED','MOUNT_FAILED','NOT_STARTED')
   if stopped:break
  except ValueError:pass
 assert stopped,'SD did not stop; refusing reset'
print(f'Identified {role} at {port}; recording stopped')
pio=shutil.which('pio') or r.parent/'P_MAIN_experiment/.venv/Scripts/pio.exe'
subprocess.run([str(pio),'run','-d',str(r),'-e',a.environment,'-t','upload','--upload-port',port],check=True)

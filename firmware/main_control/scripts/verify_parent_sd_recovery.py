from pathlib import Path
import serial,serial.tools.list_ports,time,json,zlib
root=Path(__file__).resolve().parents[1];out=root/'reports/raw/sd_recovery_verification';out.mkdir(exist_ok=True)
ports=[p.device for p in serial.tools.list_ports.comports() if p.serial_number=='A444A41EE9334705'];assert len(ports)==1
samples=[]
with serial.Serial(ports[0],115200,timeout=.25) as s:
 def observe(seconds):
  end=time.monotonic()+seconds;values=[]
  while time.monotonic()<end:
   line=s.readline()
   try:
    v=json.loads(line)
    if v.get('role')=='P_MAIN':values.append(v);samples.append(v)
   except ValueError:pass
  return values
 s.write(b'h');initial=observe(3);assert initial and 'sd_recovery' in initial[-1]
 print('Initial:',initial[-1]['sd_reason'],flush=True)
 soak=observe(110);assert soak
 s.write(b'q');stopped=observe(7);assert stopped and not stopped[-1]['sd_recovery']['enabled']
 result={'soak_samples':len(soak),'first':soak[0],'last':soak[-1],'stopped':stopped[-1],'readback':{}}
 if stopped[-1]['sd_reason']=='STOPPED':
  for command,label in [(b'd','time_position'),(b'e','health')]:
   s.write(command);end=time.monotonic()+30;lines=[];begun=False;complete=False
   while time.monotonic()<end:
    line=s.readline().strip()
    if line==b'# DUMP_BEGIN':begun=True;continue
    if line==b'# DUMP_END':complete=True;break
    if begun:lines.append(line)
   assert complete and lines
   (out/(label+'.csv')).write_bytes(b'\n'.join(lines)+b'\n')
   errors=0
   for line in lines[1:]:
    data,crc=line.rsplit(b',',1);errors+=zlib.crc32(data)!=int(crc,16)
   result['readback'][label]={'rows':len(lines)-1,'crc_errors':errors};assert errors==0
 s.write(b's');resumed=observe(12);assert resumed
 result['resumed']=resumed[-1]
 (out/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
 (out/'samples.json').write_text(json.dumps(samples),encoding='utf-8')
 print(json.dumps({'initial':initial[-1]['sd_reason'],'soak_first_rows':soak[0]['local']['rows'],'soak_last_rows':soak[-1]['local']['rows'],'first_errors':soak[0]['local']['sd_errors'],'last_errors':soak[-1]['local']['sd_errors'],'recovery':soak[-1]['sd_recovery'],'readback':result['readback'],'resumed':resumed[-1]['sd_reason'],'resumed_rows':resumed[-1]['local']['rows']},indent=2),flush=True)

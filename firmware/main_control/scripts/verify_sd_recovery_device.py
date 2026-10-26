"""USB-only parent SD recovery integration test. Requires diagnostic firmware.

Uses a synced software fault, never removal, formatting or existing-log deletion.
Restores recording in finally. Child hardware is not accessed.
"""
import argparse,json,re,time,sys
from pathlib import Path
import serial,serial.tools.list_ports
from decode_binary import decode


def run(raw):
    out=Path(raw);out.mkdir(parents=True,exist_ok=True)
    ports=[p for p in serial.tools.list_ports.comports() if p.serial_number=='A444A41EE9334705']
    assert len(ports)==1,'Parent USB identity not unique'
    lines=[];states=[];latest={};results=[]
    with serial.Serial(ports[0].device,115200,timeout=.15) as s:
        def read():
            nonlocal latest
            line=s.readline();lines.append(line)
            try:
                value=json.loads(line)
                if value.get('role')=='P_MAIN':latest=value;states.append(value)
            except ValueError:pass
            return line
        def wait(predicate,seconds=15):
            end=time.monotonic()+seconds
            while time.monotonic()<end:
                read()
                if latest and predicate(latest):return latest.copy()
            raise AssertionError('State wait expired')
        def dump(command,label):
            s.write(command);end=time.monotonic()+12;size=None
            while time.monotonic()<end:
                match=re.match(rb'# DUMP_BEGIN bytes=(\d+)',read())
                if match:size=int(match[1]);break
            assert size is not None,'No binary dump header'
            data=b'';end=time.monotonic()+30
            while len(data)<size and time.monotonic()<end:data+=s.read(size-len(data))
            assert len(data)==size,'Incomplete binary dump'
            end=time.monotonic()+5;complete=False
            while time.monotonic()<end:
                if b'# DUMP_END' in read():complete=True;break
            assert complete
            path=out/(label+'.bin');path.write_bytes(data)
            verified=decode(path);assert sum(verified['records'].values())>0
            return verified
        try:
            s.write(b'h');wait(lambda v:v.get('role')=='P_MAIN')
            s.write(b'~');end=time.monotonic()+4;ready=False
            while time.monotonic()<end:
                if b'# SD_RECOVERY_TEST ready' in read():ready=True;break
            assert ready,'Diagnostic firmware not confirmed; no fault injected'
            wait(lambda v:v['sd_reason']=='NONE' and v['local']['durable_rows']>0)
            for trial in range(2):
                before=latest.copy();s.write(b'!')
                failed=wait(lambda v:v['sd_reason']=='TEST_WRITE_INTERRUPTED' and v['sd_recovery']['waiting'])
                recovered=wait(lambda v:v['sd_reason']=='NONE' and v['sd_recovery']['recoveries']>before['sd_recovery']['recoveries'],65)
                durable=wait(lambda v:v['sd_reason']=='NONE' and v['local']['durable_rows']>=3)
                assert recovered['log_file']!=before['log_file'] and recovered['health_file']!=before['health_file']
                assert durable['local']['sd_errors']==failed['local']['sd_errors']
                results.append(dict(before=before,failed=failed,recovered=recovered,durable=durable))
                print('PASS parent automatic recovery',trial+1,flush=True)
            s.write(b'q');stopped=wait(lambda v:v['sd_reason']=='STOPPED' and not v['sd_recovery']['enabled'])
            readback={name:dump(command,name) for command,name in [(b'd','recovered_time_position'),(b'e','recovered_health')]}
            result=dict(verified=True,trials=results,readback=readback,stopped=stopped)
            (out/'parent_result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
        finally:
            s.write(b's');end=time.monotonic()+5
            while time.monotonic()<end:read()
            (out/'parent_test_usb.txt').write_bytes(b''.join(lines))
            (out/'parent_test_states.json').write_text(json.dumps(states,indent=2),encoding='utf-8')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--raw',required=True);a=p.parse_args();run(a.raw)

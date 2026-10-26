"""Explicit USB control test: no Wi-Fi switching, no log deletion or formatting.

Stops Spresense, exercises the calibration control path without finishing poses,
tests recording restart, stops again, then tests the guarded reboot. Requires
--control to perform these operations. It never saves a calibration candidate.
"""
import argparse,json,re,time
from pathlib import Path
import serial,serial.tools.list_ports

def run(control,raw_path):
    ports=[p for p in serial.tools.list_ports.comports() if p.serial_number=='A444A41EE9334705']
    assert len(ports)==1,'Parent USB serial identity must be unique'
    lines=[];latest={};results=[]
    with serial.Serial(ports[0].device,115200,timeout=.2) as connection:
        def read():
            nonlocal latest
            line=connection.readline();lines.append(line)
            try:
                value=json.loads(line)
                if value.get('role')=='P_MAIN':latest=value
            except ValueError:pass
            return line.decode(errors='replace')
        def wait(predicate,seconds=15):
            end=time.monotonic()+seconds
            while time.monotonic()<end:
                read()
                if predicate(latest.get('spresense',{})):return latest['spresense']
            raise AssertionError('Timed out waiting for Spresense state')
        def command(op,args=''):
            connection.write(f'@SPR {op}{args}\n'.encode())
            end=time.monotonic()+10;identifier=None
            while time.monotonic()<end:
                line=read();match=re.search(r'SPR_USB accepted=(\d) command_id=(\d+) op=(\d+)',line)
                if match and int(match[3])==op:
                    assert match[1]=='1',f'Command {op} rejected by parent'
                    identifier=int(match[2]);break
            assert identifier,'No command acceptance response'
            response=wait(lambda s:s.get('command_id')==identifier and not s.get('command_pending'))
            assert response['command_result'] not in (1,2,3,4,6),f'Command {op} failed: {response["command_result"]}'
            results.append({'op':op,'id':identifier,'response':response['command_result']})
            return response
        try:
            connection.write(b'h')
            wait(lambda s:s.get('link_fresh') and s.get('imu_ok'))
            assert latest.get('role')=='P_MAIN','Firmware role mismatch'
            baseline=latest['spresense'].copy()
            if control:
                command(2);wait(lambda s:not s.get('recording') and s.get('last_operation')==2 and s.get('last_result')==0)
                command(5,' 9.80665 34.6868 255');wait(lambda s:s.get('calibration')==1)
                command(8,' 9.80665 34.6868 4');wait(lambda s:s.get('calibration_pose')==4 and s.get('calibration') in (2,3))
                command(10);wait(lambda s:s.get('calibration')==0)
                assert latest['spresense']['calibration_generation']==baseline['calibration_generation'],'Unsaved calibration changed application coefficients'
                command(3);restart=wait(lambda s:s.get('last_operation')==3 and s.get('last_result') in (0,4),20).copy()
                results.append({'restart_result':restart['last_result'],'recording':restart['recording'],'sd_ok':restart['sd_ok']})
                command(2);before=wait(lambda s:not s.get('recording') and s.get('last_operation')==2 and s.get('last_result')==0).copy()
                command(4)
                # A lower sensor count with new state proves a real reboot, not just acceptance.
                after=wait(lambda s:s.get('link_fresh') and s.get('imu_ok') and s.get('imu_samples',0)<before['imu_samples'],25).copy()
                results.append({'reboot_observed':True,'samples_before':before['imu_samples'],'samples_after':after['imu_samples']})
                command(2);wait(lambda s:not s.get('recording') and s.get('last_operation')==2 and s.get('last_result')==0)
            final=latest['spresense'].copy()
            summary={'parent_usb_serial':ports[0].serial_number,'baseline':baseline,'operations':results,'final':final,'absolute_precision_verified':False,'physical_six_pose_calibration_completed':False}
            print(json.dumps(summary,ensure_ascii=False,indent=2))
            Path(raw_path).with_suffix('.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
        finally:
            Path(raw_path).write_bytes(b''.join(lines))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--control',action='store_true');parser.add_argument('--raw',default='reports/raw/spresense_completion_controls.txt')
    args=parser.parse_args();run(args.control,args.raw)

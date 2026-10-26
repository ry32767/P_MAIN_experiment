"""Synthetic integration and failure tests. No devices or network."""
import math,json,sys,struct,tempfile,zlib,csv,io
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from body_alignment import *
from sync_camera_imu import synchronize
from binary_log import FILE,RECORD
from apply_body_alignment import convert

def header(role,boot):
    prefix=struct.pack('<8sHHBBHIQ',b'AQBIN001',1,32,role,13,0,boot,1);return prefix+struct.pack('<I',zlib.crc32(prefix))
def record(kind,mono,payload,seq=0):
    prefix=struct.pack('<IHHQI',0x52425141,kind,len(payload),mono,seq);return prefix+struct.pack('<I',zlib.crc32(payload,zlib.crc32(prefix)))+payload
def fail(fn):
    try:fn()
    except (ValueError,AssertionError):return
    raise AssertionError('Expected rejection')

ir=from_vector([.4,-.3,.8]);mr=from_vector([-.7,.2,.5]);mag=[22,17,39]
acc=[rotate(transpose(ir),[v*9.8 for v in e]) for e in EXPECTED]
fields=[rotate(transpose(mr),rotate(transpose(p),mag)) for p in POSES]
fitted,errors=imu_fit(acc);assert max(errors)<1e-5
fitted,quality=magnetic_fit(fields);assert quality['fourth_pose_relative_residual']<1e-5
assert max(abs(a-b) for x,y in zip(fitted,mr) for a,b in zip(x,y))<.001
row={'ax':'0','ay':'0','az':'0','corrected_ax':'1','corrected_ay':'2','corrected_az':'3'}
converted=convert(row,{'imu_q':quaternion(ir),'mag_q':quaternion(mr)})
assert all(abs(converted['body_accel_'+k]-v)<1e-8 for k,v in zip('xyz',rotate(ir,[1,2,3]))) and converted['ax']=='0'
fail(lambda:imu_fit([acc[0]]*4));bad=[x[:] for x in fields];bad[3]=[100,100,100];fail(lambda:magnetic_fit(bad))

with tempfile.TemporaryDirectory() as d:
    root=Path(d);events=[];boot=99;mask=0
    def event(kind,phase,t):
        payload=struct.pack('<4I2Q3I4B20s64s64s64s',3,boot,1,1,int(t*1e6),0,0,0,0xffffffff,kind,phase,0,mask,b'synthetic',b'imu.bin',b'raw.bin',b'health.bin')
        events.append(record(13,int(t*1e6),payload))
    event(1,0,1)
    windows={}
    for p in range(1,6):
        start=10*p;end=start+(31 if p==5 else 6);windows[p]=(start,end);event(2,p,start);mask|=1<<(p-1);event(3,p,end)
    event(4,0,90);body=root/'body.bin';body.write_bytes(header(2,boot)+b''.join(events))
    imu=root/'imu.bin';data=bytearray(header(2,boot))
    for n in range(4600):
        t=n*.02;a=[0,0,9.8];g=[0,0,0];m=fields[0]
        for phase,(begin,end) in windows.items():
            if phase<5 and begin<=t<=end:a=acc[phase-1];m=fields[phase-1]
            if phase==5 and begin<=t<=end:g=[math.sin(t*2),math.cos(t*1.3),math.sin(t*.7)]
        stamp=int(t*1e6);payload=struct.pack('<IIQQQQB13f',n,14,stamp,stamp,stamp,0,0,1,0,0,0,*a,*g,*m)
        data.extend(record(1,stamp,payload,n))
    imu.write_bytes(data);result,motion=fit_recording(body,imu);assert result['axes']==3 and result['camera']==0 and len(motion)>1000
    assert max(abs(a-b) for a,b in zip(result['imu_q'],quaternion(ir)))<1e-4
    # CRC and unfinished recordings cannot produce a ready profile.
    corrupt=bytearray(body.read_bytes());corrupt[-1]^=1;(root/'bad.bin').write_bytes(corrupt);fail(lambda:fit_recording(root/'bad.bin',imu))
    (root/'short.bin').write_bytes(header(2,boot)+b''.join(events[:-1]));fail(lambda:body_events(root/'short.bin'))

    # Parent path uses separately clocked Spresense CSV and magnetic binary.
    epoch=1792886400000000;parent_events=[]
    for packed in events:
        payload=bytearray(packed[24:]);mono=struct.unpack_from('<Q',payload,16)[0]
        struct.pack_into('<Q',payload,24,epoch+mono);payload[46]=1
        parent_events.append(record(13,mono,payload))
    parent_body=root/'parent_body.bin';parent_body.write_bytes(header(1,boot)+b''.join(parent_events))
    columns=['parent_session','calibration_generation','sample_utc_us','time_quality']+['corrected_'+k for k in ['ax','ay','az','gx','gy','gz']]
    lines=[','.join(columns)+',crc32'];mag_data=bytearray(header(1,boot))
    for n in range(4600):
        t=n*.02;a=[0,0,9.8];g=[0,0,0];m=fields[0]
        for phase,(begin,end) in windows.items():
            if phase<5 and begin<=t<=end:a=acc[phase-1];m=fields[phase-1]
            if phase==5 and begin<=t<=end:g=[math.sin(t*2),math.cos(t*1.3),math.sin(t*.7)]
        mono=int(t*1e6);line=io.StringIO();csv.writer(line,lineterminator='').writerow([boot,1,epoch+mono,2,*a,*g]);raw=line.getvalue()
        lines.append(raw+','+f'{zlib.crc32(raw.encode()):08x}')
        payload=struct.pack('<IQQQIIIHH9fI',n,mono,mono,epoch+mono,1,1,0xffffffff,1,1,*m,*m,*m,0)
        mag_data.extend(record(10,mono,payload,n))
    parent_csv=root/'parent.csv';parent_csv.write_text('\n'.join(lines),encoding='utf-8');parent_mag=root/'parent_mag.bin';parent_mag.write_bytes(mag_data)
    fitted_parent,parent_motion=fit_recording(parent_body,parent_csv,parent_mag)
    assert fitted_parent['axes']==3 and len(parent_motion)>1000 and parent_motion[0][0]>1e9
    assert max(abs(a-b) for a,b in zip(fitted_parent['mag_q'],quaternion(mr)))<.001
    for role,source,imu_path in [(1,parent_events,parent_csv),(2,events,imu)]:
        camera_events=[]
        for i in [0,9,10,11]:
            payload=bytearray(source[i][24:]);payload[44]=6 if i==0 else payload[44];payload[47]=16 if i>=10 else 0
            camera_events.append(record(13,struct.unpack_from('<Q',payload,16)[0],payload))
        camera_body=root/f'camera{role}.bin';camera_body.write_bytes(header(role,boot)+b''.join(camera_events))
        info,movement=fit_recording(camera_body,imu_path,movement_only=True)
        assert info['format']=='AquaBeaconCameraCapture1' and len(movement)>1000
        fail(lambda:fit_recording(camera_body,imu_path))

def signal(t):return 2+math.sin(1.7*t+.2*math.sin(t))+.8*math.sin(4.31*t)+.8*math.sin(.31*t*t)
target=[(100+i*.02,signal(i*.02)) for i in range(1501)]
camera=[(i*.02,signal(1.00005*i*.02-1.5)) for i in range(1701)]
a,b,q=synchronize(camera,target,35);assert abs(a-1.00005)<.0001 and abs(b-98.5)<.01,(a,b,q)
broken=[(t,signal(t-100+(.2 if 110<t<120 else 0))) for t,_ in target]
fail(lambda:synchronize(camera,broken,35))
fail(lambda:correlate([(i*.02,1.) for i in range(100)],[(i*.02,1.) for i in range(100)],1))
periodic=[(i*.02,2+math.sin(i*.02*math.pi)) for i in range(800)];fail(lambda:correlate(periodic[:250],periodic,4))
print('PASS arbitrary IMU/magnetic rotations, tagged real-format recording fit, CRC/unfinished rejection, camera offset/drift and quiet/periodic rejection')

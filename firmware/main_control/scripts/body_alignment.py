"""Fit sensor-to-FRD rotations from tagged body recordings. Standard library only.

Input files are never modified. This is a geometric consistency fit, not an
absolute attitude/heading/time-accuracy certification. Camera CSV is optional.
"""
import argparse,bisect,csv,itertools,json,math,statistics,struct,zlib
from pathlib import Path
from binary_log import read_header,records,rows

def dot(a,b):return sum(x*y for x,y in zip(a,b))
def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def norm(v):return math.sqrt(dot(v,v))
def unit(v):
    n=norm(v)
    if not math.isfinite(n) or n<1e-8:raise ValueError('Degenerate direction')
    return [x/n for x in v]
def rotate(r,v):return [dot(row,v) for row in r]
def transpose(r):return list(map(list,zip(*r)))
def multiply(a,b):return [[dot(row,col) for col in zip(*b)] for row in a]
IDENTITY=[[1.,0,0],[0,1.,0],[0,0,1.]]
POSES=[IDENTITY,[[0,0,1],[0,1,0],[-1,0,0]],[[1,0,0],[0,0,1],[0,-1,0]],[[0,0,-1],[0,1,0],[1,0,0]]]
EXPECTED=[[0,0,-1],[1,0,0],[0,1,0],[-1,0,0]]
def quaternion(r):
    tr=sum(r[i][i] for i in range(3));q=[0.]*4
    if tr>0:
        s=2*math.sqrt(1+tr);q=[s/4,(r[2][1]-r[1][2])/s,(r[0][2]-r[2][0])/s,(r[1][0]-r[0][1])/s]
    else:
        i=max(range(3),key=lambda i:r[i][i]);j=(i+1)%3;k=(i+2)%3;s=2*math.sqrt(max(0,1+r[i][i]-r[j][j]-r[k][k]))
        if s<1e-8:raise ValueError('Invalid rotation')
        q[0]=(r[k][j]-r[j][k])/s;q[i+1]=s/4;q[j+1]=(r[j][i]+r[i][j])/s;q[k+1]=(r[k][i]+r[i][k])/s
    return unit(q)
def from_vector(v):
    a=norm(v)
    if a<1e-12:return IDENTITY
    x,y,z=[n/a for n in v];c=math.cos(a);s=math.sin(a);d=1-c
    return [[c+x*x*d,x*y*d-z*s,x*z*d+y*s],[y*x*d+z*s,c+y*y*d,y*z*d-x*s],[z*x*d-y*s,z*y*d+x*s,c+z*z*d]]
def solve3(a,b):
    m=[list(row)+[y] for row,y in zip(a,b)]
    for i in range(3):
        pivot=max(range(i,3),key=lambda k:abs(m[k][i]));m[i],m[pivot]=m[pivot],m[i]
        if abs(m[i][i])<1e-12:raise ValueError('Singular fit')
        v=m[i][i];m[i]=[x/v for x in m[i]]
        for j in range(3):
            if j!=i:
                v=m[j][i];m[j]=[x-v*y for x,y in zip(m[j],m[i])]
    return [row[3] for row in m]
def imu_fit(means):
    down=[-x for x in unit(means[0])];forward=unit([x-dot(means[1],down)*d for x,d in zip(means[1],down)])
    right=unit(cross(down,forward));r=[forward,right,down]
    errors=[math.degrees(math.acos(max(-1,min(1,dot(unit(rotate(r,a)),expected))))) for a,expected in zip(means,EXPECTED)]
    if max(errors)>5:raise ValueError('Body poses do not agree within the 5 degree fitting gate')
    return r,errors
def magnetic_fit(means):
    sizes=[norm(v) for v in means];scale=statistics.mean(sizes)
    if scale<15 or scale>100 or max(abs(x/scale-1) for x in sizes)>.08:raise ValueError('Magnetic field magnitude inconsistent; repeat away from magnetic disturbances')
    def residual(r,count=3):
        vectors=[rotate(p,rotate(r,m)) for p,m in zip(POSES[:count],means[:count])]
        center=[statistics.mean(v[i] for v in vectors) for i in range(3)]
        return [x-y for v in vectors for x,y in zip(v,center)],center
    fits=[]
    for perm in itertools.permutations(range(3)):
        for signs in itertools.product([-1,1],repeat=3):
            r=[[float(signs[i]) if j==perm[i] else 0. for j in range(3)] for i in range(3)]
            if dot(cross(r[0],r[1]),r[2])<0:continue
            for _ in range(50):
                e,_=residual(r);cost=dot(e,e);eps=1e-5;cols=[]
                for i in range(3):
                    v=[0.,0,0];v[i]=eps;next_e,_=residual(multiply(from_vector(v),r));cols.append([(a-b)/eps for a,b in zip(next_e,e)])
                a=[[dot(x,y)+(1e-6 if i==j else 0) for j,y in enumerate(cols)] for i,x in enumerate(cols)]
                step=solve3(a,[-dot(col,e) for col in cols]);n=norm(step)
                if n>0.3:step=[x*.3/n for x in step]
                changed=False
                for power in range(10):
                    trial=multiply(from_vector([x/(2**power) for x in step]),r);te,_=residual(trial)
                    if dot(te,te)<cost-1e-12:r=trial;changed=True;break
                if not changed:break
            e,center=residual(r);verification=norm([a-b for a,b in zip(rotate(POSES[3],rotate(r,means[3])),center)])/scale
            fits.append((dot(e,e)/scale**2,verification,r))
    best=min(fits,key=lambda f:f[0]+f[1]**2)
    if math.sqrt(best[0]/3)>.05 or best[1]>.05:raise ValueError('Magnetic body rotation fails pose consistency (5% gate); preserve the forward direction in all poses')
    return best[2],{'relative_fit_rms':math.sqrt(best[0]/3),'fourth_pose_relative_residual':best[1]}

def body_events(path):
    with Path(path).open('rb') as f:
        header=read_header(f);events=[next(rows(r)) for r in records(f) if r['kind']==13]
    required=16 if events and events[0]['event']==6 else 31
    if not events or events[-1]['event']!=4 or events[-1]['mask']!=required:raise ValueError('Body recording not finished')
    first=events[0]
    if first['event'] not in (1,6) or header['session']!=first['boot'] or any(e['event']==5 for e in events):raise ValueError('Invalid/aborted body recording')
    if any(b['marker_mono_us']<a['marker_mono_us'] for a,b in zip(events,events[1:])):raise ValueError('Body marker order')
    for e in events:
        if any(e[k]!=first[k] for k in ['capture','boot','device','imu_calibration','mag_calibration']):raise ValueError('Mixed body recording identity/calibration')
    if not first['imu_calibration'] or not first['mag_calibration']:raise ValueError('Individual calibration missing at recording time')
    windows={}
    for phase in range(1,6):
        if not required&(1<<(phase-1)):continue
        start=[e for e in events if e['event']==2 and e['phase']==phase];end=[e for e in events if e['event']==3 and e['phase']==phase]
        minimum=30_000_000 if required==16 else 5_000_000
        if len(start)!=1 or len(end)!=1 or end[0]['marker_mono_us']-start[0]['marker_mono_us']<minimum:raise ValueError('Incomplete or short phase')
        if end[0]['drops']!=start[0]['drops']:raise ValueError('Source drop counter changed during phase')
        windows[phase]=(start[0],end[0])
    return header,first,windows
def binary_rows(path,kind):
    with Path(path).open('rb') as f:
        header=read_header(f)
        for r in records(f):
            if r['kind']==kind:yield header,next(rows(r))
def phase_mean(values,minimum):
    if len(values)<minimum:raise ValueError('Too few fresh samples in a pose')
    return [statistics.mean(v[i] for v in values) for i in range(3)]
def fit_recording(body,imu,mag=None,movement_only=False):
    header,meta,windows=body_events(body);acc={i:[] for i in range(1,5)};gyr={i:[] for i in range(1,5)};field={i:[] for i in range(1,5)};motion=[]
    if not movement_only and meta['event']==6:raise ValueError('Camera-only capture cannot fit new sensor axes')
    parent=header['role']==1
    if parent:
        lines=Path(imu).read_text(encoding='utf-8-sig').splitlines()
        for line in lines[1:]:
            payload,crc=line.rsplit(',',1)
            if zlib.crc32(payload.encode())!=int(crc,16):raise ValueError('IMU CSV CRC mismatch')
        for v in csv.DictReader(lines):
            if int(v['parent_session'])!=meta['boot']:continue
            if int(v['calibration_generation'])!=meta['imu_calibration']:raise ValueError('IMU calibration changed')
            t=int(v['sample_utc_us'])
            if int(v['time_quality'])!=2 or not t:continue
            a=[float(v['corrected_'+k]) for k in ['ax','ay','az']];g=[float(v['corrected_'+k]) for k in ['gx','gy','gz']]
            for phase,(begin,end) in windows.items():
                if begin['utc_source']!=1 or end['utc_source']!=1:raise ValueError('Parent recording needs GNSS/PPS UTC for cross-device matching')
                if begin['utc_us']+1_000_000<=t<=end['utc_us']-250_000:
                    if phase<5:acc[phase].append(a);gyr[phase].append(g)
                    else:motion.append((t/1e6,norm(g)))
        if not mag and not movement_only:raise ValueError('Parent magnetic observation log required')
        for h,v in binary_rows(mag,10) if mag else []:
            if h['session']!=meta['boot']:raise ValueError('Wrong parent magnetic log session')
            if v['calibration_generation']!=meta['mag_calibration'] or not v['valid'] or v['hardware_overrun']:continue
            for phase,(begin,end) in windows.items():
                if phase<5 and begin['marker_mono_us']+1_000_000<=v['read_end_us']<=end['marker_mono_us']-250_000:field[phase].append([v['corrected_'+k] for k in ['x','y','z']])
    else:
        for h,v in binary_rows(imu,1):
            if h['session']!=meta['boot']:raise ValueError('Wrong child IMU log session')
            t=v['acquired_us'];a=[v[k] for k in ['ax','ay','az']];g=[v[k] for k in ['gx','gy','gz']]
            for phase,(begin,end) in windows.items():
                if begin['marker_mono_us']+1_000_000<=t<=end['marker_mono_us']-250_000:
                    if phase<5:
                        if v['fresh_mask']&2:acc[phase].append(a)
                        if v['fresh_mask']&4:gyr[phase].append(g)
                        if v['fresh_mask']&8:field[phase].append([v[k] for k in ['mx','my','mz']])
                    elif v['fresh_mask']&4:motion.append((t/1e6,norm(g)))
    if movement_only:
        if len(motion)<50 or motion[-1][0]-motion[0][0]<20:raise ValueError('Insufficient camera movement samples')
        info={'format':'AquaBeaconCameraCapture1','device':meta['device'],'capture':meta['capture'],'boot':meta['boot'],'imu':meta['imu_calibration'],'mag':meta['mag_calibration'],'camera_target_clock':'PPS_UTC_us' if parent else 'child_acquired_mono_us','camera':0,'accuracy_verified':False}
        return info,motion
    am=[];mm=[]
    for phase in range(1,5):
        a=phase_mean(acc[phase],20);g=phase_mean(gyr[phase],20);m=phase_mean(field[phase],20)
        if not 9.3<=norm(a)<=10.3 or norm(g)>.03:raise ValueError('Pose not stationary or gravity outside fitting gate')
        if max(statistics.pstdev(v[i] for v in acc[phase]) for i in range(3))>.04:raise ValueError('Acceleration unstable during pose')
        if max(statistics.pstdev(v[i] for v in gyr[phase]) for i in range(3))>.003:raise ValueError('Gyroscope unstable during pose')
        if any(not all(map(math.isfinite,v)) for v in acc[phase]+gyr[phase]+field[phase]):raise ValueError('Nonfinite samples')
        am.append(a);mm.append(m)
    ir,angles=imu_fit(am);mr,quality=magnetic_fit(mm)
    result={'format':'AquaBeaconBodySolution1','device':meta['device'],'capture':meta['capture'],'boot':meta['boot'],'imu':meta['imu_calibration'],'mag':meta['mag_calibration'],'axes':3,'camera':0,'imu_q':quaternion(ir),'mag_q':quaternion(mr),'camera_scale':1.,'camera_offset_us':0.,'quality':{'imu_pose_errors_deg':angles,'magnetic':quality},'accuracy_verified':False,'camera_target_clock':'PPS_UTC_us' if parent else 'child_acquired_mono_us'}
    return result,motion

def correlate(camera,target,search_s=10):
    """Match gyro magnitude. Returns target_seconds = camera_seconds + offset.
    Ambiguous/quiet matches fail; no absolute-time accuracy is inferred.
    """
    if len(camera)<50 or len(target)<50:raise ValueError('Too few gyro samples')
    for stream in [camera,target]:
        if any(not all(map(math.isfinite,v)) for v in stream) or any(b[0]<=a[0] for a,b in zip(stream,stream[1:])):raise ValueError('Invalid gyro timestamps')
        if max(b[0]-a[0] for a,b in zip(stream,stream[1:]))>.1:raise ValueError('Gyro stream has gaps')
    # Relative clocks keep UTC magnitudes out of the search. Both clips must
    # include the marked movement with no more than search_s extra lead-in.
    base=target[0][0]-camera[0][0];ct=[t for t,v in camera];tt=[t for t,v in target]
    def score(delta):
        pairs=[]
        for t,v in camera[::max(1,len(camera)//1000)]:
            j=bisect.bisect_left(tt,t+base+delta)
            if 0<j<len(tt):
                a,b=target[j-1],target[j];w=(t+base+delta-a[0])/(b[0]-a[0]);pairs.append((v,a[1]+w*(b[1]-a[1])))
        if len(pairs)<max(50,len(camera[::max(1,len(camera)//1000)])*.95):return -2
        x=[p[0] for p in pairs];y=[p[1] for p in pairs];mx=statistics.mean(x);my=statistics.mean(y);sx=sum((v-mx)**2 for v in x);sy=sum((v-my)**2 for v in y)
        if min(sx,sy)/len(pairs)<.0001:return -2
        return sum((a-mx)*(b-my) for a,b in pairs)/math.sqrt(sx*sy)
    candidates=[(score(i/100),i/100) for i in range(-int(search_s*100),int(search_s*100)+1)];best=max(candidates)
    if best[0]<.9:raise ValueError('No strong gyro match')
    other=max((s for s,d in candidates if abs(d-best[1])>.5),default=-2)
    if best[0]-other<.03:raise ValueError('Ambiguous/repeated gyro motion')
    fine=max((score(best[1]+i/1000),best[1]+i/1000) for i in range(-10,11))
    return base+fine[1],fine[0]

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--body',required=True);p.add_argument('--imu',required=True);p.add_argument('--mag');p.add_argument('--out',required=True);p.add_argument('--gyro-out');p.add_argument('--movement-only',action='store_true');args=p.parse_args()
    target=Path(args.out)
    if target.exists():raise ValueError('Output already exists')
    if args.gyro_out and Path(args.gyro_out).exists():raise ValueError('Gyro output already exists')
    result,motion=fit_recording(args.body,args.imu,args.mag,args.movement_only)
    if args.gyro_out:
        path=Path(args.gyro_out)
        if path.exists():raise ValueError('Gyro output already exists')
        with path.open('w',newline='',encoding='utf-8') as f:w=csv.writer(f);w.writerow(['time_s','gyro_magnitude']);w.writerows(motion)
        import hashlib
        result['movement_csv_sha256']=hashlib.sha256(path.read_bytes()).hexdigest()
    target.write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps({'output':str(target),'axes':result.get('axes',0),'camera':False,'accuracy_verified':False}))
if __name__=='__main__':main()

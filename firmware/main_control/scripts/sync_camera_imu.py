"""Match an exported camera gyro CSV to the body movement gyro CSV.

Camera CSV: time_s,gx,gy,gz (or time_s,gyro_magnitude). time_s must be
relative to the original video, with its IMU/video correspondence preserved.
Target CSV is produced by body_alignment.py --gyro-out. Keep at least
20 seconds of non-repeating multi-axis movement, with features at both ends.
"""
import argparse,csv,hashlib,json,math
from pathlib import Path
from body_alignment import correlate

def load(path):
    with Path(path).open(encoding='utf-8-sig',newline='') as f:
        out=[]
        for r in csv.DictReader(f):
            v=float(r['gyro_magnitude']) if 'gyro_magnitude' in r else math.sqrt(sum(float(r[k])**2 for k in ['gx','gy','gz']))
            out.append((float(r['time_s']),v))
        return out
def synchronize(camera,target,search_s=120):
    if not target or target[-1][0]-target[0][0]<20:raise ValueError('Need at least 20 seconds of movement for a two-window drift fit')
    width=5;first=[v for v in target if v[0]<=target[0][0]+width];last=[v for v in target if v[0]>=target[-1][0]-width]
    # Each reference window is short. Search the camera excerpt for it; invert
    # the fitted relationship to express target time as a function of camera.
    d0,s0=correlate(first,camera,search_s);d1,s1=correlate(last,camera,search_s)
    t0=(first[0][0]+first[-1][0])/2;t1=(last[0][0]+last[-1][0])/2;c0=t0+d0;c1=t1+d1
    if c1<=c0:raise ValueError('Inconsistent camera window order')
    a=(t1-t0)/(c1-c0);b=t0-a*c0
    if not .999<=a<=1.001:raise ValueError('Clock drift outside fit limits; ambiguous motion or timestamp units')
    mid=(target[0][0]+target[-1][0])/2;middle=[v for v in target if abs(v[0]-mid)<=2.5];dm,sm=correlate(middle,camera,search_s);tm=(middle[0][0]+middle[-1][0])/2;residual=abs(a*(tm+dm)+b-tm)
    if residual>.02:raise ValueError('Middle-window timing disagrees with fitted drift')
    return a,b,{'first_correlation':s0,'last_correlation':s1,'middle_correlation':sm,'middle_residual_s':residual,'reference_span_s':t1-t0,'absolute_accuracy_verified':False}
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--camera',required=True);p.add_argument('--target',required=True);p.add_argument('--solution');p.add_argument('--capture-info');p.add_argument('--out',required=True);p.add_argument('--search-seconds',type=float,default=120);a=p.parse_args()
    if Path(a.out).exists():raise ValueError('Output already exists')
    if not a.solution and not a.capture_info:raise ValueError('Provide --solution or --capture-info')
    solution=json.loads(Path(a.solution).read_text(encoding='utf-8')) if a.solution else {'format':'AquaBeaconCameraTime1'}
    if a.capture_info:
        info=json.loads(Path(a.capture_info).read_text(encoding='utf-8'))
        if info.get('format')!='AquaBeaconCameraCapture1' or info.get('movement_csv_sha256')!=hashlib.sha256(Path(a.target).read_bytes()).hexdigest():raise ValueError('Camera capture/target mismatch')
        if a.solution and any(info.get(k)!=solution.get(k) for k in ['device','imu','mag']):raise ValueError('Axes/camera capture identity mismatch')
        solution['camera_capture']=info
    scale,offset,quality=synchronize(load(a.camera),load(a.target),a.search_seconds)
    solution.update(camera=1,camera_scale=scale,camera_offset_us=offset*1e6,camera_csv_sha256=hashlib.sha256(Path(a.camera).read_bytes()).hexdigest(),camera_fit=quality)
    Path(a.out).write_text(json.dumps(solution,indent=2),encoding='utf-8');print(json.dumps(quality))
if __name__=='__main__':main()

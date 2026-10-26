"""Add body-frame vector columns to decoded IMU/magnetic CSV; preserve source columns."""
import argparse,csv,json,math
from pathlib import Path
def rotate(q,v):
    w,x,y,z=q;vx,vy,vz=v;tx=2*(y*vz-z*vy);ty=2*(z*vx-x*vz);tz=2*(x*vy-y*vx)
    return [vx+w*tx+y*tz-z*ty,vy+w*ty+z*tx-x*tz,vz+w*tz+x*ty-y*tx]
def convert(row,solution):
    out=dict(row);found=False
    for target,keys,q in [('accel',['ax','ay','az'],solution['imu_q']),('gyro',['gx','gy','gz'],solution['imu_q']),('mag',['mx','my','mz'],solution['mag_q'])]:
        if all('corrected_'+k in row for k in keys):keys=['corrected_'+k for k in keys]
        if target=='mag' and all('corrected_'+k in row for k in ['x','y','z']):keys=['corrected_'+k for k in ['x','y','z']]
        if all(k in row for k in keys):
            v=[float(row[k]) for k in keys]
            if not all(map(math.isfinite,v)):raise ValueError('Non-finite vector')
            for axis,n in zip('xyz',rotate(q,v)):out['body_'+target+'_'+axis]=n
            found=True
    if not found:raise ValueError('No supported vector columns; decode binary log first')
    return out
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--solution',required=True);p.add_argument('--input',required=True);p.add_argument('--out',required=True);a=p.parse_args()
    if Path(a.out).exists():raise ValueError('Output already exists')
    s=json.loads(Path(a.solution).read_text(encoding='utf-8'))
    if s.get('format')!='AquaBeaconBodySolution1' or s.get('axes')!=3:raise ValueError('Complete body solution required')
    for q in [s['imu_q'],s['mag_q']]:
        if len(q)!=4 or not all(map(math.isfinite,q)) or abs(sum(x*x for x in q)-1)>.001:raise ValueError('Invalid quaternion')
    with Path(a.input).open(encoding='utf-8-sig',newline='') as f:out=[convert(r,s) for r in csv.DictReader(f)]
    if not out:raise ValueError('No rows')
    with Path(a.out).open('w',encoding='utf-8',newline='') as f:w=csv.DictWriter(f,fieldnames=out[0]);w.writeheader();w.writerows(out)
if __name__=='__main__':main()

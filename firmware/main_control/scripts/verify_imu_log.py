import csv,zlib,statistics,argparse,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('file');p.add_argument('--hz',type=int,default=50);a=p.parse_args()
lines=Path(a.file).read_bytes().splitlines();assert lines
for i,line in enumerate(lines[1:],2):
 body,checksum=line.rsplit(b',',1)
 assert zlib.crc32(body)==int(checksum,16),f'CRC line {i}'
rows=list(csv.DictReader(x.decode() for x in lines));assert len(rows)>10
period=1_000_000//a.hz
ts=[int(r['scheduled_us']) for r in rows];actual=[int(r['acquired_us']) for r in rows];source=[int(r['source_us']) for r in rows]
dt=[b-a for a,b in zip(ts,ts[1:])];assert all(d>=period and d%period==0 for d in dt)
lateness=[b-a for a,b in zip(ts,actual)];assert min(lateness)>=0
fresh=[int(r['fresh_mask']) for r in rows];unique=sorted(set(t for t in source if t>0));rate=(len(unique)-1)*1e6/(unique[-1]-unique[0]) if len(unique)>1 else 0
report={'file':a.file,'rows':len(rows),'duration_s':(ts[-1]-ts[0])/1e6,'grid_hz':a.hz,'missing_grid_rows':sum(d//period-1 for d in dt),'max_lateness_us':max(lateness),'median_lateness_us':statistics.median(lateness),'all_three_fresh_rows':sum((f&7)==7 for f in fresh),'new_quat_rows':sum(bool(f&1) for f in fresh),'unique_source_times':len(unique),'source_unique_rate_hz':rate,'crc_errors':0}
print(json.dumps(report,indent=2));assert len(unique)>10,'No real IMU timestamps'

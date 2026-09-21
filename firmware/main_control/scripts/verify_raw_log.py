import csv,zlib,statistics,argparse,json
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('file');a=p.parse_args()
lines=Path(a.file).read_bytes().splitlines()
for i,line in enumerate(lines[1:],2):
 body,crc=line.rsplit(b',',1);assert zlib.crc32(body)==int(crc,16),f'CRC row {i}'
rows=list(csv.DictReader(x.decode() for x in lines));groups={}
for row in rows:groups.setdefault(int(row['sensor_id']),[]).append(row)
summary={}
for kind,data in groups.items():
 seq=[int(x['sequence']) for x in data];ts=[int(x['source_us']) for x in data];host=[int(x['acquired_us']) for x in data]
 gaps=sum(((b-a)&255)-1 for a,b in zip(seq,seq[1:]) if ((b-a)&255)>1)
 dt=[b-a for a,b in zip(ts,ts[1:])]
 summary[kind]={'rows':len(data),'sequence_gaps':gaps,'source_rate_hz':(len(data)-1)*1e6/(ts[-1]-ts[0]),'host_rate_hz':(len(data)-1)*1e6/(host[-1]-host[0]),'min_source_step_us':min(dt),'max_source_step_us':max(dt),'median_source_step_us':statistics.median(dt)}
 assert all(d>0 for d in dt),'Nonmonotonic sensor time'
print(json.dumps({'file':a.file,'crc_errors':0,'sensors':summary},indent=2));assert set(groups)=={1,2,3,8}

assert all(s["sequence_gaps"]==0 for s in summary.values()), "Sensor sequence gaps present"

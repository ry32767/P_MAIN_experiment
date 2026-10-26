"""Verify saved Spresense CSV and application calibration records offline."""
import argparse, csv, io, json, math, struct, zlib
from pathlib import Path


def coefficients(path):
    data=Path(path).read_bytes()
    assert len(data)==64 and data[:4]==b'SCAL' and data[4]==1 and data[5] in (0,1)
    assert data[6:8]==b'\0\0' and data[56:60]==b'\0'*4
    assert zlib.crc32(data[:60])==struct.unpack_from('<I',data,60)[0], 'Calibration CRC'
    generation=struct.unpack_from('<I',data,8)[0]
    gravity,latitude,*values=struct.unpack_from('<11f',data,12)
    assert generation and all(map(math.isfinite,[gravity,latitude,*values]))
    assert 9.7<=gravity<=9.9 and abs(latitude)<=90
    assert all(abs(x)<=.5 for x in values[:3]) and all(.95<=x<=1.05 for x in values[3:6])
    assert all(abs(x)<=.02 for x in values[6:])
    return generation, bool(data[5]), values


def verify(path, records=()):
    catalog={}
    for record in records:
        generation,enabled,values=coefficients(record)
        assert generation not in catalog, 'Duplicate calibration generation'
        catalog[generation]=(enabled,values)
    lines=Path(path).read_text(encoding='utf-8-sig').splitlines()
    assert len(lines)>1, 'No samples'
    for n,line in enumerate(lines[1:],2):
        body,crc=line.rsplit(',',1)
        assert zlib.crc32(body.encode())==int(crc,16), f'CSV CRC line {n}'
    rows=list(csv.DictReader(io.StringIO('\n'.join(lines))))
    assert all(int(b['seq'])>int(a['seq']) and int(b['received_mono_us'])>=int(a['received_mono_us']) for a,b in zip(rows,rows[1:])), 'Sample order'
    corrected='calibration_generation' in rows[0]
    checked=0
    for row in rows:
        raw=[float(row[k]) for k in ('gx','gy','gz','ax','ay','az')]
        assert all(map(math.isfinite,raw)), 'Non-finite raw sample'
        if not corrected:
            continue
        output=[float(row['corrected_'+k]) for k in ('gx','gy','gz','ax','ay','az')]
        assert all(map(math.isfinite,output)), 'Non-finite corrected sample'
        generation=int(row['calibration_generation'])
        assert generation==0 or generation in catalog, f'Missing calibration record {generation}'
        enabled,values=catalog.get(generation,(False,[]))
        expected=raw if not enabled else [raw[i]-values[6+i] for i in range(3)]+[(raw[3+i]-values[i])*values[3+i] for i in range(3)]
        assert all(abs(a-b)<=2e-5+abs(b)*2e-6 for a,b in zip(output,expected)), 'Correction mismatch'
        checked+=1
    timed='time_quality' in rows[0]
    if timed:
        previous=None
        for row in rows:
            quality=int(row['time_quality']);sample=int(row['sample_mono_us']);epoch=int(row['sample_utc_us'])
            assert quality in (0,1,2) and (quality==2)==bool(epoch), 'Timestamp quality/UTC mismatch'
            assert bool(sample)==(quality>0), 'Missing or unqualified sample time'
            assert not sample or sample<=int(row['received_mono_us']), 'Sample after readout'
            assert int(row['time_uncertainty_us'])==0xffffffff, 'Unmeasured latency must remain unknown'
            identity=(int(row['parent_session']),int(row['clock_generation']))
            if previous and previous[0]==identity and sample and previous[1]:assert sample>previous[1], 'Nonmonotonic sample time'
            previous=(identity,sample)
    return {'timing_schema':3 if timed else 2 if corrected else 1,'rows':len(rows),'corrected_rows_checked':checked,'generations':sorted(catalog),'crc_errors':0,'precision_verified':False}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv');parser.add_argument('--calibration',action='append',default=[])
    args=parser.parse_args()
    print(json.dumps(verify(args.csv,args.calibration),indent=2))

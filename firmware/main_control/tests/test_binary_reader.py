from pathlib import Path
import sys,io,struct,zlib,tempfile,csv
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from binary_log import read_header,records,rows,csv_lines
from decode_binary import decode
p=Path(sys.argv[1]);data=p.read_bytes();f=io.BytesIO(data);assert read_header(f)['session']==123
r=list(records(f));assert len(r)==9
d=[list(rows(x)) for x in r];assert d[0][0]['qw']==1.25 and d[0][0]['mz']==-9.5
assert d[1][0]['sequence']==255 and d[1][0]['sensor_id']==8
assert d[2][0]['observation_samples']==60000 and d[2][0]['offset_us']==-1234 and d[2][0]['frequency_hz']==50000 and d[2][0]['leak']==1
assert d[3][0]['phone_latitude']==35.1234567 and d[3][0]['phone_accuracy_m']==1.25
assert [x['env_adc'] for x in d[6]]==[0,4095,1] and d[6][2]['adc_error']
assert [x['sample_index'] for x in d[6]]==[7,8,9]
assert d[6][2]['mono_us']-d[6][0]['mono_us']==100
assert d[8][0]['reason']==0
def rejected(blob):
    f=io.BytesIO(blob)
    try:read_header(f);list(records(f))
    except ValueError:return
    raise AssertionError('Accepted invalid log')
for index in (0,28,32,52,56,len(data)-1):
    b=bytearray(data);b[index]^=1;rejected(b)
for cut in (1,31,33,55,len(data)-1):rejected(data[:cut])
f=io.BytesIO(data[:-1]);read_header(f);report={};assert len(list(records(f,True,report)))==8 and report['truncated']
lines=csv_lines(p,1);assert zlib.crc32(lines[1].rsplit(b',',1)[0])==int(lines[1].rsplit(b',',1)[1],16)
with tempfile.TemporaryDirectory() as folder:
    result=decode(p,folder);assert not result['truncated'] and len(list(Path(folder).glob('*.csv')))==9
    assert not result['captures'][42]['complete'] and result['captures'][42]['index_discontinuities']==1
    envelope=list(csv.DictReader(next(Path(folder).glob('*.envelope.csv')).open()));assert len(envelope)==3
# A small, genuinely complete observation; length-valid duplicate sample indices stay incomplete.
def packed(kind,payload,mono=123):
    header=struct.pack('<IHHQI',0x52425141,kind,len(payload),mono,42)
    return header+struct.pack('<I',zlib.crc32(payload,zlib.crc32(header)))+payload
begin=bytearray(r[5]['payload']);struct.pack_into('<II',begin,40,7,3)
block=bytearray(r[6]['payload']);struct.pack_into('<H',block,16,1)
end=struct.pack('<5IB',42,3,0,0,0,0)
complete=data[:32]+packed(6,begin)+packed(7,block)+packed(9,end)
with tempfile.TemporaryDirectory() as folder:
    fixture=Path(folder)/'complete.bin';fixture.write_bytes(complete)
    assert decode(fixture)['captures'][42]['complete']
    fixture.write_bytes(data[:32]+packed(6,begin)+packed(7,block)+packed(7,block)+packed(9,end))
    assert not decode(fixture)['captures'][42]['complete']
print('PASS C++/Python schema, all 9 record types, CRC corruption/truncation, ADC error/index gaps, CSV export and legacy verifier view')

assert d[4][0]['ranging_rate_hz']==4 and d[4][0]['acoustic_pattern']==1
for kind in (3,5):
    old=next(x for x in r if x['kind']==kind)['payload'][:-7]
    legacy=list(records(io.BytesIO(packed(kind,old))))[0]
    view=list(rows(legacy))[0]
    assert view['ranging_rate_hz']==1 and view['acoustic_pattern']==0
print('PASS new rate/pattern metadata and old binary payload compatibility')

assert d[4][0]['acoustic_pattern_id']==17 and d[4][0]['pattern_gap1_us']==1600 and d[4][0]['pattern_gap2_us']==8200
for kind in (3,5):
    previous=next(x for x in r if x['kind']==kind)['payload'][:-5]
    view=list(rows(next(records(io.BytesIO(packed(kind,previous))))))[0]
    assert view['acoustic_pattern_id'] is None
print('PASS variable-pattern metadata and both previous binary payload lengths')

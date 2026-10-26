"""Read the real C++ wire structs, and reject damaged calibration metadata."""
from pathlib import Path
import sys,io,struct,zlib,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from binary_log import read_header,records,rows
from decode_binary import decode
blob=Path(sys.argv[1]).read_bytes();f=io.BytesIO(blob);assert read_header(f)['session']==987
data=list(records(f));assert [r['kind'] for r in data]==[10,11,12]
mag,profile,gnss=[list(rows(r))[0] for r in data]
assert mag['sample_sequence']==19 and mag['raw_x']==12 and mag['corrected_y']==-34 and mag['body_z']==56
assert mag['valid'] and mag['hardware_overrun'] and mag['hardware_status']==0x88
assert mag['time_uncertainty_us']==0xffffffff and mag['parent_set_generation']==9
assert profile['cad']=='rev1' and profile['frame']=='FRD' and profile['imu_known']==1 and profile['imu_qz']>.7
assert gnss['itow_ms']==123456 and gnss['lat_e7']==351234567 and gnss['vn_mm_s']==-123
assert gnss['received_us']==12345
# Correct outer framing but corrupt the embedded profile CRC: must still reject.
bad=bytearray(blob);offset=data[1]['offset'];bad[offset+24+76]^=1
prefix=bad[offset:offset+20];payload=bad[offset+24:offset+24+376]
struct.pack_into('<I',bad,offset+20,zlib.crc32(payload,zlib.crc32(prefix)))
try:
 f=io.BytesIO(bad);read_header(f);list(records(f))
except ValueError:pass
else:raise AssertionError('Accepted corrupt profile CRC')
with tempfile.TemporaryDirectory() as folder:
 result=decode(sys.argv[1],folder);assert result['crc_errors']==0 and len(list(Path(folder).glob('*.csv')))==3
print('PASS C++/Python MAG, GNSS epoch/sequence and calibration-set ABI; embedded CRC and CSV export')

from verify_spresense_log import verify
import csv
imu_path=Path(sys.argv[1]).parent/'spresense_schema3_fixture.csv'
assert verify(imu_path)['timing_schema']==3
# Preserve a valid row CRC but lie about UTC quality: verifier must refuse it.
lines=imu_path.read_text().splitlines();header=lines[0].split(',');values=lines[1].split(',')
values[header.index('time_quality')]='0'
body=','.join(values[:-1]);values[-1]=f'{zlib.crc32(body.encode()):08x}'
with tempfile.TemporaryDirectory() as directory:
    bad=Path(directory)/'invalid.csv';bad.write_text(lines[0]+'\n'+','.join(values)+'\n')
    try:verify(bad)
    except AssertionError:pass
    else:raise AssertionError('Accepted UTC on an unsynchronised row')
print('PASS actual Spresense schema3 CSV and timestamp-quality rejection')

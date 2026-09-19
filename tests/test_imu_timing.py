import sys,tempfile,unittest,zlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from analyze_imu_capture import analyze
class TimingTests(unittest.TestCase):
 def test_rate_and_timestamp_wrap(self):
  lines=['seq,received_mono_us,sensor_timestamp_raw,temp,gx,gy,gz,ax,ay,az,crc32\n']
  for i,ts in enumerate([0xfffffff0,19984,59984]):
   row=f'{i+1},{1000000+i*1042},{ts},25,0,0,0,0,0,9.8';lines.append(f'{row},{zlib.crc32(row.encode()):08x}\n')
  with tempfile.TemporaryDirectory() as d:
   p=Path(d)/'imu.csv';p.write_text(''.join(lines))
   self.assertFalse(analyze(p,960)['validation']['warnings'])
   self.assertEqual(analyze(p,960)['sensor_interval_ms']['above_1_5_nominal_count'],1)
   self.assertEqual(analyze(p,120)['sensor_interval_ms']['above_1_5_nominal_count'],0)
   with self.assertRaises(ValueError):analyze(p,1000)
if __name__=='__main__':unittest.main()

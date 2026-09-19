import importlib.util
from pathlib import Path
import tempfile
import unittest
import zlib

spec = importlib.util.spec_from_file_location('verify_logs', Path(__file__).resolve().parents[1]/'scripts/verify_logs.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class LogTests(unittest.TestCase):
    def check(self, content):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'test.csv'
            path.write_text(content, encoding='utf-8')
            return module.verify(path)

    @staticmethod
    def row(text):
        return f'{text},{zlib.crc32(text.encode()):08x}\n'

    def test_valid_imu_and_corruption(self):
        header='seq,received_mono_us,sensor_timestamp_raw,temp,gx,gy,gz,ax,ay,az,crc32\n'
        content=header+self.row('1,1000000,10,25,0,0,0,0,0,9.8')+self.row('2,1008333,20,25,0,0,0,0,0,9.8')
        self.assertFalse(self.check(content)['errors'])
        self.assertTrue(self.check(content.replace('9.8','9.9',1))['errors'])

    def test_truncation_and_gap(self):
        header='seq,received_mono_us,sensor_timestamp_raw,temp,gx,gy,gz,ax,ay,az,crc32\n'
        content=header+self.row('1,1000000,10,25,0,0,0,0,0,9.8')
        self.assertTrue(self.check(content.rstrip())['errors'])
        content+=self.row('3,1016666,30,25,0,0,0,0,0,9.8')
        self.assertTrue(self.check(content)['errors'])

    def test_pico_valid_and_fault_counter(self):
        header='row,mono_us,utc_us,sync,pps_period_us,sd_errors,crc32\n'
        good=self.row('0,3000000,1800000000000000,1,1000000,0')
        self.assertFalse(self.check(header+good)['errors'])
        self.assertTrue(self.check(header+self.row('0,3000000,1800000000000000,1,1000000,1'))['errors'])
        self.assertTrue(self.check(header+self.row('0,3000000,1800000000000000,0,1000000,0'))['errors'])

    def test_empty_and_unrecognized(self):
        self.assertTrue(self.check('')['errors'])
        self.assertTrue(self.check('seq,received_mono_us,sensor_timestamp_raw\n')['errors'])


if __name__ == '__main__':
    unittest.main()

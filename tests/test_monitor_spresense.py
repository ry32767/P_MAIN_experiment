"""Monitoring must never send a stop command, including on early exit."""
import importlib.util
import itertools
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch


class MonitorTests(unittest.TestCase):
    def run_capture(self, interrupted=False, valid=False):
        class Port:
            in_waiting = 0
            closed = False
            writes = []
            def open(self):
                assert self.dtr is False and self.rts is False
            def close(self):
                self.closed = True
            def write(self, data):
                self.writes.append(data)
                raise AssertionError('Read-only monitoring attempted a serial write')
            def read(self, count):
                if interrupted:
                    raise KeyboardInterrupt
                return (b'# imu=10 sd=1 sd_errors=0\n'
                        b'# pps_count=10 utc_sync_valid=1\n'
                        b'# gps_seq=10 flags=15\n')
        port = Port()
        serial = types.ModuleType('serial')
        serial.Serial = lambda *args, **kwargs: port
        listing = types.ModuleType('serial.tools.list_ports')
        listing.comports = lambda: [types.SimpleNamespace(device='COM_TEST', vid=0x10c4, pid=0xea60)]
        with tempfile.TemporaryDirectory() as directory, patch.dict(sys.modules, {
            'serial': serial, 'serial.tools': types.ModuleType('serial.tools'),
            'serial.tools.list_ports': listing,
        }):
            path = Path(__file__).resolve().parents[1] / 'scripts/monitor_spresense.py'
            spec = importlib.util.spec_from_file_location('monitor_test_target', path)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            output = Path(directory) / 'capture.txt'
            args = ['monitor', '--port', 'COM_TEST', '--output', str(output),
                    '--seconds', '2', '--valid-seconds', '.1' if valid else '0']
            with patch.object(sys, 'argv', args), patch.object(module.time, 'monotonic',
                    side_effect=itertools.count(0, .1)), patch('builtins.print'):
                if interrupted:
                    with self.assertRaises(KeyboardInterrupt):
                        module.main()
                else:
                    module.main()
                    self.assertIn('flags=15', output.read_text())
            self.assertTrue(port.closed)
            self.assertEqual(port.writes, [])

    def test_timeout_keeps_device_recording(self):
        self.run_capture()

    def test_gps_success_keeps_device_recording(self):
        self.run_capture(valid=True)

    def test_interrupt_keeps_device_recording(self):
        self.run_capture(interrupted=True)


if __name__ == '__main__':
    unittest.main()

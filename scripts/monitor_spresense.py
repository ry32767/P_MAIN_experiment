"""Read-only Sony status capture. Never sends q, resets, or stops SD recording."""
import argparse
from pathlib import Path
import re
import time
import serial
from serial.tools.list_ports import comports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=60)
    parser.add_argument('--valid-seconds', type=float, default=0,
                        help='Finish observation after this many consecutive seconds of GPS/PPS/SD validity')
    args = parser.parse_args()
    ports = [p for p in comports() if p.device == args.port and
             p.vid == 0x10c4 and p.pid == 0xea60]
    if len(ports) != 1:
        parser.error('Selected port is not the expected Sony CP210x interface')
    if args.seconds <= 0 or args.valid_seconds < 0:
        parser.error('Invalid duration')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    port = serial.Serial(None, 115200, timeout=.2)
    port.dtr = False
    port.rts = False
    port.port = args.port
    started = time.monotonic()
    valid_since = None
    last_gps = None
    pending = bytearray()
    values = {}
    latest = {}
    next_print = started
    # Exclusive creation prevents overwriting earlier evidence.
    with args.output.open('xb', buffering=0) as output:
        port.open()
        try:
            while time.monotonic() - started < args.seconds:
                data = port.read(port.in_waiting or 1)
                output.write(data)
                pending.extend(data)
                while b'\n' in pending:
                    line, _, pending = pending.partition(b'\n')
                    line = line.decode(errors='replace').strip()
                    for prefix, key in [('# imu=', 'imu'), ('# pps_count=', 'pps'),
                                        ('# gps_seq=', 'gps'), ('# recording=', 'recording')]:
                        if line.startswith(prefix):
                            latest[key] = line
                            values.update({k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', line)})
                    if line.startswith('# gps_seq='):
                        last_gps = time.monotonic()
                        good = (values.get('flags', 0) & 5) == 5 and values.get('sd') == 1 and \
                            values.get('utc_sync_valid') == 1 and values.get('sd_errors') == 0
                        valid_since = (valid_since or time.monotonic()) if good else None
                now = time.monotonic()
                if last_gps is None or now-last_gps > 2:
                    valid_since = None
                if now >= next_print:
                    print(round(now-started, 1), latest, flush=True)
                    next_print = now + 15
                if args.valid_seconds and valid_since and now-valid_since >= args.valid_seconds:
                    print('GPS_PPS_SD_VALID; observation finished, recording remains active', flush=True)
                    break
        finally:
            port.close()  # No serial command is sent, even on interruption.


if __name__ == '__main__':
    main()

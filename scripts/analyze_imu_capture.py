"""Summarize exported IMU timing; this does not certify sensor accuracy."""
import argparse
import csv
import json
from pathlib import Path
import statistics

from verify_logs import verify


def analyze(path, rate=120):
    if rate not in (15,30,60,120,240,480,960,1920):
        raise ValueError("Unsupported IMU rate")
    structural = verify(path,expected_imu_rate=rate)
    if structural['errors'] or structural.get('kind') != 'imu':
        raise ValueError(f'Invalid IMU capture: {structural}')
    with path.open(newline='') as source:
        rows = list(csv.DictReader(source))
    if len(rows) < 2:
        raise ValueError('At least two samples required')
    stamps = [int(r['sensor_timestamp_raw']) for r in rows]
    steps = [(b-a) & 0xffffffff for a, b in zip(stamps, stamps[1:])]
    intervals = [step/19200 for step in steps]  # Sony reference: 19.2 MHz.
    received = [int(r["received_mono_us"]) for r in rows]
    receive_intervals = [(b-a)/1000 for a,b in zip(received,received[1:])]
    values = {}
    for key in ('temp', 'gx', 'gy', 'gz', 'ax', 'ay', 'az'):
        v = [float(row[key]) for row in rows]
        values[key] = {'mean': statistics.mean(v), 'sample_stddev': statistics.stdev(v),
                       'min': min(v), 'max': max(v)}
    return dict(requested_rate_hz=rate, validation=structural, sensor_interval_ms=dict(
        median=statistics.median(intervals), min=min(intervals), max=max(intervals),
        mean=statistics.mean(intervals), zero_count=steps.count(0),
        above_1_5_nominal_count=sum(step > 19200000 / rate * 1.5 for step in steps)),
        sensor_interval_stddev_ms=statistics.pstdev(intervals),
        sensor_rate_hz=19200000*len(steps)/sum(steps) if sum(steps) else None,
        received_interval_ms=dict(min=min(receive_intervals),max=max(receive_intervals),
            median=statistics.median(receive_intervals),stddev=statistics.pstdev(receive_intervals)),
        native_driver_values=values,
        limitations='Motion and calibrated reference not confirmed; summary is not an accuracy result.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('file', type=Path)
    parser.add_argument('--rate', type=int, default=120)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(analyze(args.file,args.rate), indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()

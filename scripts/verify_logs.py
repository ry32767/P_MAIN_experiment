"""Validate actual exported CSV logs, streaming without a size limit.

Exit 0 means structural checks passed; it is not a timing-accuracy certificate.
Use --require-sync for outdoor P-MAIN acceptance runs after GNSS warm-up.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import zlib


def verify(path, require_sync=False):
    path = Path(path)
    result = dict(file=path.name, rows=0, errors=[], warnings=[])
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
    result['sha256'] = digest.hexdigest()
    prev_seq = prev_time = first_time = last_time = None
    sync_rows = 0
    last_counters = {}
    with path.open(encoding='utf-8-sig', newline='') as source:
        header = next(csv.reader([source.readline()]), [])
        if 'mono_us' in header:
            kind, seq_key, time_key = 'pico', 'row', 'mono_us'
        elif 'sensor_timestamp_raw' in header:
            kind, seq_key, time_key = 'imu', 'seq', 'received_mono_us'
        elif 'nav_usec' in header:
            kind, seq_key, time_key = 'gps', 'seq', 'received_mono_us'
        else:
            result['errors'].append('Unrecognized CSV header')
            return result
        result['kind'] = kind
        for line_number, raw in enumerate(source, 2):
            try:
                if not raw.endswith('\n'):
                    raise ValueError('incomplete final line')
                values = next(csv.reader([raw]))
                if len(values) != len(header):
                    raise ValueError('column count mismatch')
                row = dict(zip(header, values))
                if 'crc32' in row:
                    text, checksum = raw.rstrip('\r\n').rsplit(',', 1)
                    if zlib.crc32(text.encode()) != int(checksum, 16):
                        raise ValueError('CRC32 mismatch')
                seq, stamp = int(row[seq_key]), int(row[time_key])
                if prev_seq is not None and seq != ((prev_seq + 1) & 0xffffffff):
                    raise ValueError(f'sequence gap {prev_seq} -> {seq}')
                if prev_time is not None and stamp < prev_time:
                    raise ValueError('monotonic timestamp moved backwards')
                prev_seq, prev_time = seq, stamp
                if first_time is None:
                    first_time = stamp
                last_time = stamp
                if kind == 'imu':
                    if not all(math.isfinite(float(row[k])) for k in ('temp', 'gx', 'gy', 'gz', 'ax', 'ay', 'az')):
                        raise ValueError('non-finite IMU reading')
                if kind == 'pico':
                    synced = int(row['sync']) == 1
                    sync_rows += synced
                    if synced and (int(row['utc_us']) <= 0 or not 999000 <= int(row['pps_period_us']) <= 1001000):
                        raise ValueError('invalid synchronized timestamp/PPS interval')
                    if not synced and int(row['utc_us']) != 0:
                        raise ValueError('UTC advertised while not synchronized')
                    for key in ('sd_errors', 'sp_sd_errors', 'sp_imu_errors', 'sp_imu_gaps', 'crc_errors', 'missed_rows', 'rx_overflows'):
                        last_counters[key] = max(last_counters.get(key, 0), int(row.get(key, 0)))
                result['rows'] += 1
            except (ValueError, KeyError, csv.Error) as exc:
                if len(result['errors']) < 30:
                    result['errors'].append(f'line {line_number}: {exc}')
    if not result['rows']:
        result['errors'].append('No valid data rows')
    result['duration_s'] = (last_time-first_time)/1e6 if first_time is not None else 0
    if kind == 'pico':
        result['sync_fraction'] = sync_rows/max(1, result['rows'])
        result['counters'] = last_counters
        for key, value in last_counters.items():
            if value:
                result['errors'].append(f'{key}={value}')
        if require_sync and result['sync_fraction'] < 0.95:
            result['errors'].append('PPS association below 95%; warm up GNSS before recording acceptance run')
        elif not require_sync and result['sync_fraction'] < 0.95:
            result['warnings'].append('PPS association below 95%; indoor/no-fix logs may legitimately be unsynchronized')
    if require_sync and kind != 'pico':
        result['warnings'].append('--require-sync applies only to P-MAIN logs')
    if kind == 'imu' and result['duration_s']:
        result['observed_rate_hz'] = (result['rows']-1)/result['duration_s']
        if not 115 <= result['observed_rate_hz'] <= 125:
            result['warnings'].append('Received rate outside 115..125 Hz; inspect FIFO losses and receipt batching')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('files', nargs='+', type=Path)
    parser.add_argument('--require-sync', action='store_true')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    results = []
    for path in args.files:
        try:
            results.append(verify(path, args.require_sync))
        except (OSError, UnicodeError) as exc:
            results.append(dict(file=str(path), errors=[str(exc)]))
    text = json.dumps(results, ensure_ascii=False, indent=2)
    print(text)
    if args.output:
        args.output.write_text(text+'\n', encoding='utf-8')
    return int(any(item['errors'] for item in results))


if __name__ == '__main__':
    raise SystemExit(main())

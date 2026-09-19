"""Render actual private captures; no fabricated data or location disclosure."""
import csv
import json
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

HERE = Path(__file__).resolve().parent
RAW = HERE.parent / 'raw' / HERE.name
fig, axes = plt.subplots(1, 2, figsize=(10, 3.8), layout='constrained')
rates = []
for label, name, evidence, color in [('Before', 'I_export.CSV', 'baseline.json', '#cc7042'),
                                     ('Priority 150', 'I_priority.CSV', 'priority.json', '#227c9d')]:
    result = json.loads((HERE / 'evidence' / evidence).read_text())
    rates.append(result['validation']['observed_rate_hz'])
    with (RAW / name).open() as f:
        rows = list(csv.DictReader(f))
    stamps = [int(row['sensor_timestamp_raw']) for row in rows]
    delta = [((b-a) & 0xffffffff)/19200 for a,b in zip(stamps, stamps[1:])]
    axes[1].hist(delta, bins=80, range=(0,100), alpha=.6, label=label, color=color,
                 weights=[100/len(delta)]*len(delta))
axes[0].bar(['Before', 'Priority 150'], rates, color=['#cc7042','#227c9d'])
axes[0].axhline(120, color='black', linestyle='--', label='Configured 120 Hz')
for i, rate in enumerate(rates):
    axes[0].text(i, rate+2, f'{rate:.2f}', ha='center')
axes[0].set(ylim=(0,145), ylabel='Received samples / second', title='Actual SD capture rate')
axes[0].legend(loc='lower right')
axes[1].set(xlabel='Sensor timestamp interval (ms)', ylabel='Intervals (%)',
            title='19.2 MHz counter; full 0–100 ms range')
axes[1].legend()
fig.suptitle('Spresense IMU logging: before / after scheduling change')
(HERE/'figures').mkdir(exist_ok=True)
fig.savefig(HERE/'figures'/'imu_timing.png', dpi=160)
fig.savefig(HERE/'figures'/'imu_timing.svg')

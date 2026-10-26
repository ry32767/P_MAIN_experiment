from pathlib import Path
import json,csv,hashlib
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
r=Path(__file__).resolve().parents[1];report=r/'reports/2026-09-20_001_integration';(report/'figures').mkdir(exist_ok=True)
data=list(csv.DictReader((r/'reports/raw/final_imu100_repeat.csv').open(encoding='utf-8')))
t0=int(data[0]['scheduled_us']);t=[(int(x['scheduled_us'])-t0)/1e6 for x in data];late=[(int(x['acquired_us'])-int(x['scheduled_us']))/1000 for x in data]
raw=list(csv.DictReader((r/'reports/raw/final_raw100_repeat.csv').open(encoding='utf-8')))
fig,ax=plt.subplots(2,1,figsize=(9,6),layout='constrained')
ax[0].plot(t,late,color='#087d86',linewidth=.7);ax[0].set(xlabel='Time from first scheduled sample (s)',ylabel='Sampling lateness (ms)',title='100 Hz fixed-grid recording / actual SD readback');ax[0].grid(alpha=.2)
for sid,label in [(8,'Rotation'),(1,'Acceleration'),(2,'Gyroscope'),(3,'Magnetic field')]:
 rows=[x for x in raw if int(x['sensor_id'])==sid];base=int(rows[0]['source_us']);x=[(int(v['source_us'])-base)/1e6 for v in rows];y=list(range(1,len(rows)+1));ax[1].plot(x,y,label=label,linewidth=1.2)
ax[1].set(xlabel='Time from first sensor event (s)',ylabel='Saved events',title='All sensor events saved independently of the fixed grid');ax[1].grid(alpha=.2);ax[1].legend()
fig.savefig(report/'figures/imu_verification.png',dpi=160);plt.close(fig)
manifest={}
for folder in ['src','include','common','web','lib','scripts','tests','docs']:
 for p in (r/folder).rglob('*'):
  if p.is_file() and '__pycache__' not in p.parts:manifest[str(p.relative_to(r))]=hashlib.sha256(p.read_bytes()).hexdigest()
for p in [r/'platformio.ini',r/'README.md',r/'AGENTS.md']:manifest[str(p.relative_to(r))]=hashlib.sha256(p.read_bytes()).hexdigest()
(report/'evidence/firmware_manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
for f in ['final_imu100_repeat_analysis.json','final_raw100_repeat_analysis.json','web_test_result.json']:
 (report/'evidence'/f).write_bytes((r/'reports/raw'/f).read_bytes())
print('Figure, analysis evidence, and source manifest saved')

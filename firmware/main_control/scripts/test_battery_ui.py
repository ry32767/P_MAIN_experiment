from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[2]
pages=[root/"AquaBeacon_MAIN_firmware/web/index.html",root/"P_MAIN_pages_publish/web/main-control/index.html"]
blocks=[p.read_text(encoding="utf-8").split("// BATTERY_ESTIMATE_BEGIN")[1].split("// BATTERY_ESTIMATE_END")[0] for p in pages]
assert blocks[0]==blocks[1]
subprocess.run([r"C:\Program Files\nodejs\node.exe"],input=blocks[0]+"\nconst assert=require('node:assert/strict');\nconst h=v=>({flags:17,bus_v:v});\nassert.equal(batteryEstimate(h(12.6)).percent,100);\nassert.equal(batteryEstimate(h(10.5)).percent,0);\nassert.equal(batteryEstimate(h(11.55)).percent,50);\nassert.equal(batteryEstimate(h(10.8)).low,true);\nassert.equal(batteryEstimate(h(9)).percent,0);\nfor(const v of [null,undefined,NaN,Infinity,'12',4.64,0,13])assert.equal(batteryEstimate(h(v)).percent,null);\nassert.equal(batteryEstimate(h(12),true).percent,null);\nassert.equal(batteryEstimate({flags:1,bus_v:12}).percent,null);\nassert.equal(batteryEstimate({flags:16,bus_v:12}).percent,null);\nlet prev=-1;for(let v=6;v<=12.9;v+=.01){let p=batteryEstimate(h(v)).percent;assert(p>=prev && p>=0 && p<=100);prev=p;}\nconsole.log('Battery boundary, validity, monotonicity and UI parity: PASS');\n",text=True,encoding="utf-8",check=True)

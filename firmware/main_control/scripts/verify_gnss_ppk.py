"""Inspect standard UBX observations without changing logs. Optionally convert with RTKLIB convbin."""
from pathlib import Path
import argparse,json,struct,subprocess
def inspect(path):
    data=Path(path).read_bytes();i=0;counts={};errors=0;raw=[];models=[]
    while i<len(data):
        if data[i:i+2]!=b'\xb5\x62' or i+6>len(data):errors+=1;i+=1;continue
        n=int.from_bytes(data[i+4:i+6],'little');end=i+n+8
        if end>len(data):errors+=1;break
        a=b=0
        for c in data[i+2:end-2]:a=(a+c)&255;b=(b+a)&255
        if data[end-2:end]!=bytes([a,b]):errors+=1;i+=1;continue
        cls,msg=data[i+2:i+4];p=data[i+6:end-2];key=f'{cls:02x}-{msg:02x}';counts[key]=counts.get(key,0)+1
        if (cls,msg)==(2,0x15):
            if len(p)<16 or len(p)!=16+32*p[11]:errors+=1
            else:
                tow,week,leap,num=struct.unpack_from('<dHbB',p)
                raw.append({'week':week,'tow_s':tow,'signals':num,'phase_valid_signals':sum(bool(p[16+k*32+30]&2) for k in range(num)),'clock_reset':bool(p[12]&2)})
        if (cls,msg)==(10,4):models.extend(p[k:k+30].split(b'\0')[0].decode('ascii','replace') for k in range(40,len(p),30))
        i=end
    gaps=[]
    for a,b in zip(raw,raw[1:]):
        dt=(b['week']-a['week'])*604800+b['tow_s']-a['tow_s']
        if not 0.5<=dt<=1.5:gaps.append(dt)
    return {'file':str(Path(path).resolve()),'bytes':len(data),'messages':counts,'parse_errors':errors,'rawx_epochs':len(raw),'sfrbx_messages':counts.get('02-13',0),'model_extensions':models,'first_epoch':raw[0] if raw else None,'last_epoch':raw[-1] if raw else None,'unexpected_epoch_intervals_s':gaps,'rover_raw_present':bool(raw and counts.get('02-13',0) and not errors),'absolute_position_accuracy_verified':False,'note':'PPKには同時刻の基準局観測/航法データ、既知座標、アンテナ情報も必要。データ存在は解析品質の合格ではない。'}
def main():
    p=argparse.ArgumentParser();p.add_argument('ubx',type=Path);p.add_argument('--output',type=Path);p.add_argument('--convbin',type=Path);p.add_argument('--rinex-dir',type=Path);a=p.parse_args()
    result=inspect(a.ubx);out=json.dumps(result,ensure_ascii=False,indent=2)
    if a.output:a.output.write_text(out+'\n',encoding='utf-8')
    print(out)
    if a.convbin:
        if not a.rinex_dir:p.error('--convbinには--rinex-dirが必要です')
        a.rinex_dir.mkdir(parents=True,exist_ok=True)
        obs=a.rinex_dir/'rover.obs';nav=a.rinex_dir/'rover.nav'
        if obs.exists() or nav.exists():raise SystemExit('既存RINEXを上書きしません。新しい出力先を指定してください')
        subprocess.run([str(a.convbin.resolve()),'-r','ubx','-v','3.04','-o',str(obs),'-n',str(nav),str(a.ubx.resolve())],check=True)
if __name__=='__main__':main()

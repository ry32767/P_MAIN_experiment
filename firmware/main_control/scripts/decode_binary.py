"""Validate binary logs and optionally export each record type to a separate CSV."""
import argparse,csv,json
from pathlib import Path
from binary_log import read_header,records,rows,NAMES

def decode(path,csv_dir=None,allow_truncated_tail=False):
    counts={};writers={};files=[];report={};captures={}
    try:
        with Path(path).open('rb') as f:
            header=read_header(f)
            for record in records(f,allow_truncated_tail,report):
                kind=record['kind'];name=NAMES.get(kind,f'unknown_{kind}');counts[name]=counts.get(name,0)+1
                if kind in (6,7,9):
                    values=list(rows(record));shot=values[0]['shot_seq'];capture=captures.setdefault(shot,dict(samples=0,adc_errors=0,index_discontinuities=0,has_begin=False,has_end=False))
                    if kind==6: capture.update(has_begin=True,target_samples=values[0]['target_samples'],next_index=values[0]['first_index'])
                    elif kind==7:
                        if values[0]['sample_index']!=capture.get('next_index',values[0]['sample_index']):capture['index_discontinuities']+=1
                        capture['next_index']=values[-1]['sample_index']+1
                        capture['samples']+=len(values);capture['adc_errors']+=sum(x['adc_error'] for x in values)
                    else: capture.update(has_end=True,**{k:v for k,v in values[0].items() if k not in ('shot_seq','mono_us')})
                if csv_dir:
                    for row in rows(record):
                        if kind not in writers:
                            dest=Path(csv_dir);dest.mkdir(parents=True,exist_ok=True)
                            handle=(dest/(Path(path).stem+'.'+name+'.csv')).open('w',newline='',encoding='utf-8');files.append(handle)
                            writer=csv.DictWriter(handle,fieldnames=list(row));writer.writeheader();writers[kind]=writer
                        writers[kind].writerow(row)
        for capture in captures.values():
            capture['complete']=capture['has_begin'] and capture['has_end'] and capture.get('reason')==0 and capture['samples']==capture.get('target_samples') and capture['samples']==capture.get('written_samples') and not any(capture.get(k,0) for k in ('adc_errors','index_discontinuities','lost_samples','edge_drops','request_drops'))
        return dict(file=str(path),header=header,records=counts,captures=captures,crc_errors=0,truncated=report.get('truncated',False),tail_offset=report.get('offset'))
    finally:
        for f in files:f.close()

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('file');p.add_argument('--csv-dir');p.add_argument('--allow-truncated-tail',action='store_true');a=p.parse_args()
    print(json.dumps(decode(a.file,a.csv_dir,a.allow_truncated_tail),ensure_ascii=False,indent=2))

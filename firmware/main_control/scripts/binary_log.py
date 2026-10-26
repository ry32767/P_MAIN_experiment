"""AQBIN001 reader. Little endian, IEEE754 float32, strict framing and CRC."""
from pathlib import Path
import csv, io, struct, zlib
FILE=struct.Struct('<8sHHBBHIQI')
RECORD=struct.Struct('<IHHQII')
NAMES={1:'imu',2:'bno',3:'health',4:'time_position',5:'shot',6:'capture_begin',7:'envelope',8:'comparator',9:'capture_end',10:'mag_observation',11:'calibration_set',12:'gnss_observation',13:'body_event',14:'body_profile'}
UTC={0:'none',1:'gps_pps_candidate',2:'phone_clock',3:'rtc'}
FORMATS={1:'<IIQQQQB13f',2:'<QQQIBB4f',3:'<11I3H7f4IBIBqIIQQqI24sfQQQBIHH',4:'<QqIBBiiI',5:'<IIIIQQQqBH',6:'<IIIIQQQIIH',8:'<IB',9:'<5IB',10:'<IQQQIIIHH9fI',12:'<IQQ4B6i4I'}
KEYS={
1:'index fresh_mask scheduled_us acquired_us source_us utc_us utc_source qw qx qy qz ax ay az gx gy gz mx my mz',
2:'acquired_us source_us utc_us sequence sensor_id utc_source x y z w',
3:'uptime_ms rows durable_rows sd_errors dropped imu_samples imu_gaps imu_resets imu_age_us max_lateness_us rx_edges flags env_mv buttons_mv bus_v current_ma temp_c pressure_mbar mag_x mag_y mag_z observation_shot observation_samples observation_lost observation_skipped observation_active link_rx sync_ok offset_us uncertainty_us shot_seq tx_us rx_us tof_us range_state rtc_time rtc_temp_c utc_anchor_mono_us utc_anchor_epoch_us utc_valid_until_us utc_source frequency_hz observation_seconds observation_every',
4:'utc_reference_ms phone_age_ms phone_rtt_ms time_source phone_position_fresh phone_lat_e7 phone_lon_e7 phone_accuracy_mm',
5:'session shot_seq frequency_hz range_state parent_scheduled_us tx_us rx_us tof_us observation observation_seconds',
6:'session shot_seq frequency_hz sample_rate tx_us rx_us adc_start_us first_index target_samples seconds',
8:'shot_seq high',9:'shot_seq written_samples lost_samples edge_drops request_drops reason',
10:'sample_sequence read_begin_us read_end_us utc_read_us calibration_generation parent_set_generation time_uncertainty_us utc_source valid raw_x raw_y raw_z corrected_x corrected_y corrected_z body_x body_y body_z body_valid',
12:'itow_ms received_us utc_read_us utc_source fix flags flags2 lat_e7 lon_e7 height_mm vn_mm_s ve_mm_s vd_mm_s hacc_mm vacc_mm sacc_mm_s parent_set_generation'}

def read_header(f):
    raw=f.read(FILE.size)
    if len(raw)!=FILE.size: raise ValueError('Truncated file header')
    magic,version,size,role,stream,reserved,session,created,crc=FILE.unpack(raw)
    if magic!=b'AQBIN001' or version!=1 or size!=32 or role not in (1,2) or reserved or zlib.crc32(raw[:28])!=crc:
        raise ValueError('Invalid file header/version/CRC')
    return dict(version=version,role=role,stream=stream,session=session,created_us=created)

def records(f,allow_truncated_tail=False,report=None):
    report=report if report is not None else {}
    while True:
        offset=f.tell();raw=f.read(RECORD.size)
        if not raw: return
        if len(raw)!=RECORD.size:
            if allow_truncated_tail: report.update(truncated=True,offset=offset);return
            raise ValueError(f'Truncated record header at {offset}')
        magic,kind,size,mono,sequence,crc=RECORD.unpack(raw)
        if magic!=0x52425141: raise ValueError(f'Bad record magic at {offset}')
        payload=f.read(size)
        if len(payload)!=size:
            if allow_truncated_tail: report.update(truncated=True,offset=offset);return
            raise ValueError(f'Truncated payload at {offset}')
        if zlib.crc32(payload,zlib.crc32(raw[:20]))!=crc: raise ValueError(f'Record CRC mismatch at {offset}')
        if kind in FORMATS and size not in ((struct.calcsize(FORMATS[kind]),struct.calcsize(FORMATS[kind])+2,struct.calcsize(FORMATS[kind])+7) if kind in (3,5) else (struct.calcsize(FORMATS[kind]),)): raise ValueError(f'Bad {NAMES[kind]} size at {offset}')
        if kind==11:
            if size!=376 or payload[:4]!=b'PSET' or struct.unpack_from('<HH',payload,4)!=(1,376) or zlib.crc32(payload[:-4])!=struct.unpack_from('<I',payload,size-4)[0]:
                raise ValueError(f'Bad calibration set at {offset}')
        if kind==7:
            if size<12: raise ValueError(f'Bad envelope prefix at {offset}')
            shot,index,count,period=struct.unpack_from('<IIHH',payload)
            if not count or count>1024 or size!=12+2*count or period!=50: raise ValueError(f'Bad envelope samples at {offset}')
        if kind==13 and size!=260: raise ValueError('Bad body event size')
        if kind==14 and (size!=112 or payload[:4]!=b'BODY' or zlib.crc32(payload[:-4])!=struct.unpack_from('<I',payload,108)[0]): raise ValueError('Bad body profile CRC/size')
        yield dict(kind=kind,mono_us=mono,sequence=sequence,payload=payload,offset=offset)

def rows(record):
    kind=record['kind'];p=record['payload'];mono=record['mono_us']
    if kind==13:
        keys='capture boot imu_calibration mag_calibration marker_mono_us utc_us sample_sequence drops time_uncertainty_us event phase utc_source mask'.split()
        result=dict(zip(keys,struct.unpack_from('<4I2Q3I4B',p)))
        for key,offset,n in [('device',48,20),('log_file',68,64),('raw_file',132,64),('health_file',196,64)]:result[key]=p[offset:offset+n].split(b'\0',1)[0].decode('ascii')
        result['mono_us']=mono;yield result;return
    if kind==14:
        result=dict(zip('magic version bytes generation capture boot imu_calibration mag_calibration created_utc_us utc_source axes camera reserved'.split(),struct.unpack_from('<IHH5IQ4B',p)))
        result['device']=p[40:60].split(b'\0',1)[0].decode('ascii');result['imu_q']=struct.unpack_from('<4f',p,60);result['mag_q']=struct.unpack_from('<4f',p,76)
        result['camera_scale'],result['camera_offset_us']=struct.unpack_from('<2d',p,92);result['mono_us']=mono;yield result;return
    if kind==7:
        shot,index,count,period=struct.unpack_from('<IIHH',p)
        for i,word in enumerate(struct.unpack_from('<'+str(count)+'H',p,12)):
            yield dict(shot_seq=shot,sample_index=index+i,mono_us=mono+i*period,env_adc=word&4095,adc_error=bool(word&0x8000))
        return
    if kind==11:
        magic,version,size,generation,session,imu,mag,created,source=struct.unpack_from('<IHHIIIIQI',p)
        def text(offset): return p[offset:offset+40].split(b'\0',1)[0].decode('utf-8',errors='strict')
        result=dict(mono_us=mono,generation=generation,parent_session=session,imu_calibration=imu,mag_calibration=mag,created_utc_us=created,utc_source=UTC.get(source,'unknown'),name=text(36),cad=text(76),body=text(116),frame='FRD')
        for i,name in enumerate(('imu','mag','gnss')):
            known,*v=struct.unpack_from('<I7f',p,276+32*i);result[name+'_id']=text(156+40*i);result[name+'_known']=known
            for key,value in zip(('x_m','y_m','z_m','qw','qx','qy','qz'),v):result[name+'_'+key]=value
        yield result;return
    if kind not in FORMATS:
        yield dict(mono_us=mono,kind=kind,payload_hex=p.hex());return
    base=struct.calcsize(FORMATS[kind])
    row=dict(zip(KEYS[kind].split(),struct.unpack(FORMATS[kind],p[:base])))
    if kind in (3,5):
        row['ranging_rate_hz'],row['acoustic_pattern']=struct.unpack('BB',p[base:base+2]) if len(p)>base else (1,0)
    if kind in (3,5):
        row['acoustic_pattern_id'],row['pattern_gap1_us'],row['pattern_gap2_us']=struct.unpack('<BHH',p[base+2:]) if len(p)==base+7 else (None,None,None)
    row['mono_us']=mono
    if 'utc_source' in row: row['utc_source']=UTC.get(row['utc_source'],'unknown')
    if kind==10:
        row['hardware_status']=row['valid']>>8
        row['hardware_overrun']=bool(row['hardware_status']&0x80)
        row['valid']=bool(row['valid']&1)
    if kind==3:
        row['rtc_time']=row['rtc_time'].split(b'\0',1)[0].decode('ascii',errors='replace')
        row['leak']=int(bool(row['flags']&8))
    if kind==4:
        row['time_source']=UTC.get(row['time_source'],'unknown')
        valid=row['phone_position_fresh']
        row['phone_latitude']=row['phone_lat_e7']/1e7 if valid else float('nan')
        row['phone_longitude']=row['phone_lon_e7']/1e7 if valid else float('nan')
        row['phone_accuracy_m']=row['phone_accuracy_mm']/1000 if valid else float('nan')
    yield row

def csv_lines(path,kind):
    """Compatibility view for existing CRC/IMU verification tools."""
    with Path(path).open('rb') as f:
        read_header(f);data=[]
        for record in records(f):
            if record['kind']==kind: data.extend(rows(record))
    if not data: raise ValueError('No records of requested type')
    out=[]
    def line(values):
        b=io.StringIO();csv.writer(b,lineterminator='').writerow(values);return b.getvalue().encode('utf-8')
    keys=list(data[0]);out.append(line(keys+['crc32']))
    for row in data:
        body=line([row[k] for k in keys]);out.append(body+b','+f'{zlib.crc32(body):08x}'.encode())
    return out

def input_lines(path,kind):
    path=Path(path)
    with path.open('rb') as f: binary=f.read(8)==b'AQBIN001'
    return csv_lines(path,kind) if binary else path.read_bytes().splitlines()

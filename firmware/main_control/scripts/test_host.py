from pathlib import Path
import subprocess,sys,shutil
import ziglang
r=Path(__file__).resolve().parents[1]
zig=Path(ziglang.__file__).parent/('zig.exe' if sys.platform=='win32' else 'zig')
(r/'reports/raw').mkdir(parents=True,exist_ok=True)
out=r/'reports/raw/test_protocol.exe'
subprocess.run([str(zig),'c++','-std=c++17','-DIMU_HZ=50','-I'+str(r/'common'),str(r/'tests/test_protocol.cpp'),'-o',str(out)],check=True)
subprocess.run([str(out)],check=True)
# All timing values are computed here, with unit assertions.
for hz in (50,100):
 period=1_000_000//hz
 assert hz*period==1_000_000
 print(f'IMU {hz} Hz: {period} us; 512-row queue covers {512/hz:.2f} seconds')
carrier=200_000; waves=5
assert waves/carrier*1e6==25
assert 800_000/4==carrier
print(f'PIO: divider={150_000_000/800_000}, period={4/800_000*1e6} us, burst={waves/carrier*1e6} us')
print(f'200 us early window = {200*1500/1e6} m; 100 us timestamp uncertainty = {100*1500/1e6} m')

spout=r/'reports/raw/test_spresense.exe'
subprocess.run([str(zig),'c++','-std=c++17','-I'+str(r/'common'),str(r/'tests/test_spresense.cpp'),'-o',str(spout)],check=True)
subprocess.run([str(spout)],check=True)

phoneout=r/'reports/raw/test_phone.exe'
subprocess.run([str(zig),'c++','-std=c++17','-I'+str(r/'common'),str(r/'tests/test_phone.cpp'),'-o',str(phoneout)],check=True)
subprocess.run([str(phoneout)],check=True)

sdout=r/'reports/raw/test_sd_recovery.exe'
subprocess.run([str(zig),'c++','-std=c++17','-I'+str(r/'common'),str(r/'tests/test_sd_recovery.cpp'),'-o',str(sdout)],check=True)
subprocess.run([str(sdout)],check=True)

utcout=r/"reports/raw/test_utc.exe"
subprocess.run([str(zig),"c++","-std=c++17","-DIMU_HZ=100","-I"+str(r/"common"),str(r/"tests/test_utc.cpp"),"-o",str(utcout)],check=True)
subprocess.run([str(utcout)],check=True)

seqout=r/"reports/raw/test_sequence_gap.exe"
subprocess.run([str(zig),"c++","-std=c++17","-I"+str(r/"common"),str(r/"tests/test_sequence_gap.cpp"),"-o",str(seqout)],check=True)
subprocess.run([str(seqout)],check=True)

binout=r/'reports/raw/test_binary.exe'
fixture=r/'reports/raw/binary_fixture.bin'
subprocess.run([str(zig),'c++','-std=c++17','-DIMU_HZ=100','-I'+str(r/'common'),str(r/'tests/test_binary.cpp'),'-o',str(binout)],check=True)
subprocess.run([str(binout),str(fixture)],check=True)
subprocess.run([sys.executable,str(r/'tests/test_binary_reader.py'),str(fixture)],check=True)
for hz in (10000,50000,100000,200000):
    assert hz*4<=800000
    print(f'PIO carrier={hz} Hz, 5-wave burst={5/hz*1e6:g} us, divider={150_000_000/(4*hz):g}')
assert 48_000_000/(2399+1)==20_000
assert 3*20_000*2==120_000
print('Multipath envelope: 20 kSa/s, 50 us step, 3 s raw samples=120000 bytes; DMA ring covers 0.8192 s (not measured SD throughput)')

calout=r/'reports/raw/test_bno_calibration.exe'
subprocess.run([str(zig),'c++','-std=c++17','-DIMU_HZ=100','-I'+str(r/'common'),str(r/'tests/test_bno_calibration.cpp'),'-o',str(calout)],check=True)
subprocess.run([str(calout)],check=True)
subprocess.run([sys.executable,str(r/'scripts/test_bno_control.py')],check=True)

# Conservative healthy-bus transaction budget, preserving 100ms arm lead.
packet_bytes=163
air=(packet_bytes*10*1_000_000+115199)//115200
for rate in range(1,5):
    period=1_000_000//rate
    worst_deadline=2*air+3000+100000+22000+18150+50000
    assert worst_deadline<period
    print(f'Rate {rate}/s: period={period} us, modeled deadline={worst_deadline} us (not measured)')
for hz in (10000,50000,100000,200000):
    spacing=lambda gap:(5*4_000_000+gap*hz*4+3_000_000)//(4*hz)
    for ident in range(64):
        gaps=(1000+600*(ident%8),7000+600*(ident//8))
        assert gaps[0]*hz%1_000_000==0 and gaps[1]*hz%1_000_000==0
        assert spacing(gaps[0])+spacing(gaps[1])+600<=18150
    assert 600>2*200
    print(f'Variable pattern {hz} Hz: max tail={spacing(5200)+spacing(11200)+600} us; acceptance=200 us (unvalidated)')
assert 64/4==16
print('64 patterns: minimum reuse at 4 shots/s = 16 seconds within a continuous session (not measured)')

gnssout=r/'reports/raw/test_gnss.exe'
subprocess.run([str(zig),'c++','-std=c++17','-I'+str(r/'common'),str(r/'tests/test_gnss.cpp'),'-o',str(gnssout)],check=True)
subprocess.run([str(gnssout)],check=True)

gnssbinout=r/'reports/raw/test_gnss_binary.exe'
subprocess.run([str(zig),'c++','-std=c++17','-I'+str(r/'common'),str(r/'tests/test_gnss_binary.cpp'),'-o',str(gnssbinout)],check=True)
subprocess.run([str(gnssbinout)],check=True)

magout=r/'reports/raw/test_mag_calibration.exe'
subprocess.run([str(zig),'c++','-std=c++17','-I'+str(r/'common'),str(r/'tests/test_mag_calibration.cpp'),'-o',str(magout)],check=True)
subprocess.run([str(magout)],check=True)

wireout=r/'reports/raw/test_spresense_control.exe'
subprocess.run([str(zig),'c++','-std=c++17','-I'+str(r/'common'),str(r/'tests/test_spresense_control.cpp'),'-o',str(wireout)],check=True)
subprocess.run([str(wireout)],check=True)
print(f'Spresense GPIO UART: tick=200 us, baud={1_000_000//800}, request nominal reception={(128*11*4-6)*200} us (not measured)')
assert 128*11*4*200*2+10000<3200000

calout=r/'reports/raw/test_spresense_calibration.exe'
subprocess.run([str(zig),'c++','-std=c++17','-I'+str(r/'common'),'-I'+str(r/'firmware/spresense/libraries/AquaBeacon/src'),str(r/'tests/test_spresense_calibration.cpp'),'-o',str(calout)],check=True)
subprocess.run([str(calout),str(r/'reports/raw/spresense_schema3_fixture.csv')],check=True)
# Sampling coverage, requested hold and maximum frame turnaround (modeled only).
assert int(960*10*.95)==9120
assert 2_000_000+10_000_000==12_000_000
assert 128*11*800*2+10_000<3_200_000
subprocess.run([sys.executable,str(r/'scripts/test_web_transfer.py')],check=True)

ntripout=r/'reports/raw/test_ntrip_settings.exe'
subprocess.run([str(zig),'c++','-std=c++17','-DIMU_HZ=100','-I'+str(r/'common'),'-I'+str(r/'include'),str(r/'tests/test_ntrip_settings.cpp'),'-o',str(ntripout)],check=True)
subprocess.run([str(ntripout)],check=True)
subprocess.run([sys.executable,str(r/'scripts/test_ntrip_ui.py')],check=True)

# Parent PPS/sample clocks and immutable calibration set ABI.
sensorout=r/'reports/raw/test_sensor_time.exe'
subprocess.run([str(zig),'c++','-std=c++17','-DIMU_HZ=100','-I'+str(r/'common'),str(r/'tests/test_sensor_time.cpp'),'-o',str(sensorout)],check=True)
sensorfixture=r/'reports/raw/parent_sensor_fixture.bin'
subprocess.run([str(sensorout),str(sensorfixture)],check=True)
subprocess.run([sys.executable,str(r/'tests/test_parent_sensor_reader.py'),str(sensorfixture)],check=True)
subprocess.run([sys.executable,str(r/'scripts/test_console_ui.py')],check=True)
subprocess.run([sys.executable,str(r/'scripts/test_overview_ui.py')],check=True)
subprocess.run([sys.executable,str(r/'scripts/test_spresense_ui.py')],check=True)
subprocess.run([sys.executable,str(r/'tests/test_parent_set_ui.py')],check=True)

storeout=r/'reports/raw/test_parent_store.exe'
subprocess.run([str(zig),'c++','-std=c++17','-I'+str(r/'common'),'-I'+str(r/'include'),str(r/'tests/test_parent_store.cpp'),'-o',str(storeout)],check=True)
subprocess.run([str(storeout)],check=True)

subprocess.run([sys.executable,str(r/'tests/test_cyw43_async.py')],check=True)

subprocess.run([sys.executable,str(r/'tests/test_ntrip_dns.py')],check=True)

# Actual GNSS diagnostic decoders and variable-length cross-core queue.
gdiag=r/'reports/raw/test_gnss_diagnostics.exe'
subprocess.run([str(zig),'c++','-std=c++17','-I'+str(r/'common'),str(r/'tests/test_gnss_diagnostics.cpp'),'-o',str(gdiag)],check=True)
subprocess.run([str(gdiag)],check=True)

glink=r/'reports/raw/test_gnss_link.exe'
subprocess.run([str(zig),'c++','-std=c++17','-DIMU_HZ=50','-I'+str(r/'common'),'-I'+str(r/'include'),str(r/'tests/test_gnss_link.cpp'),'-o',str(glink)],check=True)
subprocess.run([str(glink)],check=True)

# Body coordinate calibration, immutable storage and camera matching (offline).
for name in ['body_calibration','body_store']:
    target=r/('reports/raw/test_'+name+'.exe')
    subprocess.run([str(zig),'c++','-std=c++17','-DIMU_HZ=100','-I'+str(r/'common'),'-I'+str(r/'include'),str(r/('tests/test_'+name+'.cpp')),'-o',str(target)],check=True)
    subprocess.run([str(target)],check=True)
subprocess.run([sys.executable,str(r/'tests/test_body_alignment.py')],check=True)
subprocess.run([sys.executable,str(r/'tests/test_body_ui.py')],check=True)
subprocess.run([sys.executable,str(r/'tests/test_body_control.py')],check=True)
subprocess.run([sys.executable,str(r/'tests/test_spresense_maintenance.py')],check=True)

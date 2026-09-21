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

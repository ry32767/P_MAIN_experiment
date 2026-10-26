"""Execute the actual project CYW43 begin: no radio association waits."""
from pathlib import Path
import subprocess,sys,tempfile
import ziglang
root=Path(__file__).resolve().parents[1]
s=(root/'lib/lwIP_CYW43/src/utility/CYW43shim.cpp').read_text(encoding='utf-8')
begin=s[s.index('bool CYW43::begin('):s.index('void CYW43::end()')]
route=(root/'lib/lwIP_CYW43/src/lwIP_CYW43.cpp').read_text(encoding='utf-8')
route=route[route.index('bool CYW43lwIP::begin('):route.index('void CYW43lwIP::end()')]
harness=r"""
#include <cassert>
#include <cstdint>
#include <cstring>
struct netif {uint8_t hwaddr[6];bool linkUp=true;};netif* joiningNetif=nullptr;void netif_set_link_down(netif* n){n->linkUp=false;}struct Device{} cyw43_state;
constexpr int CYW43_AUTH_WPA2_AES_PSK=1,CYW43_AUTH_OPEN=0,CYW43_IOCTL_SET_VAR=2,CYW43_ITF_STA=0,CYW43_CHANNEL_NONE=0;
int joinResult=0,joins=0,staEnables=0,apEnables=0;size_t keyLength=0;unsigned auth=99;const uint8_t* usedBssid=nullptr;
void cyw43_arch_enable_sta_mode(){++staEnables;}
void cyw43_arch_enable_ap_mode(const char*,const char*,unsigned){++apEnables;}
void cyw43_wifi_get_mac(Device*,int,uint8_t*){}
void cyw43_ioctl(Device*,int,unsigned,uint8_t*,int){}
void cyw43_wifi_update_multicast_filter(Device*,uint8_t*,bool){}
int cyw43_wifi_join(Device*,size_t,const uint8_t*,size_t len,const uint8_t*,unsigned a,const uint8_t* bssid,int){assert(joiningNetif&&!joiningNetif->linkUp);++joins;keyLength=len;auth=a;usedBssid=bssid;return joinResult;}
// Any reintroduced waiting fails this test instead of hiding in the mock.
unsigned millis(){assert(false&&"join must not poll the clock");return 0;}
void delay(unsigned){assert(false&&"join must not wait for association");}
template<class T>struct LwipIntfDev {bool _ap=false,_default=false;bool begin(const uint8_t*,uint16_t){return true;}};
class CYW43 {public: bool _ap=false;Device* _self=nullptr;netif* _netif=nullptr;netif* _netifAP=nullptr;int _itf=-1;const char* _ssid="hotspot";const char* _password="secret";uint8_t _bssid[6]{};bool begin(const uint8_t*,netif*);};
struct CYW43lwIP:public LwipIntfDev<CYW43>{bool begin(const uint8_t*,uint16_t);};
"""
checks=r"""
int main(){
 CYW43lwIP route;route._ap=true;assert(route.begin(nullptr,1500)&&!route._default);route._ap=false;assert(route.begin(nullptr,1500)&&route._default);
 netif sta{},ap{};joiningNetif=&sta;CYW43 driver;
 // Join is merely accepted, not associated: initialization still returns.
 assert(driver.begin(nullptr,&sta));assert(joins==1&&staEnables==1&&apEnables==0&&driver._netif==&sta&&driver._itf==0);
 assert(auth==CYW43_AUTH_WPA2_AES_PSK&&keyLength==6&&usedBssid==nullptr);
 joinResult=-1;assert(!driver.begin(nullptr,&sta));assert(joins==2);
 joinResult=0;driver._password=nullptr;driver._bssid[0]=1;assert(driver.begin(nullptr,&sta));assert(keyLength==0&&auth==CYW43_AUTH_OPEN&&usedBssid);
 driver._ap=true;assert(driver.begin(nullptr,&ap));assert(apEnables==1&&joins==3&&driver._netifAP==&ap&&driver._itf==1);
}
"""
zig=Path(ziglang.__file__).parent/('zig.exe' if sys.platform=='win32' else 'zig')
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);cpp=p/'async.cpp';exe=p/'async.exe'
 cpp.write_text(harness+begin+route+checks,encoding='utf-8')
 subprocess.run([str(zig),'c++','-std=c++17',str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('PASS actual CYW43 begin: pending join returns without clock polling/waits, join error, open authentication, BSSID and AP path')

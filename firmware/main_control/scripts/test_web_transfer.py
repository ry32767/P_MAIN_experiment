"""Exercise the actual webTick send path with a slow/partial mock TCP client."""
from pathlib import Path
import subprocess, sys, tempfile
import ziglang
root=Path(__file__).resolve().parents[1]
source=(root/'include/web_control.h').read_text(encoding='utf-8')
tick=source[source.index('void webTick(){'):]
# Request parsing is tested elsewhere; keep the actual accept/timeout/send path.
tick=tick[:tick.index(' for(unsigned n=0;n<256')]+'}\n'
harness=r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
using std::min;
using String=std::string;
namespace ntrip {bool deferStart=false;bool wifiSettingsPending=false;int joins=0;void connectSettingsWifi(){wifiSettingsPending=false;++joins;}void wifiSettingsTick(){if(wifiSettingsPending)connectSettingsWifi();}}
uint32_t now=0; uint32_t millis(){return now;}
struct MockClient {
 int incoming=0;int available(){return incoming;}
 bool active=false,noDelay=false; std::string output; size_t total=0,capacity=512,partial=512; int stops=0;
 explicit operator bool()const{return active;}
 void setNoDelay(bool v){noDelay=v;}
 int availableForWrite(){return capacity;}
 size_t write(const uint8_t* data,size_t n){n=min(n,partial);total+=n;output.append(reinterpret_cast<const char*>(data),n);return n;}
 void stop(unsigned=0){active=false;stops++;}
} client;
struct {MockClient accept(){return MockClient{};}} server;
String request,response;size_t responseSent=0;uint32_t clientSince=0,clientProgress=0,rangeDeadline=0;
const char* responseStaticBody=nullptr;size_t responseStaticSize=0;
size_t webResponseLength(){return response.length()+responseStaticSize;}
bool runRanging=false,webWaiting=false,webRemote=false,localControlDone=false;
struct {uint32_t accepted=0,sent=0,completed=0,timeouts=0,pageCompleted=0;} webDiag;
uint32_t webSeq=0;
namespace aq { struct Packet {uint32_t seq;}; int unpack(Packet){return 0;} }
struct {bool pop(aq::Packet&){return false;}} controlIn;
int localAnswer=0; String controlJson(int){return "";} void webReply(int,String){}
void reset(uint32_t start=0){now=start;request.clear();client=MockClient{};client.active=true;clientSince=clientProgress=now;response.assign(55456,'x');responseSent=0;responseStaticBody=nullptr;responseStaticSize=0;}
'''
checks=r'''
int main(){
 // The response must finish before STA reconfiguration is allowed.
 reset();ntrip::wifiSettingsPending=true;webTick();assert(ntrip::joins==0);
 while(client.active)webTick();assert(ntrip::joins==0);
 webTick();assert(ntrip::joins==1&&!ntrip::wifiSettingsPending);
 // Flash-backed page: transfer header then body, including partial writes at
 // the segment boundary; no body-sized dynamic HTTP response is constructed.
 static const char page[]=R"AQTEST(__PAGE__)AQTEST";
 for(size_t partial : {size_t(1),size_t(137),size_t(1460)}){
  reset();response="HTTP/1.1 200 OK\r\n\r\n";client.partial=partial;
  responseStaticBody=page;responseStaticSize=sizeof(page)-1;
  while(client.active){now+=1;webTick();}
  assert(response.size()<100);assert(responseSent==webResponseLength());
  assert(client.output==response+page);assert(webDiag.pageCompleted>0);
 }

 // A browser's idle preconnect cannot block an actual queued operation for 7s.
 reset();response.clear();ntrip::deferStart=true;now=1001;webTick();assert(!client.active&&ntrip::deferStart);
 webTick();assert(!ntrip::deferStart);
 // Do not discard a socket with request data ready, or a partially received request.
 reset();response.clear();client.incoming=1;now=1001;webTick();assert(client.active);
 reset();response.clear();request="POST /api/gnss/table";now=1001;webTick();assert(client.active);
 // More than seven seconds, with acknowledged progress every 100ms.
 reset();while(client.active){now+=100;webTick();}assert(responseSent==response.size());assert(now>7000);assert(client.total==55456);
 // Partial writes must preserve every unsent byte.
 reset();client.partial=137;while(client.active){now+=100;webTick();}assert(responseSent==response.size());assert(client.total==55456);
 // Zero writes do not count as progress.
 reset();client.partial=0;now=7001;webTick();assert(!client.active && responseSent==0);
 // Idle sockets and a continuously active socket both have finite limits.
 reset();client.capacity=0;now=7001;webTick();assert(!client.active);
 reset();client.capacity=1;for(int i=0;i<122 && client.active;i++){now+=1000;webTick();}assert(!client.active && responseSent<response.size());
 // Unsigned elapsed time remains valid across millis rollover.
 reset(UINT32_MAX-500);now+=1000;webTick();assert(client.active && responseSent>0);now+=7001;webTick();assert(!client.active);
}
'''
zig=Path(ziglang.__file__).parent/('zig.exe' if sys.platform=='win32' else 'zig')
with tempfile.TemporaryDirectory() as directory:
 path=Path(directory);cpp=path/'transfer.cpp';exe=path/'transfer.exe'
 origin=source[source.index('static bool allowedOrigin('):source.index('static void webReplyHeader(')]
 originMock='''
struct IP {std::string value;bool isSet(){return value!="0.0.0.0";}std::string toString(){return value;}};
struct {bool online=true;IP ap{"192.168.4.1"},sta{"172.20.10.2"};bool connected(){return online;}IP softAPIP(){return ap;}IP localIP(){return sta;}} WiFi;
'''
 checks=checks.replace('int main(){','''int main(){
 assert(allowedOrigin(""));assert(allowedOrigin("http://192.168.4.1"));assert(allowedOrigin("http://172.20.10.2"));assert(allowedOrigin("https://ry32767.github.io"));
 for(auto s:{"http://evil.example","http://172.20.10.2.evil.example","http://172.20.10.3","null"})assert(!allowedOrigin(s));
 WiFi.online=false;assert(!allowedOrigin("http://172.20.10.2"));WiFi.online=true;WiFi.sta.value="0.0.0.0";assert(!allowedOrigin("http://0.0.0.0"));
''')
 cpp.write_text(harness+originMock+origin+tick+checks.replace('__PAGE__',(root/'web/index.html').read_text(encoding='utf-8')),encoding='utf-8')
 subprocess.run([str(zig),'c++','-std=c++17',str(cpp),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('PASS actual webTick: flash-backed HTML/header boundary, slow/partial/zero writes, timeout, clock rollover')

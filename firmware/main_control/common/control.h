#pragma once
#include "protocol.h"
namespace aq {
enum Operation:uint8_t { INFO=1, RECORD_STOP, RECORD_START, RECORD_NEW, IMU_RESET, RATE_50, RATE_100, FILE_LIST, READ_LOG, RX_START, RX_STOP, RX_SAMPLE };
enum ControlError:uint8_t { OK=0, BUSY=1, SD_ERROR=2, BAD_ARGUMENT=3, NOT_STOPPED=4, NOT_FOUND=5, TIMEOUT=6, UNSUPPORTED=7 };
#pragma pack(push,1)
struct Control {uint8_t op=0,error=0;uint16_t length=0;uint32_t arg=0;uint8_t data[104]{};};
#pragma pack(pop)
static_assert(sizeof(Control)<=sizeof(Packet)-16,"Control must fit fixed tether frame");
inline Control unpack(const Packet&p){Control c;memcpy(&c,&p.t1,sizeof(c));return c;}
inline void pack(Packet&p,const Control&c){memcpy(&p.t1,&c,sizeof(c));}
inline bool safeFilename(const char* s){
 size_t n=0;for(;n<64 && s[n];n++){char c=s[n];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return false;}
 return n>4 && n<64 && !strstr(s,"..");
}
}

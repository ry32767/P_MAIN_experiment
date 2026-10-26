#pragma once
#include "protocol.h"
namespace aq {
enum Operation:uint8_t { INFO=1, RECORD_STOP, RECORD_START, RECORD_NEW, IMU_RESET, RATE_50, RATE_100, FILE_LIST, READ_LOG, RX_START, RX_STOP, RX_SAMPLE, CAL_START, CAL_STATUS, CAL_SAVE, CAL_END, BODY_BEGIN, BODY_MARK, BODY_STATUS, BODY_END, BODY_IMPORT };
enum ControlError:uint8_t { OK=0, BUSY=1, SD_ERROR=2, BAD_ARGUMENT=3, NOT_STOPPED=4, NOT_FOUND=5, TIMEOUT=6, UNSUPPORTED=7, IMU_ERROR=8, CAL_NOT_READY=9, CAL_WARNING=10 };
#pragma pack(push,1)
struct Control {uint8_t op=0,error=0;uint16_t length=0;uint32_t arg=0;uint8_t data[104]{};};
#pragma pack(pop)
static_assert(sizeof(Control)<=sizeof(Packet)-16,"Control must fit fixed tether frame");
inline Control unpack(const Packet&p){Control c;memcpy(&c,&p.t1,sizeof(c));return c;}
inline void pack(Packet&p,const Control&c){memcpy(&p.t1,&c,sizeof(c));}
inline bool safeFilename(const char* s){
 size_t n=0;for(;n<64 && s[n];n++){char c=s[n];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return false;}
 if(!(n>4 && n<64 && !strstr(s,"..")))return false;
 // SD connection settings contain secrets and are never downloadable as logs.
 return !(s[n-4]=='.'&&(s[n-3]=='c'||s[n-3]=='C')&&(s[n-2]=='f'||s[n-2]=='F')&&(s[n-1]=='g'||s[n-1]=='G'));
}
}

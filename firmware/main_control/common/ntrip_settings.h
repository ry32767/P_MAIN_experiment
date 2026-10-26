#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>
namespace ntripsettings {
// Fixed, versioned records let a damaged/newer SD slot fall back to the other.
struct Record {
 uint32_t magic,version,generation;
 uint16_t port;
 char host[129],user[97],password[129],ssid[33],wifiPassword[64],mount[64];
 uint32_t checksum;
};
inline uint32_t crc(const void* data,size_t size){
 uint32_t c=0xffffffff;auto p=static_cast<const uint8_t*>(data);
 while(size--){c^=*p++;for(unsigned i=0;i<8;i++)c=(c>>1)^(0xedb88320u&uint32_t(-int32_t(c&1)));}return ~c;
}
template<size_t N> bool text(const char (&s)[N]){
 for(size_t i=0;i<N;i++){if(!s[i])return true;if(uint8_t(s[i])<32||uint8_t(s[i])==127)return false;}return false;
}
inline bool parsePort(const char* s,uint16_t& port){
 if(!s||!*s)return false;uint32_t n=0;
 for(;*s;s++){if(*s<'0'||*s>'9')return false;n=n*10+unsigned(*s-'0');if(n>65535)return false;}
 if(!n)return false;port=uint16_t(n);return true;
}
inline bool valid(const Record& r){
 if(r.magic!=0x41514e54||r.version!=1||!r.port||!text(r.host)||!r.host[0]||!text(r.user)||!text(r.password)||!text(r.ssid)||!text(r.wifiPassword)||!text(r.mount))return false;
 for(const char* p=r.host;*p;p++)if(!isalnum(uint8_t(*p))&&*p!='.'&&*p!='-')return false;
 if(strchr(r.user,':'))return false;
 for(const char* p=r.mount;*p;p++)if(!isalnum(uint8_t(*p))&&*p!='_'&&*p!='-'&&*p!='.')return false;
 return r.checksum==crc(&r,offsetof(Record,checksum));
}
inline void seal(Record& r){r.magic=0x41514e54;r.version=1;r.checksum=crc(&r,offsetof(Record,checksum));}
inline int newest(const Record& a,bool av,const Record& b,bool bv){
 if(!av)return bv?1:-1;if(!bv)return 0;return int32_t(b.generation-a.generation)>0?1:0;
}
}

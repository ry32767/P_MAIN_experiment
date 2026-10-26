#pragma once
#include <stdint.h>
#include <lwip/dns.h>

// Persistent storage and generation-tagged callbacks: a DNS response can arrive
// after timeout, stop or a change of caster. Never point a callback at a stack IP.
namespace ntripdns {
enum Result { IDLE, PENDING, READY, FAILED };
inline volatile Result result=IDLE;
inline uint32_t generation=0,started=0;
inline ip_addr_t address;
inline void cancel(){++generation;result=IDLE;}
inline void found(const char*,const ip_addr_t* ip,void* token){
 if(uintptr_t(token)!=generation||result!=PENDING)return;
 if(ip){address=*ip;result=READY;}else result=FAILED;
}
inline void begin(const char* host,uint32_t now){
 cancel();started=now;result=PENDING;
 auto err=dns_gethostbyname_addrtype(host,&address,found,reinterpret_cast<void*>(uintptr_t(generation)),LWIP_DNS_ADDRTYPE_IPV4);
 if(err==ERR_OK)result=READY;else if(err!=ERR_INPROGRESS)result=FAILED;
}
inline Result poll(uint32_t now){
 if(result==PENDING&&uint32_t(now-started)>=5000){++generation;result=FAILED;}
 return result;
}
}

"""Actual persistent DNS callback state machine, including late completions."""
from pathlib import Path
import subprocess,sys,tempfile
import ziglang
root=Path(__file__).resolve().parents[1]
stub=r"""
#pragma once
#include <stdint.h>
struct ip_addr_t {uint32_t value=0;};
using Callback=void(*)(const char*,const ip_addr_t*,void*);
constexpr int ERR_OK=0,ERR_INPROGRESS=-5,LWIP_DNS_ADDRTYPE_IPV4=0;
extern int nextResult;extern Callback pendingCallback;extern void* pendingToken;
inline int dns_gethostbyname_addrtype(const char*,ip_addr_t* ip,Callback cb,void* token,int){pendingCallback=cb;pendingToken=token;if(nextResult==ERR_OK)ip->value=7;return nextResult;}
"""
source=r"""
#include <cassert>
#include "ntrip_dns.h"
int nextResult=ERR_INPROGRESS;Callback pendingCallback=nullptr;void* pendingToken=nullptr;
int main(){
 using namespace ntripdns;ip_addr_t reply{42};
 begin("caster",0);assert(result==PENDING);auto oldCallback=pendingCallback;auto oldToken=pendingToken;
 assert(poll(4999)==PENDING);assert(poll(5000)==FAILED);oldCallback("caster",&reply,oldToken);assert(result==FAILED);
 begin("new-caster",10000);oldCallback("old-caster",&reply,oldToken);assert(result==PENDING);
 pendingCallback("new-caster",&reply,pendingToken);assert(result==READY&&address.value==42);
 begin("cancelled",0);auto cancelled=pendingToken;cancel();pendingCallback("cancelled",&reply,cancelled);assert(result==IDLE);
 nextResult=ERR_OK;begin("cached",0);assert(result==READY&&address.value==7);
 nextResult=-1;begin("failed",0);assert(result==FAILED);
 nextResult=ERR_INPROGRESS;begin("not-found",0);pendingCallback("not-found",nullptr,pendingToken);assert(result==FAILED);
 begin("wrap",0xfffffff0u);assert(poll(uint32_t(0xfffffff0u+4999))==PENDING);assert(poll(uint32_t(0xfffffff0u+5000))==FAILED);
}
"""
zig=Path(ziglang.__file__).parent/('zig.exe' if sys.platform=='win32' else 'zig')
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'lwip').mkdir();(p/'lwip/dns.h').write_text(stub,encoding='utf-8');(p/'dns.cpp').write_text(source,encoding='utf-8')
 exe=p/'dns.exe';subprocess.run([str(zig),'c++','-std=c++17','-I'+str(p),'-I'+str(root/'include'),str(p/'dns.cpp'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
print('PASS actual NTRIP DNS: asynchronous completion, timeout, late/stale callback, cancel, cache, failure, rollover')

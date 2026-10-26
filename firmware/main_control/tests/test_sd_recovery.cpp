#include "sd_recovery.h"
#include "../firmware/spresense/libraries/AquaBeacon/src/SpresenseSdRecovery.h"
#include <cassert>
#include <cstdio>
template<class Recovery> void verify(){
 Recovery r;r.failed(100);
 assert(!r.due(5099) && r.due(5100));r.attempting(5100);r.mounted(5101);
 assert(r.recoveries==1 && !r.pending && r.attempts==1);
 // A brief recovery does not clear the budget; a repeated fault backs off.
 r.healthy(35100);assert(r.attempts==1);r.failed(35100);
 assert(!r.due(50099) && r.due(50100));r.attempting(50100);r.failed(50200);
 assert(!r.due(110199) && r.due(110200));r.attempting(110200);r.failed(110300);
 assert(r.exhausted() && !r.due(300000));
 r.stop();assert(!r.wanted && !r.pending && !r.exhausted());
 r.failed(400000);assert(!r.pending && !r.due(500000));
 // Explicit restart clears the budget and preserves cumulative recoveries.
 r.manualStart();assert(r.wanted && r.attempts==0 && r.recoveries==1);
 r.failed(500000);assert(r.due(505000));r.attempting(505000);r.mounted(505001);
 assert(r.recoveries==2);r.healthy(535000);assert(r.attempts==1);
 r.healthy(535001);assert(r.attempts==0);
 r.failed(0xfffffff0u);assert(!r.due(100) && !r.due(4983) && r.due(4984));
 // A failure during explicit stopping must not silently restart logging.
 r.stop();r.failed(600000);assert(!r.wanted && !r.pending);
}
int main(){verify<SdRecovery>();verify<aq::SpresenseSdRecovery>();
 puts("PASS parent/child and Spresense SD: recovery, repeated fault, 5/15/60s delays, budget, explicit stop, manual restart, 30s stability, millis wrap");}

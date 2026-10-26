#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <atomic>

namespace sprwire {
// 1250 baud, 8 data bits, 2 stop bits. HIGH always means release, never drive.
// Timer ISR owns bit state; foreground owns frame publication and RX consumption.
constexpr uint32_t tick_us=200, ticks_per_bit=4, baud=1250;
constexpr size_t max_frame=128;
constexpr uint32_t frame_us=max_frame*11*ticks_per_bit*tick_us;
// Receiver accepts at the middle of the first stop bit, not at TX completion.
constexpr uint32_t receive_us=frame_us-6*tick_us;
class Uart {
  uint8_t tx[max_frame]{},rx[256]{};
  uint64_t rxTime[256]{},tickCount=0,currentTickUs=0;
  std::atomic<unsigned> txSize{0},head{0},tail{0};
  std::atomic<bool> cancelled{false};
  unsigned txByte=0,txTick=0,idleTicks=0,rxTick=0,rxBit=0;
  uint8_t rxByte=0; bool receiving=false,driving=false;
public:
  std::atomic<uint32_t> framing{0},overflow{0},collisions{0};
  bool busy() const {return txSize.load(std::memory_order_acquire)!=0;}
  void abort(){cancelled.store(true,std::memory_order_release);}
  bool send(const uint8_t* p,size_t n) {
    if(!n || n>max_frame || busy())return false;
    memcpy(tx,p,n); txSize.store(unsigned(n),std::memory_order_release);return true;
  }
  int read(uint64_t* receivedUs=nullptr) {
    unsigned t=tail.load(std::memory_order_relaxed);
    if(t==head.load(std::memory_order_acquire))return -1;
    uint8_t c=rx[t];if(receivedUs)*receivedUs=rxTime[t];tail.store((t+1)&255,std::memory_order_release);return c;
  }
  // Return true to sink LOW, false for high impedance. No callbacks or waits.
  bool tick(bool high,uint64_t capturedUs=0) {
    ++tickCount;currentTickUs=capturedUs?capturedUs:tickCount*tick_us;
    if(cancelled.exchange(false,std::memory_order_acquire)) {
      txByte=txTick=idleTicks=rxTick=rxBit=0;receiving=driving=false;
      txSize.store(0,std::memory_order_release);return false;
    }
    unsigned n=txSize.load(std::memory_order_acquire);
    if(n) {
      if(!txTick && !txByte) {
        if(receiving || idleTicks<8) {receive(high);return false;}
      }
      if(txTick) {
        unsigned previousBit=(txTick-1)/ticks_per_bit;
        // Sample in a released data/stop bit; LOW here is contention/noise.
        if(previousBit && !driving && !high && txTick%ticks_per_bit==2)++collisions;
      }
      if(txTick==11*ticks_per_bit) {
        txTick=0;
        if(++txByte==n) {
          txByte=0;receiving=false;idleTicks=0;driving=false;
          txSize.store(0,std::memory_order_release);return false;
        }
      }
      unsigned bit=txTick++/ticks_per_bit;
      driving=bit==0 || (bit<=8 && !(tx[txByte]&(1u<<(bit-1))));
      return driving;
    }
    driving=false;receive(high);return false;
  }
private:
  void receive(bool high) {
    if(!receiving) {
      if(high) {if(idleTicks<8)++idleTicks;}
      else if(idleTicks>=4) {receiving=true;rxTick=0;rxBit=0;rxByte=0;idleTicks=0;}
      else idleTicks=0;
      return;
    }
    ++rxTick;
    if(rxTick==2 && high) {receiving=false;idleTicks=0;++framing;return;}
    if(rxTick>=6 && (rxTick-6)%4==0 && rxBit<8) {
      if(high)rxByte|=uint8_t(1u<<rxBit);++rxBit;
    }
    if(rxTick==38) {
      if(!high)++framing;
      else {
        unsigned h=head.load(std::memory_order_relaxed),next=(h+1)&255;
        if(next==tail.load(std::memory_order_acquire))++overflow;
        else {rx[h]=rxByte;rxTime[h]=currentTickUs;head.store(next,std::memory_order_release);}
      }
      receiving=false;idleTicks=high?1:0;
    }
  }
};
}

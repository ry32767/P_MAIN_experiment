#pragma once
#include <SerialPIO.h>
#include "spresense_protocol.h"
struct SpresenseStatus {
 spnav::Nav nav;
 uint64_t sampledUs=0,receivedUs=0,ppsUs=0,utcUs=0;
 uint32_t packets=0,crcErrors=0,rejected=0,ppsCount=0,periodUs=0,uartAgeUs=0,overflows=0,bytes=0;
 bool synchronized=false;
};
SpresenseStatus sharedSpresense;
SerialPIO spresenseUart(-1,PIN_SPRESENSE_RX,256); // Receive only: never drive GP7.
volatile uint64_t spresensePpsUs=0;
volatile uint32_t spresensePpsCount=0;
void onSpresensePps(){spresensePpsUs=time_us_64();++spresensePpsCount;}
void initSpresense(){
 pinMode(PIN_GNSS_PPS,INPUT);pinMode(PIN_SPRESENSE_RX,INPUT);
 attachInterrupt(digitalPinToInterrupt(PIN_GNSS_PPS),onSpresensePps,RISING);
 spresenseUart.begin(115200);
}
void pollSpresense(){
 static spnav::Parser parser;static spnav::Clock clock;static SpresenseStatus current;static uint32_t seen=0;
 uint32_t saved=save_and_disable_interrupts();uint64_t edge=spresensePpsUs;uint32_t count=spresensePpsCount;restore_interrupts(saved);
 if(count!=seen){clock.pps(edge);seen=count;current.ppsUs=edge;current.ppsCount=count;}
 if(spresenseUart.overflow()){++current.overflows;parser.reset();clock.invalidate();}
 spnav::Nav nav;
 // Bounded polling preserves tether scheduling even if the incoming line is noisy.
 for(unsigned budget=0;budget<256 && spresenseUart.available();++budget){
  ++current.bytes;
  if(parser.push(uint8_t(spresenseUart.read()),nav)){current.receivedUs=time_us_64();current.nav=nav;++current.packets;clock.accept(nav,current.receivedUs);}
 }
 current.sampledUs=time_us_64();current.synchronized=clock.locked(current.sampledUs);current.utcUs=clock.utc(current.sampledUs);
 current.crcErrors=parser.errors;current.rejected=clock.rejected;current.periodUs=clock.interval_us;current.uartAgeUs=clock.uart_age_us;
 mutex_enter_blocking(&stateMutex);sharedSpresense=current;mutex_exit(&stateMutex);
}
SpresenseStatus spresenseSnapshot(){mutex_enter_blocking(&stateMutex);auto s=sharedSpresense;mutex_exit(&stateMutex);return s;}
bool spresenseFresh(const SpresenseStatus& s,uint64_t now){return s.packets && now>=s.receivedUs && now-s.receivedUs<3000000;}
bool spresenseLocked(const SpresenseStatus& s,uint64_t now){return s.synchronized && now>=s.sampledUs && now-s.sampledUs<100000 && now>=s.ppsUs && now-s.ppsUs<1500000 && now>=s.receivedUs && now-s.receivedUs<1500000;}

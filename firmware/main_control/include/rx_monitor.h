#pragma once
#ifndef BOARD_PARENT
#include <hardware/adc.h>
// Core 0 owns ADC exclusively while monitoring. 48 MHz / (479+1) = 100 kSa/s.
alignas(32768) static uint16_t monitorBuffer[16384];
static bool monitorActive=false,monitorWeb=false;
static uint32_t monitorMin=3300,monitorPeak=0,monitorBins=0;
static uint64_t monitorSum=0;
static int monitorDma=-1;
static uint32_t monitorRead=0,monitorLost=0,monitorStartMs=0;
static uint64_t monitorStartUs=0;
static void monitorStop(){
 if(!monitorActive)return;
 adc_run(false);dma_channel_abort(monitorDma);adc_fifo_drain();adc_fifo_setup(false,false,1,false,false);adc_set_clkdiv(0);
 monitorActive=false;Serial.println("# MONITOR_STOPPED");
}
static void monitorStart(){
 if(monitorActive || !stopped || !powered){Serial.println("# MONITOR_REQUIRES_STOPPED_SD");return;}
 if(monitorDma<0)monitorDma=dma_claim_unused_channel(false);
 if(monitorDma<0){Serial.println("# MONITOR_DMA_UNAVAILABLE");return;}
 adc_init();adc_gpio_init(PIN_RX_ENV);adc_select_input(PIN_RX_ENV-26);adc_fifo_drain();adc_set_clkdiv(479);
 adc_fifo_setup(true,true,1,false,false);
 auto cfg=dma_channel_get_default_config(monitorDma);
 channel_config_set_transfer_data_size(&cfg,DMA_SIZE_16);channel_config_set_read_increment(&cfg,false);channel_config_set_write_increment(&cfg,true);
 channel_config_set_ring(&cfg,true,15);channel_config_set_dreq(&cfg,DREQ_ADC);
 // RP2350 count is 28 bits; finite count comfortably exceeds the 75 s watchdog.
 dma_channel_configure(monitorDma,&cfg,monitorBuffer,&adc_hw->fifo,0x0fffffff,true);
 monitorMin=3300;monitorPeak=monitorBins=0;monitorSum=0;
 monitorRead=monitorLost=0;monitorStartMs=millis();monitorStartUs=time_us_64();monitorActive=true;adc_run(true);
 Serial.println("# MONITOR_STARTED 100000 samples/s 20ms bins");
}
static void monitorTick(){
 if(!monitorActive)return;
 if(millis()-monitorStartMs>=75000){bool resume=monitorWeb;monitorStop();monitorWeb=false;if(resume)mountSd();return;}
 uint32_t end=0x0fffffff-dma_hw->ch[monitorDma].transfer_count;
 if(end-monitorRead>16384){monitorLost+=end-monitorRead;monitorRead=end;return;}
 while(end-monitorRead>=2000){
  uint32_t sum=0,lo=4095,hi=0;
  for(unsigned i=0;i<2000;i++){uint32_t v=monitorBuffer[(monitorRead+i)&16383]&4095;sum+=v;lo=min(lo,v);hi=max(hi,v);}
  monitorRead+=2000;
  monitorMin=min(monitorMin,lo*3300/4095);monitorPeak=max(monitorPeak,hi*3300/4095);monitorSum+=uint64_t(sum)*3300/4095/2000;monitorBins++;
  if(!monitorWeb)Serial.printf("{\"rx_plot\":true,\"t_us\":%llu,\"min_mv\":%lu,\"mean_mv\":%lu,\"peak_mv\":%lu,\"cmp\":%u,\"edges\":%lu,\"lost\":%lu}\n",
   (unsigned long long)(monitorStartUs+uint64_t(monitorRead)*10),(unsigned long)(lo*3300/4095),(unsigned long)(uint64_t(sum)*3300/4095/2000),(unsigned long)(hi*3300/4095),unsigned(digitalRead(PIN_RX_COMP)),(unsigned long)edgeCount.load(),(unsigned long)monitorLost);
 }
}
#else
static constexpr bool monitorActive=false;
#endif

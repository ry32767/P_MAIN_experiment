#pragma once
#ifndef BOARD_PARENT
// Core 0 owns ADC and SD. Core 1 sends timestamped requests/edges only.
static int multipathDma=-1;
static constexpr uint32_t captureDmaCount=0x00ffffff,captureBufferSamples=16384;
static uint64_t multipathStartUs=0,multipathTimeoutUs=0;
static aq::CaptureRequest multipathRequest;
static uint32_t multipathRead=0,multipathEndIndex=0,multipathWritten=0,multipathLost=0;
static uint32_t multipathBaseEdgeDrops=0,multipathBaseRequestDrops=0,multipathSkipped=0;
static bool multipathTriggered=false;
static uint32_t captureCount(){return captureDmaCount-dma_hw->ch[multipathDma].transfer_count;}
static void multipathPublish(){
 mutex_enter_blocking(&stateMutex);sharedHealth.observationShot=multipathRequest.shot;sharedHealth.observationSamples=multipathWritten;
 sharedHealth.observationLost=multipathLost+captureEdgeDrops.load()-multipathBaseEdgeDrops+captureRequestDrops.load()-multipathBaseRequestDrops;
 sharedHealth.observationSkipped=multipathSkipped;sharedHealth.observationActive=multipathActive;mutex_exit(&stateMutex);
}
static void multipathDrainEdges(){
 aq::CaptureEdge e;
 for(unsigned budget=0;budget<64 && captureEdges.pop(e);budget++){
  if(!sdHealthy || !multipathTriggered || e.shot!=multipathRequest.shot || e.us<multipathRequest.arrival || e.us>multipathRequest.arrival+uint64_t(multipathRequest.seconds)*1000000)continue;
  aqbin::Edge edge{e.shot,e.high};if(!writeBinary(observationFile,aqbin::COMPARATOR,e.us,e.shot,edge))break;
 }
}
void multipathStop(uint8_t reason){
 if(!multipathActive)return;
 adc_run(false);dma_channel_abort(multipathDma);adc_fifo_drain();adc_fifo_setup(false,false,1,false,false);adc_set_clkdiv(0);
 // Drain all queued edge records before the end marker, with a finite bound.
 for(unsigned i=0;i<32 && captureEdges.head.load()!=captureEdges.tail.load();i++)multipathDrainEdges();
 if(sdHealthy){aqbin::CaptureEnd end{multipathRequest.shot,multipathWritten,multipathLost,captureEdgeDrops.load()-multipathBaseEdgeDrops,captureRequestDrops.load()-multipathBaseRequestDrops,reason};writeBinary(observationFile,aqbin::CAPTURE_END,time_us_64(),multipathRequest.shot,end);}
 multipathActive=false;multipathTriggered=false;multipathPublish();
}
static void multipathReject(const aq::CaptureRequest& req){
 multipathSkipped++;if(sdHealthy){aqbin::CaptureEnd end{req.shot,0,0,0,0,aqbin::UNAVAILABLE};writeBinary(observationFile,aqbin::CAPTURE_END,time_us_64(),req.shot,end);}
 multipathPublish();
}
static void multipathPrepare(const aq::CaptureRequest& req){
 if(multipathActive || monitorActive || !sdHealthy || !captureEnabled || stopRequested || !powered){multipathReject(req);return;}
 if(multipathDma<0)multipathDma=dma_claim_unused_channel(false);
 if(multipathDma<0){multipathReject(req);return;}
 multipathRequest=req;multipathWritten=multipathLost=0;multipathTriggered=false;
 multipathBaseEdgeDrops=captureEdgeDrops.load();multipathBaseRequestDrops=captureRequestDrops.load();
 // Share the existing monitor buffer; gain monitoring and observation are exclusive.
 adc_init();adc_gpio_init(PIN_RX_ENV);adc_select_input(PIN_RX_ENV-26);adc_fifo_drain();adc_set_clkdiv(2399);
 adc_fifo_setup(true,true,1,true,false);auto cfg=dma_channel_get_default_config(multipathDma);
 channel_config_set_transfer_data_size(&cfg,DMA_SIZE_16);channel_config_set_read_increment(&cfg,false);channel_config_set_write_increment(&cfg,true);
 channel_config_set_ring(&cfg,true,15);channel_config_set_dreq(&cfg,DREQ_ADC);
 dma_channel_configure(multipathDma,&cfg,monitorBuffer,&adc_hw->fifo,captureDmaCount,true);
 multipathRead=0;multipathStartUs=time_us_64();multipathTimeoutUs=multipathStartUs+700000;multipathActive=true;adc_run(true);multipathPublish();
}
static void multipathTrigger(const aq::CaptureRequest& req){
 if(!multipathActive || multipathTriggered || req.shot!=multipathRequest.shot || req.session!=multipathRequest.session)return;
 multipathRequest=req;multipathRead=aq::firstEnvelopeIndex(multipathStartUs,req.arrival);
 multipathEndIndex=aq::firstEnvelopeIndex(multipathStartUs,req.arrival+uint64_t(req.seconds)*1000000);multipathTriggered=true;
 aqbin::CaptureBegin begin{req.session,req.shot,req.frequency,aq::envelopeRate,req.reference,req.arrival,multipathStartUs,multipathRead,uint32_t(req.seconds)*aq::envelopeRate,req.seconds};
 if(sdHealthy)writeBinary(observationFile,aqbin::CAPTURE_BEGIN,req.arrival,req.shot,begin);
 // A trigger earlier than ADC setup cannot recover the leading samples.
 if(req.arrival<multipathStartUs)multipathLost+=uint32_t((multipathStartUs-req.arrival+aq::envelopePeriodUs-1)/aq::envelopePeriodUs);
}
void multipathTick(){
 aq::CaptureRequest req;
 while(captureRequests.pop(req)){if(req.action==aq::CAPTURE_ARM)multipathPrepare(req);else if(req.action==aq::CAPTURE_TRIGGER)multipathTrigger(req);}
 if(!multipathActive){aq::CaptureEdge e;for(unsigned i=0;i<64 && captureEdges.pop(e);i++){}return;}
 if(!sdHealthy || !captureEnabled){multipathStop(sdHealthy?aqbin::STOPPED:aqbin::SD_FAILURE);return;}
 uint64_t now=time_us_64();
 if(!multipathTriggered){if(now>multipathTimeoutUs)multipathStop(aqbin::NO_ARRIVAL);return;}
 multipathDrainEdges();
 struct Block {aqbin::EnvelopePrefix prefix;uint16_t samples[1024];} __attribute__((packed));
 for(unsigned budget=0;budget<2;budget++){
  uint32_t end=captureCount();
  auto plan=aq::envelopeRead(multipathRead,end,multipathEndIndex,captureBufferSamples,1024);
  multipathRead=plan.index;multipathLost+=plan.lost;if(!plan.count)break;
  Block block{};block.prefix={multipathRequest.shot,multipathRead,uint16_t(plan.count),uint16_t(aq::envelopePeriodUs)};
  for(unsigned i=0;i<block.prefix.count;i++)block.samples[i]=monitorBuffer[(multipathRead+i)&(captureBufferSamples-1)];
  // DMA can continue during copying: never pass overwritten data off as complete.
  if(captureCount()-multipathRead>captureBufferSamples)continue;
  if(writeBinary(observationFile,aqbin::ENVELOPE,multipathStartUs+uint64_t(multipathRead)*aq::envelopePeriodUs,multipathRequest.shot,&block,sizeof(block.prefix)+2*block.prefix.count)){multipathWritten+=block.prefix.count;multipathRead+=block.prefix.count;}else break;
 }
 if(multipathRead>=multipathEndIndex)multipathStop(aqbin::COMPLETE);
 else if(now>multipathRequest.arrival+uint64_t(multipathRequest.seconds)*1000000+1000000){multipathLost+=multipathEndIndex-multipathRead;multipathStop(aqbin::SD_FAILURE);}
 multipathPublish();
}
#endif

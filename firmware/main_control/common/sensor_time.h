#pragma once
#include <stdint.h>
#include <math.h>
namespace sensortime {
inline bool sampleIntervalValid(uint32_t step,uint32_t rate){
 if(!rate || rate>19200000)return false;uint32_t nominal=19200000/rate;
 // Actual Sony timestamps jitter within normal samples; equality to the
 // nominal 20,000 ticks would reject every sample in the measured stream.
 return step && step>=nominal/2 && step<=nominal+nominal/2;
}
struct Edge {uint64_t mono;uint32_t count;Edge(uint64_t m=0,uint32_t c=0):mono(m),count(c){}};
// PPS disciplines the timestamp conversion, not the IMU sampling oscillator.
// Serial timing selects the pulse identity only; the captured edge defines phase.
class PpsClock {
 Edge history[8]{};unsigned next=0;Edge anchor{},previousPair{};
 uint64_t epoch=0,previousUtc=0;unsigned good=0;double scale=1;
public:
 uint32_t period=0,rejected=0;Edge latest{};
 void edge(Edge e){
  if(!e.count||!e.mono)return;
  bool consecutive=latest.count && uint32_t(e.count-latest.count)==1 && e.mono>latest.mono;
  period=consecutive?uint32_t(e.mono-latest.mono):0;
  if(!consecutive || period<990000 || period>1010000){good=0;previousPair={};}
  latest=e;history[next++%8]=e;
 }
 void invalidate(){good=0;previousPair={};}
 bool pair(uint64_t labelUtc,uint64_t labelAge,uint64_t receiveMono,uint32_t transit){
  // Only parent GNSS-labelled PPS is passed here. No phone/RTC pulse labels.
  if(labelUtc<1577836800000000ULL || labelUtc>=4102444800000000ULL || labelUtc%1000000 || labelAge>1500000 ||
     labelAge+transit>receiveMono){invalidate();++rejected;return false;}
  uint64_t target=receiveMono-labelAge-transit,bestError=UINT64_MAX;Edge best{};
  for(auto e:history){if(!e.count)continue;uint64_t error=e.mono>target?e.mono-target:target-e.mono;if(error<bestError){bestError=error;best=e;}}
  if(!best.count || bestError>100000){invalidate();++rejected;return false;}
  if(previousPair.count==best.count){if(previousUtc==labelUtc)return true;invalidate();++rejected;return false;}
  uint32_t steps=best.count-previousPair.count;
  bool continuous=previousPair.count && steps>=1 && steps<=10 && labelUtc>previousUtc &&
   labelUtc-previousUtc==uint64_t(steps)*1000000 && best.mono>previousPair.mono &&
   best.mono-previousPair.mono>=uint64_t(steps)*990000 && best.mono-previousPair.mono<=uint64_t(steps)*1010000;
  scale=continuous?double(labelUtc-previousUtc)/double(best.mono-previousPair.mono):1;
  good=continuous?(good<3?good+1:3):1;anchor=best;epoch=labelUtc;previousPair=best;previousUtc=labelUtc;return true;
 }
 bool locked(uint64_t now)const{return good>=3 && latest.count && now>=latest.mono && now-latest.mono<1500000 && now>=anchor.mono && now-anchor.mono<6000000;}
 bool fresh(uint64_t now)const{return latest.count && now>=latest.mono && now-latest.mono<1500000;}
 uint64_t anchorMono()const{return anchor.mono;}
 uint64_t anchorUtc()const{return epoch;}
 uint64_t utc(uint64_t sample,uint64_t now)const{
  if(!locked(now))return 0;
  // Allow delayed samples on either side of the labelled edge, within history.
  if(sample>=anchor.mono)return sample-anchor.mono<8000000?epoch+uint64_t(double(sample-anchor.mono)*scale):0;
  return anchor.mono-sample<8000000?epoch-uint64_t(double(anchor.mono-sample)*scale):0;
 }
};
// Affine reconstruction from the free-running sensor counter and host readouts.
// Lower-envelope readouts reduce scheduling latency; unknown fixed latency stays
// unknown. This is explicitly an estimate, never hardware sampling-phase lock.
class ImuClock {
 uint32_t previousRaw=0;uint64_t ticks=0,start=0,lastRead=0,window=0,lastOutput=0;
 double offset=0,slope=1000000.0/19200000.0,windowMin=0,previousMin=0,previousX=0,minimumX=0;
 bool have=false,previousWindow=false;unsigned windows=0;
public:
 uint32_t generation=0,resets=0;bool ready=false;uint64_t extended=0;
 void reset(){have=false;ready=false;previousWindow=false;windows=0;lastOutput=0;++generation;++resets;}
 uint64_t feed(uint32_t raw,uint64_t read){
  uint32_t delta=raw-previousRaw;
  if(have && (read<lastRead || read-lastRead>1000000 || !delta || delta>19200000))reset();
  if(!have){have=true;previousRaw=raw;start=lastRead=window=read;ticks=0;extended=raw;offset=double(read);windowMin=double(read);minimumX=0;slope=1000000.0/19200000.0;return 0;}
  ticks+=delta;extended+=delta;previousRaw=raw;lastRead=read;double x=double(ticks),y=double(read)-x*slope;
  if(y<windowMin){windowMin=y;minimumX=x;}
  if(read-window>=1000000){
   double minimumY=windowMin+minimumX*slope;
   if(previousWindow && minimumX>previousX){
    double fitted=(windowMin+minimumX*slope-previousMin)/(minimumX-previousX);
    if(fitted>1000000.0/19200000.0*.99 && fitted<1000000.0/19200000.0*1.01){
     // Update frequency without jumping the current phase to a noisy readout.
     // A bad fit is ignored; it does not discard a valid counter history.
     double present=offset+x*slope;slope+=.05*(fitted-slope);offset=present-x*slope;ready=true;
    }
   }
   previousMin=minimumY;previousX=minimumX;previousWindow=true;++windows;
   window=read;windowMin=double(read)-x*slope;minimumX=x;
  }
  double estimated=offset+x*slope;
  // A sample cannot occur after readout. Learn the lower latency envelope even
  // during warmup. FIFO bursts can share a host tick; withhold ambiguous rows
  // instead of repeatedly restarting the entire clock estimator.
  if(estimated>double(read)){offset-=estimated-double(read);estimated=double(read);}
  if(estimated<0 || double(read)-estimated>1000000){reset();return 0;}
  if(!ready)return 0;
  uint64_t result=uint64_t(estimated);
  if(lastOutput && result<=lastOutput)return 0;lastOutput=result;return result;
 }
};
}

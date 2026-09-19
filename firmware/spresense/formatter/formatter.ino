#include <Arduino.h>
#include <MP.h>
#include <ImuBatch.h>
#if !defined(SUBCORE) || SUBCORE != 1
#error Build formatter for SubCore 1
#endif
void setup() { MP.begin(); }
void loop() {
  int8_t id; void *address=nullptr;
  if(MP.Recv(&id,&address)<0) return; // Block only this dedicated worker.
  if(id!=10 || !address) return;
  auto &batch=*static_cast<aqimu::Batch*>(address);
  aqimu::format(batch);
  __sync_synchronize();
  MP.Send(11,batch.id); // MainCore checks response and has a timeout.
}

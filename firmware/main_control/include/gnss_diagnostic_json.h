#pragma once
String gnssDiagnosticJson(const GnssStatus& g,uint64_t now){
 const auto& c=g.signalSettings;const auto& v=g.signals;const auto& r=g.corrections;
 bool cf=c.receivedUs&&now>=c.receivedUs&&now-c.receivedUs<60000000;
 bool vf=v.receivedUs&&now>=v.receivedUs&&now-v.receivedUs<15000000;
 bool rf=r.receivedUs&&now>=r.receivedUs&&now-r.receivedUs<15000000;
 String s=",\"diagnostics\":{\"settings_fresh\":"+String(cf?"true":"false")+",\"settings_complete\":"+String(cf&&c.complete()?"true":"false");
 s+=",\"l5_enable_attempted\":"+String(g.l5Enable.attempted?"true":"false")+",\"l5_enable_ack\":"+String(g.l5Enable.acknowledged?"true":"false")+",\"l5_enable_failed\":"+String(g.l5Enable.failed?"true":"false")+",\"l5_enable_verified\":"+String(g.l5Enable.verified?"true":"false");
 const char* names[]={"gps_enabled","gps_l1_enabled","gps_l5_enabled","galileo_enabled","galileo_e1_enabled","galileo_e5a_enabled","dgnss_mode"};
 for(unsigned i=0;i<7;i++){s+=",\""+String(names[i])+"\":";if(!cf||!(c.known&(1u<<i)))s+="null";else if(i==6)s+=String(c.values[i]);else s+=c.values[i]?"true":"false";}
 s+=",\"signals_fresh\":"+String(vf?"true":"false")+",\"signal_reports\":"+String(v.reports);
 auto count=[&](const char* name,uint16_t value){s+=",\""+String(name)+"\":"+(vf?String(value):String("null"));};
 count("tracked_gps_l5",v.gpsL5);count("tracked_galileo_e1",v.galE1);count("tracked_galileo_e5a",v.galE5a);count("galileo_dual_phase_used",v.galDualPhase);count("phase_used",v.phaseUsed);count("phase_corrections_used",v.phaseCorrected);
 s+=",\"rtcm_status_fresh\":"+String(rf?"true":"false")+",\"rtcm_reports\":"+String(r.reports)+",\"rtcm_used\":"+String(r.used)+",\"rtcm_unused\":"+String(r.unused)+",\"rtcm_unknown\":"+String(r.unknown)+",\"rtcm_receiver_crc_failed\":"+String(r.crcFailed)+",\"last_rtcm_type\":"+(rf?String(r.lastType):String("null"))+",\"last_rtcm_station\":"+(rf?String(r.lastStation):String("null"))+"}";
 return s;
}

// bowing_lab: replay notes with a playing transport and log each note's bow
// direction and bow position, and any bow change mid-note (a rebow, heard
// as an extra note). Checks "bowing to the meter" (docs/MODEL.md).
//   python3 -c '...' | bowing_lab METRIC [BPM NUM DEN]
// stdin: one event per line, "beat note on(1/0) velocity(0-1)".
// Example (pickup, downbeat, three quarters in 3/4):
//   printf '2.5 69 1 .6\n2.95 69 0 .5\n3 74 1 .6\n3.95 74 0 .5\n' | ./build/bowing_lab 0.5 92 3 4
#include "LabInstrument.h"
#include "../model/Instrument.h"
#include "../model/Meter.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <algorithm>
using namespace sm;
struct Ev{double beat; int note; bool on; float vel;};
int main(int argc,char**argv){
  if (argc < 2) { std::fprintf(stderr, "usage: bowing_lab METRIC [BPM NUM DEN] < events\n"); return 1; }
  const float sr=48000; const float metric=static_cast<float>(std::atof(argv[1]));
  const double bpm = argc > 2 ? std::atof(argv[2]) : 92.0; const int num = argc > 3 ? std::atoi(argv[3]) : 3, den = argc > 4 ? std::atoi(argv[4]) : 4;
  const double spb=60.0/bpm;
  std::vector<Ev> ev; double b; int n, on; float v;
  while (std::scanf("%lf %d %d %f",&b,&n,&on,&v)==4) ev.push_back({b,n,on!=0,v});
  std::sort(ev.begin(),ev.end(),[](const Ev&a,const Ev&c){return a.beat<c.beat;});
  Instrument inst; inst.prepare(sr, labInstrument()); PerformerControls pc; InstrumentControls ic; pc.metric=metric; pc.portamento=0; pc.avoidOpen=false; pc.autoChange=true;  // worst case: long notes may run out of bow
  double pendT=-1; float pendP=0; size_t k=0; float lastPos=inst.performer().bowPosition(); int lastSign=0; int held=0; double lastOnT=-1;
  const double endBeat = ev.empty() ? 0 : ev.back().beat + 2; for(long i=0;i<long(endBeat*spb*sr);++i){
    double t=i/sr, beat=t/spb;
    while(k<ev.size() && ev[k].beat<=beat){ auto&e=ev[k++];
      if(e.on){ inst.performer().setMeter(metricStrength(e.beat,num,den)); inst.performer().noteOn(e.note,e.vel,pc); ++held; lastOnT=t;
        pendT=t+0.05; pendP=inst.performer().bowPosition(); std::printf("%6.3fs beat %5.2f  note %d on  (strength %.2f, bow at %.2f)",t,e.beat,e.note,metricStrength(e.beat,num,den),pendP); }
      else { inst.performer().noteOff(e.note,0.5f,pc); --held; } }
    inst.process(pc,ic);
    if(pendT>0 && t>=pendT){ std::printf("  %s-bow\n", inst.performer().bowPosition()>pendP?"down":"up"); pendT=-1; }
    float p=inst.performer().bowPosition(); int sg=(p>lastPos+1e-7f)?1:((p<lastPos-1e-7f)?-1:0);
    if(sg!=0){ if(lastSign!=0 && sg!=lastSign && held>0 && t-lastOnT>0.03) std::printf("%6.3fs           ** mid-note bow reversal at %.2f (now %s)\n",t,p,sg>0?"down":"up"); lastSign=sg; }
    lastPos=p;
  }
}

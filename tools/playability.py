#!/usr/bin/env python3
"""The playability assistant through the synth: for held notes on each string
(open, +4, +7 semitones, the string forced), dynamics 0 / 0.3 / 0.6 / 0.9
(Velocity -> Dynamics 0, no attack swell, vibrato off, Pickup output), sweep
Pressure over its range (-1..+1 by 0.25) and mark the notes in Helmholtz motion
from 0.5 to 1.4 s: H when >= 90% of 20 ms frames are periodic at the note
(autocorrelation > 0.9) and not at half its period (< 0.5: no double slip),
h when >= 60%. A well-placed assistant is clean at Pressure 0 with margin on
both sides. Reference (docs/EVALUATION.md): the violin, mean margin 0.62.

calibrate_force.py measures the same in instrument_lab by counting slips; on
the cello's strings that count runs a few percent high at f-ff (the two-point
bow), so this one judges the sound instead.

    python3 tools/playability.py                 # the cello
    INST=violin python3 tools/playability.py     # the violin (R=<render>, J=<jobs>, OFFS=0,7)
"""
import json, os, subprocess, sys, tempfile, itertools, numpy as np
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import iowa
inst=os.environ.get("INST","cello"); R=os.environ.get("R","build/stringmodel_render")
opens=[36,43,50,57] if inst=="cello" else [55,62,69,76]; names="CGDA" if inst=="cello" else "GDAE"
offs=[int(x) for x in os.environ.get("OFFS","0,4,7").split(",")]
dyns=[0.0,0.3,0.6,0.9]; ps=[round(-1+0.25*i,2) for i in range(9)]  # -1 .. 1 (the control's range)
def clean(x,sr,note):
    q=max(1,int(sr//24000)); from scipy import signal
    y=signal.resample_poly(x,1,q); fs=sr/q; want=440*2**((note-69)/12)
    lag=fs/want; w=int(4*lag); ok=0; tot=0
    for i in range(int(0.5*fs),int(1.4*fs)-w-int(lag*1.2),int(0.02*fs)):
        a=y[i:i+w]; best=-1; bl=0
        for L in range(int(lag*0.97),int(lag*1.03)+2):
            b=y[i+L:i+L+w]; r=np.dot(a,b)/np.sqrt(np.dot(a,a)*np.dot(b,b)+1e-20)
            if r>best: best,bl=r,L
        h=int(round(bl/2)); bh=y[i+h:i+h+w]; rh=np.dot(a,bh)/np.sqrt(np.dot(a,a)*np.dot(bh,bh)+1e-20)
        tot+=1; ok+= (best>0.9 and rh<0.5)
    return ok/max(tot,1)
def run(a):
    s,n,d,p=a
    with tempfile.TemporaryDirectory() as t:
        sc={"patch":"init","length":1.6,"params":{"instrument":1 if inst=="cello" else 0,"string.select":s+1,"vib.depth":0,"room.mix":0,"out.mode":2,
            "bow.velSens":0,"bow.dynamics":d,"bow.pressure":p,"bow.swell":0},"events":[{"t":0.02,"on":n,"vel":100},{"t":1.5,"off":n}]}
        json.dump(sc,open(t+"/s.json","w")); subprocess.run([R,t+"/s.json","--out",t+"/s.wav"],capture_output=True)
        sr,x=iowa.load(t+"/s.wav"); return a,clean(x,sr,n)
jobs=[(s,opens[s]+o,d,p) for s in range(4) for o in offs for d in dyns for p in ps]
with ThreadPoolExecutor(int(os.environ.get("J","2"))) as ex: Rz=dict(ex.map(run,jobs))
tot=0;cells=0;worst=9
for s in range(4):
  for o in offs:
    n=opens[s]+o; row=[]
    for d in dyns:
      c="".join("H" if Rz[(s,n,d,p)]>=0.9 else ("h" if Rz[(s,n,d,p)]>=0.6 else ".") for p in ps)
      okp=[p for p in ps if Rz[(s,n,d,p)]>=0.9]
      if 0 in okp:
        i=ps.index(0); lo=i; hi=i
        while lo>0 and Rz[(s,n,d,ps[lo-1])]>=0.9: lo-=1
        while hi<len(ps)-1 and Rz[(s,n,d,ps[hi+1])]>=0.9: hi+=1
        m=min(-ps[lo],ps[hi])
      else: m=-1
      tot+=m; cells+=1; worst=min(worst,m); row.append("d%.1f %s"%(d,c))
    print("%s %2d  %s"%(names[s],n,"  ".join(row)),flush=True)
print("Pressure -1..+1 by 0.25 (0 in the middle). H clean, h mostly. mean margin %.2f, worst %.2f"%(tot/cells,worst))

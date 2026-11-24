#!/usr/bin/env python3
"""Harmonics' timbre against the Philharmonia recordings (never commit them).
For each natural/artificial harmonic recording (mf, 1 s, up to A6) and the
arco-normal note at the same pitch, and the synth's Harmonics and Arco at
that pitch: partials 2-6 re the 1st (dB). Reports the median over notes of
(synth harmonic - synth arco) - (real harmonic - real arco): how much purer
(or not) a harmonic is than an ordinary note, with mics and body cancelled.
Recordings labelled at another pitch than they sound are skipped. -v per note.

    python3 tools/harmonic_timbre.py --data <phil wav> [render] [-v]"""
import glob, json, os, re, subprocess, sys, tempfile, numpy as np
from scipy.io import wavfile
D=os.path.expanduser("~/data/phil/wav")
if "--data" in sys.argv: i=sys.argv.index("--data"); D=sys.argv[i+1]; del sys.argv[i:i+2]; R=[a for a in sys.argv[1:] if not a.startswith("-")][0] if [a for a in sys.argv[1:] if not a.startswith("-")] else "build/stringmodel_render"
NM={"C":0,"D":2,"E":4,"F":5,"G":7,"A":9,"B":11}
def midi(s):
    m=re.match(r"([A-G])(s?)(\d)",s); return 12*(int(m.group(3))+1)+NM[m.group(1)]+(1 if m.group(2) else 0)
def load(p):
    sr,x=wavfile.read(p); x=x.astype(float); x=x.mean(1) if x.ndim>1 else x; return sr,x
def spec(sr,x,f0,t0=None):
    e=np.abs(x); on=np.argmax(e>0.1*e.max())/sr if t0 is None else t0
    seg=x[int((on+0.25)*sr):int((on+0.65)*sr)]
    S=np.abs(np.fft.rfft(seg*np.hanning(len(seg)),1<<17))**2; f=np.fft.rfftfreq(1<<17,1/sr)
    def band(a,b): return S[(f>a)&(f<b)].sum()+1e-30
    # strongest peak 100 Hz .. 1.3 f0 to check the sounding pitch
    m=(f>100)&(f<1.3*f0); fpk=f[m][np.argmax(S[m])]
    h=[10*np.log10(band(n*f0*0.985,n*f0*1.015)) for n in range(1,7)]
    harm=sum(band(n*f0*0.985,n*f0*1.015) for n in range(1,40) if n*f0<16000)
    tot=band(80,16000)
    return fpk, [v-h[0] for v in h[1:]], 10*np.log10(max(tot-harm,1e-30)/harm)
def render(n,art):
    with tempfile.TemporaryDirectory() as t:
        json.dump({"patch":"init","length":1.5,"params":{"room.mix":0,"artic":art,"vib.depth":0},"events":[{"t":0,"on":n,"vel":80},{"t":1.2,"off":n}]},open(t+"/s.json","w"))
        subprocess.run([R,t+"/s.json","--out",t+"/s.wav"],check=True,capture_output=True); return load(t+"/s.wav")
rows=[]; diffs=[]
for p in sorted(glob.glob(D+"/*_1_mezzo-forte_*-harmonic.wav")):
    nm=os.path.basename(p).split("_")[1]; kind=p.split("_")[-1][:4]; n=midi(nm)
    if n>93: continue
    f0=440*2**((n-69)/12)
    arc=glob.glob(f"{D}/violin_{nm}_1_mezzo-forte_arco-normal.wav") or glob.glob(f"{D}/violin_{nm}_*_mezzo-forte_arco-normal.wav")
    rh=spec(*load(p),f0); ra=spec(*load(arc[0]),f0) if arc else None
    sh=spec(*render(n,5),f0,0.0); sa=spec(*render(n,0),f0,0.0)
    fmt=lambda r:"pk %5.0f h2-6 %s noise %+5.1f"%(r[0]," ".join("%+4.0f"%v for v in r[1]),r[2])
    if abs(rh[0]/f0-1) > 0.03: continue   # mislabelled recording
    if not ra or abs(ra[0]/f0-1)>0.03: continue
    diffs.append(np.subtract(np.subtract(sh[1], sa[1]), np.subtract(rh[1], ra[1])))
    if "-v" in sys.argv: print(f"{nm:4s} {kind} real {fmt(rh)} | syn {fmt(sh)}")
if not diffs: sys.exit("no recordings found in " + D)
d=np.array(diffs); print("(syn harm-arco) - (real harm-arco), h2..h6 (median over %d harmonics): %s   mean |.| %.1f dB" % (len(d), " ".join("%+.1f"%v for v in np.median(d,0)), np.mean(np.abs(np.median(d,0)))))

#!/bin/sh
# Anatomy of the first automatic bow change on a held note (instrument_lab):
# dip depth, overshoot, and ms more than 6 dB down (the gap). LAB_HF=1
# measures above 1 kHz (the bridge force's low end hides the gap otherwise).
#   LAB_HF=1 tools/bow_change_gap.sh [note] ; VEL=0.3 ... for soft
# Phase 6 reference (A4, mf, above 1 kHz): ~15 ms gap (was ~25-30 ms).
n=${1:-69}
t0=$(./build/instrument_lab hold "$n" 12 "${VEL:-0.7}" 2>&1 | grep -m1 "bow change at" | awk '{print $4}')
[ -z "$t0" ] && { echo "no automatic bow change"; exit 1; }
LAB_TRACE=$(python3 -c "print('%.3f,%.3f'%($t0-0.05,$t0+0.25))") ./build/instrument_lab hold "$n" 12 "${VEL:-0.7}" 2>&1 |
  grep "^    t" | python3 -c "
import sys
L=[(float(l.split()[1]),float(l.split()[-2])) for l in sys.stdin]
st=L[0][1]; mn=min(L,key=lambda r:r[1]); after=[r for r in L if r[0]>=mn[0]]; mx=max(after,key=lambda r:r[1])
print('steady %.1f dB  dip %+.1f  overshoot %+.1f  >6 dB down %d ms  >3 dB up %d ms'%(st,mn[1]-st,mx[1]-st,sum(1 for r in L if r[1]<st-6)*5,sum(1 for r in L if r[1]>st+3)*5))"

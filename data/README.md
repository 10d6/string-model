# Data

Everything here is CC0 or derived only from CC0 sources (KB §13, Tier A), so it can ship.

| File | What | Source |
|---|---|---|
| `violin/violin_solo_ir_{left,right,mono_smooth}_48000.wav` | library-mic impulse responses (bridge force → recording) | 10d6/instrument-research `data/library_mic_responses`, derived from VSCO-2-CE (Versilian Studios, CC0) |
| `violin/violin_solo_response.csv` | the same responses as curves (1/24 octave) | as above |
| `violin/force_tilt.csv` | this synth's bridge force vs 1/n per 1/3 octave | `tools/force_tilt.py` on this synth's own renders |
| `violin/radiation_measured.csv` | real minus synth per harmonic band (body + radiation + the library's mics and room) | `tools/radiation_target.py`: VSCO-2-CE solo violin "Arco Vib" (CC0) vs this synth's Pickup output |
| `violin/radiation_target.csv` | the Modelled output's calibration target | `tools/combine_target.py` (library curve below 800 Hz, the measurement above) |
| `cello/cello_solo_response.csv`, `cello/cello_solo_ir_{left,right,mono_smooth}_48000.wav` | the cello's library-mic response and IRs (8.5): solo, anechoic, mono (left = right) | fitted from the University of Iowa MIS cello arco (free of restrictions) with 10d6/instrument-research `tools/analysis/body_response.py` (all 24 runs, `60 1100`) and `export_ir.py`; `tools/gen_ir.py data/cello cello src/ir/CelloIR.h --iowa` |
| `cello/cello_section_response.csv` | the cello section's library-mic response (8.1-8.4; kept for reference) | 10d6/instrument-research `data/library_mic_responses`, derived from VSCO-2-CE (Versilian Studios, CC0) |
| `cello/radiation_target.csv` | the cello's calibration target | `tools/combine_target.py cello/cello_solo_response.csv <measured> … --from=60 --residual=<Modelled residual>`; measured and residual: `tools/radiation_target.py --iowa <Iowa arco> build/stringmodel_render <csv> [3]`; then `tools/gen_radiation.py … Cello model/data/CelloRadiation.h --iowa` |

Generated headers: `src/ir/ViolinIR.h`, `src/ir/CelloIR.h` (`tools/gen_ir.py`), `model/data/ViolinRadiation.h`, `model/data/CelloRadiation.h` (`tools/gen_radiation.py`, from `radiation_target.csv`).

The VSCO recordings themselves aren't kept here. Fetch them with
10d6/instrument-research's `tools/fetch_vsco.sh` (or sparse-check-out
`Strings/Solo Violin/Arco Vib/` from https://github.com/sgossner/VSCO-2-CE).

Credit: "derived from VSCO-2-CE (Versilian Studios, CC0)".

// Which instrument the labs test: SM_INSTRUMENT=cello (or 1) in the
// environment, else the violin. Labs only (getenv).
#pragma once

#include <cstdlib>
#include <cstring>

#include "../model/Radiation.h"
#include "../model/defs/Instruments.h"

namespace sm {

inline const InstrumentDef& labInstrument() {
  const char* e = std::getenv("SM_INSTRUMENT");
  if (e && (!std::strcmp(e, "cello") || !std::strcmp(e, "Cello") || !std::strcmp(e, "1"))) return cello();
  return violin();
}

// The radiation exactly as the synth prepares it for this instrument.
inline void prepareLabRadiation(Radiation& rad, float fs, const InstrumentDef& d = labInstrument()) {
  rad.prepare(fs, d.radiationHz, d.radiationDb, d.radiationCount, d.bodySeed ^ 0x9e3779b9u, d.hill.hz, d.radiationLowCut, d.shape);
}

}  // namespace sm

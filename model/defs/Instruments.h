// The instruments behind the Instrument switch, in its order.
#pragma once

#include "Cello.h"
#include "Violin.h"

namespace sm {

constexpr int kInstrumentCount = 2;

inline const InstrumentDef& instrumentDef(int i) { return i == 1 ? cello() : violin(); }

}  // namespace sm

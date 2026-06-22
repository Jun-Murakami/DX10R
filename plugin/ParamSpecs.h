#pragma once

#include "ParameterIDs.h"

// ParamSpecs — single source of truth for each IParam's range + default.
// Pure C++ (no iPlug2 dependency) so the plugin, the future WASM build and the
// unit tests can all link the same definitions and stay drift-free.
//
// Phase 1: the FM parameters are kept in mda DX10's native normalized [0,1]
// domain (the original DSP formulas are defined directly in terms of these), so
// the 32 factory presets map 1:1. Human-friendly display formatting is layered
// on in the WebUI (Phase 2). Master volume is a dB gain.

namespace dx10 {
namespace params {

struct ParamSpec
{
  const char* name;
  double defaultVal; // raw value in [minVal, maxVal]
  double minVal;
  double maxVal;
  const char* unit;  // iPlug2 label/unit ("" for the normalized FM params, "dB" for master)
};

// Returns the spec for an EParams index (0..kNumParams-1).
const ParamSpec& spec(int paramIdx);

} // namespace params
} // namespace dx10

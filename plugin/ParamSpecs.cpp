#include "ParamSpecs.h"

#include <array>
#include <cstddef>

namespace dx10 {
namespace params {

// Defaults seeded from mda DX10's first factory program, "Bright E.Piano"
// (mdaDX10.cpp fillpatch row 0), so a fresh instance is immediately a usable FM
// tone. kParamSustain defaults to 0 to preserve the original decay character.
static const std::array<ParamSpec, kNumParams> kSpecs = {{
    // name,          default, min,   max,   unit
    {"Attack",        0.000,   0.0,   1.0,   ""},
    {"Decay",         0.650,   0.0,   1.0,   ""},
    {"Sustain",       0.000,   0.0,   1.0,   ""},
    {"Release",       0.441,   0.0,   1.0,   ""},
    {"Coarse",        0.842,   0.0,   1.0,   ""},
    {"Fine",          0.329,   0.0,   1.0,   ""},
    {"Mod Init",      0.230,   0.0,   1.0,   ""},
    {"Mod Decay",     0.800,   0.0,   1.0,   ""},
    {"Mod Sustain",   0.050,   0.0,   1.0,   ""},
    {"Mod Release",   0.800,   0.0,   1.0,   ""},
    {"Mod Velocity",  0.900,   0.0,   1.0,   ""},
    {"Vibrato",       0.000,   0.0,   1.0,   ""},
    {"LFO Rate",      0.414,   0.0,   1.0,   ""},
    {"Octave",        0.500,   0.0,   1.0,   ""},
    {"Fine Tune",     0.500,   0.0,   1.0,   ""},
    {"Waveform",      0.447,   0.0,   1.0,   ""},
    {"Mod Thru",      0.000,   0.0,   1.0,   ""},
    {"Volume",        0.000,  -70.0,  12.0,  "dB"}, // master, linear dB (default 0 dB)
}};

const ParamSpec& spec(int paramIdx)
{
  return kSpecs[static_cast<size_t>(paramIdx)];
}

} // namespace params
} // namespace dx10

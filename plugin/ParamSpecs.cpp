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

    // --- Effect chain: 5 slots × {Type (enum 0..11), P0..P7 (0..1), Bypass}.
    // Type/Bypass are InitEnum/InitBool in DX10R.cpp (these min/max only bound
    // the enum index / bool). Generic params default to a neutral 0.5; the WebUI
    // applies per-effect defaults on type-switch (Phase 4b). Then chain lock.
    {"FX1 Type", 0, 0, 11, ""}, {"FX1 P1", 0.5, 0, 1, ""}, {"FX1 P2", 0.5, 0, 1, ""}, {"FX1 P3", 0.5, 0, 1, ""}, {"FX1 P4", 0.5, 0, 1, ""}, {"FX1 P5", 0.5, 0, 1, ""}, {"FX1 P6", 0.5, 0, 1, ""}, {"FX1 P7", 0.5, 0, 1, ""}, {"FX1 P8", 0.5, 0, 1, ""}, {"FX1 Bypass", 0, 0, 1, ""},
    {"FX2 Type", 0, 0, 11, ""}, {"FX2 P1", 0.5, 0, 1, ""}, {"FX2 P2", 0.5, 0, 1, ""}, {"FX2 P3", 0.5, 0, 1, ""}, {"FX2 P4", 0.5, 0, 1, ""}, {"FX2 P5", 0.5, 0, 1, ""}, {"FX2 P6", 0.5, 0, 1, ""}, {"FX2 P7", 0.5, 0, 1, ""}, {"FX2 P8", 0.5, 0, 1, ""}, {"FX2 Bypass", 0, 0, 1, ""},
    {"FX3 Type", 0, 0, 11, ""}, {"FX3 P1", 0.5, 0, 1, ""}, {"FX3 P2", 0.5, 0, 1, ""}, {"FX3 P3", 0.5, 0, 1, ""}, {"FX3 P4", 0.5, 0, 1, ""}, {"FX3 P5", 0.5, 0, 1, ""}, {"FX3 P6", 0.5, 0, 1, ""}, {"FX3 P7", 0.5, 0, 1, ""}, {"FX3 P8", 0.5, 0, 1, ""}, {"FX3 Bypass", 0, 0, 1, ""},
    {"FX4 Type", 0, 0, 11, ""}, {"FX4 P1", 0.5, 0, 1, ""}, {"FX4 P2", 0.5, 0, 1, ""}, {"FX4 P3", 0.5, 0, 1, ""}, {"FX4 P4", 0.5, 0, 1, ""}, {"FX4 P5", 0.5, 0, 1, ""}, {"FX4 P6", 0.5, 0, 1, ""}, {"FX4 P7", 0.5, 0, 1, ""}, {"FX4 P8", 0.5, 0, 1, ""}, {"FX4 Bypass", 0, 0, 1, ""},
    {"FX5 Type", 0, 0, 11, ""}, {"FX5 P1", 0.5, 0, 1, ""}, {"FX5 P2", 0.5, 0, 1, ""}, {"FX5 P3", 0.5, 0, 1, ""}, {"FX5 P4", 0.5, 0, 1, ""}, {"FX5 P5", 0.5, 0, 1, ""}, {"FX5 P6", 0.5, 0, 1, ""}, {"FX5 P7", 0.5, 0, 1, ""}, {"FX5 P8", 0.5, 0, 1, ""}, {"FX5 Bypass", 0, 0, 1, ""},
    {"FX Chain Lock", 0, 0, 1, ""},
}};

const ParamSpec& spec(int paramIdx)
{
  return kSpecs[static_cast<size_t>(paramIdx)];
}

} // namespace params
} // namespace dx10

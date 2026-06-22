#pragma once

// DX10R parameter IDs — the single enumeration of plugin parameters.
// Faithful to mda DX10's 16 parameters, plus two DX10R additions:
//   * kParamSustain  — modern A/D/S/R extension (Sustain=0 reproduces the
//                      original DX10 decay-to-silence behaviour).
//   * kParamVolume   — master output level.
// Ranges / defaults / curves live in ParamSpecs.cpp (single source of truth).
//
// Pure header (no iPlug2 dependency): both the plugin and the DSP engine
// include it, so the engine can index live values by the same enum.

enum EParams
{
  // Carrier amplitude envelope (A/D/S/R).
  kParamAttack = 0,
  kParamDecay,
  kParamSustain,
  kParamRelease,

  // FM modulator:carrier frequency ratio (coarse integer + fine fraction).
  kParamCoarse,
  kParamFine,

  // Modulator envelope (modulation index over time) + velocity sensitivity.
  kParamModInit,
  kParamModDecay,
  kParamModSustain,
  kParamModRelease,
  kParamModVelocity,

  // Vibrato depth + LFO rate.
  kParamVibrato,
  kParamLfoRate,

  // Tone / pitch.
  kParamOctave,
  kParamFineTune,
  kParamWaveform,
  kParamModThru,

  // Master output.
  kParamVolume,

  kNumParams
};

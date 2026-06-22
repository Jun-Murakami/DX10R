#pragma once

namespace dx10 {
namespace dsp {

// Shared vibrato LFO — a recursive sine oscillator updated at control rate
// (every 100 samples, like mda DX10) to keep CPU low and the vibrato speed
// independent of the host block size. Produces `mw`, the modulation value added
// to every voice's carrier phase.
struct Lfo
{
  float lfo0 = 0.0f;
  float lfo1 = 1.0f; // phase seed (mda resets lfo1 = 1 on resume)
  float mw = 0.0f;   // current modulation output
  int counter = 0;

  void reset()
  {
    lfo0 = 0.0f;
    lfo1 = 1.0f;
    mw = 0.0f;
    counter = 0;
  }

  // Advance one sample.
  //   dlfo  : rate coefficient (mda dlfo)
  //   depth : modulation depth (mod wheel + vibrato param)
  inline void tick(float dlfo, float depth)
  {
    if (--counter < 0)
    {
      lfo0 += dlfo * lfo1;
      lfo1 -= dlfo * lfo0;
      mw = lfo1 * depth;
      counter = 100;
    }
  }
};

} // namespace dsp
} // namespace dx10

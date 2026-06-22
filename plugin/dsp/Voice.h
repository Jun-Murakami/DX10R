#pragma once

namespace dx10 {
namespace dsp {

// A single 2-operator FM voice, faithfully ported from mda DX10 (mdaDX10.cpp,
// struct VOICE + the inner process loop), with one DX10R modernisation: the
// carrier amplitude envelope decays toward a *sustain target* instead of always
// toward zero. With ctarget == 0 the behaviour is bit-for-bit the original DX10
// (decay-to-silence), so factory presets that set Sustain=0 are unchanged.
struct Voice
{
  static constexpr float kSilence = 0.0003f; // mda SILENCE (voice choking)
  static constexpr int kNoteNone = -1;
  static constexpr int kNoteSustained = 128; // mda SUSTAIN (held by pedal)

  // Carrier amplitude envelope.
  float env = 0.0f;     // raw envelope (peak at note-on, then decays)
  float cenv = 0.0f;    // attack-smoothed output level (the audible amp)
  float catt = 0.0f;    // attack one-pole coefficient
  float cdec = 0.99f;   // decay/release one-pole coefficient
  float ctarget = 0.0f; // decay target: sustain level while held, 0 on release

  // Carrier oscillator (phase accumulator in [-1, 1]).
  float car = 0.0f;
  float dcar = 0.0f;

  // Modulator oscillator (recursive sine) + modulation-index envelope.
  float dmod = 0.0f;
  float mod0 = 0.0f;
  float mod1 = 0.0f;
  float menv = 0.0f; // current modulation index
  float mlev = 0.0f; // modulation-index target
  float mdec = 0.0f; // modulation-index one-pole coefficient

  int note = kNoteNone; // MIDI note that triggered this voice

  bool active() const { return env > kSilence; }

  // Render one sample.
  //   mw : shared vibrato modulation added to the carrier phase
  //   w  : "rich" waveshaper coefficient (mda rich)
  //   m  : modulator thru-mix amount (mda modmix)
  inline float render(float mw, float w, float m)
  {
    const float e = env;
    if (e <= kSilence)
    {
      env = cenv = 0.0f; // flush (also avoids denormals)
      return 0.0f;
    }

    env = ctarget + (e - ctarget) * cdec; // decay/release toward target
    cenv += catt * (e - cenv);            // attack smoothing

    float x = dmod * mod0 - mod1;         // modulator recursive sine
    mod1 = mod0;
    mod0 = x;
    menv += mdec * (mlev - menv);         // modulator index envelope

    x = car + dcar + x * menv + mw;       // carrier phase + FM + vibrato
    while (x > 1.0f) x -= 2.0f;           // wrap phase to [-1, 1]
    while (x < -1.0f) x += 2.0f;
    car = x;

    // mda 5th-order sine approximation with harmonic richness, plus the
    // modulator thru-mix.
    return cenv * (m * mod1 + (x + x * x * x * (w * x * x - 1.0f - w)));
  }
};

} // namespace dsp
} // namespace dx10

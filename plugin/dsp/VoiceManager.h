#pragma once

#include "../ParameterIDs.h"
#include "Lfo.h"
#include "Voice.h"

namespace dx10 {
namespace dsp {

// VoiceManager — 16-voice polyphonic FM engine. Owns the voices, the shared
// vibrato LFO and the global parameter-derived coefficients (mda DX10's
// update()). All methods are real-time safe (no allocation / locks / I/O).
class VoiceManager
{
public:
  static constexpr int kNumVoices = 16; // DX10R: original mda DX10 was 8

  void setSampleRate(double sampleRate);
  void reset();

  // Live parameter input.
  //   setParam      — FM parameters, normalized [0,1] (indexed by EParams)
  //   setMasterGain — master output, linear amplitude (from kParamVolume dB)
  void setParam(int paramIdx, double normValue);
  void setMasterGain(double linearGain);

  // MIDI.
  void noteOn(int note, int velocity);
  void noteOff(int note);
  void setSustainPedal(bool on);
  void setModWheel(int value0to127);
  void setVolumeCC(int value0to127);
  void setPitchBend(double norm); // -1..1
  void panic();                   // all sound off

  // Render n samples (replacing) into outL/outR starting at sample index start.
  void render(double* outL, double* outR, int start, int n);

  bool anyActive() const;

private:
  void update(); // recompute coefficients from mParams (mda update())

  double mFs = 44100.0;
  double mParams[kNumParams] = {}; // normalized [0,1]; kParamVolume slot unused

  // Derived coefficients (mda DX10).
  float mTune = 0.0f;
  float mRatio = 0.0f;
  float mDepth = 0.0f;
  float mDept2 = 0.0f;
  float mVelsens = 0.0f;
  float mVibrato = 0.0f;
  float mCatt = 0.0f;
  float mCdec = 0.99f;
  float mCrel = 0.0f;
  float mMdec = 0.0f;
  float mMrel = 0.0f;
  float mRich = 0.0f;
  float mModmix = 0.0f;
  float mDlfo = 0.0f;
  float mSustainFrac = 0.0f; // DX10R: carrier sustain level fraction

  // Runtime state.
  float mPbend = 1.0f;
  float mModwhl = 0.0f;
  float mVolume = 0.0035f; // mda base volume (CC7 scales this)
  bool mSustainPedal = false;
  double mMasterGain = 1.0;
  double mMasterGainSmoothed = 1.0;

  Lfo mLfo;
  Voice mVoices[kNumVoices];
};

} // namespace dsp
} // namespace dx10

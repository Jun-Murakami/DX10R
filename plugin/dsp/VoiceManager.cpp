#include "VoiceManager.h"

#include <cmath>

namespace dx10 {
namespace dsp {

void VoiceManager::setSampleRate(double sampleRate)
{
  const double base = sampleRate > 0.0 ? sampleRate : 44100.0;
  // The FM engine renders oversampled; ALL mda coefficients derive from
  // ifs = 1/mFs, so setting mFs to the oversampled rate keeps pitch, envelope
  // times and LFO speed correct while the per-sample waveshaper/FM run 2x.
  mFs = base * kOversample;
  // ~20 ms master-gain smoothing time constant (rate-correct at mFs).
  mGainSmoothCoef = 1.0 - std::exp(-1.0 / (0.02 * mFs));
  mDecimator.prepare();
  update();
}

void VoiceManager::reset()
{
  for (Voice& v : mVoices)
    v = Voice {};
  mLfo.reset();
  mPbend = 1.0f;
  mSustainPedal = false;
  mMasterGainSmoothed = mMasterGain;
  mDecimator.reset();
}

void VoiceManager::setParam(int paramIdx, double normValue)
{
  if (paramIdx < 0 || paramIdx >= kNumParams)
    return;
  mParams[paramIdx] = normValue;
  update(); // mda recomputes all coefficients on any parameter change (cheap)
}

void VoiceManager::setMasterGain(double linearGain)
{
  mMasterGain = linearGain;
}

// Recompute the parameter-derived coefficients. Faithful translation of
// mda DX10's mdaDX10::update(), plus the carrier sustain fraction.
void VoiceManager::update()
{
  const double ifs = 1.0 / mFs;

  mTune = static_cast<float>(8.175798915644 * ifs *
                             std::pow(2.0, std::floor(mParams[kParamOctave] * 6.9) - 2.0));

  const double rati = std::floor(40.1 * mParams[kParamCoarse] * mParams[kParamCoarse]);
  const double pf = mParams[kParamFine];
  double ratf;
  if (pf < 0.5)
  {
    ratf = 0.2 * pf * pf;
  }
  else
  {
    switch (static_cast<int>(8.9 * pf))
    {
      case 4: ratf = 0.25; break;
      case 5: ratf = 0.33333333; break;
      case 6: ratf = 0.50; break;
      case 7: ratf = 0.66666667; break;
      default: ratf = 0.75; break;
    }
  }
  mRatio = static_cast<float>(1.570796326795 * (rati + ratf));

  mDepth = static_cast<float>(0.0002 * mParams[kParamModInit] * mParams[kParamModInit]);
  mDept2 = static_cast<float>(0.0002 * mParams[kParamModSustain] * mParams[kParamModSustain]);
  mVelsens = static_cast<float>(mParams[kParamModVelocity]);
  mVibrato = static_cast<float>(0.001 * mParams[kParamVibrato] * mParams[kParamVibrato]);

  mCatt = static_cast<float>(1.0 - std::exp(-ifs * std::exp(8.0 - 8.0 * mParams[kParamAttack])));
  mCdec = (mParams[kParamDecay] > 0.98)
              ? 1.0f
              : static_cast<float>(std::exp(-ifs * std::exp(5.0 - 8.0 * mParams[kParamDecay])));
  mCrel = static_cast<float>(std::exp(-ifs * std::exp(5.0 - 5.0 * mParams[kParamRelease])));
  mMdec = static_cast<float>(1.0 - std::exp(-ifs * std::exp(6.0 - 7.0 * mParams[kParamModDecay])));
  mMrel = static_cast<float>(1.0 - std::exp(-ifs * std::exp(5.0 - 8.0 * mParams[kParamModRelease])));

  mRich = static_cast<float>(0.5 - 3.0 * mParams[kParamWaveform] * mParams[kParamWaveform]);
  mModmix = static_cast<float>(0.25 * mParams[kParamModThru] * mParams[kParamModThru]);
  mDlfo = static_cast<float>(628.3 * ifs * 25.0 * mParams[kParamLfoRate] * mParams[kParamLfoRate]);

  mSustainFrac = static_cast<float>(mParams[kParamSustain]); // DX10R extension
}

void VoiceManager::noteOn(int note, int velocity)
{
  if (velocity <= 0)
  {
    noteOff(note);
    return;
  }

  // Steal the quietest voice (lowest envelope), like mda DX10.
  float lowest = 1.0e30f;
  int vl = 0;
  for (int v = 0; v < kNumVoices; ++v)
  {
    if (mVoices[v].env < lowest)
    {
      lowest = mVoices[v].env;
      vl = v;
    }
  }

  Voice& V = mVoices[vl];

  const double ft = mParams[kParamFineTune];
  double l = std::exp(0.05776226505 * (static_cast<double>(note) + ft + ft - 1.0));

  V.note = note;
  V.car = 0.0f;
  V.dcar = static_cast<float>(mTune * mPbend * l);

  if (l > 50.0) l = 50.0;                          // key-tracking clamp
  l *= (64.0 + mVelsens * (velocity - 64));        // velocity sensitivity

  V.menv = static_cast<float>(mDepth * l);
  V.mlev = static_cast<float>(mDept2 * l);
  V.mdec = mMdec;

  const double dmod = mRatio * V.dcar;
  V.mod0 = 0.0f;
  V.mod1 = static_cast<float>(std::sin(dmod));
  V.dmod = static_cast<float>(2.0 * std::cos(dmod));

  const float peak = static_cast<float>((1.5 - mParams[kParamWaveform]) * mVolume * (velocity + 10));
  V.env = peak;
  V.catt = mCatt;
  V.cenv = 0.0f;
  V.cdec = mCdec;
  V.ctarget = peak * mSustainFrac; // DX10R: decay toward sustain level
}

void VoiceManager::noteOff(int note)
{
  for (Voice& V : mVoices)
  {
    if (V.note != note)
      continue;

    if (mSustainPedal)
    {
      V.note = Voice::kNoteSustained; // hold until pedal release
    }
    else
    {
      V.cdec = mCrel;
      V.env = V.cenv;     // release from the current audible level
      V.catt = 1.0f;      // make the smoother follow instantly
      V.ctarget = 0.0f;   // release decays to silence
      V.mlev = 0.0f;
      V.mdec = mMrel;
    }
  }
}

void VoiceManager::setSustainPedal(bool on)
{
  mSustainPedal = on;
  if (on)
    return;

  // Pedal released: release every voice that was held by the pedal.
  for (Voice& V : mVoices)
  {
    if (V.note == Voice::kNoteSustained)
    {
      V.cdec = mCrel;
      V.env = V.cenv;
      V.catt = 1.0f;
      V.ctarget = 0.0f;
      V.mlev = 0.0f;
      V.mdec = mMrel;
      V.note = Voice::kNoteNone;
    }
  }
}

void VoiceManager::setModWheel(int value0to127)
{
  mModwhl = static_cast<float>(0.00000005 * static_cast<double>(value0to127) * value0to127);
}

void VoiceManager::setVolumeCC(int value0to127)
{
  mVolume = static_cast<float>(0.00000035 * static_cast<double>(value0to127) * value0to127);
}

void VoiceManager::setPitchBend(double norm)
{
  const double raw = norm * 8192.0; // -8192..+8192
  mPbend = static_cast<float>(raw > 0.0 ? 1.0 + 0.000014951 * raw : 1.0 + 0.000013318 * raw);
}

void VoiceManager::panic()
{
  for (Voice& V : mVoices)
  {
    V.cdec = 0.99f;
    V.ctarget = 0.0f;
    V.catt = 1.0f;
  }
  mSustainPedal = false;
}

void VoiceManager::render(double* outL, double* outR, int start, int n)
{
  const float w = mRich;
  const float m = mModmix;
  const int end = start + n;

  for (int i = start; i < end; ++i)
  {
    // Render kOversample (=2) mono sub-samples, then decimate 2:1 to band-limit
    // the FM/waveshaper partials before they fold back as aliasing.
    double sub0 = 0.0;
    double sub1 = 0.0;
    for (int os = 0; os < kOversample; ++os)
    {
      mLfo.tick(mDlfo, mModwhl + mVibrato);

      float o = 0.0f;
      for (int v = 0; v < kNumVoices; ++v)
        o += mVoices[v].render(mLfo.mw, w, m);

      // Smooth the master gain to avoid zipper noise (de-zip).
      mMasterGainSmoothed += mGainSmoothCoef * (mMasterGain - mMasterGainSmoothed);
      const double sub = static_cast<double>(o) * mMasterGainSmoothed;
      (os == 0 ? sub0 : sub1) = sub;
    }

    const double s = mDecimator.process(sub0, sub1);
    outL[i] = s;
    outR[i] = s;
  }
}

bool VoiceManager::anyActive() const
{
  for (const Voice& V : mVoices)
  {
    if (V.active())
      return true;
  }
  return false;
}

} // namespace dsp
} // namespace dx10

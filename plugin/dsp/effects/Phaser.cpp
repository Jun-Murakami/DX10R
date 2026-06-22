#include "Phaser.h"

#include "../FastMath.h"

#include <algorithm>
#include <cmath>

namespace dx10::dsp {

namespace {
constexpr double kPi = 3.141592653589793;
constexpr double kTwoPi = 2.0 * kPi;

constexpr double kRateMinHz = 0.05;
constexpr double kRateMaxHz = 10.0;

// Center 周波数のレンジ (LFO=0 の時の break freq)。
constexpr double kCenterMinHz = 200.0;
constexpr double kCenterMaxHz = 4000.0;

// LFO で sweep する周波数比 (Center × 2^±depth_octaves)。
// 100% 時に ±3 octave 振れて Phase 90 系の派手さが出る。
constexpr double kMaxDepthOctaves = 3.0;

// feedback 経路に softclip + 帯域整形が入ったので 0.97 まで安全に上げられる。
constexpr double kMaxFeedback = 0.97;

// feedback 経路の帯域整形。HPF で低域ブーム (DC ループゲイン 1/(1-fb)) を切り、
// LPF でナイキスト付近の帰還ピークを落として resonance を中域に集中させる。
constexpr double kFbHpfHz = 140.0;
constexpr double kFbLpfHz = 9000.0;

// Sharp の Q レンジ。Q=0.5 で 1 次 AP × 2 相当の classic な緩さ、上げるほど
// notch が狭く深くなる。
constexpr double kQMin = 0.5;
constexpr double kQMax = 4.0;

// Stages 選択肢 (idx 0..3)。UI は 1 次 AP 換算の 4/6/8/12 表示、内部は biquad
// セクション数 (= 半分)。notch 数 = セクション数。
constexpr int kSectionOptions[4] = {2, 3, 4, 6};

double logMap(double v01, double min, double max) {
  v01 = std::clamp(v01, 0.0, 1.0);
  return min * std::pow(max / min, v01);
}

// break freq を audio safe な範囲にクランプ (DC とナイキスト近傍は AP 不安定)。
double clampApFreq(double fc, double sr) {
  return std::clamp(fc, 20.0, sr * 0.45);
}
}  // namespace

void Phaser::prepare(double sampleRate) {
  mSampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
  if (mLfoInc == 0.0) {
    mLfoInc = kTwoPi * 0.4 / mSampleRate;  // 0.4 Hz default
  }
  mFbHpfR = std::exp(-kTwoPi * kFbHpfHz / mSampleRate);
  mFbLpfB = 1.0 - std::exp(-kTwoPi * kFbLpfHz / mSampleRate);
  reset();
}

void Phaser::reset() {
  for (auto& s : mStagesL) { s = AllPass2{}; }
  for (auto& s : mStagesR) { s = AllPass2{}; }
  mLfoPhaseL = 0.0;
  mLfoPhaseR = (mStereoMode == 1) ? kPi : 0.0;
  mFbStateL = 0.0;
  mFbStateR = 0.0;
  mFbHpfX1L = 0.0;
  mFbHpfY1L = 0.0;
  mFbHpfX1R = 0.0;
  mFbHpfY1R = 0.0;
  mFbLpfYL = 0.0;
  mFbLpfYR = 0.0;
}

void Phaser::computeApCoeffs(double fc, double& c, double& e) const noexcept {
  // RBJ allpass biquad: H(z) = (c + e·z⁻¹ + z⁻²) / (1 + e·z⁻¹ + c·z⁻²)。
  // 位相は DC で 0、fc で -π、ナイキストで -2π (= notch を fc 通りに配置)。
  fc = clampApFreq(fc, mSampleRate);
  const double w0 = kTwoPi * fc / mSampleRate;
  const double alpha = std::sin(w0) / (2.0 * mQ);
  c = (1.0 - alpha) / (1.0 + alpha);
  e = -std::cos(w0) * (1.0 + c);
}

void Phaser::setParam(int paramIdx, double value01) {
  value01 = std::clamp(value01, 0.0, 1.0);
  switch (paramIdx) {
    case 0: {  // Rate (0.05..10 Hz, log)
      const double rateHz = logMap(value01, kRateMinHz, kRateMaxHz);
      mLfoInc = kTwoPi * rateHz / mSampleRate;
      break;
    }
    case 1:  // Depth (0..1)
      mDepthRatio = value01;
      break;
    case 2:  // Feedback (0..0.95)
      mFeedback = value01 * kMaxFeedback;
      break;
    case 3: {  // Stages (4/6/8/12 段相当 = 2/3/4/6 セクション)
      const int n = 4;
      const int idx = std::min(n - 1, std::max(0, static_cast<int>(std::round(value01 * (n - 1)))));
      mNumSections = kSectionOptions[idx];
      break;
    }
    case 4:  // Center (200..4000 Hz, log)
      mCenterFreqHz = logMap(value01, kCenterMinHz, kCenterMaxHz);
      break;
    case 5: {  // Stereo (0=Mono, 1=Stereo)
      const int n = 2;
      const int prev = mStereoMode;
      mStereoMode = std::min(n - 1, std::max(0, static_cast<int>(std::round(value01 * (n - 1)))));
      if (prev != mStereoMode) {
        // L/R LFO の位相関係を合わせ直す (Stereo 時は π offset)。
        mLfoPhaseR = mLfoPhaseL + ((mStereoMode == 1) ? kPi : 0.0);
        if (mLfoPhaseR >= kTwoPi) mLfoPhaseR -= kTwoPi;
      }
      break;
    }
    case 6:  // Mix (dry/wet)
      mMix = value01;
      break;
    case 7:  // Sharp (Q 0.5..4.0, log)
      mQ = logMap(value01, kQMin, kQMax);
      break;
    default:
      break;
  }
}

double Phaser::processCascade(std::array<AllPass2, kMaxSections>& sections,
                              double c, double e, int n, double in) noexcept {
  double x = in;
  for (int i = 0; i < n; ++i) {
    auto& s = sections[i];
    const double y = c * x + e * s.x1 + s.x2 - e * s.y1 - c * s.y2;
    s.x2 = s.x1;
    s.x1 = x;
    s.y2 = s.y1;
    s.y1 = y;
    x = y;
  }
  return x;
}

void Phaser::process(double& l, double& r) noexcept {
  const double dryL = l;
  const double dryR = r;

  // 1. LFO (三角波) を進める。octave 領域で線形に動き、sin のような両端滞留がない。
  const double lfoL = 4.0 * std::abs(mLfoPhaseL * (1.0 / kTwoPi) - 0.5) - 1.0;
  const double lfoR = 4.0 * std::abs(mLfoPhaseR * (1.0 / kTwoPi) - 0.5) - 1.0;
  mLfoPhaseL += mLfoInc;
  mLfoPhaseR += mLfoInc;
  if (mLfoPhaseL >= kTwoPi) mLfoPhaseL -= kTwoPi;
  if (mLfoPhaseR >= kTwoPi) mLfoPhaseR -= kTwoPi;

  // 2. break freq = Center × 2^(lfo * depth * maxOctaves)。LFO ±1 を ±maxOct に。
  const double octL = lfoL * mDepthRatio * kMaxDepthOctaves;
  const double octR = lfoR * mDepthRatio * kMaxDepthOctaves;
  const double fcL = mCenterFreqHz * std::exp2(octL);
  const double fcR = mCenterFreqHz * std::exp2(octR);
  double cL = 0.0, eL = 0.0, cR = 0.0, eR = 0.0;
  computeApCoeffs(fcL, cL, eL);
  computeApCoeffs(fcR, cR, eR);

  // 3. feedback を整形 (HPF → LPF → softclip) して加算 → AP cascade を通す。
  const double hpfL = mFbStateL - mFbHpfX1L + mFbHpfR * mFbHpfY1L;
  mFbHpfX1L = mFbStateL;
  mFbHpfY1L = hpfL;
  const double hpfR = mFbStateR - mFbHpfX1R + mFbHpfR * mFbHpfY1R;
  mFbHpfX1R = mFbStateR;
  mFbHpfY1R = hpfR;
  mFbLpfYL += mFbLpfB * (hpfL - mFbLpfYL);
  mFbLpfYR += mFbLpfB * (hpfR - mFbLpfYR);

  const double inL = dryL + fastTanh(mFbLpfYL * mFeedback);
  const double inR = dryR + fastTanh(mFbLpfYR * mFeedback);
  const double phasedL = processCascade(mStagesL, cL, eL, mNumSections, inL);
  const double phasedR = processCascade(mStagesR, cR, eR, mNumSections, inR);
  mFbStateL = phasedL;
  mFbStateR = phasedR;

  // 4. wet = (dry + phased) / 2。phaser の典型的な notch 干渉を作る。
  const double wetL = 0.5 * (dryL + phasedL);
  const double wetR = 0.5 * (dryR + phasedR);

  // 5. dry/wet ミックス。
  l = dryL * (1.0 - mMix) + wetL * mMix;
  r = dryR * (1.0 - mMix) + wetR * mMix;
}

}  // namespace dx10::dsp

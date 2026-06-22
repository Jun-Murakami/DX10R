#include "EQ3Band.h"

#include <algorithm>
#include <cmath>

namespace dx10::dsp {

namespace {
constexpr double kPi = 3.141592653589793;
constexpr double kTwoPi = 2.0 * kPi;

// Shelf 用 Q = 1/√2 (Butterworth、resonant peak なし)。
constexpr double kShelfQ = 0.7071067811865476;
// Mid peak の Q。1.0 で musical な width。値を上げると鋭く、下げると広く。
constexpr double kMidQ = 1.0;

// Frequency レンジ (band ごと)。
constexpr double kLowFreqMinHz = 50.0;
constexpr double kLowFreqMaxHz = 500.0;
constexpr double kMidFreqMinHz = 200.0;
constexpr double kMidFreqMaxHz = 5000.0;
constexpr double kHighFreqMinHz = 2000.0;
constexpr double kHighFreqMaxHz = 12000.0;

// Gain レンジ (asymmetric)。cut 側は kill 寄り、boost は控えめ。
constexpr double kCutMaxDb = -36.0;   // v01=0
constexpr double kBoostMaxDb = 12.0;  // v01=1
// v01=0.5 で 0 dB (= no effect、center detent 位置)。

double logMap(double v01, double min, double max) {
  v01 = std::clamp(v01, 0.0, 1.0);
  return min * std::pow(max / min, v01);
}
}  // namespace

double EQ3Band::gainDbFromValue01(double v01) noexcept {
  v01 = std::clamp(v01, 0.0, 1.0);
  // Asymmetric piecewise linear in dB:
  //   v01=0   → kCutMaxDb (-36 dB, kill 寄り)
  //   v01=0.5 → 0 dB (no effect、center detent)
  //   v01=1   → kBoostMaxDb (+12 dB)
  if (v01 < 0.5) return kCutMaxDb * (1.0 - v01 * 2.0);
  return kBoostMaxDb * ((v01 - 0.5) * 2.0);
}

void EQ3Band::computeShelfCoeffs(Biquad& bq, ShelfKind kind, double fc,
                                 double gainDb) noexcept {
  // 安全クランプ: DC 近傍 / Nyquist 近傍は biquad が不安定。
  fc = std::clamp(fc, 10.0, mSampleRate * 0.45);
  const double A = std::pow(10.0, gainDb / 40.0);  // shelf 用 A = √(linear gain)
  const double w = kTwoPi * fc / mSampleRate;
  const double cosw = std::cos(w);
  const double sinw = std::sin(w);
  const double alpha = sinw / (2.0 * kShelfQ);
  const double sqrtA2alpha = 2.0 * std::sqrt(A) * alpha;

  double b0, b1, b2, a0, a1, a2;
  if (kind == ShelfKind::Low) {
    b0 = A * ((A + 1.0) - (A - 1.0) * cosw + sqrtA2alpha);
    b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosw);
    b2 = A * ((A + 1.0) - (A - 1.0) * cosw - sqrtA2alpha);
    a0 = (A + 1.0) + (A - 1.0) * cosw + sqrtA2alpha;
    a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosw);
    a2 = (A + 1.0) + (A - 1.0) * cosw - sqrtA2alpha;
  } else {  // High shelf
    b0 = A * ((A + 1.0) + (A - 1.0) * cosw + sqrtA2alpha);
    b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw);
    b2 = A * ((A + 1.0) + (A - 1.0) * cosw - sqrtA2alpha);
    a0 = (A + 1.0) - (A - 1.0) * cosw + sqrtA2alpha;
    a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosw);
    a2 = (A + 1.0) - (A - 1.0) * cosw - sqrtA2alpha;
  }
  // Normalize by a0 (a0 division を毎サンプル避けるため事前に割っておく)。
  const double inv = 1.0 / a0;
  bq.b0 = b0 * inv;
  bq.b1 = b1 * inv;
  bq.b2 = b2 * inv;
  bq.a1 = a1 * inv;
  bq.a2 = a2 * inv;
}

void EQ3Band::computePeakCoeffs(Biquad& bq, double fc, double gainDb,
                                double q) noexcept {
  fc = std::clamp(fc, 10.0, mSampleRate * 0.45);
  const double A = std::pow(10.0, gainDb / 40.0);
  const double w = kTwoPi * fc / mSampleRate;
  const double cosw = std::cos(w);
  const double sinw = std::sin(w);
  const double alpha = sinw / (2.0 * q);

  const double b0 = 1.0 + alpha * A;
  const double b1 = -2.0 * cosw;
  const double b2 = 1.0 - alpha * A;
  const double a0 = 1.0 + alpha / A;
  const double a1 = -2.0 * cosw;
  const double a2 = 1.0 - alpha / A;

  const double inv = 1.0 / a0;
  bq.b0 = b0 * inv;
  bq.b1 = b1 * inv;
  bq.b2 = b2 * inv;
  bq.a1 = a1 * inv;
  bq.a2 = a2 * inv;
}

void EQ3Band::prepare(double sampleRate) {
  mSampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
  // 全 band の係数を現在値で再計算 (3 段 cascaded shelf は dB の 1/3 ずつ)。
  const double thirdLowDb = mLowGainDb / 3.0;
  computeShelfCoeffs(mLow1, ShelfKind::Low, mLowFreqHz, thirdLowDb);
  computeShelfCoeffs(mLow2, ShelfKind::Low, mLowFreqHz, thirdLowDb);
  computeShelfCoeffs(mLow3, ShelfKind::Low, mLowFreqHz, thirdLowDb);
  computePeakCoeffs(mMid, mMidFreqHz, mMidGainDb, kMidQ);
  const double thirdHighDb = mHighGainDb / 3.0;
  computeShelfCoeffs(mHigh1, ShelfKind::High, mHighFreqHz, thirdHighDb);
  computeShelfCoeffs(mHigh2, ShelfKind::High, mHighFreqHz, thirdHighDb);
  computeShelfCoeffs(mHigh3, ShelfKind::High, mHighFreqHz, thirdHighDb);
  reset();
}

void EQ3Band::reset() {
  mLow1.clearState();
  mLow2.clearState();
  mLow3.clearState();
  mMid.clearState();
  mHigh1.clearState();
  mHigh2.clearState();
  mHigh3.clearState();
}

void EQ3Band::setParam(int paramIdx, double value01) {
  value01 = std::clamp(value01, 0.0, 1.0);
  switch (paramIdx) {
    case 0: {  // Low Gain
      mLowGainDb = gainDbFromValue01(value01);
      const double third = mLowGainDb / 3.0;
      computeShelfCoeffs(mLow1, ShelfKind::Low, mLowFreqHz, third);
      computeShelfCoeffs(mLow2, ShelfKind::Low, mLowFreqHz, third);
      computeShelfCoeffs(mLow3, ShelfKind::Low, mLowFreqHz, third);
      break;
    }
    case 1: {  // Low Freq
      mLowFreqHz = logMap(value01, kLowFreqMinHz, kLowFreqMaxHz);
      const double third = mLowGainDb / 3.0;
      computeShelfCoeffs(mLow1, ShelfKind::Low, mLowFreqHz, third);
      computeShelfCoeffs(mLow2, ShelfKind::Low, mLowFreqHz, third);
      computeShelfCoeffs(mLow3, ShelfKind::Low, mLowFreqHz, third);
      break;
    }
    case 2:  // Mid Gain
      mMidGainDb = gainDbFromValue01(value01);
      computePeakCoeffs(mMid, mMidFreqHz, mMidGainDb, kMidQ);
      break;
    case 3:  // Mid Freq
      mMidFreqHz = logMap(value01, kMidFreqMinHz, kMidFreqMaxHz);
      computePeakCoeffs(mMid, mMidFreqHz, mMidGainDb, kMidQ);
      break;
    case 4: {  // High Gain
      mHighGainDb = gainDbFromValue01(value01);
      const double third = mHighGainDb / 3.0;
      computeShelfCoeffs(mHigh1, ShelfKind::High, mHighFreqHz, third);
      computeShelfCoeffs(mHigh2, ShelfKind::High, mHighFreqHz, third);
      computeShelfCoeffs(mHigh3, ShelfKind::High, mHighFreqHz, third);
      break;
    }
    case 5: {  // High Freq
      mHighFreqHz = logMap(value01, kHighFreqMinHz, kHighFreqMaxHz);
      const double third = mHighGainDb / 3.0;
      computeShelfCoeffs(mHigh1, ShelfKind::High, mHighFreqHz, third);
      computeShelfCoeffs(mHigh2, ShelfKind::High, mHighFreqHz, third);
      computeShelfCoeffs(mHigh3, ShelfKind::High, mHighFreqHz, third);
      break;
    }
    default:
      break;
  }
}

void EQ3Band::process(double& l, double& r) noexcept {
  // 直列処理: Low (× 3 cascaded) → Mid → High (× 3 cascaded)。
  // shelf 3 段で実質 ~18 dB/oct のスロープ (DJ EQ よりさらに音作り寄り)。
  mLow1.process(l, r);
  mLow2.process(l, r);
  mLow3.process(l, r);
  mMid.process(l, r);
  mHigh1.process(l, r);
  mHigh2.process(l, r);
  mHigh3.process(l, r);
}

}  // namespace dx10::dsp

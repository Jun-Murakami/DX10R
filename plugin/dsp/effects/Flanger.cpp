#include "Flanger.h"

#include "../FastMath.h"

#include <algorithm>
#include <cmath>

namespace dx10::dsp {

namespace {
constexpr double kPi = 3.141592653589793;
constexpr double kTwoPi = 2.0 * kPi;

constexpr double kRateMinHz = 0.05;
constexpr double kRateMaxHz = 8.0;

// Manual (center delay) のレンジ。0.5ms 〜 12ms で flanger / chorus 境界をカバー。
constexpr double kManualMinMs = 0.5;
constexpr double kManualMaxMs = 12.0;

// Depth 100% で Manual を幾何中心に ±2 oct (= 16:1) 掃引する。
constexpr double kMaxDepthOctaves = 2.0;

// feedback 経路に softclip + 帯域整形が入ったので 0.97 まで安全に上げられる。
constexpr double kMaxFeedback = 0.97;

// feedback 経路の帯域整形 (Phaser と同思想)。HPF で低域ブーム
// (DC ループゲイン 1/(1-fb)) を切り、LPF で帰還の超高域を落とす。
constexpr double kFbHpfHz = 100.0;
constexpr double kFbLpfHz = 9000.0;

// cubic Hermite の stencil (samplesAgo-2 .. +1) が書き込み済み履歴に収まる下限と、
// buffer 一周を超えない上限。
constexpr double kMinDelaySamples = 2.0;

double logMap(double v01, double min, double max) {
  v01 = std::clamp(v01, 0.0, 1.0);
  return min * std::pow(max / min, v01);
}
}  // namespace

void Flanger::prepare(double sampleRate) {
  mSampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
  if (mLfoInc == 0.0) {
    mLfoInc = kTwoPi * 0.3 / mSampleRate;  // 0.3 Hz default
  }
  mManualSamples = mManualMs * 0.001 * mSampleRate;
  mFbHpfR = std::exp(-kTwoPi * kFbHpfHz / mSampleRate);
  mFbLpfB = 1.0 - std::exp(-kTwoPi * kFbLpfHz / mSampleRate);
  reset();
}

void Flanger::reset() {
  std::fill(mBufL.begin(), mBufL.end(), 0.0);
  std::fill(mBufR.begin(), mBufR.end(), 0.0);
  mWritePos = 0;
  mFbStateL = 0.0;
  mFbStateR = 0.0;
  mFbHpfX1L = 0.0;
  mFbHpfY1L = 0.0;
  mFbHpfX1R = 0.0;
  mFbHpfY1R = 0.0;
  mFbLpfYL = 0.0;
  mFbLpfYR = 0.0;
  mLfoPhaseL = 0.0;
  mLfoPhaseR = (mStereoMode == 1) ? kPi : 0.0;
}

void Flanger::setParam(int paramIdx, double value01) {
  value01 = std::clamp(value01, 0.0, 1.0);
  switch (paramIdx) {
    case 0: {  // Rate (0.05..8 Hz, log)
      const double rateHz = logMap(value01, kRateMinHz, kRateMaxHz);
      mLfoInc = kTwoPi * rateHz / mSampleRate;
      break;
    }
    case 1:  // Depth (0..1 → 0..±2 oct)
      mDepthOct = value01 * kMaxDepthOctaves;
      break;
    case 2:  // Feedback (0..0.97)
      mFeedback = value01 * kMaxFeedback;
      break;
    case 3:  // Manual (0.5..12 ms, log)
      mManualMs = logMap(value01, kManualMinMs, kManualMaxMs);
      mManualSamples = mManualMs * 0.001 * mSampleRate;
      break;
    case 4: {  // Stereo (0=Mono, 1=Stereo)
      const int n = 2;
      const int prev = mStereoMode;
      mStereoMode = std::min(n - 1, std::max(0, static_cast<int>(std::round(value01 * (n - 1)))));
      if (prev != mStereoMode) {
        // Stereo 切替時に LFO 位相関係を更新 (Stereo 時 R = L + π)。
        mLfoPhaseR = mLfoPhaseL + ((mStereoMode == 1) ? kPi : 0.0);
        if (mLfoPhaseR >= kTwoPi) mLfoPhaseR -= kTwoPi;
      }
      break;
    }
    case 5:  // Mix (dry/wet)
      mMix = value01;
      break;
    case 6:  // Color (0=Normal, 1=Inv)
      mPolarity = (value01 >= 0.5) ? -1.0 : 1.0;
      break;
    case 7:  // TZF (0=Off, 1=On)
      mTzf = (value01 >= 0.5) ? 1 : 0;
      break;
    default:
      break;
  }
}

double Flanger::readDelay(const std::array<double, kBufferSize>& buf,
                          double samplesAgo) const noexcept {
  const double bufLen = static_cast<double>(kBufferSize);
  double idx = static_cast<double>(mWritePos) - samplesAgo;
  while (idx < 0.0) idx += bufLen;
  while (idx >= bufLen) idx -= bufLen;
  const int i1 = static_cast<int>(idx);
  const double frac = idx - static_cast<double>(i1);
  const int i0 = (i1 - 1 + kBufferSize) % kBufferSize;
  const int i2 = (i1 + 1) % kBufferSize;
  const int i3 = (i1 + 2) % kBufferSize;
  const double p0 = buf[i0];
  const double p1 = buf[i1];
  const double p2 = buf[i2];
  const double p3 = buf[i3];
  // 4 点 cubic Hermite (Catmull-Rom)。
  const double c1 = 0.5 * (p2 - p0);
  const double c2 = p0 - 2.5 * p1 + 2.0 * p2 - 0.5 * p3;
  const double c3 = 0.5 * (p3 - p0) + 1.5 * (p1 - p2);
  return ((c3 * frac + c2) * frac + c1) * frac + p1;
}

void Flanger::process(double& l, double& r) noexcept {
  const double dryL = l;
  const double dryR = r;

  // 1. LFO (三角波)。対数 delay 領域で線形に滑り、sin のような両端滞留がない。
  const double lfoL = 4.0 * std::abs(mLfoPhaseL * (1.0 / kTwoPi) - 0.5) - 1.0;
  const double lfoR = 4.0 * std::abs(mLfoPhaseR * (1.0 / kTwoPi) - 0.5) - 1.0;
  mLfoPhaseL += mLfoInc;
  mLfoPhaseR += mLfoInc;
  if (mLfoPhaseL >= kTwoPi) mLfoPhaseL -= kTwoPi;
  if (mLfoPhaseR >= kTwoPi) mLfoPhaseR -= kTwoPi;

  // 2. delay = Manual × 2^(tri × depthOct)。cubic stencil と buffer 一周の範囲に
  //    クランプ。
  const double maxDelay = static_cast<double>(kBufferSize) - 4.0;
  const double delayL =
      std::clamp(mManualSamples * std::exp2(lfoL * mDepthOct), kMinDelaySamples, maxDelay);
  const double delayR =
      std::clamp(mManualSamples * std::exp2(lfoR * mDepthOct), kMinDelaySamples, maxDelay);

  // 3. feedback を整形 (HPF → LPF → softclip) して write。
  const double hpfL = mFbStateL - mFbHpfX1L + mFbHpfR * mFbHpfY1L;
  mFbHpfX1L = mFbStateL;
  mFbHpfY1L = hpfL;
  const double hpfR = mFbStateR - mFbHpfX1R + mFbHpfR * mFbHpfY1R;
  mFbHpfX1R = mFbStateR;
  mFbHpfY1R = hpfR;
  mFbLpfYL += mFbLpfB * (hpfL - mFbLpfYL);
  mFbLpfYR += mFbLpfB * (hpfR - mFbLpfYR);

  // Color=Inv は負帰還。正帰還のままだと帰還ピークが反転 wet 和の notch と
  // ぶつかるので、wet と同じ極性で揃える。
  mBufL[mWritePos] = dryL + mPolarity * fastTanh(mFbLpfYL * mFeedback);
  mBufR[mWritePos] = dryR + mPolarity * fastTanh(mFbLpfYR * mFeedback);

  // 4. read (LFO modulated)。TZF 時は dry 側も Manual 固定 tap から読む。
  const double delayedL = readDelay(mBufL, delayL);
  const double delayedR = readDelay(mBufR, delayR);
  mFbStateL = delayedL;
  mFbStateR = delayedR;

  double refL = dryL;
  double refR = dryR;
  if (mTzf == 1) {
    const double refDelay = std::clamp(mManualSamples, kMinDelaySamples, maxDelay);
    refL = readDelay(mBufL, refDelay);
    refR = readDelay(mBufR, refDelay);
  }

  // 5. write pointer 進める。
  mWritePos = (mWritePos + 1) % kBufferSize;

  // 6. wet = (ref ± delayed) / 2 で comb 干渉を作る。TZF では modulated tap が
  //    ref tap を通過する瞬間に両者が同一サンプルになり、Inv なら完全 null。
  const double wetL = 0.5 * (refL + mPolarity * delayedL);
  const double wetR = 0.5 * (refR + mPolarity * delayedR);

  // 7. dry/wet ミックス。
  l = dryL * (1.0 - mMix) + wetL * mMix;
  r = dryR * (1.0 - mMix) + wetR * mMix;
}

}  // namespace dx10::dsp

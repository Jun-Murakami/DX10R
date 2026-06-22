#include "DigitalChorus.h"

#include "../FastMath.h"

#include <algorithm>
#include <cmath>

namespace dx10::dsp {

namespace {
constexpr double kPi = 3.141592653589793;
constexpr double kTwoPi = 2.0 * kPi;

// feedback 経路の帯域整形カットオフ (Flanger / Phaser と共通思想)。HPF で低域
// ブーム (DC ループゲイン 1/(1-fb)) を切り、LPF で帰還の超高域を落とす。
constexpr double kFbHpfHz = 100.0;
constexpr double kFbLpfHz = 9000.0;

// Detune knob 1.0 のときの片側 rate spread (= ±5%)。これより上げると LFO 周期
// 同士の相対関係が大きく崩れて「コーラスらしさ」より「フランジャ寄り」になる
// ため、5% 上限に固定。
constexpr double kMaxDetuneRangeRatio = 0.05;

/** 0..1 を log で min..max にマップ (rate 用)。 */
double logMap(double v01, double min, double max) {
  v01 = std::clamp(v01, 0.0, 1.0);
  return min * std::pow(max / min, v01);
}
}  // namespace

void DigitalChorus::prepare(double sampleRate) {
  mSampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
  // base delay ≒ 7ms 中心、depth で ±15ms 程度スイープ。最大遅延を kBufferSize
  // 以下に必ず収めるようクランプは setParam 側でも行う。
  mBaseDelaySamples = std::min(0.007 * mSampleRate, static_cast<double>(kBufferSize) / 2.0);
  mFbHpfR = std::exp(-kTwoPi * kFbHpfHz / mSampleRate);
  mFbLpfB = 1.0 - std::exp(-kTwoPi * kFbLpfHz / mSampleRate);
  recomputeVoiceRates();
  reset();
}

void DigitalChorus::reset() {
  std::fill(mBufL.begin(), mBufL.end(), 0.0);
  std::fill(mBufR.begin(), mBufR.end(), 0.0);
  mWritePos = 0;
  mFbHpfX1L = mFbHpfY1L = 0.0;
  mFbHpfX1R = mFbHpfY1R = 0.0;
  mFbLpfYL = mFbLpfYR = 0.0;
  // 起動時から voice 同士の位相を decorrelate するため、等間隔に分散する。
  // N=1 でも phase[0]=0 になり既存挙動と一致。
  for (int i = 0; i < kMaxVoices; ++i) {
    mPhase[i] =
        (mNumVoices > 1) ? (kTwoPi * static_cast<double>(i) / mNumVoices) : 0.0;
  }
}

void DigitalChorus::recomputeVoiceRates() noexcept {
  for (int i = 0; i < kMaxVoices; ++i) {
    double rateMultiplier = 1.0;
    if (mNumVoices > 1 && i < mNumVoices) {
      // t_i ∈ [-1, +1] で symmetric distribution。N=2 → -1, +1。N=4 → -1, -1/3, +1/3, +1。
      const double t = (static_cast<double>(i) / (mNumVoices - 1)) * 2.0 - 1.0;
      rateMultiplier = 1.0 + t * mDetuneAmount * kMaxDetuneRangeRatio;
    }
    mPhaseInc[i] = kTwoPi * mRateHz * rateMultiplier / mSampleRate;
  }
}

void DigitalChorus::setParam(int paramIdx, double value01) {
  value01 = std::clamp(value01, 0.0, 1.0);
  switch (paramIdx) {
    case 0:  // Rate (0.05..6 Hz, log)
      mRateHz = logMap(value01, 0.05, 6.0);
      recomputeVoiceRates();
      break;
    case 1: {  // Depth (sweep 振幅, 0..15ms)
      const double depthMs = value01 * 15.0;
      const double depthSamples = depthMs * 0.001 * mSampleRate;
      // base + depth が buffer 長を超えないようクランプ。
      const double maxDepth = static_cast<double>(kBufferSize) - mBaseDelaySamples - 4.0;
      mDepthSamples = std::min(depthSamples, std::max(0.0, maxDepth));
      break;
    }
    case 2:  // Width (M/S サイド倍率)。0..2 linear、0.5 で 1.0 (normal)。
      mWidth = value01 * 2.0;
      break;
    case 3:  // Feedback (0..0.7)。loop gain は process() の coherent 1/N 正規化で
             // voice 数に依らず fb 一定、かつ feedback 経路の HPF/LPF/softclip で
             // bounded なので 0.7 でも発散しない。
      mFeedback = value01 * 0.7;
      break;
    case 4:  // Mix (0=dry, 1=wet)。信号フロー上、Width で stereo 化したあと最後に
             // dry/wet を混ぜるので paramOffset も最後 (= 直近) に置く。
      mMix = value01;
      break;
    case 5: {  // Voices (1..kMaxVoices の enum、value01 を等分割で量子化)
      const int n = kMaxVoices;
      const int next = std::clamp(
          static_cast<int>(std::round(value01 * (n - 1))) + 1, 1, n);
      if (next != mNumVoices) {
        mNumVoices = next;
        recomputeVoiceRates();
        // 新規 voice は等間隔 phase で起動 (current run 中の voice の phase は維持
        // して連続性を保ちつつ、新規 voice だけ初期化)。シンプルに「voice 数が
        // 増えた直後の追加 voice」だけ phase を補完する。減るときは何もしない。
        for (int i = 0; i < mNumVoices; ++i) {
          // 既存の phase は触らず、未使用枠 (将来 voice 増のため) だけ初期化。
        }
      }
      break;
    }
    case 6:  // Detune (0..1 → ±0..5% rate spread)
      mDetuneAmount = value01;
      recomputeVoiceRates();
      break;
    default:
      // 未使用 param。将来の拡張用に noop。
      break;
  }
}

double DigitalChorus::readDelay(const std::array<double, kBufferSize>& buf,
                              double samplesAgo) const noexcept {
  // samplesAgo ≥ 1 を期待。fractional 線形補間。
  const double readPos = static_cast<double>(mWritePos) - samplesAgo;
  // wrap 後の浮動 index。
  const double bufLen = static_cast<double>(kBufferSize);
  double idx = readPos;
  while (idx < 0.0) idx += bufLen;
  while (idx >= bufLen) idx -= bufLen;
  const int i0 = static_cast<int>(idx);
  const int i1 = (i0 + 1 < kBufferSize) ? (i0 + 1) : 0;
  const double frac = idx - static_cast<double>(i0);
  return buf[i0] * (1.0 - frac) + buf[i1] * frac;
}

void DigitalChorus::process(double& l, double& r) noexcept {
  const double dryL = l;
  const double dryR = r;

  // 1. 全 voice 加算 (L/R)。voice ごとに rate がわずかに違うので、phase も
  //    drift して密度のあるコーラス感が出る。
  double sumL = 0.0;
  double sumR = 0.0;
  for (int i = 0; i < mNumVoices; ++i) {
    const double phaseL = mPhase[i];
    const double phaseR = phaseL + kPi;  // 同 voice 内 L/R は 180° 位相差
    const double lfoL = std::sin(phaseL);
    const double lfoR = std::sin(phaseR);
    // phase advance。
    mPhase[i] += mPhaseInc[i];
    if (mPhase[i] >= kTwoPi) mPhase[i] -= kTwoPi;
    // delay 読出し位置。LFO の -1..+1 を 0..+1 にして depth 振幅にスケール。
    const double delayL = mBaseDelaySamples + mDepthSamples * 0.5 * (1.0 + lfoL);
    const double delayR = mBaseDelaySamples + mDepthSamples * 0.5 * (1.0 + lfoR);
    sumL += readDelay(mBufL, std::max(1.0, delayL));
    sumR += readDelay(mBufR, std::max(1.0, delayR));
  }
  // 2. voice 数による level 正規化。1/sqrt(N) で「voice 増えても全体音量が
  //    維持される」感覚に近づける (位相相関を仮定したパワー保存)。
  const double normGain =
      (mNumVoices > 1) ? 1.0 / std::sqrt(static_cast<double>(mNumVoices)) : 1.0;
  const double wetL = sumL * normGain;
  const double wetR = sumR * normGain;

  // 3. delay buffer 書き込み。feedback はループゲインが voice 数に依存しないよう、
  //    出力用の 1/sqrt(N) ではなく coherent な 1/N で正規化した「1 タップ相当」を
  //    使う。各 voice は中心遅延付近の近接 tap を読むので低域は同相加算 (sum≈N×tap)
  //    になり、1/sqrt(N) を流用すると loop gain = fb×sqrt(N) → N≥2 でユニティ超過し
  //    指数発散・クリップしていた。1/N でゲインを fb 一定に固定する。
  const double fbNorm =
      (mNumVoices > 1) ? 1.0 / static_cast<double>(mNumVoices) : 1.0;
  const double fbInL = sumL * fbNorm;
  const double fbInR = sumR * fbNorm;
  // HPF → LPF → softclip で帯域整形 (Flanger / Phaser と同思想)。loop gain は既に
  // fb (≤0.7) < 1 なので tanh は通常域ほぼ線形で、発振寸前のみ効く最終安全弁。
  const double hpfL = fbInL - mFbHpfX1L + mFbHpfR * mFbHpfY1L;
  mFbHpfX1L = fbInL;
  mFbHpfY1L = hpfL;
  const double hpfR = fbInR - mFbHpfX1R + mFbHpfR * mFbHpfY1R;
  mFbHpfX1R = fbInR;
  mFbHpfY1R = hpfR;
  mFbLpfYL += mFbLpfB * (hpfL - mFbLpfYL);
  mFbLpfYR += mFbLpfB * (hpfR - mFbLpfYR);
  mBufL[mWritePos] = dryL + fastTanh(mFeedback * mFbLpfYL);
  mBufR[mWritePos] = dryR + fastTanh(mFeedback * mFbLpfYR);
  mWritePos = (mWritePos + 1) % kBufferSize;

  // 4. M/S 処理で wet の stereo 幅を調整 (mWidth: 0=mono, 1=normal, 2=wider)。
  const double m = 0.5 * (wetL + wetR);
  const double s = 0.5 * (wetL - wetR) * mWidth;
  const double widedL = m + s;
  const double widedR = m - s;

  // 5. dry/wet ミックス。
  l = dryL * (1.0 - mMix) + widedL * mMix;
  r = dryR * (1.0 - mMix) + widedR * mMix;
}

}  // namespace dx10::dsp

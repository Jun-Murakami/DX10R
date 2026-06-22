#include "Reverb.h"

#include <algorithm>
#include <cmath>

namespace dx10::dsp {

namespace {
constexpr double kPi = 3.141592653589793;
constexpr double kTwoPi = 2.0 * kPi;

// 入力 pre-LP cutoff (Hz)。Dattorro 原典は 4730 Hz 相当だが、ホール / 部屋系の
// topology も通すので 9kHz と少し開いて bright 寄り。
constexpr double kPreLpCutoffHz = 9000.0;

// Knob → 実値マッピング。
constexpr double kPredelayMaxMs = 200.0;
constexpr double kRt60MinSec = 0.3;
constexpr double kRt60MaxSec = 10.0;
constexpr double kToneFcMin = 1000.0;
constexpr double kToneFcMax = 16000.0;

// Mode 切替クロスフェード時間 (ms)。50ms 程度でクリック無しに 0↔1 を遷移できる。
constexpr double kCrossfadeMs = 50.0;

double logMap(double t01, double lo, double hi) noexcept {
  return lo * std::pow(hi / lo, std::clamp(t01, 0.0, 1.0));
}

double lpCoeffFromCutoff(double fcHz, double sr) noexcept {
  return 1.0 - std::exp(-kTwoPi * fcHz / sr);
}
}  // namespace

void Reverb::prepare(double sampleRate) {
  mSampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;

  mPreLpCoeff = lpCoeffFromCutoff(kPreLpCutoffHz, mSampleRate);
  recomputeCrossfadeStep();

  mHall.prepare(mSampleRate);
  mPlate.prepare(mSampleRate);
  mRoom.prepare(mSampleRate);

  reset();
}

void Reverb::reset() {
  mPredelayBuf.fill(0.0);
  mPredelayWrite = 0;
  mPreLpState = 0.0;
  mToneStateL = 0.0;
  mToneStateR = 0.0;
  mHall.reset();
  mPlate.reset();
  mRoom.reset();
  // Mode gain は target に強制スナップ (= 状態リセット時にクロスフェード残骸を残さない)
  mGains = mTargetGains;
}

void Reverb::recomputeCrossfadeStep() noexcept {
  const int n = static_cast<int>(kCrossfadeMs * 0.001 * mSampleRate + 0.5);
  mGainStep = 1.0 / static_cast<double>((n > 1) ? n : 1);
}

void Reverb::retargetGainsForMode(int mode) noexcept {
  for (int i = 0; i < kNumModes; ++i) {
    mTargetGains[i] = (i == mode) ? 1.0 : 0.0;
  }
}

ReverbCore& Reverb::core(int idx) noexcept {
  switch (idx) {
    case 0: return mHall;
    case 2: return mRoom;
    case 1:
    default: return mPlate;
  }
}

void Reverb::setParam(int paramIdx, double value01) {
  value01 = std::clamp(value01, 0.0, 1.0);
  switch (paramIdx) {
    case 0: {  // Mode (0=Hall, 1=Plate, 2=Room)
      const int next = std::clamp(
          static_cast<int>(std::round(value01 * (kNumModes - 1))), 0, kNumModes - 1);
      if (next != mActiveMode) {
        // (2026-06-12 perf D2) gain 0 の core は process がスキップされ「凍結した
        // 古い tail」を保持している。再活性化の前に reset してゼロから build-up
        // させる (50ms crossfade での fade-in なので通常のリバーブ切替の立ち上がり
        // になり、凍結 tail のゴースト再生を避ける)。
        if (mGains[next] <= 0.0) core(next).reset();
        mActiveMode = next;
        retargetGainsForMode(next);
      }
      break;
    }
    case 1: {  // Pre-Delay (0..200 ms linear)
      double pd = (value01 * kPredelayMaxMs * 0.001) * mSampleRate;
      if (pd > static_cast<double>(kPredelayBufSize - 4)) {
        pd = static_cast<double>(kPredelayBufSize - 4);
      }
      mPredelaySamples = pd;
      break;
    }
    case 2: {  // Decay (knob 0..1 → RT60 0.3..10 sec、log map)
      const double rt60 = logMap(value01, kRt60MinSec, kRt60MaxSec);
      mHall.setTargetRt60(rt60);
      mPlate.setTargetRt60(rt60);
      mRoom.setTargetRt60(rt60);
      break;
    }
    case 3:  // Damping
      mHall.setDamping01(value01);
      mPlate.setDamping01(value01);
      mRoom.setDamping01(value01);
      break;
    case 4:  // Diffusion
      mHall.setDiffusion01(value01);
      mPlate.setDiffusion01(value01);
      mRoom.setDiffusion01(value01);
      break;
    case 5: {  // Tone (knob 0=dark, 1=bright) on wet output
      const double fc = logMap(value01, kToneFcMin, kToneFcMax);
      mToneCoeff = lpCoeffFromCutoff(fc, mSampleRate);
      break;
    }
    case 6:  // Width (0..1 → 0..2 倍 M/S サイド)
      mWidth = value01 * 2.0;
      break;
    case 7:  // Mix (equal-power dry/wet)
      mMix = value01;
      mDryGain = std::cos(mMix * 0.5 * kPi);
      mWetGain = std::sin(mMix * 0.5 * kPi);
      break;
    default:
      break;
  }
}

void Reverb::process(double& l, double& r) noexcept {
  // 0. dry の保存。
  const double dryL = l;
  const double dryR = r;

  // 1. Pre-delay。入力は (L+R)/2 で mono 化。
  const double inMono = 0.5 * (l + r);
  mPredelayBuf[mPredelayWrite] = inMono;
  double pdIdx = static_cast<double>(mPredelayWrite) - mPredelaySamples;
  while (pdIdx < 0.0) pdIdx += static_cast<double>(kPredelayBufSize);
  const int pd0 = static_cast<int>(pdIdx) & kPredelayMask;
  const int pd1 = (pd0 + 1) & kPredelayMask;
  const double pdFrac = pdIdx - static_cast<double>(static_cast<int>(pdIdx));
  const double predelayed =
      mPredelayBuf[pd0] * (1.0 - pdFrac) + mPredelayBuf[pd1] * pdFrac;
  mPredelayWrite = (mPredelayWrite + 1) & kPredelayMask;

  // 2. Pre-LP (bandwidth 9kHz、固定)。
  mPreLpState += mPreLpCoeff * (predelayed - mPreLpState);
  const double x = mPreLpState;

  // 3. アクティブ (gain > 0 または fade-in 中) の core だけ process する。
  //    (2026-06-12 perf D2) 旧実装は 3 core 常時駆動 (= mode 切替で即フル tail)
  //    だったが、reverb 1 slot で CPU が単一 core の ~3 倍かかる。収束後は
  //    アクティブ core 単独、mode 切替時のみクロスフェード 50ms の間 2 core が
  //    並走する方式に変更 (新 core は setParam(0) で reset 済み = ゼロから
  //    build-up しつつ fade-in、旧 core は tail を保ったまま fade-out)。
  double hL = 0.0, hR = 0.0, pL = 0.0, pR = 0.0, rL = 0.0, rR = 0.0;
  if (mGains[0] > 0.0 || mTargetGains[0] > 0.0) mHall.process(x, hL, hR);
  if (mGains[1] > 0.0 || mTargetGains[1] > 0.0) mPlate.process(x, pL, pR);
  if (mGains[2] > 0.0 || mTargetGains[2] > 0.0) mRoom.process(x, rL, rR);

  // 4. mGains を target に向かって毎 sample ステップ。50ms で 0→1 完了。
  for (int i = 0; i < kNumModes; ++i) {
    const double t = mTargetGains[i];
    if (mGains[i] < t) {
      mGains[i] += mGainStep;
      if (mGains[i] > t) mGains[i] = t;
    } else if (mGains[i] > t) {
      mGains[i] -= mGainStep;
      if (mGains[i] < t) mGains[i] = t;
    }
  }

  // 5. wet を mode gain で混合。
  double wetL = hL * mGains[0] + pL * mGains[1] + rL * mGains[2];
  double wetR = hR * mGains[0] + pR * mGains[1] + rR * mGains[2];

  // 6. Tone LP on wet output (HF tilt)。
  mToneStateL += mToneCoeff * (wetL - mToneStateL);
  mToneStateR += mToneCoeff * (wetR - mToneStateR);
  wetL = mToneStateL;
  wetR = mToneStateR;

  // 7. M/S Width。
  const double m = 0.5 * (wetL + wetR);
  const double s = 0.5 * (wetL - wetR) * mWidth;
  wetL = m + s;
  wetR = m - s;

  // 8. dry/wet mix (equal-power)。
  l = dryL * mDryGain + wetL * mWetGain;
  r = dryR * mDryGain + wetR * mWetGain;
}

}  // namespace dx10::dsp

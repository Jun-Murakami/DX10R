#include "PlateCore.h"

#include <algorithm>
#include <cmath>

namespace dx10::dsp {

namespace {
constexpr double kPi = 3.141592653589793;
constexpr double kTwoPi = 2.0 * kPi;

// Lexicon 224 / Dattorro 論文の reference sample rate。
constexpr double kRefSampleRate = 29761.0;

// Input diffuser delay (sample @29761)。論文 Table 1。
constexpr int kInputApBaseDelay[4] = {142, 107, 379, 277};
constexpr double kInputApBaseCoeff[4] = {0.75, 0.75, 0.625, 0.625};

// Tank component base delay (@29761)。
constexpr double kTankAP5L_Base = 672.0;
constexpr double kTankAP5R_Base = 908.0;
constexpr double kTankD1L_Base = 4453.0;
constexpr double kTankD1R_Base = 4217.0;
constexpr double kTankAP6L_Base = 1800.0;
constexpr double kTankAP6R_Base = 2656.0;
constexpr double kTankD2L_Base = 3720.0;
constexpr double kTankD2R_Base = 3163.0;

constexpr double kTankAP5Coeff = -0.7;
constexpr double kTankAP6Coeff = 0.5;

// 出力 tap offsets (@29761)。論文 Table 1。
constexpr int kTapA_Base[7] = {266, 2974, 1913, 1996, 1990, 187, 1066};
constexpr int kTapB_Base[7] = {353, 3627, 1228, 2673, 2111, 335, 121};
constexpr double kTapA_Sign[7] = {+1.0, +1.0, -1.0, +1.0, -1.0, -1.0, -1.0};
constexpr double kTapB_Sign[7] = {+1.0, +1.0, -1.0, +1.0, -1.0, -1.0, -1.0};
constexpr double kTapSumGain = 0.6;

constexpr double kModRateHz = 0.7;
constexpr double kModDepthSamples = 8.0;

// Tank 1 周分の delay 合計 (@29761)。L 経路 10645 + R 経路 10944、平均 10794.5。
// /29761 で sec に直して約 0.3627 sec。
constexpr double kBaseMeanLoopSec =
    ((672.0 + 4453.0 + 1800.0 + 3720.0) +
     (908.0 + 4217.0 + 2656.0 + 3163.0)) *
    0.5 / kRefSampleRate;
constexpr double kDecayGainMin = 0.05;
constexpr double kDecayGainMax = 0.95;
constexpr double kDampingFcMin = 200.0;
constexpr double kDampingFcMax = 16000.0;

double logMap(double t01, double lo, double hi) noexcept {
  return lo * std::pow(hi / lo, std::clamp(t01, 0.0, 1.0));
}

double lpCoeffFromCutoff(double fcHz, double sr) noexcept {
  return 1.0 - std::exp(-kTwoPi * fcHz / sr);
}
}  // namespace

void PlateCore::prepare(double sampleRate) {
  mSampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
  recomputeFixedSrCoeffs();
  recomputeDiffusion();
  recomputeDecayGain();
  reset();
}

void PlateCore::reset() {
  for (auto& ap : mInputAP) ap.reset();
  mTankAPL5.reset();
  mTankAPR5.reset();
  mTankAPL6.reset();
  mTankAPR6.reset();
  mTankDelayL1.reset();
  mTankDelayR1.reset();
  mTankDelayL2.reset();
  mTankDelayR2.reset();
  mDampStateL = 0.0;
  mDampStateR = 0.0;
  mPrevTankL = 0.0;
  mPrevTankR = 0.0;
  mModPhase = 0.0;
}

void PlateCore::recomputeFixedSrCoeffs() noexcept {
  const double srFactor = mSampleRate / kRefSampleRate;

  // Input AP delays。
  for (int i = 0; i < 4; ++i) {
    int d = static_cast<int>(static_cast<double>(kInputApBaseDelay[i]) * srFactor + 0.5);
    if (d < 1) d = 1;
    if (d > kInputApBufSize - 4) d = kInputApBufSize - 4;
    mInputAP[i].delaySamples = d;
  }
  mModInc = kTwoPi * kModRateHz / mSampleRate;
  const double modDepthScaled = kModDepthSamples * srFactor;
  mTankAPL5.modDepth = modDepthScaled;
  mTankAPR5.modDepth = modDepthScaled;
  mTankAPL5.coeff = kTankAP5Coeff;
  mTankAPR5.coeff = kTankAP5Coeff;
  mTankAPL6.coeff = kTankAP6Coeff;
  mTankAPR6.coeff = kTankAP6Coeff;

  // Tank delay 長 / tap offset (Plate は ×1.0 固定、mode 倍率なし)。
  auto scale = [&](double base) noexcept -> int {
    int s = static_cast<int>(base * srFactor + 0.5);
    if (s < 1) s = 1;
    if (s > kTankBufSize - 4) s = kTankBufSize - 4;
    return s;
  };

  mTankAPL5.baseDelay = static_cast<double>(scale(kTankAP5L_Base));
  mTankAPR5.baseDelay = static_cast<double>(scale(kTankAP5R_Base));
  mTankDelayL1.delaySamples = scale(kTankD1L_Base);
  mTankDelayR1.delaySamples = scale(kTankD1R_Base);
  mTankAPL6.delaySamples = scale(kTankAP6L_Base);
  mTankAPR6.delaySamples = scale(kTankAP6R_Base);
  mTankDelayL2.delaySamples = scale(kTankD2L_Base);
  mTankDelayR2.delaySamples = scale(kTankD2R_Base);

  for (int i = 0; i < 7; ++i) {
    int a = static_cast<int>(static_cast<double>(kTapA_Base[i]) * srFactor + 0.5);
    int b = static_cast<int>(static_cast<double>(kTapB_Base[i]) * srFactor + 0.5);
    if (a < 1) a = 1;
    if (b < 1) b = 1;
    if (a > kTankBufSize - 4) a = kTankBufSize - 4;
    if (b > kTankBufSize - 4) b = kTankBufSize - 4;
    mTapsA[i] = a;
    mTapsB[i] = b;
  }
}

void PlateCore::recomputeDiffusion() noexcept {
  for (int i = 0; i < 4; ++i) {
    mInputAP[i].coeff = kInputApBaseCoeff[i] * mDiffusion;
  }
}

void PlateCore::recomputeDecayGain() noexcept {
  // RT60 → g 逆算: g = 10^(-3 × mean_loop_sec / RT60)
  const double rt60 = (mTargetRt60 > 1e-6) ? mTargetRt60 : 1e-6;
  double g = std::pow(10.0, -3.0 * kBaseMeanLoopSec / rt60);
  if (g < kDecayGainMin) g = kDecayGainMin;
  if (g > kDecayGainMax) g = kDecayGainMax;
  mDecay = g;
}

void PlateCore::setTargetRt60(double seconds) {
  mTargetRt60 = (seconds > 1e-6) ? seconds : 1e-6;
  recomputeDecayGain();
}

void PlateCore::setDamping01(double v) {
  v = std::clamp(v, 0.0, 1.0);
  const double fc = logMap(1.0 - v, kDampingFcMin, kDampingFcMax);
  mDampCoeff = lpCoeffFromCutoff(fc, mSampleRate);
}

void PlateCore::setDiffusion01(double v) {
  mDiffusion = std::clamp(v, 0.0, 1.0);
  recomputeDiffusion();
}

void PlateCore::process(double xMono, double& wetL, double& wetR) noexcept {
  // 1. 4 series input diffuser。AP coeff は Diffusion knob で scale 済み。
  double x = xMono;
  x = mInputAP[0].process(x);
  x = mInputAP[1].process(x);
  x = mInputAP[2].process(x);
  x = mInputAP[3].process(x);

  // 2. Modulation LFO。L/R で位相 π オフセット。
  const double lfoL = std::sin(mModPhase);
  const double lfoR = std::sin(mModPhase + kPi);
  mModPhase += mModInc;
  if (mModPhase >= kTwoPi) mModPhase -= kTwoPi;

  // 3. Tank L 経路。前 sample の R 出力を decay 乗じて cross-feed。
  const double inL = x + mDecay * mPrevTankR;
  const double ap5L_out = mTankAPL5.process(inL, lfoL);
  const double d1L_out = mTankDelayL1.tap(mTankDelayL1.delaySamples);
  mTankDelayL1.write(ap5L_out);
  mDampStateL += mDampCoeff * (d1L_out - mDampStateL);
  const double ap6L_in = mDecay * mDampStateL;
  const double ap6L_out = mTankAPL6.process(ap6L_in);
  const double d2L_out = mTankDelayL2.tap(mTankDelayL2.delaySamples);
  mTankDelayL2.write(ap6L_out);
  const double tankL_out = d2L_out;

  // 4. Tank R 経路 (対称)。
  const double inR = x + mDecay * mPrevTankL;
  const double ap5R_out = mTankAPR5.process(inR, lfoR);
  const double d1R_out = mTankDelayR1.tap(mTankDelayR1.delaySamples);
  mTankDelayR1.write(ap5R_out);
  mDampStateR += mDampCoeff * (d1R_out - mDampStateR);
  const double ap6R_in = mDecay * mDampStateR;
  const double ap6R_out = mTankAPR6.process(ap6R_in);
  const double d2R_out = mTankDelayR2.tap(mTankDelayR2.delaySamples);
  mTankDelayR2.write(ap6R_out);
  const double tankR_out = d2R_out;

  mPrevTankL = tankL_out;
  mPrevTankR = tankR_out;

  // 5. Multi-tap 出力。L 用は R 側 component を多く読み、R 用は L 側を多く。
  double oL = 0.0;
  oL += kTapA_Sign[0] * mTankDelayR1.tap(mTapsA[0]);
  oL += kTapA_Sign[1] * mTankDelayR1.tap(mTapsA[1]);
  oL += kTapA_Sign[2] * mTankAPR6.bufTap(mTapsA[2]);
  oL += kTapA_Sign[3] * mTankDelayR2.tap(mTapsA[3]);
  oL += kTapA_Sign[4] * mTankDelayL1.tap(mTapsA[4]);
  oL += kTapA_Sign[5] * mTankAPL6.bufTap(mTapsA[5]);
  oL += kTapA_Sign[6] * mTankDelayL2.tap(mTapsA[6]);
  oL *= kTapSumGain;

  double oR = 0.0;
  oR += kTapB_Sign[0] * mTankDelayL1.tap(mTapsB[0]);
  oR += kTapB_Sign[1] * mTankDelayL1.tap(mTapsB[1]);
  oR += kTapB_Sign[2] * mTankAPL6.bufTap(mTapsB[2]);
  oR += kTapB_Sign[3] * mTankDelayL2.tap(mTapsB[3]);
  oR += kTapB_Sign[4] * mTankDelayR1.tap(mTapsB[4]);
  oR += kTapB_Sign[5] * mTankAPR6.bufTap(mTapsB[5]);
  oR += kTapB_Sign[6] * mTankDelayR2.tap(mTapsB[6]);
  oR *= kTapSumGain;

  wetL = oL;
  wetR = oR;
}

}  // namespace dx10::dsp

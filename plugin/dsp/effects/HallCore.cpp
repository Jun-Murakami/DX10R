#include "HallCore.h"

#include <algorithm>
#include <cmath>

namespace dx10::dsp {

namespace {
constexpr double kTwoPi = 6.283185307179586;

// 入力 diffuser delay (ms)。Gardner Large Hall 系を踏襲した 4 段 (~2.1 倍ずつ
// 拡大)。段数を 2 → 4 に増やして echo density を倍加し、初期反射を「密」に
// 持っていく (= ドラム缶感の主因だった discrete echo を消す)。
constexpr double kInputApMs[HallCore::kNumInputAp] = {4.7, 9.8, 17.5, 31.4};
constexpr double kInputApCoeff[HallCore::kNumInputAp] = {0.65, 0.6, 0.55, 0.5};

// 8 並列 delay lines の base delay (ms)。"小ホール感" を脱するため tank 全体を
// 大ホールサイズ (~114ms mean loop) に拡大。互いに近接しない 8 値を取って comb
// 干渉を避ける。
//   65 / 78 / 92 / 107 / 121 / 134 / 149 / 165 ms
constexpr double kDelayMs[HallCore::kNumLines] = {
    65.0, 78.0, 92.0, 107.0, 121.0, 134.0, 149.0, 165.0};

// Hadamard 8×8 行列 (Sylvester 構成 H8 = [H4 H4; H4 -H4]、sign のみ)。scaling は 1/√8。
// [ 1  1  1  1  1  1  1  1 ]
// [ 1 -1  1 -1  1 -1  1 -1 ]
// [ 1  1 -1 -1  1  1 -1 -1 ]
// [ 1 -1 -1  1  1 -1 -1  1 ]    × 1/√8 ≈ 0.3536
// [ 1  1  1  1 -1 -1 -1 -1 ]
// [ 1 -1  1 -1 -1  1 -1  1 ]
// [ 1  1 -1 -1 -1 -1  1  1 ]
// [ 1 -1 -1  1 -1  1  1 -1 ]
constexpr double kHadamardScale = 0.35355339059327373;  // 1/√8

// RT60 → g 逆算用の mean delay sec (8 line 平均)。kDelayMs の更新に追従。
constexpr double kBaseMeanLoopSec =
    (kDelayMs[0] + kDelayMs[1] + kDelayMs[2] + kDelayMs[3] +
     kDelayMs[4] + kDelayMs[5] + kDelayMs[6] + kDelayMs[7]) *
    0.125 / 1000.0;

constexpr double kDecayGainMin = 0.3;
constexpr double kDecayGainMax = 0.99;

constexpr double kDampingFcMin = 200.0;
constexpr double kDampingFcMax = 16000.0;

// Tank 内 LFO modulation。各 line ごとに rate を変えて metallic resonance を散らす。
// depth ±3 sample (= 192k で ~15 μs / 48k で ~62 μs)、可聴 vibrato にならず
// 共鳴ピークだけ散らす範囲。
constexpr double kModRateHz[HallCore::kNumLines] = {
    0.41, 0.59, 0.71, 0.89, 1.03, 1.19, 1.31, 1.49};
constexpr double kModDepthSamples = 3.0;

// 各 line の damping LP cutoff オフセット (semitone、baseline からのずれ)。
// 8 line の HF 減衰速度を ±3 semitone (= ±18%) でばらつかせて、tank の各 mode が
// 一斉に decay しないようにする。結果として "uniform metallic ring" が分散して
// 聞こえなくなる (= 各 mode で減衰時定数が違う = ほぐれた tail)。
constexpr double kDampFcOffsetSemitones[HallCore::kNumLines] = {
    -3.0, +1.5, +3.0, -1.5, -2.0, +0.5, +2.0, -0.5};

// Output multi-tap (full delay の倍率)。3 tap を取って sum することで初期反射
// 密度を稼ぐ。feedback path には絡めず、出力にだけ使う。
constexpr double kOutputTapFracs[HallCore::kNumOutputTaps] = {0.6, 0.85, 1.0};
// 各 channel に 12 contribution (3 tap × 4 line) を sum するので 1/√12 で正規化。
constexpr double kOutputScale = 0.2886751345948129;  // 1/√12

// 出力 low shelf。180Hz の 1-pole LP で低域成分を抽出し、(G - 1) を掛けて入力に
// 足し戻す = 1-pole low shelf。DC で -1 dB、HF で 0 dB に漸近、180Hz 付近で
// shelf knee (1-pole shelf は corner で漸近値の約半分 = -0.46 dB)。Hall tank が
// 長く bass が溜まりやすいので軽く叩く。
constexpr double kLowShelfFcHz = 180.0;
// G = 10^(-1 / 20) = 0.891251、 (G - 1) = -0.108749
constexpr double kLowShelfDelta = -0.108749;

double logMap(double t01, double lo, double hi) noexcept {
  return lo * std::pow(hi / lo, std::clamp(t01, 0.0, 1.0));
}

double lpCoeffFromCutoff(double fcHz, double sr) noexcept {
  return 1.0 - std::exp(-kTwoPi * fcHz / sr);
}

int msToSamples(double ms, double sr, int maxSamples) noexcept {
  int s = static_cast<int>(ms * 0.001 * sr + 0.5);
  if (s < 1) s = 1;
  if (s > maxSamples) s = maxSamples;
  return s;
}
}  // namespace

void HallCore::prepare(double sampleRate) {
  mSampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
  recomputeFixedSrCoeffs();
  recomputeDiffusion();
  recomputeDecayGain();

  // LFO inc を sample rate から計算、phase は kNumLines 等分でずらして各 line の
  // mod を decorrelate。
  for (int i = 0; i < kNumLines; ++i) {
    mLfoInc[i] = kTwoPi * kModRateHz[i] / mSampleRate;
    mLfoPhase[i] = (kTwoPi / static_cast<double>(kNumLines)) * static_cast<double>(i);
  }

  // 出力 low shelf 用 1-pole LP の coeff (180Hz 固定)
  mShelfCoeff = lpCoeffFromCutoff(kLowShelfFcHz, mSampleRate);

  reset();
}

void HallCore::reset() {
  for (auto& ap : mInputAP) ap.reset();
  for (auto& d : mDelays) d.reset();
  mDampStates.fill(0.0);
  mShelfStateL = 0.0;
  mShelfStateR = 0.0;
  // LFO phase は維持 (連続性のため reset しない)。
}

void HallCore::recomputeFixedSrCoeffs() noexcept {
  for (int i = 0; i < kNumInputAp; ++i) {
    mInputAP[i].delaySamples =
        msToSamples(kInputApMs[i], mSampleRate, kInputApBufSize - 4);
  }
  for (int i = 0; i < kNumLines; ++i) {
    // LFO mod (±2 sample) と output multi-tap が full 倍率まで届くので、buffer 末端に
    // 余裕を残す (-8)。
    mDelays[i].delaySamples =
        msToSamples(kDelayMs[i], mSampleRate, kDelayBufSize - 8);
  }
}

void HallCore::recomputeDiffusion() noexcept {
  for (int i = 0; i < kNumInputAp; ++i) {
    mInputAP[i].coeff = kInputApCoeff[i] * mDiffusion;
  }
}

void HallCore::recomputeDecayGain() noexcept {
  const double rt60 = (mTargetRt60 > 1e-6) ? mTargetRt60 : 1e-6;
  double g = std::pow(10.0, -3.0 * kBaseMeanLoopSec / rt60);
  if (g < kDecayGainMin) g = kDecayGainMin;
  if (g > kDecayGainMax) g = kDecayGainMax;
  mDecay = g;
}

void HallCore::setTargetRt60(double seconds) {
  mTargetRt60 = (seconds > 1e-6) ? seconds : 1e-6;
  recomputeDecayGain();
}

void HallCore::setDamping01(double v) {
  v = std::clamp(v, 0.0, 1.0);
  const double baseFc = logMap(1.0 - v, kDampingFcMin, kDampingFcMax);
  for (int i = 0; i < kNumLines; ++i) {
    const double fc_i =
        baseFc * std::pow(2.0, kDampFcOffsetSemitones[i] / 12.0);
    mDampCoeffs[i] = lpCoeffFromCutoff(fc_i, mSampleRate);
  }
}

void HallCore::setDiffusion01(double v) {
  mDiffusion = std::clamp(v, 0.0, 1.0);
  recomputeDiffusion();
}

void HallCore::process(double xMono, double& wetL, double& wetR) noexcept {
  // 1. 入力 4 段 diffuser
  double x = xMono;
  for (auto& ap : mInputAP) x = ap.process(x);

  // 2. LFO phase を進める。各 line で sin の振幅を mod[i] (±kModDepthSamples) に。
  std::array<double, kNumLines> mod;
  for (int i = 0; i < kNumLines; ++i) {
    mLfoPhase[i] += mLfoInc[i];
    if (mLfoPhase[i] > kTwoPi) mLfoPhase[i] -= kTwoPi;
    mod[i] = std::sin(mLfoPhase[i]) * kModDepthSamples;
  }

  // 3. Feedback tap: full delay + LFO mod、線形補間。
  std::array<double, kNumLines> fb;
  for (int i = 0; i < kNumLines; ++i) {
    const double off = static_cast<double>(mDelays[i].delaySamples) + mod[i];
    fb[i] = mDelays[i].tapInterp(off);
  }

  // 4. 各線で damping LP。各 line 個別の cutoff (= base × ±3 semitone) で HF 減衰
  //    速度をずらすことで、4 mode が一斉に decay する uniform ring を分散させる。
  for (int i = 0; i < kNumLines; ++i) {
    mDampStates[i] += mDampCoeffs[i] * (fb[i] - mDampStates[i]);
    fb[i] = mDampStates[i];
  }

  // 5. 8×8 Hadamard mixing (Sylvester H8 = [H4 H4; H4 -H4])。scale 1/√8 で unitary。
  const double m0 = kHadamardScale * (fb[0] + fb[1] + fb[2] + fb[3] + fb[4] + fb[5] + fb[6] + fb[7]);
  const double m1 = kHadamardScale * (fb[0] - fb[1] + fb[2] - fb[3] + fb[4] - fb[5] + fb[6] - fb[7]);
  const double m2 = kHadamardScale * (fb[0] + fb[1] - fb[2] - fb[3] + fb[4] + fb[5] - fb[6] - fb[7]);
  const double m3 = kHadamardScale * (fb[0] - fb[1] - fb[2] + fb[3] + fb[4] - fb[5] - fb[6] + fb[7]);
  const double m4 = kHadamardScale * (fb[0] + fb[1] + fb[2] + fb[3] - fb[4] - fb[5] - fb[6] - fb[7]);
  const double m5 = kHadamardScale * (fb[0] - fb[1] + fb[2] - fb[3] - fb[4] + fb[5] - fb[6] + fb[7]);
  const double m6 = kHadamardScale * (fb[0] + fb[1] - fb[2] - fb[3] - fb[4] - fb[5] + fb[6] + fb[7]);
  const double m7 = kHadamardScale * (fb[0] - fb[1] - fb[2] + fb[3] - fb[4] + fb[5] + fb[6] - fb[7]);

  // 6. ×g feedback gain を掛けて入力と合算、各 delay に書き戻す。
  mDelays[0].write(x + mDecay * m0);
  mDelays[1].write(x + mDecay * m1);
  mDelays[2].write(x + mDecay * m2);
  mDelays[3].write(x + mDecay * m3);
  mDelays[4].write(x + mDecay * m4);
  mDelays[5].write(x + mDecay * m5);
  mDelays[6].write(x + mDecay * m6);
  mDelays[7].write(x + mDecay * m7);

  // 7. Multi-tap output: 各 line から 3 tap (0.6 / 0.85 / 1.0 倍)、L = even index
  //    (0/2/4/6)、R = odd index (1/3/5/7)。modulation は乗せず、純粋な多 tap で
  //    初期反射密度を稼ぐ。
  double sumL = 0.0;
  double sumR = 0.0;
  for (int t = 0; t < kNumOutputTaps; ++t) {
    const double frac = kOutputTapFracs[t];
    for (int i = 0; i < kNumLines; i += 2) {
      const double offEven = static_cast<double>(mDelays[i].delaySamples) * frac;
      const double offOdd = static_cast<double>(mDelays[i + 1].delaySamples) * frac;
      sumL += mDelays[i].tapInterp(offEven);
      sumR += mDelays[i + 1].tapInterp(offOdd);
    }
  }
  wetL = sumL * kOutputScale;
  wetR = sumR * kOutputScale;

  // 8. 出力 low shelf (180Hz @ -1.5dB)。1-pole LP で低域成分を抽出し、
  //    (G - 1) を掛けて入力に足し戻す = 1-pole low shelf。
  mShelfStateL += mShelfCoeff * (wetL - mShelfStateL);
  mShelfStateR += mShelfCoeff * (wetR - mShelfStateR);
  wetL += kLowShelfDelta * mShelfStateL;
  wetR += kLowShelfDelta * mShelfStateR;
}

}  // namespace dx10::dsp

#include "RoomCore.h"

#include <algorithm>
#include <cmath>

namespace dx10::dsp {

namespace {
constexpr double kTwoPi = 6.283185307179586;

// 入力 diffuser (mono、ER と Late で共有)。Gardner Small Room の 3 段配置を踏襲。
constexpr double kInputApMs[RoomCore::kNumInputAp] = {0.7, 8.85, 21.7};
constexpr double kInputApCoeff[RoomCore::kNumInputAp] = {0.5, 0.5, 0.625};

// === Early Reflection tap pattern (ms) ===
// L と R で第 1 tap が 5.0 vs 6.3 と異なり、tap 間隔も非等間隔 (4..7ms 程度) で
// 取って discrete な「粒」感を出す。実部屋の壁 / 床 / 天井 反射の到来時刻を
// 模した不規則性。
constexpr double kERTapsLMs[RoomCore::kNumERTaps] = {
    5.0, 9.7, 14.2, 18.6, 23.4, 30.1};
constexpr double kERTapsRMs[RoomCore::kNumERTaps] = {
    6.3, 11.5, 15.9, 21.0, 26.7, 33.5};
// 6 tap × 1/√6 で energy 保存。
constexpr double kERTapGain = 1.0 / 2.449489742783178;  // 1/√6
// ER 出力に掛ける "距離による HF 減衰" を一括模擬する 1-pole LP。
// 9kHz で立ち上がりは bright のまま。
constexpr double kERLpFcHz = 9000.0;
// ER と Late の最終ミックス比。ER 重視で「Room らしい初期反射の鮮明さ」を出す。
constexpr double kERMixGain = 0.65;
constexpr double kLateMixGain = 0.35;

// === Late tank: 8-line Hadamard FDN ===
// Even index (0/2/4/6) → wetL、odd index (1/3/5/7) → wetR。delay 長は 8 値すべて
// 互いに近接しない ms 値で、Hadamard 8×8 で全結合すると単一周期 dominance が
// 完全に消えて drum-can 反復が解消する (= modal density 4 line 比 2×)。
constexpr double kLateDelayMs[RoomCore::kNumLateLines] = {
    19.0, 22.0, 25.0, 28.0, 31.0, 34.0, 37.0, 41.0};

// Hadamard 8×8 (Sylvester H8 = [H4 H4; H4 -H4]) scaling (× 1/√8 で unitary)。
constexpr double kHadamardScale = 0.35355339059327373;  // 1/√8

// 各 line の damping LP cutoff オフセット (semitone、baseline からのずれ)。
// ±3 semitone (= ±18%) で 8 line の HF 減衰時定数を分散させ、各 mode が一斉に
// decay する uniform metal ring を防ぐ (Hall と同じ手法)。
constexpr double kDampFcOffsetSemitones[RoomCore::kNumLateLines] = {
    -3.0, +1.5, +3.0, -1.5, -2.0, +0.5, +2.0, -0.5};

// LFO modulation。各 line ごとに rate を変えて metallic resonance を散らす。
// depth ±5 sample (= 192k で ~26μs / 48k で ~104μs) で可聴 vibrato にはならず、
// 共鳴ピークだけランダム化する範囲。
constexpr double kLateLfoRateHz[RoomCore::kNumLateLines] = {
    0.71, 0.83, 0.97, 1.13, 1.27, 1.41, 1.55, 0.89};
constexpr double kLateModDepthSamples = 5.0;

// RT60 → g 逆算用 mean loop sec。8 line 平均。
constexpr double kBaseMeanLoopSec =
    (kLateDelayMs[0] + kLateDelayMs[1] + kLateDelayMs[2] + kLateDelayMs[3] +
     kLateDelayMs[4] + kLateDelayMs[5] + kLateDelayMs[6] + kLateDelayMs[7]) *
    0.125 / 1000.0;

constexpr double kDecayGainMin = 0.05;
// Mean ~28.75ms と短いので g_max=0.97 で RT60 ~ 6 秒台まで届く。それ以上は
// 素直に clamp ("Hall に切り替えて" の暗黙ヒント)。
constexpr double kDecayGainMax = 0.97;

// Room は tail の HF 減衰を Hall/Plate より早めたいので、Damping LP の cutoff
// 上限を 16k → 8k Hz に下げる (= 同じ Damping ノブ位置でも Room だけ 1 オクターブ
// 暗い tail)。物理的にも小空間は壁面・空気吸収で HF が早く落ちるので自然。
constexpr double kDampingFcMin = 200.0;
constexpr double kDampingFcMax = 8000.0;

// Damping ノブ応答カーブ。v_eff = v^kDampingResponseExp で v を bow させる。
// exp < 1 にすると中間ノブ位置 (= 0.2..0.7) で実効 v が高め側に行き、cutoff が
// 速く下がる = Hall/Plate より「同じ値で効きやすい」体感になる。両端 v=0, v=1 は
// 不変なので bright/dark 極限は維持。0.7 は効きすぎたので 0.85 に緩和。
constexpr double kDampingResponseExp = 0.85;

// Room モードは Hall / Plate と同じ Decay ノブ位置でも実際の tail を短くする
// (= 物理的に小部屋らしい挙動)。setTargetRt60 で受け取った秒数をこの倍率で
// 縮めて internal RT60 にする。Hall/Plate は 1.0 (= 縮めない)。
constexpr double kRoomRt60Scale = 0.5;

// Tank 出力の channel sum normalize (4 line × 1/√4 = 0.5)。
constexpr double kTankChannelScale = 0.5;

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

void RoomCore::prepare(double sampleRate) {
  mSampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
  recomputeFixedSrCoeffs();
  recomputeDiffusion();
  recomputeDecayGain();

  // ER 出力 LP coeff
  mERLpCoeff = lpCoeffFromCutoff(kERLpFcHz, mSampleRate);

  // Late tank LFO inc / 初期 phase (4 line を kNumLateLines 等分でずらして decorrelate)
  for (int i = 0; i < kNumLateLines; ++i) {
    mLfoInc[i] = kTwoPi * kLateLfoRateHz[i] / mSampleRate;
    mLfoPhase[i] =
        (kTwoPi / static_cast<double>(kNumLateLines)) * static_cast<double>(i);
  }

  reset();
}

void RoomCore::reset() {
  for (auto& ap : mInputAP) ap.reset();
  mERDelay.reset();
  for (auto& d : mLateDelays) d.reset();
  mDampStates.fill(0.0);
  mERLpStateL = 0.0;
  mERLpStateR = 0.0;
  // LFO phase は維持 (連続性のため reset しない)。
}

void RoomCore::recomputeFixedSrCoeffs() noexcept {
  const int safeMax = kBufSize - 4;

  for (int i = 0; i < kNumInputAp; ++i) {
    mInputAP[i].delaySamples = msToSamples(kInputApMs[i], mSampleRate, safeMax);
  }

  // ER tap (L/R 別パターン)。書き込み buffer は単一なので、tap 値を sample 数に
  // 変換しておく。
  for (int t = 0; t < kNumERTaps; ++t) {
    mERTapsL[t] = msToSamples(kERTapsLMs[t], mSampleRate, safeMax);
    mERTapsR[t] = msToSamples(kERTapsRMs[t], mSampleRate, safeMax);
  }

  // Late FDN delays (LFO mod ±5 sample を踏まえて末端を -8 で安全側に)
  for (int i = 0; i < kNumLateLines; ++i) {
    mLateDelays[i].delaySamples =
        msToSamples(kLateDelayMs[i], mSampleRate, kBufSize - 8);
  }
}

void RoomCore::recomputeDiffusion() noexcept {
  for (int i = 0; i < kNumInputAp; ++i) {
    mInputAP[i].coeff = kInputApCoeff[i] * mDiffusion;
  }
}

void RoomCore::recomputeDecayGain() noexcept {
  const double rt60 = (mTargetRt60 > 1e-6) ? mTargetRt60 : 1e-6;
  double g = std::pow(10.0, -3.0 * kBaseMeanLoopSec / rt60);
  if (g < kDecayGainMin) g = kDecayGainMin;
  if (g > kDecayGainMax) g = kDecayGainMax;
  mDecay = g;
}

void RoomCore::setTargetRt60(double seconds) {
  // Room は internal で短く解釈する (= "Decay 5s" でも実際の tail は ~2.5s)。
  // 同じ Decay ノブ位置で Hall / Plate より「短く聞こえる」ようにするため。
  // 副次効果として g が小さくなり (= less feedback build-up)、metal ring も弱化する。
  const double scaled = seconds * kRoomRt60Scale;
  mTargetRt60 = (scaled > 1e-6) ? scaled : 1e-6;
  recomputeDecayGain();
}

void RoomCore::setDamping01(double v) {
  v = std::clamp(v, 0.0, 1.0);
  // 応答カーブで bow: 中間ノブで cutoff が速く下がる (= Hall/Plate より効きやすい)。
  const double v_eff = std::pow(v, kDampingResponseExp);
  const double baseFc = logMap(1.0 - v_eff, kDampingFcMin, kDampingFcMax);
  // 各 line で ±3 semitone offset した cutoff を持たせて HF 減衰時定数をばらつかせる。
  for (int i = 0; i < kNumLateLines; ++i) {
    const double fc_i =
        baseFc * std::pow(2.0, kDampFcOffsetSemitones[i] / 12.0);
    mDampCoeffs[i] = lpCoeffFromCutoff(fc_i, mSampleRate);
  }
}

void RoomCore::setDiffusion01(double v) {
  mDiffusion = std::clamp(v, 0.0, 1.0);
  recomputeDiffusion();
}

void RoomCore::process(double xMono, double& wetL, double& wetR) noexcept {
  // 1. 入力 3 段 diffuser (mono、ER と Late で共有)
  double x = xMono;
  x = mInputAP[0].process(x);
  x = mInputAP[1].process(x);
  x = mInputAP[2].process(x);

  // 2. ER 経路: 単一 mono delay 線に書き込み、L/R 別パターンで 6 tap ずつ取って sum。
  mERDelay.write(x);
  double erL = 0.0;
  double erR = 0.0;
  for (int t = 0; t < kNumERTaps; ++t) {
    erL += mERDelay.tap(mERTapsL[t]);
    erR += mERDelay.tap(mERTapsR[t]);
  }
  erL *= kERTapGain;
  erR *= kERTapGain;
  // 距離による HF 減衰を一括模擬する 9kHz LP。
  mERLpStateL += mERLpCoeff * (erL - mERLpStateL);
  mERLpStateR += mERLpCoeff * (erR - mERLpStateR);
  const double erOutL = mERLpStateL;
  const double erOutR = mERLpStateR;

  // 3. Late tank: 4-line Hadamard FDN
  // 3a. LFO phase 進行 + mod 計算
  std::array<double, kNumLateLines> mod;
  for (int i = 0; i < kNumLateLines; ++i) {
    mLfoPhase[i] += mLfoInc[i];
    if (mLfoPhase[i] > kTwoPi) mLfoPhase[i] -= kTwoPi;
    mod[i] = std::sin(mLfoPhase[i]) * kLateModDepthSamples;
  }

  // 3b. 各 line から tap (mod 込み、線形補間)
  std::array<double, kNumLateLines> y;
  for (int i = 0; i < kNumLateLines; ++i) {
    const double off = static_cast<double>(mLateDelays[i].delaySamples) + mod[i];
    y[i] = mLateDelays[i].tapInterp(off);
  }

  // 3c. 各 line で個別 cutoff の damping LP
  std::array<double, kNumLateLines> d;
  for (int i = 0; i < kNumLateLines; ++i) {
    mDampStates[i] += mDampCoeffs[i] * (y[i] - mDampStates[i]);
    d[i] = mDampStates[i];
  }

  // 3d. 8×8 Hadamard mix (Sylvester H8 = [H4 H4; H4 -H4])。scale 1/√8 で unitary。
  const double m0 = kHadamardScale * (d[0] + d[1] + d[2] + d[3] + d[4] + d[5] + d[6] + d[7]);
  const double m1 = kHadamardScale * (d[0] - d[1] + d[2] - d[3] + d[4] - d[5] + d[6] - d[7]);
  const double m2 = kHadamardScale * (d[0] + d[1] - d[2] - d[3] + d[4] + d[5] - d[6] - d[7]);
  const double m3 = kHadamardScale * (d[0] - d[1] - d[2] + d[3] + d[4] - d[5] - d[6] + d[7]);
  const double m4 = kHadamardScale * (d[0] + d[1] + d[2] + d[3] - d[4] - d[5] - d[6] - d[7]);
  const double m5 = kHadamardScale * (d[0] - d[1] + d[2] - d[3] - d[4] + d[5] - d[6] + d[7]);
  const double m6 = kHadamardScale * (d[0] + d[1] - d[2] - d[3] - d[4] - d[5] + d[6] + d[7]);
  const double m7 = kHadamardScale * (d[0] - d[1] - d[2] + d[3] - d[4] + d[5] + d[6] - d[7]);

  // 3e. ×g feedback gain を掛けて diffused 入力 x と合算、各 delay に書き戻す。
  mLateDelays[0].write(x + mDecay * m0);
  mLateDelays[1].write(x + mDecay * m1);
  mLateDelays[2].write(x + mDecay * m2);
  mLateDelays[3].write(x + mDecay * m3);
  mLateDelays[4].write(x + mDecay * m4);
  mLateDelays[5].write(x + mDecay * m5);
  mLateDelays[6].write(x + mDecay * m6);
  mLateDelays[7].write(x + mDecay * m7);

  // 3f. Tank 出力: even index (0/2/4/6) → L、odd index (1/3/5/7) → R。
  //     各 channel 4 line × 1/√4 = 0.5 で正規化。
  const double tankL = (d[0] + d[2] + d[4] + d[6]) * kTankChannelScale;
  const double tankR = (d[1] + d[3] + d[5] + d[7]) * kTankChannelScale;

  // 4. 出力 mix。ER 重視 (= Room らしい初期反射の鮮明さ)、Late は背景に控えめ。
  wetL = erOutL * kERMixGain + tankL * kLateMixGain;
  wetR = erOutR * kERMixGain + tankR * kLateMixGain;
}

}  // namespace dx10::dsp

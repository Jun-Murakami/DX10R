// Generic digital chorus (Synth-80, "Digital Chorus")。
//
// 複数ボイスの modulated delay line + feedback。モダン synth pedal 系の wide
// chorus を想定し、各 voice で LFO rate を微妙にずらして (Detune) 厚みを出す
// 設計。Synth Chorus (Juno BBD) / Studio Chorus (Dimension D) は固有の path
// 構成を持つのに対して、こちらは voice 数を 1〜4 で連続的に切替えられる汎用
// digital chorus。Pitch Chorus (rack-mount harmonizer 系) とは原理が違い、
// LFO で delay 周期を揺らして chorus 感を作る伝統型。
//
// パラメータ (EffectBase::setParam の paramIdx → 解釈):
//   0: Rate     (0..1 → 0.05..6 Hz, log curve)。base LFO rate。
//   1: Depth    (0..1 → 0..15 ms 程度のスイープ幅)
//   2: Width    (0..1 → 0..2 倍の M/S サイド成分スケール、0.5=normal=1.0)
//   3: Feedback (0..1 → 0..0.7 程度の feedback 量)
//   4: Mix      (0..1 → dry/wet 比、0=dry, 1=wet)
//   5: Voices   (0..1 → 1/2/3/4 voice の 4 段 enum)
//   6: Detune   (0..1 → ±0..5% の rate spread。各 voice の rate を base から
//                ずらして時間 drift させ、厚みを作る)
//   7: 未使用
//
// stereo は LFO 位相反転で出す (L/R で 180° 位相差)。voice ≥2 では各 voice
// の初期 phase を `i × 2π / N` で等間隔分散して decorrelation を保証する。

#pragma once

#include "EffectBase.h"

#include <array>

namespace dx10::dsp {

class DigitalChorus final : public EffectBase {
 public:
  // 想定最大 delay = (base 7ms + depth 15ms + safety) @ 192kHz < 8000 sample。
  // 8192 で余裕を持たせる。
  static constexpr int kBufferSize = 8192;
  // Voices knob の最大段数 (= 並列 voice 数の上限)。
  static constexpr int kMaxVoices = 4;

  void prepare(double sampleRate) override;
  void reset() override;
  void setParam(int paramIdx, double value01) override;
  void process(double& l, double& r) noexcept override;

 private:
  /** 線形補間つきで delay buffer から value を読む。 */
  double readDelay(const std::array<double, kBufferSize>& buf, double samplesAgo) const noexcept;

  /** 各 voice の rate multiplier を mNumVoices と mDetuneAmount から再計算する。
   *  N=1 なら全 voice rate × 1.0 (= base rate のみ)。N≥2 では voice i に
   *  symmetric な spread を割り当てる: t_i = (i / (N-1)) × 2 - 1 ∈ [-1, +1] で、
   *  rate_i = base_rate × (1 + t_i × detune × 0.05)。 */
  void recomputeVoiceRates() noexcept;

  double mSampleRate{48000.0};

  std::array<double, kBufferSize> mBufL{};
  std::array<double, kBufferSize> mBufR{};
  int mWritePos{0};

  // feedback 経路の帯域整形係数 (Flanger / Phaser と同思想)。HPF で低域ブーム
  // (DC ループゲイン 1/(1-fb)) を切り、LPF で帰還の超高域を落とす。prepare() で
  // sampleRate から算出。
  double mFbHpfR{0.0};  // HPF 極 (= exp(-2π·fc/fs))
  double mFbLpfB{1.0};  // LPF 係数 (= 1 - exp(-2π·fc/fs))
  // feedback 整形フィルタの状態 (L/R)。
  double mFbHpfX1L{0.0}, mFbHpfY1L{0.0};
  double mFbHpfX1R{0.0}, mFbHpfY1R{0.0};
  double mFbLpfYL{0.0}, mFbLpfYR{0.0};

  // 各 voice の LFO phase / phase increment。LFO は base + per-voice detune で
  // 計算した rate に応じて進む。L/R は同 voice 内で +π オフセット。
  std::array<double, kMaxVoices> mPhase{};
  std::array<double, kMaxVoices> mPhaseInc{};

  // パラメータの実値 (0..1 から変換済み)。
  double mRateHz{0.5};
  double mDepthSamples{200.0};  // sweep 振幅 (sample)
  double mBaseDelaySamples{350.0};  // 中心遅延 (sample, 約 7ms @ 48k)
  double mMix{0.0};
  double mFeedback{0.0};
  // Stereo width 倍率 (M/S 処理で side 成分に乗算)。0=mono, 1=normal, 2=wider。
  double mWidth{1.0};
  int mNumVoices{2};        // 1..kMaxVoices
  double mDetuneAmount{0.3};  // 0..1
};

}  // namespace dx10::dsp

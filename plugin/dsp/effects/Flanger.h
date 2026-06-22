// Modulated short-delay flanger (Synth-80, "Flanger")。
//
// 短い modulated delay + feedback で comb filter 系の resonance を作る。
// EHX Electric Mistress / MXR Flanger / TC SCF 系の音作り向け。
//
// Phaser との違い:
//   - 周期的に並ぶ comb 状の notch / peak (= jet engine 感)
//   - delay 時間が連続的に変調されるので「金属的に滑る」キャラ
//   - feedback でレゾナンス的なピークが立ち、強くするとほぼ self-resonate
//
// スイープ設計: delay = Manual × 2^(tri × Depth × ±2oct)。対数領域の三角波
// スイープで、Depth 100% のとき 16:1 (Manual を幾何中心に ±2 octave) 動く。
// 旧実装は manual..1.9×manual (= 1 oct 未満) しか掃引せず jet 感が出なかった。
//
// 信号経路:
//
//   in → ┬ (dry)──────────────────┐
//        │                        │
//        + (feedback) → buffer → tap (LFO modulated) ──┬──+ → wet → mix
//                                                      │
//                                HPF→LPF→softclip ←────┘ (feedback shaping)
//
// feedback 経路の整形: delay line は DC ゲイン 1 のため、生の正帰還は低域を
// 1/(1-fb) でブーストして掃引する comb をマスクする (Phaser と同じ問題)。
// HPF + LPF + ソフトクリップで整形し、kMaxFeedback 0.97 まで安全に。
//
// delay 読み出しは 4 点 cubic Hermite 補間。線形補間は frac 依存の高域減衰で
// 掃引中に音が濁る。
//
// Color (Inv): wet 加算と feedback の極性を反転した「逆相フランジ」。notch が
// k/τ 配置 (DC 含む) に変わり hollow なキャラになる。TZF と組むとゼロ点通過の
// 瞬間に全帯域が完全に打ち消し合う tape flange の null が出る。
//
// TZF (Through-Zero Flanging): wet 和の dry 側を Manual 固定 tap に置き換え、
// modulated tap がそこを「ゼロ点通過」する。同一バッファの 2 tap 読みなので
// 交差瞬間は両 tap が同一サンプル = 干渉が厳密。wet 経路に Manual 分の遅延が
// 乗るため Mix 100% 推奨 (Mix < 100% では素の dry と comb が立つ)。
//
// パラメータ:
//   0: Rate     (0..1 → 0.05..8 Hz, log)
//   1: Depth    (0..1 → sweep 振幅、Manual を中心に ±2 oct)
//   2: Feedback (0..1 → 0..0.97)
//   3: Manual   (0..1 → 0.5..12 ms log、center delay)
//   4: Stereo   (0=Mono, 1=Stereo)
//   5: Mix      (0..1 → dry/wet)
//   6: Color    (0=Normal, 1=Inv)
//   7: TZF      (0=Off, 1=On)

#pragma once

#include "EffectBase.h"

#include <array>

namespace dx10::dsp {

class Flanger final : public EffectBase {
 public:
  // 最大 12ms × 2^2 (depth swing) × 192kHz = 9216 sample + cubic 余裕 → 16384。
  static constexpr int kBufferSize = 16384;

  void prepare(double sampleRate) override;
  void reset() override;
  void setParam(int paramIdx, double value01) override;
  void process(double& l, double& r) noexcept override;

 private:
  /** delay buffer から fractional read (4 点 cubic Hermite 補間)。 */
  double readDelay(const std::array<double, kBufferSize>& buf,
                   double samplesAgo) const noexcept;

  double mSampleRate{48000.0};

  std::array<double, kBufferSize> mBufL{};
  std::array<double, kBufferSize> mBufR{};
  int mWritePos{0};

  // Feedback 用に直前 wet を保持。
  double mFbStateL{0.0};
  double mFbStateR{0.0};

  // Feedback 経路の整形フィルタ (HPF: DC blocker 型 / LPF: 1-pole)。
  double mFbHpfR{0.0};
  double mFbLpfB{1.0};
  double mFbHpfX1L{0.0};
  double mFbHpfY1L{0.0};
  double mFbHpfX1R{0.0};
  double mFbHpfY1R{0.0};
  double mFbLpfYL{0.0};
  double mFbLpfYR{0.0};

  double mLfoPhaseL{0.0};
  double mLfoPhaseR{0.0};
  double mLfoInc{0.0};

  // パラメータ実値。
  double mManualMs{2.0};        // center delay (ms)
  double mDepthOct{0.0};        // sweep 振幅 (octave、0..kMaxDepthOctaves)
  double mFeedback{0.0};
  int mStereoMode{0};
  double mMix{0.0};
  double mPolarity{1.0};        // Color (Normal=+1, Inv=-1)
  int mTzf{0};                  // 0=Off, 1=On

  // 計算済み値 (sample 単位)。
  double mManualSamples{0.0};
};

}  // namespace dx10::dsp

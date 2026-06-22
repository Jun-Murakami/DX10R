// Cascaded all-pass phaser (Synth-80, "Phaser")。
//
// 2nd-order (biquad) all-pass を 2/3/4/6 セクションカスケードし、各セクションの
// center frequency を共通の LFO で変調する phaser。dry と phased を加算したときに
// 位相干渉で notch / peak が周波数領域に並び (1 セクション = 1 notch)、それが
// LFO で動くことで「うねり」を生む。Phase 90 / Small Stone 系の音作り向け。
//
// Sharp (Q) について: Q=0.5 で biquad AP は同一 fc の 1 次 AP × 2 に縮退し、
// classic な緩いノッチになる。Q を上げると位相遷移が急峻になりノッチが狭く
// 深い「えぐい」キャラへ。UI の Stages 表示 (4/6/8/12) は 1 次 AP 換算で、
// 内部セクション数はその半分。
//
// 信号経路:
//
//   in ──┬──────────────────────────────┐ (dry)
//        │                              │
//        + (feedback) ──→ AP2 × N ───┬──+ → wet (= dry + phased) → mix
//                                    │
//                  HPF→LPF→softclip ←┘ (feedback path shaping)
//
// feedback 経路の整形について: AP は DC でゲイン +1 のため、生の正帰還は
// 低域をループゲイン 1/(1-fb) で持ち上げてしまい (fb=0.7 で +10dB)、スイープする
// notch / peak をマスクする。実機ペダルの帰還路がカップリング C と帯域制限で
// 自然に切れているのに合わせ、HPF + LPF + ソフトクリップで整形する。
//
// LFO は三角波 (octave 領域で線形)。sin だとスイープ両端で速度が 0 になり
// 周期のかなりの時間を可聴域外の端で過ごして「効果が止まって聴こえる」ため。
//
// 旧実装の注意: 以前の 1 次 AP は差分方程式の符号反転で極が z=-1 側にミラー
// しており、ノッチが Center ではなく fs/2-fc 付近 (ほぼ可聴域外) に出ていた。
// 本実装 (RBJ allpass biquad) で notch は Center 通りに配置される。
//
// パラメータ (EffectBase::setParam の paramIdx → 解釈):
//   0: Rate     (0..1 → 0.05..10 Hz, log)
//   1: Depth    (0..1 → center sweep 振幅、Center を中心に上下 ±3 oct)
//   2: Feedback (0..1 → 0..0.97、resonance 強さ)
//   3: Stages   (0..3 → 4 / 6 / 8 / 12 段相当 = biquad 2 / 3 / 4 / 6 セクション)
//   4: Center   (0..1 → 200..4000 Hz, log)
//   5: Stereo   (0=Mono, 1=Stereo)。Stereo は L/R で LFO 位相 180° offset。
//   6: Mix      (0..1 → dry/wet 比)
//   7: Sharp    (0..1 → Q 0.5..4.0, log)

#pragma once

#include "EffectBase.h"

#include <array>

namespace dx10::dsp {

class Phaser final : public EffectBase {
 public:
  static constexpr int kMaxSections = 6;  // biquad AP セクション (= 12 段相当)

  void prepare(double sampleRate) override;
  void reset() override;
  void setParam(int paramIdx, double value01) override;
  void process(double& l, double& r) noexcept override;

 private:
  /** 2nd-order all-pass filter の状態。 */
  struct AllPass2 {
    double x1{0.0};
    double x2{0.0};
    double y1{0.0};
    double y2{0.0};
  };

  /**
   * N セクション cascade を 1 channel 分処理。
   * AP2: y = c·x + e·x1 + x2 - e·y1 - c·y2  (RBJ allpass、全セクション同係数)
   */
  double processCascade(std::array<AllPass2, kMaxSections>& sections,
                        double c, double e, int n, double in) noexcept;

  /** center freq (Hz) + Q → biquad AP 係数 (c, e)。 */
  void computeApCoeffs(double fc, double& c, double& e) const noexcept;

  double mSampleRate{48000.0};

  // L/R 各々に kMaxSections のセクション。実際に使うのは mNumSections 個。
  std::array<AllPass2, kMaxSections> mStagesL;
  std::array<AllPass2, kMaxSections> mStagesR;

  double mLfoPhaseL{0.0};
  double mLfoPhaseR{0.0};  // Stereo 時は phaseR = phaseL + π
  double mLfoInc{0.0};

  // Feedback 用に直前 wet を保持。
  double mFbStateL{0.0};
  double mFbStateR{0.0};

  // Feedback 経路の整形フィルタ (HPF: DC blocker 型 / LPF: 1-pole)。
  double mFbHpfR{0.0};   // HPF pole (sampleRate 依存、prepare で算出)
  double mFbLpfB{1.0};   // LPF coeff (同上)
  double mFbHpfX1L{0.0};
  double mFbHpfY1L{0.0};
  double mFbHpfX1R{0.0};
  double mFbHpfY1R{0.0};
  double mFbLpfYL{0.0};
  double mFbLpfYR{0.0};

  // パラメータ実値。
  double mDepthRatio{0.5};        // sweep 振幅 ratio (Center に対する scaling)
  double mFeedback{0.0};
  int mNumSections{2};            // 2/3/4/6 (UI 表示は 4/6/8/12 段相当)
  double mCenterFreqHz{700.0};
  int mStereoMode{0};             // 0=Mono, 1=Stereo
  double mMix{0.0};
  double mQ{0.5};                 // Sharp (0.5..4.0)
};

}  // namespace dx10::dsp

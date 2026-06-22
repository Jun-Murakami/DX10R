// Multi-algorithm distortion / saturation (Synth-80, "Distortion")。
//
// 5 種類の入力 → 出力カーブから選択する stateless saturator。State を持たない
// ので CPU 負荷は極めて軽い (数式 1 つ × ステレオ)。アルゴリズムごとに最終
// レベルが変わる (hard clip は ±1 ぴったり、soft 系は asymptote ±1) ので、
// ユーザは Drive で押し込んで Output で揃える設計。
//
// 信号フロー:
//   in → ×Drive (linear) → [Algo curve] → ×Output (linear, ±dB) → wet
//   l = dry × (1 - mix) + wet × mix
//
// パラメータ (EffectBase::setParam の paramIdx → 解釈):
//   0: Algo   (0..4 → Clip / Overdrive / Waveshaper / Tube / Tape)
//   1: Drive  (0..1 → 0..36 dB linear gain into saturator)
//   2: Output (0..1 → -24..+12 dB bipolar、0.5=0dB center detent)
//   3: Mix    (0..1 → dry/wet)
//   4..7: 未使用
//
// アルゴリズム:
//   - Clip:       hard clip ±1。digital fuzz 的、奇数次倍音が支配的。
//   - Overdrive:  tanh(x)。smooth soft clip、tube-screamer 系の温かさ。
//   - Waveshaper: x/(1+|x|)。algebraic soft sat、tanh より curve が gentle で
//                 倍音バランスが微妙に違う、独特の "fizz"。
//   - Tube:       非対称 tanh (x>0 で drive 1.2x、x<0 で 0.7x)。class A 真空管
//                 バイアスを模した偶数次倍音含み。
//   - Tape:       x/√(1+x²)。tanh より round な knee、tape の優しい飽和感。

#pragma once

#include "EffectBase.h"

namespace dx10::dsp {

class Distortion final : public EffectBase {
 public:
  enum class Algo : int {
    Clip = 0,
    Overdrive,
    Waveshaper,
    Tube,
    Tape,
  };

  void prepare(double sampleRate) override;
  void reset() override;
  void setParam(int paramIdx, double value01) override;
  void process(double& l, double& r) noexcept override;

 private:
  /** algo に応じた波形変換を 1 サンプルに適用。 */
  double applyAlgo(double x) const noexcept;

  Algo mAlgo{Algo::Overdrive};
  double mDriveLinear{2.0};       // 0..36 dB → 1..63 倍 (default 6 dB)
  double mOutputLinear{1.0};      // -24..+12 dB (default 0 dB)
  double mMix{1.0};
};

}  // namespace dx10::dsp

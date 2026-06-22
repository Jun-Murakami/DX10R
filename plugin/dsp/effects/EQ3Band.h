// 3-band DJ-style EQ (Synth-80, "EQ")。
//
// Pioneer DJM / Allen & Heath Xone 系の派手な cut/boost を意識した 3 帯域 EQ。
// Mixer EQ の控えめな ±12dB ではなく、cut 側を実質 -∞ (kill) まで持っていく
// asymmetric な味付け:
//
//   gain knob v01: 0..0.5 → -36..0 dB (kill 寄り)、0.5..1 → 0..+12 dB
//
// トポロジ (Low/High shelf はスロープを急にするため 3 段カスケード、音作り向け):
//   in → [Low Shelf × 3] → [Mid Peak] → [High Shelf × 3] → out
//
// 各 band は RBJ cookbook の biquad (2-pole)。Q は固定:
//   - Low / High shelf: Q = 1/√2 (Butterworth)、3 段で実質 ~18 dB/oct
//     各段は target gain の 1/3 ずつ (linear 上で立方根) を担当して総 gain 維持。
//   - Mid peak: Q = 1.0
//
// パラメータ (EffectBase::setParam の paramIdx → 解釈):
//   0: Low Gain  (0..1 → -36..+12 dB asymmetric)
//   1: Low Freq  (0..1 → 50..500 Hz log)
//   2: Mid Gain  (asymmetric)
//   3: Mid Freq  (200..5000 Hz log)
//   4: High Gain (asymmetric)
//   5: High Freq (2000..12000 Hz log)
//   6..7: 未使用

#pragma once

#include "EffectBase.h"

namespace dx10::dsp {

class EQ3Band final : public EffectBase {
 public:
  void prepare(double sampleRate) override;
  void reset() override;
  void setParam(int paramIdx, double value01) override;
  void process(double& l, double& r) noexcept override;

 private:
  /** 1 個の biquad (Direct Form I)、L/R 別 state。係数は共有。 */
  struct Biquad {
    // 正規化済み係数 (a0=1)。
    double b0{1.0}, b1{0.0}, b2{0.0}, a1{0.0}, a2{0.0};
    // L/R それぞれの input/output 履歴。
    double xL1{0.0}, xL2{0.0}, yL1{0.0}, yL2{0.0};
    double xR1{0.0}, xR2{0.0}, yR1{0.0}, yR2{0.0};

    void clearState() noexcept {
      xL1 = xL2 = yL1 = yL2 = 0.0;
      xR1 = xR2 = yR1 = yR2 = 0.0;
    }
    void process(double& l, double& r) noexcept {
      const double yL = b0 * l + b1 * xL1 + b2 * xL2 - a1 * yL1 - a2 * yL2;
      xL2 = xL1; xL1 = l; yL2 = yL1; yL1 = yL;
      l = yL;
      const double yR = b0 * r + b1 * xR1 + b2 * xR2 - a1 * yR1 - a2 * yR2;
      xR2 = xR1; xR1 = r; yR2 = yR1; yR1 = yR;
      r = yR;
    }
  };

  enum class ShelfKind { Low, High };

  /** Low/High shelf 用の RBJ 係数を計算。 */
  void computeShelfCoeffs(Biquad& bq, ShelfKind kind, double fc, double gainDb) noexcept;
  /** Peak (parametric) 用の RBJ 係数。 */
  void computePeakCoeffs(Biquad& bq, double fc, double gainDb, double q) noexcept;

  /** 0..1 → asymmetric dB (cut -36..0、boost 0..+12)。 */
  static double gainDbFromValue01(double v01) noexcept;

  // Low / High shelves are cascaded 3 段ずつ (~18 dB/oct、DJ EQ ライクで音作り向け)。
  Biquad mLow1, mLow2, mLow3;
  Biquad mMid;
  Biquad mHigh1, mHigh2, mHigh3;

  double mSampleRate{48000.0};

  // 実値 (dB / Hz)。setParam → recompute… で coef 更新。
  double mLowGainDb{0.0};
  double mLowFreqHz{200.0};
  double mMidGainDb{0.0};
  double mMidFreqHz{1000.0};
  double mHighGainDb{0.0};
  double mHighFreqHz{5000.0};
};

}  // namespace dx10::dsp

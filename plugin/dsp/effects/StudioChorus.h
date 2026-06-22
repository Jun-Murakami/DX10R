// Roland Dimension D / SDD-320 風 chorus (Synth-80, "Studio Chorus")。
//
// Dimension D は 2 系統の BBD delay を 1 つの LFO で逆相変調し、delay 後に
// dry / cross 成分を matrix mix することで「揺れているのにうねりが目立たない」
// 広がりを作る。COMP / EXP は BBD の noise reduction 由来の軽い色付けとして
// 簡易モデル化する。
//
// 実装:
//
//   L/R in → HPF → COMP → [BBD L/R, one LFO in opposite polarity] → EXP
//          → dry + wet-mid + wet-side matrix → Mix
//
//   - Mode 1..3 は base delay / depth / LFO rate の preset。
//   - Mode 4 は独立 preset ではなく、Mode 3 に wet boost を足した扱い。
//     SDD-320 の灰色ボタン相当として、通常 mode の wet pad を解除する。
//
// パラメータ (EffectBase::setParam の paramIdx → 解釈):
//   0: Mode (0=1, 1=2, 2=3, 3=4)
//   1: Width (0..1 → 0..2 倍の M/S サイド成分スケール)
//   2: legacy Mix (ignored; Dimension D と同じく processed 固定)
//   3..7: 未使用

#pragma once

#include "EffectBase.h"

#include <array>

namespace dx10::dsp {

class StudioChorus final : public EffectBase {
 public:
  static constexpr int kBufferSize = 4096;
  static constexpr int kNumLines = 2;

  void prepare(double sampleRate) override;
  void reset() override;
  void setParam(int paramIdx, double value01) override;
  void process(double& l, double& r) noexcept override;

 private:
  /** 1 BBD line (mono buffer + filtering + compander envelope)。 */
  struct DelayLine {
    std::array<double, kBufferSize> buf{};
    int writePos{0};
    double hpX1{0.0};
    double hpY1{0.0};
    double lp1{0.0};  // anti-alias LP (delay 入力前)
    double lp2{0.0};  // reconstruction LP (delay 出力後)
    double compEnv{0.0};
  };

  /** 1 BBD line 分の処理。 */
  double processLine(int idx, double in, double delaySamples) noexcept;

  /** 線形補間つきで delay buffer から value を読む。 */
  double readDelay(const DelayLine& line, double samplesAgo) const noexcept;

  /** Mode に応じて delay / depth / LFO / wet trim を再計算。 */
  void recomputeModeCoeffs() noexcept;

  static double dbToAmp(double db) noexcept;
  static double triangleLfo(double phase) noexcept;

  double mSampleRate{48000.0};

  std::array<DelayLine, kNumLines> mLines;

  // 現在 mode の中心 delay / depth / LFO。
  double mBaseDelaySamples{360.0};
  double mDepthSamples{96.0};
  double mLfoInc{0.0};
  double mLfoPhase{0.0};

  // 現在 Mode (0..3 = Mode 1..4)。
  int mMode{0};

  // Mode 1..3 は wet pad あり、Mode 4 は pad 解除 (= stronger effect)。
  double mWetTrim{0.31622776601683794};

  double mWidth{1.0};  // generated side 倍率 (0=mono, 1=normal, 2=wider)

  // 1-pole filter / compander coefficients。
  double mHpCoeff{0.99};
  double mLpCoeff{0.5};
  double mCompAttackCoeff{0.0};
  double mCompReleaseCoeff{0.0};
};

}  // namespace dx10::dsp

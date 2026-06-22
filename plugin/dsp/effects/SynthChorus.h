// JUNO-60 / JUNO-106 風 BBD chorus (Synth-80, "Synth Chorus")。
//
// Rate/Depth 可変の汎用 chorus ではなく、JUNO の fixed chorus buttons をモデル化する。
// 60 はやや暗く太いまとまり、106 は少し明るくステレオ反転感が強い方向で係数を分ける。
//
// モデル:
//   mono in → mild drive → [ MN3009/MN3007-like clocked delay x2 ] → fixed dry/wet blend
//
// パラメータ (EffectBase::setParam の paramIdx → 解釈):
//   0: Model  (0=JUNO-60, 1=JUNO-106)
//   1: Mode   (0=I, 1=II, 2=I+II)
//   2: Width  (0..1 → 0..2 倍の M/S サイド成分スケール、0.5=original)
//   3: Amount (0=dry, 1=modeled chorus circuit output)
//   4..7: 未使用

#pragma once

#include "EffectBase.h"

#include <array>

namespace dx10::dsp {

class SynthChorus final : public EffectBase {
 public:
  static constexpr int kBufferSize = 4096;

  void prepare(double sampleRate) override;
  void reset() override;
  void setParam(int paramIdx, double value01) override;
  void process(double& l, double& r) noexcept override;

 private:
  struct DelayLine {
    std::array<double, kBufferSize> buf{};
    int writePos{0};
    double preLp{0.0};
    double postLp{0.0};
  };

  void recomputeModeCoeffs() noexcept;
  void recomputeMakeup() noexcept;

  double processLine(DelayLine& line,
                     double input,
                     double lfo,
                     bool invertMod) noexcept;
  double readDelay(const std::array<double, kBufferSize>& buf,
                   int writePos,
                   double samplesAgo) const noexcept;

  static double triangleLfo(double phase) noexcept;

  double mSampleRate{48000.0};

  DelayLine mLeftLine;
  DelayLine mRightLine;

  int mModel{0};
  int mMode{0};
  double mWidth{1.0};
  double mAmount{1.0};

  double mBaseDelaySamples{345.6};
  double mClockDepth{0.16};
  double mWetGain{0.86};
  double mDryGain{0.92};
  double mInputDrive{1.18};
  double mWidthScale{1.0};
  double mChorusMakeup{1.0};  // dry+wet 加算のパワー和正規化 (recomputeModeCoeffs)

  double mLfoPhase{0.0};
  double mLfoInc{0.0};

  double mPreLpCoeff{0.5};
  double mPostLpCoeff{0.5};
};

}  // namespace dx10::dsp

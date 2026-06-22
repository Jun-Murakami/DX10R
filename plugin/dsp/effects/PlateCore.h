// Plate topology (Lexicon 224 / Dattorro 1997 figure-eight tank)。
//
// 元 Reverb.cpp のロジックを ReverbCore interface に切り出したもの。Pre-Delay /
// Pre-LP / Tone LP / Width / Mix は外側 dispatcher (Reverb) が持つので、ここでは
// pure tank と input diffuser だけ。
//
// 信号経路:
//   xMono → 4× input diffuser AP (Diffusion knob で coeff scale)
//        → tank (figure-eight、L 側と R 側が前 sample で互いに cross-feed):
//             L: x + decay * prevR ─AP5L(mod)─ D1L ─damp_L─ ×decay ─AP6L─ D2L → tankL
//             R: x + decay * prevL ─AP5R(mod)─ D1R ─damp_R─ ×decay ─AP6R─ D2R → tankR
//        → multi-tap (Dattorro Table 1, 7 tap × 2 channel)
//        → wetL, wetR

#pragma once

#include "ReverbCore.h"

#include <array>

namespace dx10::dsp {

class PlateCore final : public ReverbCore {
 public:
  // Buffer サイズ (2^n、bitwise mask で循環読み出し)。
  // - 入力 diffuser: 277 sample @29761 → 192k で 1786 sample 必要。4096 で余裕。
  // - Tank 系: 4453 sample @29761 → 192k Hall (×1.5) で ~43000。65536 でクランプなし。
  static constexpr int kInputApBufSize = 4096;
  static constexpr int kInputApMask = kInputApBufSize - 1;
  static constexpr int kTankBufSize = 65536;
  static constexpr int kTankMask = kTankBufSize - 1;

  void prepare(double sampleRate) override;
  void reset() override;
  void setTargetRt60(double seconds) override;
  void setDamping01(double v) override;
  void setDiffusion01(double v) override;
  void process(double xMono, double& wetL, double& wetR) noexcept override;

 private:
  /** 内部 delay を持つ allpass (固定 delay)。 */
  template <int BUF_SIZE, int MASK>
  struct AllpassFixed {
    std::array<double, BUF_SIZE> buf{};
    int writePos{0};
    int delaySamples{0};
    double coeff{0.7};

    double process(double in) noexcept {
      const int readPos = (writePos - delaySamples) & MASK;
      const double delayed = buf[readPos];
      const double v = in - coeff * delayed;
      buf[writePos] = v;
      writePos = (writePos + 1) & MASK;
      return delayed + coeff * v;
    }
    double bufTap(int offset) const noexcept {
      const int readPos = (writePos - offset) & MASK;
      return buf[readPos];
    }
    void reset() noexcept {
      buf.fill(0.0);
      writePos = 0;
    }
  };

  using InputAP = AllpassFixed<kInputApBufSize, kInputApMask>;
  using TankAP = AllpassFixed<kTankBufSize, kTankMask>;

  /** Modulated allpass。delay 長を baseDelay ± modDepth*lfo で揺らす。 */
  struct ModAllpass {
    std::array<double, kTankBufSize> buf{};
    int writePos{0};
    double baseDelay{0.0};
    double modDepth{0.0};
    double coeff{-0.7};

    double process(double in, double lfo /* -1..+1 */) noexcept {
      const double d = baseDelay + lfo * modDepth;
      const double safeD = (d < 1.0) ? 1.0 : d;
      double idx = static_cast<double>(writePos) - safeD;
      if (idx < 0.0) idx += static_cast<double>(kTankBufSize);
      const int i0 = static_cast<int>(idx) & kTankMask;
      const int i1 = (i0 + 1) & kTankMask;
      const double frac = idx - static_cast<double>(static_cast<int>(idx));
      const double delayed = buf[i0] * (1.0 - frac) + buf[i1] * frac;
      const double v = in - coeff * delayed;
      buf[writePos] = v;
      writePos = (writePos + 1) & kTankMask;
      return delayed + coeff * v;
    }
    void reset() noexcept {
      buf.fill(0.0);
      writePos = 0;
    }
  };

  struct DelayLine {
    std::array<double, kTankBufSize> buf{};
    int writePos{0};
    int delaySamples{0};

    double tap(int offset) const noexcept {
      const int readPos = (writePos - offset) & kTankMask;
      return buf[readPos];
    }
    void write(double v) noexcept {
      buf[writePos] = v;
      writePos = (writePos + 1) & kTankMask;
    }
    void reset() noexcept {
      buf.fill(0.0);
      writePos = 0;
    }
  };

  void recomputeFixedSrCoeffs() noexcept;
  void recomputeDiffusion() noexcept;
  void recomputeDecayGain() noexcept;

  double mSampleRate{48000.0};

  std::array<InputAP, 4> mInputAP;

  ModAllpass mTankAPL5;
  ModAllpass mTankAPR5;
  DelayLine mTankDelayL1;
  DelayLine mTankDelayR1;
  TankAP mTankAPL6;
  TankAP mTankAPR6;
  DelayLine mTankDelayL2;
  DelayLine mTankDelayR2;

  double mDampStateL{0.0};
  double mDampStateR{0.0};
  double mDampCoeff{0.5};

  double mPrevTankL{0.0};
  double mPrevTankR{0.0};

  double mModPhase{0.0};
  double mModInc{0.0};

  std::array<int, 7> mTapsA{};
  std::array<int, 7> mTapsB{};

  double mTargetRt60{3.0};
  double mDecay{0.5};
  double mDiffusion{1.0};
};

}  // namespace dx10::dsp

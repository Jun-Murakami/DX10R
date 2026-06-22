// Room topology (Early Reflection + 8-line Hadamard FDN tank、Moorer 系を踏襲)。
//
// Plate (figure-8 dense) や Hall (大型 8×8 FDN) と異なるキャラを出すため、
// ER (= 部屋の壁 / 床 / 天井から discrete に戻ってくる初期反射) と
// Late (= 拡散した残響) を **並列** に走らせ、ER 重視で mix する。
// Late は 8-line Hadamard FDN (Hall と同じ topology だが delay 長は ~30ms 級と短い)
// で構成。modal density が 4 line 比 2× になり、Hall を縮めた smooth な dense tank
// として機能、単一 comb 周期の dominance が完全に消える。
//
// 設計:
// 信号経路:
//
//   xMono → AP_in1(0.7ms) → AP_in2(8.85ms) → AP_in3(21.7ms)
//        ↓
//        ├─→ ER delay write
//        │     L tap: 5.0 / 9.7 / 14.2 / 18.6 / 23.4 / 30.1 ms × 1/√6
//        │     R tap: 6.3 / 11.5 / 15.9 / 21.0 / 26.7 / 33.5 ms × 1/√6
//        │     ↓ 9kHz LP (per channel)
//        │     ER_L, ER_R
//        │
//        └─→ 8-line FDN: delay[i] (19..41ms + LFO mod ±5)
//                      → damp[i] (LP cutoff ±3 semitone offset per line)
//                      → Hadamard 8×8 mix × 1/√8
//                      → ×g feedback + diffused input → write
//                      → tank_L = (d[0]+d[2]+d[4]+d[6]) × 0.5
//                      → tank_R = (d[1]+d[3]+d[5]+d[7]) × 0.5
//
//   wetL = ER_L * 0.65 + tank_L * 0.35
//   wetR = ER_R * 0.65 + tank_R * 0.35
//
// RT60 維持: 8 line 平均 mean loop sec から g = 10^(-3 × T_mean / RT60) を逆算。
// Room モードのみ user 指定 RT60 を internal で × 0.5 縮めて鳴らす (= 物理的な
// 小部屋らしさ、Hall/Plate より明確に短い tail)。

#pragma once

#include "ReverbCore.h"

#include <array>

namespace dx10::dsp {

class RoomCore final : public ReverbCore {
 public:
  // Buffer サイズ。192k SR で最大 41ms (Late tank delay 末) = 7872 sample → 8192 で OK。
  static constexpr int kBufSize = 8192;
  static constexpr int kBufMask = kBufSize - 1;

  static constexpr int kNumERTaps = 6;
  static constexpr int kNumInputAp = 3;
  static constexpr int kNumLateLines = 8;

  void prepare(double sampleRate) override;
  void reset() override;
  void setTargetRt60(double seconds) override;
  void setDamping01(double v) override;
  void setDiffusion01(double v) override;
  void process(double xMono, double& wetL, double& wetR) noexcept override;

 private:
  struct AllpassFixed {
    std::array<double, kBufSize> buf{};
    int writePos{0};
    int delaySamples{0};
    double coeff{0.5};

    double process(double in) noexcept {
      const int readPos = (writePos - delaySamples) & kBufMask;
      const double delayed = buf[readPos];
      const double v = in - coeff * delayed;
      buf[writePos] = v;
      writePos = (writePos + 1) & kBufMask;
      return delayed + coeff * v;
    }
    void reset() noexcept {
      buf.fill(0.0);
      writePos = 0;
    }
  };

  struct DelayLine {
    std::array<double, kBufSize> buf{};
    int writePos{0};
    int delaySamples{0};

    /** 整数 offset の tap (ER multi-tap など modulation 不要な経路)。 */
    double tap(int offset) const noexcept {
      const int readPos = (writePos - offset) & kBufMask;
      return buf[readPos];
    }

    /** 線形補間 tap (LFO mod で fractional offset になる feedback path 用)。 */
    double tapInterp(double offset) const noexcept {
      double pos = static_cast<double>(writePos) - offset;
      while (pos < 0.0) pos += static_cast<double>(kBufSize);
      const int p0 = static_cast<int>(pos);
      const int p1 = (p0 + 1) & kBufMask;
      const double frac = pos - static_cast<double>(p0);
      return buf[p0 & kBufMask] * (1.0 - frac) + buf[p1] * frac;
    }

    void write(double v) noexcept {
      buf[writePos] = v;
      writePos = (writePos + 1) & kBufMask;
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

  // 入力 diffuser 3 段 (mono、ER と Late で共有)
  std::array<AllpassFixed, kNumInputAp> mInputAP;

  // ER tap delay (mono、書き込みは 1 本、L/R 用 tap pattern は 2 セット)
  DelayLine mERDelay;
  std::array<int, kNumERTaps> mERTapsL{};
  std::array<int, kNumERTaps> mERTapsR{};
  // ER 出力に掛ける固定 9kHz 1-pole LP の状態
  double mERLpStateL{0.0};
  double mERLpStateR{0.0};
  double mERLpCoeff{1.0};

  // Late tank: 4-line Hadamard FDN。line 0/1 → wetL、line 2/3 → wetR。
  std::array<DelayLine, kNumLateLines> mLateDelays;

  // 各 line の damping LP state (各 line 個別 cutoff、HF 減衰時定数を分散)
  std::array<double, kNumLateLines> mDampStates{};
  std::array<double, kNumLateLines> mDampCoeffs{};

  // 各 line の LFO modulation (sin、独立 phase + rate で metallic resonance を散らす)
  std::array<double, kNumLateLines> mLfoPhase{};
  std::array<double, kNumLateLines> mLfoInc{};

  double mTargetRt60{1.0};
  double mDecay{0.5};
  double mDiffusion{1.0};
};

}  // namespace dx10::dsp

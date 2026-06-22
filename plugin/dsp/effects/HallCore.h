// Hall topology (8×8 FDN with Hadamard mixing matrix)。
//
// 元: Jot 系の Feedback Delay Network。Plate (Dattorro figure-eight) や
// Room (Gardner 並列 loop) と違い、8 並列遅延線を **lossless mixing matrix** で
// 全結合 feedback する。Hadamard 行列は直交かつ実数のみで、modal density が
// 線形に積み上がるため "smooth で密なホール" 特有の長い tail が出る。8 line に
// 拡張すると 4 line 比 2× の modal density で Lexicon / Bricasti 系の上品な大ホール
// 感に近づく。
//
// 設計:
//   - 入力 4 段 series AP diffuser (echo density を Gardner large hall 相当に)。
//     段数を増やすほど初期反射の密度が上がり「discrete echo (= ドラム缶感)」が
//     消える。delay 長は 4.7 / 9.8 / 17.5 / 31.4 ms (~2.1 倍ずつ増)。
//   - その後 8 並列 delay lines。長さは互いに近接しない範囲で 8 値を取って
//     comb 干渉を避ける。範囲: 65..165ms (mean ~114ms、大ホールの後期反射相当)。
//   - 各 delay 線の feedback tap には ±3 sample の超微小 LFO modulation を入れて
//     fixed-pitch resonance (= metallic ringing) を散らす。各 line ごとに rate と
//     phase を変える: 0.41..1.49 Hz の 8 種。
//   - 各 line 出力に damping LP を入れて feedback 経路で HF を吸収。各 line で
//     ±3 semitone のオフセットを掛け、HF 減衰時定数をばらつかせて mode の uniform
//     decay を防ぐ。
//   - 8×8 Hadamard 行列で混合 (Sylvester 構成、unitary scaling 1/√8 ≈ 0.3536):
//       H8 = | H4   H4 |
//            | H4  -H4 |
//   - 混合後 ×g (decay) を掛けて、入力 x と足して各 delay の入力に戻す。
//   - 出力は 8 line × 3 tap (full delay の 0.6 / 0.85 / 1.0 倍) を sum して
//     L = even index (0/2/4/6) の tap sum、R = odd index (1/3/5/7) の tap sum。
//     1/√12 で正規化 (3 tap × 4 line per channel)。Multi-tap で初期反射の密度が
//     増し、L/R が delay 差で decorrelate。
//
// RT60 維持: 各 delay 線の平均 T_mean (sec) と feedback g から
//   RT60 ≈ -3 × T_mean / log10(g)
// で g を逆算する。N 線の Hadamard は lossless なので RT60 計算は単一 g を
// そのまま使える。

#pragma once

#include "ReverbCore.h"

#include <array>
#include <cmath>

namespace dx10::dsp {

class HallCore final : public ReverbCore {
 public:
  // Buffer サイズ。192k で 165ms = 31680 sample → 32768 で余裕。LFO mod ±3 sample
  // と output multi-tap (~165ms × 1.0) を踏まえても収まる。
  static constexpr int kDelayBufSize = 32768;
  static constexpr int kDelayBufMask = kDelayBufSize - 1;
  // 入力 diffuser AP は最大 ~31.4ms = 6029@192k → 8192。
  static constexpr int kInputApBufSize = 8192;
  static constexpr int kInputApBufMask = kInputApBufSize - 1;

  static constexpr int kNumLines = 8;
  static constexpr int kNumInputAp = 4;
  static constexpr int kNumOutputTaps = 3;

  void prepare(double sampleRate) override;
  void reset() override;
  void setTargetRt60(double seconds) override;
  void setDamping01(double v) override;
  void setDiffusion01(double v) override;
  void process(double xMono, double& wetL, double& wetR) noexcept override;

 private:
  struct InputAllpass {
    std::array<double, kInputApBufSize> buf{};
    int writePos{0};
    int delaySamples{0};
    double coeff{0.5};

    double process(double in) noexcept {
      const int readPos = (writePos - delaySamples) & kInputApBufMask;
      const double delayed = buf[readPos];
      const double v = in - coeff * delayed;
      buf[writePos] = v;
      writePos = (writePos + 1) & kInputApBufMask;
      return delayed + coeff * v;
    }
    void reset() noexcept {
      buf.fill(0.0);
      writePos = 0;
    }
  };

  struct DelayLine {
    std::array<double, kDelayBufSize> buf{};
    int writePos{0};
    int delaySamples{0};

    /** 整数 offset の tap (output multi-tap 用、modulation 不要な経路)。 */
    double tap(int offset) const noexcept {
      const int readPos = (writePos - offset) & kDelayBufMask;
      return buf[readPos];
    }

    /** 線形補間 tap。LFO mod で fractional offset になる経路用。 */
    double tapInterp(double offset) const noexcept {
      double pos = static_cast<double>(writePos) - offset;
      while (pos < 0.0) pos += static_cast<double>(kDelayBufSize);
      const int p0 = static_cast<int>(pos);
      const int p1 = (p0 + 1) & kDelayBufMask;
      const double frac = pos - static_cast<double>(p0);
      return buf[p0 & kDelayBufMask] * (1.0 - frac) + buf[p1] * frac;
    }

    void write(double v) noexcept {
      buf[writePos] = v;
      writePos = (writePos + 1) & kDelayBufMask;
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

  // 入力 diffuser 4 段
  std::array<InputAllpass, kNumInputAp> mInputAP;

  // 4 並列 delay lines
  std::array<DelayLine, kNumLines> mDelays;

  // 各 line の damping LP state。各 line 個別の cutoff (= base × ±3 semitone 範囲)
  // を持たせて HF 減衰速度をずらし、4 mode が "一斉に同じ tail" として残らないように。
  std::array<double, kNumLines> mDampStates{};
  std::array<double, kNumLines> mDampCoeffs{};

  // Tank 内 modulation LFO 状態 (sin、各 line 独立)
  std::array<double, kNumLines> mLfoPhase{};
  std::array<double, kNumLines> mLfoInc{};

  // 出力 low shelf (180Hz @ -1dB asymptote)。Hall は tank が長く低域が溜まり
  // やすいので、出力直前で軽く低域を引いて bass のたまりを抑える。1-pole LP
  // 経由で低域成分を抽出し、(G - 1) を掛けて入力に足し戻す形 (= 1-pole low shelf)。
  double mShelfStateL{0.0};
  double mShelfStateR{0.0};
  double mShelfCoeff{1.0};

  double mTargetRt60{4.0};
  double mDecay{0.7};
  double mDiffusion{1.0};
};

}  // namespace dx10::dsp

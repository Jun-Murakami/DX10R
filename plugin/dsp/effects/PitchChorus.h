// 80 年代ラックハーモナイザー系の pitch-shift detune chorus (Synth-80, "Pitch Chorus")。
//
// Time-domain 2-tap pitch shifter を 2 voice 並列で動かし、各 voice 後段に
// 固定 delay と feedback を加えて、micro-pitch detune slap chorus を作る。
// LFO で delay 周期を揺らす一般的な BBD 系 chorus とは別原理で、ピッチを
// 微小にずらして「2 人の声がわずかに音程違いで混じる」効果を物理的に再現する。
//
// 実装は H910 系の simple triangle crossfade pitch shifter。各 tap は
// kWindowSamples ごとに reset し、固定 reference (= windowSamples / 2) へ
// snap する。reset 時の splice 累積ドリフト問題は固定 nominal で発生しない。
//
// 信号フロー (1 voice):
//
//   in ──┐
//        ├─→ + ──→ pitch shifter ──→ extra delay ──→ wet
//        │   ↑           |                              │
//        │   feedback ←──┘  (1-pole LP @ ~ 6 kHz)       │
//        │                                              │
//        └────────────── (dry path) ────────────────── │
//                                                      ▼
//                                          mix (dry / wet)
//
// 2 voice の出力は L/R に振り分け (Voice A → L、Voice B → R) ステレオ化する。
// dry は元の L/R をそのまま通す。
//
// パラメータ (EffectBase::setParam の paramIdx → 解釈):
//   0: Pitch A    -50..+50 cent (default +9)
//   1: Pitch B    -50..+50 cent (default -9)
//   2: Delay A    0..500 ms (default 0)
//   3: Delay B    0..500 ms (default 25)
//   4: Mod Depth  0..1 (read tap LFO 揺らし量、最大 ±2 ms)
//   5: Mod Rate   0.05..6 Hz, log
//   6: Feedback   0..0.7 (1-pole LP @ ~6 kHz on feedback path)
//   7: Mix        0..1 (dry/wet)

#pragma once

#include "EffectBase.h"

#include <array>
#include <vector>

namespace dx10::dsp {

class PitchChorus final : public EffectBase {
 public:
  // Crossfade window 長 (sample)。@ 48 kHz で 2048 サンプル ≒ 43 ms。これより
  // 短いと crossfade あたりのドリフト量が減って glitch が目立ちやすく、長いと
  // 高 pitch ratio で読出位置が奪い合いになる。
  static constexpr int kWindowSamples = 2048;

  PitchChorus() = default;

  void prepare(double sampleRate) override;
  void reset() override;
  void setParam(int paramIdx, double value01) override;
  void process(double& l, double& r) noexcept override;

 private:
  /** Pitch shifter 1 系統 (= 1 voice 分の delay buffer + 2 read tap)。 */
  struct Voice {
    // Delay buffer (prepare で sampleRate 依存サイズに resize)。
    // 必要長 = max delay (500 ms) + window (43 ms) + mod depth (2 ms) + 余裕。
    // SR=192 kHz でも 500 ms = 96000 sample なので 144 K で確保。
    std::vector<double> buf;
    int writePos{0};

    // 2 read tap の状態。各 tap は windowSamples ごとに 1 度 reset (= jump back)
    // し、tap 同士は 0.5 位相ずれて crossfade で sum 1.0 を保つ。
    std::array<double, 2> tapPhase{{0.0, 0.5}};
    std::array<double, 2> tapSamplesBehind{{0.0, 0.0}};

    // Pitch shift ratio (1.0 = unison)。setParam で更新。
    double pitchRatio{1.0};

    // 追加 fixed delay (Delay A/B) のサンプル数。pitch shifter 出力に乗せる。
    double extraDelaySamples{0.0};

    // Mod LFO の phase (0..2π)。各 voice は π オフセットで開始 (decorrelate)。
    double modPhase{0.0};

    // Feedback state (前サンプルの wet 出力)。
    double fbState{0.0};

    // Feedback path に挿す 1-pole low-pass の状態。pitch shifter 出力を
    // 再帰させるとき、interp 残渣がループごとに積み上がってキツい音色に
    // なりやすい。固定 cutoff (~ 6 kHz) で軽く切ると、shimmer 系の長時間
    // feedback でも musical に聞こえる。
    double fbLpState{0.0};
  };

  /** 4-point cubic Hermite (Catmull-Rom) で delay buffer から value を読む。 */
  double readDelay(const std::vector<double>& buf, int writePos,
                   double samplesAgo) const noexcept;

  /** 1 voice 分の pitch shifter + delay + feedback を 1 サンプル進める。 */
  double processVoice(Voice& v, double in, double modSamples) noexcept;

  double mSampleRate{48000.0};

  std::array<Voice, 2> mVoices;

  // パラメータ実値 (0..1 から変換済み)。
  double mPitchACents{9.0};
  double mPitchBCents{-9.0};
  double mDelayAMs{0.0};
  double mDelayBMs{25.0};
  double mModDepth{0.0};       // 0..1
  double mModRateHz{0.5};
  double mFeedback{0.0};       // 0..0.7
  double mMix{0.5};

  double mModInc{0.0};         // 1 サンプルあたりの phase 増分 (rad)
  double mModDepthSamples{0.0}; // 最大スイープ振幅 (sample)

  // Feedback 経路の 1-pole low-pass 係数 (prepare で固定 cutoff から決まる)。
  double mFbLpCoeff{0.0};
};

}  // namespace dx10::dsp

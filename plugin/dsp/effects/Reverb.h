// Synth-80 Reverb (multi-topology dispatcher)。
//
// 1 effect スロットの内側で 3 つの異なる reverb topology を切り替える。Mode 切替時は
// クロスフェードでクリックなしに遷移する。
// (2026-06-12 perf D2) 旧実装は 3 core 常時 process (= 切替で即フル tail、CPU は
// 単一 core の ~3 倍) だったが、gain 0 の core をスキップする方式に変更。収束後は
// アクティブ core 単独で、mode 切替時のみ 50ms クロスフェードの間 2 core が並走
// する。新 core は再活性化時に reset され、ゼロから build-up しつつ fade-in する
// (= 通常のリバーブ切替の立ち上がり。凍結した古い tail のゴースト再生はしない)。
//
// 各 mode の topology:
//   - Hall  = HallCore (4×4 FDN with Hadamard mixing) → smooth で密、長い tail
//   - Plate = PlateCore (Lexicon 224 / Dattorro 1997 figure-eight tank) → 金属的、密度高
//   - Room  = RoomCore (Gardner-style 並列 L/R loop) → 離散反射が早い、短い tail
//
// 信号経路:
//
//   in (mono化) → Pre-delay → Pre-LP (bandwidth 9kHz)
//                         ↓
//             ┌── Hall  core ──┐
//             ├── Plate core ──┤  (gain > 0 の core のみ process)
//             └── Room  core ──┘
//                         ↓ クロスフェード Σ wet[i] × gain[i]
//                         ↓ Tone LP (output)
//                         ↓ M/S Width
//                         ↓ dry/wet Mix (equal-power)
//
// パラメータ (EffectBase::setParam の paramIdx → 解釈):
//   0: Mode      (0=Hall, 1=Plate, 2=Room)
//   1: Pre-Delay (0..1 → 0..200 ms linear)
//   2: Decay     (0..1 → RT60 0.3..10 sec、log map)。各 core で個別に g が計算され、
//                 mean loop が違ってもユーザ指定秒数を維持する。
//   3: Damping   (0..1 → tank LP cutoff 16k..200 Hz log)
//   4: Diffusion (0..1 → input AP coeff scale 0..1)
//   5: Tone      (0..1 → wet 出力 LP cutoff 1k..16k Hz log、低=暗、高=明)
//   6: Width     (0..1 → 0..2 倍 M/S サイド)
//   7: Mix       (0..1 → dry/wet equal-power)

#pragma once

#include "EffectBase.h"
#include "HallCore.h"
#include "PlateCore.h"
#include "ReverbCore.h"
#include "RoomCore.h"

#include <array>

namespace dx10::dsp {

class Reverb final : public EffectBase {
 public:
  // Pre-delay buffer (200ms @ 192k = 38400 sample → 65536 余裕)。
  static constexpr int kPredelayBufSize = 65536;
  static constexpr int kPredelayMask = kPredelayBufSize - 1;

  static constexpr int kNumModes = 3;

  void prepare(double sampleRate) override;
  void reset() override;
  void setParam(int paramIdx, double value01) override;
  void process(double& l, double& r) noexcept override;

 private:
  void recomputeCrossfadeStep() noexcept;
  void retargetGainsForMode(int mode) noexcept;
  ReverbCore& core(int idx) noexcept;

  double mSampleRate{48000.0};

  // ===== Pre-delay (mode 共通、入力チェイン) =====
  std::array<double, kPredelayBufSize> mPredelayBuf{};
  int mPredelayWrite{0};
  double mPredelaySamples{0.0};

  // ===== Pre-LP (mode 共通、入力チェイン、固定 cutoff 9kHz) =====
  double mPreLpState{0.0};
  double mPreLpCoeff{1.0};

  // ===== 3 つの topology core =====
  HallCore mHall;
  PlateCore mPlate;
  RoomCore mRoom;

  // ===== Mode crossfade =====
  // 各 core の現在 / 目標 gain。新 mode 選択時は target を [0,1,0] のように
  // 切替えて、毎 sample ステップずつ近づける。50ms で 0→1 に達する step を使う。
  // 既定 mode (Plate) と gain は一致させること。旧初期値は gains={Hall} なのに
  // mActiveMode=Plate で、core スキップ導入後は「表示 Plate / 音 Hall」になる。
  std::array<double, kNumModes> mGains{0.0, 1.0, 0.0};
  std::array<double, kNumModes> mTargetGains{0.0, 1.0, 0.0};
  double mGainStep{1.0};
  int mActiveMode{1};  // default Plate

  // ===== Tone LP (mode 共通、wet 出力) =====
  double mToneStateL{0.0};
  double mToneStateR{0.0};
  double mToneCoeff{1.0};

  // ===== Width / Mix (mode 共通、出力) =====
  double mWidth{1.0};
  double mMix{0.0};
  double mDryGain{1.0};
  double mWetGain{0.0};
};

}  // namespace dx10::dsp

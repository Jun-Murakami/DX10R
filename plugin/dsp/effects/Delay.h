// Stereo digital delay (Synth-80, "Delay")。
//
// 機能:
//   - Time mode: Sync (host BPM 追従、9 段の音符 division) / Free (1..2000 ms)
//   - Stereo mode: Mono (L=R 同 delay) / Ping-Pong (L→R→L→R で跳ね返り)
//   - Feedback (0..95%) + Tone (1-pole LP cutoff、analog tape 風 damping)
//   - Mix (dry/wet)
//
// パラメータ (EffectBase::setParam の paramIdx → 解釈):
//   0: Time Mode    (0=Sync, 1=Free)
//   1: Time Sync    (0..11 → 1/64 〜 1/2 の 12 段)
//   2: Time Free    (0..1 → 1..2000 ms, log)
//   3: Stereo Mode  (0=Mono, 1=Ping-Pong)
//   4: Feedback     (0..1 → 0..0.95)
//   5: Tone         (0..1 → LP cutoff 500..18000 Hz, log。0=暗い tape feel、1=clean)
//   6: Width        (0..1 → 0..2 倍の M/S サイド成分スケール)
//   7: Mix          (0..1)
//
// 注: param 1 と 2 は両方を IParam として保持し、param 0 (Time Mode) でどちらが
// 有効になるかが決まる。UI 側は visibleWhen で片方だけ表示する。値はそれぞれ独立に
// 保持されるので、Sync ↔ Free 切替で前回値が保たれる。

#pragma once

#include "EffectBase.h"

#include <vector>

namespace dx10::dsp {

class Delay final : public EffectBase {
 public:
  // Free time の上限 (ms)。setParam の logMap range と同期させる。
  static constexpr double kMaxFreeMs = 2000.0;
  // Sync mode の音符 division 数 (1/64 〜 1/2 までの 12 段)。
  static constexpr int kNumSyncDivisions = 12;

  void prepare(double sampleRate) override;
  void reset() override;
  void setParam(int paramIdx, double value01) override;
  void setTempo(double bpm) noexcept override;
  void process(double& l, double& r) noexcept override;

 private:
  /** sync div / free ms / tempo / sr の現状から mTimeSamples を再計算。
   *  glide=true なら新旧タップの crossfade で滑らかに切替 (クリック回避)、
   *  false なら即時差し替え (prepare 初期化用)。 */
  void recomputeTimeSamples(bool glide = true) noexcept;

  /** delay buffer から fractional read (線形補間)。 */
  double readDelay(const std::vector<double>& buf, double samplesAgo) const noexcept;

  double mSampleRate{48000.0};
  double mTempoBpm{120.0};

  // Buffer は prepare(sampleRate) で必要長を確保 (kMaxFreeMs * sr * safety)。
  // 旧実装の固定 100000 sample だと 96/192 kHz で 2000 ms 仕様が silently clamp
  // していたので、SR ベースで動的に確保することで全 SR で仕様どおりに鳴る。
  std::vector<double> mBufL;
  std::vector<double> mBufR;
  int mBufSize{0};
  int mWritePos{0};

  // Tone (LP cutoff filter on feedback path) の state。
  double mToneStateL{0.0};
  double mToneStateR{0.0};
  double mToneCoeff{1.0};  // 0..1、1 で transparent (no LP)

  // パラメータ実値。
  int mTimeMode{0};        // 0=Sync, 1=Free
  int mSyncDivIdx{3};      // 1/8 がデフォルト
  double mFreeMs{350.0};
  int mStereoMode{0};      // 0=Mono, 1=Ping-Pong
  double mFeedback{0.0};
  double mMix{0.0};
  // Equal-power crossfade の事前計算済 gain。dry/wet が無相関 (delay tap は
  // dry と数百 ms ずれるので実質無相関) のため linear だと mix=0.5 で約 -3 dB
  // の音量谷ができる。cos/sin で gain² 和を mix 全域で 1 に固定し、谷を回避。
  // setParam case 7 で更新、prepare/reset でも mMix から再計算する。
  double mDryGain{1.0};
  double mWetGain{0.0};
  double mWidth{1.0};      // M/S 倍率 (0=mono, 1=normal, 2=wider)

  // 計算済み delay 長 (sample)。recomputeTimeSamples で更新。
  double mTimeSamples{0.0};

  // タイム切替時のクリーン crossfade 用。タイムが変わると旧オフセット
  // (mOldTimeSamples) から新オフセット (mTimeSamples) へ equal-power で
  // 短時間フェードし、読み出し位置の不連続 (= クリック) を消す。テープ式の
  // ピッチグライドは起こさない。mXfadePos >= mXfadeLen のときフェード非活性。
  double mOldTimeSamples{0.0};
  int mXfadePos{0};
  int mXfadeLen{0};
};

}  // namespace dx10::dsp

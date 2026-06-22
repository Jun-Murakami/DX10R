// Effect プラグインの基底インターフェース (Synth-80)。
//
// 5 スロット直列のエフェクトチェイン (EffectsRack) で各スロットが保持する
// effect 実装のための共通 API。各スロットは 1 enum (EffectType) + 8 generic
// float param (0..1) という IParam 設計と整合する。
//
// 設計方針:
//   - param は IParam 上は 0..1 linear で固定。effect 実装側が任意のレンジ
//     (Hz, ms, %, enum) に変換する。UI 側は effect ごとの metadata を参照して
//     表示する。
//   - 全 effect 実装はオーディオスレッドで動くので alloc / lock / file I/O 禁止。
//   - 状態は member variable として持ち、prepare(sampleRate) で内部 SR-依存
//     係数を計算する。
//
// プラグイン本体側の EffectsRack は各スロットに 4 effect (Off は no-op)
// インスタンスを pre-instantiate しておき、type 切替で switch dispatch する。
// std::variant や placement new のような alloc を避けるために、type 切替の
// たびに該当 effect の reset() を呼んで内部状態だけクリアする運用。

#pragma once

#include <cstdint>

namespace dx10::dsp {

/** スロット 1 つに割り当てられるエフェクト種別。 */
enum class EffectType : std::uint8_t {
  Off = 0,
  SynthChorus,    // JUNO-60 / JUNO-106 風 BBD chorus
  StudioChorus,   // Roland Dimension D 風 2-BBD matrix chorus
  DigitalChorus,    // 単純な digital chorus
  Delay,          // Tempo sync / free 切替 + Mono / Ping-Pong
  Phaser,         // Cascaded all-pass phaser
  Flanger,        // Modulated short-delay flanger
  EQ3Band,        // DJ-style 3-band EQ (low shelf / mid peak / high shelf)
  Compressor,     // Soft-knee feedforward compressor with auto makeup
  Distortion,     // Multi-algorithm distortion (Clip / Overdrive / Waveshaper / Tube / Tape)
  Reverb,         // Dattorro plate-style algorithmic reverb (Hall / Plate / Room)
  // 80 年代ラックハーモナイザー系の pitch-shift detune chorus。Time-domain
  // 2-tap pitch shifter + LFO mod + 固定 delay + feedback で MicroPitch 系の
  // detune slap chorus を作る。LFO で delay 周期を揺らす DigitalChorus とは別原理。
  PitchChorus,
  NumTypes
};

/** 1 effect スロットあたりの汎用 float param 数。 */
constexpr int kEffectParamsPerSlot = 8;

/** チェインのスロット数。仕様 (5 系統)。 */
constexpr int kEffectSlotCount = 5;

/**
 * Effect 実装の共通インターフェース。EffectsRack はスロットごとに各派生型を
 * pre-instantiate する。
 */
class EffectBase {
 public:
  virtual ~EffectBase() = default;

  /** sampleRate に応じた内部係数 (delay buffer 長など) を再計算する。 */
  virtual void prepare(double sampleRate) = 0;

  /** 内部状態 (delay buffer / 履歴) をクリアする。 */
  virtual void reset() = 0;

  /**
   * 0..1 normalized な param を effect 内部の実値に変換して反映する。
   * paramIdx は 0..kEffectParamsPerSlot-1。
   */
  virtual void setParam(int paramIdx, double value01) = 0;

  /**
   * Host BPM を effect に通知する。Tempo sync delay などで使う。default no-op
   * なので tempo を見ない effect は override 不要。Synth80::ProcessBlock 開始時に
   * EffectsRack 経由で per-block 呼ばれる (control rate)。
   */
  virtual void setTempo(double bpm) noexcept { (void)bpm; }

  /** L/R 1 サンプルを in-place で処理する。 */
  virtual void process(double& l, double& r) noexcept = 0;
};

}  // namespace dx10::dsp

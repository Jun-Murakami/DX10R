// 5 スロット直列のエフェクトチェイン (Synth-80)。
//
// 各スロットは EffectType (Off / SynthChorus / StudioChorus / DigitalChorus / ...)
// を持ち、process() は 5 スロットを上流→下流 (slot0 → slot4) で順に通す。
// "Off" のスロットは pass-through (処理スキップ)。
//
// alloc-free 方針:
//   各スロットは全 effect 実装を pre-instantiate しておく。type 切替は単に
//   enum を変えるだけで、heap allocation や virtual table reload は発生しない。
//   将来 Reverb / Delay 等を増やすときは EffectSlot に新メンバを足し、process()
//   の switch 分岐を 1 ケース増やす。
//
// パラメータ伝搬:
//   Synth80::OnParamChange からの呼び出しで、(slotIdx, paramIdx, value01) を
//   受けて該当 slot の active effect だけに setParam を流す。type 切替時は
//   全 8 param を新 type に再適用する (caller 責任)。

#pragma once

#include "Compressor.h"
#include "Delay.h"
#include "Distortion.h"
#include "EffectBase.h"
#include "EQ3Band.h"
#include "Flanger.h"
#include "DigitalChorus.h"
#include "Phaser.h"
#include "PitchChorus.h"
#include "Reverb.h"
#include "StudioChorus.h"
#include "SynthChorus.h"

#include <array>
#include <atomic>

namespace dx10::dsp {

class EffectSlot {
 public:
  void prepare(double sampleRate);
  /** 全 11 種の effect 実装をゼロクリアする。Delay / PitchChorus / Reverb の
   *  大容量バッファまで毎回 memset するので、prepare / OnReset の初期化時のみ使う。
   *  patch 切替のような高頻度経路では resetActive() を使うこと (下記参照)。 */
  void reset();
  /** 現在アクティブな 1 種の effect だけをゼロクリアする (Off なら no-op)。
   *  patch 切替 / type 切替 / bypass-on のたびに 11 種すべて (= 数十 MB) を memset
   *  すると、UI thread のメモリ帯域飽和 + L3 cache 追い出しで同プロセスの audio
   *  thread が処理落ちし CPU メータが瞬間 100% に跳ねる。アクティブな effect 以外は
   *  process() されないので tail も持たず、再アクティブ化時に setType→resetActive で
   *  クリーンになるため、ここでアクティブ分だけ消せば十分。 */
  void resetActive() noexcept;

  // mType は UI thread (Synth80::applyEffectParamChange の type 切替) と audio
  // thread (process / current) の両方からアクセスされるため atomic で扱う。
  // 並行 type 切替時に audio が古い type の effect を一瞬呼び続ける可能性は
  // 残るが、呼び出し元で soft-mute (~10ms) を被せているため聴感上問題ない。
  // ここで保証したいのは「データ競合 (UB) を避ける」こと。
  void setType(EffectType type) noexcept;
  EffectType type() const noexcept {
    return mType.load(std::memory_order_acquire);
  }

  /** 0..1 normalized param を active effect に流す。 */
  void setParam(int paramIdx, double value01);

  /** Tempo (BPM) を active effect に通知する。 */
  void setTempo(double bpm) noexcept;

  /** L/R 1 サンプルを type に応じて in-place 処理する。Off なら no-op。 */
  void process(double& l, double& r) noexcept;

 private:
  /** type → 対応する EffectBase ポインタ (Off は nullptr)。setType / prepare 時のみ呼ぶ。 */
  EffectBase* resolve(EffectType type) noexcept;
  /** 現在 type に対応する EffectBase ポインタを返す。Off なら nullptr。
   *  (2026-06-12 perf A5) per-sample の enum atomic load + switch を避けるため、
   *  setType 時に解決済みポインタを atomic にキャッシュして load するだけにした。
   *  effect 実装は本クラスのメンバ (アドレス不変) なのでポインタは常に有効。 */
  EffectBase* current() noexcept {
    return mCurrentFx.load(std::memory_order_acquire);
  }

  std::atomic<EffectType> mType{EffectType::Off};
  std::atomic<EffectBase*> mCurrentFx{nullptr};

  // pre-instantiate された effect 実装。type 切替時の heap alloc 回避目的。
  DigitalChorus mDigitalChorus;
  SynthChorus mSynthChorus;
  StudioChorus mStudioChorus;
  Delay mDelay;
  Phaser mPhaser;
  Flanger mFlanger;
  EQ3Band mEQ3Band;
  Compressor mCompressor;
  Distortion mDistortion;
  Reverb mReverb;
  PitchChorus mPitchChorus;
};

class EffectsRack {
 public:
  void prepare(double sampleRate);
  /** 全スロットの全 11 種を reset (= 重い)。初期化時のみ。 */
  void reset();
  /** 全スロットの「アクティブな effect だけ」reset (= 軽い)。patch 切替で使う。 */
  void resetActive() noexcept;

  void setSlotType(int slot, EffectType type) noexcept;
  void setSlotParam(int slot, int paramIdx, double value01);
  /** 全 8 param を一括で active effect に再適用する (type 切替時に使う)。 */
  void reapplyAllParams(int slot,
                        const std::array<double, kEffectParamsPerSlot>& values);

  /** スロット個別の bypass。true で process が pass-through になる。 */
  void setSlotBypassed(int slot, bool bypassed) noexcept;
  bool slotBypassed(int slot) const noexcept;

  /** Host BPM を全スロットの active effect に伝搬する。Synth80::ProcessBlock
   *  の per-block 呼び出し想定 (control rate)。 */
  void setTempo(double bpm) noexcept;

  /** 5 スロットを上流→下流に直列処理する。bypass slot はスキップ。 */
  void process(double& l, double& r) noexcept;

  EffectSlot& slot(int idx) noexcept { return mSlots[idx]; }

 private:
  std::array<EffectSlot, kEffectSlotCount> mSlots{};
  std::array<bool, kEffectSlotCount> mBypassed{};  // すべて false で初期化
};

}  // namespace dx10::dsp

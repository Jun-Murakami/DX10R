#include "EffectsRack.h"

namespace dx10::dsp {

// ===== EffectSlot ==========================================================

void EffectSlot::prepare(double sampleRate) {
  // 初期 type (Off=nullptr 含む) のポインタキャッシュを確定させる (perf A5)。
  // effect 実装は本クラスのメンバなので prepare 後もアドレスは不変。
  mCurrentFx.store(resolve(mType.load(std::memory_order_acquire)),
                   std::memory_order_release);
  mDigitalChorus.prepare(sampleRate);
  mSynthChorus.prepare(sampleRate);
  mStudioChorus.prepare(sampleRate);
  mDelay.prepare(sampleRate);
  mPhaser.prepare(sampleRate);
  mFlanger.prepare(sampleRate);
  mEQ3Band.prepare(sampleRate);
  mCompressor.prepare(sampleRate);
  mDistortion.prepare(sampleRate);
  mReverb.prepare(sampleRate);
  mPitchChorus.prepare(sampleRate);
}

void EffectSlot::reset() {
  mDigitalChorus.reset();
  mSynthChorus.reset();
  mStudioChorus.reset();
  mDelay.reset();
  mPhaser.reset();
  mFlanger.reset();
  mEQ3Band.reset();
  mCompressor.reset();
  mDistortion.reset();
  mReverb.reset();
  mPitchChorus.reset();
}

void EffectSlot::resetActive() noexcept {
  if (auto* fx = current()) fx->reset();
}

void EffectSlot::setType(EffectType type) noexcept {
  mType.store(type, std::memory_order_release);
  // 解決済みポインタをキャッシュ (perf A5)。process / setParam は current() で
  // このポインタを load するだけになる。
  mCurrentFx.store(resolve(type), std::memory_order_release);
}

EffectBase* EffectSlot::resolve(EffectType type) noexcept {
  switch (type) {
    case EffectType::DigitalChorus: return &mDigitalChorus;
    case EffectType::SynthChorus: return &mSynthChorus;
    case EffectType::StudioChorus: return &mStudioChorus;
    case EffectType::Delay: return &mDelay;
    case EffectType::Phaser: return &mPhaser;
    case EffectType::Flanger: return &mFlanger;
    case EffectType::EQ3Band: return &mEQ3Band;
    case EffectType::Compressor: return &mCompressor;
    case EffectType::Distortion: return &mDistortion;
    case EffectType::Reverb: return &mReverb;
    case EffectType::PitchChorus: return &mPitchChorus;
    case EffectType::Off:
    default: return nullptr;
  }
}

void EffectSlot::setParam(int paramIdx, double value01) {
  if (paramIdx < 0 || paramIdx >= kEffectParamsPerSlot) return;
  if (auto* fx = current()) fx->setParam(paramIdx, value01);
}

void EffectSlot::setTempo(double bpm) noexcept {
  if (auto* fx = current()) fx->setTempo(bpm);
}

void EffectSlot::process(double& l, double& r) noexcept {
  if (auto* fx = current()) fx->process(l, r);
}

// ===== EffectsRack =========================================================

void EffectsRack::prepare(double sampleRate) {
  for (auto& s : mSlots) s.prepare(sampleRate);
}

void EffectsRack::reset() {
  for (auto& s : mSlots) s.reset();
}

void EffectsRack::resetActive() noexcept {
  for (auto& s : mSlots) s.resetActive();
}

void EffectsRack::setSlotType(int slot, EffectType type) noexcept {
  if (slot < 0 || slot >= kEffectSlotCount) return;
  mSlots[slot].setType(type);
}

void EffectsRack::setSlotParam(int slot, int paramIdx, double value01) {
  if (slot < 0 || slot >= kEffectSlotCount) return;
  mSlots[slot].setParam(paramIdx, value01);
}

void EffectsRack::reapplyAllParams(
    int slot, const std::array<double, kEffectParamsPerSlot>& values) {
  if (slot < 0 || slot >= kEffectSlotCount) return;
  for (int i = 0; i < kEffectParamsPerSlot; ++i) {
    mSlots[slot].setParam(i, values[i]);
  }
}

void EffectsRack::setSlotBypassed(int slot, bool bypassed) noexcept {
  if (slot < 0 || slot >= kEffectSlotCount) return;
  // false→true (bypass ON) 遷移で slot のアクティブ effect の内部状態をクリアする。
  // bypass 中は process() がスキップされるので、Reverb tank / Delay buffer 等に
  // 直前の音色の残留が凍結されたまま残る。Effect Chain Lock を効かせたまま別パッチ
  // へ切替えて鳴らし、bypass を戻すと、その残留状態から旧パッチ音が再生されて
  // 「一瞬前の音色が漏れる」現象になる。bypass-on 時点で reset しておけば、
  // 再有効化時はクリーン状態から処理が始まる。resetActive は active effect の buffer
  // ゼロクリアのみで alloc/lock 無しなので audio thread から呼んで安全。
  if (bypassed && !mBypassed[slot]) {
    mSlots[slot].resetActive();  // アクティブな effect だけクリアすれば十分 (軽量)
  }
  mBypassed[slot] = bypassed;
}

bool EffectsRack::slotBypassed(int slot) const noexcept {
  if (slot < 0 || slot >= kEffectSlotCount) return false;
  return mBypassed[slot];
}

void EffectsRack::setTempo(double bpm) noexcept {
  for (auto& s : mSlots) s.setTempo(bpm);
}

void EffectsRack::process(double& l, double& r) noexcept {
  for (int i = 0; i < kEffectSlotCount; ++i) {
    if (mBypassed[i]) continue;  // bypass なら effect 通さず pass-through
    mSlots[i].process(l, r);
  }
}

}  // namespace dx10::dsp

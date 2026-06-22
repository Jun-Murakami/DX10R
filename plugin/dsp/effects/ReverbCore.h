// Synth-80 Reverb の topology バリアント共通 interface。
//
// 外側 dispatcher (Reverb.{h,cpp}) が持つ責務:
//   - Mode 切替と 50ms linear crossfade
//   - Pre-Delay buffer (mode 共通、入力チェイン)
//   - Pre-LP (bandwidth、固定 9kHz、入力チェイン)
//   - Tone LP (出力チェイン、wet 共通)
//   - M/S Width
//   - Equal-power dry/wet Mix
//
// 各 core が持つ責務:
//   - 自分の tank topology (delay line / allpass / feedback matrix)
//   - feedback path 内の damping LP (= mode 別キャラクタの中核)
//   - input diffuser (mode ごとに異なる)
//   - mono 入力 → stereo wet 出力
//
// Mode 切替時の挙動:
//   dispatcher は 3 core を **常時並行で process()** する。switching 時は出力
//   ミックス比だけを 50ms で線形にランプ。これにより、新 core で reverb tail を
//   ゼロから build-up し直す "buildup 中の薄い時間" を排除できる。
//   CPU コストは ~3× だが、reverb は luxury 系なので許容。

#pragma once

namespace dx10::dsp {

/** Reverb topology バリアントの共通 interface。 */
class ReverbCore {
 public:
  virtual ~ReverbCore() = default;

  /** sampleRate に応じた delay 長などを再計算する。reset() も呼ぶ。 */
  virtual void prepare(double sampleRate) = 0;

  /** 内部状態 (delay buffer / LP state) をクリアする。 */
  virtual void reset() = 0;

  /**
   * 目標 RT60 (sec) を更新する。core 内部の mean loop 長から feedback gain g を
   *   g = 10^(-3 × mean_loop_sec / RT60)
   * で逆算し、Mode 倍率を考慮した上でユーザ指定秒数を維持する。
   */
  virtual void setTargetRt60(double seconds) = 0;

  /** 0=bright (no LP) ... 1=dark (heavy LP)。feedback 内 damping LP の cutoff。 */
  virtual void setDamping01(double v) = 0;

  /** 0=拡散ゼロ (input AP bypass 相当) ... 1=core 標準値の input diffuser 強度。 */
  virtual void setDiffusion01(double v) = 0;

  /**
   * mono 入力 (pre-delay 後 / pre-LP 後) を受けて stereo wet を返す。
   * tank 内 modulation LFO は core 内部で進める。
   */
  virtual void process(double xMono, double& wetL, double& wetR) noexcept = 0;
};

}  // namespace dx10::dsp

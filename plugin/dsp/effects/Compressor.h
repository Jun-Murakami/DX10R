// Soft-knee feedforward compressor with 4 character modes (Synth-80, "Compressor")。
//
// ZeroComp の 4 モードキャラクター (VCA / OPTO / FET / Vari-MU) をシンセ用に
// 移植。Auto makeup gain で「踏み込み量に応じてレベルが自動補正」される設計
// なので、シンセ側で Threshold / Ratio を派手に動かしても破綻しにくい。
//
// 信号フロー:
//   in (L,R) → [stereo-linked peak detect] → [static GR curve (Giannoulis 2012)]
//          → [attack/release smoothing on GR-dB] → [Mode 別 envelope 加工]
//          → [linear gain × dry] → [Mode 別 saturation] → [auto makeup]
//          → [dry/wet mix] → out
//
// Mode 別の特徴:
//   - Clean (VCA):  シンプル feedforward。色付けなし、attack/release そのまま。
//   - Opto:         dual envelope (slow = release × 5x) + LDR 熱メモリ近似 (1秒
//                   蓄熱 / 3秒冷却)。深い GR が続くほど slow release が sticky に
//                   なり LA-2A 的な「尾を引く」挙動。
//   - FET:          1176 風の非対称ソフトクリップ (signal 経路後段)、奇数次の grit。
//   - Vari-Mu:      knee 内部で +12 dB 拡張 + 軽い 2 次ベンド saturation。
//                   レシオが GR と共に徐々に強くなる感覚 + 偶数次倍音。
//
// パラメータ (EffectBase::setParam の paramIdx → 解釈):
//   0: Threshold (-60..0 dB linear)
//   1: Ratio     (1..20、square curve)
//   2: Attack    (0.1..200 ms log)
//   3: Release   (5..1000 ms log)
//   4: Mix       (0..1 dry/wet)
//   5: Mode      (0=Clean, 1=Opto, 2=FET, 3=Vari-Mu)
//   6: Knee      (0..24 dB linear、Vari-Mu モードでは内部で +12 dB 加算)
//   7: 未使用

#pragma once

#include "EffectBase.h"

namespace dx10::dsp {

class Compressor final : public EffectBase {
 public:
  enum class Mode : int { Clean = 0, Opto, FET, VariMu };

  void prepare(double sampleRate) override;
  void reset() override;
  void setParam(int paramIdx, double value01) override;
  void process(double& l, double& r) noexcept override;

 private:
  /** attack/release/slow/sticky 係数を mAttackMs / mReleaseMs / mSampleRate から
   *  全部再計算する。Ratio 変更時は不要 (slope だけ再計算)。 */
  void updateAttackReleaseCoeffs() noexcept;
  /** LDR 熱メモリの蓄熱 / 冷却係数を sampleRate から再計算。 */
  void updateLdrCoeffs() noexcept;
  /** 入力 dB → 静的 GR (dB, 正値 = どれだけ抑制するか)。 */
  double computeGainReductionDb(double inputDb, double kneeForCurve) const noexcept;

  double mSampleRate{48000.0};

  // 静的カーブのパラメータ。
  double mThresholdDb{-12.0};
  double mRatio{4.0};
  double mSlope{0.75};        // 1 - 1/ratio (precomputed)
  double mKneeDb{6.0};
  Mode mMode{Mode::Clean};

  // 時間定数 (ms 値 + sr 依存の coeff)。
  double mAttackMs{10.0};
  double mReleaseMs{100.0};
  double mAttackCoeff{0.0};
  double mReleaseCoeff{0.0};
  double mReleaseCoeffSlow{0.0};    // Opto: release × 5
  double mReleaseCoeffSticky{0.0};  // Opto: release × 15 (LDR hot 時)
  double mLdrHeatUpCoeff{0.0};      // 1 秒で蓄熱
  double mLdrCoolCoeff{0.0};        // 3 秒で冷却

  // 動的状態。
  double mEnvelopeDb{0.0};       // 平滑化済み GR-dB (正値)
  double mEnvelopeDbSlow{0.0};   // Opto: 2 本目の slow envelope
  double mLdrHeat{0.0};          // 0..1 (cold..hot)、Opto LDR 熱メモリ

  // Mix。
  double mMix{1.0};
};

}  // namespace dx10::dsp

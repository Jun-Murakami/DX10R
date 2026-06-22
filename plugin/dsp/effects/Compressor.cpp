#include "Compressor.h"

#include <algorithm>
#include <cmath>

namespace dx10::dsp {

namespace {
// Auto makeup の調整係数。1.0 = full makeup、0.5 = half。
constexpr double kMakeupFactor = 0.5;
constexpr double kMakeupCapDb = 18.0;

// Vari-Mu モードで knee に内部加算する追加幅。レシオが GR と共に徐々に強くなる
// 感覚を生む (実機 Vari-Mu の "soft compression knee")。
constexpr double kVariMuExtraKneeDb = 12.0;

// 各 mode の saturation drive 量 (signal 経路後段)。Clean / Opto は 0。
constexpr double kFetDrive = 1.2;
constexpr double kVariMuDrive = 0.6;

// パラメータレンジ。
constexpr double kThresholdMinDb = -60.0;
constexpr double kThresholdMaxDb = 0.0;
constexpr double kRatioMin = 1.0;
constexpr double kRatioMax = 20.0;
constexpr double kAttackMinMs = 0.1;
constexpr double kAttackMaxMs = 200.0;
constexpr double kReleaseMinMs = 5.0;
constexpr double kReleaseMaxMs = 1000.0;
constexpr double kKneeMinDb = 0.0;
constexpr double kKneeMaxDb = 24.0;

// envelope log10 floor (-120 dBFS 相当)。
constexpr double kMinAbs = 1.0e-6;

double logMap(double v01, double min, double max) {
  v01 = std::clamp(v01, 0.0, 1.0);
  return min * std::pow(max / min, v01);
}

/** FET 風 (1176) の非対称ソフトクリップ。drive 0 で透過。 */
inline double fetSaturate(double x, double drive) noexcept {
  if (drive <= 0.0) return x;
  // 非対称バイアスを乗せて tanh。奇数次主体だが完全対称ではない grit。
  constexpr double asym = 0.08;
  const double y = std::tanh((x + asym) * drive) - std::tanh(asym * drive);
  return y / drive;
}

/** Vari-Mu / tube 風の柔らかい飽和。偶数次倍音を sign-preserving で。 */
inline double variMuSaturate(double x, double drive) noexcept {
  if (drive <= 0.0) return x;
  const double ax = std::abs(x);
  // x - a × sign(x) × x^2 で軽い 2 次ベンド (tube の "圧縮感")。
  return x - drive * 0.12 * std::copysign(ax * ax, x);
}
}  // namespace

void Compressor::updateAttackReleaseCoeffs() noexcept {
  const double tauA = mAttackMs * 0.001 * mSampleRate;
  const double tauR = mReleaseMs * 0.001 * mSampleRate;
  const double tauRSlow = tauR * 5.0;       // Opto cold 時の slow release
  const double tauRSticky = tauR * 15.0;    // Opto hot 時の sticky tail
  mAttackCoeff = std::exp(-1.0 / std::max(1.0, tauA));
  mReleaseCoeff = std::exp(-1.0 / std::max(1.0, tauR));
  mReleaseCoeffSlow = std::exp(-1.0 / std::max(1.0, tauRSlow));
  mReleaseCoeffSticky = std::exp(-1.0 / std::max(1.0, tauRSticky));
}

void Compressor::updateLdrCoeffs() noexcept {
  // 蓄熱 1 秒、冷却 3 秒。冷却の方が遅いので「最近の GR 履歴」が尾を引く。
  const double tauHeatUp = 1.0 * mSampleRate;
  const double tauCool = 3.0 * mSampleRate;
  mLdrHeatUpCoeff = std::exp(-1.0 / std::max(1.0, tauHeatUp));
  mLdrCoolCoeff = std::exp(-1.0 / std::max(1.0, tauCool));
}

void Compressor::prepare(double sampleRate) {
  mSampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
  updateAttackReleaseCoeffs();
  updateLdrCoeffs();
  reset();
}

void Compressor::reset() {
  mEnvelopeDb = 0.0;
  mEnvelopeDbSlow = 0.0;
  mLdrHeat = 0.0;
}

void Compressor::setParam(int paramIdx, double value01) {
  value01 = std::clamp(value01, 0.0, 1.0);
  switch (paramIdx) {
    case 0:  // Threshold
      mThresholdDb =
          kThresholdMinDb + value01 * (kThresholdMaxDb - kThresholdMinDb);
      break;
    case 1: {  // Ratio (square curve、低 ratio に分解能)
      const double t = value01 * value01;
      mRatio = kRatioMin + t * (kRatioMax - kRatioMin);
      mSlope = 1.0 - 1.0 / mRatio;
      break;
    }
    case 2:  // Attack
      mAttackMs = logMap(value01, kAttackMinMs, kAttackMaxMs);
      updateAttackReleaseCoeffs();
      break;
    case 3:  // Release
      mReleaseMs = logMap(value01, kReleaseMinMs, kReleaseMaxMs);
      updateAttackReleaseCoeffs();
      break;
    case 4:  // Mix
      mMix = value01;
      break;
    case 5: {  // Mode (4 段 enum)
      const int n = 4;
      const int idx = std::min(n - 1, std::max(0, static_cast<int>(std::round(value01 * (n - 1)))));
      mMode = static_cast<Mode>(idx);
      break;
    }
    case 6:  // Knee
      mKneeDb = kKneeMinDb + value01 * (kKneeMaxDb - kKneeMinDb);
      break;
    default:
      break;
  }
}

double Compressor::computeGainReductionDb(double inputDb,
                                          double kneeForCurve) const noexcept {
  // Giannoulis/Massberg/Reiss 2012 のソフトニーカーブ。GR は常に正値 (= 抑制量)。
  if (kneeForCurve <= 0.0) {
    if (inputDb <= mThresholdDb) return 0.0;
    return mSlope * (inputDb - mThresholdDb);
  }
  const double half = 0.5 * kneeForCurve;
  const double diff = inputDb - mThresholdDb;
  if (diff < -half) return 0.0;
  if (diff > half) return mSlope * diff;
  const double x = diff + half;  // 0..knee
  return mSlope * (x * x) / (2.0 * kneeForCurve);
}

void Compressor::process(double& l, double& r) noexcept {
  const double dryL = l;
  const double dryR = r;

  // 1. Stereo-linked peak detection (instantaneous、スムージング無し)。
  const double rectified = std::max(std::abs(dryL), std::abs(dryR));
  const double x = std::max(rectified, kMinAbs);
  const double xDb = 20.0 * std::log10(x);

  // 2. Vari-Mu mode は内部で knee +12 dB に拡張。
  const double kneeForCurve =
      (mMode == Mode::VariMu) ? (mKneeDb + kVariMuExtraKneeDb) : mKneeDb;
  const double targetGrDb = computeGainReductionDb(xDb, kneeForCurve);

  // 3. fast envelope smoothing (全 mode 共通)。GR が増える方向 = attack、
  //    減る方向 = release。targetGrDb は正値、envelope も正値で追従。
  const double coeff = (targetGrDb > mEnvelopeDb) ? mAttackCoeff : mReleaseCoeff;
  mEnvelopeDb = targetGrDb + (mEnvelopeDb - targetGrDb) * coeff;

  // 4. Mode 別 envelope 加工。
  double grApplied = mEnvelopeDb;
  if (mMode == Mode::Opto) {
    // LDR 熱メモリ: GR が深いほど熱量上昇 (heatTarget 0..1、18 dB で飽和)。
    const double heatTarget = std::min(1.0, mEnvelopeDb / 18.0);
    const double heatCoeff =
        (heatTarget > mLdrHeat) ? mLdrHeatUpCoeff : mLdrCoolCoeff;
    mLdrHeat = heatTarget + (mLdrHeat - heatTarget) * heatCoeff;

    // slow envelope の release coeff を熱量で補間: cold = slow、hot = sticky。
    const double slowReleaseCoeff =
        mReleaseCoeffSlow + (mReleaseCoeffSticky - mReleaseCoeffSlow) * mLdrHeat;
    const double coeffSlow =
        (targetGrDb > mEnvelopeDbSlow) ? mAttackCoeff : slowReleaseCoeff;
    mEnvelopeDbSlow =
        targetGrDb + (mEnvelopeDbSlow - targetGrDb) * coeffSlow;

    // 深い方の GR を採用 (slow が尾を引く)。
    grApplied = std::max(mEnvelopeDb, mEnvelopeDbSlow);
  }

  // 5. Auto makeup (factor 0.5 + cap 18 dB)。
  const double autoMakeupDb = std::min(
      kMakeupCapDb, -mThresholdDb * (1.0 - 1.0 / mRatio) * kMakeupFactor);

  // 6. linear gain (= -GR + makeup を 10^(x/20) に)。
  const double totalDb = -grApplied + autoMakeupDb;
  const double gainLin = std::pow(10.0, totalDb / 20.0);

  double wetL = dryL * gainLin;
  double wetR = dryR * gainLin;

  // 7. Mode 別 saturation (signal 経路後段、サイドチェインには入れない)。
  if (mMode == Mode::FET) {
    wetL = fetSaturate(wetL, kFetDrive);
    wetR = fetSaturate(wetR, kFetDrive);
  } else if (mMode == Mode::VariMu) {
    wetL = variMuSaturate(wetL, kVariMuDrive);
    wetR = variMuSaturate(wetR, kVariMuDrive);
  }

  // 8. dry/wet mix。
  l = dryL * (1.0 - mMix) + wetL * mMix;
  r = dryR * (1.0 - mMix) + wetR * mMix;
}

}  // namespace dx10::dsp

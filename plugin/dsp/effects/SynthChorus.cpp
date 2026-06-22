#include "SynthChorus.h"

#include <algorithm>
#include <cmath>

namespace dx10::dsp {

namespace {
constexpr double kPi = 3.141592653589793;
constexpr double kTwoPi = 2.0 * kPi;

struct ModePreset {
  double rateHz;
  double centerDelaySec;
  double clockDepth;
  double wetGain;
  double dryGain;
  double inputDrive;
  double preLpCutoffHz;
  double postLpCutoffHz;
  double widthScale;
};

constexpr int kModelCount = 2;
constexpr int kModeCount = 3;

// Model 0: JUNO-60。少し暗く、wet が太くまとまる設定。
// Model 1: JUNO-106。Roland 現行 MFX の JUNO-106 Chorus と同じく I / II / I+II
// を持つ独立モデルとして、60 より明るく、ステレオ反転感を少し強める。
constexpr ModePreset kModePresets[kModelCount][kModeCount] = {
    {
        // rate, delay, clock depth, wet, dry, drive, pre LP, post LP, width
        {0.51, 0.0072, 0.16, 0.86, 0.92, 1.18, 7200.0, 5200.0, 1.00},
        {0.86, 0.0067, 0.24, 0.94, 0.90, 1.24, 7200.0, 5200.0, 1.00},
        {7.10, 0.0058, 0.12, 0.72, 0.95, 1.12, 7600.0, 5600.0, 1.04},
    },
    {
        {0.60, 0.0066, 0.18, 0.88, 0.90, 1.10, 8400.0, 6300.0, 1.08},
        {1.08, 0.0061, 0.28, 1.00, 0.88, 1.14, 8500.0, 6500.0, 1.14},
        {6.70, 0.0053, 0.15, 0.80, 0.92, 1.08, 9000.0, 7000.0, 1.16},
    },
};

double onePoleCoeff(double cutoffHz, double sampleRate) {
  return 1.0 - std::exp(-kTwoPi * cutoffHz / sampleRate);
}

double softClip(double x, double drive) {
  const double driven = x * drive;
  return std::tanh(driven) / std::tanh(drive);
}
}  // namespace

void SynthChorus::prepare(double sampleRate) {
  mSampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
  recomputeModeCoeffs();
  reset();
}

void SynthChorus::reset() {
  std::fill(mLeftLine.buf.begin(), mLeftLine.buf.end(), 0.0);
  std::fill(mRightLine.buf.begin(), mRightLine.buf.end(), 0.0);
  mLeftLine.writePos = 0;
  mRightLine.writePos = 0;
  mLeftLine.preLp = 0.0;
  mRightLine.preLp = 0.0;
  mLeftLine.postLp = 0.0;
  mRightLine.postLp = 0.0;
  mLfoPhase = 0.0;
}

void SynthChorus::setParam(int paramIdx, double value01) {
  value01 = std::clamp(value01, 0.0, 1.0);
  switch (paramIdx) {
    case 0: {  // Model (0=JUNO-60, 1=JUNO-106)
      const int nextModel =
          std::min(kModelCount - 1,
                   std::max(0, static_cast<int>(std::round(value01 * (kModelCount - 1)))));
      if (nextModel != mModel) {
        mModel = nextModel;
        recomputeModeCoeffs();
      }
      break;
    }
    case 1: {  // Mode (0=I, 1=II, 2=I+II)
      const int nextMode =
          std::min(kModeCount - 1,
                   std::max(0, static_cast<int>(std::round(value01 * (kModeCount - 1)))));
      if (nextMode != mMode) {
        mMode = nextMode;
        recomputeModeCoeffs();
      }
      break;
    }
    case 2:  // Width (M/S side scale)。0..2 linear、0.5 で original。
      mWidth = value01 * 2.0;
      recomputeMakeup();  // width で wet side が伸びる分を makeup の headroom に反映
      break;
    case 3:  // Amount。0=dry, 1=modeled chorus circuit output。
      mAmount = value01;
      break;
    default:
      // 4..7 は未使用。
      break;
  }
}

void SynthChorus::recomputeModeCoeffs() noexcept {
  const ModePreset& p =
      kModePresets[std::clamp(mModel, 0, kModelCount - 1)]
                  [std::clamp(mMode, 0, kModeCount - 1)];
  mBaseDelaySamples = p.centerDelaySec * mSampleRate;
  mClockDepth = p.clockDepth;
  mWetGain = p.wetGain;
  mDryGain = p.dryGain;
  mInputDrive = p.inputDrive;
  mWidthScale = p.widthScale;
  mPreLpCoeff = onePoleCoeff(p.preLpCutoffHz, mSampleRate);
  mPostLpCoeff = onePoleCoeff(p.postLpCutoffHz, mSampleRate);
  mLfoInc = kTwoPi * p.rateHz / mSampleRate;

  recomputeMakeup();
}

void SynthChorus::recomputeMakeup() noexcept {
  // dry + wet 加算で chorus 経路ゲインが ~1.8x になり、Amount を上げると 0dBFS を
  // 越えてハードクリップ (= 強い歪み) していた。clean 方針として、worst-case ピーク
  // が入力フルスケールを越えないようゲイン正規化する (ソフトクリップ無し = 倍音を
  // 一切足さない)。wet 側は後段 M/S width で side 成分が mWidth*widthScale 倍される
  // ので、その分も headroom に織り込む。dry/wet の比率は変えないので音色は保持。
  const double widthGain = std::max(1.0, mWidth * mWidthScale);
  mChorusMakeup = 1.0 / (mDryGain + mWetGain * widthGain);
}

double SynthChorus::triangleLfo(double phase) noexcept {
  const double t = phase / kTwoPi;
  const double wrapped = t - std::floor(t);
  return 4.0 * std::abs(wrapped - 0.5) - 1.0;
}

double SynthChorus::readDelay(const std::array<double, kBufferSize>& buf,
                              int writePos,
                              double samplesAgo) const noexcept {
  const double bufLen = static_cast<double>(kBufferSize);
  double idx = static_cast<double>(writePos) - samplesAgo;
  while (idx < 0.0) idx += bufLen;
  while (idx >= bufLen) idx -= bufLen;
  const int i0 = static_cast<int>(idx);
  const int i1 = (i0 + 1 < kBufferSize) ? (i0 + 1) : 0;
  const double frac = idx - static_cast<double>(i0);
  return buf[i0] * (1.0 - frac) + buf[i1] * frac;
}

double SynthChorus::processLine(DelayLine& line,
                                double input,
                                double lfo,
                                bool invertMod) noexcept {
  line.preLp += mPreLpCoeff * (input - line.preLp);
  line.buf[line.writePos] = line.preLp;

  // MN3009/MN3007 系 BBD は clock で delay が決まるため、delay time を直接
  // 足し引きするより clock scale の逆数で動かす方が非対称な揺れになる。
  const double mod = invertMod ? -lfo : lfo;
  const double clockScale = std::clamp(1.0 + mClockDepth * mod, 0.55, 1.55);
  const double delaySamples =
      std::clamp(mBaseDelaySamples / clockScale, 1.0,
                 static_cast<double>(kBufferSize) - 4.0);

  const double delayed = readDelay(line.buf, line.writePos, delaySamples);
  line.writePos = (line.writePos + 1) % kBufferSize;

  line.postLp += mPostLpCoeff * (delayed - line.postLp);
  return line.postLp;
}

void SynthChorus::process(double& l, double& r) noexcept {
  const double dryL = l;
  const double dryR = r;
  const double inMono = 0.5 * (dryL + dryR);
  const double driven = softClip(inMono, mInputDrive);

  const double lfo = triangleLfo(mLfoPhase);
  mLfoPhase += mLfoInc;
  if (mLfoPhase >= kTwoPi) mLfoPhase -= kTwoPi;

  double wetL = processLine(mLeftLine, driven, lfo, false);
  double wetR = processLine(mRightLine, driven, lfo, true);

  const double wetMid = 0.5 * (wetL + wetR);
  const double wetSide = 0.5 * (wetL - wetR) * mWidth * mWidthScale;
  wetL = wetMid + wetSide;
  wetR = wetMid - wetSide;

  const double chorusL = (mDryGain * inMono + mWetGain * wetL) * mChorusMakeup;
  const double chorusR = (mDryGain * inMono + mWetGain * wetR) * mChorusMakeup;

  l = dryL * (1.0 - mAmount) + chorusL * mAmount;
  r = dryR * (1.0 - mAmount) + chorusR * mAmount;
}

}  // namespace dx10::dsp

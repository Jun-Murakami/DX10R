#include "StudioChorus.h"

#include <algorithm>
#include <cmath>

namespace dx10::dsp {

namespace {
constexpr double kPi = 3.141592653589793;
constexpr double kTwoPi = 2.0 * kPi;

constexpr double kInputHpCutoffHz = 80.0;
constexpr double kBbdLpCutoffHz = 9000.0;
constexpr double kCompAttackSec = 0.003;
constexpr double kCompReleaseSec = 0.090;
constexpr double kCompanderDrive = 1.6;
constexpr double kCompanderMakeup = 1.18;

// Dimension D の mode 1..3 は delay / depth / rate preset。Mode 4 は Mode 3
// の wet pad を解除する booster として扱う。
struct BaseMode {
  double delaySec;
  double depthSec;
  double rateHz;
};

constexpr BaseMode kBaseModes[3] = {
    {0.0100, 0.0020, 0.25},
    {0.0075, 0.0025, 0.25},
    {0.0075, 0.0015, 0.50},
};

constexpr double kNormalWetPadDb = -10.0;
// Mode 4 は「pad 完全解除」ではなく、Mode 3 を前に出す程度の軽い boost に留める。
// -10 dB → -5.5 dB で相対 +4.5 dB。以前の -4 dB (+6 dB) より押し出しを抑える。
constexpr double kBoostWetPadDb = -5.5;

// Matrix gains。wet side を強めにし、逆相 LFO の pitch motion を side 側へ逃がす。
constexpr double kDryMatrixGain = 1.0;
constexpr double kWetMidGain = 0.80;
constexpr double kWetSideGain = 1.80;
}  // namespace

void StudioChorus::prepare(double sampleRate) {
  mSampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
  mHpCoeff = std::exp(-kTwoPi * kInputHpCutoffHz / mSampleRate);
  mLpCoeff = 1.0 - std::exp(-kTwoPi * kBbdLpCutoffHz / mSampleRate);
  mCompAttackCoeff = std::exp(-1.0 / (kCompAttackSec * mSampleRate));
  mCompReleaseCoeff = std::exp(-1.0 / (kCompReleaseSec * mSampleRate));
  recomputeModeCoeffs();
  reset();
}

void StudioChorus::reset() {
  for (auto& line : mLines) {
    std::fill(line.buf.begin(), line.buf.end(), 0.0);
    line.writePos = 0;
    line.hpX1 = 0.0;
    line.hpY1 = 0.0;
    line.lp1 = 0.0;
    line.lp2 = 0.0;
    line.compEnv = 0.0;
  }
  mLfoPhase = 0.0;
}

void StudioChorus::recomputeModeCoeffs() noexcept {
  const int m = std::clamp(mMode, 0, 3);
  const int baseMode = (m == 3) ? 2 : m;
  const auto& mode = kBaseModes[baseMode];

  mBaseDelaySamples = mode.delaySec * mSampleRate;
  const double depthSamples = mode.depthSec * mSampleRate;
  const double maxDepth =
      static_cast<double>(kBufferSize) - mBaseDelaySamples - 4.0;
  mDepthSamples = std::min(depthSamples, std::max(0.0, maxDepth));

  mLfoInc = kTwoPi * mode.rateHz / mSampleRate;
  mWetTrim = dbToAmp((m == 3) ? kBoostWetPadDb : kNormalWetPadDb);
}

void StudioChorus::setParam(int paramIdx, double value01) {
  value01 = std::clamp(value01, 0.0, 1.0);
  switch (paramIdx) {
    case 0: {  // Mode (0=1, 1=2, 2=3, 3=4)
      const int n = 4;
      mMode = std::min(n - 1, std::max(0, static_cast<int>(std::round(value01 * (n - 1)))));
      recomputeModeCoeffs();
      break;
    }
    case 1:  // Width (M/S サイド倍率)。0..2 linear、0.5 で 1.0 (normal)。
      mWidth = value01 * 2.0;
      break;
    case 2:  // legacy Mix。実機同様 processed 固定なので無視する。
      break;
    default:
      // 未使用 paramOffset は noop。
      break;
  }
}

double StudioChorus::dbToAmp(double db) noexcept {
  return std::pow(10.0, db / 20.0);
}

double StudioChorus::triangleLfo(double phase) noexcept {
  return (2.0 / kPi) * std::asin(std::sin(phase));
}

double StudioChorus::readDelay(const DelayLine& line, double samplesAgo) const noexcept {
  const double bufLen = static_cast<double>(kBufferSize);
  double i = static_cast<double>(line.writePos) - samplesAgo;
  while (i < 0.0) i += bufLen;
  while (i >= bufLen) i -= bufLen;
  const int i0 = static_cast<int>(i);
  const int i1 = (i0 + 1 < kBufferSize) ? (i0 + 1) : 0;
  const double frac = i - static_cast<double>(i0);
  return line.buf[i0] * (1.0 - frac) + line.buf[i1] * frac;
}

double StudioChorus::processLine(int idx, double in, double delaySamples) noexcept {
  auto& line = mLines[idx];

  // Passive high-pass 相当で低域を delay modulation から逃がす。
  const double hp = mHpCoeff * (line.hpY1 + in - line.hpX1);
  line.hpX1 = in;
  line.hpY1 = hp;

  line.lp1 += mLpCoeff * (hp - line.lp1);

  const double rect = std::abs(line.lp1);
  const double envCoeff = (rect > line.compEnv) ? mCompAttackCoeff : mCompReleaseCoeff;
  line.compEnv = envCoeff * line.compEnv + (1.0 - envCoeff) * rect;
  const double compGain = 1.0 / std::sqrt(1.0 + kCompanderDrive * line.compEnv);
  line.buf[line.writePos] = line.lp1 * compGain * kCompanderMakeup;

  const double delayed = readDelay(line, std::max(1.0, delaySamples));
  line.writePos = (line.writePos + 1) % kBufferSize;

  line.lp2 += mLpCoeff * (delayed - line.lp2);
  const double expGain =
      std::sqrt(1.0 + kCompanderDrive * line.compEnv) / kCompanderMakeup;
  return line.lp2 * expGain;
}

void StudioChorus::process(double& l, double& r) noexcept {
  const double dryL = l;
  const double dryR = r;

  const double tri = triangleLfo(mLfoPhase);
  mLfoPhase += mLfoInc;
  if (mLfoPhase >= kTwoPi) mLfoPhase -= kTwoPi;

  const double delayL = mBaseDelaySamples + mDepthSamples * tri;
  const double delayR = mBaseDelaySamples - mDepthSamples * tri;
  const double wetL = processLine(0, dryL, delayL);
  const double wetR = processLine(1, dryR, delayR);

  const double wetMid = 0.5 * (wetL + wetR);
  const double wetSide = 0.5 * (wetL - wetR) * mWidth;

  const double dimensionL =
      dryL * kDryMatrixGain + mWetTrim * (kWetMidGain * wetMid + kWetSideGain * wetSide);
  const double dimensionR =
      dryR * kDryMatrixGain + mWetTrim * (kWetMidGain * wetMid - kWetSideGain * wetSide);

  l = dimensionL;
  r = dimensionR;
}

}  // namespace dx10::dsp

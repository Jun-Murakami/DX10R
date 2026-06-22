#include "Distortion.h"

#include <algorithm>
#include <cmath>

namespace dx10::dsp {

namespace {
constexpr double kDriveMaxDb = 36.0;          // Drive knob 最大
constexpr double kOutputMinDb = -24.0;        // Output bipolar の cut 側
constexpr double kOutputMaxDb = 12.0;         // Output bipolar の boost 側

// 0..1 → 中心 0 に asymmetric (v01=0.5 → center) でマッピング。
double bipolarMap(double v01, double min, double center, double max) noexcept {
  v01 = std::clamp(v01, 0.0, 1.0);
  if (v01 < 0.5) return min + (center - min) * (v01 * 2.0);
  return center + (max - center) * ((v01 - 0.5) * 2.0);
}

// ===== Saturation curves (stateless) =======================================

inline double satClip(double x) noexcept {
  // Hard clip ±1。digital fuzz 系の硬い色。
  return std::max(-1.0, std::min(1.0, x));
}

inline double satOverdrive(double x) noexcept {
  // tanh は smooth soft clip。tube screamer 系の温かい色。
  return std::tanh(x);
}

inline double satWaveshaper(double x) noexcept {
  // Algebraic soft saturation: x / (1 + |x|)。tanh とは curve が違って
  // gentle で fizz 寄り。倍音は奇数次主体だが分布が tanh と異なる。
  return x / (1.0 + std::abs(x));
}

inline double satTube(double x) noexcept {
  // Class A 真空管 cathode bias を模した非対称ソフトクリップ。
  // 正側は強めに圧縮、負側は緩く圧縮 → 偶数次倍音が立ち上がる。
  if (x > 0.0) return std::tanh(x * 1.2);
  return std::tanh(x * 0.7);
}

inline double satTape(double x) noexcept {
  // x / √(1 + x²)。tanh より round な knee で tape 飽和の優しい色。
  return x / std::sqrt(1.0 + x * x);
}

}  // namespace

void Distortion::prepare(double /*sampleRate*/) {
  // stateless なので reset 以外で再計算するものは無し。
  reset();
}

void Distortion::reset() {
  // 内部状態なし。
}

void Distortion::setParam(int paramIdx, double value01) {
  value01 = std::clamp(value01, 0.0, 1.0);
  switch (paramIdx) {
    case 0: {  // Algo
      const int n = 5;
      const int idx = std::min(n - 1, std::max(0, static_cast<int>(std::round(value01 * (n - 1)))));
      mAlgo = static_cast<Algo>(idx);
      break;
    }
    case 1: {  // Drive (0..36 dB linear)
      const double driveDb = value01 * kDriveMaxDb;
      mDriveLinear = std::pow(10.0, driveDb / 20.0);
      break;
    }
    case 2: {  // Output (-24..+12 dB bipolar、0.5=0dB)
      const double outDb = bipolarMap(value01, kOutputMinDb, 0.0, kOutputMaxDb);
      mOutputLinear = std::pow(10.0, outDb / 20.0);
      break;
    }
    case 3:  // Mix
      mMix = value01;
      break;
    default:
      break;
  }
}

double Distortion::applyAlgo(double x) const noexcept {
  switch (mAlgo) {
    case Algo::Clip: return satClip(x);
    case Algo::Overdrive: return satOverdrive(x);
    case Algo::Waveshaper: return satWaveshaper(x);
    case Algo::Tube: return satTube(x);
    case Algo::Tape: return satTape(x);
  }
  return x;  // unreachable
}

void Distortion::process(double& l, double& r) noexcept {
  const double dryL = l;
  const double dryR = r;

  // 1. Drive (saturator 入力に押し込む)。
  const double xL = dryL * mDriveLinear;
  const double xR = dryR * mDriveLinear;

  // 2. アルゴリズムカーブを通す。
  double yL = applyAlgo(xL);
  double yR = applyAlgo(xR);

  // 3. Output (±dB)。
  yL *= mOutputLinear;
  yR *= mOutputLinear;

  // 4. dry/wet ミックス。
  l = dryL * (1.0 - mMix) + yL * mMix;
  r = dryR * (1.0 - mMix) + yR * mMix;
}

}  // namespace dx10::dsp

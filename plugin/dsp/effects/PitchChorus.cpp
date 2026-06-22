#include "PitchChorus.h"

#include <algorithm>
#include <cmath>

namespace dx10::dsp {

namespace {
constexpr double kPi = 3.141592653589793;
constexpr double kTwoPi = 2.0 * kPi;

// Pitch shift エンジンの crossfade window 長 (sample)。 43 ms @ 48 kHz。
//
// 補足: window が長いほど 1 セグメントが長く (= 同一フレーズが連続しやすく)、
// reset 頻度が下がるので glitch も減るが、読み出し位置のドリフトが大きくなる
// ため低音側で comb filter 様のうねりが出やすくなる。
constexpr double kWindowSamplesD = static_cast<double>(PitchChorus::kWindowSamples);

// Mod LFO の最大スイープ振幅 (sample)。@ 48 kHz で約 ±2 ms。これより深い
// modulation を求める場面は別 effect (Flanger / Studio Chorus) に譲る。
constexpr double kModMaxMs = 2.0;

// Feedback 経路の 1-pole low-pass cutoff。pitch shifter 出力を再帰させると
// interp 残渣の HF が積み上がって耳痛い音色になりがち。6 kHz で軽く切ると、
// shimmer 系の長時間 feedback でも musical な減衰に化ける。
constexpr double kFeedbackLpHz = 6000.0;

// 0..1 を log で min..max にマップ (rate 用)。
double logMap(double v01, double min, double max) {
  v01 = std::clamp(v01, 0.0, 1.0);
  return min * std::pow(max / min, v01);
}
}  // namespace

void PitchChorus::prepare(double sampleRate) {
  mSampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;

  // Buffer 長: max delay (500 ms) + window (~43 ms) + mod depth (~2 ms) + 余裕。
  // SR=192 kHz で 500 ms = 96000 sample なので、150 K あれば十分。pitch shifter
  // が tap reset で過去側へジャンプするので、windowSamples は別途確保。
  const int requiredSamples =
      static_cast<int>(0.5 * mSampleRate) + kWindowSamples * 2 +
      static_cast<int>(kModMaxMs * 0.001 * mSampleRate) + 64;
  for (auto& v : mVoices) {
    if (static_cast<int>(v.buf.size()) < requiredSamples) {
      v.buf.assign(requiredSamples, 0.0);
    }
  }

  // Mod LFO 増分 (rad/sample)。Voice 同士の rate は同じだが、初期 phase で
  // decorrelate するので per-voice 速度差は不要。
  mModInc = kTwoPi * mModRateHz / mSampleRate;
  mModDepthSamples = mModDepth * kModMaxMs * 0.001 * mSampleRate;

  // Feedback LP coefficient: 1-pole で y += k * (x - y)、k = 1 - exp(-2π fc / SR)。
  mFbLpCoeff = 1.0 - std::exp(-kTwoPi * kFeedbackLpHz / mSampleRate);

  reset();
}

void PitchChorus::reset() {
  for (auto& v : mVoices) {
    std::fill(v.buf.begin(), v.buf.end(), 0.0);
    v.writePos = 0;
    v.tapPhase[0] = 0.0;
    v.tapPhase[1] = 0.5;
    // 初期 samplesBehind = windowSamples / 2。tap 0 と tap 1 は 0.5 位相分
    // 離れているが、samplesBehind 自体は同じ位置から開始 (= 同 audio を window
    // 半周遅れで再生開始する)。pitchRatio≠1 なら 1 sample ごとに drift して
    // やがて reset 境界に到達する。
    v.tapSamplesBehind[0] = kWindowSamplesD * 0.5;
    v.tapSamplesBehind[1] = kWindowSamplesD * 0.5;
    v.modPhase = 0.0;
    v.fbState = 0.0;
    v.fbLpState = 0.0;
  }
  // Voice 1 の mod LFO 位相を半周ずらして decorrelate。
  if (mVoices.size() >= 2) mVoices[1].modPhase = kPi;
}

void PitchChorus::setParam(int paramIdx, double value01) {
  value01 = std::clamp(value01, 0.0, 1.0);
  switch (paramIdx) {
    case 0:  // Pitch A: 0..1 → -50..+50 cent
      mPitchACents = (value01 - 0.5) * 100.0;
      mVoices[0].pitchRatio = std::pow(2.0, mPitchACents / 1200.0);
      break;
    case 1:  // Pitch B
      mPitchBCents = (value01 - 0.5) * 100.0;
      mVoices[1].pitchRatio = std::pow(2.0, mPitchBCents / 1200.0);
      break;
    case 2: {  // Delay A: 0..500 ms
      mDelayAMs = value01 * 500.0;
      const double samples = mDelayAMs * 0.001 * mSampleRate;
      // buffer の物理サイズより常に余裕を持たせる。
      const double maxSamples =
          static_cast<double>(mVoices[0].buf.size()) - kWindowSamplesD * 2.0 - 16.0;
      mVoices[0].extraDelaySamples = std::min(samples, std::max(0.0, maxSamples));
      break;
    }
    case 3: {  // Delay B
      mDelayBMs = value01 * 500.0;
      const double samples = mDelayBMs * 0.001 * mSampleRate;
      const double maxSamples =
          static_cast<double>(mVoices[1].buf.size()) - kWindowSamplesD * 2.0 - 16.0;
      mVoices[1].extraDelaySamples = std::min(samples, std::max(0.0, maxSamples));
      break;
    }
    case 4:  // Mod Depth (0..1, 最大 ±2 ms)
      mModDepth = value01;
      mModDepthSamples = mModDepth * kModMaxMs * 0.001 * mSampleRate;
      break;
    case 5:  // Mod Rate (0.05..6 Hz, log)
      mModRateHz = logMap(value01, 0.05, 6.0);
      mModInc = kTwoPi * mModRateHz / mSampleRate;
      break;
    case 6:  // Feedback (0..0.7)
      mFeedback = value01 * 0.7;
      break;
    case 7:  // Mix (dry..wet)
      mMix = value01;
      break;
    default:
      break;
  }
}

double PitchChorus::readDelay(const std::vector<double>& buf, int writePos,
                              double samplesAgo) const noexcept {
  // 4-point cubic Hermite (Catmull-Rom) 補間。線形補間に比べて高次倍音の
  // alias が抑えられ、pitch shifter で fractional read が連続する場面で grain
  // 感が減る。
  const int bufLen = static_cast<int>(buf.size());
  if (bufLen <= 0) return 0.0;
  double i = static_cast<double>(writePos) - samplesAgo;
  while (i < 0.0) i += bufLen;
  while (i >= bufLen) i -= bufLen;
  const int i1 = static_cast<int>(i);
  const double frac = i - static_cast<double>(i1);
  const int i0 = (i1 == 0) ? (bufLen - 1) : (i1 - 1);
  const int i2 = (i1 + 1 < bufLen) ? (i1 + 1) : 0;
  const int i3 = (i2 + 1 < bufLen) ? (i2 + 1) : 0;
  const double y0 = buf[i0];
  const double y1 = buf[i1];
  const double y2 = buf[i2];
  const double y3 = buf[i3];
  // Catmull-Rom 係数
  const double a = -0.5 * y0 + 1.5 * y1 - 1.5 * y2 + 0.5 * y3;
  const double b = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
  const double c = -0.5 * y0 + 0.5 * y2;
  const double d = y1;
  return ((a * frac + b) * frac + c) * frac + d;
}

double PitchChorus::processVoice(Voice& v, double in, double modSamples) noexcept {
  // 1. Feedback path を 1-pole LP で smooth してから input に足し戻す。
  //    pitch shifter 出力を再帰させると interp 残渣の HF が累積してキツくなるが、
  //    6 kHz で軽く切ると shimmer 系の長 feedback でも musical な減衰になる。
  //    fb < 1.0 で発振しない (ゲインが 1 未満で feedback ループは線形に bound)
  //    ため、別途の saturator は不要。
  v.fbLpState += mFbLpCoeff * (v.fbState - v.fbLpState);
  const double fbInput = in + mFeedback * v.fbLpState;

  // 2. Delay buffer に書き込み。
  const int bufLen = static_cast<int>(v.buf.size());
  v.buf[v.writePos] = fbInput;

  // 3. 各 tap の phase / samplesBehind を進める。windowSamples で 1 周。
  const double phaseInc = 1.0 / kWindowSamplesD;
  const double driftPerSample = v.pitchRatio - 1.0;

  // pitchRatio≠1 のとき、tapSamplesBehind が drift してやがて 0 を割ったり
  // buffer 端を超えたりするのを windowSamples 単位で reset で巻き戻す。reset
  // 後の samplesBehind は固定 reference (= windowSamples / 2) にスナップする
  // ことで、累積ドリフトを防ぐ。
  for (int t = 0; t < 2; ++t) {
    v.tapPhase[t] += phaseInc;
    v.tapSamplesBehind[t] -= driftPerSample;
    if (v.tapPhase[t] >= 1.0) {
      v.tapPhase[t] -= 1.0;
      v.tapSamplesBehind[t] = kWindowSamplesD * 0.5;
    }
  }

  // 4. 各 tap を crossfade して shifter 出力を作る。phase 0..1 を sin で
  //    Hann window 化、tap 0/1 が 0.5 位相ずれているので sin² の和が 1.0。
  //    実 read 位置は base samplesBehind + LFO mod + 固定 extraDelay。
  const double readBehind0 =
      v.tapSamplesBehind[0] + modSamples + v.extraDelaySamples;
  const double readBehind1 =
      v.tapSamplesBehind[1] + modSamples + v.extraDelaySamples;
  const double r0 = readDelay(v.buf, v.writePos, readBehind0);
  const double r1 = readDelay(v.buf, v.writePos, readBehind1);
  const double g0 = std::sin(kPi * v.tapPhase[0]);
  const double g1 = std::sin(kPi * v.tapPhase[1]);
  const double shifted = g0 * r0 + g1 * r1;

  // 5. write pointer 更新 + feedback state 更新。
  v.writePos = (v.writePos + 1) % bufLen;
  v.fbState = shifted;

  return shifted;
}

void PitchChorus::process(double& l, double& r) noexcept {
  const double dryL = l;
  const double dryR = r;
  // Mono 化した入力を 2 voice に分配。典型的な使い方は mono ソースに対して
  // voice を L/R 振り分けてステレオ化する形。ステレオ入力でも (L+R)/2 で
  // voice 入力を作り、出力で左右に振り直す。
  const double monoIn = 0.5 * (dryL + dryR);

  // Mod LFO 値 (sample 単位の delay tap オフセット)。Voice 0 と 1 で
  // modPhase が π ずれているので逆相揺らしになる。
  const double modA = std::sin(mVoices[0].modPhase) * mModDepthSamples;
  const double modB = std::sin(mVoices[1].modPhase) * mModDepthSamples;
  mVoices[0].modPhase += mModInc;
  mVoices[1].modPhase += mModInc;
  if (mVoices[0].modPhase >= kTwoPi) mVoices[0].modPhase -= kTwoPi;
  if (mVoices[1].modPhase >= kTwoPi) mVoices[1].modPhase -= kTwoPi;

  const double wetA = processVoice(mVoices[0], monoIn, modA);
  const double wetB = processVoice(mVoices[1], monoIn, modB);

  // Voice A → L、Voice B → R にハードパンしてステレオ wet を作る。両端で
  // 逆方向 detune が広がる定番のステレオ展開。
  const double wetL = wetA;
  const double wetR = wetB;

  // Equal-power mix。dry と wet は数 ms ずれた detune 信号で位相相関が低いので、
  // linear だと中央付近で軽い音量谷が出る。cos/sin で和を一定に。
  const double dryGain = std::cos(0.5 * kPi * mMix);
  const double wetGain = std::sin(0.5 * kPi * mMix);
  l = dryGain * dryL + wetGain * wetL;
  r = dryGain * dryR + wetGain * wetR;
}

}  // namespace dx10::dsp

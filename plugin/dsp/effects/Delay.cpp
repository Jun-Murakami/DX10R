#include "Delay.h"

#include <algorithm>
#include <cmath>

namespace dx10::dsp {

namespace {
constexpr double kPi = 3.141592653589793;
constexpr double kTwoPi = 2.0 * kPi;

// Sync 音符 division の beat 数 (4 分音符 = 1.0 beat 換算)。短い順に並べる。
//   1/64  = 0.0625 beat
//   1/32  = 0.125
//   1/16T = 1/6   (16 分音符 triplet)
//   1/16  = 0.25
//   1/8T  = 1/3
//   1/16. = 0.375 (dotted 16th)
//   1/8   = 0.5
//   1/4T  = 2/3
//   1/8.  = 0.75
//   1/4   = 1.0
//   1/4.  = 1.5
//   1/2   = 2.0
constexpr double kSyncDivBeats[Delay::kNumSyncDivisions] = {
    0.0625,
    0.125,
    1.0 / 6.0,
    0.25,
    1.0 / 3.0,
    0.375,
    0.5,
    2.0 / 3.0,
    0.75,
    1.0,
    1.5,
    2.0,
};

// Feedback の安全上限。1.0 だと無限発散するので 0.95 にクランプ。
constexpr double kMaxFeedback = 0.95;

// Tone (LP cutoff) のレンジ。0 → 500 Hz (dark tape)、1 → 18000 Hz (clean)。
constexpr double kToneCutoffMinHz = 500.0;
constexpr double kToneCutoffMaxHz = 18000.0;

// Free time のレンジ (ms)。Delay::kMaxFreeMs と同期させる。
constexpr double kFreeMinMs = 1.0;
constexpr double kFreeMaxMs = Delay::kMaxFreeMs;

// buffer 容量の安全マージン: max free time + sync max (1/2 note @ very slow tempo)
// + fractional 補間用の 4 sample。Sync max は 1/2 note @ 30 BPM = 4 秒だが、現実
// 的な使い方を考慮し 1/2 note @ 60 BPM = 2 秒で頭打ち。Free max が 2 秒なので、
// 結局 max(2, 2) + small slack = 2.5 秒分を確保する。
constexpr double kBufferSafetySeconds = 2.5;

double logMap(double v01, double min, double max) {
  v01 = std::clamp(v01, 0.0, 1.0);
  return min * std::pow(max / min, v01);
}
}  // namespace

void Delay::prepare(double sampleRate) {
  mSampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
  // SR に追従して buffer を確保。実機ホスト SR の上限 (192 kHz) を想定すると
  // 最大 2 秒 + 余裕で約 524288 sample 程度。OnReset 経由で UI thread から呼ば
  // れるので vector resize は安全。
  const int needed = static_cast<int>(std::ceil(kBufferSafetySeconds * mSampleRate));
  mBufSize = std::max(64, needed);
  mBufL.assign(static_cast<std::size_t>(mBufSize), 0.0);
  mBufR.assign(static_cast<std::size_t>(mBufSize), 0.0);
  // タイム切替 crossfade の長さ (約 20 ms)。SR に追従。
  mXfadeLen = std::max(1, static_cast<int>(0.02 * mSampleRate));
  recomputeTimeSamples(/*glide=*/false);
  // Tone coefficient は cutoff から再計算 (mToneCoeff の現状値を反映)。
  // 簡易: 既存 cutoff_idx が無いので、setParam 経由で改めて来る前の暫定として
  // bypass (= coeff 1.0) にしておく。
  mToneCoeff = 1.0;
  reset();
}

void Delay::reset() {
  std::fill(mBufL.begin(), mBufL.end(), 0.0);
  std::fill(mBufR.begin(), mBufR.end(), 0.0);
  mWritePos = 0;
  mToneStateL = 0.0;
  mToneStateR = 0.0;
  // 進行中の crossfade を畳む (現タップに確定)。
  mOldTimeSamples = mTimeSamples;
  mXfadePos = mXfadeLen;
}

void Delay::recomputeTimeSamples(bool glide) noexcept {
  double seconds;
  if (mTimeMode == 0) {  // Sync
    const int idx = std::clamp(mSyncDivIdx, 0, kNumSyncDivisions - 1);
    const double bpm = (mTempoBpm > 0.0) ? mTempoBpm : 120.0;
    // beat * (60 / BPM) 秒
    seconds = kSyncDivBeats[idx] * (60.0 / bpm);
  } else {  // Free
    seconds = mFreeMs * 0.001;
  }
  double samples = seconds * mSampleRate;
  // buffer 容量を超えない範囲で clamp。読み出しの fractional 補間で隣接 sample を
  // 触るので 4 sample 余裕を取る。prepare 前に呼ばれた場合 (mBufSize=0) は 1 で
  // 抑止する (process は無音状態なので問題ない)。
  const double maxSamples = (mBufSize > 4) ? static_cast<double>(mBufSize) - 4.0 : 1.0;
  samples = std::min(samples, maxSamples);
  samples = std::max(samples, 1.0);

  // 即時反映 (prepare 初期化) か、フェード不能なら段差ごと差し替える。
  if (!glide || mXfadeLen <= 0) {
    mTimeSamples = samples;
    mOldTimeSamples = samples;
    mXfadePos = mXfadeLen;  // フェード非活性
    return;
  }
  // 変化が 1 sample 未満 (DAW BPM の量子化揺れ等) なら無視し、フェード乱発を防ぐ。
  if (std::abs(samples - mTimeSamples) < 1.0) return;
  // クリーンデジタル: 現タップを起点に新タップへ crossfade を開始 (process で進行)。
  mOldTimeSamples = mTimeSamples;
  mTimeSamples = samples;
  mXfadePos = 0;
}

void Delay::setTempo(double bpm) noexcept {
  if (bpm <= 0.0 || std::isnan(bpm)) return;
  if (std::abs(bpm - mTempoBpm) < 1e-6) return;
  mTempoBpm = bpm;
  if (mTimeMode == 0) recomputeTimeSamples();
}

void Delay::setParam(int paramIdx, double value01) {
  value01 = std::clamp(value01, 0.0, 1.0);
  switch (paramIdx) {
    case 0: {  // Time Mode (0=Sync, 1=Free)
      const int n = 2;
      mTimeMode = std::min(n - 1, std::max(0, static_cast<int>(std::round(value01 * (n - 1)))));
      recomputeTimeSamples();
      break;
    }
    case 1: {  // Time Sync (9 段)
      const int n = kNumSyncDivisions;
      mSyncDivIdx = std::min(n - 1, std::max(0, static_cast<int>(std::round(value01 * (n - 1)))));
      if (mTimeMode == 0) recomputeTimeSamples();
      break;
    }
    case 2: {  // Time Free (1..2000 ms, log)
      mFreeMs = logMap(value01, kFreeMinMs, kFreeMaxMs);
      if (mTimeMode == 1) recomputeTimeSamples();
      break;
    }
    case 3: {  // Stereo Mode (0=Mono, 1=Ping-Pong)
      const int n = 2;
      mStereoMode = std::min(n - 1, std::max(0, static_cast<int>(std::round(value01 * (n - 1)))));
      break;
    }
    case 4:  // Feedback (0..0.95)
      mFeedback = value01 * kMaxFeedback;
      break;
    case 5: {  // Tone (LP cutoff、log map)
      const double cutoffHz = logMap(value01, kToneCutoffMinHz, kToneCutoffMaxHz);
      // 1-pole LP coefficient: alpha = 1 - exp(-2π*fc/sr)
      mToneCoeff = 1.0 - std::exp(-kTwoPi * cutoffHz / mSampleRate);
      break;
    }
    case 6:  // Width (M/S サイド倍率)。0..2 linear、0.5 で 1.0 (normal)。
      mWidth = value01 * 2.0;
      break;
    case 7:  // Mix (dry/wet)。信号フロー上、Width で stereo 化したあと最後に
             // dry/wet を混ぜるので paramOffset も最後 (= 直近) に置く。
             // Equal-power crossfade: dry と wet (delay tap) は時間的に無相関なので
             // linear だと mix=0.5 で約 -3 dB の音量谷が出る。cos/sin で gain² 和を
             // 1 に固定し、mix 全域で perceived loudness を一定に保つ。
      mMix = value01;
      mDryGain = std::cos(mMix * 0.5 * kPi);
      mWetGain = std::sin(mMix * 0.5 * kPi);
      break;
    default:
      break;
  }
}

double Delay::readDelay(const std::vector<double>& buf, double samplesAgo) const noexcept {
  const double bufLen = static_cast<double>(mBufSize);
  double idx = static_cast<double>(mWritePos) - samplesAgo;
  while (idx < 0.0) idx += bufLen;
  while (idx >= bufLen) idx -= bufLen;
  const int i0 = static_cast<int>(idx);
  const int i1 = (i0 + 1 < mBufSize) ? (i0 + 1) : 0;
  const double frac = idx - static_cast<double>(i0);
  return buf[i0] * (1.0 - frac) + buf[i1] * frac;
}

void Delay::process(double& l, double& r) noexcept {
  // prepare 前に呼ばれた場合は dry を素通しする (EffectsRack の通常経路では
  // prepare → process の順序が保証されるが、防御的に。div-by-zero を避ける)。
  if (mBufSize <= 0) return;
  const double dryL = l;
  const double dryR = r;

  // 1. delay buffer 読み出し。タイム切替中 (mXfadePos < mXfadeLen) は旧タップ→
  //    新タップを equal-power crossfade し、読み出し位置の不連続を消す。フェード
  //    後の read* を feedback / wet 双方に使うので、出力もバッファ書き戻しも段差なし。
  double readL, readR;
  if (mXfadePos < mXfadeLen) {
    const double x = static_cast<double>(mXfadePos) / static_cast<double>(mXfadeLen);
    const double gOld = std::cos(x * 0.5 * kPi);
    const double gNew = std::sin(x * 0.5 * kPi);
    readL = readDelay(mBufL, mOldTimeSamples) * gOld + readDelay(mBufL, mTimeSamples) * gNew;
    readR = readDelay(mBufR, mOldTimeSamples) * gOld + readDelay(mBufR, mTimeSamples) * gNew;
    ++mXfadePos;
  } else {
    readL = readDelay(mBufL, mTimeSamples);
    readR = readDelay(mBufR, mTimeSamples);
  }

  // 2. Tone (LP) を feedback path に適用。 1-pole LP: y += coeff * (x - y)。
  //    coeff = 1 で transparent (= input をそのまま通す)、coeff < 1 で減衰。
  mToneStateL += mToneCoeff * (readL - mToneStateL);
  mToneStateR += mToneCoeff * (readR - mToneStateR);
  const double fbL = mToneStateL * mFeedback;
  const double fbR = mToneStateR * mFeedback;

  // 3. buffer に書き込み (mode によって配線が変わる)。
  if (mStereoMode == 0) {
    // Mono: 入力 (L+R)/2 を両 buffer に同じく書き込む。Feedback も自身の delay。
    const double inMono = 0.5 * (dryL + dryR);
    mBufL[mWritePos] = inMono + fbL;
    mBufR[mWritePos] = inMono + fbR;
  } else {
    // Ping-Pong: 入力は L 側に、L → R → L → R … と跳ね返る。
    //   L buffer = input + R 経由の戻り feedback
    //   R buffer = L の delay 出力 (feedback applied)
    const double inMono = 0.5 * (dryL + dryR);
    mBufL[mWritePos] = inMono + fbR;
    mBufR[mWritePos] = fbL;
  }

  // 4. write pointer を進める。
  mWritePos = (mWritePos + 1) % mBufSize;

  // 5. M/S 処理で wet の stereo 幅を調整 (mWidth: 0=mono, 1=normal, 2=wider)。
  //    Mono mode は L=R で side = 0 なので無効化、Ping-Pong は L≠R で効果あり。
  const double m = 0.5 * (readL + readR);
  const double s = 0.5 * (readL - readR) * mWidth;
  const double widedL = m + s;
  const double widedR = m - s;

  // 6. dry/wet ミックス (equal-power)。wet 出力は read* (filter 通す前の生 delay)。
  //    Tone は feedback iteration 用なので dry/wet には混ぜない (= output は bright)。
  l = dryL * mDryGain + widedL * mWetGain;
  r = dryR * mDryGain + widedR * mWetGain;
}

}  // namespace dx10::dsp

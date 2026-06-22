#pragma once

#include <cmath>

namespace dx10 {
namespace dsp {

// 2:1 decimator — a linear-phase windowed-sinc FIR low-pass (cutoff at the base
// Nyquist = Fs/2 = 0.25 of the 2x rate) applied before downsampling by 2.
//
// DX10R renders the FM voice loop at 2x the host rate (anti-aliasing the
// 5th-order waveshaper + FM partials), then feeds each pair of oversampled mono
// sub-samples through process() to get one band-limited output sample. Mono only
// (the synth sums to a single channel that is copied to L/R), so this runs once
// per output sample — cheap.
struct Decimator2x
{
  static constexpr int kNumTaps = 31; // odd → integer group delay = 15 samples
  double h[kNumTaps] = {};
  double z[kNumTaps] = {}; // delay line (ring buffer)
  int wp = 0;

  void prepare()
  {
    constexpr double kFc = 0.25; // normalized to the 2x rate → cuts at Fs/2
    constexpr double kPi = 3.14159265358979323846;
    const double mid = (kNumTaps - 1) / 2.0;
    double sum = 0.0;
    for (int i = 0; i < kNumTaps; ++i)
    {
      const double m = i - mid;
      const double sinc = (m == 0.0) ? (2.0 * kFc) : std::sin(2.0 * kPi * kFc * m) / (kPi * m);
      // Blackman window for low stopband leakage.
      const double win = 0.42 - 0.5 * std::cos(2.0 * kPi * i / (kNumTaps - 1)) +
                         0.08 * std::cos(4.0 * kPi * i / (kNumTaps - 1));
      h[i] = sinc * win;
      sum += h[i];
    }
    for (double& c : h) c /= sum; // unity DC gain
    reset();
  }

  void reset()
  {
    for (double& s : z) s = 0.0;
    wp = 0;
  }

  inline void push(double s)
  {
    z[wp] = s;
    if (++wp >= kNumTaps) wp = 0;
  }

  // Feed two oversampled sub-samples, return one decimated output sample.
  inline double process(double a, double b)
  {
    push(a);
    push(b);
    double acc = 0.0;
    int idx = wp - 1;
    if (idx < 0) idx += kNumTaps;
    for (int k = 0; k < kNumTaps; ++k)
    {
      acc += h[k] * z[idx];
      if (--idx < 0) idx += kNumTaps;
    }
    return acc;
  }
};

} // namespace dsp
} // namespace dx10

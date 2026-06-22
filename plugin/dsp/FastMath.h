// 高速数学関数 (Synth-80)。
//
// fastTanh: std::tanh の高精度 odd rational 近似 (2026-06-12 perf C1)。
//   tanh(x) ≈ x · P(x²) / Q(x²)   (P, Q とも x² の 6 次多項式)
//
// 係数は minimax IRLS fit (scripts/calibration/scratch/fit_fast_tanh.py) で導出し、
// 2M 点 dense grid で実測検証済み:
//   - |x| ≤ 9.2 : 絶対誤差 max 1.55e-8 (≈ -156 dBFS)、小信号相対誤差 max 8.3e-8
//   - |x| > 9.2 : ±1 に clamp (tanh(9.2) = 1 - 2.05e-8 なので clamp 誤差も同オーダー、
//                 境界の不連続も ~1e-8)
//   - Q(x²) ≥ 1 on [0, 9.2²] (分母は常に正、ゼロ割なし)
//   - NaN 入力は NaN を返す (std::tanh と同挙動)
//
// 誤差は 24-bit LSB (5.96e-8) を下回るため聴感上は透明。VCF ladder の tanh 積分器
// (per-voice 毎秒 ~100 万回) と VCO サチュレーションのホットパス専用。係数を
// 変えたら fit script を再実行し、tests/test_fast_math.cpp の誤差上限で検証すること。

#pragma once

#include "Simd.h"

// scalar fastTanh と SIMD 版 (fastTanh2/4) のレーン bit 一致は「mul / add が融合
// されない」前提で成立する。SIMD 側は intrinsic で分離済みだが、scalar 側は clang
// が既定 (-ffp-contract=on) で a + b*t を FMA に融合して丸めが変わるため、関数
// 単位で contraction を切る。MSVC は既定 (/fp:precise) で contraction しない。
#if defined(__clang__)
  #define S80_FP_CONTRACT_OFF _Pragma("clang fp contract(off)")
#else
  #define S80_FP_CONTRACT_OFF
#endif

namespace dx10::dsp {

// fastTanh の係数 (scalar / SIMD 2-lane で共有。両者は同一係数・同一演算順なので
// レーン単位で bit 一致する = tests/test_fast_math.cpp で検証)。
namespace fasttanh_detail {
inline constexpr double kClamp = 9.2;
inline constexpr double kP0 = 0.9999999173136686;
inline constexpr double kP1 = -0.15887602595266753;
inline constexpr double kP2 = 0.031431905001797635;
inline constexpr double kP3 = 0.007935085524027437;
inline constexpr double kP4 = 0.00022387569687472905;
inline constexpr double kP5 = 1.3093187746573975e-06;
inline constexpr double kP6 = 8.110041249675959e-10;
inline constexpr double kQ0 = 1.0;
inline constexpr double kQ1 = 0.17445681841092459;
inline constexpr double kQ2 = -0.043748293050045635;
inline constexpr double kQ3 = 0.024058910143815344;
inline constexpr double kQ4 = 0.001622692815109229;
inline constexpr double kQ5 = 2.1088905383828075e-05;
inline constexpr double kQ6 = 4.846420359472248e-08;
}  // namespace fasttanh_detail

inline double fastTanh(double x) noexcept {
  S80_FP_CONTRACT_OFF
  namespace d = fasttanh_detail;
  if (x > d::kClamp) return 1.0;
  if (x < -d::kClamp) return -1.0;
  const double t = x * x;
  // (2026-06-12 perf D4) Horner (6 段直列 ≈ 48 cycles) → Estrin (依存深さ ~3)。
  // 同一 minimax 多項式の評価順変更のみで、丸め差は ~1 ulp (≈ -300 dB)。近似誤差
  // 上限 (≤2.5e-8 vs std::tanh) は test_fast_math.cpp が引き続き保証する。
  const double t2 = t * t;
  const double t4 = t2 * t2;
  const double p = (d::kP0 + d::kP1 * t) + (d::kP2 + d::kP3 * t) * t2 +
                   ((d::kP4 + d::kP5 * t) + d::kP6 * t2) * t4;
  const double q = (d::kQ0 + d::kQ1 * t) + (d::kQ2 + d::kQ3 * t) * t2 +
                   ((d::kQ4 + d::kQ5 * t) + d::kQ6 * t2) * t4;
  return x * p / q;
}

// fastTanh の 2-lane SIMD 版 (2026-06-12 perf D4)。係数・演算順が scalar と同一
// なのでレーンごとの結果は fastTanh(x) と bit 一致する。clamp はマスク select で
// 同値 (NaN は両比較が偽 → 計算値 NaN がそのまま通る = scalar と同挙動)。
// VCF ladder の 4 つの state tanh を 2 本に束ねるホットパス専用。
inline F64x2 fastTanh2(F64x2 x) noexcept {
  namespace d = fasttanh_detail;
  auto B = [](double c) noexcept { return F64x2::broadcast(c); };
  const F64x2 t = x * x;
  // scalar fastTanh と同一の Estrin 評価順 (= レーン bit 一致を維持)。
  const F64x2 t2 = t * t;
  const F64x2 t4 = t2 * t2;
  const F64x2 p = (B(d::kP0) + B(d::kP1) * t) + (B(d::kP2) + B(d::kP3) * t) * t2 +
                  ((B(d::kP4) + B(d::kP5) * t) + B(d::kP6) * t2) * t4;
  const F64x2 q = (B(d::kQ0) + B(d::kQ1) * t) + (B(d::kQ2) + B(d::kQ3) * t) * t2 +
                  ((B(d::kQ4) + B(d::kQ5) * t) + B(d::kQ6) * t2) * t4;
  F64x2 r = x * p / q;
  r = F64x2::select(cmpGt(x, B(d::kClamp)), B(1.0), r);
  r = F64x2::select(cmpGt(B(-d::kClamp), x), B(-1.0), r);
  return r;
}

#if defined(S80_SIMD_AVX2_COMPILE)
// fastTanh の 4-lane AVX2 版 (2026-06-12 perf D5)。係数・Estrin 評価順が scalar /
// 2-lane と同一なのでレーンごとの結果は fastTanh(x) と bit 一致する (FMA 不使用)。
// 呼び出し側も S80_TARGET_AVX2 を付け、cpuHasAvx2() でガードすること。
S80_TARGET_AVX2 inline F64x4 fastTanh4(F64x4 x) noexcept {
  namespace d = fasttanh_detail;
  // (lambda には target attribute を付けられないので broadcast を直接呼ぶ)
  using V = F64x4;
  const F64x4 t = x * x;
  const F64x4 t2 = t * t;
  const F64x4 t4 = t2 * t2;
  const F64x4 p =
      (V::broadcast(d::kP0) + V::broadcast(d::kP1) * t) +
      (V::broadcast(d::kP2) + V::broadcast(d::kP3) * t) * t2 +
      ((V::broadcast(d::kP4) + V::broadcast(d::kP5) * t) +
       V::broadcast(d::kP6) * t2) * t4;
  const F64x4 q =
      (V::broadcast(d::kQ0) + V::broadcast(d::kQ1) * t) +
      (V::broadcast(d::kQ2) + V::broadcast(d::kQ3) * t) * t2 +
      ((V::broadcast(d::kQ4) + V::broadcast(d::kQ5) * t) +
       V::broadcast(d::kQ6) * t2) * t4;
  F64x4 r = x * p / q;
  r = F64x4::select(cmpGt(x, V::broadcast(d::kClamp)), V::broadcast(1.0), r);
  r = F64x4::select(cmpGt(V::broadcast(-d::kClamp), x), V::broadcast(-1.0), r);
  return r;
}
#endif  // S80_SIMD_AVX2_COMPILE

// fastCos2Pi: cos(2π·x) の多項式近似 (2026-06-12 perf D1)。x は発振器位相 [0, 1]。
//   u = x - 0.5 で [-0.5, 0.5] に中心化し、cos(2πx) = -cos(2πu)。y = 2πu ∈ [-π, π]
//   に対して Taylor 級数を y^20 (w = y² の 10 次 Horner) まで評価する。
//   打ち切り誤差は |y|=π で y²²/22! ≈ 7.7e-11 (≈ -202 dBFS)、丸めを含め max ~1e-10。
//   triangle 倍音 makeup (H2..H14 の Chebyshev 漸化式の種) のホットパス専用。
//   範囲外の x には範囲縮小を行わないので [0, 1] 以外を渡さないこと
//   (tests/test_fast_math.cpp が dense grid で誤差上限を検証する)。
inline double fastCos2Pi(double x) noexcept {
  constexpr double kTwoPi = 6.283185307179586476925286766559;
  const double y = kTwoPi * (x - 0.5);
  const double w = y * y;
  // cos(y) = Σ (-1)^k y^(2k) / (2k)!  (k = 0..10)。Horner (直列 10 段 = レイテンシ
  // 律速) ではなく Estrin 評価 (依存深さ ~4) にして OoO 実行で並列化する。
  constexpr double a0 = 1.0;
  constexpr double a1 = -0.5;
  constexpr double a2 = 0.041666666666666664;
  constexpr double a3 = -0.001388888888888889;
  constexpr double a4 = 2.48015873015873e-05;
  constexpr double a5 = -2.7557319223985893e-07;
  constexpr double a6 = 2.08767569878681e-09;
  constexpr double a7 = -1.1470745597729725e-11;
  constexpr double a8 = 4.779477332387385e-14;
  constexpr double a9 = -1.5619206968586225e-16;
  constexpr double a10 = 4.110317623312165e-19;
  const double w2 = w * w;
  const double w4 = w2 * w2;
  const double w8 = w4 * w4;
  const double b0 = a0 + a1 * w;
  const double b1 = a2 + a3 * w;
  const double b2 = a4 + a5 * w;
  const double b3 = a6 + a7 * w;
  const double b4 = a8 + a9 * w;
  const double c0 = b0 + b1 * w2;
  const double c1 = b2 + b3 * w2;
  const double c2 = b4 + a10 * w2;
  return -(c0 + c1 * w4 + c2 * w8);
}

}  // namespace dx10::dsp

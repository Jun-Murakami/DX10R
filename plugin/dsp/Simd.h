// 2-lane double SIMD ラッパ (Synth-80, 2026-06-12 perf D4)。
//
// VCF ladder の tanh 積分器 4 段を 2 レーン × 2 本に束ねるための最小限の抽象。
// 設計方針:
//   - x64 (Windows/macOS Intel) = SSE2 (x64 では常に利用可能、ビルドフラグ不要)
//   - arm64 (Apple Silicon / Windows ARM) = NEON float64x2_t (aarch64 で常に利用可能)
//   - それ以外 (Emscripten WASM 等) = スカラ fallback (= 演算内容は完全に同一)
//   - FMA は使わない (mul / add を分離)。スカラコードも MSVC x64 既定 (/fp:precise,
//     SSE2) では FMA contraction されないため、レーンごとの演算結果はスカラ実装と
//     **bit 一致** する (tests/test_ladder_golden.cpp で検証)。
//   - 演算子は要素ごとの IEEE 演算そのもの。丸め順を変える水平演算は提供しない。

#pragma once

#if defined(__SSE2__) || defined(_M_X64) || defined(__x86_64__) || \
    (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
  #define S80_SIMD_SSE2 1
  #include <emmintrin.h>
#elif defined(__aarch64__) || defined(_M_ARM64)
  #define S80_SIMD_NEON 1
  #include <arm_neon.h>
#endif

// AVX2 (4-lane double) はコンパイル可能性とランタイム可用性が別 (2013 年以降の
// Intel / Zen 以降の AMD のみ。Apple Silicon には存在しない)。x86-64 ネイティブ
// ビルドでのみコンパイルし、実行時に cpuHasAvx2() でディスパッチする。
// clang/gcc は AVX2 命令を含む関数に target attribute が必要 (グローバルな
// -mavx2 を立てると非対応 CPU でクラッシュするため関数単位で付ける)。
#if defined(S80_SIMD_SSE2) && (defined(_M_X64) || defined(__x86_64__)) && \
    !defined(__EMSCRIPTEN__)
  #define S80_SIMD_AVX2_COMPILE 1
  #include <immintrin.h>
  #if defined(_MSC_VER) && !defined(__clang__)
    #include <intrin.h>
    #define S80_TARGET_AVX2
  #else
    #define S80_TARGET_AVX2 __attribute__((target("avx2")))
  #endif
#endif

namespace dx10::dsp {

#if defined(S80_SIMD_SSE2)

struct F64x2 {
  __m128d v;
  static F64x2 set(double lane0, double lane1) noexcept {
    return {_mm_set_pd(lane1, lane0)};
  }
  static F64x2 broadcast(double x) noexcept { return {_mm_set1_pd(x)}; }
  double lane0() const noexcept { return _mm_cvtsd_f64(v); }
  double lane1() const noexcept { return _mm_cvtsd_f64(_mm_unpackhi_pd(v, v)); }
  friend F64x2 operator+(F64x2 a, F64x2 b) noexcept { return {_mm_add_pd(a.v, b.v)}; }
  friend F64x2 operator-(F64x2 a, F64x2 b) noexcept { return {_mm_sub_pd(a.v, b.v)}; }
  friend F64x2 operator*(F64x2 a, F64x2 b) noexcept { return {_mm_mul_pd(a.v, b.v)}; }
  friend F64x2 operator/(F64x2 a, F64x2 b) noexcept { return {_mm_div_pd(a.v, b.v)}; }
  /** 要素ごとの a > b。真レーン = 全 bit 1 のマスクを返す。 */
  friend F64x2 cmpGt(F64x2 a, F64x2 b) noexcept { return {_mm_cmpgt_pd(a.v, b.v)}; }
  /** mask が真のレーンは a、偽のレーンは b (bit-blend、NaN 透過)。 */
  static F64x2 select(F64x2 mask, F64x2 a, F64x2 b) noexcept {
    return {_mm_or_pd(_mm_and_pd(mask.v, a.v), _mm_andnot_pd(mask.v, b.v))};
  }
};

#elif defined(S80_SIMD_NEON)

struct F64x2 {
  float64x2_t v;
  static F64x2 set(double lane0, double lane1) noexcept {
    const double tmp[2] = {lane0, lane1};
    return {vld1q_f64(tmp)};
  }
  static F64x2 broadcast(double x) noexcept { return {vdupq_n_f64(x)}; }
  double lane0() const noexcept { return vgetq_lane_f64(v, 0); }
  double lane1() const noexcept { return vgetq_lane_f64(v, 1); }
  friend F64x2 operator+(F64x2 a, F64x2 b) noexcept { return {vaddq_f64(a.v, b.v)}; }
  friend F64x2 operator-(F64x2 a, F64x2 b) noexcept { return {vsubq_f64(a.v, b.v)}; }
  friend F64x2 operator*(F64x2 a, F64x2 b) noexcept { return {vmulq_f64(a.v, b.v)}; }
  friend F64x2 operator/(F64x2 a, F64x2 b) noexcept { return {vdivq_f64(a.v, b.v)}; }
  friend F64x2 cmpGt(F64x2 a, F64x2 b) noexcept {
    return {vreinterpretq_f64_u64(vcgtq_f64(a.v, b.v))};
  }
  static F64x2 select(F64x2 mask, F64x2 a, F64x2 b) noexcept {
    return {vbslq_f64(vreinterpretq_u64_f64(mask.v), a.v, b.v)};
  }
};

#else  // スカラ fallback (WASM 等)。演算内容は SIMD 版とレーン単位で同一。

struct F64x2 {
  double d0, d1;
  static F64x2 set(double lane0, double lane1) noexcept { return {lane0, lane1}; }
  static F64x2 broadcast(double x) noexcept { return {x, x}; }
  double lane0() const noexcept { return d0; }
  double lane1() const noexcept { return d1; }
  friend F64x2 operator+(F64x2 a, F64x2 b) noexcept {
    return {a.d0 + b.d0, a.d1 + b.d1};
  }
  friend F64x2 operator-(F64x2 a, F64x2 b) noexcept {
    return {a.d0 - b.d0, a.d1 - b.d1};
  }
  friend F64x2 operator*(F64x2 a, F64x2 b) noexcept {
    return {a.d0 * b.d0, a.d1 * b.d1};
  }
  friend F64x2 operator/(F64x2 a, F64x2 b) noexcept {
    return {a.d0 / b.d0, a.d1 / b.d1};
  }
  friend F64x2 cmpGt(F64x2 a, F64x2 b) noexcept {
    // mask 表現: 真 = 1.0 / 偽 = 0.0 (select でしか使わない)。
    return {a.d0 > b.d0 ? 1.0 : 0.0, a.d1 > b.d1 ? 1.0 : 0.0};
  }
  static F64x2 select(F64x2 mask, F64x2 a, F64x2 b) noexcept {
    return {mask.d0 != 0.0 ? a.d0 : b.d0, mask.d1 != 0.0 ? a.d1 : b.d1};
  }
};

#endif

#if defined(S80_SIMD_AVX2_COMPILE)

/** AVX2 + OS の YMM state 保存 (OSXSAVE/XCR0) を実行時検出する。 */
inline bool cpuHasAvx2() noexcept {
#if defined(_MSC_VER) && !defined(__clang__)
  int info[4];
  __cpuid(info, 0);
  if (info[0] < 7) return false;
  __cpuid(info, 1);
  if ((info[2] & (1 << 27)) == 0) return false;  // OSXSAVE
  if ((info[2] & (1 << 28)) == 0) return false;  // AVX
  if ((_xgetbv(0) & 0x6) != 0x6) return false;   // XMM + YMM state enabled
  __cpuidex(info, 7, 0);
  return (info[1] & (1 << 5)) != 0;              // AVX2
#else
  return __builtin_cpu_supports("avx2");
#endif
}

// 4-lane double (AVX2)。F64x2 と同じ方針: FMA 不使用、要素ごとの IEEE 演算のみ
// (= レーンごとの結果はスカラ実装と bit 一致)。利用側の関数にも S80_TARGET_AVX2
// を付け、cpuHasAvx2() でガードして呼ぶこと。
struct F64x4 {
  __m256d v;
  S80_TARGET_AVX2 static F64x4 broadcast(double x) noexcept {
    return {_mm256_set1_pd(x)};
  }
  S80_TARGET_AVX2 static F64x4 set(double l0, double l1, double l2,
                                   double l3) noexcept {
    return {_mm256_set_pd(l3, l2, l1, l0)};
  }
  S80_TARGET_AVX2 double lane(int i) const noexcept {
    alignas(32) double tmp[4];
    _mm256_store_pd(tmp, v);
    return tmp[i];
  }
  S80_TARGET_AVX2 friend F64x4 operator+(F64x4 a, F64x4 b) noexcept {
    return {_mm256_add_pd(a.v, b.v)};
  }
  S80_TARGET_AVX2 friend F64x4 operator-(F64x4 a, F64x4 b) noexcept {
    return {_mm256_sub_pd(a.v, b.v)};
  }
  S80_TARGET_AVX2 friend F64x4 operator*(F64x4 a, F64x4 b) noexcept {
    return {_mm256_mul_pd(a.v, b.v)};
  }
  S80_TARGET_AVX2 friend F64x4 operator/(F64x4 a, F64x4 b) noexcept {
    return {_mm256_div_pd(a.v, b.v)};
  }
  /** 要素ごとの a > b (ordered, quiet = スカラ `>` と同値)。 */
  S80_TARGET_AVX2 friend F64x4 cmpGt(F64x4 a, F64x4 b) noexcept {
    return {_mm256_cmp_pd(a.v, b.v, _CMP_GT_OQ)};
  }
  S80_TARGET_AVX2 static F64x4 select(F64x4 mask, F64x4 a, F64x4 b) noexcept {
    return {_mm256_blendv_pd(b.v, a.v, mask.v)};
  }
};

#endif  // S80_SIMD_AVX2_COMPILE

}  // namespace dx10::dsp

#pragma once

#include "openwow/data/model/m2_model.h"
#include "openwow/render/api/math/render_math_types.h"
#include "openwow/render/api/math/render_matrix_math.h"

#include <bx/math.h>
#include <bx/simd_t.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace openwow::render::m2::animation_math {

inline constexpr float kCompactQuaternionScale = 2.0f / 65535.0f;

struct alignas(16) M2Float4 {
  float lane[4];
};
static_assert(sizeof(M2Float4) == 16u, "M2Float4 must be exactly one SIMD register wide.");

struct M2BonePose {
  M2Float4 translation{{0.0f, 0.0f, 0.0f, 0.0f}};
  M2Float4 rotation{{0.0f, 0.0f, 0.0f, 1.0f}};
  M2Float4 scale{{1.0f, 1.0f, 1.0f, 0.0f}};
};

struct alignas(16) M2Matrix4x4 {
  float m[16];
};
static_assert(sizeof(M2Matrix4x4) == 16u * sizeof(float),
              "M2Matrix4x4 must be layout-compatible with RenderMatrix4x4.");

enum class M2MathBackend {

  kReference,

  kVectorized,
};

#if !defined(OPENWOW_M2_ANIMATION_MATH_VECTORIZE)
#  if BX_SIMD_SUPPORTED
#    define OPENWOW_M2_ANIMATION_MATH_VECTORIZE 1
#  else
#    define OPENWOW_M2_ANIMATION_MATH_VECTORIZE 0
#  endif
#endif

inline constexpr M2MathBackend kM2AnimationMathBackend =
    OPENWOW_M2_ANIMATION_MATH_VECTORIZE != 0 ? M2MathBackend::kVectorized
                                             : M2MathBackend::kReference;

namespace detail {

[[nodiscard]] inline bx::simd128_t LoadFloat4(const float* const source) noexcept {
#if BX_SIMD_SSE
  return _mm_loadu_ps(source);
#else
  return bx::simd_ld(static_cast<const void*>(source));
#endif
}

inline void StoreFloat4(float* const destination, const bx::simd128_t value) noexcept {
#if BX_SIMD_SSE
  _mm_storeu_ps(destination, value);
#else
  bx::simd_st(static_cast<void*>(destination), value);
#endif
}

[[nodiscard]] inline bx::simd128_t SingleProductMultiplyAdd(
    const bx::simd128_t a, const bx::simd128_t b, const bx::simd128_t c) noexcept {
#if BX_SIMD_NEON && defined(__ARM_FEATURE_FMA)
  return vfmaq_f32(c, a, b);
#elif BX_SIMD_SSE && defined(__FMA__)
  return _mm_fmadd_ps(a, b, c);
#else
  return bx::simd_madd(a, b, c);
#endif
}

[[nodiscard]] inline bx::simd128_t XyzLaneMask() noexcept {
  return bx::simd_ild(0xffffffffu, 0xffffffffu, 0xffffffffu, 0x00000000u);
}

[[nodiscard]] inline bx::simd128_t SignBitMask() noexcept {
  return bx::simd_isplat(0x80000000u);
}

}

template <M2MathBackend Backend>
struct M2AnimationKernels;

template <>
struct M2AnimationKernels<M2MathBackend::kReference> {
  static constexpr const char* kBackendName = "reference";

  [[nodiscard]] static M2Float4 DecodeCompactQuaternion(
      const openwow::data::model::M2Quat16& value) noexcept {
    const auto decode = [](const std::int16_t raw) {
      return static_cast<float>(static_cast<std::uint16_t>(raw)) * kCompactQuaternionScale - 1.0f;
    };
    return M2Float4{{decode(value.x), decode(value.y), decode(value.z), decode(value.w)}};
  }

  [[nodiscard]] static M2Float4 WeightedSum(const M2Float4& a, const M2Float4& b,
                                            const float a_weight,
                                            const float b_weight) noexcept {
    return M2Float4{{
        a.lane[0] * a_weight + b.lane[0] * b_weight,
        a.lane[1] * a_weight + b.lane[1] * b_weight,
        a.lane[2] * a_weight + b.lane[2] * b_weight,
        a.lane[3] * a_weight + b.lane[3] * b_weight,
    }};
  }

  [[nodiscard]] static M2Float4 Negate(const M2Float4& a) noexcept {
    return M2Float4{{-a.lane[0], -a.lane[1], -a.lane[2], -a.lane[3]}};
  }

  [[nodiscard]] static M2Float4 LerpVec3FromTrack(const float* const components,
                                                  const std::size_t index,
                                                  const float alpha) noexcept {
    const float* const first = components + index * 3u;
    const float* const second = first + 3u;
    return M2Float4{{
        first[0] + (second[0] - first[0]) * alpha,
        first[1] + (second[1] - first[1]) * alpha,
        first[2] + (second[2] - first[2]) * alpha,
        0.0f,
    }};
  }

  static void MultiplyMatrix(const float* const lhs, const float* const rhs,
                             float* const out) noexcept {
    const RenderMatrix4x4 product = openwow::render::MultiplyMatrix4x4(
        RenderMatrix4x4View{lhs, 16u}, RenderMatrix4x4View{rhs, 16u});
    std::memcpy(out, product.data(), sizeof(product));
  }

  static void BuildBoneLocalMatrix(const M2Float4& pivot, const M2BonePose& pose,
                                   const float* const basis_override,
                                   float* const out) noexcept {
    RenderMatrix4x4 matrix = openwow::render::BuildRotationMatrix4x4Quaternion(
        RenderVec4{pose.rotation.lane[0], pose.rotation.lane[1], pose.rotation.lane[2],
                   pose.rotation.lane[3]});
    matrix = openwow::render::ScaleMatrix4x4BasisRows(
        matrix, RenderVec3{pose.scale.lane[0], pose.scale.lane[1], pose.scale.lane[2]});
    if (basis_override != nullptr) {
      matrix = openwow::render::MultiplyMatrix4x4(RenderMatrix4x4View{matrix},
                                                  RenderMatrix4x4View{basis_override, 16u});
    }

    const RenderVec3 rs_pivot{
        matrix[0] * pivot.lane[0] + matrix[4] * pivot.lane[1] + matrix[8] * pivot.lane[2],
        matrix[1] * pivot.lane[0] + matrix[5] * pivot.lane[1] + matrix[9] * pivot.lane[2],
        matrix[2] * pivot.lane[0] + matrix[6] * pivot.lane[1] + matrix[10] * pivot.lane[2],
    };

    matrix[12] = pivot.lane[0] + pose.translation.lane[0] - rs_pivot[0];
    matrix[13] = pivot.lane[1] + pose.translation.lane[1] - rs_pivot[1];
    matrix[14] = pivot.lane[2] + pose.translation.lane[2] - rs_pivot[2];
    matrix[15] = 1.0f;
    std::memcpy(out, matrix.data(), sizeof(matrix));
  }
};

template <>
struct M2AnimationKernels<M2MathBackend::kVectorized> {
  static constexpr const char* kBackendName = "vectorized";

  [[nodiscard]] static M2Float4 DecodeCompactQuaternion(
      const openwow::data::model::M2Quat16& value) noexcept {

#if BX_SIMD_NEON
    const uint16x4_t raw = vreinterpret_u16_s16(vld1_s16(&value.x));
    const bx::simd128_t lanes = vcvtq_f32_u32(vmovl_u16(raw));
#elif BX_SIMD_SSE
    const __m128i raw = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(&value.x));
    const bx::simd128_t lanes =
        _mm_cvtepi32_ps(_mm_unpacklo_epi16(raw, _mm_setzero_si128()));
#else
    const bx::simd128_t lanes = bx::simd_itof(bx::simd_ild(
        static_cast<std::uint32_t>(static_cast<std::uint16_t>(value.x)),
        static_cast<std::uint32_t>(static_cast<std::uint16_t>(value.y)),
        static_cast<std::uint32_t>(static_cast<std::uint16_t>(value.z)),
        static_cast<std::uint32_t>(static_cast<std::uint16_t>(value.w))));
#endif

    M2Float4 out;
    bx::simd_st(out.lane,
                detail::SingleProductMultiplyAdd(lanes,
                                                 bx::simd_splat(kCompactQuaternionScale),
                                                 bx::simd_splat(-1.0f)));
    return out;
  }

  [[nodiscard]] static BX_FORCE_INLINE M2Float4 WeightedSum(
      const M2Float4& a, const M2Float4& b, const float a_weight,
      const float b_weight) noexcept {
    return M2Float4{{
        a.lane[0] * a_weight + b.lane[0] * b_weight,
        a.lane[1] * a_weight + b.lane[1] * b_weight,
        a.lane[2] * a_weight + b.lane[2] * b_weight,
        a.lane[3] * a_weight + b.lane[3] * b_weight,
    }};
  }

  [[nodiscard]] static M2Float4 Negate(const M2Float4& a) noexcept {
    M2Float4 out;
    bx::simd_st(out.lane, bx::simd_xor(bx::simd_ld(a.lane), detail::SignBitMask()));
    return out;
  }

  [[nodiscard]] static M2Float4 LerpVec3FromTrack(const float* const components,
                                                  const std::size_t index,
                                                  const float alpha) noexcept {

    const float* const base = components + index * 3u;
    const bx::simd128_t first = detail::LoadFloat4(base);
    const bx::simd128_t second = bx::simd_swiz_yzwx(detail::LoadFloat4(base + 2u));
    const bx::simd128_t delta = bx::simd_sub(second, first);

    const bx::simd128_t lerped =
        detail::SingleProductMultiplyAdd(delta, bx::simd_splat(alpha), first);

    M2Float4 out;
    bx::simd_st(out.lane, bx::simd_and(lerped, detail::XyzLaneMask()));
    return out;
  }

  static BX_FORCE_INLINE void MultiplyMatrix(const float* const lhs, const float* const rhs,
                                             float* const out) noexcept {
    float product[16];
    product[0] = rhs[0] * lhs[0] + rhs[4] * lhs[1] +
                 rhs[8] * lhs[2] + rhs[12] * lhs[3];
    product[1] = rhs[1] * lhs[0] + rhs[5] * lhs[1] +
                 rhs[9] * lhs[2] + rhs[13] * lhs[3];
    product[2] = rhs[2] * lhs[0] + rhs[6] * lhs[1] +
                 rhs[10] * lhs[2] + rhs[14] * lhs[3];
    product[3] = lhs[0] * rhs[3] + lhs[1] * rhs[7] +
                 lhs[2] * rhs[11] + lhs[3] * rhs[15];
    product[4] = rhs[0] * lhs[4] + rhs[4] * lhs[5] +
                 rhs[8] * lhs[6] + rhs[12] * lhs[7];
    product[5] = rhs[1] * lhs[4] + rhs[5] * lhs[5] +
                 rhs[9] * lhs[6] + rhs[13] * lhs[7];
    product[6] = rhs[2] * lhs[4] + rhs[6] * lhs[5] +
                 rhs[10] * lhs[6] + rhs[14] * lhs[7];
    product[7] = lhs[4] * rhs[3] + rhs[7] * lhs[5] +
                 rhs[11] * lhs[6] + rhs[15] * lhs[7];
    product[8] = rhs[0] * lhs[8] + rhs[4] * lhs[9] +
                 rhs[8] * lhs[10] + rhs[12] * lhs[11];
    product[9] = rhs[1] * lhs[8] + rhs[5] * lhs[9] +
                 rhs[9] * lhs[10] + rhs[13] * lhs[11];
    product[10] = rhs[2] * lhs[8] + rhs[6] * lhs[9] +
                  rhs[10] * lhs[10] + rhs[14] * lhs[11];
    product[11] = lhs[8] * rhs[3] + rhs[7] * lhs[9] +
                  rhs[11] * lhs[10] + rhs[15] * lhs[11];
    product[12] = lhs[12] * rhs[0] + lhs[13] * rhs[4] +
                  lhs[14] * rhs[8] + lhs[15] * rhs[12];
    product[13] = lhs[12] * rhs[1] + lhs[13] * rhs[5] +
                  lhs[14] * rhs[9] + lhs[15] * rhs[13];
    product[14] = lhs[12] * rhs[2] + lhs[13] * rhs[6] +
                  lhs[14] * rhs[10] + lhs[15] * rhs[14];
    product[15] = lhs[12] * rhs[3] + lhs[13] * rhs[7] +
                  lhs[14] * rhs[11] + lhs[15] * rhs[15];
    std::memcpy(out, product, sizeof(product));
  }

  static BX_FORCE_INLINE void BuildBoneLocalMatrix(const M2Float4& pivot,
                                                   const M2BonePose& pose,
                                                   const float* const basis_override,
                                                   float* const out) noexcept {
    const float x = pose.rotation.lane[0];
    const float y = pose.rotation.lane[1];
    const float z = pose.rotation.lane[2];
    const float w = pose.rotation.lane[3];
    const float two_y = y + y;
    const float two_wx = (x + x) * w;
    const float two_z = z + z;
    const float two_xx = (x + x) * x;

    float matrix[16];
    matrix[0] = 1.0f - (two_y * y + z * two_z);
    matrix[1] = w * two_z + x * two_y;
    matrix[2] = x * two_z - two_y * w;
    matrix[3] = 0.0f;
    matrix[4] = x * two_y - w * two_z;
    matrix[5] = 1.0f - (z * two_z + two_xx);
    matrix[6] = two_wx + y * two_z;
    matrix[7] = 0.0f;
    matrix[8] = two_y * w + x * two_z;
    matrix[9] = y * two_z - two_wx;
    matrix[10] = 1.0f - (two_y * y + two_xx);
    matrix[11] = 0.0f;
    matrix[12] = 0.0f;
    matrix[13] = 0.0f;
    matrix[14] = 0.0f;
    matrix[15] = 1.0f;

    const float scale_x = pose.scale.lane[0];
    const float scale_y = pose.scale.lane[1];
    const float scale_z = pose.scale.lane[2];
    matrix[0] = matrix[0] * scale_x;
    matrix[1] = matrix[1] * scale_x;
    matrix[2] = matrix[2] * scale_x;
    matrix[4] = matrix[4] * scale_y;
    matrix[5] = matrix[5] * scale_y;
    matrix[6] = matrix[6] * scale_y;
    matrix[8] = matrix[8] * scale_z;
    matrix[9] = matrix[9] * scale_z;
    matrix[10] = matrix[10] * scale_z;

    if (basis_override != nullptr) {
      MultiplyMatrix(matrix, basis_override, matrix);
    }

    const float pivot_x = pivot.lane[0];
    const float pivot_y = pivot.lane[1];
    const float pivot_z = pivot.lane[2];
    const float rs_pivot_0 =
        matrix[0] * pivot_x + matrix[4] * pivot_y + matrix[8] * pivot_z;
    const float rs_pivot_1 =
        matrix[1] * pivot_x + matrix[5] * pivot_y + matrix[9] * pivot_z;
    const float rs_pivot_2 =
        matrix[2] * pivot_x + matrix[6] * pivot_y + matrix[10] * pivot_z;

    matrix[12] = pivot_x + pose.translation.lane[0] - rs_pivot_0;
    matrix[13] = pivot_y + pose.translation.lane[1] - rs_pivot_1;
    matrix[14] = pivot_z + pose.translation.lane[2] - rs_pivot_2;
    matrix[15] = 1.0f;
    std::memcpy(out, matrix, sizeof(matrix));
  }
};

using M2Kernels = M2AnimationKernels<kM2AnimationMathBackend>;

template <M2MathBackend Backend>
[[nodiscard]] inline M2Float4 NormalizeQuaternion(const M2Float4& quaternion) noexcept {
  const bx::Quaternion normalized = bx::normalize(bx::Quaternion{
      quaternion.lane[0], quaternion.lane[1], quaternion.lane[2], quaternion.lane[3]});
  return M2Float4{{normalized.x, normalized.y, normalized.z, normalized.w}};
}

template <M2MathBackend Backend>
[[nodiscard]] inline M2Float4 NlerpQuaternion(const M2Float4& a, const M2Float4& b,
                                              const float t) noexcept {
  const float inv_t = 1.0f - t;
  return NormalizeQuaternion<Backend>(
      M2AnimationKernels<Backend>::WeightedSum(a, b, inv_t, t));
}

template <M2MathBackend Backend>
[[nodiscard]] inline M2Float4 SlerpQuaternion(const M2Float4& a, M2Float4 b,
                                              const float t) noexcept {
  float dot = a.lane[0] * b.lane[0] + a.lane[1] * b.lane[1] + a.lane[2] * b.lane[2] +
              a.lane[3] * b.lane[3];
  if (dot < 0.0f) {
    b = M2AnimationKernels<Backend>::Negate(b);
    dot = -dot;
  }
  if (dot > 0.9995f) {
    return NlerpQuaternion<Backend>(a, b, t);
  }

  dot = std::clamp(dot, -1.0f, 1.0f);
  const float theta = std::acos(dot);
  const float sin_theta = std::sin(theta);
  if (std::abs(sin_theta) < 1e-6f) {
    return NlerpQuaternion<Backend>(a, b, t);
  }

  const float source_weight = std::sin((1.0f - t) * theta) / sin_theta;
  const float target_weight = std::sin(t * theta) / sin_theta;
  return NormalizeQuaternion<Backend>(
      M2AnimationKernels<Backend>::WeightedSum(a, b, source_weight, target_weight));
}

template <M2MathBackend Backend>
[[nodiscard]] inline M2BonePose BlendBonePose(const M2BonePose& source,
                                              const M2BonePose& target,
                                              const float blend_factor) noexcept {
  const float t = std::clamp(blend_factor, 0.0f, 1.0f);
  const float source_weight = 1.0f - t;
  return M2BonePose{
      .translation = M2AnimationKernels<Backend>::WeightedSum(source.translation,
                                                              target.translation,
                                                              source_weight, t),
      .rotation = SlerpQuaternion<Backend>(source.rotation, target.rotation, t),
      .scale = M2AnimationKernels<Backend>::WeightedSum(source.scale, target.scale,
                                                        source_weight, t),
  };
}

}

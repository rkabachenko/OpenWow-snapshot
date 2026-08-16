#include "openwow/game/character_component_bc1_sse.h"

#if defined(__SSE2__)
#include <emmintrin.h>
#elif defined(_MSC_VER)
#include <intrin.h>
#endif

#include <cstdint>
#include <cstring>

namespace openwow::game {

#if defined(__SSE2__) || (defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86)))
#define OPENWOW_HAS_SSE2 1
#else
#define OPENWOW_HAS_SSE2 0
#endif

#if OPENWOW_HAS_SSE2

static inline std::uint16_t PackBgraToRgb565(std::uint32_t bgra) {
  return static_cast<std::uint16_t>(
      ((bgra & 0xF8u)
       | (((bgra & 0xFC00u) | ((bgra >> 3) & 0x1F0000u)) >> 2))
      >> 3);
}

static inline std::uint32_t PackFourSelectors(std::uint32_t raw) {
  return (raw & 0x3u)
       | (((raw & 0x300u)
           | (((raw & 0x30000u) | ((raw >> 6) & 0xC0000u)) >> 6)) >> 6);
}

void EncodeCharacterComponentBc1BlockSse(const void* pixels,
                                         std::int32_t stride,
                                         void* output) {
  const auto* src = static_cast<const std::uint8_t*>(pixels);
  auto* dst = static_cast<std::uint32_t*>(output);

  const __m128i row0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src));
  const __m128i row1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + stride));
  const __m128i row2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + 2 * stride));
  const __m128i row3 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + 3 * stride));

  const __m128i zero = _mm_setzero_si128();

  __m128i sum16 = _mm_add_epi16(_mm_unpacklo_epi8(row0, zero),
                                _mm_unpackhi_epi8(row0, zero));
  sum16 = _mm_add_epi16(sum16, _mm_unpacklo_epi8(row1, zero));
  sum16 = _mm_add_epi16(sum16, _mm_unpackhi_epi8(row1, zero));
  sum16 = _mm_add_epi16(sum16, _mm_unpacklo_epi8(row2, zero));
  sum16 = _mm_add_epi16(sum16, _mm_unpackhi_epi8(row2, zero));
  sum16 = _mm_add_epi16(sum16, _mm_unpacklo_epi8(row3, zero));
  sum16 = _mm_add_epi16(sum16, _mm_unpackhi_epi8(row3, zero));

  __m128i avg16 = _mm_add_epi16(sum16, _mm_shuffle_epi32(sum16, 0x4E));
  avg16 = _mm_srli_epi16(avg16, 4);

  const __m128 inv255 = _mm_set1_ps(1.0f / 255.0f);
  const __m128 avg_f = _mm_mul_ps(
      _mm_cvtepi32_ps(_mm_unpacklo_epi16(avg16, zero)), inv255);

  __m128 cov_b = _mm_setzero_ps();
  __m128 cov_g = _mm_setzero_ps();
  __m128 cov_r = _mm_setzero_ps();

  const __m128i* row_ptr = reinterpret_cast<const __m128i*>(src);
  for (int row_idx = 0; row_idx < 4; ++row_idx) {
    const __m128i px = _mm_loadu_si128(row_ptr);
    const __m128i lo16 = _mm_unpacklo_epi8(px, zero);
    const __m128i hi16 = _mm_unpackhi_epi8(px, zero);

    const __m128i ch[4] = {
        _mm_unpackhi_epi16(hi16, zero),
        _mm_unpacklo_epi16(hi16, zero),
        _mm_unpackhi_epi16(lo16, zero),
        _mm_unpacklo_epi16(lo16, zero),
    };

    for (int p = 0; p < 4; ++p) {
      const __m128 c = _mm_sub_ps(
          _mm_mul_ps(_mm_cvtepi32_ps(ch[p]), inv255), avg_f);
      cov_b = _mm_add_ps(cov_b, _mm_mul_ps(_mm_shuffle_ps(c, c, 0x00), c));
      cov_g = _mm_add_ps(cov_g, _mm_mul_ps(_mm_shuffle_ps(c, c, 0x55), c));
      cov_r = _mm_add_ps(cov_r, _mm_mul_ps(_mm_shuffle_ps(c, c, 0xAA), c));
    }

    row_ptr = reinterpret_cast<const __m128i*>(
        reinterpret_cast<const std::uint8_t*>(row_ptr) + stride);
  }

  __m128 axis;
  {
    __m128 t = _mm_setzero_ps();
    t = _mm_move_ss(t, _mm_set_ss(1.0f));
    axis = _mm_shuffle_ps(t, t, 0x00);
  }

  const __m128 fzero = _mm_setzero_ps();
  for (int iter = 0; iter < 8; ++iter) {
    __m128 next = _mm_add_ps(
        _mm_add_ps(
            _mm_mul_ps(_mm_shuffle_ps(axis, axis, 0xAA), cov_r),
            _mm_mul_ps(_mm_shuffle_ps(axis, axis, 0x55), cov_g)),
        _mm_mul_ps(_mm_shuffle_ps(axis, axis, 0x00), cov_b));

    __m128 mx = _mm_max_ps(next, _mm_shuffle_ps(next, next, 0xB1));
    mx = _mm_max_ps(mx, _mm_shuffle_ps(mx, mx, 0x4E));

    axis = _mm_mul_ps(
        _mm_and_ps(_mm_cmpneq_ps(fzero, mx), _mm_rcp_ps(mx)), next);
  }

  {
    __m128 sq = _mm_mul_ps(axis, axis);
    __m128 dot = _mm_add_ps(sq, _mm_shuffle_ps(sq, sq, 0xB1));
    dot = _mm_add_ps(dot, _mm_shuffle_ps(dot, dot, 0x4E));
    axis = _mm_and_ps(
        _mm_mul_ps(_mm_rsqrt_ps(dot), axis),
        _mm_cmpneq_ps(fzero, dot));
  }

  __m128 proj_store[4];
  __m128 proj_min, proj_max;
  {
    __m128 t = _mm_setzero_ps();
    t = _mm_move_ss(t, _mm_set_ss(1.0f));
    proj_min = t;
    t = _mm_setzero_ps();
    t = _mm_move_ss(t, _mm_set_ss(-1.0f));
    proj_max = t;
  }

  const __m128 avg_b = _mm_shuffle_ps(avg_f, avg_f, 0x00);
  const __m128 avg_g = _mm_shuffle_ps(avg_f, avg_f, 0x55);
  const __m128 avg_r = _mm_shuffle_ps(avg_f, avg_f, 0xAA);
  const __m128 ax_b = _mm_shuffle_ps(axis, axis, 0x00);
  const __m128 ax_g = _mm_shuffle_ps(axis, axis, 0x55);
  const __m128 ax_r = _mm_shuffle_ps(axis, axis, 0xAA);

  const __m128i* proj_row_ptr = reinterpret_cast<const __m128i*>(src);
  for (int ri = 0; ri < 4; ++ri) {
    const __m128i px = _mm_loadu_si128(proj_row_ptr);

    const __m128i s8d = _mm_shuffle_epi32(px, 0x8D);
    const __m128i sd8 = _mm_shuffle_epi32(px, 0xD8);
    const __m128i il = _mm_unpackhi_epi8(s8d, sd8);
    const __m128i tr = _mm_unpackhi_epi16(_mm_slli_si128(il, 8), il);

    const __m128i bg16 = _mm_unpacklo_epi8(tr, zero);
    const __m128 bf = _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpacklo_epi16(bg16, zero)), inv255);
    const __m128 gf = _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpackhi_epi16(bg16, zero)), inv255);
    const __m128i ra16 = _mm_unpackhi_epi8(tr, zero);
    const __m128 rf = _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpacklo_epi16(ra16, zero)), inv255);

    __m128 proj = _mm_mul_ps(_mm_sub_ps(bf, avg_b), ax_b);
    proj = _mm_add_ps(proj, _mm_mul_ps(_mm_sub_ps(gf, avg_g), ax_g));
    proj = _mm_add_ps(proj, _mm_mul_ps(_mm_sub_ps(rf, avg_r), ax_r));

    proj_store[ri] = proj;
    proj_min = _mm_min_ps(proj_min, proj);
    proj_max = _mm_max_ps(proj_max, proj);

    proj_row_ptr = reinterpret_cast<const __m128i*>(
        reinterpret_cast<const std::uint8_t*>(proj_row_ptr) + stride);
  }

  {
    __m128 mn = _mm_min_ps(proj_min, _mm_shuffle_ps(proj_min, proj_min, 0xB1));
    proj_min = _mm_min_ps(mn, _mm_shuffle_ps(mn, mn, 0x4E));
    __m128 mx = _mm_max_ps(proj_max, _mm_shuffle_ps(proj_max, proj_max, 0xB1));
    proj_max = _mm_max_ps(mx, _mm_shuffle_ps(mx, mx, 0x4E));
  }

  const __m128 f255 = _mm_set1_ps(255.0f);

  const __m128i ep_min_i = _mm_cvtps_epi32(
      _mm_mul_ps(_mm_add_ps(_mm_mul_ps(proj_min, axis), avg_f), f255));
  const __m128i ep_max_i = _mm_cvtps_epi32(
      _mm_mul_ps(_mm_add_ps(_mm_mul_ps(proj_max, axis), avg_f), f255));

  const __m128i ep16 = _mm_packs_epi32(ep_min_i, ep_max_i);

  const std::uint32_t min_bgra = static_cast<std::uint32_t>(
      _mm_cvtsi128_si32(_mm_packus_epi16(ep16, zero)));
  const std::uint32_t max_bgra = static_cast<std::uint32_t>(
      _mm_cvtsi128_si32(_mm_packus_epi16(_mm_srli_si128(ep16, 8), zero)));

  std::uint16_t color0 = PackBgraToRgb565(max_bgra);
  std::uint16_t color1 = PackBgraToRgb565(min_bgra);

  std::uint32_t selectors = 0;

  if (color0 != color1) {
    const __m128 scale = _mm_div_ps(
        _mm_set1_ps(3.0f), _mm_sub_ps(proj_max, proj_min));

    const __m128i q01 = _mm_packs_epi32(
        _mm_cvtps_epi32(_mm_mul_ps(_mm_sub_ps(proj_store[0], proj_min), scale)),
        _mm_cvtps_epi32(_mm_mul_ps(_mm_sub_ps(proj_store[1], proj_min), scale)));
    const __m128i q23 = _mm_packs_epi32(
        _mm_cvtps_epi32(_mm_mul_ps(_mm_sub_ps(proj_store[2], proj_min), scale)),
        _mm_cvtps_epi32(_mm_mul_ps(_mm_sub_ps(proj_store[3], proj_min), scale)));

    const __m128i one16 = _mm_set1_epi16(1);
    const __m128i s01 = _mm_add_epi16(
        q01, _mm_srli_epi16(_mm_add_epi16(q01, one16), 1));
    const __m128i s23 = _mm_add_epi16(
        q23, _mm_srli_epi16(_mm_add_epi16(q23, one16), 1));

    const __m128i sel8 = _mm_packs_epi16(s01, s23);

    const std::uint32_t g0 = static_cast<std::uint32_t>(_mm_cvtsi128_si32(sel8));
    const __m128i t4 = _mm_srli_si128(sel8, 4);
    const std::uint32_t g1 = static_cast<std::uint32_t>(_mm_cvtsi128_si32(t4));
    const __m128i t8 = _mm_srli_si128(t4, 4);
    const std::uint32_t g2 = static_cast<std::uint32_t>(_mm_cvtsi128_si32(t8));
    const __m128i t12 = _mm_srli_si128(t8, 4);
    const std::uint32_t g3 = static_cast<std::uint32_t>(_mm_cvtsi128_si32(t12));

    selectors = (PackFourSelectors(g0) & 0xFFu)
              | ((PackFourSelectors(g1) & 0xFFu) << 8)
              | ((PackFourSelectors(g2) & 0xFFu) << 16)
              | ((PackFourSelectors(g3) & 0xFFu) << 24);

    selectors ^= 0x55555555u;

    if (color0 < color1) {
      const std::uint16_t tmp = color0;
      color0 = color1;
      color1 = tmp;
      selectors ^= 0x55555555u;
    }
  }

  dst[0] = static_cast<std::uint32_t>(color0) | (static_cast<std::uint32_t>(color1) << 16);
  dst[1] = selectors;
}

#else

void EncodeCharacterComponentBc1BlockSse(const void* ,
                                         std::int32_t ,
                                         void* ) {

}

#endif

bool Bc1SseEncoderAvailable() noexcept {
#if OPENWOW_HAS_SSE2
  return true;
#else
  return false;
#endif
}

}

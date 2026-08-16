
#pragma once

#include <array>
#include <bit>
#include <cstdint>

namespace openwow::math::exp2_approx {

namespace detail {

constexpr double FromBits(std::uint64_t bits) {
  return std::bit_cast<double>(bits);
}

inline constexpr float kLog2e = std::bit_cast<float>(0x3fb8aa3bu);
inline constexpr double kBias1023 = FromBits(0x408ff80000000000ull);
inline constexpr double kPoly0 = FromBits(0x3f65f6df6afdfca4ull);
inline constexpr double kPoly1 = FromBits(0x3f77e13b6e2818d0ull);
inline constexpr double kPoly2 = FromBits(0x3faf2b2865324148ull);
inline constexpr double kPoly3 = FromBits(0x3fce364fa5bce718ull);
inline constexpr double kPoly4 = FromBits(0x3fe63c6a5f945da2ull);
inline constexpr double kPoly5 = FromBits(0x3feffd95da61f655ull);
inline constexpr double kInfinity = FromBits(0x7ff0000000000000ull);

inline constexpr std::array<double, 64> kExp2MantissaTable = {
    FromBits(0x3ff0000000000000ull), FromBits(0x3ff02c9a3e778061ull),
    FromBits(0x3ff059b0d3158574ull), FromBits(0x3ff0874518759bc8ull),
    FromBits(0x3ff0b5586cf9890full), FromBits(0x3ff0e3ec32d3d1a2ull),
    FromBits(0x3ff11301d0125b51ull), FromBits(0x3ff1429aaea92de0ull),
    FromBits(0x3ff172b83c7d517bull), FromBits(0x3ff1a35beb6fcb75ull),
    FromBits(0x3ff1d4873168b9aaull), FromBits(0x3ff2063b88628cd6ull),
    FromBits(0x3ff2387a6e756238ull), FromBits(0x3ff26b4565e27cddull),
    FromBits(0x3ff29e9df51fdee1ull), FromBits(0x3ff2d285a6e4030bull),
    FromBits(0x3ff306fe0a31b715ull), FromBits(0x3ff33c08b26416ffull),
    FromBits(0x3ff371a7373aa9cbull), FromBits(0x3ff3a7db34e59ff7ull),
    FromBits(0x3ff3dea64c123422ull), FromBits(0x3ff4160a21f72e2aull),
    FromBits(0x3ff44e086061892dull), FromBits(0x3ff486a2b5c13cd0ull),
    FromBits(0x3ff4bfdad5362a27ull), FromBits(0x3ff4f9b2769d2ca7ull),
    FromBits(0x3ff5342b569d4f82ull), FromBits(0x3ff56f4736b527daull),
    FromBits(0x3ff5ab07dd485429ull), FromBits(0x3ff5e76f15ad2148ull),
    FromBits(0x3ff6247eb03a5585ull), FromBits(0x3ff6623882552225ull),
    FromBits(0x3ff6a09e667f3bcdull), FromBits(0x3ff6dfb23c651a2full),
    FromBits(0x3ff71f75e8ec5f74ull), FromBits(0x3ff75feb564267c9ull),
    FromBits(0x3ff7a11473eb0187ull), FromBits(0x3ff7e2f336cf4e62ull),
    FromBits(0x3ff82589994cce13ull), FromBits(0x3ff868d99b4492edull),
    FromBits(0x3ff8ace5422aa0dbull), FromBits(0x3ff8f1ae99157736ull),
    FromBits(0x3ff93737b0cdc5e5ull), FromBits(0x3ff97d829fde4e50ull),
    FromBits(0x3ff9c49182a3f090ull), FromBits(0x3ffa0c667b5de565ull),
    FromBits(0x3ffa5503b23e255dull), FromBits(0x3ffa9e6b5579fdbfull),
    FromBits(0x3ffae89f995ad3adull), FromBits(0x3ffb33a2b84f15fbull),
    FromBits(0x3ffb7f76f2fb5e47ull), FromBits(0x3ffbcc1e904bc1d2ull),
    FromBits(0x3ffc199bdd85529cull), FromBits(0x3ffc67f12e57d14bull),
    FromBits(0x3ffcb720dcef9069ull), FromBits(0x3ffd072d4a07897cull),
    FromBits(0x3ffd5818dcfba487ull), FromBits(0x3ffda9e603db3285ull),
    FromBits(0x3ffdfc97337b9b5full), FromBits(0x3ffe502ee78b3ff6ull),
    FromBits(0x3ffea4afa2a490daull), FromBits(0x3ffefa1bee615a27ull),
    FromBits(0x3fff50765b6e4540ull), FromBits(0x3fffa7c1819e90d8ull),
};

}

inline double Evaluate(double exponent) {
  if (!(exponent > -1022.0)) {
    return exponent == exponent ? 0.0 : detail::kInfinity;
  }
  if (exponent > 1025.0) {
    return detail::kInfinity;
  }

  const double biased = exponent + detail::kBias1023;
  const int scale_exponent = static_cast<int>(biased) - 1;
  const auto scale_exponent_bits = static_cast<std::uint32_t>(scale_exponent);

  if (scale_exponent_bits > 0x7ffu) {
    return scale_exponent <= 0 ? 0.0 : detail::kInfinity;
  }

  double residual =
      biased - static_cast<double>(static_cast<std::uint32_t>(scale_exponent));

  std::uint64_t residual_bits = std::bit_cast<std::uint64_t>(residual);
  std::uint32_t residual_hi = static_cast<std::uint32_t>(residual_bits >> 32);
  const std::uint32_t table_selector = residual_hi & 0xFC000u;
  residual_hi ^= table_selector;
  residual_bits =
      (static_cast<std::uint64_t>(residual_hi) << 32) |
      static_cast<std::uint32_t>(residual_bits);
  residual = std::bit_cast<double>(residual_bits);

  const double scale = std::bit_cast<double>(
      static_cast<std::uint64_t>(scale_exponent_bits) << 52);
  const double residual_sq = residual * residual;
  const double polynomial =
      residual_sq *
          (((residual * detail::kPoly0 + detail::kPoly1) * residual_sq) +
           residual * detail::kPoly2 + detail::kPoly3) +
      residual * detail::kPoly4 + detail::kPoly5;

  return detail::kExp2MantissaTable[table_selector >> 14] * scale * polynomial;
}

inline double FromNaturalExponent(double exponent) {
  return Evaluate(static_cast<double>(detail::kLog2e) * exponent);
}

}

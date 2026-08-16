#pragma once

#include <cstdint>

namespace openwow::data::dbc {

enum class DbcLocale : std::uint8_t {
  kEnUs = 0,
  kKoKr = 1,
  kFrFr = 2,
  kDeDe = 3,
  kZhCn = 4,
  kZhTw = 5,
  kEsEs = 6,
  kEsMx = 7,
  kRuRu = 8,
  kReserved9 = 9,
  kReserved10 = 10,
  kReserved11 = 11,
  kReserved12 = 12,
  kReserved13 = 13,
  kReserved14 = 14,
  kReserved15 = 15,
};

inline constexpr std::uint32_t kDbcLocaleCount =
    static_cast<std::uint32_t>(DbcLocale::kReserved15) + 1u;
inline constexpr std::uint32_t kDbcLocalizedStringFlagsFieldCount = 1u;
inline constexpr std::uint32_t kDbcLocalizedStringFieldCount =
    kDbcLocaleCount + kDbcLocalizedStringFlagsFieldCount;
inline constexpr int kFirstDbcLocaleIndex = 0;
inline constexpr int kLastDbcLocaleIndex =
    static_cast<int>(kDbcLocaleCount) - 1;

[[nodiscard]] constexpr std::uint32_t ToDbcLocaleIndex(const DbcLocale locale) {
  return static_cast<std::uint32_t>(locale);
}

}

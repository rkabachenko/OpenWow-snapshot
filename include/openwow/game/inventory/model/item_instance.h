#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace openwow::game {

enum class EnchantmentSlot : std::uint8_t {
  Permanent = 0,
  Temporary = 1,
  Socket1 = 2,
  Socket2 = 3,
  Socket3 = 4,
  Bonus = 5,
  Prismatic = 6,
  Use = 7,
  Prop0 = 8,
  Prop1 = 9,
  Prop2 = 10,
  Prop3 = 11,
  MaxSlots = 12,
};

struct EnchantmentData {
  std::uint32_t id{0};
  std::uint32_t duration{0};
  std::uint32_t charges{0};
};

namespace ItemFlags {
constexpr std::uint32_t kSoulbound = 0x00000001;
constexpr std::uint32_t kConjured = 0x00000002;
constexpr std::uint32_t kLootable = 0x00000004;
constexpr std::uint32_t kGiftWrapped = 0x00000008;
constexpr std::uint32_t kCantDestroy = 0x00000020;
constexpr std::uint32_t kTradeWindow = 0x00000100;
constexpr std::uint32_t kWrapped = 0x00000200;
constexpr std::uint32_t kQuestItem = 0x00002000;
constexpr std::uint32_t kAccountBound = 0x00080000;
constexpr std::uint32_t kMillable = 0x00200000;
constexpr std::uint32_t kBOERefund = 0x00400000;
}

namespace ItemTemplateFlags {
constexpr std::uint32_t kHasText = 0x00004000;
}

namespace ItemFieldFlags {
constexpr std::uint32_t kReadable = 0x00000200;
}

struct ItemInstance {
  std::uint64_t guid{0};
  std::uint32_t entry{0};
  std::uint32_t count{0};
  std::uint32_t flags{0};
  std::int32_t random_property{0};
  std::uint32_t random_suffix{0};
  std::uint32_t durability{0};
  std::uint32_t max_durability{0};
  std::uint32_t duration{0};
  std::uint32_t create_played_time{0};
  std::uint64_t creator_guid{0};
  std::uint8_t quality{0};
  bool is_locked{false};
  std::array<std::int32_t, 5> charges{};
  std::array<EnchantmentData,
             static_cast<std::size_t>(EnchantmentSlot::MaxSlots)>
      enchantments{};

  [[nodiscard]] std::uint32_t enchant_id() const {
    return GetPermanentEnchant();
  }
  [[nodiscard]] std::uint32_t GetPermanentEnchant() const {
    return enchantments[static_cast<std::size_t>(
                            EnchantmentSlot::Permanent)]
        .id;
  }
  [[nodiscard]] std::uint32_t GetTemporaryEnchant() const {
    return enchantments[static_cast<std::size_t>(
                            EnchantmentSlot::Temporary)]
        .id;
  }
  [[nodiscard]] std::uint32_t GetSocketEnchant(
      const std::uint8_t socket) const {
    if (socket >= 3) {
      return 0;
    }
    return enchantments[static_cast<std::size_t>(EnchantmentSlot::Socket1) +
                        socket]
        .id;
  }
  [[nodiscard]] bool IsSoulbound() const {
    return (flags & ItemFlags::kSoulbound) != 0;
  }
  [[nodiscard]] bool IsQuestItem() const {
    return (flags & ItemFlags::kQuestItem) != 0;
  }
  [[nodiscard]] bool IsConjured() const {
    return (flags & ItemFlags::kConjured) != 0;
  }
  [[nodiscard]] bool CantBeDestroyed() const {
    return (flags & ItemFlags::kCantDestroy) != 0;
  }
  [[nodiscard]] constexpr bool IsReadable(
      const std::uint32_t template_page_text) const noexcept {
    return template_page_text != 0 ||
           (flags & ItemFieldFlags::kReadable) != 0;
  }
  [[nodiscard]] bool IsEmpty() const { return entry == 0; }
};

}

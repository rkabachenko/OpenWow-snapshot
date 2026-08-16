#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <type_traits>

namespace openwow::render {

struct EquipmentItemVisual {
  std::uint32_t display_id{0};
  std::uint32_t item_visuals_id{0};

  std::uint32_t class_id{0};
  std::uint32_t subclass_id{0};

  std::uint8_t inventory_type{0};
  std::uint8_t sheathe_type{0};

  std::uint8_t item_visuals_enabled{1};
  std::uint8_t padding{0};

  friend bool operator==(const EquipmentItemVisual &lhs,
                         const EquipmentItemVisual &rhs) noexcept {
    return std::memcmp(&lhs, &rhs, sizeof(EquipmentItemVisual)) == 0;
  }
};

static_assert(std::has_unique_object_representations_v<EquipmentItemVisual>,
              "EquipmentItemVisual must be padding-free: its blocks are "
              "compared bytewise");
static_assert(sizeof(EquipmentItemVisual) == 20u);

using EquipmentItemVisualResolver =
    std::function<std::optional<EquipmentItemVisual>(std::uint32_t item_id)>;

}

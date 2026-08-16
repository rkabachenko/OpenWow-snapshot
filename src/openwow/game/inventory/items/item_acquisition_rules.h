#pragma once

#include <cstdint>
#include <string>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

class ItemDefinitions;
struct ItemTemplate;
class PlayerInventoryReplica;

enum class ItemAcquisitionFailure : std::uint8_t {
  kNone,
  kUniqueItemLimit,
  kLimitCategory,
};

struct ItemAcquisitionResult {
  ItemAcquisitionFailure failure = ItemAcquisitionFailure::kNone;
  std::uint32_t limit = 0;
  std::string category;

  [[nodiscard]] bool allowed() const noexcept {
    return failure == ItemAcquisitionFailure::kNone;
  }
};

[[nodiscard]] ItemAcquisitionResult EvaluateItemAcquisition(
    const PlayerInventoryReplica& inventory,
    const ItemDefinitions& definitions,
    const data::dbc::DbcLoader* dbc,
    const ItemTemplate& item,
    std::uint32_t count);

}

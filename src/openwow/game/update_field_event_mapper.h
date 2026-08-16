#pragma once

#include "openwow/game/object_types.h"
#include "openwow/game/update_fields.h"

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::game {

struct FieldEvent {
  const char* event_name;
  bool needs_unit_token;
  std::uint64_t guid_raw;

  std::uint8_t power_type{0};
};

std::vector<FieldEvent> MapChangedFieldsToEvents(
    TypeID type_id,
    std::uint64_t guid_raw,
    const std::vector<std::uint16_t>& updated_fields,
    bool is_create = false);

std::vector<std::uint16_t> ExtractFieldIndices(
    const std::vector<std::uint32_t>& bitmask);

}

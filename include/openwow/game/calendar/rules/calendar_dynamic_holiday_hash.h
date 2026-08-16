#pragma once

#include <cstdint>
#include <string>

#include "openwow/core/storm_hash_table.h"

namespace openwow::game {

struct DynamicHolidayHashEntry {
  using KeyType = std::uint32_t;

  std::uint32_t hash_key = 0;
  std::uint32_t holiday_id = 0;
  std::uint32_t texture_id = 0;
  std::uint32_t duration = 0;
  std::uint32_t flags = 0;
  std::uint32_t sort_priority = 0;
  std::int32_t  filter_type = -1;
  std::string   name;
  std::string   description;

  [[nodiscard]] KeyType GetKey() const { return hash_key; }
};

class DynamicHolidayHashTable
    : public openwow::core::TSHashTable<DynamicHolidayHashEntry> {
 public:
  DynamicHolidayHashTable() = default;
  ~DynamicHolidayHashTable() override = default;

  void InsertHoliday(const DynamicHolidayHashEntry& entry);

  [[nodiscard]] std::vector<const DynamicHolidayHashEntry*>
  FindByDateKey(std::uint32_t date_key) const;
};

}


#include "openwow/game/calendar/rules/calendar_dynamic_holiday_hash.h"

namespace openwow::game {

void DynamicHolidayHashTable::InsertHoliday(
    const DynamicHolidayHashEntry& entry) {
  if (!map_.initialized()) {
    map_.InitWithBuckets();
  }

  static_cast<void>(map_.InsertHashedKey(entry.hash_key));
  entries_.push_back(entry);
}

std::vector<const DynamicHolidayHashEntry*>
DynamicHolidayHashTable::FindByDateKey(const std::uint32_t date_key) const {
  std::vector<const DynamicHolidayHashEntry*> result;
  for (const auto& e : entries_) {
    if (e.hash_key == date_key) {
      result.push_back(&e);
    }
  }
  return result;
}

}

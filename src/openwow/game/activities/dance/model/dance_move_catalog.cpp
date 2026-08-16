#include "openwow/game/activities/dance/model/dance_move_catalog.h"

#include <algorithm>
#include <utility>

namespace openwow::game {

bool IsDanceDelayAction(const DanceMoveAction& action) {
  return std::holds_alternative<DanceDelayAction>(action) ||
         std::holds_alternative<DanceRepeatPreviousAction>(action);
}

DanceMoveCatalog::DanceMoveCatalog(std::vector<DanceMoveRecord> records)
    : records_(std::move(records)) {
  std::sort(records_.begin(), records_.end(),
            [](const DanceMoveRecord& left, const DanceMoveRecord& right) {
              return left.id.value < right.id.value;
            });
}

const DanceMoveRecord* DanceMoveCatalog::Lookup(const DanceMoveId id) const {
  const auto record =
      std::lower_bound(records_.begin(), records_.end(), id.value,
                       [](const DanceMoveRecord& candidate,
                          const std::int16_t value) {
                         return candidate.id.value < value;
                       });
  if (record == records_.end() || record->id != id) {
    return nullptr;
  }
  return &*record;
}

}

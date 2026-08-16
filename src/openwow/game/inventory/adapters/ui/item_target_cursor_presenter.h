#pragma once

#include "openwow/game/object_guid.h"

namespace openwow::game {
class SpellTargeting;

namespace inventory::ui {

[[nodiscard]] bool HasActiveItemTargetCursor(const SpellTargeting &targeting);
[[nodiscard]] ObjectGuid GetItemTargetCursorSource(const SpellTargeting &targeting);
void BeginItemTargetCursor(SpellTargeting &targeting, ObjectGuid source_item);
void ClearItemTargetCursor(SpellTargeting &targeting);

}
}

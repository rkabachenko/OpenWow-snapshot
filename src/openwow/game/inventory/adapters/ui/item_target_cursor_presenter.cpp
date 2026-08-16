#include "openwow/game/inventory/adapters/ui/item_target_cursor_presenter.h"

#include "openwow/game/actions/held_cursor/adapters/platform/cursor_surface.h"
#include "openwow/game/spell_targeting.h"
#include "openwow/ui/game/game_ui_core.h"

namespace openwow::game::inventory::ui {
namespace {

constexpr std::uint32_t kRetailDefaultCursorType = 1;
constexpr std::uint32_t kRetailCastCursorType = 2;

void ApplyCursorType(const CursorType base_type, const std::uint32_t retail_type) {
  auto *cursor = GetActiveCursorSurface();
  if (cursor == nullptr) {
    return;
  }

  cursor->SetBaseCursor(base_type);
  cursor->SetImmediateCursorType(retail_type);
}

}

bool HasActiveItemTargetCursor(const SpellTargeting &targeting) {
  return !targeting.GetItemCursorSource().IsEmpty();
}

ObjectGuid GetItemTargetCursorSource(const SpellTargeting &targeting) {
  return targeting.GetItemCursorSource();
}

void BeginItemTargetCursor(SpellTargeting &targeting, const ObjectGuid source_item) {
  if (source_item.IsEmpty()) {
    return;
  }

  const auto previous_source = targeting.GetItemCursorSource();
  if (!previous_source.IsEmpty() && previous_source != source_item) {
    openwow::ui::game::GameUI_OnMouseoverUnitLeave(previous_source.GetRawValue());
  }

  targeting.SetItemCursorSource(source_item);
  openwow::ui::game::GameUI_OnMouseoverUnitEnter(source_item.GetRawValue());
  ApplyCursorType(CursorType::kCast, kRetailCastCursorType);
}

void ClearItemTargetCursor(SpellTargeting &targeting) {
  const auto source_item = targeting.GetItemCursorSource();
  if (source_item.IsEmpty()) {
    return;
  }

  openwow::ui::game::GameUI_OnMouseoverUnitLeave(source_item.GetRawValue());
  targeting.SetItemCursorSource(ObjectGuid());
  ApplyCursorType(CursorType::kDefault, kRetailDefaultCursorType);
}

}

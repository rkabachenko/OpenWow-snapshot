#include "openwow/game/commerce/adapters/ui/money_cursor_controller.h"

#include "openwow/game/money_display.h"

#include <utility>

namespace openwow::game::commerce::ui {

MoneyCursorPickupEffect PickupMoneyCursor(
    actions::held_cursor::HeldCursor& cursor, const CopperAmount amount,
    const MoneyCursorKind kind) {
  if (amount.IsZero()) {
    return MoneyCursorPickupEffect::kNone;
  }

  actions::held_cursor::Presentation presentation{
      .texture_path = MoneyDisplay::GetCoinIconPath(amount.value()),
      .texture_mode = actions::held_cursor::TextureMode::CopiedPath,
      .sound = actions::held_cursor::Sound::Coins,
  };
  if (kind == MoneyCursorKind::kGuildBankMoney) {
    cursor.HoldGuildBankMoney(amount.value(), std::move(presentation));
  } else {
    cursor.HoldPlayerMoney(amount.value(), std::move(presentation));
  }
  return kind == MoneyCursorKind::kPlayerMoney
             ? MoneyCursorPickupEffect::kCursorUpdate
             : MoneyCursorPickupEffect::kGuildBankMoneyUpdate;
}

}

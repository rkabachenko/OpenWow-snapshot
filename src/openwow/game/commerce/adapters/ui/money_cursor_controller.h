#pragma once

#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/game/commerce/model/copper_amount.h"

#include <cstdint>

namespace openwow::game::commerce::ui {

enum class MoneyCursorKind : std::uint8_t {
  kPlayerMoney,
  kGuildBankMoney,
};

enum class MoneyCursorPickupEffect : std::uint8_t {
  kNone,
  kCursorUpdate,
  kGuildBankMoneyUpdate,
};

[[nodiscard]] MoneyCursorPickupEffect PickupMoneyCursor(
    actions::held_cursor::HeldCursor& cursor, CopperAmount amount,
    MoneyCursorKind kind);

}

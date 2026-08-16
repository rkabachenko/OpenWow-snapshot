#pragma once

#include "openwow/game/actions/macros/model/macro_id.h"

#include <cstdint>
#include <string>

namespace openwow::game {

using actions::macros::MacroId;

enum class MacroScope : std::uint8_t {
  kAccount = 0,
  kCharacter = 1,
};

struct MacroDocument {
  MacroId id;
  std::string name;
  std::uint32_t icon_id{0};
  std::string icon_name;
  std::string body;
  MacroScope scope{MacroScope::kAccount};

  std::int32_t action_bar_links{0};
  std::int32_t resolved_spell_id{0};
  std::uint32_t resolved_item_id{0};
  bool resolved_spell_from_pet_book{false};
  bool has_showtooltip{false};
  bool needs_icon_update{false};
  bool requires_action_bar_icon_updates{false};
  std::uint64_t target_guid{0};
};

}

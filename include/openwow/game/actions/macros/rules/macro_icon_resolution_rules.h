#pragma once

#include "openwow/game/actions/macros/rules/secure_command_option_parser.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace openwow::game::actions::macros::rules {

struct ResolvedMacroSpell {
  std::int32_t id{0};
  bool from_pet_spellbook{false};
};

struct MacroIconResolution {
  std::int32_t spell_id{0};
  std::uint32_t item_id{0};
  bool spell_from_pet_book{false};
  std::uint64_t target_guid{0};
};

struct MacroIconResolutionQueries {
  std::function<SecureCommandOptionResult(std::string_view)> secure_options;
  std::function<std::optional<std::string>(std::string_view)> cast_sequence;
  std::function<std::uint64_t(std::string_view)> target_guid;
  std::function<std::uint32_t(std::string_view)> inventory_item;
  std::function<std::uint32_t(std::string_view)> item;
  std::function<std::optional<ResolvedMacroSpell>(std::string_view)> spell;
};

class MacroIconResolutionRules {
 public:
  [[nodiscard]] static MacroIconResolution Resolve(
      const std::string& body,
      const MacroIconResolutionQueries& queries);
};

}

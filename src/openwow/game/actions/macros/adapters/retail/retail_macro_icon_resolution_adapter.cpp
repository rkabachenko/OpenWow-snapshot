#include "openwow/game/actions/macros/adapters/retail/retail_macro_icon_resolution_adapter.h"

#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/game/actions/macros/rules/retail_macro_text.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/spellbook_frame.h"
#include "openwow/game/unit_query_bridge.h"
#include "openwow/game/world_session.h"

#include <charconv>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace openwow::game::actions::macros::adapters::retail {
namespace {

std::optional<std::uint32_t> ParseUnsigned(
    const std::string_view value) {
  std::uint32_t parsed = 0;
  const auto [end, error] =
      std::from_chars(value.data(), value.data() + value.size(), parsed);
  return !value.empty() && error == std::errc{} &&
                 end == value.data() + value.size()
             ? std::optional<std::uint32_t>(parsed)
             : std::nullopt;
}

std::uint64_t TargetGuid(WorldSession& session,
                         const UnitQueryBridge& unit_queries,
                         const std::string_view unit) {
  return !unit.empty()
             ? unit_queries
                   .ResolveToGuid(&session, unit)
                   .GetRawValue()
             : 0;
}

std::uint32_t Item(WorldSession& session, const std::string_view value) {
  if (value.empty()) {
    return 0;
  }
  std::string item_name(value);
  if (const char* payload =
          std::strstr(item_name.c_str(), "item:");
      payload != nullptr) {
    return static_cast<std::uint32_t>(
        std::strtoul(payload + 5, nullptr, 10));
  }
  const auto* item =
      session.query_cache().GetItemTemplateByName(item_name);
  return item != nullptr ? item->entry : 0;
}

std::uint32_t InventoryItem(const PlayerInventoryReplica& inventory,
                            const std::string_view value) {
  const auto first =
      rules::SplitRetailMacroToken(value, 0, value.size(), ' ');
  const auto first_value = ParseUnsigned(first.value);
  if (!first_value) {
    return 0;
  }
  if (first.raw_end >= value.size()) {
    if (*first_value == 0) {
      return 0;
    }
    const auto slot = *first_value - 1;
    if (slot > InventorySlots::kBagSlotsEnd - 1) {
      return 0;
    }
    const auto* item =
        inventory.GetItemInSlot(static_cast<std::uint8_t>(slot));
    return item != nullptr ? item->entry : 0;
  }
  const auto second = rules::SplitRetailMacroToken(
      value, first.raw_end + 1, value.size(), '\0');
  const auto second_value = ParseUnsigned(second.value);
  if (!second_value || *second_value == 0) {
    return 0;
  }
  const auto slot = *second_value - 1;
  if (*first_value == 0) {
    if (slot >= PlayerInventoryReplica::kBackpackSize) {
      return 0;
    }
    const auto* item =
        inventory.GetBackpackSlot(static_cast<std::uint8_t>(slot));
    return item != nullptr ? item->entry : 0;
  }
  if (*first_value > PlayerInventoryReplica::kMaxBags) {
    return 0;
  }
  const auto bag_index = static_cast<std::uint8_t>(*first_value);
  const auto* bag = inventory.GetBag(bag_index);
  if (bag == nullptr || slot >= bag->num_slots) {
    return 0;
  }
  const auto* item = inventory.GetBagSlot(
      bag_index, static_cast<std::uint8_t>(slot));
  return item != nullptr ? item->entry : 0;
}

std::optional<rules::ResolvedMacroSpell> ResolveSpell(
    const std::string_view value) {
  std::string name(value);
  if (!name.empty() && name.front() == '!') {
    name.erase(name.begin());
  }
  if (name.empty()) {
    return std::nullopt;
  }
  if (const auto direct = SpellBookFrame::ResolveSpellByName(name, {})) {
    return rules::ResolvedMacroSpell{
        .id = static_cast<std::int32_t>(direct->spell_id),
        .from_pet_spellbook = direct->from_pet_spellbook};
  }
  const auto open = name.find('(');
  if (open == std::string::npos) {
    return std::nullopt;
  }
  std::string qualifier = name.substr(open + 1);
  name.resize(open);
  if (name.empty()) {
    return std::nullopt;
  }
  if (const auto close = qualifier.find(')');
      close != std::string::npos) {
    qualifier.resize(close);
  }
  const auto qualified =
      SpellBookFrame::ResolveSpellByName(name, qualifier);
  return qualified
             ? std::optional<rules::ResolvedMacroSpell>(
                   rules::ResolvedMacroSpell{
                       .id = static_cast<std::int32_t>(
                           qualified->spell_id),
                       .from_pet_spellbook =
                           qualified->from_pet_spellbook})
             : std::nullopt;
}

}

std::optional<rules::ResolvedMacroSpell> ResolveRetailMacroSpell(
    const std::string_view value) {
  return ResolveSpell(value);
}

rules::MacroIconResolutionQueries MakeRetailMacroIconResolutionQueries(
    MacroCatalog& macros, WorldSession& session,
    const UnitQueryBridge& unit_queries,
    const PlayerInventoryReplica& inventory,
    std::function<std::optional<rules::ResolvedMacroSpell>(
        std::string_view)> spell_query) {
  return {
      .secure_options =
          [&macros](const std::string_view options) {
            return macros.ParseSecureCommandOptions(options);
          },
      .cast_sequence =
          [&macros](const std::string_view body) {
            return macros.ResolveCastSequenceToken(body);
          },
      .target_guid =
          [&session, &unit_queries](const std::string_view unit) {
            return TargetGuid(session, unit_queries, unit);
          },
      .inventory_item =
          [&inventory](const std::string_view value) {
            return InventoryItem(inventory, value);
          },
      .item =
          [&session](const std::string_view value) {
            return Item(session, value);
          },
      .spell = std::move(spell_query),
  };
}

}

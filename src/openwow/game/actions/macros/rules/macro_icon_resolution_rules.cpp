#include "openwow/game/actions/macros/rules/macro_icon_resolution_rules.h"

#include "openwow/game/actions/macros/rules/macro_body_rules.h"
#include "openwow/game/actions/macros/rules/retail_macro_text.h"

#include <cctype>

namespace openwow::game::actions::macros::rules {
namespace {

bool EqualsNoCase(const std::string_view lhs,
                  const std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
        std::tolower(static_cast<unsigned char>(rhs[index]))) {
      return false;
    }
  }
  return true;
}

enum class Command {
  kNone,
  kShow,
  kCastOrUse,
  kRandom,
  kCastSequence,
  kEquip,
  kEquipSlot,
};

Command Classify(const std::string_view command) {
  if (EqualsNoCase(command, "#show") ||
      EqualsNoCase(command, "#showtooltip")) return Command::kShow;
  if (EqualsNoCase(command, "/cast") ||
      EqualsNoCase(command, "/use")) return Command::kCastOrUse;
  if (EqualsNoCase(command, "/castrandom") ||
      EqualsNoCase(command, "/userandom")) return Command::kRandom;
  if (EqualsNoCase(command, "/castsequence")) {
    return Command::kCastSequence;
  }
  if (EqualsNoCase(command, "/equip")) return Command::kEquip;
  if (EqualsNoCase(command, "/equipslot")) return Command::kEquipSlot;
  return Command::kNone;
}

std::string_view StripSlot(std::string_view value) {
  std::size_t cursor = 0;
  while (cursor < value.size() && std::isdigit(
                                      static_cast<unsigned char>(
                                          value[cursor]))) {
    ++cursor;
  }
  while (cursor < value.size() && value[cursor] == ' ') {
    ++cursor;
  }
  return value.substr(cursor);
}

}

MacroIconResolution MacroIconResolutionRules::Resolve(
    const std::string& body,
    const MacroIconResolutionQueries& queries) {
  MacroIconResolution result;
  for (const auto& line : MacroBodyRules::SplitLines(body)) {
    const auto command =
        SplitRetailMacroToken(line, 0, line.size(), ' ');
    const auto kind = Classify(command.value);
    if (kind == Command::kNone) {
      continue;
    }
    const auto raw_arguments =
        SplitRetailMacroToken(line, command.raw_end,
                              line.size(), '\0');
    if (raw_arguments.value.empty() || !queries.secure_options) {
      continue;
    }
    const auto secure = queries.secure_options(raw_arguments.value);
    if (!secure.matched) {
      continue;
    }
    std::string candidate(TrimRetailMacroSpaces(secure.value));
    if (candidate.empty()) {
      continue;
    }
    if (EqualsNoCase(candidate, "none")) {
      result.target_guid =
          queries.target_guid ? queries.target_guid(secure.target) : 0;
      break;
    }
    if (kind == Command::kEquipSlot) {
      candidate = std::string(TrimRetailMacroSpaces(StripSlot(candidate)));
      if (candidate.empty()) {
        continue;
      }
    }
    const bool numeric =
        std::isdigit(static_cast<unsigned char>(candidate.front()));
    const bool continue_on_failure =
        kind == Command::kCastSequence || numeric;
    if (kind == Command::kRandom) {
      candidate = std::string(
          SplitRetailMacroToken(candidate, 0, candidate.size(), ',').value);
      if (candidate.empty()) {
        continue;
      }
    } else if (kind == Command::kCastSequence &&
               queries.cast_sequence) {
      if (const auto resolved = queries.cast_sequence(candidate)) {
        candidate = std::string(TrimRetailMacroSpaces(*resolved));
      }
    }

    const auto target_guid =
        queries.target_guid ? queries.target_guid(secure.target) : 0;
    if (!candidate.empty() && std::isdigit(
                                  static_cast<unsigned char>(
                                      candidate.front()))) {
      const auto item_id =
          queries.inventory_item ? queries.inventory_item(candidate) : 0;
      if (item_id != 0) {
        result.item_id = item_id;
        result.target_guid = target_guid;
        break;
      }
      result.target_guid = target_guid;
      if (kind == Command::kShow || !continue_on_failure) {
        break;
      }
      continue;
    }
    const auto item_id = queries.item ? queries.item(candidate) : 0;
    if (item_id != 0) {
      result.item_id = item_id;
      result.target_guid = target_guid;
      break;
    }
    if (kind != Command::kEquip && kind != Command::kEquipSlot &&
        queries.spell) {
      if (const auto spell = queries.spell(candidate);
          spell && spell->id > 0) {
        result.spell_id = spell->id;
        result.spell_from_pet_book = spell->from_pet_spellbook;
        result.target_guid = target_guid;
        break;
      }
    }
    result.target_guid = target_guid;
    if (kind == Command::kShow || !continue_on_failure) {
      break;
    }
  }
  return result;
}

}

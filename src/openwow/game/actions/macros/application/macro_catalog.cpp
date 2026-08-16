#include "openwow/game/actions/macros/application/macro_catalog.h"

#include "openwow/game/actions/macros/rules/macro_body_rules.h"

#include <cctype>

namespace openwow::game {
namespace {

constexpr std::string_view kDefaultMacroIcon = "INV_Misc_QuestionMark";

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

}

bool MacroCatalog::ApplyBodyTextAndResolveLocked(
    MacroDocument& macro, const std::string_view body) {
  const auto previous_spell = macro.resolved_spell_id;
  const auto previous_item = macro.resolved_item_id;
  const auto previous_target = macro.target_guid;
  macro.body =
      actions::macros::rules::MacroBodyRules::NormalizeRetailBody(body);
  const auto presentation =
      actions::macros::rules::MacroBodyRules::AnalyzePresentation(
          macro.body);
  macro.has_showtooltip = presentation.has_showtooltip;
  macro.requires_action_bar_icon_updates =
      presentation.requires_action_bar_icon_updates;
  const auto resolved = ResolveIconStateFromBodyLocked(macro);
  macro.resolved_spell_id = resolved.spell_id;
  macro.resolved_item_id = resolved.item_id;
  macro.resolved_spell_from_pet_book =
      resolved.spell_from_pet_book;
  macro.target_guid = resolved.target_guid;
  macro.needs_icon_update =
      macro.has_showtooltip ||
      EqualsNoCase(macro.icon_name, kDefaultMacroIcon);
  return previous_spell != macro.resolved_spell_id ||
         previous_item != macro.resolved_item_id ||
         previous_target != macro.target_guid;
}

std::string MacroCatalog::ResolveMacroIconName(
    const std::uint32_t icon_index) {
  if (icon_index == 0) {
    return {};
  }
  LoadIconList();
  return icon_library_.MacroIcon(icon_index).value_or("");
}

const MacroDocument* MacroCatalog::FindMacroByNameLocked(
    const std::string_view name) const {
  return name.empty() ? nullptr : store_.FindByName(name);
}

std::int32_t MacroCatalog::FindSlotIndexLocked(
    const MacroId id) const {
  const auto slot = store_.FindSlot(id);
  return slot ? static_cast<std::int32_t>(slot->value()) : -1;
}

void MacroCatalog::PromoteMacroLookupOrderLocked(const MacroId id) {
  store_.PromoteLookup(id);
}

void MacroCatalog::RemoveMacroLookupOrderLocked(const MacroId id) {
  store_.RemoveLookup(id);
}

void MacroCatalog::QueuePendingActionBarIconUpdateLocked(
    const MacroId id) {
  presentation_runtime_.QueueIconUpdate(id);
}

void MacroCatalog::UnqueuePendingActionBarIconUpdateLocked(
    const MacroId id) {
  presentation_runtime_.RemoveIconUpdate(id);
}

void MacroCatalog::SyncPendingActionBarIconUpdateLocked(
    const MacroDocument& macro) {
  if (macro.requires_action_bar_icon_updates &&
      macro.action_bar_links > 0) {
    QueuePendingActionBarIconUpdateLocked(macro.id);
  } else {
    UnqueuePendingActionBarIconUpdateLocked(macro.id);
  }
}

void MacroCatalog::RebuildSlotArrayLocked() {
  store_.RebuildSlots();
}

void MacroCatalog::NotifyMacrosChanged() const {
  const auto callback = presentation_runtime_.MacrosChanged();
  if (callback) {
    callback();
  }
}

actions::macros::rules::MacroIconResolution
MacroCatalog::ResolveIconStateFromBodyLocked(
    const MacroDocument& macro) const {
  return actions::macros::rules::MacroIconResolutionRules::Resolve(
      macro.body, presentation_runtime_.IconResolution());
}

void MacroCatalog::DispatchExecutableLinesLocked(
    const std::string& body) {
  for (const auto& line :
       actions::macros::rules::MacroBodyRules::SplitLines(body)) {
    if (!execution_runtime_.active()) {
      break;
    }
    if (line.empty() || line.front() == '-' || line.front() == '#') {
      continue;
    }
    execution_runtime_.DispatchCommand(line);
  }
}

bool MacroCatalog::CanPerformLocked(
    const actions::macros::MacroProtectedOperation operation) const {
  return execution_runtime_.CanPerform(operation);
}

}

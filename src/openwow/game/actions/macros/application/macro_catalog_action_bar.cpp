#include "openwow/game/actions/macros/application/macro_catalog.h"

#include <utility>

namespace openwow::game {

void MacroCatalog::IncrementActionBarLinksLocked(
    MacroDocument& macro) {
  if (macro.requires_action_bar_icon_updates &&
      macro.action_bar_links == 0) {
    QueuePendingActionBarIconUpdateLocked(macro.id);
  }
  ++macro.action_bar_links;
}

void MacroCatalog::DecrementActionBarLinksLocked(
    MacroDocument& macro) {
  --macro.action_bar_links;
  if (macro.requires_action_bar_icon_updates &&
      macro.action_bar_links == 0) {
    UnqueuePendingActionBarIconUpdateLocked(macro.id);
  }
}

void MacroCatalog::SetIconResolutionQueries(
    IconResolutionQueries queries) {
  presentation_runtime_.SetIconResolutionQueries(std::move(queries));
}

void MacroCatalog::SetIconPathResolver(IconPathResolver resolver) {
  presentation_runtime_.SetIconPathResolver(std::move(resolver));
}

void MacroCatalog::SetMacrosChangedCallback(
    MacrosChangedCallback callback) {
  presentation_runtime_.SetMacrosChangedCallback(std::move(callback));
}

std::vector<std::string> MacroCatalog::GetMacroIconList() {
  LoadIconList();
  return icon_library_.MacroIcons();
}

void MacroCatalog::RebuildActionBarReferences() {
  const auto provider = presentation_runtime_.ActionBarSlots();
  const auto ids = provider ? provider() : std::vector<MacroId>{};
  std::lock_guard lock(mutex_);
  for (auto& [id, macro] : store_.documents_) {
    (void)id;
    macro.action_bar_links = 0;
  }
  presentation_runtime_.ClearIconUpdates();
  for (const auto id : ids) {
    if (!id.IsValid()) {
      continue;
    }
    if (auto it = store_.documents_.find(id);
        it != store_.documents_.end()) {
      IncrementActionBarLinksLocked(it->second);
    }
  }
}

void MacroCatalog::IncrementActionBarLinks(const MacroId id) {
  std::lock_guard lock(mutex_);
  if (auto it = store_.documents_.find(id);
      it != store_.documents_.end()) {
    IncrementActionBarLinksLocked(it->second);
  }
}

void MacroCatalog::DecrementActionBarLinks(const MacroId id) {
  std::lock_guard lock(mutex_);
  if (auto it = store_.documents_.find(id);
      it != store_.documents_.end()) {
    DecrementActionBarLinksLocked(it->second);
  }
}

void MacroCatalog::ClearActionBarReferencesForMacro(
    const MacroId id) {
  if (!id.IsValid()) {
    return;
  }
  if (const auto clear = presentation_runtime_.ClearActionBar()) {
    clear(id);
  }
}

std::string MacroCatalog::GetIconPath(const MacroId id) {
  std::lock_guard lock(mutex_);
  auto it = store_.documents_.find(id);
  if (it == store_.documents_.end()) {
    return {};
  }
  auto& macro = it->second;
  if (macro.requires_action_bar_icon_updates &&
      macro.action_bar_links == 0) {
    const auto resolved = ResolveIconStateFromBodyLocked(macro);
    macro.resolved_spell_id = resolved.spell_id;
    macro.resolved_item_id = resolved.item_id;
    macro.resolved_spell_from_pet_book =
        resolved.spell_from_pet_book;
    macro.target_guid = resolved.target_guid;
  }
  const auto resolver = presentation_runtime_.IconPath();
  return resolver ? resolver(macro) : std::string{};
}

void MacroCatalog::UpdateDirtyIcons() {
  std::vector<MacroId> changed;
  {
    std::lock_guard lock(mutex_);
    for (const auto id : store_.lookup_order_) {
      auto it = store_.documents_.find(id);
      if (it == store_.documents_.end() ||
          !it->second.needs_icon_update) {
        continue;
      }
      auto& macro = it->second;
      const auto resolved = ResolveIconStateFromBodyLocked(macro);
      const bool did_change =
          macro.resolved_spell_id != resolved.spell_id ||
          macro.resolved_item_id != resolved.item_id ||
          macro.target_guid != resolved.target_guid;
      macro.resolved_spell_id = resolved.spell_id;
      macro.resolved_item_id = resolved.item_id;
      macro.resolved_spell_from_pet_book =
          resolved.spell_from_pet_book;
      macro.target_guid = resolved.target_guid;
      macro.needs_icon_update = false;
      if (did_change) {
        changed.push_back(id);
      }
    }
  }
  for (const auto id : changed) {
    NotifyActionBarRefresh(id);
  }
}

void MacroCatalog::UpdateAllPendingIcons() {
  std::vector<MacroId> changed;
  const auto pending = presentation_runtime_.PendingIconUpdates();
  {
    std::lock_guard lock(mutex_);
    for (const auto id : pending) {
      auto it = store_.documents_.find(id);
      if (it == store_.documents_.end()) {
        continue;
      }
      auto& macro = it->second;
      if (!macro.requires_action_bar_icon_updates) {
        UnqueuePendingActionBarIconUpdateLocked(id);
        continue;
      }
      const auto resolved = ResolveIconStateFromBodyLocked(macro);
      const bool did_change =
          macro.resolved_spell_id != resolved.spell_id ||
          macro.resolved_item_id != resolved.item_id ||
          macro.target_guid != resolved.target_guid;
      macro.resolved_spell_id = resolved.spell_id;
      macro.resolved_item_id = resolved.item_id;
      macro.resolved_spell_from_pet_book =
          resolved.spell_from_pet_book;
      macro.target_guid = resolved.target_guid;
      if (did_change) {
        changed.push_back(id);
      }
    }
  }
  for (const auto id : changed) {
    NotifyActionBarRefresh(id);
  }
}

void MacroCatalog::UpdateActionBarLinks(const MacroId id) {
  const auto provider = presentation_runtime_.ActionBarSlots();
  {
    std::lock_guard lock(mutex_);
    if (!store_.documents_.contains(id)) {
      return;
    }
  }
  std::int32_t count = 0;
  if (provider) {
    for (const auto linked : provider()) {
      count += linked == id;
    }
  }
  std::lock_guard lock(mutex_);
  if (auto it = store_.documents_.find(id);
      it != store_.documents_.end()) {
    it->second.action_bar_links = count;
    SyncPendingActionBarIconUpdateLocked(it->second);
  }
}

void MacroCatalog::SetActionBarRefreshCallback(
    ActionBarRefreshCallback callback) {
  presentation_runtime_.SetRefreshCallback(std::move(callback));
}

void MacroCatalog::SetActionBarMacroSlotProvider(
    ActionBarMacroSlotProvider provider) {
  presentation_runtime_.SetActionBarSlotProvider(std::move(provider));
  RebuildActionBarReferences();
}

void MacroCatalog::SetShapeshiftSlotProvider(
    ShapeshiftSlotProvider provider) {
  presentation_runtime_.SetShapeshiftSlotProvider(std::move(provider));
}

void MacroCatalog::SetActiveShapeshiftFormProvider(
    ActiveShapeshiftFormProvider provider) {
  presentation_runtime_.SetActiveShapeshiftFormProvider(
      std::move(provider));
}

void MacroCatalog::SetClearActionBarMacro(
    ClearActionBarMacro callback) {
  presentation_runtime_.SetClearActionBarMacro(std::move(callback));
}

std::uint32_t MacroCatalog::GetRetailShapeshiftFormIndex() const {
  const auto active_provider =
      presentation_runtime_.ActiveShapeshiftForm();
  const auto active = active_provider ? active_provider() : 0;
  if (active == 0) {
    return 0;
  }
  const auto provider = presentation_runtime_.ShapeshiftSlots();
  const auto visible =
      provider ? provider() : std::vector<std::uint32_t>{};
  for (std::size_t index = 0; index < visible.size(); ++index) {
    if (visible[index] == active) {
      return static_cast<std::uint32_t>(index + 1);
    }
  }
  return static_cast<std::uint32_t>(visible.size() + 1);
}

void MacroCatalog::NotifyActionBarRefresh(const MacroId id) const {
  const auto callback = presentation_runtime_.Refresh();
  if (id.IsValid() && callback) {
    callback(id);
  }
}

}

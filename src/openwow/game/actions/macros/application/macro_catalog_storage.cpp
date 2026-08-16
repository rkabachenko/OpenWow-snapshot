#include "openwow/game/actions/macros/application/macro_catalog.h"

#include "openwow/game/actions/macros/rules/retail_macro_text.h"

#include <algorithm>
#include <utility>

namespace openwow::game {
namespace {

std::uint16_t NextAccountDataGeneration(std::uint16_t generation) {
  do {
    ++generation;
  } while (generation == 0);
  return generation;
}

}

void MacroCatalog::SetMacro(const MacroId id,
                            const MacroDocument& macro) {
  {
    std::lock_guard lock(mutex_);
    const bool existed = store_.documents_.contains(id);
    auto document = macro;
    document.id = id;
    store_.documents_[id] = std::move(document);
    SyncPendingActionBarIconUpdateLocked(store_.documents_[id]);
    if (!existed) {
      PromoteMacroLookupOrderLocked(id);
    }
    store_.dirty_ = true;
    RebuildSlotArrayLocked();
  }
  NotifyMacrosChanged();
}

void MacroCatalog::DeleteMacro(const MacroId id) {
  {
    std::lock_guard lock(mutex_);
    UnqueuePendingActionBarIconUpdateLocked(id);
    if (store_.documents_.erase(id) == 0) {
      return;
    }
    RemoveMacroLookupOrderLocked(id);
    store_.dirty_ = true;
    RebuildSlotArrayLocked();
  }
  if (const auto clear = presentation_runtime_.ClearActionBar()) {
    clear(id);
  }
  NotifyMacrosChanged();
}

std::optional<MacroDocument> MacroCatalog::FindMacro(
    const MacroId id) const {
  std::lock_guard lock(mutex_);
  const auto* document = store_.Find(id);
  return document ? std::optional<MacroDocument>(*document)
                  : std::nullopt;
}

bool MacroCatalog::UpdateMacro(
    const MacroId id,
    const std::function<void(MacroDocument&)>& update) {
  std::lock_guard lock(mutex_);
  if (!store_.Update(id, update)) {
    return false;
  }
  SyncPendingActionBarIconUpdateLocked(*store_.Find(id));
  return true;
}

std::size_t MacroCatalog::GetNumAccountMacros() const {
  std::lock_guard lock(mutex_);
  return store_.account_count_;
}

std::size_t MacroCatalog::GetNumCharacterMacros() const {
  std::lock_guard lock(mutex_);
  return store_.character_count_;
}

std::vector<MacroDocument> MacroCatalog::GetAccountMacros() const {
  std::lock_guard lock(mutex_);
  return store_.Snapshot(MacroScope::kAccount);
}

std::vector<MacroDocument> MacroCatalog::GetCharacterMacros() const {
  std::lock_guard lock(mutex_);
  return store_.Snapshot(MacroScope::kCharacter);
}

std::optional<MacroDocument> MacroCatalog::FindMacroByName(
    const std::string_view name) const {
  std::lock_guard lock(mutex_);
  const auto* document = FindMacroByNameLocked(name);
  return document ? std::optional<MacroDocument>(*document)
                  : std::nullopt;
}

std::int32_t MacroCatalog::FindSlotIndex(const MacroId id) const {
  std::lock_guard lock(mutex_);
  return FindSlotIndexLocked(id);
}

std::int32_t MacroCatalog::FindSlotIndexByName(
    const std::string& name) const {
  std::lock_guard lock(mutex_);
  const auto* document = FindMacroByNameLocked(name);
  return document ? FindSlotIndexLocked(document->id) : -1;
}

std::uint32_t MacroCatalog::GetMacroIndexByName(
    const std::string& name) const {
  return static_cast<std::uint32_t>(FindSlotIndexByName(name) + 1);
}

std::optional<MacroDocument> MacroCatalog::FindMacroAtSlot(
    const std::uint32_t slot_index) const {
  std::lock_guard lock(mutex_);
  const auto slot =
      actions::macros::MacroSlotIndex::FromZeroBased(slot_index);
  return slot ? store_.AtSlot(*slot) : std::nullopt;
}

bool MacroCatalog::UpdateMacroAtSlot(
    const std::uint32_t slot_index,
    const std::function<void(MacroDocument&)>& update) {
  std::lock_guard lock(mutex_);
  const auto slot =
      actions::macros::MacroSlotIndex::FromZeroBased(slot_index);
  if (!slot || !store_.UpdateAtSlot(*slot, update)) {
    return false;
  }
  SyncPendingActionBarIconUpdateLocked(*store_.AtSlot(*slot));
  return true;
}

MacroId MacroCatalog::CreateMacro(
    const std::string& name, const std::uint32_t icon_id,
    const std::string& body, const MacroScope scope) {
  if (name.empty()) {
    return {};
  }
  const auto icon_name = ResolveMacroIconName(icon_id);
  MacroId id;
  bool refresh = false;
  {
    std::lock_guard lock(mutex_);
    const bool character = scope == MacroScope::kCharacter;
    const auto count = character ? store_.character_count_
                                 : store_.account_count_;
    const auto capacity =
        character ? kMaxCharacterMacros : kMaxAccountMacros;
    if (count >= capacity) {
      return {};
    }
    MacroDocument document;
    document.id = store_.next_id_;
    store_.next_id_ = store_.next_id_.Next();
    document.name =
        actions::macros::rules::CopyRetailMacroSpan(
            name, kMaxNameLength + 1);
    document.icon_id = icon_id;
    document.icon_name =
        actions::macros::rules::CopyRetailMacroSpan(icon_name, 256);
    document.scope = scope;
    ApplyBodyTextAndResolveLocked(document, body);
    refresh = document.resolved_spell_id > 0 ||
              document.resolved_item_id != 0;
    id = document.id;
    store_.documents_[id] = std::move(document);
    PromoteMacroLookupOrderLocked(id);
    store_.dirty_ = true;
    RebuildSlotArrayLocked();
  }
  NotifyMacrosChanged();
  UpdateActionBarLinks(id);
  if (refresh) {
    NotifyActionBarRefresh(id);
  }
  return id;
}

MacroId MacroCatalog::EditMacro(
    const MacroId id, const std::string& name,
    const std::uint32_t icon_id, const std::string& body) {
  const auto icon_name =
      icon_id != 0 ? ResolveMacroIconName(icon_id) : std::string{};
  {
    std::lock_guard lock(mutex_);
    auto it = store_.documents_.find(id);
    if (it == store_.documents_.end()) {
      return {};
    }
    it->second.name =
        actions::macros::rules::CopyRetailMacroSpan(
            name, kMaxNameLength + 1);
    if (icon_id != 0) {
      it->second.icon_id = icon_id;
      it->second.icon_name =
          actions::macros::rules::CopyRetailMacroSpan(icon_name, 256);
    }
    ApplyBodyTextAndResolveLocked(it->second, body);
    SyncPendingActionBarIconUpdateLocked(it->second);
    store_.dirty_ = true;
    RebuildSlotArrayLocked();
  }
  NotifyMacrosChanged();
  NotifyActionBarRefresh(id);
  return id;
}

void MacroCatalog::SetBodyText(const std::uint32_t slot_index,
                               const std::string& body) {
  std::lock_guard lock(mutex_);
  if (slot_index >= store_.slots_.size() ||
      !store_.slots_[slot_index]) {
    return;
  }
  auto it = store_.documents_.find(*store_.slots_[slot_index]);
  if (it == store_.documents_.end()) {
    return;
  }
  ApplyBodyTextAndResolveLocked(it->second, body);
  SyncPendingActionBarIconUpdateLocked(it->second);
}

void MacroCatalog::InitializeUiSession() {
  {
    std::lock_guard lock(mutex_);
    account_data_generation_ =
        NextAccountDataGeneration(account_data_generation_);
  }
  LoadIconList();
}

std::uint32_t MacroCatalog::GetNumMacroIcons() const {
  return icon_library_.MacroIconCount();
}

std::uint32_t MacroCatalog::GetNumMacroItemIcons() const {
  return icon_library_.ItemIconCount();
}

std::optional<std::string> MacroCatalog::MacroIconName(
    const std::uint32_t index) const {
  return icon_library_.MacroIcon(index);
}

std::optional<std::string> MacroCatalog::MacroItemIconName(
    const std::uint32_t index) const {
  return icon_library_.ItemIcon(index);
}

void MacroCatalog::ClearAll() {
  std::lock_guard lock(mutex_);
  account_data_generation_ =
      NextAccountDataGeneration(account_data_generation_);
  store_.documents_.clear();
  store_.lookup_order_.clear();
  store_.slots_.fill(std::nullopt);
  store_.account_count_ = 0;
  store_.character_count_ = 0;
  store_.next_id_ = MacroId(1);
  presentation_runtime_.ClearIconUpdates();
  secure_command_option_parser_.Reset();
  icon_library_.Reset();
  execution_runtime_.Reset();
}

void MacroCatalog::Reset() {
  ClearAll();
  std::lock_guard lock(mutex_);
  store_.dirty_ = false;
}

void MacroCatalog::RebuildSlotArray() {
  {
    std::lock_guard lock(mutex_);
    RebuildSlotArrayLocked();
  }
  NotifyMacrosChanged();
}

void MacroCatalog::MarkDirty() {
  std::lock_guard lock(mutex_);
  store_.dirty_ = true;
}

bool MacroCatalog::IsDirty() const {
  std::lock_guard lock(mutex_);
  return store_.dirty_;
}

void MacroCatalog::ClearDirty() {
  std::lock_guard lock(mutex_);
  store_.dirty_ = false;
}

void MacroCatalog::ReplaceMacros(
    const MacroScope scope, std::vector<MacroDocument> documents) {
  {
    std::lock_guard lock(mutex_);
    for (auto it = store_.documents_.begin();
         it != store_.documents_.end();) {
      if (it->second.scope == scope) {
        RemoveMacroLookupOrderLocked(it->first);
        UnqueuePendingActionBarIconUpdateLocked(it->first);
        it = store_.documents_.erase(it);
      } else {
        ++it;
      }
    }
    const auto capacity = scope == MacroScope::kCharacter
                              ? kMaxCharacterMacros
                              : kMaxAccountMacros;
    std::size_t count = 0;
    for (auto& document : documents) {
      if (count >= capacity || !document.id.IsValid() ||
          store_.documents_.contains(document.id)) {
        continue;
      }
      document.scope = scope;
      store_.next_id_ =
          std::max(store_.next_id_, document.id.Next());
      const auto id = document.id;
      store_.documents_[id] = std::move(document);
      PromoteMacroLookupOrderLocked(id);
      SyncPendingActionBarIconUpdateLocked(store_.documents_[id]);
      ++count;
    }
    store_.dirty_ = false;
    RebuildSlotArrayLocked();
  }
  NotifyMacrosChanged();
  UpdateDirtyIcons();
  RebuildActionBarReferences();
}

std::vector<MacroDocument> MacroCatalog::SnapshotMacros(
    const MacroScope scope) const {
  std::lock_guard lock(mutex_);
  std::vector<MacroDocument> result;
  result.reserve(scope == MacroScope::kCharacter
                     ? store_.character_count_
                     : store_.account_count_);
  for (const auto& id : store_.slots_) {
    if (!id) {
      continue;
    }
    const auto it = store_.documents_.find(*id);
    if (it != store_.documents_.end() &&
        it->second.scope == scope) {
      result.push_back(it->second);
    }
  }
  return result;
}

bool MacroCatalog::ConsumeDirty() {
  std::lock_guard lock(mutex_);
  return std::exchange(store_.dirty_, false);
}

}

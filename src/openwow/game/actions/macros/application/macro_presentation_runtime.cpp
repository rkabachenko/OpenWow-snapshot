#include "openwow/game/actions/macros/application/macro_presentation_runtime.h"

#include <utility>

namespace openwow::game::actions::macros {

void MacroPresentationRuntime::QueueIconUpdate(const MacroId id) {
  std::lock_guard lock(mutex_);
  if (const auto existing =
          pending_icon_update_positions_.find(id);
      existing != pending_icon_update_positions_.end()) {
    pending_icon_updates_.erase(existing->second);
  }
  pending_icon_updates_.push_front(id);
  pending_icon_update_positions_[id] =
      pending_icon_updates_.begin();
}

void MacroPresentationRuntime::RemoveIconUpdate(const MacroId id) {
  std::lock_guard lock(mutex_);
  const auto existing = pending_icon_update_positions_.find(id);
  if (existing == pending_icon_update_positions_.end()) {
    return;
  }
  pending_icon_updates_.erase(existing->second);
  pending_icon_update_positions_.erase(existing);
}

void MacroPresentationRuntime::ClearIconUpdates() {
  std::lock_guard lock(mutex_);
  pending_icon_updates_.clear();
  pending_icon_update_positions_.clear();
}

std::vector<MacroId> MacroPresentationRuntime::PendingIconUpdates() const {
  std::lock_guard lock(mutex_);
  return {pending_icon_updates_.begin(), pending_icon_updates_.end()};
}

void MacroPresentationRuntime::SetRefreshCallback(
    RefreshCallback callback) {
  std::lock_guard lock(mutex_);
  refresh_callback_ = std::move(callback);
}

MacroPresentationRuntime::RefreshCallback
MacroPresentationRuntime::Refresh() const {
  std::lock_guard lock(mutex_);
  return refresh_callback_;
}

void MacroPresentationRuntime::SetActionBarSlotProvider(
    ActionBarSlotProvider provider) {
  std::lock_guard lock(mutex_);
  action_bar_slot_provider_ = std::move(provider);
}

MacroPresentationRuntime::ActionBarSlotProvider
MacroPresentationRuntime::ActionBarSlots() const {
  std::lock_guard lock(mutex_);
  return action_bar_slot_provider_;
}

void MacroPresentationRuntime::SetShapeshiftSlotProvider(
    ShapeshiftSlotProvider provider) {
  std::lock_guard lock(mutex_);
  shapeshift_slot_provider_ = std::move(provider);
}

MacroPresentationRuntime::ShapeshiftSlotProvider
MacroPresentationRuntime::ShapeshiftSlots() const {
  std::lock_guard lock(mutex_);
  return shapeshift_slot_provider_;
}

void MacroPresentationRuntime::SetCastSequenceTokenResolver(
    CastSequenceTokenResolver resolver) {
  std::lock_guard lock(mutex_);
  cast_sequence_token_resolver_ = std::move(resolver);
}

MacroPresentationRuntime::CastSequenceTokenResolver
MacroPresentationRuntime::CastSequenceToken() const {
  std::lock_guard lock(mutex_);
  return cast_sequence_token_resolver_;
}

void MacroPresentationRuntime::SetMacrosChangedCallback(
    MacrosChangedCallback callback) {
  std::lock_guard lock(mutex_);
  macros_changed_callback_ = std::move(callback);
}

MacroPresentationRuntime::MacrosChangedCallback
MacroPresentationRuntime::MacrosChanged() const {
  std::lock_guard lock(mutex_);
  return macros_changed_callback_;
}

void MacroPresentationRuntime::SetIconResolutionQueries(
    IconResolutionQueries queries) {
  std::lock_guard lock(mutex_);
  icon_resolution_queries_ = std::move(queries);
}

MacroPresentationRuntime::IconResolutionQueries
MacroPresentationRuntime::IconResolution() const {
  std::lock_guard lock(mutex_);
  return icon_resolution_queries_;
}

void MacroPresentationRuntime::SetIconPathResolver(
    IconPathResolver resolver) {
  std::lock_guard lock(mutex_);
  icon_path_resolver_ = std::move(resolver);
}

MacroPresentationRuntime::IconPathResolver
MacroPresentationRuntime::IconPath() const {
  std::lock_guard lock(mutex_);
  return icon_path_resolver_;
}

void MacroPresentationRuntime::SetClearActionBarMacro(
    ClearActionBarMacro callback) {
  std::lock_guard lock(mutex_);
  clear_action_bar_macro_ = std::move(callback);
}

MacroPresentationRuntime::ClearActionBarMacro
MacroPresentationRuntime::ClearActionBar() const {
  std::lock_guard lock(mutex_);
  return clear_action_bar_macro_;
}

void MacroPresentationRuntime::SetActiveShapeshiftFormProvider(
    ActiveShapeshiftFormProvider provider) {
  std::lock_guard lock(mutex_);
  active_shapeshift_form_provider_ = std::move(provider);
}

MacroPresentationRuntime::ActiveShapeshiftFormProvider
MacroPresentationRuntime::ActiveShapeshiftForm() const {
  std::lock_guard lock(mutex_);
  return active_shapeshift_form_provider_;
}

}

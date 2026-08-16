#include "openwow/game/actions/macros/application/macro_condition_runtime.h"

#include <utility>

namespace openwow::game::actions::macros {

void MacroConditionRuntime::SetModifiedClickQuery(
    ModifiedClickQuery query) {
  std::lock_guard lock(mutex_);
  modified_click_query_ = std::move(query);
}

std::optional<bool> MacroConditionRuntime::QueryModifiedClick(
    const std::optional<std::string_view> action,
    const std::uint16_t modifier_state,
    const std::string_view mouse_button) const {
  ModifiedClickQuery query;
  {
    std::lock_guard lock(mutex_);
    query = modified_click_query_;
  }
  return query ? std::optional<bool>(
                     query(action, modifier_state, mouse_button))
               : std::nullopt;
}

void MacroConditionRuntime::SetUnknownConditionHandler(
    UnknownConditionHandler handler) {
  std::lock_guard lock(mutex_);
  unknown_condition_handler_ = std::move(handler);
}

void MacroConditionRuntime::ReportUnknownCondition(
    const std::string_view condition) const {
  UnknownConditionHandler handler;
  {
    std::lock_guard lock(mutex_);
    handler = unknown_condition_handler_;
  }
  if (handler) {
    handler(condition);
  }
}

void MacroConditionRuntime::SetSnapshotProvider(
    SnapshotProvider provider) {
  std::lock_guard lock(mutex_);
  snapshot_provider_ = std::move(provider);
}

bool MacroConditionRuntime::Evaluate(
    const std::string_view conditions,
    const std::string_view target) const {
  SnapshotProvider snapshots;
  UnknownConditionHandler unknown;
  {
    std::lock_guard lock(mutex_);
    snapshots = snapshot_provider_;
    unknown = unknown_condition_handler_;
  }
  return rules::MacroConditionRules::Evaluate(
      conditions, target, snapshots, unknown);
}

rules::SecureConditionBlockResult
MacroConditionRuntime::EvaluateBlock(
    const std::string_view condition_block) const {
  SnapshotProvider snapshots;
  UnknownConditionHandler unknown;
  {
    std::lock_guard lock(mutex_);
    snapshots = snapshot_provider_;
    unknown = unknown_condition_handler_;
  }
  return rules::MacroConditionRules::EvaluateBlock(
      condition_block, snapshots, unknown);
}

}

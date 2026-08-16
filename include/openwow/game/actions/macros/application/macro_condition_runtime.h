#pragma once

#include "openwow/game/actions/macros/rules/macro_condition_rules.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string_view>

namespace openwow::game::actions::macros {

class MacroConditionRuntime {
 public:
  using ModifiedClickQuery =
      std::function<bool(std::optional<std::string_view>,
                         std::uint16_t, std::string_view)>;
  using UnknownConditionHandler = std::function<void(std::string_view)>;
  using SnapshotProvider =
      rules::MacroConditionRules::SnapshotProvider;

  void SetModifiedClickQuery(ModifiedClickQuery query);
  [[nodiscard]] std::optional<bool> QueryModifiedClick(
      std::optional<std::string_view> action,
      std::uint16_t modifier_state,
      std::string_view mouse_button) const;

  void SetUnknownConditionHandler(UnknownConditionHandler handler);
  void ReportUnknownCondition(std::string_view condition) const;
  void SetSnapshotProvider(SnapshotProvider provider);
  [[nodiscard]] bool Evaluate(std::string_view conditions,
                              std::string_view target = {}) const;
  [[nodiscard]] rules::SecureConditionBlockResult EvaluateBlock(
      std::string_view condition_block) const;

 private:
  mutable std::mutex mutex_;
  ModifiedClickQuery modified_click_query_;
  UnknownConditionHandler unknown_condition_handler_;
  SnapshotProvider snapshot_provider_;
};

}

#include "openwow/game/actions/macros/application/macro_catalog.h"

#include "openwow/game/actions/macros/rules/macro_body_rules.h"

#include <utility>

namespace openwow::game {

void MacroCatalog::ExecuteBody(
    const std::uint32_t slot_index,
    std::optional<actions::macros::MacroInputButton> button) {
  std::lock_guard lock(mutex_);
  if (slot_index >= store_.slots_.size() ||
      !store_.slots_[slot_index]) {
    return;
  }
  const auto id = *store_.slots_[slot_index];
  const auto it = store_.documents_.find(id);
  if (it == store_.documents_.end() ||
      !CanPerformLocked(
          actions::macros::MacroProtectedOperation::kExecuteCommands)) {
    return;
  }
  execution_runtime_.Run(id, std::move(button), [&] {
    DispatchExecutableLinesLocked(it->second.body);
    return CanPerformLocked(
        actions::macros::MacroProtectedOperation::kExecuteCommands);
  });
}

void MacroCatalog::ExecuteByUniqueId(
    const MacroId id,
    std::optional<actions::macros::MacroInputButton> button) {
  std::lock_guard lock(mutex_);
  const auto it = store_.documents_.find(id);
  if (it == store_.documents_.end() ||
      !CanPerformLocked(
          actions::macros::MacroProtectedOperation::kExecuteCommands)) {
    return;
  }
  execution_runtime_.Run(id, std::move(button), [&] {
    DispatchExecutableLinesLocked(it->second.body);
    return CanPerformLocked(
        actions::macros::MacroProtectedOperation::kExecuteCommands);
  });
}

void MacroCatalog::ExecuteBodyText(
    const std::string& body,
    std::optional<actions::macros::MacroInputButton> button) {
  std::lock_guard lock(mutex_);
  if (!CanPerformLocked(
          actions::macros::MacroProtectedOperation::kExecuteCommands)) {
    return;
  }
  execution_runtime_.Run(std::nullopt, std::move(button), [&] {
    DispatchExecutableLinesLocked(body);
    return CanPerformLocked(
        actions::macros::MacroProtectedOperation::kExecuteCommands);
  });
}

void MacroCatalog::StopMacro() {
  std::lock_guard lock(mutex_);
  if (CanPerformLocked(
          actions::macros::MacroProtectedOperation::kExecuteCommands)) {
    execution_runtime_.Stop();
  }
}

bool MacroCatalog::IsRunningMacro() const {
  std::lock_guard lock(mutex_);
  return execution_runtime_.active();
}

std::optional<MacroId> MacroCatalog::GetRunningMacroId() const {
  std::lock_guard lock(mutex_);
  return execution_runtime_.macro_id();
}

std::int32_t MacroCatalog::GetRunningMacroSlot() const {
  std::lock_guard lock(mutex_);
  const auto id = execution_runtime_.macro_id();
  return id ? FindSlotIndexLocked(*id) : -1;
}

std::optional<actions::macros::MacroInputButton>
MacroCatalog::RunningMacroInputButton() const {
  std::lock_guard lock(mutex_);
  return execution_runtime_.button();
}

bool MacroCatalog::WithTransientButtonContext(
    const std::string_view button,
    const std::function<bool()>& operation) {
  std::lock_guard lock(mutex_);
  return execution_runtime_.WithTransientButton(
      actions::macros::MacroInputButton::FromCompatibilityText(button),
      operation);
}

bool MacroCatalog::HasRetailProtectedActionFlag(
    const std::uint32_t flag_mask) const {
  std::lock_guard lock(mutex_);
  return execution_runtime_.HasRetailProtectedActionFlag(flag_mask);
}

void MacroCatalog::ConsumeRetailProtectedActionFlag(
    const std::uint32_t flag_mask) {
  std::lock_guard lock(mutex_);
  execution_runtime_.ConsumeRetailProtectedActionFlag(flag_mask);
}

std::string MacroCatalog::ResolveMacroBody(
    const std::string& body) const {
  return actions::macros::rules::MacroBodyRules::ResolveExecutableBody(
      body, [this](const std::string_view options) {
        return ParseSecureCommandOptions(options);
      });
}

std::string MacroCatalog::EvaluateConditional(
    const std::string& conditional) const {
  return condition_runtime_.Evaluate(conditional)
             ? conditional
             : std::string{};
}

MacroCatalog::SecureCommandOptionResult
MacroCatalog::ParseSecureCommandOptions(
    const std::string_view options) const {
  return secure_command_option_parser_.Parse(
      options, [this](const std::string_view condition) {
        return condition_runtime_.EvaluateBlock(condition);
      });
}

void MacroCatalog::SetChatCommandHandler(
    ChatCommandHandler handler) {
  std::lock_guard lock(mutex_);
  execution_runtime_.SetCommandHandler(std::move(handler));
}

void MacroCatalog::SetProtectionGate(ProtectionGate gate) {
  std::lock_guard lock(mutex_);
  execution_runtime_.SetProtectionGate(std::move(gate));
}

bool MacroCatalog::CanPerform(
    const actions::macros::MacroProtectedOperation operation) const {
  std::lock_guard lock(mutex_);
  return CanPerformLocked(operation);
}

void MacroCatalog::SetCastSequenceTokenResolver(
    CastSequenceTokenResolver resolver) {
  presentation_runtime_.SetCastSequenceTokenResolver(
      std::move(resolver));
}

std::optional<std::string> MacroCatalog::ResolveCastSequenceToken(
    const std::string_view body) const {
  const auto resolver = presentation_runtime_.CastSequenceToken();
  return resolver ? resolver(body) : std::nullopt;
}

void MacroCatalog::SetModifiedClickConditionQuery(
    ModifiedClickConditionQuery query) {
  condition_runtime_.SetModifiedClickQuery(std::move(query));
}

std::optional<bool> MacroCatalog::QueryModifiedClickCondition(
    const std::optional<std::string_view> action,
    const std::uint16_t modifier_state,
    const std::string_view mouse_button) const {
  return condition_runtime_.QueryModifiedClick(
      action, modifier_state, mouse_button);
}

void MacroCatalog::SetUnknownConditionHandler(
    UnknownConditionHandler handler) {
  condition_runtime_.SetUnknownConditionHandler(std::move(handler));
}

void MacroCatalog::SetConditionSnapshotProvider(
    ConditionSnapshotProvider provider) {
  condition_runtime_.SetSnapshotProvider(std::move(provider));
}

}

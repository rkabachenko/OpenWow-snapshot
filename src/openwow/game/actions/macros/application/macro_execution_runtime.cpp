#include "openwow/game/actions/macros/application/macro_execution_runtime.h"

#include <utility>

namespace openwow::game::actions::macros {

void MacroExecutionRuntime::SetCommandHandler(CommandHandler handler) {
  command_handler_ = std::move(handler);
}

void MacroExecutionRuntime::SetProtectionGate(ProtectionGate gate) {
  protection_gate_ = std::move(gate);
}

void MacroExecutionRuntime::DispatchCommand(const std::string& command) const {
  if (command_handler_) {
    command_handler_(command);
  }
}

bool MacroExecutionRuntime::CanPerform(
    const MacroProtectedOperation operation) const {
  return !protection_gate_ || protection_gate_(operation);
}

void MacroExecutionRuntime::Run(
    std::optional<MacroId> macro_id,
    std::optional<MacroInputButton> button,
    const std::function<bool()>& dispatch_and_should_restore) {
  const Session previous = session_;
  session_ = {
      .active = true,
      .macro_id = macro_id,
      .button = std::move(button),
      .retail_protected_action_flags =
          previous.active ? previous.retail_protected_action_flags
                          : kInitialRetailProtectedActionFlags,
  };
  if (dispatch_and_should_restore()) {
    session_ = previous;
  }
}

bool MacroExecutionRuntime::WithTransientButton(
    std::optional<MacroInputButton> button,
    const std::function<bool()>& operation) {
  const Session previous = session_;
  session_.button = std::move(button);
  try {
    const bool result = operation();
    session_ = previous;
    return result;
  } catch (...) {
    session_ = previous;
    throw;
  }
}

void MacroExecutionRuntime::Stop() noexcept {
  session_ = {};
}

void MacroExecutionRuntime::Reset() noexcept {
  session_ = {};
}

bool MacroExecutionRuntime::active() const noexcept {
  return session_.active;
}

std::optional<MacroId> MacroExecutionRuntime::macro_id() const noexcept {
  return session_.active ? session_.macro_id : std::nullopt;
}

std::optional<MacroInputButton> MacroExecutionRuntime::button() const {
  return session_.button;
}

bool MacroExecutionRuntime::HasRetailProtectedActionFlag(
    const std::uint32_t flag_mask) const noexcept {
  return session_.active &&
         (session_.retail_protected_action_flags & flag_mask) != 0;
}

void MacroExecutionRuntime::ConsumeRetailProtectedActionFlag(
    const std::uint32_t flag_mask) noexcept {
  if (session_.active) {
    session_.retail_protected_action_flags &= ~flag_mask;
  }
}

}

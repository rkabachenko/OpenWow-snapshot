#pragma once

#include "openwow/game/actions/macros/model/macro_id.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace openwow::game::actions::macros {

enum class MacroProtectedOperation : std::uint8_t {
  kExecuteCommands,
  kModifyCatalog,
};

class MacroInputButton {
 public:
  [[nodiscard]] static MacroInputButton FromCompatibilityText(
      std::string_view value) {
    return MacroInputButton(std::string(value));
  }

  [[nodiscard]] std::string_view value() const noexcept { return value_; }

 private:
  explicit MacroInputButton(std::string value) : value_(std::move(value)) {}

  std::string value_;
};

class MacroExecutionRuntime {
 public:
  using CommandHandler = std::function<void(const std::string&)>;
  using ProtectionGate = std::function<bool(MacroProtectedOperation)>;

  static constexpr std::uint32_t kInitialRetailProtectedActionFlags =
      0xFFFFFFFFu;

  void SetCommandHandler(CommandHandler handler);
  void SetProtectionGate(ProtectionGate gate);
  void DispatchCommand(const std::string& command) const;
  [[nodiscard]] bool CanPerform(MacroProtectedOperation operation) const;

  void Run(std::optional<MacroId> macro_id,
           std::optional<MacroInputButton> button,
           const std::function<bool()>& dispatch_and_should_restore);
  bool WithTransientButton(std::optional<MacroInputButton> button,
                           const std::function<bool()>& operation);

  void Stop() noexcept;
  void Reset() noexcept;

  [[nodiscard]] bool active() const noexcept;
  [[nodiscard]] std::optional<MacroId> macro_id() const noexcept;
  [[nodiscard]] std::optional<MacroInputButton> button() const;
  [[nodiscard]] bool HasRetailProtectedActionFlag(
      std::uint32_t flag_mask) const noexcept;
  void ConsumeRetailProtectedActionFlag(std::uint32_t flag_mask) noexcept;

 private:
  struct Session {
    bool active{false};
    std::optional<MacroId> macro_id;
    std::optional<MacroInputButton> button;
    std::uint32_t retail_protected_action_flags{0};
  };

  Session session_;
  CommandHandler command_handler_;
  ProtectionGate protection_gate_;
};

}

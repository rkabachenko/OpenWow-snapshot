
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "openwow/foundation/compiler/printf_format.h"

namespace openwow::core {

struct LoginDialogEvent {
  std::vector<std::string> args;
};

class LoginStateDialogHandler {
 public:
  using ResolveStringFn = std::function<std::string(std::string_view)>;

  void Reset();

  [[nodiscard]] std::optional<LoginDialogEvent> Poll(
      std::int32_t current_state,
      std::int32_t current_result,
      const ResolveStringFn& resolve_string);

 private:
  std::int32_t last_state_{-1};
  std::int32_t last_result_{-1};
};

inline void Login_SetScreen(const std::string& screen_name,
                            const std::function<void(const std::string&)>& fire) {
    if (fire) fire(screen_name);
}

class ConnectionLogger {
 public:
    static ConnectionLogger& Instance();

    void Log(const char* message);
    void Shutdown();

 private:
    ConnectionLogger() = default;
};

class LoginConsoleDiagnostics {
 public:
  static LoginConsoleDiagnostics& Instance();

  void SetEnabled(bool enabled);
  [[nodiscard]] bool IsEnabled() const;
  OPENWOW_PRINTF_FORMAT(2, 3) void EnqueueFormattedLine(const char* format, ...);
  void DrainToConsole();
  void Shutdown();

 private:
  LoginConsoleDiagnostics() = default;
};

std::uint64_t GetAccountDataTimestamp();

}

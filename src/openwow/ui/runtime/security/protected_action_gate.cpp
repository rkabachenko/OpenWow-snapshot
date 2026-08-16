#include "openwow/ui/runtime/security/protected_action_gate.h"

#include "openwow/core/decimal_parse.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/secure_execution.h"

extern "C" {
#include <lua.hpp>
}

#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

namespace openwow::ui::game {
namespace {

class TaintLogRuntime final {
 public:
  void SetEnabled(bool enabled) noexcept {
    enabled_.store(enabled, std::memory_order_relaxed);
  }
  [[nodiscard]] bool enabled() const noexcept {
    return enabled_.load(std::memory_order_relaxed);
  }

 private:
  std::atomic_bool enabled_{false};
};

constexpr TaintSourceId kSecureTaintSourceId = 0;
constexpr TaintSourceId kAnonymousTaintSourceId = 1;

constexpr std::string_view kAnonymousTaintLogName = "MACRO_TAINT";

constexpr std::string_view kInCombatTaintLogPrefix =
    "An action was blocked in combat because of taint from ";
constexpr std::string_view kTaintLogPrefix =
    "An action was blocked because of taint from ";

TaintLogRuntime& TaintLog() {
  static TaintLogRuntime runtime;
  return runtime;
}

constexpr std::string_view kUnknownActionName = "UNKNOWN";
constexpr std::string_view kUnnamedObjectName = "<unnamed>";

bool PushMethodReceiverPrefix(lua_State* state, std::string* out) {
  if (lua_type(state, 1) != LUA_TTABLE) {
    return false;
  }
  lua_pushvalue(state, 1);
  const int receiver_index = lua_gettop(state);

  lua_pushliteral(state, "__ow_type");
  lua_rawget(state, receiver_index);
  const bool is_script_object = lua_isstring(state, -1) != 0;
  lua_pop(state, 1);
  if (!is_script_object) {
    lua_pop(state, 1);
    return false;
  }

  lua_pushliteral(state, "__ow_name");
  lua_rawget(state, receiver_index);
  const char* name = lua_tostring(state, -1);
  *out = (name != nullptr && name[0] != '\0')
             ? std::string(name)
             : std::string(kUnnamedObjectName);
  lua_pop(state, 2);
  return true;
}

std::string ResolveFailedActionName(lua_State* state) {
  if (state == nullptr) {
    return std::string(kUnknownActionName) + "()";
  }

  lua_Debug debug{};
  if (lua_getstack(state, 0, &debug) == 0 ||
      lua_getinfo(state, "n", &debug) == 0 || debug.name == nullptr ||
      debug.name[0] == '\0') {
    return std::string(kUnknownActionName) + "()";
  }

  std::string name(debug.name);
  if (debug.namewhat != nullptr && std::strcmp(debug.namewhat, "method") == 0) {
    std::string receiver;
    if (PushMethodReceiverPrefix(state, &receiver)) {
      name = receiver + ":" + name;
    }
  }
  return name + "()";
}

std::string ResolveTaintLogCallSite(lua_State* state) {
  if (state == nullptr) {
    return {};
  }
  for (int level = 0;; ++level) {
    lua_Debug debug{};
    if (lua_getstack(state, level, &debug) == 0) break;
    if (lua_getinfo(state, "Sl", &debug) == 0) continue;
    if (debug.source == nullptr || debug.currentline <= 0) continue;
    std::string source(debug.source);
    if (source.starts_with('@') || source.starts_with('=')) {
      source.erase(0, 1);
    }
    return source + ':' + std::to_string(debug.currentline);
  }
  return {};
}

}

namespace {

constexpr std::uint32_t kUnconditionallyForbiddenKinds = 0x0003003fu;
constexpr std::uint32_t kHardwareEventGrantedKinds = 0x00bc83c0u;
constexpr std::uint32_t kOutOfCombatGrantedKinds = 0x00407800u;
constexpr int kActionKindCount = 0x18;

constexpr int kMovementActionKind = 0;

}

int GameUI_CanPerformProtectedAction(const int action_kind) {
  auto& secure = SecureExecution::Get();
  if (secure.CurrentTaint() != kSecureTaintSourceId && action_kind >= 0 &&
      action_kind < kActionKindCount) {
    const std::uint32_t bit = 1u << static_cast<unsigned>(action_kind);
    bool granted = true;
    auto mode = ProtectedActionFailureMode::kForbidden;
    if ((bit & kHardwareEventGrantedKinds) != 0u) {
      granted = secure.HardwareActionGranted();
      mode = ProtectedActionFailureMode::kBlocked;
    } else if ((bit & kUnconditionallyForbiddenKinds) != 0u) {
      granted = false;
    } else if ((bit & kOutOfCombatGrantedKinds) != 0u) {
      granted = !secure.InCombatLockdown();
      mode = ProtectedActionFailureMode::kBlockedType4;
    }
    if (!granted) {
      GameUI_ReportProtectedActionFailure(mode);
      return 0;
    }
  }

  if (action_kind == kMovementActionKind) {
    secure.SetHardwareActionGrant(false);
  }
  return 1;
}

int GameUI_CanPerformTaintForbiddenAction() {
  auto& secure = SecureExecution::Get();
  if (secure.CurrentTaint() == kSecureTaintSourceId) {
    return 1;
  }
  GameUI_ReportProtectedActionFailure(ProtectedActionFailureMode::kForbidden);
  return 0;
}

int GameUI_CanPerformHardwareEventAction() {
  auto& secure = SecureExecution::Get();
  if (secure.CurrentTaint() == kSecureTaintSourceId ||
      secure.HardwareActionGranted()) {
    return 1;
  }
  GameUI_ReportProtectedActionFailure(ProtectedActionFailureMode::kBlocked);
  return 0;
}

void GameUI_ReportProtectedActionFailure(
    lua_State* state, const ProtectedActionFailureMode mode) {

  static bool reporting = false;
  if (reporting) {
    return;
  }
  reporting = true;
  struct ReportingGuard {
    bool* flag;
    ~ReportingGuard() { *flag = false; }
  } const reporting_guard{&reporting};

  const std::string call = ResolveFailedActionName(state);

  const auto& secure = SecureExecution::Get();
  const auto taint_source = secure.CurrentTaint(state);
  if (taint_source == kSecureTaintSourceId) {
    return;
  }

  const bool anonymous_source = taint_source == kAnonymousTaintSourceId;
  if (!anonymous_source) {
    const char* event = mode == ProtectedActionFailureMode::kForbidden
                            ? events::ADDON_ACTION_FORBIDDEN
                            : events::ADDON_ACTION_BLOCKED;
    ScriptEventDispatch::Get().FireEventArgs(
        event, {secure.TaintSourceName(taint_source), call});
  } else {
    const char* event = mode == ProtectedActionFailureMode::kForbidden
                            ? events::MACRO_ACTION_FORBIDDEN
                            : events::MACRO_ACTION_BLOCKED;
    ScriptEventDispatch::Get().FireEventArgs(event, {call});
  }

  if (!TaintLog().enabled()) {
    return;
  }
  std::filesystem::create_directories("Logs");
  std::ofstream log("Logs/taint.log", std::ios::app);
  log << (mode == ProtectedActionFailureMode::kBlockedType4
              ? kInCombatTaintLogPrefix
              : kTaintLogPrefix)
      << (anonymous_source ? std::string(kAnonymousTaintLogName)
                           : secure.TaintSourceName(taint_source))
      << " - " << ResolveTaintLogCallSite(state) << ' ' << call << '\n';
}

void GameUI_ReportProtectedActionFailure(
    const ProtectedActionFailureMode mode) {
  GameUI_ReportProtectedActionFailure(
      ScriptEventDispatch::Get().GetLuaState(), mode);
}

bool GameUI_TaintLogCVarValidationCallback(
    const std::string&, const std::string&, const std::string& new_value) {
  TaintLog().SetEnabled(
      openwow::core::ParseSignedDecimalLikeSub76F0D0(new_value) > 0);
  return true;
}

bool GameUI_IsTaintLogEventSinkActive() {
  return TaintLog().enabled();
}

void GameUI_ResetTaintLogRuntimeForTesting() {
  TaintLog().SetEnabled(false);
}

}

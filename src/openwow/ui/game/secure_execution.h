
#pragma once

#include "openwow/ui/lua_taint_api.h"

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct lua_State;

namespace openwow::ui::lua {
struct NativeBindingCatalog;
}

namespace openwow::ui::game {

enum class ExecutionContext {
  Secure,
  Insecure,
};

enum class SecureLuaBindingProfile {
  Game,
  Glue,
};

[[nodiscard]] SecureLuaBindingProfile GetSecureLuaBindingProfile(lua_State* state);

using TaintSourceId = int;

class SecureExecution {
 public:
  static SecureExecution& Get();

  [[nodiscard]] ExecutionContext GetContext() const;
  [[nodiscard]] bool IsSecure() const;
  [[nodiscard]] bool IsSecure(lua_State* state) const;
  [[nodiscard]] TaintSourceId CurrentTaint(lua_State* state = nullptr) const;

  void PushSecure(lua_State* state = nullptr);
  void PushInsecure(lua_State* state = nullptr,
                    std::string_view source = {});
  void PushTaint(lua_State* state, TaintSourceId source);
  void Pop();

  [[nodiscard]] TaintSourceId InternTaintSource(std::string_view source);
  [[nodiscard]] std::string TaintSourceName(TaintSourceId source) const;

  void TaintVariable(const std::string& name,
                     std::string_view source = {});
  void TaintTable(const std::string& table_prefix,
                  std::string_view source = {});
  [[nodiscard]] bool IsVariableTainted(const std::string& name) const;
  [[nodiscard]] TaintSourceId VariableTaint(const std::string& name) const;
  void ClearTaint(const std::string& name);
  void ClearAllTaint();

  void SetInCombatLockdown(bool in_lockdown);
  [[nodiscard]] bool InCombatLockdown() const;

  void SetHardwareActionGrant(bool granted);
  [[nodiscard]] bool HardwareActionGranted() const;

  class HardwareActionGrantScope {
   public:
    HardwareActionGrantScope() {
      SecureExecution::Get().SetHardwareActionGrant(true);
    }
    ~HardwareActionGrantScope() {
      SecureExecution::Get().SetHardwareActionGrant(false);
    }
    HardwareActionGrantScope(const HardwareActionGrantScope&) = delete;
    HardwareActionGrantScope& operator=(const HardwareActionGrantScope&) =
        delete;
  };

  [[nodiscard]] std::size_t ContextDepth() const noexcept;
  void UnwindContextTo(std::size_t depth);

  void ForceInsecure(lua_State* state = nullptr);
  void Reset();

  class SecureScope {
   public:
    explicit SecureScope(lua_State* state = nullptr) {
      SecureExecution::Get().PushSecure(state);
    }
    ~SecureScope() { SecureExecution::Get().Pop(); }
    SecureScope(const SecureScope&) = delete;
    SecureScope& operator=(const SecureScope&) = delete;
  };

  class InsecureScope {
   public:
    explicit InsecureScope(lua_State* state = nullptr,
                           std::string_view source = {}) {
      SecureExecution::Get().PushInsecure(state, source);
    }
    ~InsecureScope() { SecureExecution::Get().Pop(); }
    InsecureScope(const InsecureScope&) = delete;
    InsecureScope& operator=(const InsecureScope&) = delete;
  };

  class TaintScope {
   public:
    TaintScope(lua_State* state, TaintSourceId source) {
      SecureExecution::Get().PushTaint(state, source);
    }
    ~TaintScope() { SecureExecution::Get().Pop(); }
    TaintScope(const TaintScope&) = delete;
    TaintScope& operator=(const TaintScope&) = delete;
  };

  static void RegisterLuaBindings(
      lua_State* state,
      SecureLuaBindingProfile profile = SecureLuaBindingProfile::Game);

 private:
  struct ContextFrame {
    ExecutionContext context{ExecutionContext::Secure};
    lua_State* state{nullptr};
    LuaExecutionTaintState previous_lua_state{};
    TaintSourceId source{0};
  };

  SecureExecution();
  void PushContext(lua_State* state, TaintSourceId source);

  std::vector<ContextFrame> context_stack_;
  std::unordered_map<std::string, TaintSourceId> taint_source_ids_;
  std::vector<std::string> taint_source_names_;
  std::unordered_map<std::string, TaintSourceId> tainted_vars_;
  mutable std::mutex taint_source_mutex_;
  bool in_combat_lockdown_{false};
  bool hardware_action_grant_{false};
};

[[nodiscard]] openwow::ui::lua::NativeBindingCatalog
SecurityNativeBindingCatalog();

}

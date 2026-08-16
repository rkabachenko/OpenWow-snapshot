
#include "openwow/ui/game/secure_execution.h"

#include "openwow/ui/game/lua_cpu_profiler.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/lua_post_hook_closure.h"
#include "openwow/ui/lua_taint_api.h"
#include "openwow/ui/runtime/lua/lua_composition.h"

extern "C" {
#include "ldo.h"
#include <lua.hpp>
}

#include <limits>
#include <string_view>

namespace openwow::ui::game {

namespace {

void PushWowBool(lua_State *L, const bool value) {
  if (value) {
    lua_pushnumber(L, 1.0);
    return;
  }

  lua_pushnil(L);
}

constexpr std::string_view kAnonymousTaintSourceName = "";
constexpr std::string_view kForceInsecureTaintSourceName = "*** TaintForced ***";
constexpr TaintSourceId kAnonymousTaintSourceId = 1;
constexpr TaintSourceId kForceInsecureTaintSourceId = 2;

class LuaTaintStateGuard {
 public:
  explicit LuaTaintStateGuard(lua_State* const state)
      : state_(state), saved_(openwow::ui::lua_get_execution_taint_state(state)) {}

  ~LuaTaintStateGuard() {
    Restore();
  }

  void Set(const TaintSourceId source, const bool increment_tracking = true) {
    auto next = saved_;
    next.source = source;
    if (increment_tracking &&
        next.tracking_depth != std::numeric_limits<std::uint32_t>::max()) {
      ++next.tracking_depth;
    }
    openwow::ui::lua_set_execution_taint_state(state_, next);
  }

  [[nodiscard]] const LuaExecutionTaintState& saved() const { return saved_; }

  void Restore() const {
    openwow::ui::lua_set_execution_taint_state(state_, saved_);
  }

 private:
  lua_State* state_;
  LuaExecutionTaintState saved_;
};

}

SecureExecution& SecureExecution::Get() {
  static SecureExecution instance;
  return instance;
}

namespace {

std::size_t SaveHostContextDepth(lua_State*) {
  return SecureExecution::Get().ContextDepth();
}

void RestoreHostContextDepth(lua_State*, const std::size_t token) {
  SecureExecution::Get().UnwindContextTo(token);
}

}

SecureExecution::SecureExecution() {
  luaW_savehostcontext = &SaveHostContextDepth;
  luaW_restorehostcontext = &RestoreHostContextDepth;
  context_stack_.push_back({});

  taint_source_names_.emplace_back();
  taint_source_names_.emplace_back(kAnonymousTaintSourceName);
  taint_source_names_.emplace_back(kForceInsecureTaintSourceName);
  taint_source_ids_.emplace(kForceInsecureTaintSourceName,
                            kForceInsecureTaintSourceId);
}

ExecutionContext SecureExecution::GetContext() const {
  return context_stack_.empty() ? ExecutionContext::Secure
                                : context_stack_.back().context;
}

bool SecureExecution::IsSecure() const {
  if (!context_stack_.empty() && context_stack_.back().state != nullptr) {
    const auto taint = openwow::ui::lua_get_execution_taint_state(
        context_stack_.back().state);
    if (taint.tracking_depth != 0 || taint.source != 0) {
      return taint.source == 0;
    }
  }
  return GetContext() == ExecutionContext::Secure;
}

bool SecureExecution::IsSecure(lua_State* const state) const {
  if (state != nullptr) {
    const auto taint = openwow::ui::lua_get_execution_taint_state(state);
    if (taint.tracking_depth != 0 || taint.source != 0) {
      return taint.source == 0;
    }
  }
  return IsSecure();
}

TaintSourceId SecureExecution::CurrentTaint(lua_State* const state) const {
  if (state != nullptr) {
    const auto taint = openwow::ui::lua_get_execution_taint_state(state);
    if (taint.tracking_depth != 0 || taint.source != 0) {
      return taint.source;
    }
  }
  if (state == nullptr && !context_stack_.empty() &&
      context_stack_.back().state != nullptr) {
    const auto taint = openwow::ui::lua_get_execution_taint_state(
        context_stack_.back().state);
    if (taint.tracking_depth != 0 || taint.source != 0) {
      return taint.source;
    }
  }
  return context_stack_.empty() ? 0 : context_stack_.back().source;
}

void SecureExecution::PushContext(lua_State* const state,
                                  const TaintSourceId source) {
  ContextFrame frame;
  frame.context = source == 0 ? ExecutionContext::Secure
                              : ExecutionContext::Insecure;
  frame.state = state;
  frame.source = source;
  if (state != nullptr) {
    frame.previous_lua_state =
        openwow::ui::lua_get_execution_taint_state(state);
    auto next = frame.previous_lua_state;
    next.source = source;
    if (next.tracking_depth != std::numeric_limits<std::uint32_t>::max()) {
      ++next.tracking_depth;
    }
    openwow::ui::lua_set_execution_taint_state(state, next);
  }
  context_stack_.push_back(frame);
}

void SecureExecution::PushSecure(lua_State* const state) {
  PushContext(state, 0);
}

void SecureExecution::PushInsecure(lua_State* const state,
                                   const std::string_view source) {
  PushContext(state, InternTaintSource(source));
}

void SecureExecution::PushTaint(lua_State* const state,
                                const TaintSourceId source) {
  PushContext(state, source);
}

void SecureExecution::Pop() {
  if (context_stack_.size() <= 1) {
    return;
  }
  const ContextFrame frame = context_stack_.back();
  context_stack_.pop_back();
  if (frame.state != nullptr) {
    openwow::ui::lua_set_execution_taint_state(frame.state,
                                                frame.previous_lua_state);
  }
}

TaintSourceId SecureExecution::InternTaintSource(
    const std::string_view source) {
  if (source.empty()) {
    return kAnonymousTaintSourceId;
  }
  std::lock_guard lock(taint_source_mutex_);
  const std::string key(source);
  if (const auto found = taint_source_ids_.find(key);
      found != taint_source_ids_.end()) {
    return found->second;
  }
  if (taint_source_names_.size() >=
      static_cast<std::size_t>(std::numeric_limits<TaintSourceId>::max())) {
    return kAnonymousTaintSourceId;
  }
  const auto id = static_cast<TaintSourceId>(taint_source_names_.size());
  taint_source_names_.push_back(key);
  taint_source_ids_.emplace(taint_source_names_.back(), id);
  return id;
}

std::string SecureExecution::TaintSourceName(
    const TaintSourceId source) const {
  std::lock_guard lock(taint_source_mutex_);
  if (source > 0 &&
      static_cast<std::size_t>(source) < taint_source_names_.size()) {
    return taint_source_names_[static_cast<std::size_t>(source)];
  }

  return std::string{};
}

void SecureExecution::TaintVariable(const std::string& name,
                                    const std::string_view source) {
  tainted_vars_.insert_or_assign(name, InternTaintSource(source));
}

void SecureExecution::TaintTable(const std::string& table_prefix,
                                 const std::string_view source) {
  TaintVariable(table_prefix, source);
}

bool SecureExecution::IsVariableTainted(const std::string& name) const {
  return VariableTaint(name) != 0;
}

TaintSourceId SecureExecution::VariableTaint(const std::string& name) const {
  const auto found = tainted_vars_.find(name);
  return found == tainted_vars_.end() ? 0 : found->second;
}

void SecureExecution::ClearTaint(const std::string& name) {
  tainted_vars_.erase(name);
}

void SecureExecution::ClearAllTaint() {
  tainted_vars_.clear();
}

void SecureExecution::SetInCombatLockdown(bool in_lockdown) {
  in_combat_lockdown_ = in_lockdown;
}

bool SecureExecution::InCombatLockdown() const {
  return in_combat_lockdown_;
}

void SecureExecution::SetHardwareActionGrant(const bool granted) {
  hardware_action_grant_ = granted;
}

bool SecureExecution::HardwareActionGranted() const {
  return hardware_action_grant_;
}

void SecureExecution::ForceInsecure(lua_State* const state) {
  constexpr TaintSourceId kForceInsecureSource = kForceInsecureTaintSourceId;
  if (state != nullptr) {
    auto taint = openwow::ui::lua_get_execution_taint_state(state);

    if (taint.tracking_depth == 0 || taint.source != 0) {
      return;
    }
    taint.source = kForceInsecureSource;
    openwow::ui::lua_set_execution_taint_state(state, taint);
  }
  if (!context_stack_.empty()) {
    context_stack_.back().context = ExecutionContext::Insecure;
    context_stack_.back().source = kForceInsecureSource;
  }
}

std::size_t SecureExecution::ContextDepth() const noexcept {
  return context_stack_.size();
}

void SecureExecution::UnwindContextTo(const std::size_t depth) {

  while (context_stack_.size() > depth && context_stack_.size() > 1) {
    Pop();
  }
}

void SecureExecution::Reset() {
  while (context_stack_.size() > 1) {
    Pop();
  }
  context_stack_.assign(1, ContextFrame{});
  tainted_vars_.clear();
  in_combat_lockdown_ = false;
  hardware_action_grant_ = false;
}

static int Lua_securecall_impl(lua_State* L, const char* error_handler_key) {
  LuaTaintStateGuard taint_guard(L);
  const TaintSourceId caller_taint = taint_guard.saved().source;
  taint_guard.Set(0);

  if (!lua_isfunction(L, 1) && !lua_iscfunction(L, 1)) {

    if (lua_isstring(L, 1)) {
      const char* name = lua_tostring(L, 1);
      lua_getglobal(L, name);
      lua_replace(L, 1);
    }
    if (!lua_isfunction(L, 1) && !lua_iscfunction(L, 1)) return 0;
  }

  const int nargs = lua_gettop(L) - 1;
  const int status = lua_pcall(L, nargs, LUA_MULTRET, 0);
  if (status != 0) {

    lua_getfield(L, LUA_REGISTRYINDEX, error_handler_key);
    if (lua_isfunction(L, -1)) {
      lua_pushvalue(L, -2);
      lua_pcall(L, 1, 0, 0);
    } else {
      lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return 0;
  }

  const int result_count = lua_gettop(L);
  for (int index = 1; index <= result_count; ++index) {

    openwow::ui::lua_set_taint(L, index, caller_taint);
  }
  return result_count;
}

constexpr char kSecureLuaBindingProfileRegistryKey[] =
    "openwow.secure_execution.binding_profile";

SecureLuaBindingProfile GetSecureLuaBindingProfile(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, kSecureLuaBindingProfileRegistryKey);
  const auto profile = lua_isnumber(L, -1) != 0
      ? static_cast<SecureLuaBindingProfile>(lua_tointeger(L, -1))
      : SecureLuaBindingProfile::Game;
  lua_pop(L, 1);
  return profile;
}

const char* GetSecureLuaErrorHandlerRegistryKey(lua_State* L) {
  return GetSecureLuaBindingProfile(L) == SecureLuaBindingProfile::Glue
      ? openwow::ui::kGlueLuaErrorHandlerRegistryKey
      : openwow::ui::kGameLuaErrorHandlerRegistryKey;
}

static int Lua_securecall(lua_State* L) {
  return Lua_securecall_impl(L, GetSecureLuaErrorHandlerRegistryKey(L));
}

static int Lua_issecurevariable(lua_State* L) {
  auto& sec = SecureExecution::Get();

  const bool has_table = lua_gettop(L) >= 2 && lua_istable(L, 1) != 0;
  const int name_index = has_table ? 2 : 1;
  if (lua_isstring(L, name_index) == 0) {
    return luaL_error(L, "Usage: issecurevariable([table,] \"variable\")");
  }

  const char* name_value = lua_tostring(L, name_index);
  const std::string name = name_value != nullptr ? name_value : "";

  TaintSourceId value_taint = 0;
  {
    LuaTaintStateGuard taint_guard(L);
    taint_guard.Set(0);
    lua_getfield(L, has_table ? 1 : LUA_GLOBALSINDEX, name.c_str());
    value_taint = openwow::ui::lua_get_taint(L, -1);
    lua_pop(L, 1);
  }

  if (value_taint == 0 && !has_table) {
    value_taint = sec.VariableTaint(name);
  }

  if (value_taint != 0) {
    PushWowBool(L, false);
    const std::string source = sec.TaintSourceName(value_taint);
    lua_pushstring(L, source.c_str());
    return 2;
  }

  PushWowBool(L, true);
  lua_pushnil(L);
  return 2;
}

static int Lua_issecure(lua_State* L) {
  const bool secure = SecureExecution::Get().IsSecure(L);
  PushWowBool(L, secure);
  return 1;
}

static int Lua_forceinsecure(lua_State* L) {
  SecureExecution::Get().ForceInsecure(L);
  return 0;
}

template <int (*Call)(lua_State*, int, int, int)>
static int Lua_hooksecurefunc_impl(lua_State* L, const char* error_handler_key) {
  LuaTaintStateGuard taint_guard(L);
  int tbl_idx = 0;
  int name_idx = 0;
  int hook_idx = 0;

  if (lua_istable(L, 1)) {
    tbl_idx = 1;
    name_idx = 2;
    hook_idx = 3;
  } else {
    name_idx = 1;
    hook_idx = 2;
  }

  if (lua_isstring(L, name_idx) == 0 || lua_type(L, hook_idx) != LUA_TFUNCTION) {
    return luaL_error(L, "Usage: hooksecurefunc([table,] \"function\", hookfunc)");
  }
  const char* func_name = lua_tostring(L, name_idx);

  taint_guard.Set(0);
  if (tbl_idx) {
    lua_getfield(L, tbl_idx, func_name);
  } else {
    lua_getglobal(L, func_name);
  }

  if (!lua_isfunction(L, -1) && !lua_iscfunction(L, -1)) {
    lua_pop(L, 1);

    taint_guard.Restore();
    return luaL_error(L, "hooksecurefunc(): %s is not a function", func_name);
  }
  const TaintSourceId original_taint =
      openwow::ui::lua_get_execution_taint_state(L).source;

  lua_pushvalue(L, hook_idx);

  const TaintSourceId caller_taint = taint_guard.saved().source;
  openwow::ui::lua_set_taint(L, -2, original_taint);
  openwow::ui::lua_set_taint(L, -1, caller_taint);

  openwow::ui::PushLuaCallOriginalThenHookClosure<Call>(L, error_handler_key);

  openwow::ui::lua_set_taint(L, -1, original_taint);
  auto publication_taint = openwow::ui::lua_get_execution_taint_state(L);
  publication_taint.source = original_taint;
  openwow::ui::lua_set_execution_taint_state(L, publication_taint);

  if (tbl_idx) {
    lua_setfield(L, tbl_idx, func_name);
  } else {
    openwow::ui::ReplaceLuaGlobalValue(L, func_name, -1);
    lua_pop(L, 1);
  }

  return 0;
}

static int Lua_hooksecurefunc(lua_State* L) {
  if (GetSecureLuaBindingProfile(L) == SecureLuaBindingProfile::Glue) {
    return Lua_hooksecurefunc_impl<openwow::ui::LuaPlainProtectedCall>(
        L, openwow::ui::kGlueLuaErrorHandlerRegistryKey);
  }
  return Lua_hooksecurefunc_impl<ProfiledPCall>(
      L, openwow::ui::kGameLuaErrorHandlerRegistryKey);
}

static int Lua_InCombatLockdown(lua_State* L) {
  PushWowBool(L, SecureExecution::Get().InCombatLockdown());
  return 1;
}

void SecureExecution::RegisterLuaBindings(
    lua_State* L, const SecureLuaBindingProfile profile) {
  const openwow::ui::LuaGlobalBinding bindings[] = {
      {"securecall", Lua_securecall},
      {"issecurevariable", Lua_issecurevariable},
      {"issecure", Lua_issecure},
      {"forceinsecure", Lua_forceinsecure},
      {"hooksecurefunc", Lua_hooksecurefunc},
  };

  openwow::ui::RegisterLuaGlobals(L, bindings);
  lua_pushinteger(L, static_cast<lua_Integer>(profile));
  lua_setfield(L, LUA_REGISTRYINDEX, kSecureLuaBindingProfileRegistryKey);
}

openwow::ui::lua::NativeBindingCatalog SecurityNativeBindingCatalog() {
  static constexpr openwow::ui::LuaGlobalBinding kSecurityBindings[] = {
      {"InCombatLockdown", Lua_InCombatLockdown},
  };
  return openwow::ui::lua::NativeFunctionCatalog(
      "ui.runtime.security", openwow::ui::lua::BindingScope::kWorld,
      kSecurityBindings);
}

}

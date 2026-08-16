
#pragma once

#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/script_boolean.h"

#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/faction_reaction.h"
#include "openwow/game/inventory/search/action_item_inventory_search.h"
#include "openwow/game/battlenet_utf8.h"
#include "openwow/game/chat_system.h"
#include "openwow/game/death_manager.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/localization.h"
#include "openwow/game/inventory/equipment/equipment_sets.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_icon_resolver.h"
#include "openwow/game/inventory/items/item_on_use_spell.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/pet_system.h"
#include "openwow/game/inventory/operations/inventory_commands.h"
#include "openwow/game/profession_system.h"
#include "openwow/game/quest_poi.h"
#include "openwow/game/quest_turnin_state.h"
#include "openwow/game/reputation_info.h"
#include "openwow/game/skill_line_ability_lookup.h"
#include "openwow/game/spell_cast_lifecycle.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_c_internals.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/spellbook_frame.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/game/targeting.h"
#include "openwow/game/trivial_level.h"
#include "openwow/game/unit_defines.h"
#include "openwow/game/unit_query_bridge.h"
#include "openwow/game/update_fields.h"
#include "openwow/game/world_session.h"
#include "openwow/input/input_manager.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/render/ui/ui_texture_capabilities.h"
#include "openwow/ui/game/quest_log_interleaved.h"
#include "openwow/ui/game/framescript/core/frame_alpha.h"
#include "openwow/ui/game/framescript/core/frame_alpha_lua.h"
#include "openwow/ui/game/lua_effective_visibility.h"
#include "openwow/ui/game/framescript/core/frame_script_invocation.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/api/game_lua_api_companion.h"
#include "openwow/ui/runtime/security/protected_action_gate.h"
#include "openwow/ui/game/lua_addon_memory_tracker.h"
#include "openwow/ui/game/lua_cpu_profiler.h"
#include "openwow/ui/game/lua_table_graph_worklist.h"
#include "openwow/ui/game/runtime/lua_interned_field_key.h"
#include "openwow/ui/game/runtime/texture_render_state_source.h"
#include "openwow/ui/game/runtime/world_ui_runtime_context.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/secure_execution.h"
#include "openwow/ui/game/world_map_system.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/ui/lua_table_field.h"
#include "openwow/ui/widgets/cooldown_visuals.h"
#include "openwow/ui/widgets/script_object.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/foundation/text/utf8.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::ui::game::detail {

inline constexpr const char *kWorldSessionRegistryKey = "openwow.world_session";
inline constexpr const char *kInventoryCommerceContextRegistryKey =
    "openwow.inventory_commerce_context";

bool IsWithinNpcInteractionDistance(const openwow::game::CGPlayer_C &player,
                                    const openwow::game::ObjectManager &object_manager,
                                    std::uint64_t npc_guid);

using namespace openwow::game;

class ScopedNeutralLuaTaint final {
 public:
  explicit ScopedNeutralLuaTaint(lua_State *state)
      : state_(state), saved_(openwow::ui::lua_get_execution_taint_state(state)) {
    openwow::ui::lua_set_execution_taint_state(state_, {});
  }

  ~ScopedNeutralLuaTaint() {
    Restore();
  }

  void Restore() {
    if (state_ == nullptr) {
      return;
    }
    openwow::ui::lua_set_execution_taint_state(state_, saved_);
    state_ = nullptr;
  }

  ScopedNeutralLuaTaint(const ScopedNeutralLuaTaint &) = delete;
  ScopedNeutralLuaTaint &operator=(const ScopedNeutralLuaTaint &) = delete;

 private:
  lua_State *state_;
  openwow::ui::LuaExecutionTaintState saved_;
};

inline constexpr const char *kDeathManagerRegistryKey = "openwow.death_manager";
inline constexpr const char *kTextureVfsRegistryKey = "openwow.texture_vfs";
inline constexpr const char *kCurrentMouseButtonMaskRegistryKey =
    "openwow.current_mouse_button_mask";
inline constexpr const char *kCurrentModifierStateRegistryKey = "openwow.current_modifier_state";
inline constexpr const char *kScriptObjectThisTokenRegistryKey =
    "openwow.script_object_this_tokens";
inline constexpr const char *kScriptObjectThisLookupRegistryKey =
    "openwow.script_object_this_lookup";
inline constexpr const char *kScriptObjectCanonicalTypeRegistryKey =
    "openwow.script_object_canonical_types";
inline constexpr const char *kScriptObjectRuntimeTypeRegistryKey =
    "openwow.script_object_runtime_types";
inline constexpr const char *kLayoutProtectionGenerationRegistryKey =
    "openwow.layout_protection_generation";
inline constexpr const char *kLayoutProtectionCacheRegistryKey =
    "openwow.layout_protection_cache";

inline void FrameScript_PushNil(lua_State *L) {
  lua_pushnil(L);
}

inline void FrameScript_PushNumber(lua_State *L, const lua_Number value) {
  lua_pushnumber(L, value);
}

inline void FrameScript_PushNumberFromInt(lua_State *L, const int value) {
  lua_pushnumber(L, static_cast<lua_Number>(value));
}

inline void FrameScript_PushBoolean(lua_State *L, const bool value) {
  lua_pushboolean(L, value ? 1 : 0);
}

inline void FireProtectedActionFailureEvent(lua_State *L, ProtectedActionFailureMode failure_mode) {
  GameUI_ReportProtectedActionFailure(L, failure_mode);
}

inline std::optional<std::uint16_t> GetCurrentModifierStateOverride(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, kCurrentModifierStateRegistryKey);
  std::optional<std::uint16_t> modifier_state;
  if (lua_isnumber(L, -1) != 0) {
    modifier_state = static_cast<std::uint16_t>(lua_tointeger(L, -1));
  }
  lua_pop(L, 1);
  return modifier_state;
}

inline std::optional<std::uint32_t> GetCurrentMouseButtonMaskOverride(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, kCurrentMouseButtonMaskRegistryKey);
  std::optional<std::uint32_t> mouse_button_mask;
  if (lua_isnumber(L, -1) != 0) {
    mouse_button_mask = static_cast<std::uint32_t>(lua_tointeger(L, -1));
  }
  lua_pop(L, 1);
  return mouse_button_mask;
}

inline std::uint32_t GetCurrentScriptMouseButtonMask() {
  return openwow::input::InputManager::Get().GetMouseButtonFlags();
}

inline std::uint32_t ApplyMouseButtonEventToScriptMask(const std::uint32_t base_mask,
                                                       const std::uint32_t button_flag,
                                                       const bool is_button_down_event) {
  if (button_flag == 0u) {
    return base_mask;
  }

  if (is_button_down_event) {
    return base_mask | button_flag;
  }

  return base_mask & ~button_flag;
}

class ScopedCurrentMouseButtonMaskOverride {
public:
  ScopedCurrentMouseButtonMaskOverride(lua_State *state,
                                       std::optional<std::uint32_t> mouse_button_mask)
      : state_(state) {
    if (state_ == nullptr) {
      return;
    }

    previous_mask_ = GetCurrentMouseButtonMaskOverride(state_);

    if (mouse_button_mask.has_value()) {
      lua_pushinteger(state_, static_cast<lua_Integer>(*mouse_button_mask));
    } else {
      lua_pushnil(state_);
    }
    lua_setfield(state_, LUA_REGISTRYINDEX, kCurrentMouseButtonMaskRegistryKey);
  }

  ~ScopedCurrentMouseButtonMaskOverride() {
    if (state_ == nullptr) {
      return;
    }

    if (previous_mask_.has_value()) {
      lua_pushinteger(state_, static_cast<lua_Integer>(*previous_mask_));
    } else {
      lua_pushnil(state_);
    }
    lua_setfield(state_, LUA_REGISTRYINDEX, kCurrentMouseButtonMaskRegistryKey);
  }

private:
  lua_State *state_ = nullptr;
  std::optional<std::uint32_t> previous_mask_;
};

inline bool GetLuaWidgetShownState(lua_State *L, int index) {
  index = lua_absindex(L, index);
  openwow::ui::game::runtime::GetInternedLuaField(L, index, "__ow_visible");
  const bool shown = lua_isboolean(L, -1) == 0 || lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  return shown;
}

inline openwow::ui::widgets::ScriptObjectType GetLuaFrameLookupObjectType(lua_State *L, int index);

inline void PushScriptObjectThisTokenRegistry(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, kScriptObjectThisTokenRegistryKey);
  if (lua_istable(L, -1) != 0) {
    return;
  }

  lua_pop(L, 1);
  lua_newtable(L);
  const int token_registry_index = lua_absindex(L, -1);
  lua_newtable(L);
  lua_pushstring(L, "k");
  lua_setfield(L, -2, "__mode");
  lua_setmetatable(L, token_registry_index);
  lua_pushvalue(L, token_registry_index);
  lua_setfield(L, LUA_REGISTRYINDEX, kScriptObjectThisTokenRegistryKey);
}

inline void PushScriptObjectCanonicalTypeRegistry(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, kScriptObjectCanonicalTypeRegistryKey);
  if (lua_istable(L, -1) != 0) {
    return;
  }

  lua_pop(L, 1);
  lua_newtable(L);
  const int registry_index = lua_absindex(L, -1);
  lua_newtable(L);
  lua_pushstring(L, "k");
  lua_setfield(L, -2, "__mode");
  lua_setmetatable(L, registry_index);
  lua_pushvalue(L, registry_index);
  lua_setfield(L, LUA_REGISTRYINDEX, kScriptObjectCanonicalTypeRegistryKey);
}

inline void PushScriptObjectThisLookupRegistry(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, kScriptObjectThisLookupRegistryKey);
  if (lua_istable(L, -1) != 0) {
    return;
  }

  lua_pop(L, 1);
  lua_newtable(L);
  const int registry_index = lua_absindex(L, -1);
  lua_newtable(L);
  lua_pushstring(L, "v");
  lua_setfield(L, -2, "__mode");
  lua_setmetatable(L, registry_index);
  lua_pushvalue(L, registry_index);
  lua_setfield(L, LUA_REGISTRYINDEX, kScriptObjectThisLookupRegistryKey);
}

inline void PushScriptObjectRuntimeTypeRegistry(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, kScriptObjectRuntimeTypeRegistryKey);
  if (lua_istable(L, -1) != 0) {
    return;
  }

  lua_pop(L, 1);
  lua_newtable(L);
  const int registry_index = lua_absindex(L, -1);
  lua_newtable(L);
  lua_pushstring(L, "k");
  lua_setfield(L, -2, "__mode");
  lua_setmetatable(L, registry_index);
  lua_pushvalue(L, registry_index);
  lua_setfield(L, LUA_REGISTRYINDEX, kScriptObjectRuntimeTypeRegistryKey);
}

inline void CacheLuaScriptObjectCanonicalType(lua_State *L, int index) {
  if (lua_istable(L, index) == 0) {
    return;
  }

  index = lua_absindex(L, index);
  const auto type = GetLuaFrameLookupObjectType(L, index);
  if (type == openwow::ui::widgets::ScriptObjectType::COUNT_) {
    return;
  }

  PushScriptObjectCanonicalTypeRegistry(L);
  const int registry_index = lua_absindex(L, -1);
  lua_pushvalue(L, index);
  lua_pushinteger(L, static_cast<lua_Integer>(type));
  lua_rawset(L, registry_index);
  lua_pop(L, 1);
}

inline void CacheLuaScriptObjectRuntimeType(lua_State *L, int index) {
  if (lua_istable(L, index) == 0) {
    return;
  }

  index = lua_absindex(L, index);
  lua_getfield(L, index, "__ow_type");
  if (lua_isstring(L, -1) == 0) {
    lua_pop(L, 1);
    return;
  }

  PushScriptObjectRuntimeTypeRegistry(L);
  const int registry_index = lua_absindex(L, -1);
  lua_pushvalue(L, index);
  lua_pushvalue(L, -3);
  openwow::ui::lua_set_taint(L, -1, 0);
  lua_rawset(L, registry_index);
  lua_pop(L, 2);
}

inline const char *GetLuaScriptObjectRuntimeTypeName(lua_State *L, int index) {
  if (lua_istable(L, index) == 0) {
    return nullptr;
  }

  const ScopedNeutralLuaTaint neutral_taint(L);
  index = lua_absindex(L, index);
  PushScriptObjectRuntimeTypeRegistry(L);
  const int registry_index = lua_absindex(L, -1);
  lua_pushvalue(L, index);
  lua_rawget(L, registry_index);

  const char *type_name = nullptr;
  if (lua_isstring(L, -1) != 0) {
    type_name = lua_tostring(L, -1);
  } else {
    lua_pop(L, 1);
    lua_getfield(L, index, "__ow_type");
    if (lua_isstring(L, -1) != 0) {
      type_name = lua_tostring(L, -1);
    }
  }

  lua_pop(L, 2);
  return type_name;
}

inline void CacheLuaScriptObjectThisLookup(lua_State *L, int index, void *this_pointer) {
  if (lua_istable(L, index) == 0 || this_pointer == nullptr) {
    return;
  }

  index = lua_absindex(L, index);
  PushScriptObjectThisLookupRegistry(L);
  const int registry_index = lua_absindex(L, -1);
  lua_pushlightuserdata(L, this_pointer);
  lua_pushvalue(L, index);
  openwow::ui::lua_set_taint(L, -1, 0);
  lua_rawset(L, registry_index);
  lua_pop(L, 1);
}

inline bool PushLuaScriptObjectByThis(lua_State *L, int index) {
  if (lua_istable(L, index) == 0) {
    return false;
  }

  const ScopedNeutralLuaTaint neutral_taint(L);
  index = lua_absindex(L, index);
  lua_rawgeti(L, index, 0);
  if (lua_type(L, -1) != LUA_TLIGHTUSERDATA) {
    lua_pop(L, 1);
    return false;
  }

  void *this_pointer = lua_touserdata(L, -1);
  lua_pop(L, 1);
  if (this_pointer == nullptr) {
    return false;
  }

  PushScriptObjectThisLookupRegistry(L);
  const int registry_index = lua_absindex(L, -1);
  lua_pushlightuserdata(L, this_pointer);
  lua_rawget(L, registry_index);
  lua_remove(L, registry_index);
  return lua_istable(L, -1) != 0;
}

inline bool CanonicalizeLuaScriptObjectTable(lua_State *L, int index) {
  if (lua_istable(L, index) == 0) {
    return false;
  }
  index = lua_absindex(L, index);
  if (!PushLuaScriptObjectByThis(L, index)) {
    return false;
  }
  lua_replace(L, index);
  return true;
}

inline void *EnsureLuaScriptObjectThisToken(lua_State *L, int index) {
  index = lua_absindex(L, index);
  PushScriptObjectThisTokenRegistry(L);
  const int token_registry_index = lua_absindex(L, -1);

  lua_pushvalue(L, index);
  lua_rawget(L, token_registry_index);
  if (lua_isuserdata(L, -1) != 0) {
    void *token = lua_touserdata(L, -1);
    lua_pop(L, 2);
    return token;
  }

  lua_pop(L, 1);
  auto *token_storage = static_cast<unsigned char *>(lua_newuserdata(L, 1));
  token_storage[0] = 0;
  void *token = token_storage;
  lua_pushvalue(L, index);
  lua_pushvalue(L, -2);
  lua_rawset(L, token_registry_index);
  lua_pop(L, 2);
  return token;
}

inline void AttachLuaScriptObjectThis(lua_State *L, int index, void *this_pointer = nullptr) {
  index = lua_absindex(L, index);
  const ScopedNeutralLuaTaint neutral_taint(L);
  if (this_pointer == nullptr) {
    lua_rawgeti(L, index, 0);
    if (lua_type(L, -1) == LUA_TLIGHTUSERDATA) {
      this_pointer = lua_touserdata(L, -1);
    }
    lua_pop(L, 1);
  }

  if (this_pointer == nullptr) {
    this_pointer = EnsureLuaScriptObjectThisToken(L, index);
  }

  lua_pushlightuserdata(L, this_pointer);
  lua_rawseti(L, index, 0);
  CacheLuaScriptObjectThisLookup(L, index, this_pointer);
  CacheLuaScriptObjectCanonicalType(L, index);
  CacheLuaScriptObjectRuntimeType(L, index);
}

inline void *GetLuaScriptObjectThisPointer(lua_State *L, int index) {
  if (lua_istable(L, index) == 0) {
    return nullptr;
  }

  const ScopedNeutralLuaTaint neutral_taint(L);
  index = lua_absindex(L, index);
  lua_rawgeti(L, index, 0);
  void *this_pointer = lua_touserdata(L, -1);
  lua_pop(L, 1);
  return this_pointer;
}

inline void *GetLuaNativeScriptObjectThisPointer(lua_State *L, int index) {
  if (lua_istable(L, index) == 0) {
    return nullptr;
  }

  const ScopedNeutralLuaTaint neutral_taint(L);
  index = lua_absindex(L, index);
  lua_rawgeti(L, index, 0);
  void *this_pointer = lua_touserdata(L, -1);
  lua_pop(L, 1);
  if (this_pointer == nullptr) {
    return nullptr;
  }

  PushScriptObjectThisTokenRegistry(L);
  const int token_registry_index = lua_absindex(L, -1);
  lua_pushvalue(L, index);
  lua_rawget(L, token_registry_index);
  const bool synthetic_token =
      lua_isuserdata(L, -1) != 0 && lua_touserdata(L, -1) == this_pointer;
  lua_pop(L, 2);
  return synthetic_token ? nullptr : this_pointer;
}

inline bool HasLuaScriptObjectThis(lua_State *L, int index) {
  return GetLuaScriptObjectThisPointer(L, index) != nullptr;
}

inline bool AllowLuaFrameProtectedMutation(
    lua_State *L, int index,
    ProtectedActionFailureMode failure_mode =
        ProtectedActionFailureMode::kBlockedType4);

inline constexpr std::string_view kCooldownDefaultBlingTexture = "interface\\cooldown\\star4.blp";
inline constexpr std::string_view kCooldownDefaultEdgeTexture = "interface\\cooldown\\edge.blp";

inline void StoreCooldownVisualState(lua_State *L, int frame_index, const bool ready_flash_active) {
  frame_index = lua_absindex(L, frame_index);
  const auto colors = openwow::ui::widgets::ComputeCooldownVisualColors(
      ready_flash_active, ReadLuaFrameAlphaByteOrDefault(L, frame_index),
      ComputeLuaFrameParentInheritedAlpha(L, frame_index));

  lua_pushnumber(L, static_cast<lua_Number>(colors.fill_abgr));
  lua_setfield(L, frame_index, "__ow_cd_fill_abgr");
  lua_pushnumber(L, static_cast<lua_Number>(colors.ready_flash_abgr));
  lua_setfield(L, frame_index, "__ow_cd_ready_flash_abgr");
  lua_pushnumber(L, static_cast<lua_Number>(colors.edge_abgr));
  lua_setfield(L, frame_index, "__ow_cd_edge_abgr");
  lua_pushboolean(L, ready_flash_active ? 1 : 0);
  lua_setfield(L, frame_index, "__ow_cd_ready_flash_active");
}

inline void SeedCooldownTextureField(lua_State *L, int frame_index, const char *field_name,
                                     const std::string_view texture_path) {
  frame_index = lua_absindex(L, frame_index);
  lua_getfield(L, frame_index, field_name);
  const bool has_value = lua_isstring(L, -1) != 0 && lua_rawlen(L, -1) != 0;
  lua_pop(L, 1);
  if (has_value) {
    return;
  }

  lua_pushlstring(L, texture_path.data(), texture_path.size());
  lua_setfield(L, frame_index, field_name);
}

inline void SeedCooldownDefaultTextures(lua_State *L, int frame_index) {
  SeedCooldownTextureField(L, frame_index, "__ow_cd_bling_texture", kCooldownDefaultBlingTexture);
  SeedCooldownTextureField(L, frame_index, "__ow_cd_edge_texture", kCooldownDefaultEdgeTexture);
}

inline void ApplyCooldownScriptState(lua_State *L, int frame_index, const lua_Number start,
                                     const lua_Number duration, const bool enabled) {
  frame_index = lua_absindex(L, frame_index);

  lua_pushnumber(L, start);
  lua_setfield(L, frame_index, "__ow_cd_start");
  lua_pushnumber(L, duration);
  lua_setfield(L, frame_index, "__ow_cd_dur");
  lua_pushnumber(L, duration);
  lua_setfield(L, frame_index, "__ow_cd_duration");
  lua_pushboolean(L, enabled ? 1 : 0);
  lua_setfield(L, frame_index, "__ow_cd_enabled");

  StoreCooldownVisualState(L, frame_index, false);
  SeedCooldownDefaultTextures(L, frame_index);
}

inline bool SetLuaScriptRegionShown(lua_State *L, int region_index,
                                    const bool shown) {
  region_index = lua_absindex(L, region_index);
  lua_getfield(L, region_index, "__ow_visible");
  const bool was_shown =
      lua_isboolean(L, -1) == 0 || lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  if (was_shown == shown) {
    return false;
  }

  if (!AllowLuaFrameProtectedMutation(L, region_index)) {
    return false;
  }

  auto visibility = CaptureLuaVisibilitySubtree(L, region_index);
  lua_pushboolean(L, shown ? 1 : 0);
  lua_setfield(L, region_index, "__ow_visible");
  DispatchLuaVisibilityTransitions(&visibility);
  ClearLuaEffectiveVisibilityOverrides(&visibility);
  return true;
}

inline bool ShowLuaScriptFrame(lua_State *L, const int frame_index) {
  return SetLuaScriptRegionShown(L, frame_index, true);
}

inline bool HideLuaScriptFrame(lua_State *L, int frame_index) {
  return SetLuaScriptRegionShown(L, frame_index, false);
}

inline bool TextureMatchesObjectType(const char *type_name) {
  return type_name != nullptr && (openwow::text::EqualsIgnoreCaseAscii(type_name, "Texture") ||
                                  openwow::text::EqualsIgnoreCaseAscii(type_name, "Region") ||
                                  openwow::text::EqualsIgnoreCaseAscii(type_name, "Object"));
}

inline void SetPortraitStateField(lua_State *L, const int texture_index,
                                  const runtime::TextureRenderStateField field,
                                  const std::string_view value) {
  runtime::SetTextureRenderStateString(
      L, texture_index, field,
      value.empty() ? std::optional<std::string_view>{}
                    : std::optional<std::string_view>(value));
}

inline void ClearPortraitState(lua_State *L, const int texture_index) {
  using runtime::TextureRenderStateField;
  SetPortraitStateField(L, texture_index, TextureRenderStateField::kTexture, {});
  SetPortraitStateField(L, texture_index, TextureRenderStateField::kPortraitUnit, {});
  SetPortraitStateField(L, texture_index, TextureRenderStateField::kPortraitGuid, {});
}

inline void BindPortraitTexturePath(lua_State *L, const int texture_index,
                                    const std::string_view texture_path) {
  using runtime::TextureRenderStateField;
  SetPortraitStateField(L, texture_index, TextureRenderStateField::kTexture,
                        texture_path);
  SetPortraitStateField(L, texture_index, TextureRenderStateField::kPortraitUnit, {});
  SetPortraitStateField(L, texture_index, TextureRenderStateField::kPortraitGuid, {});
}

inline void BindPortraitUnitToken(lua_State *L, const int texture_index,
                                  const std::string_view unit_id) {
  using runtime::TextureRenderStateField;
  SetPortraitStateField(L, texture_index, TextureRenderStateField::kTexture, {});
  SetPortraitStateField(L, texture_index, TextureRenderStateField::kPortraitUnit,
                        unit_id);
  SetPortraitStateField(L, texture_index, TextureRenderStateField::kPortraitGuid, {});
}

inline void BindPortraitGuid(lua_State *L, const int texture_index, const ObjectGuid &guid) {
  using runtime::TextureRenderStateField;
  SetPortraitStateField(L, texture_index, TextureRenderStateField::kTexture, {});
  SetPortraitStateField(L, texture_index, TextureRenderStateField::kPortraitUnit, {});
  if (guid.IsEmpty()) {
    SetPortraitStateField(L, texture_index, TextureRenderStateField::kPortraitGuid, {});
    return;
  }

  SetPortraitStateField(L, texture_index, TextureRenderStateField::kPortraitGuid,
                        std::string_view(std::to_string(guid.GetRawValue())));
}

inline int ValidateTextureWidgetArgument(lua_State *L) {
  if (lua_type(L, 1) != LUA_TTABLE) {
    return luaL_error(L, "Attempt to find 'this' in non-table object (used '.' instead of ':' ?)");
  }

  if (!HasLuaScriptObjectThis(L, 1)) {
    return luaL_error(L, "Attempt to find 'this' in non-framescript object");
  }

  const char *type_name = openwow::ui::BorrowRawLuaStringField(L, 1, "__ow_type");
  if (type_name == nullptr || *type_name == '\0') {
    return luaL_error(L, "Attempt to find 'this' in non-framescript object");
  }

  if (!TextureMatchesObjectType(type_name)) {
    return luaL_error(L, "Wrong object type for member function");
  }

  return lua_absindex(L, 1);
}

inline bool EqualsIgnoreCaseAscii(std::string_view lhs, std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }

  for (std::size_t index = 0; index < lhs.size(); ++index) {
    const auto left = static_cast<unsigned char>(lhs[index]);
    const auto right = static_cast<unsigned char>(rhs[index]);
    if (std::tolower(left) != std::tolower(right)) {
      return false;
    }
  }

  return true;
}

inline openwow::ui::widgets::ScriptObjectType GetLuaFrameLookupObjectType(lua_State *L, int index) {
  const char *type_name = openwow::ui::BorrowRawLuaStringField(L, index, "__ow_type");
  if (type_name == nullptr || *type_name == '\0') {
    return openwow::ui::widgets::ScriptObjectType::COUNT_;
  }
  if (std::strcmp(type_name, "ModelFFX") == 0) {
    return openwow::ui::widgets::ScriptObjectType::Model;
  }
  return openwow::ui::widgets::ScriptObjectTypeFromName(type_name);
}

inline openwow::ui::widgets::ScriptObjectType GetLuaCanonicalScriptObjectType(lua_State *L,
                                                                              int index) {
  using openwow::ui::widgets::ScriptObjectType;

  if (lua_istable(L, index) == 0) {
    return ScriptObjectType::COUNT_;
  }

  const ScopedNeutralLuaTaint neutral_taint(L);
  index = lua_absindex(L, index);
  PushScriptObjectCanonicalTypeRegistry(L);
  const int registry_index = lua_absindex(L, -1);
  lua_pushvalue(L, index);
  lua_rawget(L, registry_index);

  ScriptObjectType type = ScriptObjectType::COUNT_;
  if (lua_isnumber(L, -1) != 0) {
    const auto raw_type = static_cast<int>(lua_tointeger(L, -1));
    if (raw_type >= 0 && raw_type < static_cast<int>(ScriptObjectType::COUNT_)) {
      type = static_cast<ScriptObjectType>(raw_type);
    }
  }

  lua_pop(L, 2);
  if (type != ScriptObjectType::COUNT_) {
    return type;
  }

  return GetLuaFrameLookupObjectType(L, index);
}

inline openwow::ui::widgets::ScriptObjectType GetAttachedLuaCanonicalScriptObjectType(lua_State *L,
                                                                                      int index) {
  using openwow::ui::widgets::ScriptObjectType;

  if (lua_istable(L, index) == 0) {
    return ScriptObjectType::COUNT_;
  }

  const ScopedNeutralLuaTaint neutral_taint(L);
  index = lua_absindex(L, index);
  PushScriptObjectCanonicalTypeRegistry(L);
  const int registry_index = lua_absindex(L, -1);
  lua_pushvalue(L, index);
  lua_rawget(L, registry_index);

  ScriptObjectType type = ScriptObjectType::COUNT_;
  if (lua_isnumber(L, -1) != 0) {
    const auto raw_type = static_cast<int>(lua_tointeger(L, -1));
    if (raw_type >= 0 && raw_type < static_cast<int>(ScriptObjectType::COUNT_)) {
      type = static_cast<ScriptObjectType>(raw_type);
    }
  }

  lua_pop(L, 2);
  return type;
}

inline bool
LuaScriptObjectHasCanonicalType(lua_State *L, int index,
                                const openwow::ui::widgets::ScriptObjectType expected_type) {
  return GetLuaCanonicalScriptObjectType(L, index) == expected_type;
}

inline bool
LuaScriptObjectIsKindOfCanonicalType(
    lua_State *L, int index,
    const openwow::ui::widgets::ScriptObjectType expected_type) {
  return openwow::ui::widgets::IsScriptTypeKindOf(
      GetLuaCanonicalScriptObjectType(L, index), expected_type);
}

inline bool IsFrameLikeLookupObjectType(const openwow::ui::widgets::ScriptObjectType type) {
  using openwow::ui::widgets::ScriptObjectType;

  switch (type) {
  case ScriptObjectType::Frame:
  case ScriptObjectType::Button:
  case ScriptObjectType::CheckButton:
  case ScriptObjectType::EditBox:
  case ScriptObjectType::Slider:
  case ScriptObjectType::StatusBar:
  case ScriptObjectType::ScrollFrame:
  case ScriptObjectType::ScrollingMessageFrame:
  case ScriptObjectType::MessageFrame:
  case ScriptObjectType::SimpleHTML:
  case ScriptObjectType::ColorSelect:
  case ScriptObjectType::Model:
  case ScriptObjectType::PlayerModel:
  case ScriptObjectType::DressUpModel:
  case ScriptObjectType::TabardModel:
  case ScriptObjectType::Minimap:
  case ScriptObjectType::GameTooltip:
  case ScriptObjectType::Cooldown:
  case ScriptObjectType::MovieFrame:
  case ScriptObjectType::WorldFrame:
    return true;
  default:
    return false;
  }
}

bool PushNamedFrameLikeObject(lua_State *L, std::string_view frame_name);
void ClearClickFrameLookupCache(lua_State *L);

inline bool IsLuaWidgetEffectivelyVisible(lua_State *L, int index, int depth = 0) {
  (void)depth;
  return IsLuaWidgetEffectivelyVisibleIterative(L, index);
}

inline std::uint32_t TruncateLuaNumberToWrappedLowU32(lua_Number value) {

  if (!std::isfinite(value)) {
    return 0;
  }

  constexpr auto kMinSigned64 = static_cast<lua_Number>(std::numeric_limits<std::int64_t>::min());
  constexpr auto kMaxSigned64 = static_cast<lua_Number>(std::numeric_limits<std::int64_t>::max());
  if (value < kMinSigned64 || value > kMaxSigned64) {
    return 0;
  }

  const auto truncated = static_cast<std::int64_t>(std::trunc(value));
  return static_cast<std::uint32_t>(truncated);
}

inline std::int32_t TruncateLuaNumberToSseI32(lua_Number value) {
  return openwow::ui::TruncateLuaNumberToI32(value);
}

inline void SetCurrentModifierStateOverride(lua_State *L,
                                            std::optional<std::uint16_t> modifier_state) {
  if (modifier_state.has_value()) {
    lua_pushinteger(L, static_cast<lua_Integer>(*modifier_state));
  } else {
    lua_pushnil(L);
  }
  lua_setfield(L, LUA_REGISTRYINDEX, kCurrentModifierStateRegistryKey);
}

struct InventoryCommerceLuaContext {
  openwow::game::WorldSession* session = nullptr;
  openwow::game::PlayerInventoryReplica* inventory = nullptr;
  openwow::game::ItemDefinitions* item_definitions = nullptr;
  openwow::game::EquipmentSets* equipment = nullptr;
  openwow::game::InventoryCommands* inventory_commands = nullptr;
  openwow::game::AuctionInteraction* auction = nullptr;
  openwow::game::LootInteraction* loot = nullptr;
};

inline void BindInventoryCommerceLuaContext(
    lua_State* L, openwow::game::WorldSession* session) {
  if (session == nullptr) {
    lua_pushnil(L);
    lua_setfield(
        L, LUA_REGISTRYINDEX, kInventoryCommerceContextRegistryKey);
    return;
  }

  auto* context = static_cast<InventoryCommerceLuaContext*>(
      lua_newuserdata(L, sizeof(InventoryCommerceLuaContext)));
  *context = {
      .session = session,
      .inventory = &session->inventory_replica(),
      .item_definitions = &session->item_definitions(),
      .equipment = &session->equipment(),
      .inventory_commands = &session->inventory_commands(),
      .auction = &session->auction(),
      .loot = &session->loot(),
  };
  lua_setfield(L, LUA_REGISTRYINDEX, kInventoryCommerceContextRegistryKey);
}

inline InventoryCommerceLuaContext& RequireInventoryCommerceLuaContext(
    lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, kInventoryCommerceContextRegistryKey);
  auto* context = static_cast<InventoryCommerceLuaContext*>(
      lua_touserdata(L, -1));
  lua_pop(L, 1);
  if (context == nullptr) {
    luaL_error(L, "inventory and commerce APIs require an active binding");
  }
  return *context;
}

inline openwow::game::WorldSession* GetWorldSession(lua_State* L) {
  lua_getfield(L, LUA_REGISTRYINDEX, kInventoryCommerceContextRegistryKey);
  auto* context = static_cast<InventoryCommerceLuaContext*>(
      lua_touserdata(L, -1));
  lua_pop(L, 1);
  return context != nullptr ? context->session : nullptr;
}

inline openwow::ui::WorldMapSystem* WorldMapStateOrNull(lua_State* L) {
  auto* const context =
      openwow::ui::game::runtime::WorldUiRuntimeContext::FromLua(L);
  return context != nullptr ? &context->world_map() : nullptr;
}

inline openwow::ui::MinimapSystem* MinimapStateOrNull(lua_State* L) {
  auto* const context =
      openwow::ui::game::runtime::WorldUiRuntimeContext::FromLua(L);
  return context != nullptr ? &context->minimap_state() : nullptr;
}

inline openwow::game::PlayerInventoryReplica&
RequirePlayerInventoryReplica(lua_State* L) {
  return *RequireInventoryCommerceLuaContext(L).inventory;
}

inline openwow::game::ItemDefinitions& RequireItemDefinitions(lua_State* L) {
  return *RequireInventoryCommerceLuaContext(L).item_definitions;
}

inline openwow::game::EquipmentSets& RequireEquipmentSets(lua_State* L) {
  return *RequireInventoryCommerceLuaContext(L).equipment;
}

inline openwow::game::InventoryCommands& RequireInventoryCommands(lua_State* L) {
  return *RequireInventoryCommerceLuaContext(L).inventory_commands;
}

inline openwow::game::AuctionInteraction& RequireAuctionInteraction(
    lua_State* L) {
  return *RequireInventoryCommerceLuaContext(L).auction;
}

inline openwow::game::LootInteraction& RequireLootInteraction(lua_State* L) {
  return *RequireInventoryCommerceLuaContext(L).loot;
}

inline int ResolvePendingItemDecision(lua_State* L, const char* usage, const bool accepted) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "%s", usage);
  }

  auto* session = GetWorldSession(L);
  if (session == nullptr || session->objects().GetActivePlayer() == nullptr) {
    return 0;
  }

  const auto pending_index = TruncateLuaNumberToWrappedLowU32(lua_tonumber(L, 1));
  RequireInventoryCommands(L).ResolvePending(pending_index, accepted);
  return 0;
}

inline const openwow::game::AuraSlotInfo *
FindLocalPlayerAuraBySpellId(const openwow::game::WorldSession *session,
                             const std::uint32_t spell_id) {
  if (session == nullptr || spell_id == 0) {
    return nullptr;
  }

  const auto local_guid = session->objects().GetLocalPlayerGuid();
  if (local_guid.IsEmpty()) {
    return nullptr;
  }

  return session->aura().FindAuraBySpellId(local_guid.GetRawValue(), spell_id);
}

inline int PushLocalPlayerAuraRemainingSecondsIfPresent(
    lua_State *L, const openwow::game::WorldSession *session, const std::uint32_t spell_id) {
  const auto *aura = FindLocalPlayerAuraBySpellId(session, spell_id);
  if (aura == nullptr) {
    return 0;
  }

  lua_pushnumber(L, static_cast<lua_Number>(aura->remaining_duration) * 0.001);
  return 1;
}

inline const VendorList *GetActiveVendorList(const WorldSession &session) {
  if (!session.gossip().merchant().active()) {
    return nullptr;
  }

  const auto &vendor = session.gossip().merchant().snapshot();
  if (vendor.vendor_guid.IsEmpty()) {
    return nullptr;
  }

  return &vendor;
}

inline const VendorItem *GetActiveVendorItemByZeroBasedIndex(const VendorList *vendor,
                                                             const std::int32_t zero_based_index) {
  if (vendor == nullptr || vendor->vendor_guid.IsEmpty() || zero_based_index < 0 ||
      zero_based_index >= static_cast<std::int32_t>(vendor->items.size())) {
    return nullptr;
  }

  return &vendor->items[static_cast<std::size_t>(zero_based_index)];
}

inline const VendorItem *GetVendorItemByCursorSlot(const WorldSession &session,
                                                   const std::uint32_t cursor_slot) {
  if (cursor_slot == 0) {
    return nullptr;
  }

  return GetActiveVendorItemByZeroBasedIndex(GetActiveVendorList(session),
                                             static_cast<std::int32_t>(cursor_slot - 1));
}

inline bool SendCursorMerchantItemToTarget(WorldSession &session, const std::uint32_t cursor_slot,
                                           const std::uint64_t target_guid,
                                           const std::uint8_t target_slot,
                                           const std::uint32_t count = 1) {
  const auto *vendor = GetActiveVendorList(session);
  const auto *item = GetVendorItemByCursorSlot(session, cursor_slot);
  if (vendor == nullptr || item == nullptr || item->item_id == 0 || target_guid == 0) {
    return false;
  }

  session.interaction().SendBuyItemInSlot(vendor->vendor_guid.GetRawValue(), item->item_id,
                                          item->slot, target_guid, target_slot, count);
  return true;
}

inline const openwow::data::dbc::DbcLoader *GetDbcLoader(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "openwow.dbc_loader");
  auto *dbc = static_cast<const openwow::data::dbc::DbcLoader *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  if (dbc != nullptr) {
    return dbc;
  }

  const auto *session = GetWorldSession(L);
  return session != nullptr ? session->GetDbcLoader() : nullptr;
}

inline const openwow::data::dbc::PaperDollItemFrameEntry *
FindLuaInventorySlotInfo(lua_State *L, const std::string_view slot_name) {

  const auto *dbc = GetDbcLoader(L);
  if (dbc == nullptr) {
    return nullptr;
  }

  for (const auto &entry : dbc->paper_doll_item_frame().entries()) {
    if (openwow::text::EqualsIgnoreCaseAscii(entry.item_button_name, slot_name)) {
      return &entry;
    }
  }
  return nullptr;
}

inline bool IsValidResolvedInventorySlot(const int slot_id) {
  return slot_id == -1 || (slot_id >= 0 && slot_id <= 22) || (slot_id >= 39 && slot_id <= 73) ||
         (slot_id >= 86 && slot_id <= 117);
}

inline bool ResolveInventorySlotArgument(lua_State *L, int index, int *out_slot) {
  if (out_slot == nullptr) {
    return false;
  }

  if (lua_isnumber(L, index) != 0) {

    const auto parsed = openwow::ui::TruncateLuaNumberToI32(
        lua_tonumber(L, index));
    *out_slot = openwow::ui::SignedI32FromU32Bits(
        static_cast<std::uint32_t>(parsed) - 1u);
  } else if (lua_isstring(L, index) != 0) {
    const char *slot_name = lua_tostring(L, index);
    if (const auto *entry =
            FindLuaInventorySlotInfo(L, slot_name != nullptr ? slot_name : "")) {
      *out_slot = static_cast<int>(entry->slot_number);
    }

  }

  return IsValidResolvedInventorySlot(*out_slot);
}

inline const openwow::data::dbc::CharTitlesEntry *
FindCharTitleEntryById(lua_State *L, const std::uint32_t title_id) {
  if (title_id == 0) {
    return nullptr;
  }

  const auto *dbc = GetDbcLoader(L);
  if (dbc == nullptr) {
    return nullptr;
  }

  return dbc->char_titles().LookupEntry(title_id);
}

inline const openwow::data::dbc::CharTitlesEntry *
FindCharTitleEntryByMaskId(lua_State *L, const std::int32_t mask_id) {
  const auto *dbc = GetDbcLoader(L);
  if (dbc == nullptr) {
    return nullptr;
  }

  const auto &char_titles = dbc->char_titles();
  for (const auto &entry : char_titles) {
    if (static_cast<std::int32_t>(entry.mask_id) == mask_id) {
      return &entry;
    }
  }
  return nullptr;
}

inline bool ActivePlayerUsesFemaleTitles(lua_State *L) {
  const auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return false;
  }

  const auto *player = session->objects().GetActivePlayer();
  return player != nullptr && player->State().GetGender() == 1;
}

inline std::string GetActivePlayerNameForTitleFormatting(lua_State *L) {
  const auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return {};
  }

  const auto *player = session->objects().GetActivePlayer();
  return player != nullptr ? player->ResolveRetailName(*session) : std::string{};
}

inline std::string_view
SelectTitleNameForActiveGender(const openwow::data::dbc::CharTitlesEntry &entry,
                               const bool use_female_title) {
  if (use_female_title) {
    if (!entry.name_female.empty()) {
      return entry.name_female;
    }
    return entry.name_male;
  }

  if (!entry.name_male.empty()) {
    return entry.name_male;
  }
  return entry.name_female;
}

inline std::string StripLuaTitleFormatTokens(std::string_view raw_title) {
  std::string cleaned_title;
  cleaned_title.reserve(raw_title.size());

  bool skipping_format_token = false;
  for (const char character : raw_title) {
    if (character == '%') {
      skipping_format_token = true;
      continue;
    }

    if (skipping_format_token) {
      if (character != ' ') {
        continue;
      }
      skipping_format_token = false;
    }

    cleaned_title.push_back(character);
  }

  return cleaned_title;
}

inline std::string
FormatCharTitleForActivePlayer(lua_State *L, const openwow::data::dbc::CharTitlesEntry &entry) {
  char formatted_title[256] = {};
  const std::string title_format(
      SelectTitleNameForActiveGender(entry, ActivePlayerUsesFemaleTitles(L)));
  const auto player_name = GetActivePlayerNameForTitleFormatting(L);

  openwow::game::FormatRuntimeStringTemplateInto(formatted_title, sizeof(formatted_title),
                                                 title_format.c_str(), player_name.c_str());
  return formatted_title;
}

inline std::uint32_t ResolveSpellItemEnchantmentGemId(lua_State *L,
                                                      const std::uint32_t enchant_id) {
  if (enchant_id == 0) {
    return 0;
  }

  const auto *dbc = GetDbcLoader(L);
  if (dbc == nullptr) {
    return 0;
  }

  const auto *enchant = dbc->spell_item_enchantment().LookupEntry(enchant_id);
  return enchant != nullptr ? enchant->gem_id : 0;
}

inline const openwow::data::dbc::PageTextMaterialEntry *
LookupPageTextMaterialEntry(const openwow::data::dbc::DbcLoader &dbc,
                            const std::uint32_t material_id) {
  if (material_id == 0) {
    return nullptr;
  }

  return dbc.page_text_material().LookupEntry(material_id);
}

inline std::optional<std::uint32_t>
ResolveReadableGameObjectPageMaterialId(const openwow::game::CGGameObject_C &game_object) {
  const auto material_id = game_object.GetReadablePageMaterialId();
  if (material_id == 0) {
    return std::nullopt;
  }

  return material_id;
}

inline std::optional<std::uint32_t>
ResolveReadableObjectPageMaterialId(const openwow::game::WorldSession &session,
                                    const openwow::game::ObjectGuid guid) {
  if (!guid) {
    return std::nullopt;
  }

  if (const auto *item = session.objects().GetItem(guid); item != nullptr) {
    const auto entry = item->GetEntry();
    if (entry == 0) {
      return std::nullopt;
    }

    const auto *item_template = session.query_cache().GetItemTemplate(entry);
    if (item_template == nullptr || item_template->page_material == 0) {
      return std::nullopt;
    }

    return item_template->page_material;
  }

  if (const auto *game_object = session.objects().GetGameObject(guid); game_object != nullptr) {
    return ResolveReadableGameObjectPageMaterialId(*game_object);
  }

  return std::nullopt;
}

inline std::optional<std::string_view>
ResolveReadableObjectPageMaterialName(lua_State *L, const openwow::game::WorldSession &session,
                                      const openwow::game::ObjectGuid guid) {
  const auto *dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    dbc = GetDbcLoader(L);
  }
  if (dbc == nullptr) {
    return std::nullopt;
  }

  const auto material_id = ResolveReadableObjectPageMaterialId(session, guid);
  if (!material_id.has_value()) {
    return std::nullopt;
  }

  const auto *entry = LookupPageTextMaterialEntry(*dbc, *material_id);
  if (entry == nullptr) {
    return std::nullopt;
  }

  return entry->name;
}

inline const openwow::data::dbc::SpellEntry *LookupSpellEntry(lua_State *L, std::uint32_t spell_id);

inline constexpr std::string_view kItemIconTexturePathPrefix =
    ::openwow::game::kItemInventoryIconTexturePrefix;
inline constexpr std::string_view kFallbackItemIconName =
    ::openwow::game::kFallbackItemInventoryIconName;

inline std::string BuildItemIconTexturePath(std::string_view icon_name) {
  if (icon_name.empty()) {
    icon_name = kFallbackItemIconName;
  }
  return ::openwow::game::BuildItemInventoryIconTexturePath(icon_name);
}

inline std::optional<std::string> TryResolveItemDisplayIdIconTexturePath(lua_State *L,
                                                                         std::uint32_t display_id) {
  if (display_id == 0) {
    return std::nullopt;
  }

  return ::openwow::game::ResolveItemInventoryIconTexturePath(
      GetDbcLoader(L), display_id);
}

inline std::string ResolveItemDisplayIdIconTexturePathOrFallback(lua_State *L,
                                                                 std::uint32_t display_id) {
  if (const auto icon_path = TryResolveItemDisplayIdIconTexturePath(L, display_id);
      icon_path.has_value()) {
    return *icon_path;
  }
  return BuildItemIconTexturePath(kFallbackItemIconName);
}

inline std::optional<std::string>
TryResolveVisibleItemIconTexturePath(lua_State *L, openwow::game::WorldSession &session,
                                     const openwow::game::CGPlayer_C &player,
                                     const std::uint8_t slot) {
  const auto entry = player.GetVisibleItemTemplateEntry(slot);
  if (!entry.has_value()) {
    return std::nullopt;
  }

  if (const auto *item_template = session.query_cache().GetOrRequestItemTemplate(*entry);
      item_template != nullptr) {
    if (const auto icon_path = TryResolveItemDisplayIdIconTexturePath(L, item_template->display_id);
        icon_path.has_value()) {
      return icon_path;
    }
  }

  if (const auto *cached_item = RequireItemDefinitions(L).GetItem(*entry);
      cached_item != nullptr) {
    return TryResolveItemDisplayIdIconTexturePath(L, cached_item->display_id);
  }

  return std::nullopt;
}

inline std::optional<std::string> TryResolveItemEntryIconTexturePath(lua_State *L,
                                                                     std::uint32_t item_entry) {
  if (item_entry == 0) {
    return std::nullopt;
  }

  if (auto *session = GetWorldSession(L); session != nullptr) {
    if (const auto *item_template = session->query_cache().GetItemTemplate(item_entry);
        item_template != nullptr) {
      return TryResolveItemDisplayIdIconTexturePath(L, item_template->display_id);
    }
  }

  if (const auto *cached_item = RequireItemDefinitions(L).GetItem(item_entry);
      cached_item != nullptr) {
    return TryResolveItemDisplayIdIconTexturePath(L, cached_item->display_id);
  }

  return std::nullopt;
}

inline std::string ResolveItemEntryIconTexturePathOrFallback(lua_State *L,
                                                             std::uint32_t item_entry) {
  if (const auto icon_path = TryResolveItemEntryIconTexturePath(L, item_entry);
      icon_path.has_value()) {
    return *icon_path;
  }
  return BuildItemIconTexturePath(kFallbackItemIconName);
}

inline bool StopPlayerAttackFromScript(openwow::game::WorldSession *session,
                                       openwow::game::TargetingSystem *targeting) {
  if (session == nullptr || session->objects().GetActivePlayer() == nullptr) {
    return false;
  }

  if (targeting != nullptr && targeting->HasMeleeAttackState()) {
    targeting->StopAttack();
    return true;
  }

  return false;
}

inline bool ActivePlayerHasTransportGuid(const openwow::game::WorldSession &session) {
  const auto *player = session.objects().GetActivePlayer();
  return player != nullptr && !player->GetTransportGUID().IsEmpty();
}

inline bool HasTeleportActionRestriction(
    const openwow::game::WorldSession &session,
    const openwow::game::CGPlayer_C &player) {
  constexpr std::uint32_t kDeniedUnitFlag = 0x00000800u;
  return (player.State().GetUnitFlags() & kDeniedUnitFlag) != 0 ||
         session.pet().attack_command_active();
}

inline void lua_pushwowbool(lua_State *L, bool value) {
  if (value)
    lua_pushnumber(L, 1.0);
  else
    lua_pushnil(L);
}

inline constexpr std::uint32_t kLuaUnitDerivedStatGtMaxLevel = 100;
inline constexpr std::uint8_t kLuaUnitStatAgility = 1;
inline constexpr std::uint8_t kLuaUnitStatIntellect = 3;
inline constexpr std::uint8_t kLuaUnitStatSpirit = 4;

inline bool ScriptUnitUsesDerivedStatGate(const openwow::game::CGUnit_C &unit) {
  return unit.IsActivePlayer() || unit.GetUInt32(UNIT_FIELD_PETNUMBER) != 0u;
}

inline int LuaUnitDerivedStatGtRowIndex(const openwow::game::CGUnit_C &unit) {
  const auto class_id = unit.State().GetClass();
  const auto level = unit.GetLevel();
  if (class_id == 0 || level == 0) {
    return -1;
  }
  return static_cast<int>((class_id - 1u) * kLuaUnitDerivedStatGtMaxLevel + (level - 1u));
}

template <typename Store>
inline float LuaLookupGtValue(const Store &table, const int row_index) {
  const auto *entry = row_index >= 0 ? table.LookupEntryByRowIndex(row_index) : nullptr;
  return entry != nullptr ? entry->value : 0.0f;
}

class LuaUnitDerivedStatQuery {
 public:
  LuaUnitDerivedStatQuery(const openwow::data::dbc::DbcLoader *dbc,
                          const openwow::game::CGUnit_C *unit)
      : dbc_(dbc), unit_(unit) {}

  [[nodiscard]] bool can_query_unit() const {
    return unit_ != nullptr && ScriptUnitUsesDerivedStatGate(*unit_);
  }

  [[nodiscard]] bool can_query_dbc() const {
    return dbc_ != nullptr && unit_ != nullptr && ScriptUnitUsesDerivedStatGate(*unit_);
  }

  [[nodiscard]] lua_Number crit_from_agility() const {
    if (!can_query_dbc()) {
      return 0.0;
    }
    const int row = LuaUnitDerivedStatGtRowIndex(*unit_);
    const float base = LuaLookupGtValue(
        dbc_->gt_chance_to_melee_crit_base(), static_cast<int>(unit_->State().GetClass()) - 1);
    const float per_agility = LuaLookupGtValue(dbc_->gt_chance_to_melee_crit(), row);
    return static_cast<lua_Number>(openwow::game::UnitStateRuntime::GetCritChanceFromAgility(
        per_agility, base, unit_->State().GetStat(kLuaUnitStatAgility)));
  }

  [[nodiscard]] lua_Number spell_crit_from_intellect() const {
    if (!can_query_dbc()) {
      return 0.0;
    }
    const int row = LuaUnitDerivedStatGtRowIndex(*unit_);
    const float base = LuaLookupGtValue(
        dbc_->gt_chance_to_spell_crit_base(), static_cast<int>(unit_->State().GetClass()) - 1);
    const float per_intellect = LuaLookupGtValue(dbc_->gt_chance_to_spell_crit(), row);
    return static_cast<lua_Number>(openwow::game::UnitStateRuntime::GetSpellCritChanceFromIntellect(
        per_intellect, base, unit_->State().GetStat(kLuaUnitStatIntellect)));
  }

  [[nodiscard]] lua_Number health_regen_from_spirit() const {
    if (!can_query_dbc()) {
      return 0.0;
    }
    const int row = LuaUnitDerivedStatGtRowIndex(*unit_);
    const float per_spirit = LuaLookupGtValue(dbc_->gt_regen_hp_per_spt(), row);
    const float base = LuaLookupGtValue(dbc_->gt_oct_regen_hp(), row);
    return static_cast<lua_Number>(openwow::game::UnitStateRuntime::GetHealthRegenRateFromSpirit(
        per_spirit, base, unit_->State().GetStat(kLuaUnitStatSpirit), 50));
  }

  [[nodiscard]] lua_Number mana_regen_from_spirit() const {
    if (!can_query_dbc()) {
      return 0.0;
    }
    const int row = LuaUnitDerivedStatGtRowIndex(*unit_);
    const float per_spirit = LuaLookupGtValue(dbc_->gt_regen_mp_per_spt(), row);
    const float regen = openwow::game::UnitStateRuntime::GetManaRegenRateFromSpirit(
        per_spirit, unit_->State().GetStat(kLuaUnitStatSpirit), unit_->State().GetStat(kLuaUnitStatIntellect));
    return static_cast<lua_Number>(regen) + 0.001;
  }

  [[nodiscard]] lua_Number health_modifier() const {
    return can_query_unit() ? static_cast<lua_Number>(unit_->State().GetCreatureHealthModifier()) : 0.0;
  }

  [[nodiscard]] lua_Number power_modifier() const {
    return can_query_unit() ? static_cast<lua_Number>(unit_->State().GetCreaturePowerModifier()) : 0.0;
  }

  [[nodiscard]] lua_Number max_health_modifier() const {
    return can_query_unit() ? static_cast<lua_Number>(unit_->State().GetMaxHealthModifier() + 1.0f) : 0.0;
  }

 private:
  const openwow::data::dbc::DbcLoader *dbc_ = nullptr;
  const openwow::game::CGUnit_C *unit_ = nullptr;
};

class LuaCallFrame {
 public:
  explicit LuaCallFrame(lua_State *state) : state_(state) {}

  [[nodiscard]] lua_State *state() const { return state_; }

  [[nodiscard]] openwow::game::WorldSession *world_session() const {
    return GetWorldSession(state_);
  }

  [[nodiscard]] const openwow::data::dbc::DbcLoader *dbc() const {
    return GetDbcLoader(state_);
  }

  [[nodiscard]] std::string require_string(const int index, const char *usage) const {
    if (!lua_isstring(state_, index)) {
      luaL_error(state_, usage);
      return {};
    }

    std::size_t length = 0;
    const char *value = lua_tolstring(state_, index, &length);
    return value != nullptr ? std::string(value, length) : std::string{};
  }

  [[nodiscard]] bool truthy(const int index) const {
    return lua_toboolean(state_, index) != 0;
  }

  [[nodiscard]] lua_Number number_arg_or(const int index, const lua_Number fallback) const {
    return lua_isnumber(state_, index) ? lua_tonumber(state_, index) : fallback;
  }

  int none() const {
    return 0;
  }

  int nil() const {
    lua_pushnil(state_);
    return 1;
  }

  int nil_pair() const {
    lua_pushnil(state_);
    lua_pushnil(state_);
    return 2;
  }

  int number(const lua_Number value) const {
    lua_pushnumber(state_, value);
    return 1;
  }

  int number_pair(const lua_Number first, const lua_Number second) const {
    lua_pushnumber(state_, first);
    lua_pushnumber(state_, second);
    return 2;
  }

  int boolean(const bool value) const {
    lua_pushboolean(state_, value ? 1 : 0);
    return 1;
  }

  int wow_bool(const bool value) const {
    lua_pushwowbool(state_, value);
    return 1;
  }

  int string(std::string_view value) const {
    lua_pushlstring(state_, value.data(), value.size());
    return 1;
  }

  int string_pair(std::string_view first, std::string_view second) const {
    lua_pushlstring(state_, first.data(), first.size());
    lua_pushlstring(state_, second.data(), second.size());
    return 2;
  }

  int string_nil_pair(std::string_view first) const {
    lua_pushlstring(state_, first.data(), first.size());
    lua_pushnil(state_);
    return 2;
  }

  int optional_string(std::optional<std::string_view> value) const {
    if (value.has_value()) {
      lua_pushlstring(state_, value->data(), value->size());
    } else {
      lua_pushnil(state_);
    }
    return 1;
  }

 private:
  lua_State *state_;
};

using openwow::ui::ScriptParseBoolStringOrDefault;
using openwow::ui::ScriptReadBoolArgOrDefault;

inline int PushActivePlayerFlagAsLegacyNumberOrNil(lua_State *L, const std::uint32_t flag_mask) {
  auto *session = GetWorldSession(L);
  if (session != nullptr) {
    if (const auto *player = session->objects().GetActivePlayer();
        player != nullptr && (player->GetUInt32(PLAYER_FLAGS) & flag_mask) != 0) {
      lua_pushnumber(L, 1.0);
      return 1;
    }
  }

  lua_pushnil(L);
  return 1;
}

inline constexpr char kTextureNormalStateSupportedRegistryKey[] =
    "openwow.texture_state.normal_supported";
inline constexpr char kTextureDesaturatedStateSupportedRegistryKey[] =
    "openwow.texture_state.desaturated_supported";

inline bool TextureStateSupported(lua_State *L, bool desaturated_state) {
  const char *const registry_key = desaturated_state ? kTextureDesaturatedStateSupportedRegistryKey
                                                     : kTextureNormalStateSupportedRegistryKey;
  lua_getfield(L, LUA_REGISTRYINDEX, registry_key);
  const bool supported =
      lua_isboolean(L, -1) != 0
          ? (lua_toboolean(L, -1) != 0)
          : openwow::render::ui::UiTextureStateSupported(
                desaturated_state ? openwow::render::ui::UiTextureState::kDesaturated
                                  : openwow::render::ui::UiTextureState::kNormal);
  lua_pop(L, 1);
  return supported;
}

inline void SetTextureStateSupport(lua_State *L, bool normal_supported,
                                   bool desaturated_supported) {
  lua_pushboolean(L, normal_supported ? 1 : 0);
  lua_setfield(L, LUA_REGISTRYINDEX, kTextureNormalStateSupportedRegistryKey);
  lua_pushboolean(L, desaturated_supported ? 1 : 0);
  lua_setfield(L, LUA_REGISTRYINDEX, kTextureDesaturatedStateSupportedRegistryKey);
}

inline bool LuaTableFieldIsTruthy(lua_State *L, int index, const char *field) {
  index = lua_absindex(L, index);
  lua_getfield(L, index, field);
  const bool value = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  return value;
}

inline thread_local std::uint64_t g_layout_protection_traversal_count = 0;
inline thread_local std::uint64_t g_layout_protection_cache_hit_count = 0;

inline void ResetLuaLayoutProtectionDebugCounters() noexcept {
  g_layout_protection_traversal_count = 0;
  g_layout_protection_cache_hit_count = 0;
}

[[nodiscard]] inline std::uint64_t LuaLayoutProtectionTraversalCount() noexcept {
  return g_layout_protection_traversal_count;
}

[[nodiscard]] inline std::uint64_t LuaLayoutProtectionCacheHitCount() noexcept {
  return g_layout_protection_cache_hit_count;
}

inline std::uint32_t GetLuaLayoutProtectionGeneration(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, kLayoutProtectionGenerationRegistryKey);
  std::uint32_t generation =
      lua_isnumber(L, -1) != 0
          ? static_cast<std::uint32_t>(lua_tointeger(L, -1))
          : 1u;
  lua_pop(L, 1);
  if (generation == 0u) {
    generation = 1u;
  }
  return generation;
}

inline void PushLuaLayoutProtectionCache(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, kLayoutProtectionCacheRegistryKey);
  if (lua_istable(L, -1) != 0) {
    return;
  }

  lua_pop(L, 1);
  lua_newtable(L);
  lua_newtable(L);
  lua_pushstring(L, "k");
  lua_setfield(L, -2, "__mode");
  lua_setmetatable(L, -2);
  lua_pushvalue(L, -1);
  lua_setfield(L, LUA_REGISTRYINDEX, kLayoutProtectionCacheRegistryKey);
}

inline void BumpLuaLayoutProtectionGeneration(lua_State *L) {
  if (L == nullptr) {
    return;
  }

  std::uint32_t generation = GetLuaLayoutProtectionGeneration(L);
  if (generation >= 0x3fffffffu) {
    generation = 1u;
    lua_pushnil(L);
    lua_setfield(L, LUA_REGISTRYINDEX, kLayoutProtectionCacheRegistryKey);
  } else {
    ++generation;
  }
  lua_pushinteger(L, static_cast<lua_Integer>(generation));
  lua_setfield(L, LUA_REGISTRYINDEX, kLayoutProtectionGenerationRegistryKey);
}

inline bool TryGetCachedLuaLayoutProtection(lua_State *L, int index,
                                            const std::uint32_t generation,
                                            bool *const protected_state) {
  index = lua_absindex(L, index);
  PushLuaLayoutProtectionCache(L);
  const int cache_index = lua_absindex(L, -1);
  lua_pushvalue(L, index);
  lua_rawget(L, cache_index);
  const lua_Integer encoded =
      lua_isnumber(L, -1) != 0 ? lua_tointeger(L, -1) : 0;
  lua_pop(L, 2);

  if (encoded <= 0 ||
      static_cast<std::uint32_t>(encoded >> 1) != generation) {
    return false;
  }
  if (protected_state != nullptr) {
    *protected_state = (encoded & 1) != 0;
  }
  ++g_layout_protection_cache_hit_count;
  return true;
}

inline void CacheLuaLayoutProtection(lua_State *L, int index,
                                     const std::uint32_t generation,
                                     const bool protected_state) {
  index = lua_absindex(L, index);
  PushLuaLayoutProtectionCache(L);
  const int cache_index = lua_absindex(L, -1);
  lua_pushvalue(L, index);
  const auto encoded = static_cast<lua_Integer>(
      (static_cast<std::uint64_t>(generation) << 1u) |
      static_cast<std::uint64_t>(protected_state));
  lua_pushinteger(L, encoded);
  lua_rawset(L, cache_index);
  lua_pop(L, 1);
}

inline constexpr const char *kLuaAnchorDependentsField =
    "__ow_anchor_dependents";
inline constexpr const char *kLuaAnchorTargetsField = "__ow_anchor_targets";

inline void PushLuaWeakTable(lua_State *L, const char *mode) {
  lua_newtable(L);
  lua_newtable(L);
  runtime::PushInternedLuaString(L, mode);
  runtime::SetInternedLuaField(L, -2, "__mode");
  lua_setmetatable(L, -2);
}

inline bool PushLuaAnchorTargetTable(lua_State *L, int anchor_index) {
  anchor_index = lua_absindex(L, anchor_index);
  runtime::GetInternedLuaField(L, anchor_index, "relativeTo");
  if (lua_istable(L, -1) != 0) {
    return true;
  }
  if (lua_isstring(L, -1) != 0) {
    const char *target_name = lua_tostring(L, -1);
    lua_getglobal(L, target_name != nullptr ? target_name : "");
    lua_remove(L, -2);
    if (lua_istable(L, -1) != 0) {
      return true;
    }
  }
  lua_pop(L, 1);
  return false;
}

inline void PushLuaAnchorDependentsTable(lua_State *L, int target_index) {
  target_index = lua_absindex(L, target_index);
  runtime::GetInternedLuaField(L, target_index, kLuaAnchorDependentsField);
  if (lua_istable(L, -1) != 0) {
    return;
  }
  lua_pop(L, 1);

  PushLuaWeakTable(L, "k");
  lua_pushvalue(L, -1);
  runtime::SetInternedLuaField(L, target_index, kLuaAnchorDependentsField);
}

inline void ReindexLuaAnchorDependents(lua_State *L, int frame_index) {
  if (L == nullptr) {
    return;
  }
  frame_index = lua_absindex(L, frame_index);
  if (lua_istable(L, frame_index) == 0) {
    return;
  }

  const ScopedNeutralLuaTaint neutral_taint(L);
  const int original_top = lua_gettop(L);

  runtime::RawGetInternedLuaField(L, frame_index, kLuaAnchorTargetsField);
  bool reuse_targets = false;
  if (lua_istable(L, -1) != 0 && lua_getmetatable(L, -1) != 0) {
    runtime::RawGetInternedLuaField(L, -1, "__mode");
    const char *const mode = lua_type(L, -1) == LUA_TSTRING
                                 ? lua_tostring(L, -1)
                                 : nullptr;
    reuse_targets = mode != nullptr && std::strcmp(mode, "v") == 0;
    lua_pop(L, 2);
  }
  lua_pop(L, 1);

  runtime::GetInternedLuaField(L, frame_index, kLuaAnchorTargetsField);
  if (lua_istable(L, -1) != 0) {
    const int targets_index = lua_absindex(L, -1);
    lua_pushnil(L);
    while (lua_next(L, targets_index) != 0) {
      if (lua_istable(L, -1) != 0) {
        runtime::GetInternedLuaField(L, -1, kLuaAnchorDependentsField);
        if (lua_istable(L, -1) != 0) {
          lua_pushvalue(L, frame_index);
          lua_pushnil(L);
          lua_rawset(L, -3);
        }
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
      if (reuse_targets) {
        lua_pushvalue(L, -1);
        lua_pushnil(L);
        lua_rawset(L, targets_index);
      }
    }
  } else {
    reuse_targets = false;
  }

  if (reuse_targets) {
    lua_settop(L, original_top + 1);
  } else {
    lua_settop(L, original_top);
    PushLuaWeakTable(L, "v");
  }
  const int new_targets_index = lua_absindex(L, -1);
  lua_Integer stored_target_count = 0;

  runtime::GetInternedLuaField(L, frame_index, "__ow_anchors");
  if (lua_istable(L, -1) != 0) {
    const int anchors_index = lua_absindex(L, -1);
    lua_pushnil(L);
    while (lua_next(L, anchors_index) != 0) {
      if (lua_istable(L, -1) != 0 && PushLuaAnchorTargetTable(L, -1)) {
        const int target_index = lua_absindex(L, -1);

        if (lua_rawequal(L, target_index, frame_index) == 0) {
          PushLuaAnchorDependentsTable(L, target_index);
          lua_pushvalue(L, frame_index);
          lua_pushboolean(L, 1);
          lua_rawset(L, -3);
          lua_pop(L, 1);

          lua_pushvalue(L, target_index);
          lua_rawseti(L, new_targets_index, ++stored_target_count);
        }
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);

  runtime::SetInternedLuaField(L, frame_index, kLuaAnchorTargetsField);
  lua_settop(L, original_top);

  BumpLuaLayoutProtectionGeneration(L);
}

inline bool LuaLayoutObjectIsProtectedIterative(lua_State *L, const int index) {
  if (lua_istable(L, index) == 0) {
    return false;
  }

  const int original_top = lua_gettop(L);
  LuaTableGraphWorklist worklist(L);
  (void)worklist.Enqueue(index);

  while (worklist.PushNext()) {
    const int current_index = lua_absindex(L, -1);
    if (LuaTableFieldIsTruthy(L, current_index, "__ow_protected")) {
      lua_settop(L, original_top);
      return true;
    }

    lua_getfield(L, current_index, "__ow_children");
    if (lua_istable(L, -1) != 0) {
      const int children_index = lua_absindex(L, -1);
      lua_pushnil(L);
      while (lua_next(L, children_index) != 0) {
        (void)worklist.Enqueue(-1);
        lua_pop(L, 1);
      }
    }
    lua_pop(L, 1);

    lua_getfield(L, current_index, kLuaAnchorDependentsField);
    if (lua_istable(L, -1) != 0) {
      const int dependents_index = lua_absindex(L, -1);
      lua_pushnil(L);
      while (lua_next(L, dependents_index) != 0) {
        lua_pop(L, 1);
        (void)worklist.Enqueue(-1);
      }
    }
    lua_settop(L, current_index - 1);
  }

  lua_settop(L, original_top);
  return false;
}

inline bool LuaFrameHierarchyHasTruthyField(lua_State *L, int index, const char *field) {
  index = lua_absindex(L, index);
  if (lua_istable(L, index) == 0) {
    return false;
  }

  lua_pushvalue(L, index);
  const int traversal_top = lua_gettop(L);
  bool found = false;
  constexpr int kMaxLuaFrameParentDepth = 100;
  for (int depth = 0; depth < kMaxLuaFrameParentDepth && lua_istable(L, -1) != 0; ++depth) {
    if (LuaTableFieldIsTruthy(L, -1, field)) {
      found = true;
      break;
    }
    lua_getfield(L, -1, "__ow_parent");
    lua_remove(L, -2);
  }

  while (lua_gettop(L) >= traversal_top) {
    lua_pop(L, 1);
  }
  return found;
}

inline bool LuaFrameIsExplicitlyProtected(lua_State *L, int index) {
  const ScopedNeutralLuaTaint neutral_taint(L);
  return lua_istable(L, index) != 0 && LuaTableFieldIsTruthy(L, index, "__ow_protected");
}

inline bool LuaFrameIsProtected(lua_State *L, int index) {
  index = lua_absindex(L, index);
  if (lua_istable(L, index) == 0) {
    return false;
  }

  const ScopedNeutralLuaTaint neutral_taint(L);

  const std::uint32_t generation = GetLuaLayoutProtectionGeneration(L);
  bool protected_state = false;
  if (TryGetCachedLuaLayoutProtection(L, index, generation,
                                      &protected_state)) {
    return protected_state;
  }

  ++g_layout_protection_traversal_count;
  protected_state = LuaLayoutObjectIsProtectedIterative(L, index);
  CacheLuaLayoutProtection(L, index, generation, protected_state);
  return protected_state;
}

inline bool LuaFrameAllowsAttributeChanges(lua_State *L, int index) {
  return LuaFrameHierarchyHasTruthyField(L, index, "__ow_allow_attribute_changes");
}

inline bool LuaFrameCanChangeProtectedState(lua_State *L, int index) {
  auto &secure = SecureExecution::Get();
  return secure.IsSecure(L) || !secure.InCombatLockdown() ||
         !LuaFrameIsProtected(L, index);
}

inline int PushLuaLayoutProtectionResults(lua_State *L, int index) {
  lua_pushwowbool(L, LuaFrameIsProtected(L, index));
  lua_pushwowbool(L, LuaFrameIsExplicitlyProtected(L, index));
  return 2;
}

inline bool LuaFrameMutationBlocked(lua_State *L, int index) {
  return lua_istable(L, index) != 0 &&
         !LuaFrameCanChangeProtectedState(L, index);
}

inline bool LuaFrameAttributeMutationBlocked(lua_State *L, int index) {
  return lua_istable(L, index) != 0 &&
         !LuaFrameCanChangeProtectedState(L, index) &&
         !LuaFrameAllowsAttributeChanges(L, index);
}

inline bool AllowLuaFrameProtectedMutation(
    lua_State *L, const int index,
    const ProtectedActionFailureMode failure_mode) {
  if (!LuaFrameMutationBlocked(L, index)) {
    return true;
  }

  FireProtectedActionFailureEvent(L, failure_mode);
  return false;
}

inline constexpr std::size_t kBNetSanitizedChatTextBufferSize = 1021;

inline void CopySanitizedBNetChatText(std::string_view source, char *dest, std::size_t dest_size) {
  (void)::openwow::game::CopySanitizedBNetChatText(dest, dest_size, source);
}

struct QuestGreetingEntryView {
  std::uint64_t npc_guid = 0;
  std::uint32_t quest_id = 0;
  std::uint32_t quest_icon = 0;
  std::int32_t quest_level = 0;
  std::uint32_t quest_flags = 0;
  bool is_repeatable = false;
  const std::string *title = nullptr;
};

inline bool IsQuestGreetingActiveIcon(std::uint32_t quest_icon) {
  return quest_icon == 3 || quest_icon == 4;
}

inline bool IsQuestGreetingAvailableIcon(std::uint32_t quest_icon) {
  return !IsQuestGreetingActiveIcon(quest_icon);
}

template <typename EntryRange, typename Predicate>
inline const typename EntryRange::value_type *
FindQuestGreetingEntryByLuaIndex(const EntryRange &entries, int lua_index, Predicate predicate) {
  if (lua_index < 1) {
    return nullptr;
  }

  int visible_index = 0;
  for (const auto &entry : entries) {
    if (!predicate(entry)) {
      continue;
    }
    ++visible_index;
    if (visible_index == lua_index) {
      return &entry;
    }
  }
  return nullptr;
}

inline int CountQuestGreetingEntries(const openwow::game::WorldSession &session,
                                     bool want_active_entries) {
  const auto &quest_list = session.last_quest_list();
  if (quest_list.quests.empty()) {
    return 0;
  }

  int count = 0;
  for (const auto &entry : quest_list.quests) {
    const bool matches = want_active_entries ? IsQuestGreetingActiveIcon(entry.quest_icon)
                                             : IsQuestGreetingAvailableIcon(entry.quest_icon);
    if (matches) {
      ++count;
    }
  }
  return count;
}

inline std::optional<QuestGreetingEntryView>
GetQuestGreetingEntry(const openwow::game::WorldSession &session, int lua_index,
                      bool want_active_entry) {
  const auto &quest_list = session.last_quest_list();
  if (quest_list.quests.empty()) {
    return std::nullopt;
  }

  const auto *entry = FindQuestGreetingEntryByLuaIndex(
      quest_list.quests, lua_index, [want_active_entry](const auto &candidate) {
        return want_active_entry ? IsQuestGreetingActiveIcon(candidate.quest_icon)
                                 : IsQuestGreetingAvailableIcon(candidate.quest_icon);
      });
  if (!entry) {
    return std::nullopt;
  }

  QuestGreetingEntryView view;
  view.npc_guid = quest_list.npc_guid;
  view.quest_id = entry->quest_id;
  view.quest_icon = entry->quest_icon;
  view.quest_level = entry->quest_level;
  view.quest_flags = entry->quest_flags;
  view.is_repeatable = entry->is_repeatable;
  view.title = &entry->title;
  return view;
}

inline std::optional<openwow::game::CGPlayer_C::QuestLogEntry>
FindLocalPlayerQuestLogSlot(const openwow::game::WorldSession &session,
                            const std::uint32_t quest_id) {
  const auto *player = session.objects().GetLocalPlayerTyped();
  if (player == nullptr || quest_id == 0) {
    return std::nullopt;
  }

  for (std::uint8_t slot = 0; slot < openwow::game::kMaxQuestLogEntries; ++slot) {
    const auto entry = player->GetQuestLog(slot);
    if (entry.quest_id == quest_id) {
      return entry;
    }
  }

  return std::nullopt;
}

inline std::uint32_t
ComputeQuestWorldMapObjectiveMask(const openwow::game::QuestTemplate &quest_template,
                                  const openwow::game::CGPlayer_C::QuestLogEntry &player_slot,
                                  const openwow::game::PlayerInventoryReplica& inventory) {
  std::uint32_t objective_mask = 0;

  for (int objective_index = 0; objective_index < openwow::game::kQuestObjectivesCount;
       ++objective_index) {
    const auto &objective = quest_template.npc_or_go_objectives[objective_index];
    if (objective.creature_or_go != 0 &&
        player_slot.counts[objective_index] < objective.required_count) {
      objective_mask |= (1u << objective_index);
    }
  }

  for (int item_index = 0; item_index < openwow::game::kQuestItemObjectivesCount; ++item_index) {
    const auto &item_objective = quest_template.item_objectives[item_index];
    if (item_objective.item_id != 0 &&
        inventory.GetItemCount(item_objective.item_id) < item_objective.required_count) {
      objective_mask |= (1u << (item_index + 4));
    }
  }

  for (int item_index = 0; item_index < openwow::game::kQuestRewardsCount; ++item_index) {
    const auto &item_objective = quest_template.item_drop_objectives[item_index];
    if (item_objective.item_id != 0 && item_objective.item_id != quest_template.src_item_id &&
        inventory.GetItemCount(item_objective.item_id) == 0) {
      objective_mask |= (1u << (item_index + 10));
    }
  }

  if ((HasFlag(quest_template.flags, openwow::game::QuestFlags::kPartyAccept) ||
       HasFlag(quest_template.flags, openwow::game::QuestFlags::kExploration)) &&
      (player_slot.state & 0x01u) == 0) {
    objective_mask |= (1u << 16);
  }

  return objective_mask;
}

inline std::int32_t BuildQuestPoiIncompleteObjectiveMask(const openwow::game::WorldSession &session,
                                                         const std::uint32_t quest_id) {
  auto &mutable_session = const_cast<openwow::game::WorldSession &>(session);
  if (quest_id == 0 || FindInterleavedQuestIndexById(mutable_session, quest_id) == 0) {
    return 0;
  }

  const auto *quest_log_entry = session.quests().FindQuestLogEntry(quest_id);
  if (quest_log_entry == nullptr ||
      quest_log_entry->status == openwow::game::QuestStatus::kFailed) {
    return 0;
  }

  const auto player_slot = FindLocalPlayerQuestLogSlot(session, quest_id);
  if (!player_slot.has_value() || (player_slot->state & 0x02u) != 0) {
    return 0;
  }

  const auto *quest_template = session.quests().GetTemplate(quest_id);
  if (quest_template == nullptr) {
    return 0;
  }

  if (IsQuestTurnInReady(session, quest_id)) {
    return -1;
  }

  return static_cast<std::int32_t>(
      ComputeQuestWorldMapObjectiveMask(*quest_template, *player_slot,
                                        session.inventory_replica()));
}

inline bool QuestPoiPassesObjectiveMask(const openwow::game::QuestPOIEntry &poi,
                                        const std::int32_t objective_mask) {
  if (objective_mask == -1) {
    return poi.objectiveIndex == -1;
  }

  if (poi.objectiveIndex < 0 || poi.objectiveIndex >= 31) {
    return false;
  }

  return (static_cast<std::uint32_t>(objective_mask) & (1u << poi.objectiveIndex)) != 0;
}

inline bool IsQuestPoiVisibleOnCurrentSelection(
    const openwow::ui::WorldMapSystem &world_map,
    const openwow::ui::WorldMapSystem::QuestPoiSelectionContext &selection,
    const openwow::game::QuestPOIEntry &poi) {
  if (poi.points.empty()) {
    return false;
  }

  const auto anchor = openwow::game::QuestPOIData::Get().GetCentroid(poi);
  openwow::ui::WorldMapSystem::SelectionProjection projection;
  if (selection.selected_dungeon_map_id > 0) {
    if (poi.floorId != static_cast<std::uint32_t>(world_map.GetCurrentDungeonFloorIndex())) {
      return false;
    }

    projection = world_map.ProjectCurrentSelectionWithIndoorFlag(
        poi.mapId, anchor.x, anchor.y, 0.0f, selection.selected_dungeon_map_id);
  } else {
    if ((poi.flags & 0x4u) != 0 &&
        static_cast<std::int32_t>(poi.areaId) != selection.displayed_world_map_area_id) {
      return false;
    }

    projection = world_map.ProjectCurrentSelectionWithIndoorFlag(poi.mapId, anchor.x, anchor.y);
  }

  return projection.valid && !projection.indoors &&
         std::fabs(projection.x * projection.y) >= 0.001f;
}

inline std::uint32_t GetQuestGreenRangeForPlayerLevel(const std::uint32_t player_level) {
  return openwow::game::GetTrivialLevelDifference(player_level);
}

inline std::uint32_t GetQuestTrivialLevelOffsetForPlayerLevel(const std::uint32_t player_level) {
  if (player_level == 0) {
    return 0;
  }

  return GetQuestGreenRangeForPlayerLevel(player_level);
}

inline bool IsQuestLevelTrivialForPlayerLevel(std::uint32_t player_level,
                                              std::int32_t quest_level) {
  if (player_level == 0 || quest_level == -1) {
    return false;
  }

  return quest_level +
             static_cast<std::int32_t>(GetQuestTrivialLevelOffsetForPlayerLevel(player_level)) <
         static_cast<std::int32_t>(player_level);
}

inline bool IsQuestLevelTrivial(const openwow::game::WorldSession &session,
                                std::int32_t quest_level) {
  const auto *player = session.objects().GetLocalPlayer();
  if (!player) {
    return false;
  }
  return IsQuestLevelTrivialForPlayerLevel(player->GetLevel(), quest_level);
}

inline openwow::game::TargetingSystem *GetTargetingSystem(lua_State *L) {
  auto *session = GetWorldSession(L);
  return session != nullptr ? session->targeting_system() : nullptr;
}

inline openwow::game::DeathManager *GetDeathManager(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, kDeathManagerRegistryKey);
  auto *mgr = static_cast<openwow::game::DeathManager *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  return mgr;
}

inline openwow::game::ObjectGuid ResolveUnitId(openwow::game::WorldSession *session,
                                               const std::string &unit_id) {
  return openwow::game::UnitQueryBridge::Get().ResolveToGuid(session, unit_id);
}

inline const openwow::game::WorldObject *ResolveUnit(openwow::game::WorldSession *session,
                                                     const std::string &unit_id) {
  auto guid = ResolveUnitId(session, unit_id);
  if (guid.IsEmpty() || !session)
    return nullptr;
  return session->objects().Get(guid);
}

inline bool InventorySlotRequiresBankInteraction(const int slot) {
  return slot >= openwow::game::InventorySlots::kBankStart &&
         slot < openwow::game::InventorySlots::kBuybackStart;
}

inline const openwow::game::CGItem_C *GetLocalInventoryItemByAbsoluteSlot(lua_State *L,
                                                                          const int slot) {
  if (slot < 0 || slot >= openwow::game::InventorySlots::kTotalSlots) {
    return nullptr;
  }

  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return nullptr;
  }

  if (InventorySlotRequiresBankInteraction(slot) && session->bank_npc_guid() == 0) {
    return nullptr;
  }

  const auto *player = session->objects().GetLocalPlayerTyped();
  if (player == nullptr) {
    return nullptr;
  }

  const auto field_index = static_cast<std::uint16_t>(PLAYER_FIELD_INV_SLOT_HEAD + slot * 2);
  const auto item_guid = player->GetGuidField(field_index);
  if (item_guid == ObjectGuid()) {
    return nullptr;
  }

  return session->objects().GetItem(item_guid);
}

inline const openwow::game::ItemInstance *GetLocalInventoryItemInstanceByAbsoluteSlot(
    lua_State *L, const int slot) {
  if (slot < 0 || slot >= openwow::game::InventorySlots::kTotalSlots) {
    return nullptr;
  }

  auto *session = GetWorldSession(L);
  if (session == nullptr || session->objects().GetActivePlayer() == nullptr) {
    return nullptr;
  }

  if (InventorySlotRequiresBankInteraction(slot) && session->bank_npc_guid() == 0) {
    return nullptr;
  }

  return RequirePlayerInventoryReplica(L).GetItemInSlot(static_cast<std::uint8_t>(slot));
}

struct InventoryQueryOwner {
  const openwow::game::CGPlayer_C *player = nullptr;
  bool is_active_player = false;
};

inline std::optional<InventoryQueryOwner>
ResolveInventoryQueryOwner(openwow::game::WorldSession &session,
                           const std::string_view unit_token) {
  const auto owner_guid = ResolveUnitId(&session, std::string(unit_token));
  if (owner_guid.IsEmpty()) {
    return std::nullopt;
  }

  const auto *active_player = session.objects().GetLocalPlayerTyped();
  if (active_player == nullptr) {
    return std::nullopt;
  }

  if (owner_guid == active_player->GetGuid()) {
    return InventoryQueryOwner{
        .player = active_player,
        .is_active_player = true,
    };
  }

  if (owner_guid.GetRawValue() != session.arena().inspect_target_guid()) {
    return std::nullopt;
  }

  const auto *inspect_player = session.objects().GetPlayer(owner_guid);
  if (inspect_player == nullptr) {
    return std::nullopt;
  }

  return InventoryQueryOwner{
      .player = inspect_player,
      .is_active_player = false,
  };
}

inline std::optional<std::uint32_t>
ResolveInventoryItemIdEntry(lua_State *L, openwow::game::WorldSession &session,
                            std::string_view unit_token, const int slot) {
  const auto owner = ResolveInventoryQueryOwner(session, unit_token);
  if (!owner.has_value() || owner->player == nullptr) {
    return std::nullopt;
  }

  if (owner->is_active_player) {
    if (slot == -1) {
      const auto ammo_item_id = owner->player->GetUInt32(PLAYER_AMMO_ID);
      if (ammo_item_id == 0) {
        return std::nullopt;
      }
      return ammo_item_id;
    }

    const auto *item = GetLocalInventoryItemByAbsoluteSlot(L, slot);
    if (item != nullptr && item->GetEntry() != 0) {
      return item->GetEntry();
    }
    if (slot >= openwow::game::InventorySlots::kEquipStart &&
        slot < openwow::game::InventorySlots::kEquipEnd) {
      return owner->player->GetVisibleItemTemplateEntry(
          static_cast<std::uint8_t>(slot));
    }
    return std::nullopt;
  }

  if (slot < openwow::game::InventorySlots::kEquipStart ||
      slot >= openwow::game::InventorySlots::kEquipEnd) {
    return std::nullopt;
  }

  return owner->player->GetVisibleItemTemplateEntry(
      static_cast<std::uint8_t>(slot));
}

inline const openwow::game::CGUnit_C *ResolveUnitObject(const openwow::game::WorldObject *unit) {
  if (unit == nullptr || !unit->IsUnit()) {
    return nullptr;
  }

  return static_cast<const openwow::game::CGUnit_C *>(unit);
}

inline LuaUnitDerivedStatQuery LuaDerivedStatQuery(lua_State *L, std::string_view token) {
  auto *session = GetWorldSession(L);
  const auto *unit =
      session != nullptr ? ResolveUnitObject(ResolveUnit(session, std::string(token))) : nullptr;
  return LuaUnitDerivedStatQuery(GetDbcLoader(L), unit);
}

inline std::string SafeLuaString(lua_State *L, int index) {
  const char *s = lua_tostring(L, index);
  return s ? std::string(s) : std::string();
}

inline std::uint32_t
GetDefaultCreatureDisplayId(const openwow::game::CreatureTemplateInfo &creature_template) {
  return creature_template.display_ids[0];
}

inline void
ApplyCreatureTemplateBindingToFrame(lua_State *L, int frame_index,
                                    const openwow::game::CreatureTemplateInfo &creature_template) {
  frame_index = lua_absindex(L, frame_index);

  lua_pushinteger(L, 0);
  lua_setfield(L, frame_index, "__ow_model_unit_guid_lo");
  lua_pushinteger(L, 0);
  lua_setfield(L, frame_index, "__ow_model_unit_guid_hi");

  lua_pushinteger(L, static_cast<lua_Integer>(creature_template.entry));
  lua_setfield(L, frame_index, "__ow_model_creature");

  const auto display_id = GetDefaultCreatureDisplayId(creature_template);
  lua_pushinteger(L, static_cast<lua_Integer>(display_id));
  lua_setfield(L, frame_index, "__ow_model_display");
  if (display_id != 0) {
    lua_pushinteger(L, 0);
    lua_setfield(L, frame_index, "__ow_model_sequence");
  }
}

inline bool ParseClientBoolStringOrDefault(std::string_view value, bool default_value) {
  if (value.empty()) {
    return default_value;
  }

  switch (value.front()) {
  case '0':
  case 'F':
  case 'N':
  case 'f':
  case 'n':
    return false;
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9':
  case 'T':
  case 'Y':
  case 't':
  case 'y':
    return true;
  default:
    break;
  }

  if (openwow::text::EqualsIgnoreCaseAscii(value, "off") ||
      openwow::text::EqualsIgnoreCaseAscii(value, "disabled")) {
    return false;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(value, "on") ||
      openwow::text::EqualsIgnoreCaseAscii(value, "enabled")) {
    return true;
  }
  return default_value;
}

inline bool ReadClientBoolArgOrDefault(lua_State *L, int index, bool default_value) {
  switch (lua_type(L, index)) {
  case LUA_TNIL:
    return false;
  case LUA_TBOOLEAN:
    return lua_toboolean(L, index) != 0;
  case LUA_TNUMBER:

    return TruncateLuaNumberToSseI32(lua_tonumber(L, index)) != 0;
  case LUA_TSTRING: {
    const char *value = lua_tostring(L, index);
    return ParseClientBoolStringOrDefault(value ? std::string_view(value) : std::string_view(),
                                          default_value);
  }
  default:
    return default_value;
  }
}

inline const char *FindAsciiSubstringIgnoreCase(const char *haystack, const char *needle) {
  if (haystack == nullptr || needle == nullptr || *needle == '\0') {
    return nullptr;
  }

  const auto needle_length = std::strlen(needle);
  for (const char *cursor = haystack; *cursor != '\0'; ++cursor) {
    if (openwow::core::SStrCmpI(cursor, needle, needle_length) == 0) {
      return cursor;
    }
  }

  return nullptr;
}

inline std::uint32_t ResolveItemIdArg(lua_State *L, int index) {
  if (lua_isnumber(L, index)) {
    const auto item_id = static_cast<lua_Integer>(lua_tointeger(L, index));
    return item_id > 0 ? static_cast<std::uint32_t>(item_id) : 0;
  }

  if (!lua_isstring(L, index)) {
    return 0;
  }

  const auto item_name = SafeLuaString(L, index);
  if (item_name.empty()) {
    return 0;
  }

  if (const char *payload = FindAsciiSubstringIgnoreCase(item_name.c_str(), "item:")) {
    return static_cast<std::uint32_t>(std::strtoul(payload + 5, nullptr, 10));
  }

  auto *session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  const auto *tmpl = session->query_cache().GetItemTemplateByName(item_name);
  return tmpl ? tmpl->entry : 0;
}

inline const openwow::game::ItemSpellData *
FindItemOnUseSpell(const openwow::game::ItemTemplate &item) {
  return openwow::game::FindFirstOnUseSpell(item);
}

struct ScriptItemSpellView {
  std::uint32_t item_id = 0;
  std::uint32_t spell_id = 0;
  const openwow::game::ItemTemplate *item = nullptr;
  const openwow::game::ItemSpellData *item_spell = nullptr;
  const openwow::data::dbc::SpellEntry *spell = nullptr;

  [[nodiscard]] explicit operator bool() const {
    return item != nullptr && spell_id != 0 && spell != nullptr;
  }
};

inline ScriptItemSpellView ResolveItemSpellView(lua_State *L, int index) {
  ScriptItemSpellView view;
  view.item_id = ResolveItemIdArg(L, index);
  if (view.item_id == 0) {
    return view;
  }

  auto *session = GetWorldSession(L);
  if (!session) {
    return view;
  }

  view.item = session->query_cache().GetItemTemplate(view.item_id);
  if (!view.item) {
    return view;
  }

  view.item_spell = FindItemOnUseSpell(*view.item);
  if (view.item_spell != nullptr) {
    view.spell_id = view.item_spell->spell_id;
  } else {
    view.spell_id = openwow::game::ResolveItemUseSpellIdWithEquippedFallback(
        session->inventory_replica(), view.item_id, view.item,
        session->GetDbcLoader());
  }

  if (view.spell_id == 0) {
    return view;
  }

  view.spell = LookupSpellEntry(L, view.spell_id);
  return view;
}

inline bool SpellHasConsumableRequirement(const openwow::data::dbc::SpellEntry &spell) {
  for (std::size_t i = 0; i < spell.reagent.size(); ++i) {
    if (spell.reagent[i] != 0 && spell.reagent_count[i] > 0) {
      return true;
    }
  }

  return false;
}

inline bool SpellHasScriptRange(lua_State *L, const openwow::data::dbc::SpellEntry &spell) {
  if (spell.range_index == 0) {
    return false;
  }

  const auto *dbc = GetDbcLoader(L);
  if (!dbc) {
    return false;
  }

  const auto *range = dbc->spell_range().LookupEntry(spell.range_index);
  if (!range) {
    return false;
  }

  return range->range_min != 0.0f || range->range_min_friendly != 0.0f ||
         range->range_max != 0.0f || range->range_max_friendly != 0.0f;
}

inline std::string UnitIdArg(lua_State *L, int index) {
  return SafeLuaString(L, index);
}

inline std::int32_t ParseChannelCommandIndex(std::string_view value) {
  if (value.empty()) {
    return 0;
  }

  std::size_t offset = 0;
  const bool negative = value.front() == '-';
  if (negative) {
    offset = 1;
    if (offset >= value.size()) {
      return 0;
    }
  }

  const auto first = static_cast<unsigned char>(value[offset]);
  if (first < '0' || first > '9') {
    return 0;
  }

  std::uint32_t result = static_cast<std::uint32_t>(first - '0');
  ++offset;
  while (offset < value.size()) {
    const auto digit = static_cast<unsigned char>(value[offset]);
    if (digit < '0' || digit > '9') {
      break;
    }
    result = result * 10u + static_cast<std::uint32_t>(digit - '0');
    ++offset;
  }

  if (negative) {
    return static_cast<std::int32_t>(0u - result);
  }
  return static_cast<std::int32_t>(result);
}

inline std::optional<std::string> ResolveChannelNameOrIndex(const char *value) {
  if (!value || value[0] == '\0') {
    return std::nullopt;
  }

  const auto channel_index = ParseChannelCommandIndex(value);
  if (channel_index == 0) {
    return std::string(value);
  }
  if (channel_index < 1) {
    return std::nullopt;
  }

  const auto &chat_system = openwow::game::ChatSystem::Get();
  const auto index = static_cast<std::size_t>(channel_index - 1);
  if (index >= chat_system.GetChannelSlotCount()) {
    return std::nullopt;
  }

  const auto *channel = chat_system.GetLuaChannelBySlot(index);
  if (!channel) {
    return std::nullopt;
  }

  return channel->name;
}

inline int LuaSingleChannelCommand(lua_State *L, const char *function_name, std::uint16_t opcode) {
  if (!lua_isstring(L, 1)) {
    return luaL_error(L, "Usage: %s(\"channel\")", function_name);
  }

  const char *raw_channel = lua_tostring(L, 1);
  const auto channel_name = ResolveChannelNameOrIndex(raw_channel);
  if (!channel_name) {
    return 0;
  }

  if (auto *session = GetWorldSession(L)) {
    session->interaction().SendChannelCommand(opcode, *channel_name);
  }
  return 0;
}

inline void SendWatchedChannelSelection(lua_State *L, const std::string &channel_name,
                                        const bool suppress_duplicate_packet) {
  auto &chat_system = openwow::game::ChatSystem::Get();
  if (suppress_duplicate_packet && chat_system.IsWatchingJoinedChannel(channel_name)) {
    return;
  }

  chat_system.SelectWatchedJoinedChannel(channel_name);
  if (auto *session = GetWorldSession(L)) {
    session->interaction().SendChannelCommand(
        static_cast<std::uint16_t>(openwow::net::wotlk::Opcode::CMSG_SET_CHANNEL_WATCH),
        channel_name);
  }
}

inline void ClearWatchedChannelSelection(lua_State *L) {
  auto &chat_system = openwow::game::ChatSystem::Get();
  chat_system.ClearWatchedChannelSelection();
  if (auto *session = GetWorldSession(L)) {
    session->interaction().SendClearChannelWatch();
  }
}

inline bool MatchesGameUiTypeMask(const openwow::game::WorldObject &object,
                                  std::uint16_t type_mask) {
  return (object.GetTypeMask() & type_mask) != 0;
}

inline std::uint32_t FoldGameUiLookupCodepoint(std::uint32_t codepoint) {
  if (codepoint >= 'a' && codepoint <= 'z') {
    return codepoint - 32;
  }
  if (codepoint >= 0xE0 && codepoint <= 0xFE) {
    return codepoint - 32;
  }
  if (codepoint == 339) {
    return 338;
  }
  if (codepoint == 1025 || codepoint == 1105) {
    return 1045;
  }
  if (codepoint >= 0x410 && codepoint <= 0x415) {
    return codepoint - 1;
  }
  if (codepoint >= 0x430 && codepoint <= 0x44F) {
    return codepoint - (codepoint <= 0x435 ? 33 : 32);
  }
  return codepoint;
}

inline std::uint32_t DecodeGameUiLookupCodepoint(std::string_view text, std::size_t *offset) {
  if (!offset || *offset >= text.size()) {
    return 0;
  }

  const auto read_byte = [&](std::size_t index) { return static_cast<std::uint8_t>(text[index]); };

  const std::size_t i = *offset;
  const auto lead = read_byte(i);
  std::uint32_t codepoint = 0xFFFDu;
  std::size_t step = 1;

  if ((lead & 0x80u) == 0) {
    codepoint = lead;
  } else if ((lead & 0xE0u) == 0xC0u && i + 1 < text.size()) {
    codepoint = (static_cast<std::uint32_t>(lead & 0x1Fu) << 6) |
                static_cast<std::uint32_t>(read_byte(i + 1) & 0x3Fu);
    step = 2;
  } else if ((lead & 0xF0u) == 0xE0u && i + 2 < text.size()) {
    codepoint = (static_cast<std::uint32_t>(lead & 0x0Fu) << 12) |
                (static_cast<std::uint32_t>(read_byte(i + 1) & 0x3Fu) << 6) |
                static_cast<std::uint32_t>(read_byte(i + 2) & 0x3Fu);
    step = 3;
  } else if ((lead & 0xF8u) == 0xF0u && i + 3 < text.size()) {
    codepoint = (static_cast<std::uint32_t>(lead & 0x07u) << 18) |
                (static_cast<std::uint32_t>(read_byte(i + 1) & 0x3Fu) << 12) |
                (static_cast<std::uint32_t>(read_byte(i + 2) & 0x3Fu) << 6) |
                static_cast<std::uint32_t>(read_byte(i + 3) & 0x3Fu);
    step = 4;
  }

  *offset += step;
  return FoldGameUiLookupCodepoint(codepoint);
}

inline bool GameUiLookupMatches(std::string_view candidate, std::string_view query,
                                bool exact_match) {
  if (candidate.empty() || query.empty()) {
    return false;
  }

  int remaining = exact_match ? std::numeric_limits<int>::max()
                              : static_cast<int>(openwow::core::CountLegacyUtf8Codepoints(query));
  std::size_t candidate_offset = 0;
  std::size_t query_offset = 0;

  while ((candidate_offset < candidate.size() || query_offset < query.size()) && remaining-- > 0) {
    const auto candidate_cp = DecodeGameUiLookupCodepoint(candidate, &candidate_offset);
    const auto query_cp = DecodeGameUiLookupCodepoint(query, &query_offset);
    if (candidate_cp != query_cp) {
      return false;
    }
  }

  if (!exact_match) {
    return true;
  }

  return candidate_offset >= candidate.size() && query_offset >= query.size();
}

enum class GameUiLookupAction : int {
  kNone = 0,
  kAttack = 1,
  kAttackPlayerOnly = 2,
  kAssist = 3,
  kAssistPlayerOnly = 4,
  kPartyMember = 5,
  kRaidMember = 6,
};

inline std::optional<openwow::game::TargetFilter>
ToTargetFilter(GameUiLookupAction action) {
  switch (action) {
  case GameUiLookupAction::kAttack:
    return openwow::game::TargetFilter::kEnemy;
  case GameUiLookupAction::kAttackPlayerOnly:
    return openwow::game::TargetFilter::kEnemyPlayer;
  case GameUiLookupAction::kAssist:
    return openwow::game::TargetFilter::kFriend;
  case GameUiLookupAction::kAssistPlayerOnly:
    return openwow::game::TargetFilter::kFriendPlayer;
  case GameUiLookupAction::kNone:
  case GameUiLookupAction::kPartyMember:
  case GameUiLookupAction::kRaidMember:
    return std::nullopt;
  }

  return std::nullopt;
}

inline bool GameUiLookupHasRealmSuffix(std::string_view query) {
  return query.find('-') != std::string_view::npos;
}

inline bool IsGameUiPartyMember(const openwow::game::WorldSession &session,
                                openwow::game::ObjectGuid guid) {
  if (guid.IsEmpty()) {
    return false;
  }
  const auto &members = session.group().members();
  const auto local_guid = session.objects().GetLocalPlayerGuid();

  if (guid == local_guid) {
    return true;
  }

  if (!session.group().IsRaid()) {
    for (const auto &member : members) {
      if (member.guid == guid) {
        return true;
      }
    }
    return false;
  }

  for (const auto &member : members) {
    if (member.guid == guid) {
      return member.sub_group == session.group().my_sub_group();
    }
  }
  return false;
}

inline bool IsGameUiRaidMember(const openwow::game::WorldSession &session,
                               openwow::game::ObjectGuid guid) {
  if (guid.IsEmpty()) {
    return false;
  }
  if (guid == session.objects().GetLocalPlayerGuid()) {
    return true;
  }
  return std::any_of(session.group().members().begin(), session.group().members().end(),
                     [&](const openwow::game::GroupMember &member) { return member.guid == guid; });
}

inline bool PassesGameUiLookupUnresolvedGuidAction(openwow::game::WorldSession *session,
                                                   openwow::game::ObjectGuid guid,
                                                   GameUiLookupAction action,
                                                   std::uint16_t type_mask) {
  if (!session || guid.IsEmpty()) {
    return false;
  }

  switch (action) {
  case GameUiLookupAction::kNone:
    return true;
  case GameUiLookupAction::kAttack:
  case GameUiLookupAction::kAttackPlayerOnly:
    return false;
  case GameUiLookupAction::kAssist:
    return IsGameUiRaidMember(*session, guid);
  case GameUiLookupAction::kAssistPlayerOnly: {
    const auto raw_high = static_cast<std::uint32_t>(guid.GetRawValue() >> 32);
    if ((raw_high & 0xF0000000u) != 0) {
      return false;
    }
    if ((raw_high & 0x0F07FFFFu) == 0 && static_cast<std::uint32_t>(guid.GetRawValue()) == 0) {
      return false;
    }
    return IsGameUiRaidMember(*session, guid);
  }
  case GameUiLookupAction::kPartyMember:
    return (type_mask == openwow::game::kTypeMaskUnit ||
            type_mask == openwow::game::kTypeMaskPlayer) &&
           IsGameUiPartyMember(*session, guid);
  case GameUiLookupAction::kRaidMember:
    return (type_mask == openwow::game::kTypeMaskUnit ||
            type_mask == openwow::game::kTypeMaskPlayer) &&
           IsGameUiRaidMember(*session, guid);
  }

  return false;
}

inline bool PassesGameUiLookupObjectAction(openwow::game::WorldSession *session,
                                           const openwow::game::WorldObject &object,
                                           GameUiLookupAction action) {
  if (action == GameUiLookupAction::kNone) {
    return true;
  }

  const auto *player_object = session->objects().GetLocalPlayer();
  if (!player_object || !player_object->IsUnit()) {
    return false;
  }
  if (!object.IsUnit()) {
    return false;
  }

  const auto &player = static_cast<const openwow::game::CGUnit_C &>(*player_object);
  const auto &unit = static_cast<const openwow::game::CGUnit_C &>(object);

  if (const auto filter = ToTargetFilter(action); filter.has_value()) {
    return openwow::game::MatchesTargetFilterAction(player, unit, *filter);
  }

  switch (action) {
  case GameUiLookupAction::kPartyMember:
    return IsGameUiPartyMember(*session, object.GetGuid());
  case GameUiLookupAction::kRaidMember:
    return IsGameUiRaidMember(*session, object.GetGuid());
  case GameUiLookupAction::kNone:
  case GameUiLookupAction::kAttack:
  case GameUiLookupAction::kAttackPlayerOnly:
  case GameUiLookupAction::kAssist:
  case GameUiLookupAction::kAssistPlayerOnly:
    break;
  }

  return true;
}

inline std::string BuildGameUiLookupObjectName(openwow::game::WorldSession *session,
                                               const openwow::game::WorldObject &object,
                                               bool query_has_realm_suffix) {
  std::string name = object.GetName();

  if (object.IsPlayer()) {
    const auto raw_guid = object.GetGuid().GetRawValue();
    if (const auto *entry = session->query_cache().GetPlayerName(raw_guid)) {
      if (name.empty()) {
        name = entry->name;
      }
      if (query_has_realm_suffix && !entry->realm_name.empty()) {
        return name + "-" + entry->realm_name;
      }
    } else if (name.empty()) {
      name = session->objects().GetPlayerName(object.GetGuid());
    }
    return name;
  }

  if (name.empty() && object.IsGameObject()) {
    if (const auto *entry = session->query_cache().GetGameObjectTemplate(object.GetEntry())) {
      name = entry->name;
    }
  } else if (name.empty()) {
    if (const auto *entry = session->query_cache().GetCreatureTemplate(object.GetEntry())) {
      name = entry->name;
    }
  }

  return name;
}

inline std::string BuildGameUiLookupGroupMemberName(openwow::game::WorldSession *session,
                                                    const openwow::game::GroupMember &member,
                                                    bool query_has_realm_suffix) {
  if (!session) {
    return {};
  }
  if (const auto *entry = session->query_cache().GetPlayerName(member.guid.GetRawValue())) {
    if (query_has_realm_suffix && !entry->realm_name.empty()) {
      return entry->name + "-" + entry->realm_name;
    }
    if (!entry->name.empty()) {
      return entry->name;
    }
  }
  if (!member.name.empty()) {
    return member.name;
  }
  return session->objects().GetPlayerName(member.guid);
}

inline openwow::game::ObjectGuid
ResolveGameUiExactGroupMemberByName(openwow::game::WorldSession *session, std::string_view query,
                                    bool query_has_realm_suffix) {
  if (!session || query.empty()) {
    return {};
  }

  const auto check_guid = [&](openwow::game::ObjectGuid guid, std::string name) {
    return !guid.IsEmpty() && GameUiLookupMatches(name, query, true);
  };

  const auto local_guid = session->objects().GetLocalPlayerGuid();
  if (!local_guid.IsEmpty()) {
    const auto *local_player = session->objects().GetLocalPlayer();
    const auto local_name =
        local_player != nullptr
            ? BuildGameUiLookupObjectName(session, *local_player, query_has_realm_suffix)
            : session->objects().GetPlayerName(local_guid);
    if (check_guid(local_guid, local_name)) {
      return local_guid;
    }
  }

  for (const auto &member : session->group().members()) {
    const auto name = BuildGameUiLookupGroupMemberName(session, member, query_has_realm_suffix);
    if (check_guid(member.guid, name)) {
      return member.guid;
    }
  }

  return {};
}

inline void UpdateGameUiLookupBestCandidate(openwow::game::ObjectGuid guid,
                                            std::string_view candidate_name, std::string_view query,
                                            bool exact_match, float candidate_score,
                                            openwow::game::ObjectGuid *best_guid,
                                            float *best_score) {
  if (!best_guid || !best_score || candidate_name.empty()) {
    return;
  }
  if (!GameUiLookupMatches(candidate_name, query, exact_match)) {
    return;
  }
  if (*best_score > candidate_score) {
    *best_score = candidate_score;
    *best_guid = guid;
  }
}

inline float ComputeGameUiLookupObjectScore(openwow::game::WorldSession *session,
                                            const openwow::game::WorldObject &object,
                                            bool filter_foreign_totems) {
  constexpr float kCurrentTargetPenalty = 2500000000.0f;
  constexpr float kCorpseOrCritterPenalty = 10000.0f;
  constexpr std::uint32_t kUnitDynFlagDead = 0x20u;

  const auto current_target = session->objects().GetTargetGuid();
  if (current_target == object.GetGuid()) {
    return kCurrentTargetPenalty;
  }

  const auto *player_object = session->objects().GetLocalPlayer();
  float score = 0.0f;
  if (player_object) {
    score = static_cast<float>(player_object->GetSquaredDistanceToPosition(object.GetPosition()));
  }

  if (!object.IsUnit()) {
    return score;
  }

  const auto &unit = static_cast<const openwow::game::CGUnit_C &>(object);
  if (filter_foreign_totems && player_object && player_object->IsUnit() &&
      unit.State().GetCreatureType() == CreatureTypeId::kTotem) {
    const auto &player = static_cast<const openwow::game::CGUnit_C &>(*player_object);
    if (!player.Interaction().IsFriendlyTo(unit)) {
      return std::numeric_limits<float>::infinity();
    }
  }

  if (unit.State().IsDead() || (unit.GetUInt32(UNIT_DYNAMIC_FLAGS) & kUnitDynFlagDead) != 0 ||
      unit.State().GetCreatureType() == CreatureTypeId::kCritter) {
    score += kCorpseOrCritterPenalty;
  }

  return score;
}

inline openwow::game::ObjectGuid
ResolveGameUiLookup(openwow::game::WorldSession *session, std::string_view token_or_name,
                    std::uint16_t type_mask, int interaction_kind, bool exact_match,
                    bool filter_foreign_totems,
                    float range_sq = std::numeric_limits<float>::max()) {
  if (!session || token_or_name.empty()) {
    return {};
  }

  const auto action = static_cast<GameUiLookupAction>(interaction_kind);
  const auto query_has_realm_suffix = GameUiLookupHasRealmSuffix(token_or_name);

  auto guid = openwow::game::UnitQueryBridge::Get().ResolveToGuid(session, token_or_name);
  if (guid.IsEmpty()) {
    guid = ResolveGameUiExactGroupMemberByName(session, token_or_name, query_has_realm_suffix);
  }
  if (!guid.IsEmpty()) {
    if (const auto *object = session->objects().Get(guid); object != nullptr) {
      if (MatchesGameUiTypeMask(*object, type_mask) &&
          PassesGameUiLookupObjectAction(session, *object, action)) {
        return guid;
      }
      return {};
    }
    if (type_mask != openwow::game::kTypeMaskUnit && type_mask != openwow::game::kTypeMaskPlayer) {
      return guid;
    }
    if (PassesGameUiLookupUnresolvedGuidAction(session, guid, action, type_mask)) {
      return guid;
    }
  }

  openwow::game::ObjectGuid best_guid;
  float best_score = range_sq;

  if (action != GameUiLookupAction::kAttack && action != GameUiLookupAction::kAttackPlayerOnly) {
    for (const auto &member : session->group().members()) {
      if (member.guid == session->objects().GetLocalPlayerGuid()) {
        continue;
      }
      if (session->group().IsRaid() && member.sub_group != session->group().my_sub_group()) {
        continue;
      }

      const auto name = BuildGameUiLookupGroupMemberName(session, member, query_has_realm_suffix);
      UpdateGameUiLookupBestCandidate(member.guid, name, token_or_name, exact_match, 2500000000.0f,
                                      &best_guid, &best_score);
    }

    if (action != GameUiLookupAction::kPartyMember) {
      for (const auto &member : session->group().members()) {
        const auto name = BuildGameUiLookupGroupMemberName(session, member, query_has_realm_suffix);
        UpdateGameUiLookupBestCandidate(member.guid, name, token_or_name, exact_match,
                                        2500000000.0f, &best_guid, &best_score);
      }
    }
  }

  session->objects().ForEach([&](const openwow::game::WorldObject &object) {
    if (!MatchesGameUiTypeMask(object, type_mask)) {
      return;
    }
    if (!PassesGameUiLookupObjectAction(session, object, action)) {
      return;
    }

    const auto name = BuildGameUiLookupObjectName(session, object, query_has_realm_suffix);
    const auto score = ComputeGameUiLookupObjectScore(session, object, filter_foreign_totems);
    if (!std::isfinite(score)) {
      return;
    }
    UpdateGameUiLookupBestCandidate(object.GetGuid(), name, token_or_name, exact_match, score,
                                    &best_guid, &best_score);
  });

  return best_guid;
}

inline openwow::game::ObjectGuid
ResolveGroupPlayerTargetGuid(openwow::game::WorldSession *session,
                             std::string_view token_or_name,
                             const bool exact_match) {
  return ResolveGameUiLookup(session, token_or_name, openwow::game::kTypeMaskPlayer, 6,
                             exact_match, false);
}

inline std::uint64_t DefaultActionTargetGuid(openwow::game::WorldSession *session, bool on_self) {
  if (!session)
    return 0;
  const auto *player = session->objects().GetLocalPlayer();
  if (!player)
    return 0;
  if (on_self) {
    return player->GetGuid().GetRawValue();
  }
  return session->objects().GetTargetGuid().GetRawValue();
}

inline const openwow::data::dbc::SpellEntry *LookupSpellEntry(lua_State *L,
                                                              std::uint32_t spell_id) {
  if (spell_id == 0)
    return nullptr;

  auto *dbc = GetDbcLoader(L);
  if (!dbc)
    return nullptr;

  return dbc->spell().LookupEntry(spell_id);
}

inline bool SpellHasAttackActionEffect(lua_State *L, std::uint32_t spell_id) {
  return openwow::game::SpellHasAttackActionEffect(spell_id,
                                                   L != nullptr ? GetDbcLoader(L) : nullptr);
}

inline const openwow::data::dbc::StableSlotPricesEntry *
GetNextStableSlotPriceEntry(const openwow::game::WorldSession &session,
                            const openwow::data::dbc::DbcLoader *dbc) {
  if (dbc == nullptr) {
    return nullptr;
  }

  const auto next_slot = static_cast<std::uint32_t>(session.pet().stable_list().max_slots) + 1u;
  return dbc->stable_slot_prices().LookupEntry(next_slot);
}

inline bool IsCurrentSpellIdActive(lua_State *L, std::uint32_t spell_id) {
  if (spell_id == 0)
    return false;

  if (LookupSpellEntry(L, spell_id) == nullptr)
    return false;

  const auto* session = GetWorldSession(L);
  return session != nullptr &&
         openwow::game::IsCurrentSpellId(*session, spell_id);
}

inline std::uint32_t ResolveSpellBookSpellId(lua_State *L, std::uint32_t slot,
                                             std::string_view book_type) {
  if (slot == 0)
    return 0;

  if (openwow::text::EqualsIgnoreCaseAscii(book_type, "pet")) {
    auto *session = GetWorldSession(L);
    if (!session) {
      return 0;
    }
    return session->pet().GetSpellbookSpellId(slot);
  }

  const auto *spell =
      openwow::game::SpellbookSystem::Get().GetPlayerSpellBookSlot(slot);
  return spell ? spell->spell_id : 0;
}

inline bool IsSpellBookSelector(std::string_view value) {
  return openwow::text::EqualsIgnoreCaseAscii(value, "spell") ||
         openwow::text::EqualsIgnoreCaseAscii(value, "pet");
}

inline bool AllowsRawSpellIdCurrentSpellQuery(std::string_view api_name) {
  return api_name == "IsUsableSpell";
}

inline std::optional<std::uint32_t>
FindSpellBookSlotIndexBySpellId(const openwow::game::WorldSession *session,
                                const std::uint32_t spell_id, const bool from_pet_book) {
  if (spell_id == 0) {
    return std::nullopt;
  }

  if (from_pet_book) {
    if (session == nullptr) {
      return std::nullopt;
    }

    const auto &visible_packet_spells = session->pet().pet_bar().spellbook_spells;
    for (std::size_t slot = visible_packet_spells.size(); slot > 0; --slot) {
      if (visible_packet_spells[slot - 1] == spell_id) {
        return static_cast<std::uint32_t>(slot - 1);
      }
    }

    return std::nullopt;
  }

  const auto &spellbook = openwow::game::SpellbookSystem::Get();
  for (std::size_t slot = spellbook.GetPlayerSpellBookSlotCount(); slot > 0; --slot) {
    const auto *spell =
        spellbook.GetPlayerSpellBookSlot(static_cast<std::uint32_t>(slot));
    if (spell != nullptr && spell->spell_id == spell_id) {
      return static_cast<std::uint32_t>(slot - 1);
    }
  }

  return std::nullopt;
}

inline std::optional<std::uint32_t> FindSpellBookSlotIndexBySpellId(lua_State *L,
                                                                    const std::uint32_t spell_id,
                                                                    const bool from_pet_book) {
  return FindSpellBookSlotIndexBySpellId(GetWorldSession(L), spell_id, from_pet_book);
}

inline const PetActionButton *
FindPetActionBarSpellBySpellId(const openwow::game::WorldSession *session,
                               const std::uint32_t spell_id) {
  if (session == nullptr || spell_id == 0) {
    return nullptr;
  }

  const auto &pet_bar = session->pet().pet_bar();
  for (std::size_t slot = 10; slot > 0; --slot) {
    const auto &action = pet_bar.action_bar[slot - 1];
    if (!IsPetSpellActionKind(action.ActionKind())) {
      continue;
    }
    if (action.ActionId() == spell_id) {
      return &action;
    }
  }

  return nullptr;
}

inline bool HasPetActionBarSpellBySpellId(const openwow::game::WorldSession *session,
                                          const std::uint32_t spell_id) {
  return FindPetActionBarSpellBySpellId(session, spell_id) != nullptr;
}

inline std::uint32_t ResolveSpellBookSlotBySpellId(lua_State *L, const std::uint32_t spell_id,
                                                   const bool from_pet_book) {
  const auto slot_index = FindSpellBookSlotIndexBySpellId(L, spell_id, from_pet_book);
  if (!slot_index.has_value()) {
    return 0;
  }

  return *slot_index + 1;
}

struct ResolvedSpellNameQuery {
  std::uint32_t spell_id = 0;
  bool from_pet_book = false;
};

inline std::optional<ResolvedSpellNameQuery>
FindKnownSpellQueryByNameQualifier(lua_State * , std::string_view spell_name,
                                   std::string_view qualifier) {
  if (spell_name.empty()) {
    return std::nullopt;
  }

  const auto match = openwow::game::SpellBookFrame::ResolveSpellByName(spell_name, qualifier);
  if (!match.has_value()) {
    return std::nullopt;
  }

  return ResolvedSpellNameQuery{
      match->spell_id,
      match->from_pet_spellbook,
  };
}

inline std::optional<ResolvedSpellNameQuery>
ResolveSpellQueryFromLooseNameQuery(lua_State *L, std::string raw_spell_name) {
  if (!raw_spell_name.empty() && raw_spell_name.front() == '!') {
    raw_spell_name.erase(raw_spell_name.begin());
  }

  if (raw_spell_name.empty()) {
    return std::nullopt;
  }

  if (const auto direct_match = FindKnownSpellQueryByNameQualifier(L, raw_spell_name, {});
      direct_match.has_value()) {
    return direct_match;
  }

  const auto open_paren = raw_spell_name.find('(');
  if (open_paren == std::string::npos) {
    return std::nullopt;
  }

  std::string qualifier = raw_spell_name.substr(open_paren + 1);
  raw_spell_name.resize(open_paren);
  if (raw_spell_name.empty()) {
    return std::nullopt;
  }

  if (const auto close_paren = qualifier.find(')'); close_paren != std::string::npos) {
    qualifier.resize(close_paren);
  }

  return FindKnownSpellQueryByNameQualifier(L, raw_spell_name, qualifier);
}

inline std::uint32_t ResolveKnownSpellIdFromLooseNameQuery(lua_State *L,
                                                           std::string raw_spell_name) {
  if (const auto resolved = ResolveSpellQueryFromLooseNameQuery(L, std::move(raw_spell_name));
      resolved.has_value()) {
    return resolved->spell_id;
  }

  return 0;
}

inline std::optional<ResolvedSpellNameQuery>
ResolveSpellQueryFromQualifiedNameQuery(lua_State *L, std::string_view spell_name,
                                        std::string_view qualifier) {
  return FindKnownSpellQueryByNameQualifier(L, spell_name, qualifier);
}

struct ScriptResolvedCurrentSpellQuery {
  std::uint32_t spell_id = 0;
  std::uint32_t spellbook_slot = 0;
  bool from_pet_book = false;
  int trailing_argument_index = 0;
};

inline std::optional<ScriptResolvedCurrentSpellQuery>
ResolveScriptCurrentSpellQuery(lua_State *L, std::string_view api_name) {
  if (lua_isnumber(L, 1)) {
    const auto slot = static_cast<lua_Integer>(lua_tointeger(L, 1));
    if (!lua_isstring(L, 2) && AllowsRawSpellIdCurrentSpellQuery(api_name)) {
      return slot > 0 ? std::optional<ScriptResolvedCurrentSpellQuery>(
                            ScriptResolvedCurrentSpellQuery{
                                static_cast<std::uint32_t>(slot), 0, false, 2})
                      : std::nullopt;
    }

    if (slot < 1 || slot > 1024) {
      luaL_error(L, "%s(): Invalid spell slot", std::string(api_name).c_str());
      return std::nullopt;
    }

    if (!lua_isstring(L, 2)) {
      luaL_error(L, "%s(): Invalid spell slot", std::string(api_name).c_str());
      return std::nullopt;
    }

    auto book_selector = SafeLuaString(L, 2);
    int trailing_argument_index = 3;
    if (!IsSpellBookSelector(book_selector)) {
      if (api_name == "IsSpellInRange") {
        book_selector = "spell";
      } else {
        luaL_error(L, "%s(): Invalid spell slot", std::string(api_name).c_str());
        return std::nullopt;
      }
    }

    return ScriptResolvedCurrentSpellQuery{
        ResolveSpellBookSpellId(L, static_cast<std::uint32_t>(slot), book_selector),
        static_cast<std::uint32_t>(slot),
        openwow::text::EqualsIgnoreCaseAscii(book_selector, "pet"),
        trailing_argument_index};
  }

  if (!lua_isstring(L, 1)) {
    luaL_error(L, "%s(): Invalid spell slot", std::string(api_name).c_str());
    return std::nullopt;
  }

  const auto spell_name = SafeLuaString(L, 1);
  if (!lua_isstring(L, 2)) {
    if (const auto resolved = ResolveSpellQueryFromLooseNameQuery(L, spell_name);
        resolved.has_value()) {
      const auto slot =
          ResolveSpellBookSlotBySpellId(L, resolved->spell_id, resolved->from_pet_book);
      if (slot == 0) {
        return std::nullopt;
      }

      return ScriptResolvedCurrentSpellQuery{resolved->spell_id, slot, resolved->from_pet_book, 2};
    }
    return std::nullopt;
  }

  const auto arg2 = SafeLuaString(L, 2);
  const auto treat_as_loose_name =
      openwow::game::ParseUnitId(arg2).kind != openwow::game::UnitIdKind::kUnknown;
  const auto resolved = treat_as_loose_name
                            ? ResolveSpellQueryFromLooseNameQuery(L, spell_name)
                            : ResolveSpellQueryFromQualifiedNameQuery(L, spell_name, arg2);
  if (!resolved.has_value()) {
    return std::nullopt;
  }

  const auto slot = ResolveSpellBookSlotBySpellId(L, resolved->spell_id, resolved->from_pet_book);
  if (slot == 0) {
    return std::nullopt;
  }

  return ScriptResolvedCurrentSpellQuery{resolved->spell_id, slot, resolved->from_pet_book,
                                         treat_as_loose_name ? 2 : 3};
}

inline std::optional<std::uint32_t> ResolveSpellIdOrCurrentSpellQuery(lua_State *L,
                                                                      std::string_view api_name) {
  if (lua_isnumber(L, 1) && lua_gettop(L) == 1) {
    const auto spell_id = static_cast<lua_Integer>(lua_tointeger(L, 1));
    return spell_id > 0 ? static_cast<std::uint32_t>(spell_id) : 0u;
  }

  if (const auto query = ResolveScriptCurrentSpellQuery(L, api_name); query.has_value()) {
    return query->spell_id;
  }

  return std::nullopt;
}

inline constexpr std::uint32_t kBlockedPetActionUnitFlags =
    static_cast<std::uint32_t>(UnitStateFlag::kStunned) |
    static_cast<std::uint32_t>(UnitStateFlag::kConfused) |
    static_cast<std::uint32_t>(UnitStateFlag::kFleeing);

inline bool PetActionAvailabilityRequiresForceCheck(const std::uint8_t action_kind) {
  return IsPetSpellActionKind(action_kind);
}

inline ObjectGuid GetPrimaryPetActionGuid(const WorldSession &session) {
  return session.pet().GetPrimaryPetGuid();
}

inline bool CanUsePetActions(const WorldSession &session, const bool force_check) {
  const auto *local_player = session.objects().GetLocalPlayerTyped();
  if (local_player == nullptr || !local_player->State().GetCharmedBy().IsEmpty()) {
    return false;
  }

  const ObjectGuid primary_pet_guid = GetPrimaryPetActionGuid(session);
  if (primary_pet_guid.IsEmpty()) {
    return false;
  }

  const ObjectGuid active_control_guid = local_player->GetActiveControlGuid();
  if (!active_control_guid.IsEmpty() && active_control_guid != primary_pet_guid) {
    return false;
  }

  const bool require_active_control = session.pet().pet_bar().RequiresActiveControl();

  std::vector<std::uint64_t> candidate_pet_guids = session.pet().pet_guids();
  if (candidate_pet_guids.empty() && !primary_pet_guid.IsEmpty()) {
    candidate_pet_guids.push_back(primary_pet_guid.GetRawValue());
  }

  for (const auto raw_guid : candidate_pet_guids) {
    if (raw_guid == 0) {
      continue;
    }

    const ObjectGuid pet_guid(raw_guid);
    const auto *pet_unit = session.objects().GetUnit(pet_guid);
    if (pet_unit == nullptr) {
      continue;
    }

    ObjectGuid owner_guid = pet_unit->State().GetCharmedBy();
    if (owner_guid.IsEmpty()) {
      owner_guid = pet_unit->State().GetSummonedBy();
    }

    if (owner_guid != local_player->GetGuid()) {
      continue;
    }

    if (!force_check && (pet_unit->State().GetUnitFlags() & kBlockedPetActionUnitFlags) != 0) {
      continue;
    }

    if (!require_active_control) {
      return true;
    }

    if (!active_control_guid.IsEmpty() && active_control_guid == pet_guid) {
      return true;
    }
  }

  return false;
}

inline bool CanUsePetAutocastActions(const WorldSession &session) {
  return CanUsePetActions(session, false);
}

inline std::uint64_t ResolveSelectedActionTargetGuid(
    const WorldSession &session, const std::uint64_t requested_target_guid,
    const bool has_explicit_target) {
  if (has_explicit_target || requested_target_guid != 0) {
    return requested_target_guid;
  }

  return session.objects().GetTargetGuid().GetRawValue();
}

inline std::uint64_t ResolvePlayerAttackCommandTargetGuid(
    lua_State *state, const WorldSession &session,
    const std::uint64_t requested_target_guid,
    const bool has_explicit_target) {
  std::uint64_t target_guid = ResolveSelectedActionTargetGuid(
      session, requested_target_guid, has_explicit_target);

  if (!has_explicit_target && target_guid != 0) {
    const auto *player = session.objects().GetActivePlayer();
    const auto *target_unit = session.objects().GetUnit(ObjectGuid(target_guid));
    if (player != nullptr && target_unit != nullptr &&
        player->Interaction().IsNeutralOrCivilian(*target_unit)) {
      target_guid = 0;
    }
  }

  if (target_guid == 0 && !has_explicit_target && state != nullptr) {
    if (auto *targeting = GetTargetingSystem(state); targeting != nullptr) {
      targeting->TabTarget(false, openwow::game::TargetFilter::kEnemy);
      target_guid = session.objects().GetTargetGuid().GetRawValue();
    }
  }

  return target_guid;
}

inline std::uint64_t ResolvePetAttackCommandTargetGuid(lua_State *state,
                                                       const WorldSession &session,
                                                       const std::uint64_t requested_target_guid,
                                                       const bool has_explicit_target) {
  std::uint64_t target_guid =
      ResolveSelectedActionTargetGuid(session, requested_target_guid, has_explicit_target);

  if (!has_explicit_target && target_guid != 0) {
    const auto *target_unit = session.objects().GetUnit(ObjectGuid(target_guid));
    if (target_unit != nullptr) {
      for (const auto raw_guid : session.pet().pet_guids()) {
        if (raw_guid == 0) {
          continue;
        }

        const auto *pet_unit = session.objects().GetUnit(ObjectGuid(raw_guid));
        if (pet_unit != nullptr &&
            pet_unit->Interaction().IsNeutralOrCivilian(*target_unit)) {
          target_guid = 0;
          break;
        }
      }
    }
  }

  if (target_guid == 0 && !has_explicit_target && state != nullptr) {
    if (auto *targeting = GetTargetingSystem(state); targeting != nullptr) {
      targeting->TabTarget(false, openwow::game::TargetFilter::kEnemy);
      target_guid = session.objects().GetTargetGuid().GetRawValue();
    }
  }

  return target_guid;
}

inline bool CanAnyPetIssueAttackCommand(const WorldSession &session,
                                        const std::uint64_t target_guid) {
  if (target_guid == 0) {
    return false;
  }

  const auto *target_unit = session.objects().GetUnit(ObjectGuid(target_guid));
  if (target_unit == nullptr) {
    return false;
  }

  for (const auto raw_guid : session.pet().pet_guids()) {
    if (raw_guid == 0) {
      continue;
    }

    const auto *pet_unit = session.objects().GetUnit(ObjectGuid(raw_guid));
    if (pet_unit == nullptr) {
      continue;
    }

    float interaction_range_sq = 0.0f;
    if (pet_unit->Interaction().GetInteractionRangeSquared(
            session, ObjectGuid(target_guid), 5, &interaction_range_sq) &&
        pet_unit->GetSquaredDistanceToPosition(target_unit->GetPosition()) > interaction_range_sq) {
      continue;
    }

    if (pet_unit->Interaction().CanAttackSpellTarget(*target_unit)) {
      return true;
    }
  }

  return false;
}

inline bool ExecutePetAction(lua_State *state, WorldSession &session,
                             const std::uint32_t action_data,
                             const std::uint64_t requested_target_guid,
                             const bool has_explicit_target) {
  const ObjectGuid pet_guid = GetPrimaryPetActionGuid(session);
  if (pet_guid.IsEmpty()) {
    return false;
  }

  const auto action_kind = static_cast<std::uint8_t>((action_data >> 24) & 0x3Fu);
  if (!CanUsePetActions(session, PetActionAvailabilityRequiresForceCheck(action_kind))) {
    return false;
  }

  const auto action_id = action_data & 0x00FFFFFFu;
  std::uint64_t target_guid =
      ResolveSelectedActionTargetGuid(session, requested_target_guid, has_explicit_target);

  switch (action_kind) {
  case 6:
    if (action_id <= static_cast<std::uint32_t>(openwow::game::PetReactState::kAggressive)) {
      session.pet().SetLocalReactState(static_cast<openwow::game::PetReactState>(action_id));
      ScriptEventDispatch::Get().FirePetBarUpdate();
    }
    break;
  case 7:
    if (action_id <= static_cast<std::uint32_t>(openwow::game::PetCommandState::kFollow)) {
      session.pet().SetLocalCommandState(static_cast<openwow::game::PetCommandState>(action_id));
      ScriptEventDispatch::Get().FirePetBarUpdate();
      break;
    }

    if (action_id == static_cast<std::uint32_t>(openwow::game::PetCommandState::kAttack)) {
      target_guid = ResolvePetAttackCommandTargetGuid(state, session, requested_target_guid,
                                                      has_explicit_target);
      if (!CanAnyPetIssueAttackCommand(session, target_guid)) {
        return false;
      }

      session.pet().SetAttackCommandActive(true);
      ScriptEventDispatch::Get().FirePetBarUpdate();
    }
    break;
  default:
    break;
  }

  session.interaction().SendPetAction(pet_guid.GetRawValue(), action_data, target_guid);
  return true;
}

inline void DispatchResolvedPlayerSpell(lua_State *state, WorldSession &session,
                                        const std::uint32_t spell_id, std::uint64_t target_guid) {
  constexpr std::uint32_t kShootWandSpellId = 5019;

  if (spell_id == 0) {
    return;
  }

  if (SpellHasAttackActionEffect(state, spell_id)) {
    if (target_guid == 0) {
      target_guid = session.objects().GetTargetGuid().GetRawValue();
    }

    if (target_guid == 0) {
      return;
    }

    if (state != nullptr) {
      if (auto *targeting = GetTargetingSystem(state); targeting != nullptr) {
        targeting->StartAttack(target_guid, false, false, spell_id);
        return;
      }
    }

    session.interaction().SendAttackSwing(target_guid);
    return;
  }

  if (spell_id == kShootWandSpellId) {
    auto& casts = session.spells();
    if (casts.CastSpell(session, spell_id, target_guid) ==
        SpellCastResult::kSuccess) {
      casts.StartAutoRepeat(spell_id);
    }
    return;
  }

  session.spells().CastSpell(session, spell_id, target_guid);
}

inline bool DispatchResolvedPetSpell(WorldSession &session, const std::uint32_t spell_id,
                                     std::uint64_t target_guid) {
  const auto spell_entry = std::find_if(
      session.pet().pet_bar().spells.begin(), session.pet().pet_bar().spells.end(),
      [spell_id](const PetActionButton &spell) { return spell.ActionId() == spell_id; });
  if (spell_entry == session.pet().pet_bar().spells.end()) {
    return false;
  }

  const ObjectGuid pet_guid = GetPrimaryPetActionGuid(session);
  if (pet_guid.IsEmpty()) {
    return false;
  }

  const auto action_kind = static_cast<std::uint8_t>((spell_entry->raw >> 24) & 0x3Fu);
  if (!CanUsePetActions(session, PetActionAvailabilityRequiresForceCheck(action_kind))) {
    return false;
  }

  if (target_guid == 0) {
    target_guid = session.objects().GetTargetGuid().GetRawValue();
  }

  session.interaction().SendPetAction(pet_guid.GetRawValue(), spell_entry->raw, target_guid);
  return true;
}

inline void DispatchResolvedSpellNameQuery(lua_State *state, const ResolvedSpellNameQuery &query,
                                            std::uint64_t target_guid) {
  auto *session = state != nullptr ? GetWorldSession(state) : nullptr;
  if (session == nullptr) {
    return;
  }

  if (query.from_pet_book) {
    (void)DispatchResolvedPetSpell(*session, query.spell_id, target_guid);
    return;
  }

  DispatchResolvedPlayerSpell(state, *session, query.spell_id, target_guid);
}

inline bool ApplyPetSpellAutocastMutation(WorldSession &session, const std::uint32_t spell_id,
                                          const std::optional<bool> requested_enabled) {
  if (!GameUI_CanPerformProtectedAction(protected_action_kind::kSpellCast) || !CanUsePetAutocastActions(session)) {
    return false;
  }

  const auto enabled = session.pet().SetSpellAutocastStateBySpellId(spell_id, requested_enabled);
  if (!enabled.has_value()) {
    return false;
  }

  session.interaction().SendTogglePetAutocast(spell_id, *enabled);
  ScriptEventDispatch::Get().FirePetBarUpdate();

  return true;
}

enum class PowerType : std::int32_t {
  kMana = 0,
  kRage = 1,
  kFocus = 2,
  kEnergy = 3,
  kHappiness = 4,
  kRunes = 5,
  kRunicPower = 6,
};

inline std::uint16_t PowerFieldIndex(PowerType pt) {
  auto idx = static_cast<int>(pt);
  if (idx < 0 || idx > 6)
    return UNIT_FIELD_POWER1;
  return static_cast<std::uint16_t>(UNIT_FIELD_POWER1 + idx);
}

inline std::uint16_t MaxPowerFieldIndex(PowerType pt) {
  auto idx = static_cast<int>(pt);
  if (idx < 0 || idx > 6)
    return UNIT_FIELD_MAXPOWER1;
  return static_cast<std::uint16_t>(UNIT_FIELD_MAXPOWER1 + idx);
}

inline PowerType GetUnitPowerType(const openwow::game::WorldObject *unit) {
  if (!unit)
    return PowerType::kMana;
  std::uint32_t bytes0 = unit->GetUInt32(UNIT_FIELD_BYTES_0);
  auto pt = static_cast<PowerType>((bytes0 >> 24) & 0xFF);
  return pt;
}

inline std::uint8_t GetUnitRace(const openwow::game::WorldObject *unit) {
  if (!unit)
    return 0;
  return static_cast<std::uint8_t>(unit->GetUInt32(UNIT_FIELD_BYTES_0) & 0xFF);
}

inline std::uint8_t GetUnitClass(const openwow::game::WorldObject *unit) {
  if (!unit)
    return 0;
  return static_cast<std::uint8_t>((unit->GetUInt32(UNIT_FIELD_BYTES_0) >> 8) & 0xFF);
}

inline std::uint8_t GetUnitGender(const openwow::game::WorldObject *unit) {
  if (!unit)
    return 0;
  return static_cast<std::uint8_t>((unit->GetUInt32(UNIT_FIELD_BYTES_0) >> 16) & 0xFF);
}

inline const char *RaceName(std::uint8_t race_id) {
  switch (race_id) {
  case 1:
    return "Human";
  case 2:
    return "Orc";
  case 3:
    return "Dwarf";
  case 4:
    return "Night Elf";
  case 5:
    return "Undead";
  case 6:
    return "Tauren";
  case 7:
    return "Gnome";
  case 8:
    return "Troll";
  case 10:
    return "Blood Elf";
  case 11:
    return "Draenei";
  default:
    return "Unknown";
  }
}

inline const char *RaceFileToken(std::uint8_t race_id) {
  switch (race_id) {
  case 1:
    return "Human";
  case 2:
    return "Orc";
  case 3:
    return "Dwarf";
  case 4:
    return "NightElf";
  case 5:
    return "Scourge";
  case 6:
    return "Tauren";
  case 7:
    return "Gnome";
  case 8:
    return "Troll";
  case 10:
    return "BloodElf";
  case 11:
    return "Draenei";
  default:
    return nullptr;
  }
}

inline std::string_view LookupRaceDisplayName(lua_State *L, std::uint8_t race_id,
                                              std::uint8_t gender_id) {
  if (const auto *dbc = GetDbcLoader(L); dbc != nullptr) {
    if (const auto *race = dbc->chr_races().LookupEntry(race_id); race != nullptr) {
      const std::string_view name = race->DisplayNameForSex(gender_id);
      if (!name.empty()) {
        return name;
      }
    }
  }

  return RaceName(race_id);
}

inline std::string_view LookupRaceFileToken(lua_State *L, std::uint8_t race_id) {
  if (const auto *dbc = GetDbcLoader(L); dbc != nullptr) {
    if (const auto *race = dbc->chr_races().LookupEntry(race_id);
        race != nullptr && !race->client_file_string.empty()) {
      return race->client_file_string;
    }
  }

  const char *fallback = RaceFileToken(race_id);
  return fallback != nullptr ? std::string_view(fallback) : std::string_view{};
}

inline const openwow::data::dbc::ChrClassesEntry *LookupChrClassEntry(lua_State *L,
                                                                      std::uint8_t class_id) {
  const auto *dbc = GetDbcLoader(L);
  if (dbc == nullptr) {
    return nullptr;
  }

  return dbc->chr_classes().LookupEntry(class_id);
}

inline const char *TryClassName(std::uint8_t class_id) {
  switch (class_id) {
  case 1:
    return "Warrior";
  case 2:
    return "Paladin";
  case 3:
    return "Hunter";
  case 4:
    return "Rogue";
  case 5:
    return "Priest";
  case 6:
    return "Death Knight";
  case 7:
    return "Shaman";
  case 8:
    return "Mage";
  case 9:
    return "Warlock";
  case 11:
    return "Druid";
  default:
    return nullptr;
  }
}

inline const char *ClassName(std::uint8_t class_id) {
  if (const char *name = TryClassName(class_id); name != nullptr) {
    return name;
  }

  return "Unknown";
}

inline const char *ClassFileToken(std::uint8_t class_id) {
  switch (class_id) {
  case 1:
    return "WARRIOR";
  case 2:
    return "PALADIN";
  case 3:
    return "HUNTER";
  case 4:
    return "ROGUE";
  case 5:
    return "PRIEST";
  case 6:
    return "DEATHKNIGHT";
  case 7:
    return "SHAMAN";
  case 8:
    return "MAGE";
  case 9:
    return "WARLOCK";
  case 11:
    return "DRUID";
  default:
    return nullptr;
  }
}

inline std::string_view LookupClassDisplayName(lua_State *L, std::uint8_t class_id,
                                               std::uint8_t gender_id) {
  if (const auto *entry = LookupChrClassEntry(L, class_id); entry != nullptr) {
    const std::string_view display_name = entry->DisplayNameForSex(gender_id);
    if (!display_name.empty()) {
      return display_name;
    }
    if (!entry->name.empty()) {
      return entry->name;
    }
  }

  if (const char *fallback = TryClassName(class_id); fallback != nullptr) {
    return std::string_view(fallback);
  }

  return std::string_view{};
}

inline std::string_view LookupClassBaseName(lua_State *L, std::uint8_t class_id) {
  if (const auto *entry = LookupChrClassEntry(L, class_id);
      entry != nullptr && !entry->name.empty()) {
    return entry->name;
  }

  if (const char *fallback = TryClassName(class_id); fallback != nullptr) {
    return std::string_view(fallback);
  }

  return std::string_view{};
}

inline std::string_view LookupClassFileToken(lua_State *L, std::uint8_t class_id) {
  if (const auto *entry = LookupChrClassEntry(L, class_id);
      entry != nullptr && !entry->client_file_string.empty()) {
    return entry->client_file_string;
  }

  if (const char *fallback = ClassFileToken(class_id); fallback != nullptr) {
    return std::string_view(fallback);
  }

  return std::string_view{};
}

inline bool UnitIsGhost(const openwow::game::WorldObject *unit) {
  if (!unit || !unit->IsPlayer())
    return false;

  constexpr std::uint32_t kPlayerFlagsGhost = 0x10;
  return (unit->GetUInt32(PLAYER_FLAGS) & kPlayerFlagsGhost) != 0;
}

inline bool UnitIsDeadOrGhost(const openwow::game::WorldObject *unit) {
  if (!unit)
    return true;
  if (unit->GetHealth() == 0)
    return true;
  return UnitIsGhost(unit);
}

inline bool UnitIsPlayer(const openwow::game::WorldObject *unit) {
  if (!unit)
    return false;
  return unit->IsPlayer();
}

inline openwow::game::ReactionType GetUnitReaction(
    const openwow::game::WorldObject *unit,
    const openwow::game::WorldObject *other,
    const openwow::data::dbc::DbcLoader *dbc) {
  if (!unit || !other)
    return openwow::game::ReactionType::kNeutral;

  if (const auto *lhs = ResolveUnitObject(unit), *rhs = ResolveUnitObject(other);
      lhs != nullptr && rhs != nullptr) {
    return lhs->Interaction().GetReaction(*rhs);
  }

  if (unit->GetGuid() == other->GetGuid()) {
    return openwow::game::ReactionType::kFriendly;
  }

  if (dbc == nullptr) {
    return openwow::game::ReactionType::kNeutral;
  }

  auto ft_a = unit->GetUInt32(UNIT_FIELD_FACTIONTEMPLATE);
  auto ft_b = other->GetUInt32(UNIT_FIELD_FACTIONTEMPLATE);

  return openwow::data::dbc::ComputeFactionReaction(ft_a, ft_b,
                                                     dbc->faction_template());
}

inline bool UnitIsFriend(const openwow::game::WorldObject *unit,
                         const openwow::game::WorldObject *other,
                         const openwow::data::dbc::DbcLoader *dbc) {
  if (const auto *lhs = ResolveUnitObject(unit), *rhs = ResolveUnitObject(other);
      lhs != nullptr && rhs != nullptr) {
    return lhs->Interaction().IsNeutralOrCivilian(*rhs);
  }

  return GetUnitReaction(unit, other, dbc) >= openwow::game::ReactionType::kFriendly;
}

inline void PushReactionColor(lua_State *L, openwow::game::ReactionType reaction) {
  using RT = openwow::game::ReactionType;
  switch (reaction) {
  case RT::kHated:
  case RT::kHostile:

    lua_pushnumber(L, 1.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 1.0);
    break;
  case RT::kUnfriendly:

    lua_pushnumber(L, 1.0);
    lua_pushnumber(L, 0.5);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 1.0);
    break;
  case RT::kNeutral:

    lua_pushnumber(L, 1.0);
    lua_pushnumber(L, 1.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 1.0);
    break;
  default:

    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 1.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 1.0);
    break;
  }
}

inline bool UnitIsEnemy(const openwow::game::WorldObject *unit,
                        const openwow::game::WorldObject *other,
                        const openwow::data::dbc::DbcLoader *dbc) {
  if (const auto *lhs = ResolveUnitObject(unit), *rhs = ResolveUnitObject(other);
      lhs != nullptr && rhs != nullptr) {
    return lhs->Interaction().IsHostileTo(*rhs);
  }

  return GetUnitReaction(unit, other, dbc) <= openwow::game::ReactionType::kHostile;
}

inline bool UnitCanAttack(const openwow::game::WorldObject *unit,
                          const openwow::game::WorldObject *other,
                          const openwow::data::dbc::DbcLoader *dbc) {
  if (!unit || !other)
    return false;
  if (unit->GetGuid() == other->GetGuid())
    return false;
  auto reaction = GetUnitReaction(unit, other, dbc);
  return reaction <= openwow::game::ReactionType::kUnfriendly;
}

inline bool UnitCanCooperate(const openwow::game::WorldObject *unit,
                             const openwow::game::WorldObject *other,
                             const openwow::data::dbc::DbcLoader *dbc) {
  if (!unit || !other)
    return false;
  auto reaction = GetUnitReaction(unit, other, dbc);
  return reaction >= openwow::game::ReactionType::kNeutral;
}

inline bool UnitCanAssist(const openwow::game::WorldObject *unit,
                          const openwow::game::WorldObject *other,
                          const openwow::data::dbc::DbcLoader *dbc) {
  if (!unit || !other)
    return false;
  auto reaction = GetUnitReaction(unit, other, dbc);
  if (reaction < openwow::game::ReactionType::kFriendly)
    return false;

  if (other->GetHealth() == 0)
    return false;
  return true;
}

}

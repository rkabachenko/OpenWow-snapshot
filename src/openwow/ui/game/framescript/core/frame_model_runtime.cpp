#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/game/framescript/core/frame_color_runtime.h"
#include "openwow/ui/game/runtime/texture_render_state_source.h"
#include "openwow/ui/game/framescript/core/frame_model_runtime.h"
#include "openwow/ui/game/framescript/core/frame_model_lifecycle.h"
#include "openwow/ui/game/framescript/xml/frame_xml_region_materializer.h"
#include "openwow/ui/game/framescript/core/frame_script_object_runtime.h"

#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/game/dressup_model.h"
#include "openwow/game/inventory/model/item_instance.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/inventory/items/item_link_parser.h"
#include "openwow/game/inventory/equipment/item_equip_check.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgitem.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/foundation/text/ascii.h"

#include <lua.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openwow::ui::game::frame_api {

std::array<std::uint32_t, openwow::game::kTabardNumAxes>
ReadTabardDesignValues(lua_State *L, int frame_index) {
  frame_index = lua_absindex(L, frame_index);
  std::array<std::uint32_t, openwow::game::kTabardNumAxes> values{};
  for (std::size_t i = 0; i < kTabardDesignFieldNames.size(); ++i) {
    lua_getfield(L, frame_index, kTabardDesignFieldNames[i]);
    if (lua_isnumber(L, -1) != 0) {
      values[i] = static_cast<std::uint32_t>(lua_tointeger(L, -1));
    }
    lua_pop(L, 1);
  }
  return values;
}

void WriteTabardDesignValues(
    lua_State *L,
    int frame_index,
    const std::array<std::uint32_t, openwow::game::kTabardNumAxes> &values) {
  frame_index = lua_absindex(L, frame_index);
  for (std::size_t i = 0; i < kTabardDesignFieldNames.size(); ++i) {
    lua_pushinteger(L, static_cast<lua_Integer>(values[i]));
    lua_setfield(L, frame_index, kTabardDesignFieldNames[i]);
  }
}

void WriteTabardDesignValuesAndRefreshPreview(
    lua_State *L,
    int frame_index,
    const std::array<std::uint32_t, openwow::game::kTabardNumAxes> &values) {
  WriteTabardDesignValues(L, frame_index, values);
  openwow::game::TabardFrame_RefreshActivePlayerPreview(
      openwow::ui::game::detail::GetWorldSession(L));
}

void InitializeTabardDesignValues(lua_State *L, int frame_index) {
  auto values = ReadTabardDesignValues(L, frame_index);
  if (!openwow::game::TabardFrame_InitializeColors(
          openwow::ui::game::detail::GetWorldSession(L), values.data())) {
    return;
  }

  WriteTabardDesignValuesAndRefreshPreview(L, frame_index, values);
}

int GetValidatedTabardTextureArg(lua_State *L, const char *method_name) {
  const int self_idx = ValidateFrameObjectSelf(L, "TabardModel");
  if (lua_istable(L, 2) == 0) {
    luaL_error(L, "Usage: %s:%s(texture)", lua_adapter::ScriptObjectDisplayName(L, self_idx),
               method_name);
  }

  if (!openwow::ui::game::detail::HasLuaScriptObjectThis(L, 2)) {
    luaL_error(L, "%s:%s(): Couldn't find 'this' in texture object",
               lua_adapter::ScriptObjectDisplayName(L, self_idx), method_name);
  }

  const char *type_name = openwow::ui::BorrowRawLuaStringField(L, 2, "__ow_type");
  if (type_name == nullptr || std::strcmp(type_name, "Texture") != 0) {
    luaL_error(L, "%s:%s(): Wrong object type, expected texture",
               lua_adapter::ScriptObjectDisplayName(L, self_idx), method_name);
  }

  return lua_absindex(L, 2);
}

void SetTabardEmblemRenderTarget(
    lua_State *L,
    int texture_index,
    const openwow::game::TabardEmblemRenderTargetDescriptor &descriptor) {
  texture_index = lua_absindex(L, texture_index);
  runtime::SetTextureRenderStateString(
      L, texture_index, runtime::TextureRenderStateField::kTexture,
      std::string_view(descriptor.sourceTexturePath));
  runtime::SetTextureRenderStateTabardEmblem(
      L, texture_index, descriptor.sourceTexturePath,
      descriptor.renderTargetName,
      static_cast<std::int32_t>(descriptor.width),
      static_cast<std::int32_t>(descriptor.height),
      static_cast<std::int32_t>(descriptor.pitch), descriptor.forceWhiteRgb,
      descriptor.copySourceAlpha);
}

int TruncateLuaIntegerArgument(lua_State *L, int index) {
  return openwow::ui::game::detail::TruncateLuaNumberToSseI32(
      lua_tonumber(L, index));
}

bool IsDressUpPreviewFrame(lua_State *L, int frame_index) {
  frame_index = lua_absindex(L, frame_index);
  lua_getfield(L, frame_index, "__ow_type");
  const char *frame_type = lua_tostring(L, -1);
  const bool is_dressup = frame_type != nullptr && (std::strcmp(frame_type, "DressUpModel") == 0 ||
                                                    std::strcmp(frame_type, "TabardModel") == 0);
  lua_pop(L, 1);
  return is_dressup;
}

void StoreBoundUnitGuid(lua_State *L, int frame_index, const openwow::game::ObjectGuid guid) {
  frame_index = lua_absindex(L, frame_index);
  const std::uint64_t raw_guid = guid.GetRawValue();

  lua_pushinteger(L, static_cast<lua_Integer>(raw_guid & 0xFFFFFFFFu));
  lua_setfield(L, frame_index, "__ow_model_unit_guid_lo");
  lua_pushinteger(L, static_cast<lua_Integer>((raw_guid >> 32) & 0xFFFFFFFFu));
  lua_setfield(L, frame_index, "__ow_model_unit_guid_hi");
}

void StoreBoundCreatureEntry(lua_State *L, int frame_index, const std::uint32_t creature_entry) {
  frame_index = lua_absindex(L, frame_index);
  lua_pushinteger(L, static_cast<lua_Integer>(creature_entry));
  lua_setfield(L, frame_index, "__ow_model_creature");
}

void StoreBoundUnitDisplayId(lua_State *L, int frame_index,
                             const std::uint32_t display_id) {
  frame_index = lua_absindex(L, frame_index);
  lua_pushinteger(L, static_cast<lua_Integer>(display_id));
  lua_setfield(L, frame_index, "__ow_model_display");
}

void StoreBoundUnitSequence(lua_State *L, int frame_index,
                            const std::uint32_t sequence_id) {
  frame_index = lua_absindex(L, frame_index);
  lua_pushinteger(L, static_cast<lua_Integer>(sequence_id));
  lua_setfield(L, frame_index, "__ow_model_sequence");
}

void ClearBoundUnitSequence(lua_State *L, int frame_index) {
  frame_index = lua_absindex(L, frame_index);
  lua_pushnil(L);
  lua_setfield(L, frame_index, "__ow_model_sequence");
  lua_pushnil(L);
  lua_setfield(L, frame_index, "__ow_model_sequence_time");
}

constexpr std::uint32_t kSimpleModelSequenceCount = 0x1FAu;

std::uint32_t ClampLuaNumberToClientU32(lua_State *L, const int index) {
  const lua_Number value = lua_tonumber(L, index);
  if (!std::isfinite(value) || value <= 0.0) {
    return 0u;
  }

  constexpr auto max_u32 =
      static_cast<lua_Number>(std::numeric_limits<std::uint32_t>::max());
  if (value >= max_u32) {
    return std::numeric_limits<std::uint32_t>::max();
  }

  return static_cast<std::uint32_t>(std::trunc(value));
}

int ClampModelIndexForRuntime(const std::uint32_t value) {
  if (value > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
    return std::numeric_limits<int>::max();
  }
  return static_cast<int>(value);
}

void StoreModelCamera(lua_State *L, int frame_index,
                      const std::uint32_t camera_index) {
  frame_index = lua_absindex(L, frame_index);
  lua_pushinteger(L,
                  static_cast<lua_Integer>(ClampModelIndexForRuntime(camera_index)));
  lua_setfield(L, frame_index, "__ow_model_camera");
}

void StoreModelSequenceTime(lua_State *L,
                            int frame_index,
                            const std::uint32_t sequence_id,
                            const std::uint32_t time_ms) {
  frame_index = lua_absindex(L, frame_index);
  StoreBoundUnitSequence(L, frame_index, sequence_id);
  lua_pushinteger(L, static_cast<lua_Integer>(time_ms));
  lua_setfield(L, frame_index, "__ow_model_sequence_time");
}

int ValidateAndReadModelSequence(lua_State *L, const char *method_name) {
  if (lua_isnumber(L, 2) == 0) {
    return luaL_error(L, "Usage: %s:%s(sequence)",
                      lua_adapter::ScriptObjectDisplayName(L, 1), method_name);
  }

  const std::uint32_t sequence = ClampLuaNumberToClientU32(L, 2);
  if (sequence >= kSimpleModelSequenceCount) {
    return luaL_error(L, "Error: %s:%s(sequence) exceeds valid range of 0 - %d",
                      lua_adapter::ScriptObjectDisplayName(L, 1), method_name,
                      static_cast<int>(kSimpleModelSequenceCount));
  }
  return ClampModelIndexForRuntime(sequence);
}

void StoreBoundUnitAlpha(lua_State *L, int frame_index,
                         const std::uint8_t alpha_byte) {
  frame_index = lua_absindex(L, frame_index);
  lua_pushnumber(L, openwow::ui::game::NormalizeFrameAlphaByte(alpha_byte));
  lua_setfield(L, frame_index, "__ow_alpha");
}

bool TryLoadLuaUnsignedField(lua_State *L, int table_index,
                             const char *field_name,
                             std::uint32_t &value) {
  table_index = lua_absindex(L, table_index);
  lua_getfield(L, table_index, field_name);
  if (lua_isnumber(L, -1) == 0) {
    lua_pop(L, 1);
    return false;
  }

  value = static_cast<std::uint32_t>(lua_tointeger(L, -1));
  lua_pop(L, 1);
  return true;
}

void StoreOptionalLuaUnsignedField(lua_State *L, int table_index,
                                   const char *field_name,
                                   const std::optional<std::uint32_t> value) {
  table_index = lua_absindex(L, table_index);
  if (!value.has_value()) {
    lua_pushnil(L);
    lua_setfield(L, table_index, field_name);
    return;
  }

  lua_pushinteger(L, static_cast<lua_Integer>(*value));
  lua_setfield(L, table_index, field_name);
}

openwow::game::ObjectGuid LoadBoundUnitGuid(lua_State *L, int frame_index) {
  frame_index = lua_absindex(L, frame_index);

  lua_getfield(L, frame_index, "__ow_model_unit_guid_lo");
  const auto low = static_cast<std::uint32_t>(lua_tointeger(L, -1));
  lua_pop(L, 1);

  lua_getfield(L, frame_index, "__ow_model_unit_guid_hi");
  const auto high = static_cast<std::uint32_t>(lua_tointeger(L, -1));
  lua_pop(L, 1);

  return openwow::game::ObjectGuid((static_cast<std::uint64_t>(high) << 32) | low);
}

void ClearBoundCreatureBinding(lua_State *L, int frame_index) {
  StoreBoundCreatureEntry(L, frame_index, 0);
}

void ClearCharacterModelHiddenState(lua_State *L, int frame_index) {
  frame_index = lua_absindex(L, frame_index);
  lua_pushnil(L);
  lua_setfield(L, frame_index, "__ow_hidden_model_display");
  lua_pushnil(L);
  lua_setfield(L, frame_index, "__ow_hidden_model_sequence");
}

void SuspendCharacterModelForHide(lua_State *L, int frame_index) {
  frame_index = lua_absindex(L, frame_index);

  std::uint32_t active_display = 0;
  if (TryLoadLuaUnsignedField(L, frame_index, "__ow_model_display",
                              active_display) &&
      active_display != 0) {
    StoreOptionalLuaUnsignedField(L, frame_index, "__ow_hidden_model_display",
                                  active_display);
  } else {
    StoreOptionalLuaUnsignedField(L, frame_index, "__ow_hidden_model_display",
                                  std::nullopt);
  }

  std::uint32_t sequence_id = 0;
  if (TryLoadLuaUnsignedField(L, frame_index, "__ow_model_sequence",
                              sequence_id)) {
    StoreOptionalLuaUnsignedField(L, frame_index, "__ow_hidden_model_sequence",
                                  sequence_id);
  } else {
    StoreOptionalLuaUnsignedField(L, frame_index, "__ow_hidden_model_sequence",
                                  std::nullopt);
  }

  if (active_display != 0) {
    StoreBoundUnitDisplayId(L, frame_index, 0);
  }
}

void RefreshBoundCreatureModelState(lua_State *L, int frame_index) {
  frame_index = lua_absindex(L, frame_index);

  std::uint32_t creature_entry = 0;
  if (!TryLoadLuaUnsignedField(L, frame_index, "__ow_model_creature",
                               creature_entry) ||
      creature_entry == 0) {
    return;
  }

  auto *session = openwow::ui::game::detail::GetWorldSession(L);
  if (session == nullptr) {
    return;
  }

  const auto *creature_template =
      session->query_cache().GetCreatureTemplate(creature_entry);
  if (creature_template == nullptr) {
    return;
  }

  openwow::ui::game::detail::ApplyCreatureTemplateBindingToFrame(
      L, frame_index, *creature_template);
}

void RestoreCharacterModelAfterShow(lua_State *L, int frame_index) {
  frame_index = lua_absindex(L, frame_index);

  std::uint32_t active_display = 0;
  if (TryLoadLuaUnsignedField(L, frame_index, "__ow_model_display",
                              active_display) &&
      active_display != 0) {
    ClearCharacterModelHiddenState(L, frame_index);
    return;
  }

  const auto bound_guid = LoadBoundUnitGuid(L, frame_index);
  if (!bound_guid.IsEmpty()) {
    RefreshBoundUnitModelState(L, frame_index, bound_guid);
    ClearCharacterModelHiddenState(L, frame_index);
    return;
  }

  std::uint32_t creature_entry = 0;
  if (TryLoadLuaUnsignedField(L, frame_index, "__ow_model_creature",
                              creature_entry) &&
      creature_entry != 0) {
    RefreshBoundCreatureModelState(L, frame_index);
    ClearCharacterModelHiddenState(L, frame_index);
    return;
  }

  std::uint32_t cached_display = 0;
  if (TryLoadLuaUnsignedField(L, frame_index, "__ow_hidden_model_display",
                              cached_display) &&
      cached_display != 0) {
    StoreBoundUnitDisplayId(L, frame_index, cached_display);

    std::uint32_t cached_sequence = 0;
    if (TryLoadLuaUnsignedField(L, frame_index, "__ow_hidden_model_sequence",
                                cached_sequence)) {
      StoreBoundUnitSequence(L, frame_index, cached_sequence);
    }
  }

  ClearCharacterModelHiddenState(L, frame_index);
}

void ClearDressUpPreviewState(lua_State *L, int frame_index) {
  frame_index = lua_absindex(L, frame_index);
  lua_pushnil(L);
  lua_setfield(L, frame_index, "__ow_dressup_preview_slots");
  lua_pushnil(L);
  lua_setfield(L, frame_index, "__ow_dressup_preview_enchants");
  lua_pushnil(L);
  lua_setfield(L, frame_index, "__ow_dressup_mainhand_override");
  lua_pushnil(L);
  lua_setfield(L, frame_index, "__ow_dressup_offhand_override");
  lua_pushnil(L);
  lua_setfield(L, frame_index, "__ow_dressup_uses_guild_tabard");
}

static constexpr int kTryOnErrorNotEquippable = 21;
static constexpr std::uintptr_t kDressUpItemTemplateCallbackFunctionId =
    0x004ffdd0u;
static constexpr char kDressUpAsyncCookieField[] =
    "__ow_dressup_async_cookie";
static constexpr std::size_t kMaximumDressUpRedirectDepth = 16;

struct PendingDressUpFrame {
  int frame_ref{LUA_NOREF};
  std::unordered_set<std::uint32_t> item_entries;
};

struct PendingDressUpState {
  std::unordered_map<std::uint32_t, PendingDressUpFrame> frames;
  std::shared_ptr<const std::uint8_t> callback_lifetime{
      std::make_shared<const std::uint8_t>(0)};
};

std::unordered_map<lua_State *, PendingDressUpState> s_pending_dressup_frames;
std::uint32_t s_next_pending_dressup_cookie{1};

bool PendingDressUpCookieInUse(const std::uint32_t cookie) {
  return std::any_of(
      s_pending_dressup_frames.begin(), s_pending_dressup_frames.end(),
      [cookie](const auto &entry) {
        return entry.second.frames.contains(cookie);
      });
}

std::optional<openwow::game::ItemTemplate> LookupDressUpItemTemplate(
    lua_State *L, const std::uint32_t item_entry) {
  using namespace openwow::game;

  if (item_entry == 0) {
    return std::nullopt;
  }
  auto *session = openwow::ui::game::detail::GetWorldSession(L);
  if (session == nullptr) {
    return std::nullopt;
  }
  if (auto item = session->item_definitions().GetItemSnapshot(item_entry)) {
    return item;
  }
  const auto *query_item =
      session->query_cache().GetItemTemplate(item_entry);
  if (query_item == nullptr) {
    return std::nullopt;
  }

  session->item_definitions().CacheItem(*query_item);
  return session->item_definitions().GetItemSnapshot(item_entry);
}

void SetDressUpArmorPreview(lua_State *L, int frame_index,
                            const int model_slot,
                            const std::uint32_t display_id,
                            const std::int32_t enchant_visual) {
  frame_index = lua_absindex(L, frame_index);
  const auto preview_slot = static_cast<lua_Integer>(model_slot + 1);

  const auto set_slot = [&](const char *field_name, const lua_Integer value) {
    lua_getfield(L, frame_index, field_name);
    if (lua_istable(L, -1) == 0) {
      lua_pop(L, 1);
      lua_newtable(L);
      lua_pushvalue(L, -1);
      lua_setfield(L, frame_index, field_name);
    }
    lua_pushinteger(L, value);
    lua_seti(L, -2, preview_slot);
    lua_pop(L, 1);
  };

  set_slot("__ow_dressup_preview_slots",
           static_cast<lua_Integer>(display_id));
  set_slot("__ow_dressup_preview_enchants",
           static_cast<lua_Integer>(enchant_visual));
}

void ClearDressUpWeaponPreview(lua_State *L, int frame_index,
                               const int model_slot) {
  frame_index = lua_absindex(L, frame_index);
  lua_pushnil(L);
  lua_setfield(L, frame_index,
               model_slot == openwow::game::kDressUpSlotOffhand
                   ? "__ow_dressup_offhand_override"
                   : "__ow_dressup_mainhand_override");
}

void StoreDressUpWeaponPreview(
    lua_State *L, int frame_index, const int model_slot,
    const openwow::game::ItemTemplate &item,
    const std::uint32_t display_id, const std::int32_t enchant_visual) {
  frame_index = lua_absindex(L, frame_index);
  lua_newtable(L);
  lua_pushinteger(L, static_cast<lua_Integer>(display_id));
  lua_setfield(L, -2, "itemDisplayId");
  lua_pushinteger(L, static_cast<lua_Integer>(enchant_visual));
  lua_setfield(L, -2, "enchantVisual");
  lua_pushinteger(L, static_cast<lua_Integer>(item.item_class));
  lua_setfield(L, -2, "itemClass");
  lua_pushinteger(L, static_cast<lua_Integer>(item.subclass));
  lua_setfield(L, -2, "subclass");
  lua_pushinteger(L, static_cast<lua_Integer>(item.inventory_type));
  lua_setfield(L, -2, "inventoryType");
  lua_pushinteger(L, static_cast<lua_Integer>(item.sheath));
  lua_setfield(L, -2, "sheath");
  lua_setfield(L, frame_index,
               model_slot == openwow::game::kDressUpSlotOffhand
                   ? "__ow_dressup_offhand_override"
                   : "__ow_dressup_mainhand_override");
}

openwow::game::DressUpWeaponOverrideSlot ReadDressUpWeaponPreview(
    lua_State *L, int frame_index, const char *field_name) {
  openwow::game::DressUpWeaponOverrideSlot result{};
  frame_index = lua_absindex(L, frame_index);
  lua_getfield(L, frame_index, field_name);
  if (lua_istable(L, -1) != 0) {
    std::uint32_t value = 0;
    if (TryLoadLuaUnsignedField(L, -1, "itemClass", value)) {
      result.item_class = static_cast<std::uint8_t>(value);
    }
    if (TryLoadLuaUnsignedField(L, -1, "subclass", value)) {
      result.subclass = static_cast<std::uint8_t>(value);
    }
    if (TryLoadLuaUnsignedField(L, -1, "inventoryType", value) ||
        TryLoadLuaUnsignedField(L, -1, "invType", value)) {
      result.inventory_type = static_cast<std::uint8_t>(value);
    }
    if (TryLoadLuaUnsignedField(L, -1, "sheath", value)) {
      result.sheath = static_cast<std::uint8_t>(value);
    }
  }
  lua_pop(L, 1);
  return result;
}

bool PendingDressUpFrameMatches(lua_State *L, const PendingDressUpFrame &owner,
                                int frame_index) {
  frame_index = lua_absindex(L, frame_index);
  lua_rawgeti(L, LUA_REGISTRYINDEX, owner.frame_ref);
  const bool matches = lua_rawequal(L, -1, frame_index) != 0;
  lua_pop(L, 1);
  return matches;
}

std::uint32_t EnsurePendingDressUpFrame(lua_State *L, int frame_index) {
  frame_index = lua_absindex(L, frame_index);
  auto &state = s_pending_dressup_frames[L];

  std::uint32_t cookie = 0;
  lua_getfield(L, frame_index, kDressUpAsyncCookieField);
  if (lua_isnumber(L, -1) != 0) {
    const auto candidate = lua_tointeger(L, -1);
    if (candidate > 0 &&
        static_cast<std::uint64_t>(candidate) <=
            std::numeric_limits<std::uint32_t>::max()) {
      cookie = static_cast<std::uint32_t>(candidate);
    }
  }
  lua_pop(L, 1);

  if (cookie != 0) {
    const auto existing = state.frames.find(cookie);
    if (existing != state.frames.end() &&
        PendingDressUpFrameMatches(L, existing->second, frame_index)) {
      return cookie;
    }
  }

  do {
    cookie = s_next_pending_dressup_cookie++;
    if (s_next_pending_dressup_cookie == 0) {
      s_next_pending_dressup_cookie = 1;
    }
  } while (cookie == 0 || PendingDressUpCookieInUse(cookie));

  lua_pushvalue(L, frame_index);
  state.frames.emplace(
      cookie,
      PendingDressUpFrame{.frame_ref = luaL_ref(L, LUA_REGISTRYINDEX)});
  lua_pushinteger(L, static_cast<lua_Integer>(cookie));
  lua_setfield(L, frame_index, kDressUpAsyncCookieField);
  return cookie;
}

bool TakePendingDressUpFrame(lua_State *L, const std::uint32_t cookie,
                             const std::uint32_t item_entry) {
  const auto state_it = s_pending_dressup_frames.find(L);
  if (state_it == s_pending_dressup_frames.end()) {
    return false;
  }
  auto owner_it = state_it->second.frames.find(cookie);
  if (owner_it == state_it->second.frames.end() ||
      owner_it->second.item_entries.erase(item_entry) == 0) {
    return false;
  }

  lua_rawgeti(L, LUA_REGISTRYINDEX, owner_it->second.frame_ref);
  const bool has_frame = lua_istable(L, -1) != 0;
  if (owner_it->second.item_entries.empty()) {
    luaL_unref(L, LUA_REGISTRYINDEX, owner_it->second.frame_ref);
    state_it->second.frames.erase(owner_it);
    if (state_it->second.frames.empty()) {
      s_pending_dressup_frames.erase(state_it);
    }
  }
  if (!has_frame) {
    lua_pop(L, 1);
  }
  return has_frame;
}

bool ResolveDressUpItem(lua_State *L, int frame_index,
                        std::uint32_t item_entry, std::int32_t enchant_visual,
                        std::int32_t force_slot, std::size_t redirect_depth);

void QueueDressUpItemTemplate(lua_State *L, int frame_index,
                              const std::uint32_t item_entry) {
  using openwow::game::QueryCache;

  auto *session = openwow::ui::game::detail::GetWorldSession(L);
  if (session == nullptr || item_entry == 0) {
    return;
  }

  const std::uint32_t cookie = EnsurePendingDressUpFrame(L, frame_index);
  auto &pending_state = s_pending_dressup_frames[L];
  auto &owner = pending_state.frames.at(cookie);
  if (!owner.item_entries.insert(item_entry).second) {
    return;
  }
  const std::weak_ptr<const std::uint8_t> callback_lifetime =
      pending_state.callback_lifetime;

  const QueryCache::CallbackKey callback_key(
      kDressUpItemTemplateCallbackFunctionId, cookie);
  const QueryCache::QueryRequestOptions options{
      .callback_key = callback_key,
      .dedupe_callbacks = true,
      .callback = [L, cookie, item_entry,
                   callback_lifetime](const bool success) {
        const auto lifetime_guard = callback_lifetime.lock();
        if (!lifetime_guard) {
          return;
        }
        if (!TakePendingDressUpFrame(L, cookie, item_entry)) {
          return;
        }
        const int frame_index = lua_absindex(L, -1);
        if (success) {
          ResolveDressUpItem(L, frame_index, item_entry, 0, -1, 0);
        }
        lua_pop(L, 1);
      },
  };

  const auto *already_cached =
      session->query_cache().GetOrRequestItemTemplate(item_entry, options);
  if (already_cached != nullptr) {
    session->query_cache().CancelItemTemplateCallback(item_entry, callback_key);
    session->item_definitions().CacheItem(*already_cached);
    if (TakePendingDressUpFrame(L, cookie, item_entry)) {
      const int pending_frame = lua_absindex(L, -1);
      ResolveDressUpItem(L, pending_frame, item_entry, 0, -1, 0);
      lua_pop(L, 1);
    }
  }
}

void CancelPendingDressUpItemTemplates(lua_State *L) {
  const auto state_it = s_pending_dressup_frames.find(L);
  if (state_it == s_pending_dressup_frames.end()) {
    return;
  }

  auto *session = openwow::ui::game::detail::GetWorldSession(L);
  const bool session_is_alive = session != nullptr;
  for (const auto &[cookie, owner] : state_it->second.frames) {
    if (session_is_alive) {
      session->query_cache().CancelItemTemplateCallbacks(
          openwow::game::QueryCache::CallbackKey(
              kDressUpItemTemplateCallbackFunctionId, cookie));
    }
    luaL_unref(L, LUA_REGISTRYINDEX, owner.frame_ref);
  }
  s_pending_dressup_frames.erase(state_it);
}

bool ResolveDressUpItem(lua_State *L, int frame_index,
                        const std::uint32_t item_entry,
                        const std::int32_t enchant_visual,
                        const std::int32_t force_slot,
                        const std::size_t redirect_depth) {
  using namespace openwow::game;

  frame_index = lua_absindex(L, frame_index);
  if (redirect_depth >= kMaximumDressUpRedirectDepth) {
    return false;
  }

  const auto item = LookupDressUpItemTemplate(L, item_entry);
  if (!item.has_value()) {
    QueueDressUpItemTemplate(L, frame_index, item_entry);
    return false;
  }

  auto *session = openwow::ui::game::detail::GetWorldSession(L);
  const auto *dbc = openwow::ui::game::detail::GetDbcLoader(L);

  DressUpTryOnItemInput input{};
  input.item_id = item_entry;
  input.enchant_id = enchant_visual;
  input.force_slot = force_slot;
  input.has_backing_model = true;
  input.has_character_model = true;
  input.bound_guid = LoadBoundUnitGuid(L, frame_index).GetRawValue();
  input.mainhand_override = ReadDressUpWeaponPreview(
      L, frame_index, "__ow_dressup_mainhand_override");
  input.offhand_override = ReadDressUpWeaponPreview(
      L, frame_index, "__ow_dressup_offhand_override");

  DressUpTryOnCallbacks callbacks{};
  callbacks.lookup_item = [&item](const std::uint32_t requested_entry) {
    return requested_entry == item->entry ? &*item : nullptr;
  };
  callbacks.lookup_spell = [dbc](const std::uint32_t spell_id)
      -> std::optional<DressUpSpellEffectInfo> {
    if (dbc == nullptr) {
      return std::nullopt;
    }
    const auto *spell = dbc->spell().LookupEntry(spell_id);
    if (spell == nullptr) {
      return std::nullopt;
    }
    return DressUpSpellEffectInfo{
        .effect_0 = spell->effect[0],
        .effect_trigger_spell_0 = spell->effect_trigger_spell[0],
        .effect_item_type_0 = spell->effect_item_type[0],
    };
  };
  callbacks.resolve_and_dispatch =
      [L, frame_index, redirect_depth](const std::uint32_t target_item,
                                       const std::int32_t target_enchant,
                                       const std::int32_t target_slot) {
        ResolveDressUpItem(L, frame_index, target_item, target_enchant,
                           target_slot, redirect_depth + 1);
      };
  callbacks.equip_armor_slot =
      [L, frame_index](const int slot, const std::uint32_t display_id,
                       const std::int32_t enchant_id) {
        SetDressUpArmorPreview(L, frame_index, slot, display_id, enchant_id);
      };
  callbacks.clear_weapon_slot =
      [L, frame_index](const int slot, const std::uint8_t, const bool) {
        ClearDressUpWeaponPreview(L, frame_index, slot);
      };
  callbacks.dispatch_weapon =
      [L, frame_index, &item](
          const DressUpTryOnCallbacks::WeaponDispatchParams &params) {
        StoreDressUpWeaponPreview(L, frame_index, params.slot, *item,
                                  params.display_id, params.enchant_id);
      };
  callbacks.get_display_info_flags = [dbc](const std::uint32_t display_id) {
    if (dbc == nullptr) {
      return std::uint32_t{0};
    }
    const auto *display = dbc->item_display_info().LookupEntry(display_id);
    return display != nullptr ? display->flags : std::uint32_t{0};
  };
  callbacks.apply_guild_tabard = [L, frame_index](const std::uint64_t) {
    lua_pushboolean(L, 1);
    lua_setfield(L, frame_index, "__ow_dressup_uses_guild_tabard");
  };
  callbacks.is_active_player = [session](const std::uint64_t bound_guid) {
    return session != nullptr && bound_guid != 0 &&
           session->objects().GetActivePlayerGuid().GetRawValue() == bound_guid;
  };
  callbacks.can_dual_wield = [session] {
    const auto *active_player =
        session != nullptr ? session->objects().GetActivePlayer() : nullptr;
    return active_player != nullptr &&
           active_player->Casts().CanEquipWeaponInOffHand();
  };
  callbacks.can_equip_in_slot = [session](const std::uint32_t entry,
                                         const std::uint32_t slot) {
    if (session == nullptr) {
      return false;
    }
    const auto candidate = session->item_definitions().GetItemSnapshot(entry);
    return candidate.has_value() &&
           CanInventoryTypeGoInSlot(
               static_cast<std::uint32_t>(candidate->inventory_type), slot);
  };
  callbacks.display_system_message = [](const int message_id) {
    DisplaySystemMessage(message_id);
  };

  const auto output = DressUpModel_TryOnItemTyped(input, callbacks);
  if (output.has_value()) {
    DressUpModel_SetNextWeaponSlot(output->next_weapon_slot);
  }
  return output.has_value();
}

int LuaScriptTryOn(lua_State *Ls) {
  using namespace openwow::ui::game::detail;
  using namespace openwow::game;

  const int self = ValidateFrameObjectSelf(Ls, "DressUpModel");
  if (lua_isnumber(Ls, 2) != 0) {
    ResolveDressUpItem(
        Ls, self,
        static_cast<std::uint32_t>(detail::TruncateLuaNumberToSseI32(lua_tonumber(Ls, 2))),
        0, -1, 0);
    return 0;
  }

  if (lua_isstring(Ls, 2) == 0) {
    DisplaySystemMessage(kTryOnErrorNotEquippable);
    return 0;
  }

  const std::string arg = SafeLuaString(Ls, 2);
  if (std::strstr(arg.c_str(), "item:") != nullptr) {
    DressUpParsedItemLink parsed{};
    if (!DressUpModel_ParseItemLink(arg, parsed)) {

      return 0;
    }

    std::int32_t aura_visual = 0;
    const auto item = LookupDressUpItemTemplate(Ls, parsed.item_id);
    const auto *dbc = GetDbcLoader(Ls);
    if (item.has_value() && dbc != nullptr) {
      aura_visual = static_cast<std::int32_t>(DressUpModel_ResolveItemLinkAuraId(
          *item, parsed, &dbc->item_display_info(), &dbc->item_visuals(),
          &dbc->spell_item_enchantment()));
    }
    ResolveDressUpItem(Ls, self, parsed.item_id, aura_visual, -1, 0);
    return 0;
  }

  if (const char *enchant = std::strstr(arg.c_str(), "enchant:");
      enchant != nullptr) {
    const auto spell_id =
        static_cast<std::uint32_t>(std::strtoul(enchant + 8, nullptr, 10));
    const auto *dbc = GetDbcLoader(Ls);
    const auto item_id = dbc != nullptr
                             ? DressUpModel_ResolveEnchantLinkItemId(
                                   spell_id, &dbc->spell())
                             : std::nullopt;
    if (item_id.has_value()) {
      ResolveDressUpItem(Ls, self, *item_id, 0, -1, 0);
      return 0;
    }
    DisplaySystemMessage(kTryOnErrorNotEquippable);
    return 0;
  }

  auto *session = GetWorldSession(Ls);
  if (session != nullptr && session->objects().GetActivePlayer() != nullptr) {
    const auto requested_id =
        ::openwow::game::ItemLinkParser::GetItemId(arg);
    const auto matches = [&](const ::openwow::game::ItemInstance* item) {
      if (item == nullptr || item->IsEmpty()) {
        return false;
      }
      if (requested_id.has_value()) {
        return item->entry == *requested_id;
      }
      const auto* definition = session->item_definitions().GetItem(item->entry);
      return definition != nullptr &&
             openwow::core::SStrCmpI(definition->name.c_str(), arg.c_str(),
                                     0x7fffffffu) == 0;
    };
    const ::openwow::game::ItemInstance* found = nullptr;
    for (std::uint8_t slot = 0;
         slot < ::openwow::game::InventorySlots::kTotalSlots &&
         found == nullptr;
         ++slot) {
      const auto* candidate = session->inventory_replica().GetItemInSlot(slot);
      if (matches(candidate)) {
        found = candidate;
      }
    }
    for (std::uint8_t bag = 1;
         bag <= ::openwow::game::PlayerInventoryReplica::kMaxBags &&
         found == nullptr;
         ++bag) {
      if (const auto* contents = session->inventory_replica().GetBag(bag);
          contents != nullptr) {
        const auto it = std::ranges::find_if(
            contents->slots,
            [&](const ::openwow::game::ItemInstance& candidate) {
              return matches(&candidate);
            });
        if (it != contents->slots.end()) {
          found = &*it;
        }
      }
    }
    if (found != nullptr) {
      const auto *item =
          session->objects().GetItem(::openwow::game::ObjectGuid(found->guid));
      if (item != nullptr) {
        ResolveDressUpItem(Ls, self, item->GetEntry(),
                           item->GetEnchantVisualAuraId(), -1, 0);
        return 0;
      }
    }
  }

  DisplaySystemMessage(kTryOnErrorNotEquippable);
  return 0;
}

void ApplyDressUpPreviewState(lua_State *L, int frame_index,
                              const openwow::game::CGPlayer_C &player) {
  frame_index = lua_absindex(L, frame_index);
  ClearDressUpPreviewState(L, frame_index);
  openwow::game::DressUpModel_ResetWeaponSlotCycle();

  const auto unit_flags = player.State().GetUnitFlags();
  for (std::uint8_t visible_slot = 0; visible_slot < 19; ++visible_slot) {
    if ((visible_slot == 0 && (unit_flags & 0x400u) != 0) ||
        (visible_slot == 14 && (unit_flags & 0x800u) != 0) ||
        visible_slot == 17) {
      continue;
    }

    const auto visible_item = player.GetVisibleEquipSlotInfo(visible_slot);
    if (!visible_item.has_value()) {
      continue;
    }

    const std::int32_t force_slot =
        visible_slot == openwow::game::kDressUpSlotMainhand ||
                visible_slot == openwow::game::kDressUpSlotOffhand
            ? static_cast<std::int32_t>(visible_slot)
            : -1;
    ResolveDressUpItem(
        L, frame_index, visible_item->item_id,
        static_cast<std::int32_t>(player.GetVisibleItemAuraVisual(visible_slot)),
        force_slot, 0);
  }
}

void ApplyResolvedUnitModelState(lua_State *L, int frame_index,
                                 const openwow::game::CGUnit_C &unit) {
  frame_index = lua_absindex(L, frame_index);
  StoreBoundUnitDisplayId(L, frame_index, unit.Presentation().DisplayId());
  ClearBoundUnitSequence(L, frame_index);
  StoreBoundUnitAlpha(L, frame_index, unit.Presentation().UnitAlphaByte());
}

void RefreshBoundUnitModelState(lua_State *L, int frame_index,
                                const openwow::game::ObjectGuid guid) {
  frame_index = lua_absindex(L, frame_index);
  if (guid.IsEmpty()) {
    return;
  }

  auto *session = openwow::ui::game::detail::GetWorldSession(L);
  if (session == nullptr) {
    return;
  }

  const auto *unit = session->objects().GetUnit(guid);
  if (unit == nullptr) {

    lua_pushnil(L);
    lua_setfield(L, frame_index, "__ow_model_unit_guid_lo");
    lua_pushnil(L);
    lua_setfield(L, frame_index, "__ow_model_unit_guid_hi");
    return;
  }

  ApplyResolvedUnitModelState(L, frame_index, *unit);
  if (!IsDressUpPreviewFrame(L, frame_index)) {
    return;
  }

  if (const auto *player = session->objects().GetPlayer(guid)) {
    ApplyDressUpPreviewState(L, frame_index, *player);
    return;
  }

  ClearDressUpPreviewState(L, frame_index);
}

int LuaPlayerModelSetUnit(lua_State *Ls) {
  if (!lua_istable(Ls, 1)) {
    return 0;
  }
  if (!lua_isstring(Ls, 2)) {
    return luaL_error(Ls, "Usage: SetUnit(\"unit\")");
  }

  auto *session = openwow::ui::game::detail::GetWorldSession(Ls);
  if (session == nullptr) {
    return 0;
  }

  const std::string unit_id = openwow::ui::game::detail::SafeLuaString(Ls, 2);
  const auto guid = openwow::ui::game::detail::ResolveUnitId(session, unit_id);
  if (guid.IsEmpty()) {
    return 0;
  }

  lua_pushvalue(Ls, 2);
  lua_setfield(Ls, 1, "__ow_model_unit");
  StoreBoundUnitGuid(Ls, 1, guid);
  ClearBoundCreatureBinding(Ls, 1);
  ClearCharacterModelHiddenState(Ls, 1);
  RefreshBoundUnitModelState(Ls, 1, guid);
  return 0;
}

int LuaPlayerModelSetCreature(lua_State *Ls) {
  if (!lua_istable(Ls, 1)) {
    return 0;
  }
  if (!lua_isnumber(Ls, 2)) {
    return luaL_error(Ls, "Usage: SetCreature(creatureID)");
  }

  auto *session = openwow::ui::game::detail::GetWorldSession(Ls);
  if (session == nullptr) {
    return 0;
  }

  const auto creature_entry = static_cast<std::uint32_t>(lua_tointeger(Ls, 2));
  const auto* creature_template =
      session->query_cache().GetOrRequestCreatureTemplate(creature_entry);
  if (creature_template == nullptr) {
    return 0;
  }

  openwow::ui::game::detail::ApplyCreatureTemplateBindingToFrame(
      Ls, 1, *creature_template);
  ClearCharacterModelHiddenState(Ls, 1);
  return 0;
}

int LuaPlayerModelRefreshUnit(lua_State *Ls) {
  if (!lua_istable(Ls, 1)) {
    return 0;
  }

  ClearBoundCreatureBinding(Ls, 1);
  RefreshBoundUnitModelState(Ls, 1, LoadBoundUnitGuid(Ls, 1));
  return 0;
}

}

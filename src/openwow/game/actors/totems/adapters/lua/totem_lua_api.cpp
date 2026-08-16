#include "openwow/game/actors/totems/adapters/lua/totem_lua_api.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/spellbook_frame.h"
#include "openwow/ui/game/error_message.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"
#include "openwow/ui/lua_result_capacity.h"

#include <cmath>

namespace openwow::ui::game::detail {

namespace {

constexpr std::array<std::uint32_t, 4> kEmptyTotemSlotFallbackCategories{{
    4,
    2,
    5,
    3,
}};

std::optional<std::uint8_t> ToTotemSlotIndex(lua_State *L, const int arg_index, const char *usage) {
  if (!lua_isnumber(L, arg_index)) {
    luaL_error(L, usage);
    return std::nullopt;
  }

  const int slot = static_cast<int>(lua_tonumber(L, arg_index));
  if (static_cast<unsigned>(slot - 1) > 3) {
    luaL_error(L, "Totem slot must be in range of 1 to %d", 4);
    return std::nullopt;
  }

  return static_cast<std::uint8_t>(slot - 1);
}

template <typename Visitor>
bool VisitCarriedItemEntries(const PlayerInventoryReplica& inventory, Visitor&& visitor) {
  for (std::uint8_t slot = 0; slot < PlayerInventoryReplica::kMaxEquipSlots; ++slot) {
    if (const auto *item = inventory.GetEquipSlot(slot); item != nullptr && visitor(item->entry)) {
      return true;
    }
  }

  for (std::uint8_t bag_index = 1; bag_index <= PlayerInventoryReplica::kMaxBags; ++bag_index) {
    const auto *bag = inventory.GetBag(bag_index);
    if (bag == nullptr) {
      continue;
    }

    if (visitor(bag->entry)) {
      return true;
    }

    for (const auto &item : bag->slots) {
      if (visitor(item.entry)) {
        return true;
      }
    }
  }

  for (std::uint8_t slot = 0; slot < PlayerInventoryReplica::kBackpackSize; ++slot) {
    if (const auto *item = inventory.GetBackpackSlot(slot);
        item != nullptr && visitor(item->entry)) {
      return true;
    }
  }

  for (std::uint8_t slot = 0; slot < PlayerInventoryReplica::kKeyringSlots; ++slot) {
    if (const auto *item = inventory.GetKeyringSlot(slot);
        item != nullptr && visitor(item->entry)) {
      return true;
    }
  }

  return false;
}

bool HasTrackedCarriedTotemCategory(const openwow::game::WorldSession &session,
                                    const std::uint32_t totem_category) {
  if (totem_category == 0 || session.objects().GetLocalPlayer() == nullptr) {
    return false;
  }

  return VisitCarriedItemEntries(session.inventory_replica(),
                                 [&session, totem_category](const std::uint32_t entry) {
    if (entry == 0) {
      return false;
    }

    const auto *item_template = session.query_cache().GetItemTemplate(entry);
    return item_template != nullptr && item_template->totem_category == totem_category;
                                 });
}

bool EmptyTotemSlotHasFallbackItem(const openwow::game::WorldSession &session,
                                   const std::uint8_t slot_index) {
  if (slot_index >= kEmptyTotemSlotFallbackCategories.size()) {
    return false;
  }

  return HasTrackedCarriedTotemCategory(session, kEmptyTotemSlotFallbackCategories[slot_index]);
}

std::string ResolveTotemName(lua_State *L, const std::uint32_t spell_id) {
  const auto *spell = LookupSpellEntry(L, spell_id);
  if (!spell || std::string_view(spell->spell_name).empty()) {
    return {};
  }

  return std::string(spell->spell_name);
}

std::string ResolveTotemIconPath(lua_State *L, const std::uint32_t spell_id) {
  const auto *spell = LookupSpellEntry(L, spell_id);
  if (!spell) {
    return {};
  }

  lua_getfield(L, LUA_REGISTRYINDEX, "openwow.dbc_loader");
  const auto *dbc = static_cast<const openwow::data::dbc::DbcLoader *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  if (!dbc || spell->spell_icon_id == 0) {
    return {};
  }

  const auto *icon = dbc->spell_icon().LookupEntry(spell->spell_icon_id);
  if (!icon || std::string_view(icon->icon_path).empty()) {
    return {};
  }

  std::string path(icon->icon_path);
  if (path.find(".blp") == std::string::npos && path.find(".BLP") == std::string::npos) {
    path += ".blp";
  }
  return path;
}

const openwow::data::dbc::DbcLoader *GetBoundDbcLoader(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "openwow.dbc_loader");
  const auto *dbc = static_cast<const openwow::data::dbc::DbcLoader *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  return dbc;
}

std::uint8_t NormalizeMultiCastTotemSlot(lua_State *L, const int arg_index) {
  const auto raw_slot_value = lua_tonumber(L, arg_index);
  auto wrapped_slot = std::fmod(std::trunc(raw_slot_value), 256.0);
  if (wrapped_slot < 0.0) {
    wrapped_slot += 256.0;
  }

  const auto slot_byte = static_cast<std::uint8_t>(wrapped_slot);
  return static_cast<std::uint8_t>((slot_byte - 1u) & 0x3u);
}

}

int LuaGetTotemInfo(lua_State *L) {
  const auto slot_index = ToTotemSlotIndex(L, 1, "Usage: GetTotemInfo(slot)");
  auto *session = GetWorldSession(L);

  if (session && slot_index.has_value()) {
    const auto slot = session->spell_book().GetTotemSlot(*slot_index);
    if (slot.has_value() && slot->has_totem()) {
      lua_pushboolean(L, 1);
      const auto name = ResolveTotemName(L, slot->spell_id);
      lua_pushstring(L, name.c_str());
      lua_pushnumber(L, static_cast<lua_Number>(slot->start_time_ms / 1000.0));
      lua_pushnumber(L, static_cast<lua_Number>(slot->duration_ms / 1000.0));
      const auto icon = ResolveTotemIconPath(L, slot->spell_id);
      lua_pushstring(L, icon.c_str());
      return 5;
    }
  }

  const bool has_fallback_item =
      session && slot_index.has_value() && EmptyTotemSlotHasFallbackItem(*session, *slot_index);
  lua_pushboolean(L, has_fallback_item ? 1 : 0);
  lua_pushstring(L, "");
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushstring(L, "");
  return 5;
}

int LuaGetTotemTimeLeft(lua_State *L) {
  const auto slot_index = ToTotemSlotIndex(L, 1, "Usage: GetTotemTimeLeft(slot)");
  auto *session = GetWorldSession(L);
  if (!session || !slot_index.has_value()) {
    lua_pushnumber(L, 0);
    return 1;
  }

  const auto slot = session->spell_book().GetTotemSlot(*slot_index);
  if (!slot.has_value() || !slot->has_totem()) {
    lua_pushnumber(L, 0);
    return 1;
  }

  const auto remaining_ms = slot->RemainingTimeMs(session->CurrentClientTimeMs());
  lua_pushnumber(L, static_cast<lua_Number>(remaining_ms / 1000.0));
  return 1;
}

int LuaDestroyTotem(lua_State *L) {
  const auto slot_index = ToTotemSlotIndex(L, 1, "Usage: DestroyTotem(slot)");

  auto *session = GetWorldSession(L);
  if (!session || !slot_index.has_value())
    return 0;

  const auto slot = session->spell_book().GetTotemSlot(*slot_index);
  if (!slot.has_value() || !slot->has_totem()) {
    return 0;
  }

  session->interaction().SendTotemDestroyed(*slot_index);
  if (session->spell_book().ClearTotemSlot(*slot_index)) {
    ScriptEventDispatch::Get().FirePlayerTotemUpdate(*slot_index + 1);
  }
  return 0;
}

openwow::ui::lua::NativeBindingCatalog TotemConstantCatalog() {
  constexpr openwow::ui::LuaIntegerGlobal kTotemConstants[] = {
      {"MAX_TOTEMS", 4},
      {"EARTH_TOTEM_SLOT", 1},
      {"FIRE_TOTEM_SLOT", 2},
      {"WATER_TOTEM_SLOT", 3},
      {"AIR_TOTEM_SLOT", 4},
  };
  return openwow::ui::lua::NativeConstantCatalog(
      "game.actors.totems", openwow::ui::lua::BindingScope::kWorld,
      kTotemConstants);
}

int LuaGetMultiCastTotemSpells(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetMultiCastTotemSpells(slot)");
  }

  const auto slot_index = NormalizeMultiCastTotemSlot(L, 1);
  const auto spell_ids =
      ::openwow::game::SpellBookFrame::GetMultiCastTotemSpells(slot_index, GetBoundDbcLoader(L));
  const int result_count = openwow::ui::ReserveLuaResultCapacity(
      L, spell_ids.size(), "multicast totem spells");
  for (const auto spell_id : spell_ids) {
    lua_pushnumber(L, static_cast<lua_Number>(spell_id));
  }
  return result_count;
}

int LuaSetMultiCastSpell(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: SetMultiCastSpell(slot, spellID)");
  }

  if (!openwow::ui::game::GameUI_CanPerformProtectedAction(
          openwow::ui::game::protected_action_kind::kActionSlotMutation)) {
    return 0;
  }

  auto *session = GetWorldSession(L);
  if (!session) {
    return 0;
  }

  const auto slot_index = static_cast<int>(lua_tonumber(L, 1)) - 1;
  if (slot_index < 0 || !IsMultiCastActionSlot(static_cast<std::size_t>(slot_index))) {
    return 0;
  }

  if (lua_isnoneornil(L, 2) || lua_tonumber(L, 2) == 0.0) {
    session->action_assignments().ClearAssignment(static_cast<std::size_t>(slot_index));
    session->interaction().SendClearActionButton(static_cast<std::uint8_t>(slot_index));
    ScriptEventDispatch::Get().FireActionbarSlotChanged(static_cast<std::uint8_t>(slot_index + 1));
    return 0;
  }

  const auto spell_id = static_cast<std::uint32_t>(lua_tonumber(L, 2));
  const auto validation =
      ValidateMultiCastSpellPlacement(L, static_cast<std::size_t>(slot_index), spell_id);
  if (validation == MultiCastSlotValidationResult::kMissingSpellData) {
    return 0;
  }
  if (validation != MultiCastSlotValidationResult::kAccept) {
    ReportMultiCastSlotError(L, static_cast<std::size_t>(slot_index));
    return 0;
  }

  ::openwow::game::ActionPresentationEntry button;
  button.action = spell_id;
  button.type = ::openwow::game::ActionPresentationKind::kSpell;
  session->action_assignments().SetPresentationEntry(static_cast<std::size_t>(slot_index), button);
  session->interaction().SendSetActionButton(static_cast<std::uint8_t>(slot_index), button);
  ScriptEventDispatch::Get().FireActionbarSlotChanged(static_cast<std::uint8_t>(slot_index + 1));
  return 0;
}

}

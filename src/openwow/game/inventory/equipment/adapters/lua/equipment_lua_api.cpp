#include "openwow/game/inventory/equipment/adapters/lua/equipment_lua_api.h"

#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/game/inventory/items/adapters/lua/item_lua_adapter.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/runtime/security/protected_action_gate.h"

extern "C" {
#include <lua.hpp>
}

#include <cstdio>
#include <string>

namespace openwow::ui::game::detail {
namespace {

constexpr std::string_view kIconPrefix = "Interface\\Icons\\";

constexpr std::string_view kEquipmentManagerCVar = "equipmentManager";

openwow::game::EquipmentSets& sets(lua_State* state) {
  return RequireItemLuaAdapter(state).equipment();
}

std::string icon_path(const std::string& icon) {
  return std::string(kIconPrefix) + icon;
}

std::size_t save_slot(lua_State* state, const char* usage) {
  if (!lua_isnumber(state, 1)) {
    luaL_error(state, "%s", usage);
  }
  const auto slot = static_cast<int>(lua_tonumber(state, 1)) - 1;
  if (slot < 0 || slot >= 19) {
    luaL_error(state, "%s", usage);
  }
  return static_cast<std::size_t>(slot);
}

std::string checked_name(lua_State* state, const int argument,
                         const char* usage) {
  if (!lua_isstring(state, argument)) {
    luaL_error(state, "%s", usage);
  }
  const char* value = lua_tostring(state, argument);
  if (value == nullptr || *value == '\0') {
    return {};
  }
  std::string name(value);
  if (name.size() > 16) {
    name.resize(16);
  }
  return name;
}

std::int32_t location_of(
    const openwow::game::PlayerInventoryReplica& inventory,
    const openwow::game::ObjectGuid item, const bool bank_open) {
  if (item.IsEmpty()) {
    return 0;
  }
  if (item.GetRawValue() == 1) {
    return 1;
  }

  const auto absolute = inventory.FindSlotByGuid(item.GetRawValue());
  if (absolute >= 0) {
    if (absolute < 19) {
      return 0x100001 + absolute;
    }
    if (absolute >= 23 && absolute <= 38) {
      return 0x300001 + absolute - 23;
    }
    if (bank_open && absolute >= 39 && absolute <= 66) {
      return 0x400001 + absolute;
    }
  }

  for (std::uint8_t bag = 1;
       bag <= openwow::game::PlayerInventoryReplica::kMaxBags; ++bag) {
    const auto* contents = inventory.GetBag(bag);
    if (contents == nullptr) {
      continue;
    }
    for (std::size_t slot = 0; slot < contents->slots.size(); ++slot) {
      if (contents->slots[slot].guid == item.GetRawValue()) {
        return 0x300000 | (static_cast<std::int32_t>(bag) << 8) |
               static_cast<std::int32_t>(slot + 1);
      }
    }
  }

  if (bank_open) {
    for (std::uint8_t bag = 0;
         bag < openwow::game::PlayerInventoryReplica::kMaxBankBags; ++bag) {
      const auto* contents = inventory.GetBankBag(bag);
      if (contents == nullptr) {
        continue;
      }
      for (std::size_t slot = 0; slot < contents->slots.size(); ++slot) {
        if (contents->slots[slot].guid == item.GetRawValue()) {
          return ((static_cast<std::int32_t>(bag + 1) | 0x6000) << 8) |
                 static_cast<std::int32_t>(slot + 1);
        }
      }
    }
  }
  return -1;
}

void hold_set(lua_State* state, const openwow::game::EquipmentSet& set) {
  auto* cursor = RequireItemLuaAdapter(state).held_cursor();
  if (cursor == nullptr) {
    return;
  }
  cursor->Clear();
  cursor->HoldEquipmentSet(
      openwow::game::actions::held_cursor::EquipmentSet{
          .stable_id = set.id},
      openwow::game::actions::held_cursor::Presentation{
          .texture_path = icon_path(set.icon),
          .sound =
              openwow::game::actions::held_cursor::Sound::CursorGrabObject,
          .grid = openwow::game::actions::held_cursor::Grid::ActionBar,
      });
}

}

int LuaGetNumEquipmentSets(lua_State* state) {
  lua_pushnumber(state, static_cast<lua_Number>(sets(state).size()));
  return 1;
}

int LuaGetEquipmentSetInfo(lua_State* state) {
  const auto index = static_cast<int>(lua_tonumber(state, 1)) - 1;
  if (index < 0 || index >= 10) {
    return luaL_error(state, "Usage: GetEquipmentSetInfo(setIndex)");
  }
  const auto* set = sets(state).at(static_cast<std::size_t>(index));
  if (set == nullptr) {
    return 0;
  }
  lua_pushlstring(state, set->name.data(), set->name.size());
  const auto icon = icon_path(set->icon);
  lua_pushlstring(state, icon.data(), icon.size());
  lua_pushnumber(state, static_cast<lua_Number>(set->id));
  return 3;
}

int LuaGetEquipmentSetInfoByName(lua_State* state) {
  const auto name = checked_name(
      state, 1, "Usage: GetEquipmentSetInfoByName(\"setName\")");
  const auto* set = sets(state).find(name);
  if (set == nullptr) {
    return 0;
  }
  lua_pushlstring(state, set->icon.data(), set->icon.size());
  lua_pushnumber(state, static_cast<lua_Number>(set->id));
  return 2;
}

int LuaSaveEquipmentSet(lua_State* state) {

  auto& adapter = RequireItemLuaAdapter(state);
  if (!adapter.equipment().received_list() || !lua_isnumber(state, 2)) {
    return 0;
  }
  const auto name =
      checked_name(state, 1, "Usage: SaveEquipmentSet(\"setName\", iconIndex)");
  if (name.empty()) {
    return luaL_error(state, "Invalid string for setName");
  }

  const auto icon_index = static_cast<int>(lua_tonumber(state, 2));
  std::string icon = "INV_Misc_QuestionMark";
  if (icon_index < 0) {
    if (auto resolved = adapter.ResolveVisibleSlotIcon(
            state, static_cast<std::uint8_t>(-icon_index - 1));
        resolved.has_value()) {
      const auto slash = resolved->find_last_of("\\/");
      icon = slash == std::string::npos ? *resolved
                                       : resolved->substr(slash + 1);
    }
  }

  const auto request =
      adapter.equipment().prepare_save(name, icon, adapter.inventory());
  if (!request.has_value()) {
    return luaL_error(state, "Too many sets! You can only have %d sets", 10);
  }
  adapter.SaveSet(*request);
  adapter.equipment().clear_next_save_ignored_slots();
  return 0;
}

int LuaDeleteEquipmentSet(lua_State* state) {
  auto& adapter = RequireItemLuaAdapter(state);
  const auto name =
      checked_name(state, 1, "Usage: DeleteEquipmentSet(\"setName\")");
  if (const auto* set = adapter.equipment().find(name);
      set != nullptr && !set->guid.IsEmpty()) {
    adapter.DeleteSet(set->guid);
  }
  return 0;
}

int LuaUseEquipmentSet(lua_State* state) {
  auto& adapter = RequireItemLuaAdapter(state);
  const auto name =
      checked_name(state, 1, "Usage: UseEquipmentSet(\"setName\")");
  const auto* set = adapter.equipment().find(name);
  if (set == nullptr) {
    return 0;
  }
  const auto request = adapter.equipment().prepare_use(
      set->id, adapter.inventory(), adapter.bank_frame_open());
  if (!request.has_value()) {
    lua_pushnil(state);
    return 1;
  }
  adapter.UseSet(*request);
  adapter.equipment().mark_use_pending(set->id);
  lua_pushboolean(state, 1);
  return 1;
}

int LuaGetEquipmentSetItemIDs(lua_State* state) {
  const auto name = checked_name(
      state, 1, "Usage: GetEquipmentSetItemIDs(\"setName\" [, returnTable])");
  const auto* set = sets(state).find(name);
  if (set == nullptr) {
    return 0;
  }
  auto& inventory = RequireItemLuaAdapter(state).inventory();
  lua_settop(state, lua_istable(state, 2) ? 2 : 1);
  if (!lua_istable(state, 2)) {
    lua_newtable(state);
  }
  for (std::size_t slot = 0; slot < set->items.size(); ++slot) {
    lua_pushnumber(state, static_cast<lua_Number>(slot + 1));
    std::int32_t item_id = 0;
    if (set->ignored.test(slot)) {
      item_id = 1;
    } else if (set->items[slot].has_value()) {
      if (const auto* item = inventory.FindItemByGuid(
                     set->items[slot]->GetRawValue());
                 item != nullptr) {
        item_id = static_cast<std::int32_t>(item->entry);
      } else {
        item_id = -1;
      }
    }
    lua_pushnumber(state, static_cast<lua_Number>(item_id));
    lua_rawset(state, -3);
  }
  return 1;
}

int LuaCanUseEquipmentSets(lua_State* state) {

  const bool available =
      !sets(state).pending_use().has_value() &&
      openwow::ui::game::CVarSystem::Instance().GetCVarBool(
          std::string(kEquipmentManagerCVar));
  lua_pushnumber(state, available ? 1.0 : 0.0);
  return 1;
}

int LuaGetEquipmentSetLocations(lua_State* state) {
  const auto name = checked_name(
      state, 1,
      "Usage: GetEquipmentSetLocations(\"setName\" [, returnTable])");
  const auto* set = sets(state).find(name);
  if (set == nullptr) {
    return 0;
  }
  auto& adapter = RequireItemLuaAdapter(state);
  lua_settop(state, lua_istable(state, 2) ? 2 : 1);
  if (!lua_istable(state, 2)) {
    lua_newtable(state);
  }
  for (std::size_t slot = 0; slot < set->items.size(); ++slot) {
    lua_pushnumber(state, static_cast<lua_Number>(slot + 1));
    lua_pushnumber(
        state,
        static_cast<lua_Number>(
            set->ignored.test(slot)
                ? 0
                : set->items[slot].has_value()
                ? location_of(adapter.inventory(), *set->items[slot],
                              adapter.bank_frame_open())
                : 0));
    lua_rawset(state, -3);
  }
  return 1;
}

int LuaPickupEquipmentSetByName(lua_State* state) {

  if (!openwow::ui::game::GameUI_CanPerformProtectedAction(
          openwow::ui::game::protected_action_kind::kActionSlotMutation)) {
    return 0;
  }
  auto& adapter = RequireItemLuaAdapter(state);
  const auto name = checked_name(
      state, 1, "Usage: PickupEquipmentSetByName(\"setName\")");
  if (const auto* set = adapter.equipment().find(name); set != nullptr) {
    hold_set(state, *set);
  }
  return 0;
}

int LuaPickupEquipmentSet(lua_State* state) {
  if (!lua_isnumber(state, 1)) {
    return luaL_error(state, "Usage: PickupEquipmentSet(setIndex)");
  }
  const auto index = static_cast<int>(lua_tonumber(state, 1)) - 1;
  if (index >= 0) {
    if (const auto* set = sets(state).at(static_cast<std::size_t>(index));
        set != nullptr) {
      hold_set(state, *set);
    }
  }
  return 0;
}

int LuaEquipmentManagerClearIgnoredSlotsForSave(lua_State* state) {
  sets(state).clear_next_save_ignored_slots();
  return 0;
}

int LuaEquipmentManagerIgnoreSlotForSave(lua_State* state) {
  sets(state).ignore_next_save_slot(
      save_slot(state, "Usage: EquipmentManagerIgnoreSlotForSave(slot)"),
      true);
  return 0;
}

int LuaEquipmentManagerIsSlotIgnoredForSave(lua_State* state) {
  lua_pushboolean(
      state,
      sets(state).next_save_slot_ignored(
          save_slot(state, "Usage: EquipmentSetIsSlotIgnoredForSave(slot)")));
  return 1;
}

int LuaEquipmentManagerUnignoreSlotForSave(lua_State* state) {
  sets(state).ignore_next_save_slot(
      save_slot(state, "Usage: UpdatePendingEquipmentSetSlot(slot)"),
      false);
  return 0;
}

int LuaRenameEquipmentSet(lua_State* state) {
  auto& adapter = RequireItemLuaAdapter(state);
  const auto old_name = checked_name(
      state, 1, "Usage: RenameEquipmentSet(\"oldName\", \"newName\")");
  const auto new_name = checked_name(
      state, 2, "Usage: RenameEquipmentSet(\"oldName\", \"newName\")");
  if (new_name.empty()) {
    return luaL_error(state, "Invalid string for setName");
  }
  if (const auto request =
          adapter.equipment().prepare_rename(old_name, new_name);
      request.has_value()) {
    adapter.SaveSet(*request);
  }
  return 0;
}

}

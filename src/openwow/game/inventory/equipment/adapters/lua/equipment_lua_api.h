#pragma once

struct lua_State;

namespace openwow::ui::game::detail {

int LuaGetNumEquipmentSets(lua_State*);
int LuaGetEquipmentSetInfo(lua_State*);
int LuaGetEquipmentSetInfoByName(lua_State*);
int LuaSaveEquipmentSet(lua_State*);
int LuaDeleteEquipmentSet(lua_State*);
int LuaUseEquipmentSet(lua_State*);
int LuaGetEquipmentSetItemIDs(lua_State*);
int LuaCanUseEquipmentSets(lua_State*);
int LuaGetEquipmentSetLocations(lua_State*);
int LuaPickupEquipmentSetByName(lua_State*);
int LuaPickupEquipmentSet(lua_State*);
int LuaEquipmentManagerClearIgnoredSlotsForSave(lua_State*);
int LuaEquipmentManagerIgnoreSlotForSave(lua_State*);
int LuaEquipmentManagerIsSlotIgnoredForSave(lua_State*);
int LuaEquipmentManagerUnignoreSlotForSave(lua_State*);
int LuaRenameEquipmentSet(lua_State*);

}

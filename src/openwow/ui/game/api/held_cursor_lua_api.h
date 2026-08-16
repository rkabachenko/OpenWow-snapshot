
#pragma once

#include "openwow/game/actions/held_cursor/held_cursor.h"

#include <cstdint>

struct lua_State;

namespace openwow::game {
class PlayerInventoryReplica;
class WorldSession;
}

namespace openwow::ui::game::detail {

int LuaGetCursorInfo(lua_State* L);
int LuaClearCursor(lua_State* L);
int LuaShowCursor(lua_State* L);
int LuaHideCursor(lua_State* L);

int LuaPickupItem(lua_State* L);
int LuaPickupInventoryItem(lua_State* L);
int LuaPickupSpell(lua_State* L);
int LuaPickupPetAction(lua_State* L);
int LuaPickupMacro(lua_State* L);
int LuaPickupMerchantItem(lua_State* L);
int LuaPickupBagFromSlot(lua_State* L);
int LuaCursorCanGoInSlot(lua_State* L);
[[nodiscard]] bool TryPlaceHeldPetActionOnBar(lua_State* L,
                                              ::openwow::game::WorldSession& session,
                                              std::uint64_t pet_guid,
                                              std::uint32_t slot_index);
bool ResolveHeldCursorServerCoords(
    const ::openwow::game::actions::held_cursor::HeldCursor& cursor,
    std::uint8_t* out_bag, std::uint8_t* out_slot);
bool AutoEquipHeldCursorItem(::openwow::game::WorldSession& session,
                             bool skip_bind_check = false);
bool PaperDollInfo_PickupInventoryItem(lua_State* L,
                                       ::openwow::game::WorldSession& session,
                                       unsigned int slot);
void HandleResolvedSpellbookPickupOrDrop(lua_State* L,
                                         std::uint32_t spell_id,
                                         std::uint32_t spellbook_slot,
                                         bool from_pet_book);

int LuaPutItemInBag(lua_State* L);
int LuaPutItemInBackpack(lua_State* L);
int LuaDropItemOnUnit(lua_State* L);
int LuaEquipCursorItem(lua_State* L);

int LuaInRepairMode(lua_State* L);
int LuaShowRepairCursor(lua_State* L);
int LuaHideRepairCursor(lua_State* L);
[[nodiscard]] bool IsRepairCursorModeActive();

int LuaCursorHasItem(lua_State* L);
int LuaCursorHasMacro(lua_State* L);
int LuaCursorHasMoney(lua_State* L);
int LuaCursorHasSpell(lua_State* L);
int LuaDropCursorMoney(lua_State* L);
int LuaPickupPlayerMoney(lua_State* L);

}

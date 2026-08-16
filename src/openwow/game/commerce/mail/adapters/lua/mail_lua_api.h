#pragma once

#include <cstdint>

struct lua_State;

namespace openwow::game {
class WorldSession;
struct ItemInstance;
}

namespace openwow::ui::game::detail {

int LuaHasNewMail(lua_State*);
int LuaGetLatestThreeSenders(lua_State*);
int LuaGetInboxNumItems(lua_State*);
int LuaGetInboxHeaderInfo(lua_State*);
int LuaGetInboxText(lua_State*);
int LuaGetInboxItem(lua_State*);
int LuaGetInboxItemLink(lua_State*);
int LuaGetInboxInvoiceInfo(lua_State*);
int LuaTakeInboxItem(lua_State*);
int LuaTakeInboxMoney(lua_State*);
int LuaTakeInboxTextItem(lua_State*);
int LuaDeleteInboxItem(lua_State*);
int LuaReturnInboxItem(lua_State*);
int LuaInboxItemCanDelete(lua_State*);
int LuaAutoLootMailItem(lua_State*);
int LuaCheckInbox(lua_State*);
int LuaCloseMail(lua_State*);
int LuaSendMail(lua_State*);
int LuaSetSendMailMoney(lua_State*);
int LuaSetSendMailCOD(lua_State*);
int LuaGetSendMailPrice(lua_State*);
int LuaClickSendMailItemButton(lua_State*);
int LuaGetSendMailItem(lua_State*);
int LuaClearSendMail(lua_State*);
int LuaGetNumPackages(lua_State*);
int LuaGetPackageInfo(lua_State*);
int LuaSelectPackage(lua_State*);
int LuaGetSendMailItemLink(lua_State*);
int LuaGetSendMailMoney(lua_State*);
int LuaGetSendMailCOD(lua_State*);
int LuaGetNumStationeries(lua_State*);
int LuaGetSelectedStationeryTexture(lua_State*);
int LuaSelectStationery(lua_State*);
int LuaRespondMailLockSendItem(lua_State*);
int LuaSetSendMailShowing(lua_State*);
int LuaGetStationeryInfo(lua_State*);
int LuaComplainInboxItem(lua_State*);
int LuaInboxItemCanComplain(lua_State*);

bool TryAttachSendMailContainerItem(
    lua_State*, openwow::game::WorldSession&,
    const openwow::game::ItemInstance&, std::uint8_t source_bag,
    std::uint8_t source_slot);
void ResetMailComposeUiState(bool fire_script_events);
void CloseMailInteraction(openwow::game::WorldSession*);
void PrimeMailUiRuntimeForPlayerEnterWorld(openwow::game::WorldSession&);

}

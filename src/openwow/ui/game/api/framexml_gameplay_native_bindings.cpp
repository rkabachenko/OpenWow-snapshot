#include "openwow/ui/game/api/framexml_native_bindings.h"
#include "openwow/ui/lua_binding_registry.h"

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::game::detail {

int LuaApi_AcceptLevelGrant(lua_State* L);
int LuaApi_AcceptXPLoss(lua_State* L);
int LuaCanGrantLevel(lua_State* L);
int LuaApi_CheckSpiritHealerDist(lua_State* L);
int LuaApi_CheckTalentMasterDist(lua_State* L);
int LuaApi_CloseBattlefield(lua_State* L);
int LuaApi_ConfirmBinder(lua_State* L);
int LuaConfirmTalentWipe(lua_State* L);
int LuaApi_ConfirmSummon(lua_State* L);
int LuaApi_DeclineLevelGrant(lua_State* L);
int LuaApi_DownloadSettings(lua_State* L);
int LuaApi_GetCritChanceFromAgility(lua_State* L);
int LuaApi_GetDamageBonusStat(lua_State* L);
int LuaApi_GetMirrorTimerInfo(lua_State* L);
int LuaApi_GetMirrorTimerProgress(lua_State* L);
int LuaApi_GetMouseButtonName(lua_State* L);
int LuaGetNumQuestLogRewardFactions(lua_State* L);
int LuaGetQuestLogRewardFactionInfo(lua_State* L);
int LuaGetSelectedDisplayChannel(lua_State* L);
int LuaApi_GetNextStableSlotCost(lua_State* L);
int LuaApi_GetSummonConfirmAreaName(lua_State* L);
int LuaApi_GetSummonConfirmSummoner(lua_State* L);
int LuaApi_GetSummonConfirmTimeLeft(lua_State* L);
int LuaApi_GetTaxiBenchmarkMode(lua_State* L);
int LuaApi_GetUnitHealthRegenRateFromSpirit(lua_State* L);
int LuaGetUnitManaRegenRateFromSpirit(lua_State* L);
int LuaGrantLevel(lua_State* L);
int LuaApi_HasLFGRestrictions(lua_State* L);
int LuaApi_IsAtStableMaster(lua_State* L);
int LuaApi_IsDesaturateSupported(lua_State* L);
int LuaApi_IsInArenaTeam(lua_State* L);
int LuaIsSilenced(lua_State* L);
int LuaApi_IsThreatWarningEnabled(lua_State* L);
int LuaApi_NoPlayTime(lua_State* L);
int LuaApi_NotWhileDeadError(lua_State* L);
int LuaApi_PartialPlayTime(lua_State* L);
int LuaApi_PlayerCanTeleport(lua_State* L);
int LuaProcessQuestLogRewardFactions(lua_State* L);
int LuaApi_RespondInstanceLock(lua_State* L);
int LuaApi_SetLayoutMode(lua_State* L);
int LuaSetSelectedDisplayChannel(lua_State* L);
int LuaApi_SetTaxiBenchmarkMode(lua_State* L);
int LuaApi_SetUIVisibility(lua_State* L);
int LuaSilenceMember(lua_State* L);
int LuaUnSilenceMember(lua_State* L);
int LuaApi_UnitIsControlling(lua_State* L);
int LuaApi_UnitIsPVPSanctuary(lua_State* L);
int LuaUnitIsPartyLeader(lua_State* L);
int LuaApi_UnitSwitchToVehicleSeat(lua_State* L);
int LuaApi_UnitVehicleSkin(lua_State* L);
int LuaApi_UpdateSpells(lua_State* L);
int LuaApi_UploadSettings(lua_State* L);

}

namespace openwow::ui::game::detail {

namespace {

constexpr openwow::ui::LuaGlobalBinding kGameplayLuaBindings[] = {
    {"AcceptLevelGrant", LuaApi_AcceptLevelGrant},
    {"AcceptXPLoss", LuaApi_AcceptXPLoss},
    {"CanGrantLevel", LuaCanGrantLevel},
    {"CheckSpiritHealerDist", LuaApi_CheckSpiritHealerDist},
    {"CheckTalentMasterDist", LuaApi_CheckTalentMasterDist},
    {"CloseBattlefield", LuaApi_CloseBattlefield},
    {"ConfirmBinder", LuaApi_ConfirmBinder},
    {"ConfirmTalentWipe", LuaConfirmTalentWipe},
    {"ConfirmSummon", LuaApi_ConfirmSummon},
    {"DeclineLevelGrant", LuaApi_DeclineLevelGrant},
    {"DownloadSettings", LuaApi_DownloadSettings},
    {"GetCritChanceFromAgility", LuaApi_GetCritChanceFromAgility},
    {"GetDamageBonusStat", LuaApi_GetDamageBonusStat},
    {"GetMirrorTimerInfo", LuaApi_GetMirrorTimerInfo},
    {"GetMirrorTimerProgress", LuaApi_GetMirrorTimerProgress},
    {"GetMouseButtonName", LuaApi_GetMouseButtonName},
    {"GetNumQuestLogRewardFactions", LuaGetNumQuestLogRewardFactions},
    {"GetQuestLogRewardFactionInfo", LuaGetQuestLogRewardFactionInfo},
    {"GetNextStableSlotCost", LuaApi_GetNextStableSlotCost},
    {"GetSelectedDisplayChannel", LuaGetSelectedDisplayChannel},
    {"GetSummonConfirmAreaName", LuaApi_GetSummonConfirmAreaName},
    {"GetSummonConfirmSummoner", LuaApi_GetSummonConfirmSummoner},
    {"GetSummonConfirmTimeLeft", LuaApi_GetSummonConfirmTimeLeft},
    {"GetTaxiBenchmarkMode", LuaApi_GetTaxiBenchmarkMode},
    {"GetUnitHealthRegenRateFromSpirit", LuaApi_GetUnitHealthRegenRateFromSpirit},
    {"GetUnitManaRegenRateFromSpirit", LuaGetUnitManaRegenRateFromSpirit},
    {"GrantLevel", LuaGrantLevel},
    {"HasLFGRestrictions", LuaApi_HasLFGRestrictions},
    {"IsAtStableMaster", LuaApi_IsAtStableMaster},
    {"IsDesaturateSupported", LuaApi_IsDesaturateSupported},
    {"IsInArenaTeam", LuaApi_IsInArenaTeam},
    {"IsSilenced", LuaIsSilenced},
    {"IsThreatWarningEnabled", LuaApi_IsThreatWarningEnabled},
    {"NoPlayTime", LuaApi_NoPlayTime},
    {"NotWhileDeadError", LuaApi_NotWhileDeadError},
    {"PartialPlayTime", LuaApi_PartialPlayTime},
    {"PlayerCanTeleport", LuaApi_PlayerCanTeleport},
    {"ProcessQuestLogRewardFactions", LuaProcessQuestLogRewardFactions},
    {"RespondInstanceLock", LuaApi_RespondInstanceLock},
    {"SetLayoutMode", LuaApi_SetLayoutMode},
    {"SetSelectedDisplayChannel", LuaSetSelectedDisplayChannel},
    {"SetTaxiBenchmarkMode", LuaApi_SetTaxiBenchmarkMode},
    {"SetUIVisibility", LuaApi_SetUIVisibility},
    {"SilenceMember", LuaSilenceMember},
    {"UnSilenceMember", LuaUnSilenceMember},
    {"UnitIsControlling", LuaApi_UnitIsControlling},
    {"UnitIsPVPSanctuary", LuaApi_UnitIsPVPSanctuary},
    {"UnitIsPartyLeader", LuaUnitIsPartyLeader},
    {"UnitSwitchToVehicleSeat", LuaApi_UnitSwitchToVehicleSeat},
    {"UnitVehicleSkin", LuaApi_UnitVehicleSkin},
    {"UpdateSpells", LuaApi_UpdateSpells},
    {"UploadSettings", LuaApi_UploadSettings},
};

}

openwow::ui::lua::NativeBindingCatalog
FrameXmlGameplayNativeBindings() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "ui.framescript.gameplay",
      openwow::ui::lua::BindingScope::kWorld, kGameplayLuaBindings);
}

}

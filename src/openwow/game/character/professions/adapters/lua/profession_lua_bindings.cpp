#include "openwow/game/character/professions/adapters/lua/profession_lua_bindings.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/lua_composition.h"

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::game::detail {

int LuaGetNumTrainerServices(lua_State* L);
int LuaGetTrainerServiceInfo(lua_State* L);
int LuaGetTrainerServiceCost(lua_State* L);
int LuaGetTrainerServiceLevelReq(lua_State* L);
int LuaGetTrainerServiceSkillReq(lua_State* L);
int LuaGetTrainerServiceSkillLine(lua_State* L);
int LuaGetTrainerServiceIcon(lua_State* L);
int LuaGetTrainerServiceTypeFilter(lua_State* L);
int LuaSetTrainerServiceTypeFilter(lua_State* L);
int LuaBuyTrainerServiceApi(lua_State* L);
int LuaGetTrainerGreetingText(lua_State* L);
int LuaIsTradeskillTrainer(lua_State* L);
int LuaCloseTrainer(lua_State* L);
int LuaGetTrainerServiceStepReq(lua_State* L);
int LuaGetTrainerSkillLineFilter(lua_State* L);
int LuaGetTrainerSkillLines(lua_State* L);
int LuaGetNumTradeSkills(lua_State* L);
int LuaGetTradeSkillLine(lua_State* L);
int LuaGetTradeSkillInfo(lua_State* L);
int LuaGetTradeSkillIcon(lua_State* L);
int LuaGetTradeSkillNumReagents(lua_State* L);
int LuaGetTradeSkillReagentInfo(lua_State* L);
int LuaGetTradeSkillReagentItemLink(lua_State* L);
int LuaGetTradeSkillItemLink(lua_State* L);
int LuaGetTradeSkillCooldown(lua_State* L);
int LuaGetTradeSkillTools(lua_State* L);
int LuaDoTradeSkill(lua_State* L);
int LuaGetTradeSkillSelectionIndex(lua_State* L);
int LuaStopTradeSkillRepeat(lua_State* L);
int LuaCloseTradeSkill(lua_State* L);
int LuaGetTradeSkillListLink(lua_State* L);
int LuaCollapseTradeSkillSubClass(lua_State* L);
int LuaExpandTradeSkillSubClass(lua_State* L);
int LuaGetFirstTradeSkill(lua_State* L);
int LuaIsTradeSkillLinked(lua_State* L);
int LuaGetTradeSkillInvSlots(lua_State* L);
int LuaGetTradeSkillInvSlotFilter(lua_State* L);
int LuaSetTradeSkillInvSlotFilter(lua_State* L);
int LuaGetTradeSkillSubClasses(lua_State* L);
int LuaGetTradeSkillSubClassFilter(lua_State* L);
int LuaSetTradeSkillSubClassFilter(lua_State* L);
int LuaGetSkillLineInfo(lua_State* L);
int LuaGetNumSkillLines(lua_State* L);
int LuaExpandSkillHeader(lua_State* L);
int LuaCollapseSkillHeader(lua_State* L);
int LuaAbandonSkill(lua_State* L);
int LuaCollapseTrainerSkillLine(lua_State* L);
int LuaExpandTrainerSkillLine(lua_State* L);
int LuaSelectTradeSkill(lua_State* L);
int LuaGetTradeSkillDescription(lua_State* L);
int LuaGetTradeSkillNumMade(lua_State* L);
int LuaSetTradeSkillItemNameFilter(lua_State* L);
int LuaGetTradeSkillItemNameFilter(lua_State* L);
int LuaSetTradeSkillItemLevelFilter(lua_State* L);
int LuaGetTradeSkillItemLevelFilter(lua_State* L);
int LuaTradeSkillOnlyShowMakeable(lua_State* L);
int LuaTradeSkillOnlyShowSkillUps(lua_State* L);
int LuaGetSelectedSkill(lua_State* L);
int LuaGetTradeSkillRecipeLink(lua_State* L);
int LuaGetAdjustedSkillPoints(lua_State* L);
int LuaGetTradeskillRepeatCount(lua_State* L);
int LuaGetTrainerSelectionIndex(lua_State* L);
int LuaGetTrainerServiceAbilityReq(lua_State* L);
int LuaGetTrainerServiceDescription(lua_State* L);
int LuaGetTrainerServiceItemLink(lua_State* L);
int LuaGetTrainerServiceNumAbilityReq(lua_State* L);
int LuaGetTrainerServiceStepIncrease(lua_State* L);
int LuaIsTrainerServiceSkillStep(lua_State* L);
int LuaOpenTrainer(lua_State* L);
int LuaSelectTrainerService(lua_State* L);
int LuaAddSkillUp(lua_State* L);
int LuaAcceptSkillUps(lua_State* L);
int LuaCancelSkillUps(lua_State* L);
int LuaRemoveSkillUp(lua_State* L);
int LuaBuySkillTier(lua_State* L);
int LuaSetSelectedSkill(lua_State* L);
int LuaSetTrainerSkillLineFilter(lua_State* L);

}

namespace openwow::ui::game {

using namespace detail;

namespace {

constexpr openwow::ui::LuaGlobalBinding kTradeSkillLuaBindings[] = {
    {"GetNumTrainerServices", LuaGetNumTrainerServices},
    {"GetTrainerServiceInfo", LuaGetTrainerServiceInfo},
    {"GetTrainerServiceCost", LuaGetTrainerServiceCost},
    {"GetTrainerServiceLevelReq", LuaGetTrainerServiceLevelReq},
    {"GetTrainerServiceSkillReq", LuaGetTrainerServiceSkillReq},
    {"GetTrainerServiceSkillLine", LuaGetTrainerServiceSkillLine},
    {"GetTrainerServiceIcon", LuaGetTrainerServiceIcon},
    {"GetTrainerServiceTypeFilter", LuaGetTrainerServiceTypeFilter},
    {"SetTrainerServiceTypeFilter", LuaSetTrainerServiceTypeFilter},
    {"BuyTrainerService", LuaBuyTrainerServiceApi},
    {"GetTrainerGreetingText", LuaGetTrainerGreetingText},
    {"IsTradeskillTrainer", LuaIsTradeskillTrainer},
    {"CloseTrainer", LuaCloseTrainer},
    {"GetTrainerServiceStepReq", LuaGetTrainerServiceStepReq},
    {"GetTrainerSkillLineFilter", LuaGetTrainerSkillLineFilter},
    {"GetTrainerSkillLines", LuaGetTrainerSkillLines},
    {"GetNumTradeSkills", LuaGetNumTradeSkills},
    {"GetTradeSkillLine", LuaGetTradeSkillLine},
    {"GetTradeSkillInfo", LuaGetTradeSkillInfo},
    {"GetTradeSkillIcon", LuaGetTradeSkillIcon},
    {"GetTradeSkillNumReagents", LuaGetTradeSkillNumReagents},
    {"GetTradeSkillReagentInfo", LuaGetTradeSkillReagentInfo},
    {"GetTradeSkillReagentItemLink", LuaGetTradeSkillReagentItemLink},
    {"GetTradeSkillItemLink", LuaGetTradeSkillItemLink},
    {"GetTradeSkillCooldown", LuaGetTradeSkillCooldown},
    {"GetTradeSkillTools", LuaGetTradeSkillTools},
    {"DoTradeSkill", LuaDoTradeSkill},
    {"GetTradeSkillSelectionIndex", LuaGetTradeSkillSelectionIndex},
    {"StopTradeSkillRepeat", LuaStopTradeSkillRepeat},
    {"CloseTradeSkill", LuaCloseTradeSkill},
    {"GetTradeSkillListLink", LuaGetTradeSkillListLink},
    {"CollapseTradeSkillSubClass", LuaCollapseTradeSkillSubClass},
    {"ExpandTradeSkillSubClass", LuaExpandTradeSkillSubClass},
    {"GetFirstTradeSkill", LuaGetFirstTradeSkill},
    {"IsTradeSkillLinked", LuaIsTradeSkillLinked},
    {"GetTradeSkillInvSlots", LuaGetTradeSkillInvSlots},
    {"GetTradeSkillInvSlotFilter", LuaGetTradeSkillInvSlotFilter},
    {"SetTradeSkillInvSlotFilter", LuaSetTradeSkillInvSlotFilter},
    {"GetTradeSkillSubClasses", LuaGetTradeSkillSubClasses},
    {"GetTradeSkillSubClassFilter", LuaGetTradeSkillSubClassFilter},
    {"SetTradeSkillSubClassFilter", LuaSetTradeSkillSubClassFilter},
    {"GetSkillLineInfo", LuaGetSkillLineInfo},
    {"GetNumSkillLines", LuaGetNumSkillLines},
    {"ExpandSkillHeader", LuaExpandSkillHeader},
    {"CollapseSkillHeader", LuaCollapseSkillHeader},
    {"AbandonSkill", LuaAbandonSkill},
    {"CollapseTrainerSkillLine", LuaCollapseTrainerSkillLine},
    {"ExpandTrainerSkillLine", LuaExpandTrainerSkillLine},
    {"SelectTradeSkill", LuaSelectTradeSkill},
    {"GetTradeSkillDescription", LuaGetTradeSkillDescription},
    {"GetTradeSkillNumMade", LuaGetTradeSkillNumMade},
    {"SetTradeSkillItemNameFilter", LuaSetTradeSkillItemNameFilter},
    {"GetTradeSkillItemNameFilter", LuaGetTradeSkillItemNameFilter},
    {"SetTradeSkillItemLevelFilter", LuaSetTradeSkillItemLevelFilter},
    {"GetTradeSkillItemLevelFilter", LuaGetTradeSkillItemLevelFilter},
    {"TradeSkillOnlyShowMakeable", LuaTradeSkillOnlyShowMakeable},
    {"TradeSkillOnlyShowSkillUps", LuaTradeSkillOnlyShowSkillUps},
    {"GetSelectedSkill", LuaGetSelectedSkill},
    {"GetTradeSkillRecipeLink", LuaGetTradeSkillRecipeLink},
    {"GetAdjustedSkillPoints", LuaGetAdjustedSkillPoints},
    {"GetTradeskillRepeatCount", LuaGetTradeskillRepeatCount},
    {"GetTrainerSelectionIndex", LuaGetTrainerSelectionIndex},
    {"GetTrainerServiceAbilityReq", LuaGetTrainerServiceAbilityReq},
    {"GetTrainerServiceDescription", LuaGetTrainerServiceDescription},
    {"GetTrainerServiceItemLink", LuaGetTrainerServiceItemLink},
    {"GetTrainerServiceNumAbilityReq", LuaGetTrainerServiceNumAbilityReq},
    {"GetTrainerServiceStepIncrease", LuaGetTrainerServiceStepIncrease},
    {"IsTrainerServiceSkillStep", LuaIsTrainerServiceSkillStep},
    {"OpenTrainer", LuaOpenTrainer},
    {"SelectTrainerService", LuaSelectTrainerService},
    {"AddSkillUp", LuaAddSkillUp},
    {"AcceptSkillUps", LuaAcceptSkillUps},
    {"CancelSkillUps", LuaCancelSkillUps},
    {"RemoveSkillUp", LuaRemoveSkillUp},
    {"BuySkillTier", LuaBuySkillTier},
    {"SetSelectedSkill", LuaSetSelectedSkill},
    {"SetTrainerSkillLineFilter", LuaSetTrainerSkillLineFilter},
};

}

openwow::ui::lua::NativeBindingCatalog TradeSkillNativeBindingCatalog() {
  return openwow::ui::lua::NativeFunctionCatalog(
      "game.character.professions", openwow::ui::lua::BindingScope::kWorld, kTradeSkillLuaBindings);
}

}

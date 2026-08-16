#pragma once

#include "openwow/ui/game/framescript/core/lua_script_object_access.h"
#include "openwow/ui/game/api/game_lua_api_cvar.h"
#include "openwow/ui/runtime/lua/lua_binding.h"

#include <optional>
#include <string>
#include <variant>

namespace openwow::ui::game {
class TooltipSystem;
}

namespace openwow::ui::lua {

template <>
struct LuaMethodReceiverPolicy<openwow::ui::game::TooltipSystem>
    : LuaFrameMethodReceiverPolicy {
  static constexpr bool enabled = true;

  static void* Identity(lua_State* state, int index) noexcept {
    if (!openwow::ui::game::lua_adapter::IsScriptObjectKindOf(
            state, index,
            openwow::ui::widgets::ScriptObjectType::GameTooltip)) {
      return nullptr;
    }
    return LuaFrameMethodReceiverPolicy::Identity(state, index);
  }
};

}

namespace openwow::ui::game::detail {

struct TooltipLuaValue final {
  std::variant<std::monostate, double, std::string> value;
};

struct TooltipLuaString final {
  std::optional<std::string> value;
};

using TooltipVoidResult =
    std::variant<openwow::ui::lua::NoLuaResults,
                 openwow::ui::lua::LuaUsageError>;
using TooltipOptionalTruthyResult =
    std::variant<openwow::ui::lua::NoLuaResults,
                 openwow::ui::lua::LuaTruthy,
                 openwow::ui::lua::LuaUsageError>;
using TooltipItemQueryResult = std::variant<
    openwow::ui::lua::LuaReturns<openwow::ui::lua::LuaNil,
                                 openwow::ui::lua::LuaNil>,
    openwow::ui::lua::LuaReturns<std::string, std::string>>;
using TooltipSpellQueryResult = std::variant<
    openwow::ui::lua::NoLuaResults,
    openwow::ui::lua::LuaReturns<std::string, std::string, std::uint32_t>,
    openwow::ui::lua::LuaReturns<std::string, std::string, std::uint32_t,
                                 std::string, std::string, std::uint32_t>>;
using TooltipUnitQueryResult = std::variant<
    openwow::ui::lua::NoLuaResults,
    openwow::ui::lua::LuaReturns<std::string, std::string>>;
using TooltipInventoryResult = std::variant<
    openwow::ui::lua::NoLuaResults, openwow::ui::lua::LuaNil,
    openwow::ui::lua::LuaUsageError,
    openwow::ui::lua::LuaReturns<openwow::ui::lua::LuaNil,
                                 openwow::ui::lua::LuaNil>,
    openwow::ui::lua::LuaReturns<openwow::ui::lua::LuaNil,
                                 openwow::ui::lua::LuaNil, double>,
    openwow::ui::lua::LuaReturns<double, openwow::ui::lua::LuaNil>,
    openwow::ui::lua::LuaReturns<double, openwow::ui::lua::LuaNil, double>,
    openwow::ui::lua::LuaReturns<double, openwow::ui::lua::LuaTruthy, double>>;
using TooltipBagResult = std::variant<
    openwow::ui::lua::NoLuaResults, openwow::ui::lua::LuaNil,
    openwow::ui::lua::LuaReturns<openwow::ui::lua::LuaTruthy, std::uint32_t>>;
using TooltipInboxResult = std::variant<openwow::ui::lua::LuaNil,
                                          openwow::ui::lua::LuaTruthy,
                                          openwow::ui::lua::LuaUsageError>;
using TooltipTruthyResult =
    std::variant<openwow::ui::lua::LuaNil, openwow::ui::lua::LuaTruthy,
                 openwow::ui::lua::LuaUsageError>;

void BindTooltipLuaContext(lua_State* lua, TooltipSystem* tooltip);
TooltipSystem& ResolveTooltipLuaReceiver(lua_State* lua);

TooltipVoidResult SetTooltipText(TooltipSystem& tooltip, TooltipLuaString text,
                                 TooltipLuaValue red, TooltipLuaValue green,
                                 TooltipLuaValue blue, TooltipLuaValue,
                                 std::optional<openwow::ui::lua::LuaTruthy> wrap);
TooltipVoidResult AddTooltipTexture(TooltipSystem& tooltip,
                                    TooltipLuaString filename,
                                    TooltipLuaValue min_x,
                                    TooltipLuaValue max_x,
                                    TooltipLuaValue min_y,
                                    TooltipLuaValue max_y);
TooltipVoidResult AppendTooltipText(TooltipSystem& tooltip,
                                    TooltipLuaString text);
openwow::ui::lua::NoLuaResults AddTooltipLine(
    TooltipSystem& tooltip, TooltipLuaString text, TooltipLuaValue red,
    TooltipLuaValue green, TooltipLuaValue blue,
    std::optional<openwow::ui::lua::LuaTruthy> wrap);
openwow::ui::lua::NoLuaResults AddTooltipDoubleLine(
    TooltipSystem& tooltip, TooltipLuaString left, TooltipLuaString right,
    TooltipLuaValue left_red, TooltipLuaValue left_green,
    TooltipLuaValue left_blue, TooltipLuaValue right_red,
    TooltipLuaValue right_green, TooltipLuaValue right_blue,
    std::optional<openwow::ui::lua::LuaTruthy> wrap);
TooltipVoidResult SetTooltipAnchorType(TooltipSystem& tooltip,
                                       TooltipLuaString anchor,
                                       TooltipLuaValue offset_x,
                                       TooltipLuaValue offset_y);
std::string GetTooltipAnchorType(TooltipSystem& tooltip);
openwow::ui::lua::NoLuaResults SetTooltipOwner(TooltipSystem& tooltip,
                                               TooltipLuaValue owner,
                                               TooltipLuaString anchor);
openwow::ui::lua::NoLuaResults ShowTooltip(TooltipSystem& tooltip);
openwow::ui::lua::NoLuaResults HideTooltip(TooltipSystem& tooltip);
openwow::ui::lua::NoLuaResults FadeTooltip(TooltipSystem& tooltip);
openwow::ui::lua::NoLuaResults ClearTooltipLines(TooltipSystem& tooltip);
int GetTooltipNumLines(TooltipSystem& tooltip);
openwow::ui::lua::NoLuaResults SetTooltipMinimumWidth(
    TooltipSystem& tooltip, float width,
    std::optional<openwow::ui::lua::LuaTruthy> force);
openwow::ui::lua::LuaReturns<float, openwow::ui::lua::LuaTruthy>
GetTooltipMinimumWidth(TooltipSystem& tooltip);
openwow::ui::lua::NoLuaResults SetTooltipPadding(TooltipSystem& tooltip,
                                                 std::optional<float> padding);
float GetTooltipPadding(TooltipSystem& tooltip);
TooltipItemQueryResult GetTooltipItem(TooltipSystem& tooltip);
TooltipSpellQueryResult GetTooltipSpell(TooltipSystem& tooltip);
TooltipUnitQueryResult GetTooltipUnit(TooltipSystem& tooltip);
TooltipOptionalTruthyResult SetTooltipUnit(TooltipSystem& tooltip,
                                           TooltipLuaString unit,
                                           std::optional<openwow::ui::lua::LuaTruthy> hide);
TooltipVoidResult SetTooltipUnitAura(TooltipSystem& tooltip,
                                     TooltipLuaString unit,
                                     TooltipLuaValue selector,
                                     TooltipLuaString rank_or_filter,
                                     TooltipLuaString filter);
TooltipVoidResult SetTooltipUnitBuff(TooltipSystem& tooltip,
                                     TooltipLuaString unit,
                                     TooltipLuaValue selector,
                                     TooltipLuaString rank_or_filter,
                                     TooltipLuaString filter);
TooltipVoidResult SetTooltipUnitDebuff(TooltipSystem& tooltip,
                                       TooltipLuaString unit,
                                       TooltipLuaValue selector,
                                       TooltipLuaString rank_or_filter,
                                       TooltipLuaString filter);
TooltipInventoryResult SetTooltipInventoryItem(
    TooltipSystem& tooltip, TooltipLuaString unit, TooltipLuaValue slot,
    std::optional<openwow::ui::lua::LuaTruthy> name_only);
TooltipBagResult SetTooltipBagItem(TooltipSystem& tooltip,
                                   TooltipLuaValue bag,
                                   TooltipLuaValue slot);
openwow::ui::lua::LuaTruthy SetTooltipAuctionSellItem(TooltipSystem& tooltip);
TooltipVoidResult SetTooltipAuctionItem(TooltipSystem& tooltip,
                                        TooltipLuaString list,
                                        TooltipLuaValue index);
openwow::ui::lua::LuaTruthy SetTooltipSendMailItem(TooltipSystem& tooltip,
                                                   TooltipLuaValue slot);
TooltipInboxResult SetTooltipInboxItem(TooltipSystem& tooltip,
                                       TooltipLuaValue message,
                                       TooltipLuaValue attachment);
TooltipVoidResult SetTooltipHyperlink(
    TooltipSystem& tooltip, openwow::ui::lua::RawLuaState lua,
    TooltipLuaValue link);
TooltipVoidResult SetTooltipLootItem(TooltipSystem& tooltip,
                                     TooltipLuaValue slot);
TooltipVoidResult SetTooltipLootRollItem(TooltipSystem& tooltip,
                                         openwow::ui::lua::RawLuaState lua,
                                         TooltipLuaValue roll);
TooltipVoidResult SetTooltipQuestLogSpecialItem(
    TooltipSystem& tooltip, openwow::ui::lua::RawLuaState lua,
    TooltipLuaValue index);
openwow::ui::lua::NoLuaResults SetTooltipQuestRewardSpell(
    TooltipSystem& tooltip);
TooltipVoidResult SetTooltipQuestItem(
    TooltipSystem& tooltip, openwow::ui::lua::RawLuaState lua,
    TooltipLuaValue type, TooltipLuaValue index);
TooltipVoidResult SetTooltipQuestLogItem(
    TooltipSystem& tooltip, openwow::ui::lua::RawLuaState lua,
    TooltipLuaValue type, TooltipLuaValue index);
openwow::ui::lua::NoLuaResults SetTooltipSocketedItem(
    TooltipSystem& tooltip);
openwow::ui::lua::NoLuaResults SetTooltipExistingSocketGem(
    TooltipSystem& tooltip, TooltipLuaValue index,
    std::optional<openwow::ui::lua::LuaTruthy> socket_to_destroy);
TooltipVoidResult SetTooltipSocketGem(
    TooltipSystem& tooltip, openwow::ui::lua::RawLuaState lua,
    TooltipLuaValue index);
TooltipTruthyResult SetTooltipHyperlinkCompareItem(
    TooltipSystem& tooltip, openwow::ui::lua::RawLuaState lua,
    TooltipLuaValue link, TooltipLuaValue offset,
    std::optional<openwow::ui::lua::LuaTruthy> shift_button);
openwow::ui::lua::NoLuaResults SetTooltipTalent(
    TooltipSystem& tooltip, double tab_index, double talent_index,
    std::optional<openwow::ui::lua::LuaTruthy> is_inspect,
    std::optional<openwow::ui::lua::LuaTruthy> is_pet,
    TooltipLuaValue group_index,
    std::optional<openwow::ui::lua::LuaTruthy> is_preview);
TooltipTruthyResult SetTooltipAction(TooltipSystem& tooltip, double slot);
TooltipVoidResult SetTooltipTradeSkillItem(TooltipSystem& tooltip,
                                           TooltipLuaValue recipe,
                                           TooltipLuaValue reagent);
TooltipVoidResult SetTooltipTradePlayerItem(TooltipSystem& tooltip,
                                            TooltipLuaValue slot);
TooltipVoidResult SetTooltipTradeTargetItem(TooltipSystem& tooltip,
                                            TooltipLuaValue slot);
TooltipTruthyResult SetTooltipShapeshift(
    TooltipSystem& tooltip, CVarSystem& cvars,
    openwow::ui::lua::RawLuaState lua, TooltipLuaValue slot);
TooltipTruthyResult SetTooltipPossession(
    TooltipSystem& tooltip, openwow::ui::lua::RawLuaState lua,
    TooltipLuaValue slot);
TooltipVoidResult SetTooltipGlyph(TooltipSystem& tooltip,
                                   TooltipLuaValue glyph_slot,
                                   TooltipLuaValue group_index);
TooltipVoidResult SetTooltipCurrencyToken(
    TooltipSystem& tooltip, openwow::ui::lua::RawLuaState lua,
    TooltipLuaValue index);
TooltipVoidResult SetTooltipBackpackToken(
    TooltipSystem& tooltip, openwow::ui::lua::RawLuaState lua,
    TooltipLuaValue index);

TooltipVoidResult SetTooltipLFGDungeonReward(TooltipSystem& tooltip,
                                             TooltipLuaValue dungeon_id,
                                             TooltipLuaValue loot_index);
TooltipVoidResult SetTooltipLFGCompletionReward(TooltipSystem& tooltip,
                                                TooltipLuaValue loot_index);
TooltipVoidResult SetTooltipBuybackItem(TooltipSystem& tooltip,
                                         TooltipLuaValue slot);
TooltipVoidResult SetTooltipGuildBankItem(TooltipSystem& tooltip,
                                           TooltipLuaValue tab,
                                           TooltipLuaValue slot);
TooltipVoidResult SetTooltipEquipmentSet(
    TooltipSystem& tooltip, openwow::ui::lua::RawLuaState lua,
    TooltipLuaValue set_name);
TooltipTruthyResult SetTooltipPetAction(
    TooltipSystem& tooltip, openwow::ui::lua::RawLuaState lua,
    TooltipLuaValue slot);
TooltipVoidResult SetTooltipTotem(
    TooltipSystem& tooltip, openwow::ui::lua::RawLuaState lua,
    TooltipLuaValue slot);

inline constexpr openwow::ui::lua::ConversionPolicy kTooltipLuaConversion{
    openwow::ui::lua::IntegralConversion::kTruncate,
    true, true, true, true, true};

}

namespace openwow::ui::lua {

template <ConversionPolicy Policy>
struct LuaConverter<openwow::ui::game::detail::TooltipLuaValue, Policy> {
  using Storage = openwow::ui::game::detail::TooltipLuaValue;
  static bool Valid(lua_State*, int) noexcept { return true; }
  static Storage Read(lua_State* state, int index);
  static Storage Argument(Storage value) noexcept { return value; }
};

template <ConversionPolicy Policy>
struct LuaConverter<openwow::ui::game::detail::TooltipLuaString, Policy> {
  using Storage = openwow::ui::game::detail::TooltipLuaString;
  static bool Valid(lua_State*, int) noexcept { return true; }
  static Storage Read(lua_State* state, int index);
  static Storage Argument(Storage value) noexcept { return value; }
};

}

namespace openwow::ui::lua::detail {

template <>
struct IsOptional<openwow::ui::game::detail::TooltipLuaValue>
    : std::true_type {};
template <>
struct IsOptional<openwow::ui::game::detail::TooltipLuaString>
    : std::true_type {};

}

namespace openwow::ui::game::detail {

#define OPENWOW_TOOLTIP_BINDING(symbol, function, name) \
  inline constexpr auto symbol =                        \
      openwow::ui::lua::bind<&function, kTooltipLuaConversion>(name)

OPENWOW_TOOLTIP_BINDING(kSetTooltipText, SetTooltipText, "SetText");
OPENWOW_TOOLTIP_BINDING(kAddTooltipTexture, AddTooltipTexture, "AddTexture");
OPENWOW_TOOLTIP_BINDING(kAppendTooltipText, AppendTooltipText, "AppendText");
OPENWOW_TOOLTIP_BINDING(kAddTooltipLine, AddTooltipLine, "AddLine");
OPENWOW_TOOLTIP_BINDING(kAddTooltipDoubleLine, AddTooltipDoubleLine,
                        "AddDoubleLine");
OPENWOW_TOOLTIP_BINDING(kSetTooltipAnchorType, SetTooltipAnchorType,
                        "SetAnchorType");
OPENWOW_TOOLTIP_BINDING(kGetTooltipAnchorType, GetTooltipAnchorType,
                        "GetAnchorType");
OPENWOW_TOOLTIP_BINDING(kSetTooltipOwner, SetTooltipOwner, "SetOwner");
OPENWOW_TOOLTIP_BINDING(kShowTooltip, ShowTooltip, "Show");
OPENWOW_TOOLTIP_BINDING(kHideTooltip, HideTooltip, "Hide");
OPENWOW_TOOLTIP_BINDING(kFadeTooltip, FadeTooltip, "FadeOut");
OPENWOW_TOOLTIP_BINDING(kClearTooltipLines, ClearTooltipLines, "ClearLines");
OPENWOW_TOOLTIP_BINDING(kGetTooltipNumLines, GetTooltipNumLines, "NumLines");
OPENWOW_TOOLTIP_BINDING(kSetTooltipMinimumWidth, SetTooltipMinimumWidth,
                        "SetMinimumWidth");
OPENWOW_TOOLTIP_BINDING(kGetTooltipMinimumWidth, GetTooltipMinimumWidth,
                        "GetMinimumWidth");
OPENWOW_TOOLTIP_BINDING(kSetTooltipPadding, SetTooltipPadding, "SetPadding");
OPENWOW_TOOLTIP_BINDING(kGetTooltipPadding, GetTooltipPadding, "GetPadding");
OPENWOW_TOOLTIP_BINDING(kGetTooltipItem, GetTooltipItem, "GetItem");
OPENWOW_TOOLTIP_BINDING(kGetTooltipSpell, GetTooltipSpell, "GetSpell");
OPENWOW_TOOLTIP_BINDING(kGetTooltipUnit, GetTooltipUnit, "GetUnit");
OPENWOW_TOOLTIP_BINDING(kSetTooltipUnit, SetTooltipUnit, "SetUnit");
OPENWOW_TOOLTIP_BINDING(kSetTooltipUnitAura, SetTooltipUnitAura, "SetUnitAura");
OPENWOW_TOOLTIP_BINDING(kSetTooltipUnitBuff, SetTooltipUnitBuff, "SetUnitBuff");
OPENWOW_TOOLTIP_BINDING(kSetTooltipUnitDebuff, SetTooltipUnitDebuff,
                        "SetUnitDebuff");
OPENWOW_TOOLTIP_BINDING(kSetTooltipInventoryItem, SetTooltipInventoryItem,
                        "SetInventoryItem");
OPENWOW_TOOLTIP_BINDING(kSetTooltipBagItem, SetTooltipBagItem, "SetBagItem");
OPENWOW_TOOLTIP_BINDING(kSetTooltipAuctionSellItem, SetTooltipAuctionSellItem,
                        "SetAuctionSellItem");
OPENWOW_TOOLTIP_BINDING(kSetTooltipAuctionItem, SetTooltipAuctionItem,
                        "SetAuctionItem");
OPENWOW_TOOLTIP_BINDING(kSetTooltipSendMailItem, SetTooltipSendMailItem,
                        "SetSendMailItem");
OPENWOW_TOOLTIP_BINDING(kSetTooltipInboxItem, SetTooltipInboxItem,
                        "SetInboxItem");
OPENWOW_TOOLTIP_BINDING(kSetTooltipHyperlink, SetTooltipHyperlink,
                        "SetHyperlink");
OPENWOW_TOOLTIP_BINDING(kSetTooltipLootItem, SetTooltipLootItem,
                        "SetLootItem");
OPENWOW_TOOLTIP_BINDING(kSetTooltipLootRollItem, SetTooltipLootRollItem,
                        "SetLootRollItem");
OPENWOW_TOOLTIP_BINDING(kSetTooltipQuestLogSpecialItem,
                        SetTooltipQuestLogSpecialItem,
                        "SetQuestLogSpecialItem");
OPENWOW_TOOLTIP_BINDING(kSetTooltipQuestRewardSpell,
                        SetTooltipQuestRewardSpell,
                        "SetQuestRewardSpell");
OPENWOW_TOOLTIP_BINDING(kSetTooltipQuestItem, SetTooltipQuestItem,
                        "SetQuestItem");
OPENWOW_TOOLTIP_BINDING(kSetTooltipQuestLogItem, SetTooltipQuestLogItem,
                        "SetQuestLogItem");
OPENWOW_TOOLTIP_BINDING(kSetTooltipSocketedItem, SetTooltipSocketedItem,
                        "SetSocketedItem");
OPENWOW_TOOLTIP_BINDING(kSetTooltipExistingSocketGem,
                        SetTooltipExistingSocketGem,
                        "SetExistingSocketGem");
OPENWOW_TOOLTIP_BINDING(kSetTooltipSocketGem, SetTooltipSocketGem,
                        "SetSocketGem");
OPENWOW_TOOLTIP_BINDING(kSetTooltipHyperlinkCompareItem,
                        SetTooltipHyperlinkCompareItem,
                        "SetHyperlinkCompareItem");
OPENWOW_TOOLTIP_BINDING(kSetTooltipTalent, SetTooltipTalent, "SetTalent");
OPENWOW_TOOLTIP_BINDING(kSetTooltipAction, SetTooltipAction, "SetAction");
OPENWOW_TOOLTIP_BINDING(kSetTooltipTradeSkillItem, SetTooltipTradeSkillItem,
                        "SetTradeSkillItem");
OPENWOW_TOOLTIP_BINDING(kSetTooltipTradePlayerItem, SetTooltipTradePlayerItem,
                        "SetTradePlayerItem");
OPENWOW_TOOLTIP_BINDING(kSetTooltipTradeTargetItem, SetTooltipTradeTargetItem,
                        "SetTradeTargetItem");
OPENWOW_TOOLTIP_BINDING(kSetTooltipShapeshift, SetTooltipShapeshift,
                        "SetShapeshift");
OPENWOW_TOOLTIP_BINDING(kSetTooltipPossession, SetTooltipPossession,
                        "SetPossession");
OPENWOW_TOOLTIP_BINDING(kSetTooltipGlyph, SetTooltipGlyph, "SetGlyph");
OPENWOW_TOOLTIP_BINDING(kSetTooltipCurrencyToken, SetTooltipCurrencyToken,
                        "SetCurrencyToken");
OPENWOW_TOOLTIP_BINDING(kSetTooltipBackpackToken, SetTooltipBackpackToken,
                        "SetBackpackToken");
OPENWOW_TOOLTIP_BINDING(kSetTooltipLFGDungeonReward,
                        SetTooltipLFGDungeonReward,
                        "SetLFGDungeonReward");
OPENWOW_TOOLTIP_BINDING(kSetTooltipLFGCompletionReward,
                        SetTooltipLFGCompletionReward,
                        "SetLFGCompletionReward");
OPENWOW_TOOLTIP_BINDING(kSetTooltipBuybackItem, SetTooltipBuybackItem,
                        "SetBuybackItem");
OPENWOW_TOOLTIP_BINDING(kSetTooltipGuildBankItem, SetTooltipGuildBankItem,
                        "SetGuildBankItem");
OPENWOW_TOOLTIP_BINDING(kSetTooltipEquipmentSet, SetTooltipEquipmentSet,
                        "SetEquipmentSet");
OPENWOW_TOOLTIP_BINDING(kSetTooltipPetAction, SetTooltipPetAction,
                        "SetPetAction");
OPENWOW_TOOLTIP_BINDING(kSetTooltipTotem, SetTooltipTotem, "SetTotem");

#undef OPENWOW_TOOLTIP_BINDING

}

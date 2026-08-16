#pragma once

#include "openwow/game/actions/application/action_assignment_runtime.h"
#include "openwow/game/actions/macros/model/macro_id.h"
#include "openwow/game/spells/model/spell_values.h"

#include <cstddef>
#include <cstdint>

struct lua_State;

namespace openwow::game {
class WorldSession;
}

namespace openwow::ui::game::detail {

int LuaGetActionInfo(lua_State* L);
int LuaGetActionTexture(lua_State* L);
int LuaGetActionCount(lua_State* L);
int LuaGetActionCooldown(lua_State* L);
int LuaGetActionAutocast(lua_State* L);
int LuaHasAction(lua_State* L);
int LuaIsUsableAction(lua_State* L);
int LuaIsCurrentAction(lua_State* L);
int LuaIsAutoRepeatAction(lua_State* L);
int LuaActionHasRange(lua_State* L);
int LuaIsAttackAction(lua_State* L);
int LuaIsConsumableAction(lua_State* L);
int LuaIsStackableAction(lua_State* L);
int LuaGetActionText(lua_State* L);
int LuaGetMultiCastBarOffset(lua_State* L);

int LuaUseAction(lua_State* L);

[[nodiscard]] std::uint64_t ActionUseTransitionSequence() noexcept;
void ResetActionUseTransitionSequence() noexcept;
int LuaPickupAction(lua_State* L);
int LuaPlaceAction(lua_State* L);
[[nodiscard]] bool RefreshAllActionSlotValidation(
    openwow::game::WorldSession& session);
void ResetActionBarRuntimeState(openwow::game::WorldSession& session);
void RefreshPetActionBarState(openwow::game::WorldSession& session);
void RefreshGeneratedActionBarState(openwow::game::WorldSession& session);
void RefreshActionBarBootstrapState(openwow::game::WorldSession& session);
[[nodiscard]] openwow::game::ActionPresentationEntry GetResolvedActionButton(
    openwow::game::WorldSession& session,
    std::size_t slot_index);
[[nodiscard]] bool ResolvePetActionBarSlotIndex(
    openwow::game::WorldSession& session,
    std::size_t slot_index,
    std::size_t* out_pet_slot_index = nullptr);
[[nodiscard]] openwow::game::ActionButtonUsabilityState
ComputeActionSlotUsability(openwow::game::WorldSession& session,
                           std::size_t slot_index);
[[nodiscard]] std::uint32_t ResolveSpellLikeActionIdForValidation(
    openwow::game::WorldSession& session,
    const openwow::game::ActionPresentationEntry& button,
    std::size_t slot_index,
    bool* is_pet_action = nullptr);
[[nodiscard]] bool RefreshActionSlotsForChangedItemEntry(
    openwow::game::WorldSession& session,
    std::uint32_t item_entry);
[[nodiscard]] bool RefreshActionSlotsForChangedMacro(
    openwow::game::WorldSession& session,
    openwow::game::actions::macros::MacroId macro_id);
[[nodiscard]] bool RefreshActionSlotsForEquipmentSet(
    openwow::game::WorldSession& session,
    std::uint32_t set_id);
[[nodiscard]] bool RefreshActionSlotsForSpellId(
    openwow::game::WorldSession& session,
    std::uint32_t spell_id);
[[nodiscard]] bool RefreshActionSlotsForAttackActions(
    openwow::game::WorldSession& session);
[[nodiscard]] bool RefreshActionSlotsForRangedAttackActions(
    openwow::game::WorldSession& session);
[[nodiscard]] bool ClearActionSlotsForEquipmentSet(
    openwow::game::WorldSession& session,
    std::uint32_t set_id);

[[nodiscard]] bool IsPickupPlaceBlockedActionSlot(std::size_t slot_index);
[[nodiscard]] bool IsMultiCastActionSlot(std::size_t slot_index);
[[nodiscard]] openwow::game::spells::TotemCategoryId
GetMultiCastRequiredTotemCategoryForElementIndex(
    std::size_t element_index);
[[nodiscard]] std::uint32_t GetMultiCastSlotMaskForTotemCategory(
    openwow::game::spells::TotemCategoryId totem_category);
[[nodiscard]] openwow::game::spells::TotemCategoryId
GetMultiCastRequiredTotemCategory(
    std::size_t slot_index);
enum class MultiCastSlotValidationResult : std::uint8_t {
  kReject,
  kAccept,
  kMissingSpellData,
};
[[nodiscard]] MultiCastSlotValidationResult ValidateMultiCastSpellPlacement(
    lua_State* L,
    std::size_t slot_index,
    std::uint32_t spell_id,
    openwow::game::spells::TotemCategoryId* out_category = nullptr);
[[nodiscard]] bool SlotAcceptsMultiCastSpell(lua_State* L,
                                             std::size_t slot_index,
                                             std::uint32_t spell_id,
                                             openwow::game::spells::TotemCategoryId*
                                                 out_category = nullptr);
void ReportMultiCastSlotError(lua_State* L, std::size_t slot_index);

int LuaGetSpellInfo(lua_State* L);
int LuaGetSpellCooldown(lua_State* L);
int LuaGetSpellCount(lua_State* L);
int LuaIsSpellKnown(lua_State* L);
int LuaGetNumSpellTabs(lua_State* L);
int LuaGetSpellTabInfo(lua_State* L);

int LuaGetBonusBarOffset(lua_State* L);
int LuaGetActionBarPage(lua_State* L);
int LuaChangeActionBarPage(lua_State* L);

int LuaGetActionBarToggles(lua_State* L);
int LuaIsActionInRange(lua_State* L);
int LuaIsEquippedAction(lua_State* L);
int LuaSetActionBarToggles(lua_State* L);

}

#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/game/actions/macros/adapters/ui/macro_cursor_controller.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/game/actions/application/action_assignment_runtime.h"
#include "openwow/game/inventory/search/action_item_inventory_search.h"
#include "openwow/game/action_validation_utils.h"
#include "openwow/game/cooldown_tracker.h"
#include "openwow/game/inventory/equipment/equipment_sets.h"
#include "openwow/game/generated_action_bar.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/inventory/adapters/ui/item_cursor_pickup_controller.h"
#include "openwow/game/inventory/adapters/ui/item_spell_target_controller.h"
#include "openwow/game/inventory/adapters/ui/item_target_cursor_presenter.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/localization.h"
#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/game/actions/macros/adapters/retail/macro_input_adapter.h"
#include "openwow/game/actions/bindings/adapters/retail/modified_click_adapter.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/spell_cooldown_state.h"
#include "openwow/game/power_display.h"
#include "openwow/game/spell_book.h"
#include "openwow/game/spell_action.h"
#include "openwow/game/spell_runtime_values.h"
#include "openwow/game/spell_target_resolver.h"
#include "openwow/game/spell_target_validation.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/spell_usability.h"
#include "openwow/game/spellbook_frame.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/ui/game/cursor_texture_resolver.h"
#include "openwow/ui/game/error_message.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/api/game_lua_api_companion.h"
#include "openwow/game/inventory/equipment/adapters/lua/equipment_lua_api.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/game/actions/macros/adapters/lua/macro_lua_api.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/lua_numeric.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <optional>

#include <SDL.h>

namespace openwow::ui::game::detail {

namespace item_targeting = ::openwow::game::inventory::ui;

namespace {

using ::openwow::game::ActionAssignmentRuntime;
using ::openwow::game::ActionPresentationEntry;
using ::openwow::game::ActionPresentationKind;
using ::openwow::game::ActionButtonUsabilityState;
using ::openwow::game::CGUnit_C;
using ::openwow::game::PlayerInventoryReplica;
using ::openwow::game::ItemInstance;
using ::openwow::game::MacroCatalog;
using ::openwow::game::SpellQueryBridge;
using ::openwow::game::spells::TotemCategoryId;
using ::openwow::game::SpellUsabilityChecker;
using ::openwow::game::WorldSession;
using ::openwow::game::actions::held_cursor::HeldCursor;

constexpr std::size_t kActionSlotCount = ActionAssignmentRuntime::kMaxActionButtons;
constexpr std::size_t kFirstPickupPlaceBlockedActionSlot = 120;
constexpr std::size_t kFirstMultiCastActionSlot = 132;
constexpr std::uint32_t kSpellAttrEx7RestrictMultiCastActionBarPlacement = 0x20u;
std::atomic<std::uint64_t> s_action_use_transition_sequence{0u};

void PublishActionUseTransition() noexcept {
  s_action_use_transition_sequence.fetch_add(1u, std::memory_order_relaxed);
}

bool CanPerformActionBarProtectedAction() {
  return GameUI_CanPerformProtectedAction(
             protected_action_kind::kActionSlotMutation) != 0;
}

constexpr float kRetailActionRangeThreshold = 0x1p-22F;

bool HasRetailActionRange(const ::openwow::game::SpellTargetRangeWindow &window) {
  const float absolute_minimum = std::fabs(window.min_range);
  if (std::isnan(absolute_minimum) ||
      absolute_minimum >= kRetailActionRangeThreshold) {
    return true;
  }

  return std::fabs(window.max_range) >= kRetailActionRangeThreshold;
}

using ActionCooldownResult = ::openwow::game::SpellCooldownState;

struct ActionCooldownResolution {
  std::optional<ActionCooldownResult> cooldown;
  double enabled{0.0};
};

bool HasPetActionSpellId(const WorldSession &session, const std::uint32_t spell_id) {
  if (spell_id == 0) {
    return false;
  }

  const auto &action_bar = session.pet().pet_bar().action_bar;
  return std::ranges::any_of(
      action_bar, [spell_id](const openwow::game::PetActionButton &action) {
        return IsPetSpellActionKind(action.ActionKind()) &&
               action.ActionId() == spell_id;
      });
}

bool IsActionSlotInRange(int slot) {
  return slot >= 1 && slot <= static_cast<int>(kActionSlotCount);
}

const openwow::data::dbc::SpellShapeshiftFormEntry *
LookupActiveShapeshiftFormEntry(WorldSession &session) {
  const auto *dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return nullptr;
  }

  const auto *player = session.objects().GetActivePlayer();
  if (player == nullptr) {
    return nullptr;
  }

  const auto form_id = static_cast<std::uint32_t>(player->Animation().GetShapeshiftForm());
  if (form_id == 0) {
    return nullptr;
  }

  return dbc->spell_shapeshift_form().LookupEntry(form_id);
}

const openwow::data::dbc::OverrideSpellDataEntry *
LookupActiveOverrideSpellDataEntry(WorldSession &session) {
  const auto *dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return nullptr;
  }

  const auto *player = session.objects().GetActivePlayer();
  if (player == nullptr) {
    return nullptr;
  }

  const auto override_spell_data_id = player->GetOverrideSpellDataId();
  if (override_spell_data_id == 0) {
    return nullptr;
  }

  return dbc->override_spell_data().LookupEntry(override_spell_data_id);
}

void SyncGeneratedActionButton(
    ActionAssignmentRuntime& assignments, const std::size_t slot_index,
    const ActionPresentationEntry& desired_button) {
  const auto& current_button = assignments.GetPresentationEntry(slot_index);
  if (current_button.ToAssignedAction() ==
      desired_button.ToAssignedAction()) {
    return;
  }

  if (desired_button.IsEmpty()) {
    assignments.ClearAssignment(slot_index);
  } else {
    assignments.SetPresentationEntry(slot_index, desired_button);
  }
}

void SyncGeneratedActionSlots(WorldSession &session) {
  auto& assignments = session.action_assignments();
  const auto *override_spell_data = LookupActiveOverrideSpellDataEntry(session);
  const auto *shapeshift_form = LookupActiveShapeshiftFormEntry(session);
  const auto &pet_bar = session.pet().pet_bar();
  const auto generated_bar =
      ::openwow::game::DescribeGeneratedActionBar(override_spell_data, shapeshift_form, &pet_bar);

  const auto slot_source =
      generated_bar.source == ::openwow::game::GeneratedActionBarSource::kNone && pet_bar.active
          ? ::openwow::game::GeneratedActionBarSource::kPet
          : generated_bar.source;

  for (std::size_t index = 0; index < ::openwow::game::kGeneratedActionBarSlotCount; ++index) {
    SyncGeneratedActionButton(
        assignments, kFirstPickupPlaceBlockedActionSlot + index,
        ::openwow::game::GetGeneratedActionButton(slot_source, override_spell_data,
                                                  shapeshift_form, &pet_bar, index));
  }
}

std::uint32_t ResolveActionButtonItemActionId(
    const MacroCatalog &macros, const ActionPresentationEntry &button) {
  if (button.type == ActionPresentationKind::kItem) {
    return button.action;
  }

  if ((button.type != ActionPresentationKind::kMacro && button.type != ActionPresentationKind::kCompanionMacro) ||
      button.action == 0) {
    return 0;
  }

  const auto macro = macros.FindMacro(
      ::openwow::game::actions::macros::MacroId(button.action));
  return macro ? macro->resolved_item_id : 0u;
}

const openwow::game::PetActionButton *
ResolvePetActionButton(WorldSession &session, const std::size_t slot_index,
                       std::size_t *out_pet_slot_index = nullptr) {
  if (out_pet_slot_index != nullptr) {
    *out_pet_slot_index = 0;
  }

  if (slot_index < kFirstPickupPlaceBlockedActionSlot ||
      slot_index >= kFirstPickupPlaceBlockedActionSlot + 10) {
    return nullptr;
  }

  const auto button = GetResolvedActionButton(session, slot_index);
  if (button.type != ActionPresentationKind::kPet) {
    return nullptr;
  }

  if (session.pet().GetPrimaryPetGuid().IsEmpty()) {
    return nullptr;
  }

  const auto pet_slot_index = slot_index - kFirstPickupPlaceBlockedActionSlot;
  const auto &pet_action = session.pet().pet_bar().action_bar[pet_slot_index];
  if (pet_action.raw == 0) {
    return nullptr;
  }

  if (out_pet_slot_index != nullptr) {
    *out_pet_slot_index = pet_slot_index;
  }

  return &pet_action;
}

std::uint32_t ResolveActionButtonSpellLikeActionId(lua_State *L, WorldSession &session,
                                                   const std::size_t slot_index,
                                                   const ActionPresentationEntry &button,
                                                   bool *out_is_pet_action = nullptr,
                                                   std::size_t *out_pet_slot_index = nullptr) {
  if (out_is_pet_action != nullptr) {
    *out_is_pet_action = false;
  }
  if (out_pet_slot_index != nullptr) {
    *out_pet_slot_index = 0;
  }

  switch (button.type) {
  case ActionPresentationKind::kSpell:
    return button.action;

  case ActionPresentationKind::kItem: {
    auto *dbc = session.GetDbcLoader();
    if (dbc == nullptr) {
      dbc = GetDbcLoader(L);
    }
    return ResolveItemUseSpellWithEquippedFallback(
        session.inventory_replica(), session.item_definitions(), button.action, dbc);
  }

  case ActionPresentationKind::kMacro:
  case ActionPresentationKind::kCompanionMacro: {
    if (button.action == 0) {
      return 0;
    }

    const auto macro = session.macros().FindMacro(
        ::openwow::game::actions::macros::MacroId(button.action));
    if (!macro) {
      return 0;
    }

    if (macro->resolved_spell_id > 0) {
      return static_cast<std::uint32_t>(macro->resolved_spell_id);
    }

    if (macro->resolved_item_id == 0) {
      return 0;
    }

    auto *dbc = session.GetDbcLoader();
    if (dbc == nullptr) {
      dbc = GetDbcLoader(L);
    }
    return ResolveItemUseSpellWithEquippedFallback(
        session.inventory_replica(), session.item_definitions(),
        macro->resolved_item_id, dbc);
  }

  case ActionPresentationKind::kPet: {
    std::size_t pet_slot_index = 0;
    const auto *pet_action = ResolvePetActionButton(session, slot_index, &pet_slot_index);
    if (pet_action == nullptr || !IsPetSpellActionKind(pet_action->ActionKind())) {
      return 0;
    }

    if (out_is_pet_action != nullptr) {
      *out_is_pet_action = true;
    }
    if (out_pet_slot_index != nullptr) {
      *out_pet_slot_index = pet_slot_index;
    }
    return pet_action->ActionId();
  }

  default:
    return 0;
  }
}

std::optional<ActionCooldownResult> ResolveTrackedItemActionCooldown(const std::uint32_t item_id) {
  const auto *cooldown = CooldownTracker::Get().GetItemCooldown(item_id);
  if (cooldown == nullptr || cooldown->duration == 0) {
    return std::nullopt;
  }

  const auto current_time_ms = openwow::core::GameClock::GetTickCount32();
  if (cooldown->IsReady(current_time_ms)) {
    return std::nullopt;
  }

  return ActionCooldownResult{
      .start_time_s = static_cast<double>(cooldown->start_time) / 1000.0,
      .duration_s = static_cast<double>(cooldown->duration) / 1000.0,
      .enabled = 1.0,
  };
}

std::optional<ActionCooldownResult> ResolvePetActionCooldown(const WorldSession &session,
                                                             const std::size_t pet_slot_index) {
  const auto &pet_bar = session.pet().pet_bar();
  if (pet_slot_index >= 10) {
    return std::nullopt;
  }

  const auto &action_button = pet_bar.action_bar[pet_slot_index];
  if (!IsPetSpellActionKind(action_button.ActionKind())) {
    return std::nullopt;
  }

  const auto spell_id = action_button.ActionId();
  return ::openwow::game::ResolvePetBarSpellCooldown(pet_bar, spell_id,
                                                     session.GetDbcLoader());
}

ActionCooldownResolution ResolveActionCooldown(lua_State *L, WorldSession &session,
                                               const std::size_t slot_index) {
  const auto button = GetResolvedActionButton(session, slot_index);
  if (button.IsEmpty()) {
    return {};
  }

  const auto item_id =
      ResolveActionButtonItemActionId(session.macros(), button);
  if (item_id != 0) {
    const auto *item_template = RequireItemDefinitions(L).GetItem(item_id);
    if (item_template == nullptr) {
      return {};
    }

    std::optional<ActionCooldownResult> best_cooldown = ResolveTrackedItemActionCooldown(item_id);

    auto *dbc = session.GetDbcLoader();
    if (dbc == nullptr) {
      dbc = GetDbcLoader(L);
    }
    const auto spell_id = ResolveItemUseSpellIdWithEquippedFallback(
        session.inventory_replica(), item_id, item_template, dbc);
    if (spell_id == 0) {
      return {};
    }

    if (const auto spellbook_cooldown =
            ::openwow::game::ResolveSpellbookCooldown(session.spell_book(), spell_id);
        spellbook_cooldown.has_value() &&
        (!best_cooldown.has_value() ||
         spellbook_cooldown->ExpiresAt() > best_cooldown->ExpiresAt())) {
      best_cooldown = spellbook_cooldown;
    }

    return {.cooldown = std::move(best_cooldown), .enabled = 1.0};
  }

  bool is_pet_action = false;
  std::size_t pet_slot_index = 0;
  const auto spell_id = ResolveActionButtonSpellLikeActionId(L, session, slot_index, button,
                                                             &is_pet_action, &pet_slot_index);
  if (spell_id == 0) {
    return {.cooldown = std::nullopt,
            .enabled = button.type == ActionPresentationKind::kPet ? 1.0 : 0.0};
  }

  if (is_pet_action) {
    return {.cooldown = ResolvePetActionCooldown(session, pet_slot_index), .enabled = 1.0};
  }

  return {.cooldown = ::openwow::game::ResolveSpellbookCooldown(
              session.spell_book(), spell_id),
          .enabled = 1.0};
}

void PushActionCooldownResult(lua_State *L, const ActionCooldownResult &cooldown) {
  lua_pushnumber(L, static_cast<lua_Number>(cooldown.start_time_s));
  lua_pushnumber(L, static_cast<lua_Number>(cooldown.duration_s));
  lua_pushnumber(L, static_cast<lua_Number>(cooldown.enabled));
}

void PushEmptyActionCooldownResult(lua_State *L, const double enabled) {
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, static_cast<lua_Number>(enabled));
}

const char *ResolvePetActionTextureToken(const openwow::game::PetActionButton &action) {
  switch (action.ActionKind()) {
  case 6:
    switch (action.ActionId()) {
    case static_cast<std::uint32_t>(::openwow::game::PetReactState::kPassive):
      return "PET_PASSIVE_TEXTURE";
    case static_cast<std::uint32_t>(::openwow::game::PetReactState::kDefensive):
      return "PET_DEFENSIVE_TEXTURE";
    case static_cast<std::uint32_t>(::openwow::game::PetReactState::kAggressive):
      return "PET_AGGRESSIVE_TEXTURE";
    default:
      return nullptr;
    }

  case 7:
    switch (action.ActionId()) {
    case static_cast<std::uint32_t>(::openwow::game::PetCommandState::kStay):
      return "PET_WAIT_TEXTURE";
    case static_cast<std::uint32_t>(::openwow::game::PetCommandState::kFollow):
      return "PET_FOLLOW_TEXTURE";
    case static_cast<std::uint32_t>(::openwow::game::PetCommandState::kAttack):
      return "PET_ATTACK_TEXTURE";
    case static_cast<std::uint32_t>(::openwow::game::PetCommandState::kAbandon):
      return "PET_DISMISS_TEXTURE";
    default:
      return nullptr;
    }

  default:
    return nullptr;
  }
}

}

std::uint64_t ActionUseTransitionSequence() noexcept {
  return s_action_use_transition_sequence.load(std::memory_order_relaxed);
}

void ResetActionUseTransitionSequence() noexcept {
  s_action_use_transition_sequence.store(0u, std::memory_order_relaxed);
}

ActionPresentationEntry GetResolvedActionButton(WorldSession &session, std::size_t slot_index) {
  SyncGeneratedActionSlots(session);
  return session.action_assignments().GetPresentationEntry(slot_index);
}

bool ResolvePetActionBarSlotIndex(WorldSession &session, std::size_t slot_index,
                                  std::size_t *out_pet_slot_index) {
  return ResolvePetActionButton(session, slot_index, out_pet_slot_index) != nullptr;
}

bool IsPickupPlaceBlockedActionSlot(std::size_t slot_index) {
  return slot_index >= kFirstPickupPlaceBlockedActionSlot && slot_index < kFirstMultiCastActionSlot;
}

bool IsMultiCastActionSlot(std::size_t slot_index) {
  return slot_index >= kFirstMultiCastActionSlot && slot_index < kActionSlotCount;
}

TotemCategoryId GetMultiCastRequiredTotemCategoryForElementIndex(std::size_t element_index) {
  switch (element_index % 4) {
  case 0:
    return TotemCategoryId{4};
  case 1:
    return TotemCategoryId{2};
  case 2:
    return TotemCategoryId{5};
  case 3:
    return TotemCategoryId{3};
  default:
    return TotemCategoryId{0};
  }
}

std::uint32_t GetMultiCastSlotMaskForTotemCategory(const TotemCategoryId totem_category) {
  return ::openwow::game::SpellBookFrame::MultiCastTotemCategoryToSlotMask(
      totem_category.value());
}

TotemCategoryId GetMultiCastRequiredTotemCategory(std::size_t slot_index) {
  if (!IsMultiCastActionSlot(slot_index)) {
    return TotemCategoryId{0};
  }

  return GetMultiCastRequiredTotemCategoryForElementIndex(slot_index - kFirstMultiCastActionSlot);
}

struct MultiCastSpellPlacementData {
  bool has_restricted_slot_requirements{false};
  std::array<TotemCategoryId, 2> totem_categories{
      TotemCategoryId{0}, TotemCategoryId{0}};
};

std::optional<MultiCastSpellPlacementData>
ResolveMultiCastSpellPlacementData(lua_State *L, const std::uint32_t spell_id) {
  if (spell_id == 0) {
    return std::nullopt;
  }

  if (const auto query = SpellQueryBridge::Get().Query(spell_id); query.has_value()) {
    MultiCastSpellPlacementData placement{};
    placement.has_restricted_slot_requirements =
        query->restrictsMultiCastActionBarPlacement;
    placement.totem_categories = query->multiCastTotemCategories;
    return placement;
  }

  const auto *spell = cursor_texture::GetSpellEntry(L, spell_id);
  if (spell == nullptr) {
    return std::nullopt;
  }

  MultiCastSpellPlacementData placement{};
  placement.has_restricted_slot_requirements =
      (spell->attributes_ex7 &
       kSpellAttrEx7RestrictMultiCastActionBarPlacement) != 0;
  std::transform(spell->totem_category.begin(), spell->totem_category.end(),
                 placement.totem_categories.begin(),
                 [](const std::uint32_t category) {
                   return TotemCategoryId{category};
                 });
  return placement;
}

MultiCastSlotValidationResult ValidateMultiCastSpellPlacement(lua_State *L, std::size_t slot_index,
                                                              std::uint32_t spell_id,
                                                              TotemCategoryId *out_category) {
  const auto required_category = GetMultiCastRequiredTotemCategory(slot_index);
  if (out_category) {
    *out_category = required_category;
  }
  if (!required_category.IsValid() || spell_id == 0) {
    return MultiCastSlotValidationResult::kReject;
  }

  const auto placement = ResolveMultiCastSpellPlacementData(L, spell_id);
  if (!placement.has_value()) {
    return MultiCastSlotValidationResult::kMissingSpellData;
  }

  if (!placement->has_restricted_slot_requirements) {
    return MultiCastSlotValidationResult::kAccept;
  }

  for (const auto totem_category : placement->totem_categories) {
    if (totem_category == required_category) {
      return MultiCastSlotValidationResult::kAccept;
    }
  }

  return MultiCastSlotValidationResult::kReject;
}

namespace {

bool SlotHasBaseSpellAction(const ActionPresentationEntry &button) {
  return button.action != 0 && (static_cast<std::uint8_t>(button.type) & 0xF0u) == 0;
}

bool SlotHasSpellLikeAction(const ActionPresentationEntry &button) {
  return SlotHasBaseSpellAction(button) ||
         (button.action != 0 && button.type == ActionPresentationKind::kPet);
}

bool SlotHasItemAction(const ActionPresentationEntry &button) {
  return button.action != 0 && button.type == ActionPresentationKind::kItem;
}

bool SlotHasMacroReference(const ActionPresentationEntry &button) {
  return button.action != 0 && (static_cast<std::uint8_t>(button.type) & 0xF0u) == 0x40u;
}

std::uint32_t GetMacroReferenceSlotIndex(
    const MacroCatalog &macros, const ActionPresentationEntry &button) {
  if (!SlotHasMacroReference(button)) {
    return 0;
  }

  const auto macro_slot = macros.FindSlotIndex(
      ::openwow::game::actions::macros::MacroId(button.action));
  if (macro_slot < 0) {
    return 0;
  }

  return static_cast<std::uint32_t>(macro_slot + 1);
}

bool SlotHasLiveMacroAction(const MacroCatalog &macros,
                            const ActionPresentationEntry &button) {
  if (!SlotHasMacroReference(button)) {
    return false;
  }

  return macros
      .FindMacro(::openwow::game::actions::macros::MacroId(button.action))
      .has_value();
}

bool SlotHasEquipmentSetAction(const ActionPresentationEntry &button) {
  return button.type == ActionPresentationKind::kEquipmentSet;
}

std::uint32_t GetEquipmentSetActionId(const ActionPresentationEntry &button) {
  if (!SlotHasEquipmentSetAction(button)) {
    return 0xFFFFFFFFu;
  }

  return button.action;
}

bool EquipmentSetExists(lua_State* L, std::uint32_t set_id) {
  return RequireEquipmentSets(L).find(set_id) != nullptr;
}

std::uint32_t ResolvePetActionSpellLikeId(WorldSession &session, std::size_t slot_index) {
  std::size_t pet_slot = 0;
  if (!ResolvePetActionBarSlotIndex(session, slot_index, &pet_slot)) {
    return 0;
  }

  const auto &pet_bar = session.pet().pet_bar();
  const auto &action = pet_bar.action_bar[pet_slot];
  if (!IsPetSpellActionKind(action.ActionKind())) {
    return 0;
  }

  return action.ActionId();
}

bool SlotHasScriptAction(WorldSession &session, std::size_t slot_index) {
  const auto button = GetResolvedActionButton(session, slot_index);
  if (button.IsEmpty()) {
    return false;
  }

  if (SlotHasMacroReference(button)) {
    return session.macros()
        .FindMacro(::openwow::game::actions::macros::MacroId(button.action))
        .has_value();
  }

  if (button.type != ActionPresentationKind::kPet) {
    return true;
  }

  if (slot_index < kFirstPickupPlaceBlockedActionSlot ||
      slot_index >= kFirstPickupPlaceBlockedActionSlot + 10) {
    return false;
  }

  const auto &pet_bar = session.pet().pet_bar();
  if (!pet_bar.active || pet_bar.guid.IsEmpty()) {
    return false;
  }

  const auto action_kind =
      pet_bar.action_bar[slot_index - kFirstPickupPlaceBlockedActionSlot].ActionKind();
  return action_kind == 6 || action_kind == 7 ||
         ResolvePetActionSpellLikeId(session, slot_index) != 0;
}

enum class CursorPayloadKind : std::uint8_t {
  kNone,
  kSpellLike,
  kItem,
  kMacro,
  kEquipmentSet,
};

struct CursorPayload {
  CursorPayloadKind kind{CursorPayloadKind::kNone};
  ActionPresentationEntry button{};
  bool from_inventory_item{false};
};

enum class HeldSpellValidationResult : std::uint8_t {
  kAccept,
  kMissingSpellData,
  kPassive,
};

bool CursorHasActionPayload(const HeldCursor& cursor) {
  using Kind = ::openwow::game::actions::held_cursor::Kind;
  switch (cursor.kind()) {
    case Kind::LiveItem:
    case Kind::Spell:
    case Kind::ActionBarItem:
    case Kind::Macro:
    case Kind::EquipmentSet:
      return true;
    default:
      return false;
  }
}

std::string ResolveCursorTexturePath(lua_State *L, const ActionPresentationEntry &button) {
  switch (button.type) {
  case ActionPresentationKind::kSpell:
  case ActionPresentationKind::kCompanion:
  case ActionPresentationKind::kPet:
    return cursor_texture::ResolveSpellTexturePath(L, button.action);
  case ActionPresentationKind::kItem:
    return cursor_texture::ResolveItemTexturePath(L, button.action);
  case ActionPresentationKind::kMacro:
  case ActionPresentationKind::kCompanionMacro:
    return cursor_texture::ResolveMacroTexturePath(L, button.action);
  case ActionPresentationKind::kEquipmentSet:
    return cursor_texture::ResolveEquipmentSetTexturePath(L, button.action);
  default:
    return {};
  }
}

bool GetCursorPayload(const HeldCursor& cursor, CursorPayload& out_payload) {
  namespace held_cursor = ::openwow::game::actions::held_cursor;
  if (const auto* spell = cursor.get_if<held_cursor::Spell>()) {
    out_payload.kind = CursorPayloadKind::kSpellLike;
    out_payload.button.action = spell->spell_id;
    out_payload.button.type =
        spell->from_pet_spellbook ? ActionPresentationKind::kPet
                                  : ActionPresentationKind::kSpell;
    return out_payload.button.action != 0;
  }

  if (const auto* item = cursor.get_if<held_cursor::LiveItem>()) {
    out_payload.kind = CursorPayloadKind::kItem;
    out_payload.button.action = item->item.entry;
    out_payload.button.type = ActionPresentationKind::kItem;
    out_payload.from_inventory_item = true;
    return out_payload.button.action != 0;
  }
  if (const auto* item = cursor.get_if<held_cursor::ActionBarItem>()) {
    out_payload.kind = CursorPayloadKind::kItem;
    out_payload.button.action = item->item_entry;
    out_payload.button.type = ActionPresentationKind::kItem;
    return out_payload.button.action != 0;
  }
  if (const auto* macro = cursor.get_if<held_cursor::Macro>()) {
    const auto macro_id = macro->stable_id;
    if (macro_id == 0) {
      return false;
    }

    out_payload.kind = CursorPayloadKind::kMacro;
    out_payload.button.action = macro_id;
    out_payload.button.type = ActionPresentationKind::kMacro;
    return true;
  }

  if (const auto* set = cursor.get_if<held_cursor::EquipmentSet>()) {
    out_payload.kind = CursorPayloadKind::kEquipmentSet;
    out_payload.button.action = set->stable_id;
    out_payload.button.type = ActionPresentationKind::kEquipmentSet;

    return true;
  }

  return false;
}

HeldSpellValidationResult ValidateHeldSpellPayload(lua_State *L, const CursorPayload &payload) {
  if (payload.kind != CursorPayloadKind::kSpellLike || payload.button.action == 0) {
    return HeldSpellValidationResult::kAccept;
  }

  const auto *spell = cursor_texture::GetSpellEntry(L, payload.button.action);
  if (spell != nullptr) {
    return (spell->attributes & 0x40u) != 0 ? HeldSpellValidationResult::kPassive
                                           : HeldSpellValidationResult::kAccept;
  }

  if (const auto query = SpellQueryBridge::Get().Query(payload.button.action);
      query.has_value()) {
    return (query->attributes & 0x40u) != 0 || query->isPassive
               ? HeldSpellValidationResult::kPassive
               : HeldSpellValidationResult::kAccept;
  }

  return HeldSpellValidationResult::kAccept;
}

bool HeldItemCanBePlacedOnActionBar(
    const ItemDefinitions& item_definitions, const CursorPayload &payload) {
  if (payload.kind != CursorPayloadKind::kItem || payload.button.action == 0) {
    return true;
  }

  const auto *item = item_definitions.GetItem(payload.button.action);
  if (item == nullptr) {

    return !payload.from_inventory_item;
  }

  if (ResolveItemUseSpell(item_definitions, payload.button.action) != 0) {
    return true;
  }

  return item->inventory_type != ::openwow::game::InventoryType::NonEquip &&
         item->inventory_type != ::openwow::game::InventoryType::Bag;
}

void ReportPassiveActionPlacementError() {
  auto &localization = ::openwow::game::Localization::Get();
  ErrorMessageSystem::Get().ShowError(
      localization.GetString("ERR_PASSIVE_ABILITY", "That ability is passive."));
}

void SetCursorFromActionButton(lua_State* L, MacroCatalog& macros,
                               HeldCursor& cursor,
                               const ActionPresentationEntry& button) {
  namespace held_cursor = ::openwow::game::actions::held_cursor;
  auto hold_spell = [&](const bool from_pet_spellbook,
                        const held_cursor::Grid grid) {
    cursor.HoldSpell(
        held_cursor::Spell{
            .spell_id = button.action,
            .from_pet_spellbook = from_pet_spellbook,
        },
        held_cursor::Presentation{
            .texture_path = ResolveCursorTexturePath(L, button),
            .sound = held_cursor::Sound::CursorGrabObject,
            .grid = grid,
        });
  };

  if (SlotHasBaseSpellAction(button)) {
    hold_spell(false, held_cursor::Grid::ActionBar);
    return;
  }

  switch (button.type) {
  case ActionPresentationKind::kSpell:
  case ActionPresentationKind::kCompanion:
    hold_spell(false, held_cursor::Grid::ActionBar);
    return;
  case ActionPresentationKind::kPet:
    hold_spell(true, held_cursor::Grid::None);
    return;
  case ActionPresentationKind::kItem:
    cursor.HoldActionBarItem(
        held_cursor::ActionBarItem{.item_entry = button.action},
        held_cursor::Presentation{
            .texture_path = ResolveCursorTexturePath(L, button),
            .texture_mode = held_cursor::TextureMode::HeldTexture,
            .sound = held_cursor::Sound::CursorGrabObject,
            .grid = held_cursor::Grid::ActionBar,
        });
    return;
  case ActionPresentationKind::kMacro:
  case ActionPresentationKind::kCompanionMacro:
    (void)::openwow::game::actions::macros::ui::PickupMacroCursor(
        macros, cursor,
        ::openwow::game::actions::macros::MacroId(button.action));
    return;
  case ActionPresentationKind::kEquipmentSet:
    cursor.HoldEquipmentSet(
        held_cursor::EquipmentSet{.stable_id = button.action},
        held_cursor::Presentation{
            .texture_path = ResolveCursorTexturePath(L, button),
            .sound = held_cursor::Sound::CursorGrabObject,
            .grid = held_cursor::Grid::ActionBar,
        });
    return;
  default:
    return;
  }
}

bool IsActionSlotSupported(lua_State *L, const MacroCatalog &macros,
                           const ActionPresentationEntry &button) {
  return SlotHasSpellLikeAction(button) || SlotHasItemAction(button) ||
         SlotHasLiveMacroAction(macros, button) ||
         (SlotHasEquipmentSetAction(button) &&
          EquipmentSetExists(L, GetEquipmentSetActionId(button)));
}

bool ActionBarMutationsAllowed(WorldSession &session) {
  return session.objects().GetActivePlayer() != nullptr &&
         !session.action_assignments().IsServerSyncPending();
}

void UpdateActionSlot(WorldSession &session, std::size_t slot_index, const ActionPresentationEntry &button);

void ClearActionSlot(WorldSession &session, std::size_t slot_index);

std::uint64_t GetCurrentTargetGuid(WorldSession &session) {
  return session.objects().GetTargetGuid().GetRawValue();
}

std::optional<std::uint64_t> ResolveUseActionTargetGuid(lua_State *L, WorldSession &session) {
  if (!lua_isstring(L, 2) || SafeLuaString(L, 2).empty()) {

    if (const auto *profiles = session.binding_profiles(); profiles != nullptr) {
      const auto modifier_state = GetCurrentModifierStateOverride(L).value_or(
          static_cast<std::uint16_t>(SDL_GetModState()));
      const auto mouse_button = lua_isstring(L, 3) ? lua_tostring(L, 3) : nullptr;
      const std::string_view button = mouse_button != nullptr ? mouse_button : "";
      using ::openwow::game::actions::bindings::adapters::retail::
          IsModifiedClickActive;
      if (IsModifiedClickActive(*profiles, "SELFCAST", modifier_state, button)) {
        return session.objects().GetActivePlayerGuid().GetRawValue();
      }
      if (IsModifiedClickActive(*profiles, "FOCUSCAST", modifier_state, button)) {
        return session.objects().GetFocusTargetGuid().GetRawValue();
      }
    }
    return GetCurrentTargetGuid(session);
  }

  const auto unit_token = SafeLuaString(L, 2);

  const auto guid = ResolveUnitId(&session, unit_token);
  if (guid.IsEmpty()) {
    return std::nullopt;
  }

  return guid.GetRawValue();
}

const char *GetUseActionButtonArg(lua_State *L) {
  return lua_isstring(L, 3) ? lua_tostring(L, 3) : nullptr;
}

struct ItemUseLocation {
  std::uint8_t bag = 0;
  std::uint8_t slot = 0;
  const ItemInstance *item = nullptr;
};

std::optional<ItemUseLocation>
FindEquippedInventoryItemByEntry(lua_State *L, std::uint32_t item_entry) {
  if (item_entry == 0) {
    return std::nullopt;
  }

  auto &inv = RequirePlayerInventoryReplica(L);

  for (std::uint8_t slot = InventorySlots::kEquipStart; slot < InventorySlots::kEquipEnd; ++slot) {
    const auto *item = inv.GetEquipSlot(slot);
    if (item && item->entry == item_entry) {
      return ItemUseLocation{InventorySlots::kMainBag, slot, item};
    }
  }

  return std::nullopt;
}

std::optional<ItemUseLocation>
FindBagInventoryItemByEntry(lua_State *L, std::uint32_t item_entry) {
  if (item_entry == 0) {
    return std::nullopt;
  }

  auto &inv = RequirePlayerInventoryReplica(L);

  for (std::uint8_t slot = 0; slot < PlayerInventoryReplica::kBackpackSize; ++slot) {
    const auto *item = inv.GetBackpackSlot(slot);
    if (item && item->entry == item_entry) {
      return ItemUseLocation{InventorySlots::kMainBag,
                             static_cast<std::uint8_t>(InventorySlots::kBackpackStart + slot),
                             item};
    }
  }

  for (std::uint8_t bag = 1; bag <= PlayerInventoryReplica::kMaxBags; ++bag) {
    const auto *bag_info = inv.GetBag(bag);
    if (!bag_info) {
      continue;
    }
    for (std::uint8_t slot = 0; slot < bag_info->num_slots; ++slot) {
      const auto *item = inv.GetBagSlot(bag, slot);
      if (item && item->entry == item_entry) {
        return ItemUseLocation{
            static_cast<std::uint8_t>(InventorySlots::kBagSlotsStart + (bag - 1)), slot, item};
      }
    }
  }

  return std::nullopt;
}

bool PickupInventoryItemByEntry(lua_State *L, std::uint32_t item_entry) {
  if (item_entry == 0) {
    return false;
  }

  auto &inv = RequirePlayerInventoryReplica(L);
  auto* session = GetWorldSession(L);
  if (session == nullptr || session->held_cursor() == nullptr) {
    return false;
  }
  auto& cursor = *session->held_cursor();
  const auto hold_item =
      [&](const ItemInstance& item, const std::uint64_t container_guid,
          const std::uint8_t source_bag, const std::uint8_t source_slot) {
        namespace held_cursor =
            ::openwow::game::actions::held_cursor;
        cursor.Clear();
        cursor.HoldLiveItem(
            held_cursor::LiveItem{
                .item = item,
                .source_container_guid = container_guid,
                .source_bag = source_bag,
                .source_slot = source_slot,
            },
            held_cursor::Presentation{
                .texture_path =
                    cursor_texture::ResolveItemTexturePath(L, item.entry),
                .texture_mode = held_cursor::TextureMode::HeldTexture,
                .sound = held_cursor::Sound::CursorGrabObject,

                .grid = ::openwow::game::inventory::ui::ResolveItemCursorGrid(
                    RequireItemDefinitions(L), item.entry),
            });
      };

  for (std::uint8_t slot = 0; slot < PlayerInventoryReplica::kBackpackSize; ++slot) {
    const auto *item = inv.GetBackpackSlot(slot);
    const auto* live_item =
        item != nullptr
            ? session->objects().GetItem(ObjectGuid(item->guid))
            : nullptr;
    if (item && item->entry == item_entry && live_item != nullptr &&
        !live_item->IsLocked()) {
      hold_item(
          *item,
          session->objects().GetActivePlayerGuid().GetRawValue(),
          InventorySlots::kMainBag,
          static_cast<std::uint8_t>(InventorySlots::kBackpackStart + slot));
      return true;
    }
  }

  for (std::uint8_t bag = 1; bag <= PlayerInventoryReplica::kMaxBags; ++bag) {
    const auto *bag_info = inv.GetBag(bag);
    if (!bag_info) {
      continue;
    }
    for (std::uint8_t slot = 0; slot < bag_info->num_slots; ++slot) {
      const auto *item = inv.GetBagSlot(bag, slot);
      const auto* live_item =
          item != nullptr
              ? session->objects().GetItem(ObjectGuid(item->guid))
              : nullptr;
      if (item && item->entry == item_entry && live_item != nullptr &&
          !live_item->IsLocked()) {
        hold_item(
            *item, bag_info->guid,
            static_cast<std::uint8_t>(
                InventorySlots::kBagSlotsStart + bag - 1),
            slot);
        return true;
      }
    }
  }

  return false;
}

std::uint32_t ResolveSpellLikeActionId(WorldSession &session,
                                       const ActionPresentationEntry &button, std::size_t slot_index) {
  switch (button.type) {
  case ActionPresentationKind::kSpell:
  case ActionPresentationKind::kCompanion:
    return button.action;
  case ActionPresentationKind::kPet: {
    return ResolvePetActionSpellLikeId(session, slot_index);
  }
  case ActionPresentationKind::kMacro:
  case ActionPresentationKind::kCompanionMacro: {
    const auto macro = session.macros().FindMacro(
        ::openwow::game::actions::macros::MacroId(button.action));
    return macro && macro->resolved_spell_id > 0
               ? static_cast<std::uint32_t>(macro->resolved_spell_id)
               : 0;
  }
  default:
    return 0;
  }
}

std::optional<::openwow::game::MacroDocument> GetLiveMacro(
    const MacroCatalog &macros, const ActionPresentationEntry &button) {
  if (!SlotHasLiveMacroAction(macros, button)) {
    return std::nullopt;
  }
  return macros.FindMacro(
      ::openwow::game::actions::macros::MacroId(button.action));
}

std::uint32_t ResolveStoredMacroItemActionId(
    const MacroCatalog &macros, const ActionPresentationEntry &button) {
  const auto macro = GetLiveMacro(macros, button);
  return macro ? macro->resolved_item_id : 0;
}

std::uint32_t ResolveStoredItemActionId(
    const MacroCatalog &macros, const ActionPresentationEntry &button) {
  if (SlotHasItemAction(button)) {
    return button.action;
  }
  if (SlotHasMacroReference(button)) {
    return ResolveStoredMacroItemActionId(macros, button);
  }
  return 0;
}

}

std::uint32_t ResolveSpellLikeActionIdForValidation(WorldSession &session,
                                                    const ActionPresentationEntry &button,
                                                    std::size_t slot_index,
                                                    bool *is_pet_action) {
  if (is_pet_action != nullptr) {
    *is_pet_action = false;
  }

  switch (button.type) {
  case ActionPresentationKind::kSpell:
    return button.action;
  case ActionPresentationKind::kCompanion:

    return 0;
  case ActionPresentationKind::kPet: {
    if (is_pet_action != nullptr) {
      *is_pet_action = true;
    }
    return ResolvePetActionSpellLikeId(session, slot_index);
  }
  case ActionPresentationKind::kMacro:
  case ActionPresentationKind::kCompanionMacro: {
    const auto macro = GetLiveMacro(session.macros(), button);
    if (!macro) {
      return 0;
    }
    if (macro->resolved_spell_id > 0) {
      return static_cast<std::uint32_t>(macro->resolved_spell_id);
    }
    if (macro->resolved_item_id != 0) {
      return ResolveItemUseSpellWithEquippedFallback(
          session.inventory_replica(), session.item_definitions(),
          macro->resolved_item_id,
          session.GetDbcLoader());
    }
    return 0;
  }
  default:
    return 0;
  }
}

const CGUnit_C *ResolveUsabilityCaster(WorldSession &session, bool is_pet_action) {
  if (!is_pet_action) {
    return session.objects().GetActivePlayer();
  }

  const auto *player = session.objects().GetActivePlayer();
  if (player == nullptr) {
    return nullptr;
  }

  const auto pet_guid = player->State().GetPetGUID();
  if (pet_guid.IsEmpty()) {
    return nullptr;
  }
  return session.objects().GetUnit(pet_guid);
}

ActionButtonUsabilityState ComputeActionSlotUsability(WorldSession &session,
                                                      std::size_t slot_index) {
  ActionButtonUsabilityState state;
  if (slot_index >= kActionSlotCount) {
    return state;
  }

  const auto *player = session.objects().GetActivePlayer();
  if (player == nullptr) {
    return state;
  }

  const auto &button = session.action_assignments().GetPresentationEntry(slot_index);
  if (button.IsEmpty()) {
    return state;
  }

  if (SlotHasEquipmentSetAction(button)) {
    const auto set_id = GetEquipmentSetActionId(button);
    if (session.equipment().find(set_id) != nullptr) {
      state.is_usable = true;
    }
    return state;
  }

  const auto item_id = ResolveStoredItemActionId(session.macros(), button);
  if (item_id != 0) {
    const auto *item =
        FindInventoryItemByEntry(session.inventory_replica(), item_id);
    if (item == nullptr || session.item_locks().IsItemLocked(*item) ||
        !ItemPassesPlayerRequirements(
            &session, session.item_definitions(), *player, item_id)) {
      return {};
    }

    return {true, false};
  }

  bool is_pet_action = false;
  const auto spell_id =
      ResolveSpellLikeActionIdForValidation(session, button, slot_index, &is_pet_action);
  if (spell_id == 0) {
    if (SlotHasMacroReference(button) &&
        GetLiveMacro(session.macros(), button).has_value()) {
      return {true, false};
    }
    return state;
  }

  const auto *caster = ResolveUsabilityCaster(session, is_pet_action);
  if (caster == nullptr) {
    return {};
  }

  auto spell = SpellQueryBridge::Get().Query(spell_id);
  if (!spell.has_value()) {
    return {};
  }

  if (!is_pet_action) {

    spell->isKnown = SpellbookSystem::Get().HasSpellOrSupersedingRank(
        spell_id, player->State().GetRace(), player->State().GetClass());
  }

  const auto snapshot =
      is_pet_action
          ? BuildUnitUsabilitySnapshot(*caster, session.GetDbcLoader())
          : BuildPlayerUsabilitySnapshot(
                *player, session.inventory_replica(), session.item_definitions(),
                &session.runes(), session.GetDbcLoader());
  const auto usability = SpellUsabilityChecker::ComputeUsability(
      BuildActionSpellUsabilityInfo(*spell, is_pet_action ? nullptr : player, &session.aura(),
                                    session.GetDbcLoader()),
      snapshot);

  if (usability.not_enough_power) {
    return {true, true};
  }
  if (!usability.is_usable) {
    return {};
  }
  return {true, false};
}

namespace {

const openwow::data::dbc::SpellEntry *GetSpellEntryForActionCount(WorldSession &session,
                                                                  std::uint32_t spell_id) {
  const auto *dbc = session.GetDbcLoader();
  if (dbc == nullptr || spell_id == 0) {
    return nullptr;
  }
  return dbc->spell().LookupEntry(spell_id);
}

bool SpellUsesChangedReagent(WorldSession &session, std::uint32_t spell_id,
                             std::uint32_t item_entry) {
  const auto *spell = GetSpellEntryForActionCount(session, spell_id);
  if (spell == nullptr || item_entry == 0) {
    return false;
  }

  for (std::size_t i = 0; i < spell->reagent.size(); ++i) {
    if (spell->reagent[i] == static_cast<std::int32_t>(item_entry) && spell->reagent_count[i] > 0) {
      return true;
    }
  }

  return false;
}

std::uint32_t ResolveInventoryDrivenSpellId(WorldSession &session, const ActionPresentationEntry &button,
                                            std::size_t slot_index, bool *is_pet_action = nullptr) {
  if (is_pet_action != nullptr) {
    *is_pet_action = false;
  }

  const auto item_id = ResolveStoredItemActionId(session.macros(), button);
  if (item_id != 0) {
    return ResolveItemUseSpellWithEquippedFallback(
        session.inventory_replica(), session.item_definitions(), item_id,
        session.GetDbcLoader());
  }

  return ResolveSpellLikeActionIdForValidation(session, button, slot_index, is_pet_action);
}

std::uint32_t FindKnownLearnSpellForItem(
    const WorldSession &session, const ActionPresentationEntry &button) {
  if (button.type != ActionPresentationKind::kItem || button.action == 0) {
    return 0;
  }

  const auto *item = session.item_definitions().GetItem(button.action);
  if (item == nullptr) {
    return 0;
  }

  for (const auto &spell : item->spells) {
    if (spell.trigger == 6 && spell.spell_id != 0 &&
        SpellQueryBridge::Get().IsSpellKnown(spell.spell_id)) {
      return spell.spell_id;
    }
  }

  return 0;
}

std::int32_t ComputeActionDisplayCount(WorldSession &session, const ActionPresentationEntry &button,
                                       std::size_t slot_index) {
  const auto item_id = ResolveStoredItemActionId(session.macros(), button);
  if (item_id != 0) {
    const auto carried_count = static_cast<std::int32_t>(
        CountCarriedItemsOfEntry(session.inventory_replica(), item_id));
    if (carried_count <= 0) {
      return 0;
    }

    const auto *item = session.item_definitions().GetItem(item_id);
    if (item != nullptr) {
      return ComputeDisplayedInventoryItemCount(
          session.inventory_replica(), item_id, carried_count, *item,
          session.GetDbcLoader());
    }

    return carried_count;
  }

  bool is_pet_action = false;
  const auto spell_id =
      ResolveSpellLikeActionIdForValidation(session, button, slot_index, &is_pet_action);
  if (spell_id == 0 || is_pet_action ||
      !openwow::game::SpellHasDisplayReagentCount(session.GetDbcLoader(), spell_id)) {
    return 0;
  }

  return openwow::game::ComputeSpellReagentCastCount(
      session.inventory_replica(), session.GetDbcLoader(), spell_id);
}

bool SpellHasConsumableReagentRequirement(const openwow::data::dbc::DbcLoader *dbc,
                                          const std::uint32_t spell_id) {
  if (dbc == nullptr || spell_id == 0) {
    return false;
  }

  const auto *spell = dbc->spell().LookupEntry(spell_id);
  if (spell == nullptr) {
    return false;
  }

  for (std::size_t index = 0; index < spell->reagent.size(); ++index) {
    if (spell->reagent[index] != 0 && spell->reagent_count[index] > 0) {
      return true;
    }
  }

  return false;
}

bool IsConsumableItemAction(WorldSession& session,
                            const std::uint32_t item_id,
                            const openwow::data::dbc::DbcLoader *dbc) {
  if (item_id == 0) {
    return false;
  }

  const auto *item_template = session.item_definitions().GetItem(item_id);
  if (item_template == nullptr) {
    return false;
  }

  if (item_template->inventory_type == ::openwow::game::InventoryType::Ammo ||
      item_template->inventory_type == ::openwow::game::InventoryType::Thrown) {
    return true;
  }

  return ResolveIsEquippedActionUseCharges(
             session.inventory_replica(), item_id, *item_template, dbc) < 0;
}

bool IsConsumableActionSlot(lua_State *L, WorldSession &session,
                            const std::size_t slot_index) {
  if (slot_index >= kActionSlotCount) {
    return false;
  }

  SyncGeneratedActionSlots(session);
  const auto &button = session.action_assignments().GetPresentationEntry(slot_index);
  if (button.IsEmpty()) {
    return false;
  }

  auto *dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    dbc = GetDbcLoader(L);
  }

  if (const auto item_id =
          ResolveStoredItemActionId(session.macros(), button);
      IsConsumableItemAction(session, item_id, dbc)) {
    return true;
  }

  bool is_pet_action = false;
  const auto spell_id =
      ResolveSpellLikeActionIdForValidation(session, button, slot_index, &is_pet_action);
  if (spell_id == 0 || is_pet_action) {
    return false;
  }

  return SpellHasConsumableReagentRequirement(dbc, spell_id);
}

bool CurrentPlayerHasActiveAttackTarget(WorldSession *session) {
  if (session == nullptr) return false;
  const auto *player = session->objects().GetActivePlayer();
  if (player == nullptr) return false;

  return !player->GetGuidField(UNIT_FIELD_TARGET).IsEmpty();
}

bool IsAttackActionCurrent(WorldSession &session) {
  return CurrentPlayerHasActiveAttackTarget(&session);
}

bool ExecutePetActionUseAction(WorldSession &session, std::uint32_t action_data,
                               std::uint64_t target_guid) {
  if (action_data == 0) {
    return false;
  }

  const auto action_id = action_data & 0x00FFFFFFu;
  const auto action_type = static_cast<std::uint8_t>((action_data >> 24) & 0x3Fu);
  const auto pet_guid = GetPrimaryPetActionGuid(session);
  if (pet_guid.IsEmpty() ||
      !CanUsePetActions(session, PetActionAvailabilityRequiresForceCheck(action_type))) {
    return false;
  }

  switch (action_type) {
  case 6: {
    const auto previous_react = session.pet().pet_bar().EffectiveReactState();
    const bool valid_react =
        action_id <= static_cast<std::uint32_t>(
                         ::openwow::game::PetReactState::kAggressive);
    if (valid_react) {
      session.pet().SetLocalReactState(static_cast<::openwow::game::PetReactState>(action_id));
    }
    const bool sent = session.interaction().SendPetAction(
        pet_guid.GetRawValue(), action_data, 0);
    return sent ||
           (valid_react &&
            session.pet().pet_bar().EffectiveReactState() != previous_react);
  }
  case 7:
    if (action_id <= static_cast<std::uint32_t>(::openwow::game::PetCommandState::kFollow)) {
      const auto previous_command = session.pet().pet_bar().command;
      const bool previous_attack = session.pet().attack_command_active();
      session.pet().SetLocalCommandState(static_cast<::openwow::game::PetCommandState>(action_id));
      if (session.pet().pet_bar().command != previous_command ||
          session.pet().attack_command_active() != previous_attack) {
        ScriptEventDispatch::Get().FirePetBarUpdate();
      }
      const bool sent = session.interaction().SendPetAction(
          pet_guid.GetRawValue(), action_data, target_guid);
      return sent || session.pet().pet_bar().command != previous_command ||
             session.pet().attack_command_active() != previous_attack;
    }

    if (action_id == static_cast<std::uint32_t>(::openwow::game::PetCommandState::kAttack) &&
        target_guid == 0) {
      return false;
    }

    if (action_id == static_cast<std::uint32_t>(::openwow::game::PetCommandState::kAttack)) {
      const bool previous_attack = session.pet().attack_command_active();
      session.pet().SetAttackCommandActive(true);
      ScriptEventDispatch::Get().FirePetBarUpdate();
      const bool sent = session.interaction().SendPetAction(
          pet_guid.GetRawValue(), action_data, target_guid);
      return sent || !previous_attack;
    }

    return session.interaction().SendPetAction(
        pet_guid.GetRawValue(), action_data, target_guid);
  default:
    return session.interaction().SendPetAction(
        pet_guid.GetRawValue(), action_data, target_guid);
  }
}

bool IsPetActionCurrent(lua_State *L, WorldSession &session, std::size_t slot_index,
                        const ActionPresentationEntry &button) {
  if (button.type != ActionPresentationKind::kPet ||
      slot_index < kFirstPickupPlaceBlockedActionSlot || slot_index >= kFirstMultiCastActionSlot) {
    return false;
  }

  const auto *pet_action = ResolvePetActionButton(session, slot_index);
  if (pet_action == nullptr) {
    return false;
  }

  const auto &pet_bar = session.pet().pet_bar();
  const auto action_id = pet_action->ActionId();
  const auto action_type = pet_action->ActionKind();

  if (action_type == 6) {
    return static_cast<std::uint32_t>(pet_bar.EffectiveReactState()) == action_id;
  }

  if (action_type == 7) {
    return static_cast<std::uint32_t>(pet_bar.command) == action_id ||
           (action_id == static_cast<std::uint32_t>(::openwow::game::PetCommandState::kAttack) &&
            session.pet().attack_command_active());
  }

  const auto spell_id = ResolveSpellLikeActionId(session, button, slot_index);
  return spell_id != 0 && IsCurrentSpellIdActive(L, spell_id);
}

bool IsAttackLikeAction(lua_State *L, WorldSession &session, const ActionPresentationEntry &button,
                        std::size_t slot_index) {
  return SpellHasAttackActionEffect(L, ResolveInventoryDrivenSpellId(session, button, slot_index));
}

bool NotifyWearEquipmentSetById(lua_State* L, std::uint32_t set_id) {
  if (const auto* set = RequireEquipmentSets(L).find(set_id);
      set != nullptr) {
    ScriptEventDispatch::Get().FireWearEquipmentSet(set->name);
    return true;
  }
  return false;
}

bool UseSpellLikeAction(lua_State *L, WorldSession &session, std::uint32_t spell_id,
                        std::uint64_t target_guid) {
  if (spell_id == 0) {
    return false;
  }

  if (SpellHasAttackActionEffect(L, spell_id)) {
    if (auto *targeting = GetTargetingSystem(L); targeting != nullptr) {
      const bool was_active = targeting->IsAttackActive();
      const bool was_swinging = targeting->IsAttackSwingActive();
      const bool was_following = targeting->IsAttackFollowing();
      targeting->StartAttack(target_guid, false, false, spell_id);
      return targeting->IsAttackActive() != was_active ||
             targeting->IsAttackSwingActive() != was_swinging ||
             targeting->IsAttackFollowing() != was_following;
    } else if (target_guid != 0) {
      return session.interaction().SendAttackSwing(target_guid);
    }
    return false;
  }

  return ::openwow::game::SpellAction_ValidateAndInitiateCast(
      session, spell_id, target_guid, -1, 0);
}

bool HandleActionSlotClick(lua_State *L, WorldSession &session, std::size_t slot_index,
                           const bool takes_action_slot_gate) {
  if (slot_index >= kActionSlotCount) {
    return false;
  }

  auto* cursor = session.held_cursor();
  if (cursor == nullptr) {
    return false;
  }
  if (takes_action_slot_gate && !CanPerformActionBarProtectedAction()) {
    return false;
  }
  if (!ActionBarMutationsAllowed(session)) {
    return false;
  }

  CursorPayload held_payload{};
  const bool has_payload = GetCursorPayload(*cursor, held_payload);
  if (!has_payload) {
    return false;
  }

  const auto held_spell_validation = ValidateHeldSpellPayload(L, held_payload);
  if (held_spell_validation == HeldSpellValidationResult::kMissingSpellData) {
    return false;
  }
  if (held_spell_validation == HeldSpellValidationResult::kPassive) {
    ReportPassiveActionPlacementError();
    return false;
  }

  if (!HeldItemCanBePlacedOnActionBar(
          RequireItemDefinitions(L), held_payload)) {
    return false;
  }

  const auto destination = GetResolvedActionButton(session, slot_index);

  if (IsMultiCastActionSlot(slot_index)) {
    if (held_payload.kind != CursorPayloadKind::kSpellLike) {
      ReportMultiCastSlotError(L, slot_index);
      return false;
    }

    const auto multi_cast_validation =
        ValidateMultiCastSpellPlacement(L, slot_index, held_payload.button.action, nullptr);
    if (multi_cast_validation == MultiCastSlotValidationResult::kMissingSpellData) {
      return false;
    }
    if (multi_cast_validation != MultiCastSlotValidationResult::kAccept) {
      ReportMultiCastSlotError(L, slot_index);
      return false;
    }
  }

  if (!destination.IsEmpty()) {
    const bool same_spell_like = held_payload.kind == CursorPayloadKind::kSpellLike &&
                                 SlotHasSpellLikeAction(destination) &&
                                 destination.action == held_payload.button.action;
    if (same_spell_like) {
      cursor->Clear();
      return true;
    }

    const bool same_item = held_payload.kind == CursorPayloadKind::kItem &&
                           SlotHasItemAction(destination) &&
                           destination.action == held_payload.button.action;
    if (same_item) {
      cursor->Clear();
      return true;
    }

    const bool same_macro = held_payload.kind == CursorPayloadKind::kMacro &&
                            SlotHasLiveMacroAction(session.macros(), destination) &&
                            destination.action == held_payload.button.action;
    if (same_macro) {
      cursor->Clear();
      return true;
    }

    const bool same_equipment_set = held_payload.kind == CursorPayloadKind::kEquipmentSet &&
                                    SlotHasEquipmentSetAction(destination) &&
                                    EquipmentSetExists(L, destination.action) &&
                                    destination.action == held_payload.button.action;
    if (same_equipment_set) {
      cursor->Clear();
      return true;
    }
  }

  if (destination.IsEmpty()) {
    UpdateActionSlot(session, slot_index, held_payload.button);
    cursor->Clear();
    return true;
  }

  const bool destination_supported =
      IsActionSlotSupported(L, session.macros(), destination);
  if (destination_supported) {
    SetCursorFromActionButton(L, session.macros(), *cursor, destination);
  } else {
    cursor->Clear();
  }

  UpdateActionSlot(session, slot_index, held_payload.button);
  return true;
}

bool PickupActionFromSlot(lua_State *L, WorldSession &session, std::size_t slot_index) {
  auto* cursor = session.held_cursor();
  if (cursor == nullptr) {
    return false;
  }
  const auto button = GetResolvedActionButton(session, slot_index);
  if (button.IsEmpty()) {
    return false;
  }
  if (!cursor->empty()) {
    cursor->Clear();
  }

  if (SlotHasItemAction(button)) {
    SetCursorFromActionButton(L, session.macros(), *cursor, button);
    ClearActionSlot(session, slot_index);
    return true;
  }

  if (SlotHasSpellLikeAction(button)) {
    SetCursorFromActionButton(L, session.macros(), *cursor, button);
    ClearActionSlot(session, slot_index);
    return true;
  }

  if (SlotHasMacroReference(button)) {
    if (SlotHasLiveMacroAction(session.macros(), button)) {
      SetCursorFromActionButton(L, session.macros(), *cursor, button);
    }
    ClearActionSlot(session, slot_index);
    return true;
  }

  if (button.type == ActionPresentationKind::kEquipmentSet) {
    if (EquipmentSetExists(L, GetEquipmentSetActionId(button))) {
      SetCursorFromActionButton(L, session.macros(), *cursor, button);
    }
    ClearActionSlot(session, slot_index);
    return true;
  }

  ClearActionSlot(session, slot_index);
  return true;
}

void UpdateActionSlot(WorldSession &session, std::size_t slot_index, const ActionPresentationEntry &button) {
  if (slot_index >= kActionSlotCount || !ActionBarMutationsAllowed(session)) {
    return;
  }

  session.action_assignments().SetPresentationEntry(slot_index, button);
  session.interaction().SendSetActionButton(static_cast<std::uint8_t>(slot_index), button);
  ScriptEventDispatch::Get().FireActionbarSlotChanged(static_cast<std::uint8_t>(slot_index + 1));
}

void ClearActionSlot(WorldSession &session, std::size_t slot_index) {
  if (slot_index >= kActionSlotCount || !ActionBarMutationsAllowed(session)) {
    return;
  }

  session.action_assignments().ClearAssignment(slot_index);
  session.interaction().SendClearActionButton(static_cast<std::uint8_t>(slot_index));
  ScriptEventDispatch::Get().FireActionbarSlotChanged(static_cast<std::uint8_t>(slot_index + 1));
}

}

bool ClearActionSlotsForEquipmentSet(WorldSession &session, std::uint32_t set_id) {
  if (set_id > 9 || !ActionBarMutationsAllowed(session)) {
    return false;
  }

  bool cleared = false;
  for (std::size_t slot_index = 0; slot_index < kActionSlotCount; ++slot_index) {
    const auto button = session.action_assignments().GetPresentationEntry(slot_index);
    if (button.type != ActionPresentationKind::kEquipmentSet || button.action != set_id) {
      continue;
    }

    ClearActionSlot(session, slot_index);
    cleared = true;
  }
  return cleared;
}

bool SlotAcceptsMultiCastSpell(lua_State *L, std::size_t slot_index, std::uint32_t spell_id,
                               TotemCategoryId *out_category) {
  return ValidateMultiCastSpellPlacement(L, slot_index, spell_id, out_category) ==
         MultiCastSlotValidationResult::kAccept;
}

void ReportMultiCastSlotError(lua_State *L, std::size_t slot_index) {
  const auto required_category = GetMultiCastRequiredTotemCategory(slot_index);
  if (!required_category.IsValid()) {
    return;
  }

  std::string category_name;
  if (const auto *dbc = cursor_texture::GetDbcLoader(L)) {
    if (const auto *category =
            dbc->totem_category().LookupEntry(required_category.value());
        category && !category->name.empty()) {
      category_name = std::string(category->name);
    }
  }

  if (category_name.empty()) {
    return;
  }

  auto &localization = ::openwow::game::Localization::Get();
  const std::string format = localization.GetString("ERR_MULTI_CAST_ACTION_TOTEM_S",
                                                    "That spell cannot be placed in the %s slot.");
  ErrorMessageSystem::Get().ShowError(localization.FormatString(format, {category_name}));
}

int LuaGetActionInfo(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetActionInfo(slot)");
  }
  auto *session = GetWorldSession(L);
  int slot = static_cast<int>(lua_tonumber(L, 1));

  if (slot < 1 || slot > 144) {
    return 0;
  }

  const auto push_spell_action_info = [&](const std::uint32_t spell_id, const bool from_pet_book) {
    const auto spellbook_slot_index = FindSpellBookSlotIndexBySpellId(L, spell_id, from_pet_book);
    lua_pushstring(L, "spell");
    const auto action_index = spellbook_slot_index.has_value()
                                  ? *spellbook_slot_index + (from_pet_book ? 2u : 1u)
                                  : 0u;
    lua_pushnumber(L, static_cast<lua_Number>(action_index));
    lua_pushstring(L, from_pet_book ? "pet" : "spell");
    lua_pushnumber(L, static_cast<lua_Number>(spell_id));
    return 4;
  };

  if (session) {
    const auto btn = GetResolvedActionButton(*session, static_cast<std::size_t>(slot - 1));
    if (!btn.IsEmpty()) {
      switch (btn.type) {
      case ::openwow::game::ActionPresentationKind::kSpell:
      case ::openwow::game::ActionPresentationKind::kCompanion: {
        std::uint32_t companion_index = 0;
        const char *companion_type = nullptr;
        if (ResolveCompanionSpellCursorInfo(L, btn.action, companion_index, companion_type)) {
          lua_pushstring(L, "companion");
          lua_pushnumber(L, static_cast<lua_Number>(companion_index));
          lua_pushstring(L, companion_type ? companion_type : "UNKNOWN");
          lua_pushnumber(L, static_cast<lua_Number>(btn.action));
          return 4;
        }

        return push_spell_action_info(btn.action, false);
      }
      case ::openwow::game::ActionPresentationKind::kPet: {
        const auto spell_id =
            ResolvePetActionSpellLikeId(*session, static_cast<std::size_t>(slot - 1));
        return push_spell_action_info(spell_id, true);
      }
      case ::openwow::game::ActionPresentationKind::kItem:
        lua_pushstring(L, "item");
        lua_pushnumber(L, static_cast<lua_Number>(btn.action));
        return 2;
      case ::openwow::game::ActionPresentationKind::kMacro:
      case ::openwow::game::ActionPresentationKind::kCompanionMacro:
        lua_pushstring(L, "macro");
        lua_pushnumber(
            L, static_cast<lua_Number>(
                   GetMacroReferenceSlotIndex(session->macros(), btn)));
        return 2;
      case ::openwow::game::ActionPresentationKind::kEquipmentSet: {
        const auto* set = RequireEquipmentSets(L).find(btn.action);
        if (set == nullptr) {
          return 0;
        }

        lua_pushstring(L, "equipmentset");
        lua_pushstring(L, set->name.c_str());
        return 2;
      }
      default:
        return push_spell_action_info(btn.action, false);
      }
    }
  }

  return 0;
}

int LuaGetActionTexture(lua_State *L) {

  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetActionTexture(slot)");
  }
  auto *session = GetWorldSession(L);
  int slot = static_cast<int>(lua_tonumber(L, 1));

  if (!session || slot < 1 || slot > 144) {
    lua_pushnil(L);
    return 1;
  }

  const auto btn = GetResolvedActionButton(*session, static_cast<std::size_t>(slot - 1));
  if (btn.IsEmpty()) {
    lua_pushnil(L);
    return 1;
  }

  const auto *dbc = session->GetDbcLoader();

  const auto spell_like_id = ResolveSpellLikeActionIdForValidation(
      *session, btn, static_cast<std::size_t>(slot - 1), nullptr);
  if (spell_like_id != 0 && ::openwow::game::SpellHasAttackActionEffect(spell_like_id, dbc)) {
    if (auto texture = ::openwow::game::ResolveActiveAttackActionTexturePath(*session, dbc);
        !texture.empty()) {
      lua_pushstring(L, texture.c_str());
      return 1;
    }
  }

  if (spell_like_id != 0 && ::openwow::game::SpellHasRangedAttackActionFlags(spell_like_id, dbc)) {
    if (auto texture = ::openwow::game::ResolveActiveRangedActionTexturePath(*session, dbc);
        !texture.empty()) {
      lua_pushstring(L, texture.c_str());
      return 1;
    }
  }

  if (btn.type == ::openwow::game::ActionPresentationKind::kPet) {
    const auto *pet_action = ResolvePetActionButton(*session, static_cast<std::size_t>(slot - 1));
    if (pet_action == nullptr) {
      lua_pushnil(L);
      return 1;
    }

    if (const auto *token = ResolvePetActionTextureToken(*pet_action); token != nullptr) {

      auto resolved = ::openwow::game::ResolveLocalizedGlobalString(L, token);
      if (!resolved.empty()) {
        lua_pushstring(L, resolved.c_str());
      } else {
        lua_pushnil(L);
      }
      return 1;
    }
  }

  if (dbc) {
    if (btn.type == ::openwow::game::ActionPresentationKind::kSpell ||
        btn.type == ::openwow::game::ActionPresentationKind::kPet ||
        btn.type == ::openwow::game::ActionPresentationKind::kCompanion) {
      const auto *spell = dbc->spell().LookupEntry(btn.action);
      if (spell) {
        const auto *icon = dbc->spell_icon().LookupEntry(spell->spell_icon_id);
        if (icon && !std::string_view(icon->icon_path).empty()) {
          lua_pushstring(L, std::string(icon->icon_path).c_str());
          return 1;
        }
      }
    } else if (SlotHasMacroReference(btn)) {
      if (const auto macro = session->macros().FindMacro(
              ::openwow::game::actions::macros::MacroId(btn.action))) {
        if (macro->resolved_spell_id > 0) {
          const auto *spell =
              dbc->spell().LookupEntry(static_cast<std::uint32_t>(macro->resolved_spell_id));
          if (spell) {
            const auto *icon = dbc->spell_icon().LookupEntry(spell->spell_icon_id);
            if (icon && !std::string_view(icon->icon_path).empty()) {
              lua_pushstring(L, std::string(icon->icon_path).c_str());
              return 1;
            }
          }
        } else if (macro->resolved_item_id != 0) {
          const auto *item = RequireItemDefinitions(L).GetItem(macro->resolved_item_id);
          if (item && item->display_id > 0) {
            const auto *disp = dbc->item_display_info().LookupEntry(item->display_id);
            if (disp && !std::string_view(disp->inventory_icon).empty()) {
              std::string path = "Interface\\Icons\\" + std::string(disp->inventory_icon);
              lua_pushstring(L, path.c_str());
              return 1;
            }
          }
        }
      }
    } else if (btn.type == ::openwow::game::ActionPresentationKind::kItem) {
      const auto *item = RequireItemDefinitions(L).GetItem(btn.action);
      if (item && item->display_id > 0) {
        const auto *disp = dbc->item_display_info().LookupEntry(item->display_id);
        if (disp && !std::string_view(disp->inventory_icon).empty()) {
          std::string path = "Interface\\Icons\\" + std::string(disp->inventory_icon);
          lua_pushstring(L, path.c_str());
          return 1;
        }
      }
    }
  }

  if (btn.type == ::openwow::game::ActionPresentationKind::kEquipmentSet) {
    if (const auto* set = RequireEquipmentSets(L).find(btn.action);
        set != nullptr) {
      const auto path =
          ::openwow::game::equipment_set_icon_path(set->icon);
      if (!path.empty()) {
        lua_pushstring(L, path.c_str());
        return 1;
      }
    }
  }

  lua_pushnil(L);
  return 1;
}

int LuaGetActionCount(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetActionCount(slot)");
  }
  auto *session = GetWorldSession(L);
  int slot = static_cast<int>(lua_tonumber(L, 1));

  if (session && slot >= 1 && slot <= 144) {
    const auto btn = GetResolvedActionButton(*session, static_cast<std::size_t>(slot - 1));
    if (!btn.IsEmpty()) {
      const auto count =
          ComputeActionDisplayCount(*session, btn, static_cast<std::size_t>(slot - 1));
      lua_pushnumber(L, static_cast<lua_Number>(count));
      return 1;
    }
  }

  lua_pushnumber(L, 0);
  return 1;
}

int LuaGetActionCooldown(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetActionCooldown(slot)");
  }

  auto *session = GetWorldSession(L);
  int slot = static_cast<int>(lua_tonumber(L, 1));

  if (!session || slot < 1 || slot > 144) {
    PushEmptyActionCooldownResult(L, 0.0);
    return 3;
  }

  const auto resolution = ResolveActionCooldown(L, *session, static_cast<std::size_t>(slot - 1));
  if (resolution.cooldown.has_value()) {
    PushActionCooldownResult(L, *resolution.cooldown);
    return 3;
  }

  PushEmptyActionCooldownResult(L, resolution.enabled);
  return 3;
}

int LuaHasAction(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: HasAction(slot)");
  }
  auto *session = GetWorldSession(L);
  int slot = static_cast<int>(lua_tonumber(L, 1));

  if (slot < 1 || slot > 144) {
    lua_pushnil(L);
    return 1;
  }

  if (session) {
    if (SlotHasScriptAction(*session, static_cast<std::size_t>(slot - 1))) {
      lua_pushnumber(L, 1);
      return 1;
    }
  }

  lua_pushnil(L);
  return 1;
}

bool RefreshAllActionSlotValidation(WorldSession &session) {
  bool changed = false;
  auto& assignments = session.action_assignments();
  for (std::size_t slot_index = 0; slot_index < kActionSlotCount; ++slot_index) {
    changed |= assignments.UpdateUsabilityState(
        slot_index, ComputeActionSlotUsability(session, slot_index));
  }
  return changed;
}

void ResetActionBarRuntimeState(WorldSession &session) {

  session.action_assignments().ClearAll();
  session.set_cached_bonus_action_bar_offset(0);
  session.action_page_state().Reset();
  ResetActionUseTransitionSequence();
}

void RefreshPetActionBarState(WorldSession &session) {

  RefreshGeneratedActionBarState(session);
  ScriptEventDispatch::Get().FirePetBarUpdate();
}

void RefreshGeneratedActionBarState(WorldSession &session) {
  session.pet().RefreshGeneratedBarState(session);
  SyncGeneratedActionSlots(session);

  const auto *override_spell_data = LookupActiveOverrideSpellDataEntry(session);
  const auto *shapeshift_form = LookupActiveShapeshiftFormEntry(session);
  const auto generated_bar = ::openwow::game::DescribeGeneratedActionBar(
      override_spell_data, shapeshift_form, &session.pet().pet_bar());

  const auto previous_offset = session.cached_bonus_action_bar_offset();
  const auto current_offset = generated_bar.bonus_bar_offset;
  if (current_offset == previous_offset) {
    return;
  }

  session.set_cached_bonus_action_bar_offset(current_offset);

  auto &dispatch = ScriptEventDispatch::Get();
  if (previous_offset == ::openwow::game::kGeneratedActionBarBonusOffset) {
    dispatch.FireActionbarPageChanged();
  }
  dispatch.FireUpdateBonusActionbar();
}

void RefreshActionBarBootstrapState(WorldSession &session) {

  auto& assignments = session.action_assignments();
  for (std::size_t slot = 0;
       slot < openwow::game::ActionAssignmentRuntime::kMaxActionButtons;
       ++slot) {
    const auto button = assignments.GetPresentationEntry(slot);
    if (button.IsEmpty()) {
      continue;
    }
    const bool invalid =
        (button.type == openwow::game::ActionPresentationKind::kSpell &&
         button.action != 0 &&
         !openwow::game::SpellbookSystem::Get().HasSpell(button.action)) ||
        (button.type ==
             openwow::game::ActionPresentationKind::kEquipmentSet &&
         session.equipment().find(button.action) == nullptr);
    if (invalid) {
      assignments.ClearAssignment(slot);
    }
  }
  if (RefreshAllActionSlotValidation(session)) {
    ScriptEventDispatch::Get().FireActionbarUpdateUsable();
  }
  RefreshGeneratedActionBarState(session);

  ScriptEventDispatch::Get().FireActionbarSlotChanged(0);
}

bool RefreshActionSlotsForChangedItemEntry(WorldSession &session, std::uint32_t item_entry) {
  if (item_entry == 0) {
    return false;
  }

  bool changed = false;
  auto& assignments = session.action_assignments();
  auto &dispatch = ScriptEventDispatch::Get();

  for (std::size_t slot_index = 0; slot_index < kActionSlotCount; ++slot_index) {
    const auto button = assignments.GetPresentationEntry(slot_index);
    if (button.IsEmpty()) {
      continue;
    }

    bool slot_affected = false;
    const auto action_item_id =
        ResolveStoredItemActionId(session.macros(), button);
    if (action_item_id == item_entry) {
      slot_affected = true;

      if (const auto learn_spell_id =
              FindKnownLearnSpellForItem(session, button);
          learn_spell_id != 0 && ComputeActionDisplayCount(session, button, slot_index) == 0) {
        ActionPresentationEntry promoted{};
        promoted.action = learn_spell_id;
        promoted.type = ActionPresentationKind::kSpell;
        assignments.SetPresentationEntry(slot_index, promoted);
      }
    } else {
      bool is_pet_action = false;
      const auto spell_id =
          ResolveInventoryDrivenSpellId(session, button, slot_index, &is_pet_action);
      if (!is_pet_action && spell_id != 0 &&
          SpellUsesChangedReagent(session, spell_id, item_entry)) {
        slot_affected = true;
      }
    }

    if (!slot_affected) {
      continue;
    }

    (void)assignments.UpdateUsabilityState(
        slot_index, ComputeActionSlotUsability(session, slot_index));
    dispatch.FireActionbarSlotChanged(static_cast<std::uint8_t>(slot_index + 1));
    changed = true;
  }

  return changed;
}

bool RefreshActionSlotsForChangedMacro(
    WorldSession &session, actions::macros::MacroId macro_id) {
  if (!macro_id.IsValid()) {
    return false;
  }

  bool changed = false;
  auto& assignments = session.action_assignments();
  auto &dispatch = ScriptEventDispatch::Get();

  for (std::size_t slot_index = 0; slot_index < kActionSlotCount; ++slot_index) {
    const auto button = assignments.GetPresentationEntry(slot_index);
    if (!SlotHasMacroReference(button) || button.action != macro_id.value()) {
      continue;
    }

    (void)assignments.UpdateUsabilityState(
        slot_index, ComputeActionSlotUsability(session, slot_index));
    dispatch.FireActionbarSlotChanged(static_cast<std::uint8_t>(slot_index + 1));
    changed = true;
  }

  return changed;
}

bool RefreshActionSlotsForEquipmentSet(WorldSession &session, std::uint32_t set_id) {
  if (set_id > 9) {
    return false;
  }

  bool changed = false;
  auto& assignments = session.action_assignments();
  auto &dispatch = ScriptEventDispatch::Get();

  for (std::size_t slot_index = 0; slot_index < kActionSlotCount; ++slot_index) {
    const auto button = assignments.GetPresentationEntry(slot_index);
    if (button.type != ActionPresentationKind::kEquipmentSet || button.action != set_id) {
      continue;
    }

    (void)assignments.UpdateUsabilityState(
        slot_index, ComputeActionSlotUsability(session, slot_index));
    dispatch.FireActionbarSlotChanged(static_cast<std::uint8_t>(slot_index + 1));
    changed = true;
  }

  return changed;
}

namespace {

template <typename Predicate>
bool RefreshActionSlotsForSpellPredicate(WorldSession &session, Predicate &&predicate) {
  bool changed = false;
  auto& assignments = session.action_assignments();
  auto &dispatch = ScriptEventDispatch::Get();

  for (std::size_t slot_index = 0; slot_index < kActionSlotCount; ++slot_index) {
    const auto button = assignments.GetPresentationEntry(slot_index);
    if (button.IsEmpty()) {
      continue;
    }

    bool is_pet_action = false;
    const auto spell_id =
        ResolveSpellLikeActionIdForValidation(session, button, slot_index, &is_pet_action);
    if (spell_id == 0 || is_pet_action || !predicate(spell_id)) {
      continue;
    }

    (void)assignments.UpdateUsabilityState(
        slot_index, ComputeActionSlotUsability(session, slot_index));
    dispatch.FireActionbarSlotChanged(static_cast<std::uint8_t>(slot_index + 1));
    changed = true;
  }

  return changed;
}

}

bool RefreshActionSlotsForSpellId(WorldSession &session, std::uint32_t spell_id) {
  if (spell_id == 0) {
    return false;
  }

  return RefreshActionSlotsForSpellPredicate(
      session, [spell_id](const std::uint32_t slot_spell_id) {
        return slot_spell_id == spell_id;
      });
}

bool RefreshActionSlotsForAttackActions(WorldSession &session) {
  const auto *dbc = session.GetDbcLoader();
  return RefreshActionSlotsForSpellPredicate(
      session, [dbc](const std::uint32_t spell_id) {
        return ::openwow::game::SpellHasAttackActionEffect(spell_id, dbc);
      });
}

bool RefreshActionSlotsForRangedAttackActions(WorldSession &session) {
  const auto *dbc = session.GetDbcLoader();
  return RefreshActionSlotsForSpellPredicate(
      session, [dbc](const std::uint32_t spell_id) {
        return ::openwow::game::SpellHasRangedAttackActionFlags(spell_id, dbc);
      });
}

int LuaIsUsableAction(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: IsUsableAction(slot)");
  }
  auto *session = GetWorldSession(L);
  int slot = static_cast<int>(lua_tonumber(L, 1));

  if (!session || slot < 1 || slot > 144) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  SyncGeneratedActionSlots(*session);
  const auto &usability =
      session->action_assignments().GetUsabilityState(static_cast<std::size_t>(slot - 1));
  if (!usability.is_usable) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  if (usability.not_enough_power) {
    lua_pushnil(L);
    lua_pushwowbool(L, true);
    return 2;
  }

  lua_pushwowbool(L, true);
  lua_pushnil(L);
  return 2;
}

int LuaIsCurrentAction(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: IsCurrentAction(slot)");
  }

  auto *session = GetWorldSession(L);
  int slot = static_cast<int>(lua_tonumber(L, 1));

  if (!session || slot < 1 || slot > 144) {
    lua_pushnil(L);
    return 1;
  }

  const auto slot_index = static_cast<std::size_t>(slot - 1);
  const auto btn = GetResolvedActionButton(*session, slot_index);
  bool is_current = false;

  if (const auto item_id = ResolveStoredItemActionId(session->macros(), btn);
      item_id != 0) {
    is_current = ::openwow::game::IsCurrentItemEntry(*session, item_id);
  } else if (IsAttackLikeAction(L, *session, btn, slot_index)) {
    is_current = IsAttackActionCurrent(*session);
  } else if (IsPetActionCurrent(L, *session, slot_index, btn)) {
    is_current = true;
  } else if (const auto spell_id = ResolveSpellLikeActionId(*session, btn, slot_index);
             spell_id != 0) {
    is_current = IsCurrentSpellIdActive(L, spell_id);
  }

  lua_pushwowbool(L, is_current);
  return 1;
}

int LuaIsAutoRepeatAction(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: IsAutorepeatAction(slot)");
  }
  auto *session = GetWorldSession(L);
  const int slot = static_cast<int>(lua_tonumber(L, 1));

  if (!session || slot < 1 || slot > 144) {
    lua_pushnil(L);
    return 1;
  }

  const auto slot_index = static_cast<std::size_t>(slot - 1);
  const auto btn = GetResolvedActionButton(*session, slot_index);

  bool is_pet_action = false;
  const auto spell_id =
      ResolveSpellLikeActionIdForValidation(*session, btn, slot_index, &is_pet_action);
  if (spell_id == 0 || is_pet_action) {
    lua_pushnil(L);
    return 1;
  }

  const auto &spell_client = session->spells();
  const bool is_autorepeat = (spell_client.GetAutoRepeatSpellId() == spell_id) ||
                             (spell_client.GetAutoAttackSpellId() == spell_id);

  if (is_autorepeat) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaIsAttackAction(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: IsAttackAction(slot)");
  }
  auto *session = GetWorldSession(L);
  int slot = static_cast<int>(lua_tonumber(L, 1));
  if (!session || slot < 1 || slot > 144) {
    lua_pushnil(L);
    return 1;
  }
  const auto slot_index = static_cast<std::size_t>(slot - 1);
  const auto btn = GetResolvedActionButton(*session, slot_index);
  lua_pushwowbool(L, IsAttackLikeAction(L, *session, btn, slot_index));
  return 1;
}

int LuaIsConsumableAction(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: IsConsumableAction(slot)");
  }
  auto *session = GetWorldSession(L);
  const int slot = static_cast<int>(lua_tonumber(L, 1));

  if (!session || slot < 1 || slot > 144) {
    lua_pushnil(L);
    return 1;
  }

  if (IsConsumableActionSlot(L, *session, static_cast<std::size_t>(slot - 1))) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaIsStackableAction(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: IsStackableAction(slot)");
  }
  auto *session = GetWorldSession(L);
  const int slot = static_cast<int>(lua_tonumber(L, 1));

  if (!session || slot < 1 || slot > 144) {
    lua_pushnil(L);
    return 1;
  }

  const auto slot_index = static_cast<std::size_t>(slot - 1);
  const auto btn = GetResolvedActionButton(*session, slot_index);
  const auto item_id = ResolveStoredItemActionId(session->macros(), btn);
  if (item_id != 0) {
    if (const auto *item = RequireItemDefinitions(L).GetItem(item_id);
        item != nullptr && item->stackable > 1) {
      lua_pushnumber(L, 1.0);
      return 1;
    }
  }

  lua_pushnil(L);
  return 1;
}

int LuaGetActionText(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetActionText(slot)");
  }
  auto *session = GetWorldSession(L);
  int slot = static_cast<int>(lua_tonumber(L, 1));

  if (session && slot >= 1 && slot <= 144) {
    const auto btn = GetResolvedActionButton(*session, static_cast<std::size_t>(slot - 1));
    if (SlotHasMacroReference(btn)) {
      const auto macro = session->macros().FindMacro(
          ::openwow::game::actions::macros::MacroId(btn.action));
      if (macro) {
        lua_pushstring(L, macro->name.c_str());
        return 1;
      }
    } else if (btn.type == ::openwow::game::ActionPresentationKind::kEquipmentSet) {
      const auto* set = RequireEquipmentSets(L).find(btn.action);
      if (set != nullptr) {
        lua_pushstring(L, set->name.c_str());
        return 1;
      }
    }
  }

  lua_pushnil(L);
  return 1;
}

int LuaUseAction(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: UseAction(slot, [, target] [, button])");
  }

  auto *session = GetWorldSession(L);
  int slot = static_cast<int>(lua_tonumber(L, 1));

  if (!session || !IsActionSlotInRange(slot)) {
    return 0;
  }

  const auto slot_index = static_cast<std::size_t>(slot - 1);
  if (session->held_cursor() != nullptr &&
      CursorHasActionPayload(*session->held_cursor()) &&
      !IsPickupPlaceBlockedActionSlot(slot_index)) {

    if (HandleActionSlotClick(L, *session, slot_index, true)) {
      PublishActionUseTransition();
    }
    return 0;
  }

  const auto target_guid = ResolveUseActionTargetGuid(L, *session);
  if (!target_guid.has_value()) {
    return 0;
  }

  const auto button_arg = GetUseActionButtonArg(L);
  const auto btn = GetResolvedActionButton(*session, slot_index);
  if (btn.IsEmpty())
    return 0;

  if (SlotHasItemAction(btn)) {
    const auto item_id = btn.action;
    const auto *item = RequireItemDefinitions(L).GetItem(item_id);
    if (!item) {
      return 0;
    }

    const bool prefer_equipped_use = item->IsEquippable();
    const auto loc =
        prefer_equipped_use ? FindEquippedInventoryItemByEntry(L, item_id)
                            : FindBagInventoryItemByEntry(L, item_id);
    if (loc.has_value()) {
      if (session->held_cursor() != nullptr) {
        session->held_cursor()->Clear();
      }

      if (session->interaction().TryQueueBindOnUseConfirmation(loc->item->guid, loc->item->entry,
                                                               loc->item->flags, *target_guid)) {
        PublishActionUseTransition();
        return 0;
      }

      if (*target_guid == 0) {
        if (const auto *item_template =
                session->query_cache().GetOrRequestItemTemplate(loc->item->entry);
            item_template != nullptr &&
            item_targeting::TryStartItemSpellTargeting(
                session->GetDbcLoader(), session->spells(),
                *loc->item, *item_template)) {
          PublishActionUseTransition();
          return 0;
        }
      }

      if (session->interaction().SendUseItem(loc->bag, loc->slot, 0,
                                             *target_guid)) {
        PublishActionUseTransition();
      }
    } else if (prefer_equipped_use) {
      PickupInventoryItemByEntry(L, item_id);
    }
    return 0;
  }

  if (SlotHasMacroReference(btn)) {

    session->macros().ExecuteByUniqueId(
        ::openwow::game::actions::macros::MacroId(btn.action),
        ::openwow::game::actions::macros::adapters::retail::
            DecodeMacroInputButton(button_arg));
    return 0;
  }

  if (btn.type == ::openwow::game::ActionPresentationKind::kEquipmentSet) {
    if (NotifyWearEquipmentSetById(L, btn.action)) {
      PublishActionUseTransition();
    }
    return 0;
  }

  if (btn.type == ::openwow::game::ActionPresentationKind::kPet &&
      slot_index >= kFirstPickupPlaceBlockedActionSlot && slot_index < kFirstMultiCastActionSlot) {
    if (const auto *pet_action = ResolvePetActionButton(*session, slot_index);
        pet_action != nullptr) {
      const bool dispatched =
          ExecutePetActionUseAction(*session, pet_action->raw, *target_guid);

      ScriptEventDispatch::Get().FireActionbarUpdateCooldown();
      if (dispatched) {
        PublishActionUseTransition();
      }
    }
    return 0;
  }

  if (const auto spell_id = ResolveSpellLikeActionId(*session, btn, slot_index); spell_id != 0) {
    if (UseSpellLikeAction(L, *session, spell_id, *target_guid)) {
      PublishActionUseTransition();
    }
  }

  return 0;
}

int LuaPickupAction(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: PickupAction(slot)");
  }

  auto *session = GetWorldSession(L);
  int slot = static_cast<int>(lua_tonumber(L, 1));
  if (!session || !IsActionSlotInRange(slot))
    return 0;

  const auto slot_index = static_cast<std::size_t>(slot - 1);

  if (!CanPerformActionBarProtectedAction()) {
    return 0;
  }

  if (IsPickupPlaceBlockedActionSlot(slot_index)) {
    return 0;
  }

  if (session->held_cursor() != nullptr &&
      CursorHasActionPayload(*session->held_cursor())) {
    HandleActionSlotClick(L, *session, slot_index, false);
    return 0;
  }

  PickupActionFromSlot(L, *session, slot_index);
  return 0;
}

int LuaPlaceAction(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: PlaceAction(slot)");
  }

  auto *session = GetWorldSession(L);
  int slot = static_cast<int>(lua_tonumber(L, 1));
  if (!session || !IsActionSlotInRange(slot))
    return 0;

  const auto slot_index = static_cast<std::size_t>(slot - 1);
  if (IsPickupPlaceBlockedActionSlot(slot_index)) {
    return 0;
  }

  if (session->held_cursor() == nullptr ||
      !CursorHasActionPayload(*session->held_cursor())) {
    return 0;
  }

  HandleActionSlotClick(L, *session, slot_index, true);
  return 0;
}

int LuaGetSpellInfo(lua_State *L) {
  const auto spell_id = ResolveSpellIdOrCurrentSpellQuery(L, "GetSpellInfo");
  if (!spell_id.has_value() || *spell_id == 0) {
    return 0;
  }

  auto* session = GetWorldSession(L);

  const auto *dbc =
      session != nullptr ? session->GetDbcLoader() : GetDbcLoader(L);

  if (dbc) {
    const auto *spell = dbc->spell().LookupEntry(*spell_id);
    if (spell) {

      lua_pushstring(L, std::string(spell->spell_name).c_str());

      lua_pushstring(L, std::string(spell->rank).c_str());

      std::string icon_path;
      if (session != nullptr &&
          ::openwow::game::SpellHasAttackActionEffect(*spell_id, dbc)) {
        icon_path = ::openwow::game::ResolveActiveAttackActionTexturePath(*session, dbc);
      } else if (session != nullptr &&
                 ::openwow::game::SpellHasRangedAttackActionFlags(*spell_id, dbc)) {
        icon_path = ::openwow::game::ResolveActiveRangedActionTexturePath(*session, dbc);
      }
      if (icon_path.empty()) {
        icon_path = "Interface\\Icons\\INV_Misc_QuestionMark";
        const auto *icon = dbc->spell_icon().LookupEntry(spell->spell_icon_id);
        if (icon && !std::string_view(icon->icon_path).empty()) {
          icon_path = std::string(icon->icon_path);
        }
      }
      lua_pushstring(L, icon_path.c_str());

      const auto power_type = static_cast<std::int32_t>(spell->power_type);
      std::uint32_t cost = spell->mana_cost;
      if (cost == 0 && spell->mana_cost_percentage > 0)
        cost = spell->mana_cost_percentage;
      cost = ::openwow::game::NormalizePowerDisplayValue(cost, power_type);
      lua_pushnumber(L, static_cast<lua_Number>(cost));

      bool is_funnel = (spell->attributes & 0x40u) != 0;
      lua_pushboolean(L, is_funnel ? 1 : 0);

      lua_pushnumber(L, static_cast<lua_Number>(power_type));

      std::uint32_t cast_time = 0;
      if (spell->casting_time_index > 0) {
        const auto *ct = dbc->spell_cast_times().LookupEntry(spell->casting_time_index);
        if (ct) {
          cast_time = ct->base_cast_time;
        }
      }
      lua_pushnumber(L, static_cast<lua_Number>(cast_time));

      float min_range = 0, max_range = 0;
      if (spell->range_index > 0) {
        const auto *rng = dbc->spell_range().LookupEntry(spell->range_index);
        if (rng) {
          min_range = rng->range_min;
          max_range = rng->range_max;
        }
      }
      lua_pushnumber(L, static_cast<lua_Number>(min_range));
      lua_pushnumber(L, static_cast<lua_Number>(max_range));

      return 9;
    }
  }

  return 0;
}

int LuaGetSpellCooldown(lua_State *L) {
  auto *session = GetWorldSession(L);
  const auto spell_id = ResolveSpellIdOrCurrentSpellQuery(L, "GetSpellCooldown");
  if (!spell_id.has_value()) {
    return 0;
  }

  if (!session || *spell_id == 0) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 1);
    return 3;
  }

  std::optional<ActionCooldownResult> best_cooldown;
  if (const auto resolved = openwow::game::ResolveSpellbookCooldown(
          session->spell_book(), *spell_id);
      resolved.has_value()) {
    best_cooldown = ActionCooldownResult{
        .start_time_s = resolved->start_time_s,
        .duration_s = resolved->duration_s,
        .enabled = resolved->enabled,
    };
  }

  const auto *active_player = session->objects().GetActivePlayer();
  if (active_player != nullptr) {
    const auto predicted_rune_cooldown =
        openwow::game::PredictRuneDrivenSpellCooldown(
            *spell_id, active_player, &session->aura(), session->GetDbcLoader(),
            session->runes(), openwow::core::GameClock::GetTickCount32(),
            openwow::core::GameClock::GetTickCountSeconds());
    if (predicted_rune_cooldown.has_value() &&
        (!best_cooldown.has_value() ||
         predicted_rune_cooldown->ExpiresAt() > best_cooldown->ExpiresAt())) {
      best_cooldown = ActionCooldownResult{
          .start_time_s = predicted_rune_cooldown->start_time_s,
          .duration_s = predicted_rune_cooldown->duration_s,
          .enabled = 1.0,
      };
    }
  }

  if (best_cooldown.has_value()) {
    lua_pushnumber(L, best_cooldown->start_time_s);
    lua_pushnumber(L, best_cooldown->duration_s);
    lua_pushnumber(L, best_cooldown->enabled);
    return 3;
  }

  lua_pushnumber(L, 0);
  lua_pushnumber(L, 0);
  lua_pushnumber(L, 1);
  return 3;
}

int LuaGetSpellCount(lua_State *L) {
  const auto query = ResolveScriptCurrentSpellQuery(L, "GetSpellCount");
  const auto* session = GetWorldSession(L);
  if (!query.has_value() || session == nullptr) {
    return 0;
  }

  lua_pushnumber(
      L,
      static_cast<lua_Number>(
          ::openwow::game::GetReagentCastCount(*session, query->spell_id)));
  return 1;
}

int LuaIsSpellKnown(lua_State *L) {
  if (!lua_isnumber(L, 1) || static_cast<int>(lua_tonumber(L, 1)) <= 0)
    luaL_error(L, "Usage: IsSpellKnown(spellID[, isPet])");
  const auto spell_id = static_cast<std::uint32_t>(lua_tonumber(L, 1));
  const bool is_pet_book = ScriptReadBoolArgOrDefault(L, 2, false);
  auto *session = GetWorldSession(L);

  bool known = FindSpellBookSlotIndexBySpellId(session, spell_id, is_pet_book).has_value();
  if (!known && is_pet_book && session != nullptr) {
    known = HasPetActionSpellId(*session, spell_id);
  }

  lua_pushboolean(L, known);
  return 1;
}

int LuaGetNumSpellTabs(lua_State *L) {
  auto &sys = ::openwow::game::SpellbookSystem::Get();
  lua_pushnumber(L, static_cast<lua_Number>(sys.GetNumTabs()));
  return 1;
}

int LuaGetSpellTabInfo(lua_State *L) {
  if (lua_isnumber(L, 1) == 0) {
    return luaL_error(L, "Usage: GetSpellTabInfo(index)");
  }

  int tab = static_cast<int>(lua_tonumber(L, 1));
  auto &sys = ::openwow::game::SpellbookSystem::Get();

  if (tab >= 1 && static_cast<size_t>(tab) <= sys.GetNumTabs()) {
    const auto *t = sys.GetTab(static_cast<size_t>(tab - 1));
    if (t) {
      if (t->skill_line_id == 0) {
        lua_getglobal(L, "GENERAL_SPELLS");
        if (!lua_isstring(L, -1)) {
          lua_pop(L, 1);
          lua_pushstring(L, t->name.c_str());
        }
      } else if (t->name.empty()) {

        lua_pushnil(L);
      } else {
        lua_pushstring(L, t->name.c_str());
      }

      if (t->texture.empty()) {
        lua_pushnil(L);
      } else {
        lua_pushstring(L, t->texture.c_str());
      }
      lua_pushnumber(L, static_cast<lua_Number>(t->offset));
      lua_pushnumber(L, static_cast<lua_Number>(t->num_spells));
      lua_pushnumber(L, static_cast<lua_Number>(t->highest_rank_offset));

      lua_pushnumber(L, static_cast<lua_Number>(t->num_known.value_or(0u)));
      return 6;
    }
  }

  lua_pushnil(L);
  lua_pushnil(L);
  lua_pushnumber(L, 0.0);
  lua_pushnumber(L, 0.0);
  lua_pushnumber(L, 0.0);
  lua_pushnumber(L, 0.0);
  return 6;
}

int LuaGetBonusBarOffset(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    lua_pushnumber(L, 0);
    return 1;
  }

  lua_pushnumber(L, static_cast<lua_Number>(session->cached_bonus_action_bar_offset()));
  return 1;
}

int LuaGetActionBarPage(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session != nullptr) {
    const auto *override_spell_data = LookupActiveOverrideSpellDataEntry(*session);
    const auto *shapeshift_form = LookupActiveShapeshiftFormEntry(*session);
    const auto generated_bar = ::openwow::game::DescribeGeneratedActionBar(
        override_spell_data, shapeshift_form, &session->pet().pet_bar());
    if (generated_bar.uses_generated_slots()) {
      lua_pushnumber(L, 1);
      return 1;
    }
  }

  const auto page =
      session != nullptr
          ? session->action_page_state().current()
          : ::openwow::game::actions::ActionPage::First();
  lua_pushnumber(L, static_cast<lua_Number>(page.value()));
  return 1;
}

int LuaChangeActionBarPage(lua_State *L) {
  const int page = static_cast<int>(lua_tonumber(L, 1));
  if (page < 1 || page > 6) {
    return luaL_error(L, "ChangeActionBarPage() needs a page in the range 1 to %d", 6);
  }

  if (!CanPerformActionBarProtectedAction()) {
    return 0;
  }

  auto *session = GetWorldSession(L);
  const auto action_page =
      ::openwow::game::actions::ActionPage::FromValue(
          static_cast<std::uint8_t>(page));
  if (session == nullptr || !action_page ||
      !session->action_page_state().Set(*action_page)) {
    return 0;
  }

  ScriptEventDispatch::Get().FireActionbarPageChanged();
  return 0;
}

int LuaGetActionBarToggles(lua_State *L) {
  auto *session = GetWorldSession(L);
  uint8_t flags = 0;
  if (session != nullptr) {
    if (const auto *player = session->objects().GetActivePlayer(); player != nullptr) {
      flags = player->GetActionBarToggles();
    }
  }

  for (int i = 0; i < 4; ++i) {
    if (flags & (1 << i)) {
      lua_pushnumber(L, 1.0);
    } else {
      lua_pushnil(L);
    }
  }
  return 4;
}

bool CheckActionTargetInRange(WorldSession &session, std::size_t slot_index,
                              openwow::game::ObjectGuid target_guid,
                              bool *out_in_range) {
  using ::openwow::game::MacroCatalog;
  using ::openwow::game::SpellTargetResult;
  using ::openwow::game::SpellTargetValidator;

  if (slot_index >= kActionSlotCount) {
    return false;
  }

  const auto btn = GetResolvedActionButton(session, slot_index);
  bool is_pet_action = false;
  const auto spell_id = ResolveSpellLikeActionIdForValidation(
      session, btn, slot_index, &is_pet_action);
  if (spell_id == 0) {
    return false;
  }

  const auto *dbc = session.GetDbcLoader();
  if (!dbc) {
    return false;
  }

  const auto *spell = dbc->spell().LookupEntry(spell_id);
  if (!spell) {
    return false;
  }

  if (target_guid.IsEmpty() && SlotHasMacroReference(btn)) {
    const auto macro = session.macros().FindMacro(
        ::openwow::game::actions::macros::MacroId(btn.action));
    if (macro && macro->target_guid != 0) {
      target_guid = openwow::game::ObjectGuid(macro->target_guid);
    }
  }

  if (target_guid.IsEmpty()) {
    target_guid = session.objects().GetTargetGuid();
  }

  const openwow::game::CGUnit_C *caster = nullptr;
  if (is_pet_action) {
    const auto &pet_bar = session.pet().pet_bar();
    if (pet_bar.active && !pet_bar.guid.IsEmpty()) {
      caster = session.objects().GetUnit(pet_bar.guid);
    }
  } else {
    caster = session.objects().GetActivePlayer();
  }
  if (!caster) {
    return false;
  }

  const openwow::game::CGUnit_C *target = nullptr;
  if (!target_guid.IsEmpty()) {
    target = session.objects().GetUnit(target_guid);
  }
  if (!target) {
    return false;
  }

  const auto result = SpellTargetValidator::ValidateUnitTarget(
      session, *dbc, spell_id, *caster, *target, true);

  if (result == SpellTargetResult::kValid) {
    if (out_in_range) *out_in_range = true;
    return true;
  }
  if (result == SpellTargetResult::kOutOfRange ||
      result == SpellTargetResult::kTooClose) {
    if (out_in_range) *out_in_range = false;
    return true;
  }

  if (out_in_range) *out_in_range = false;
  return true;
}

int LuaIsActionInRange(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    lua_pushnil(L);
    return 1;
  }

  auto *session = GetWorldSession(L);
  const int slot = static_cast<int>(lua_tonumber(L, 1));

  if (!session || slot < 1 || slot > 144) {
    lua_pushnil(L);
    return 1;
  }

  const auto slot_index = static_cast<std::size_t>(slot - 1);

  openwow::game::ObjectGuid target_guid;
  const auto unit_id = SafeLuaString(L, 2);
  if (!unit_id.empty()) {
    target_guid = ResolveUnitId(session, std::string(unit_id));
  }

  bool in_range = false;
  if (CheckActionTargetInRange(*session, slot_index, target_guid, &in_range)) {
    lua_pushnumber(L, in_range ? 1.0 : 0.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaIsEquippedAction(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: IsEquippedAction(slot)");
  }
  auto *session = GetWorldSession(L);
  const int slot = static_cast<int>(lua_tonumber(L, 1));

  if (!session || slot < 1 || slot > 144) {
    lua_pushnil(L);
    return 1;
  }

  const auto slot_index = static_cast<std::size_t>(slot - 1);
  const auto btn = GetResolvedActionButton(*session, slot_index);
  const auto item_id = ResolveStoredItemActionId(session->macros(), btn);
  const auto *item_template = RequireItemDefinitions(L).GetItem(item_id);
  if (::openwow::game::IsEquippedActionItemByEntry(
          session->inventory_replica(), item_id, item_template,
                                                   session->GetDbcLoader())) {
    lua_pushnumber(L, 1.0);
    return 1;
  }

  lua_pushnil(L);
  return 1;
}

int LuaSetActionBarToggles(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  std::uint8_t flags = 0;
  if (ScriptReadBoolArgOrDefault(L, 1, false))
    flags |= 0x01;
  if (ScriptReadBoolArgOrDefault(L, 2, false))
    flags |= 0x02;
  if (ScriptReadBoolArgOrDefault(L, 3, false))
    flags |= 0x04;
  if (ScriptReadBoolArgOrDefault(L, 4, false))
    flags |= 0x08;

  session->interaction().SendSetActionBarToggles(flags);
  return 0;
}

int LuaActionHasRange(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    lua_pushnil(L);
    return 1;
  }

  auto *session = GetWorldSession(L);
  const auto slot = openwow::ui::TruncateLuaNumberToI32(lua_tonumber(L, 1));

  if (!session || slot < 1 ||
      slot > static_cast<std::int32_t>(kActionSlotCount)) {
    lua_pushnil(L);
    return 1;
  }

  const auto slot_index = static_cast<std::size_t>(slot - 1);
  bool is_pet_action = false;
  const auto btn = GetResolvedActionButton(*session, slot_index);
  const auto spell_id = ResolveActionButtonSpellLikeActionId(
      L, *session, slot_index, btn, &is_pet_action);
  if (spell_id == 0) {
    lua_pushnil(L);
    return 1;
  }

  const auto *dbc = session->GetDbcLoader();
  if (!dbc) {
    lua_pushnil(L);
    return 1;
  }

  const auto *spell = dbc->spell().LookupEntry(spell_id);
  if (!spell) {
    lua_pushnil(L);
    return 1;
  }

  const ::openwow::game::CGUnit_C *caster = nullptr;
  if (is_pet_action) {
    const auto &pet_bar = session->pet().pet_bar();
    if (pet_bar.active && !pet_bar.guid.IsEmpty()) {
      caster = session->objects().GetUnit(pet_bar.guid);
    }
  } else {
    caster = session->objects().GetActivePlayer();
  }
  if (!caster) {
    lua_pushnil(L);
    return 1;
  }

  const auto *range_entry = dbc->spell_range().LookupEntry(spell->range_index);
  const bool use_friendly =
      ::openwow::game::GetHelpfulHarmfulDisposition(*spell) ==
      ::openwow::game::SpellHelpfulHarmfulDisposition::kHelpful;
  const auto window = ::openwow::game::SpellTargetValidator::GetUntargetedRangeWindow(
      *spell, range_entry, *caster, use_friendly, session);

  if (HasRetailActionRange(window)) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaGetActionAutocast(lua_State* L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetActionAutocast(slot)");
  }

  const int slot = static_cast<int>(lua_tonumber(L, 1));
  auto* session = GetWorldSession(L);
  if (!session || slot < 1 || slot > 144) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  constexpr std::size_t kPetActionSlotBase = 120;
  const auto slot_index = static_cast<std::size_t>(slot - 1);
  if (slot_index < kPetActionSlotBase ||
      slot_index >= kPetActionSlotBase + 10) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  const auto& pet_bar = session->pet().pet_bar();
  if (!pet_bar.active) {
    lua_pushnil(L);
    lua_pushnil(L);
    return 2;
  }

  const auto pet_slot = slot_index - kPetActionSlotBase;
  const auto& action = pet_bar.action_bar[pet_slot];
  const auto* autocast_state = &action;
  if (action.ActionKind() == 1) {
    if (const auto* packet_spell = session->pet().FindSpellEntryBySpellId(action.ActionId());
        packet_spell != nullptr) {

      autocast_state = packet_spell;
    }
  }

  if (autocast_state->IsAutocastAllowed()) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  if (autocast_state->IsAutocastEnabled()) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 2;
}

int LuaGetMultiCastBarOffset(lua_State* L) {

  lua_pushinteger(L, 6);
  return 1;
}

}

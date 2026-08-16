#include "openwow/game/actions/macros/adapters/retail/retail_macro_condition_adapter.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/actions/bindings/adapters/retail/modified_click_adapter.h"
#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/game/actions/macros/rules/retail_macro_text.h"
#include "openwow/game/inventory/equipment/equipped_item_type_matcher.h"
#include "openwow/game/localization.h"
#include "openwow/game/flyable_area.h"
#include "openwow/game/group_system.h"
#include "openwow/game/talent_info.h"
#include "openwow/game/unit_query_bridge.h"
#include "openwow/game/vehicle_helpers.h"
#include "openwow/game/vehicle_system.h"
#include "openwow/game/world_session.h"
#include "openwow/game/world_environment_state.h"
#include "openwow/ui/widgets/simple_frame.h"

#include <cctype>
#include <utility>

namespace openwow::game::actions::macros::adapters::retail {
namespace {

constexpr std::uint32_t kVehicleUiSeatFlag = 0x20000000u;

bool EqualsNoCase(const std::string_view lhs,
                  const std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t index = 0; index < lhs.size(); ++index) {
    if (std::tolower(static_cast<unsigned char>(lhs[index])) !=
        std::tolower(static_cast<unsigned char>(rhs[index]))) {
      return false;
    }
  }
  return true;
}

bool VehicleUiActive(VehicleSystem& vehicles) {
  if (!vehicles.IsInVehicle()) {
    return false;
  }
  const auto seat = vehicles.GetSeat(vehicles.GetSeatIndex());
  return seat && seat->hasUI();
}

bool UnitHasVehicleUi(WorldSession& session,
                      VehicleSystem& vehicles,
                      const ObjectGuid guid) {
  if (guid.IsEmpty()) {
    return false;
  }
  if (guid == session.objects().GetActivePlayerGuid()) {
    return VehicleUiActive(vehicles);
  }
  if (const auto* unit = session.objects().GetUnit(guid)) {
    const auto* seat = ResolveUnitVehicleSeatEntry(session, *unit);
    return seat != nullptr && (seat->flags & kVehicleUiSeatFlag) != 0;
  }
  const auto* seat = LookupCachedUnitVehicleSeatEntry(session, guid);
  return seat != nullptr && (seat->flags & kVehicleUiSeatFlag) != 0;
}

std::optional<std::string> CursorType(
    const actions::held_cursor::HeldCursor& cursor) {
  using Kind = actions::held_cursor::Kind;
  switch (cursor.kind()) {
    case Kind::LiveItem:
    case Kind::ActionBarItem:
    case Kind::AmmoItem:
    case Kind::GuildBankItem:
      return "item";
    case Kind::PlayerMoney:
      return "money";
    case Kind::GuildBankMoney:
      return "guildbankmoney";
    case Kind::Spell:
      return "spell";
    case Kind::PetAction:
    case Kind::StablePet:
      return "petaction";
    case Kind::MerchantItem:
      return "merchant";
    case Kind::Macro:
      return "macro";
    case Kind::EquipmentSet:
      return "equipmentset";
    case Kind::None:
      return std::nullopt;
  }
  return std::nullopt;
}

rules::MacroConditionSnapshot Capture(
    MacroCatalog& macros, WorldSession& session,
    const GroupSystem& groups, VehicleSystem& vehicles,
    const actions::held_cursor::HeldCursor& cursor,
    const TalentInfoStore& talents,
    const UnitQueryBridge& unit_queries, const WorldEnvironmentState& environment,
    const OutdoorPositionQuery& outdoor_query,
    const std::function<std::uint16_t()>& modifier_state_query,
    const std::function<actions::ActionPage()>& action_page_query,
    const std::string_view target_token) {
  rules::MacroConditionSnapshot state;
  state.in_party = groups.GetTrackedPartyMemberCount() != 0;
  state.in_raid = groups.IsInRaid();
  state.flyable = false;
  state.vehicle_ui = VehicleUiActive(vehicles);
  state.stance = macros.GetRetailShapeshiftFormIndex();
  state.action_bar_page =
      action_page_query ? action_page_query()
                        : actions::ActionPage::First();
  state.specialization =
      talents.GetDefaultGroupIndex(false) + 1u;
  state.cursor_type = CursorType(cursor);

  const auto modifier_state =
      modifier_state_query ? modifier_state_query() : 0u;
  const auto running_button = macros.RunningMacroInputButton();
  const std::string current_button =
      running_button ? std::string(running_button->value()) : std::string{};
  const std::string condition_button =
      running_button ? std::string(running_button->value()) : "LeftButton";
  state.modifier_matches =
      [&macros, modifier_state, current_button](
          const std::optional<std::string_view> argument) {
        if (!argument) {
          if (const auto modified = macros.QueryModifiedClickCondition(
                  std::nullopt, modifier_state, current_button)) {
            return *modified;
          }
          return actions::bindings::adapters::retail::
              AnyModifierKeyDown(modifier_state);
        }
        std::string_view remainder = *argument;
        const auto bits = actions::bindings::adapters::retail::
            ParseModifierBits(*argument, remainder);
        if (bits != 0) {
          return actions::bindings::adapters::retail::
              ModifierBitsMatch(bits, modifier_state);
        }
        return macros.QueryModifiedClickCondition(
                   *argument, modifier_state, current_button)
            .value_or(false);
      };
  state.button_matches =
      [condition_button](const std::string_view argument) {
        std::string ordinal_name;
        auto candidate = argument;
        if (!candidate.empty() &&
            std::isdigit(static_cast<unsigned char>(candidate.front()))) {
          const auto ordinal =
              rules::ParseRetailMacroUnsignedPrefix(candidate);
          ordinal_name = openwow::ui::widgets::MouseButtonName(
              openwow::ui::widgets::MouseButtonScriptOrdinalToFlag(
                  static_cast<int>(ordinal)));
          candidate = ordinal_name;
        }
        return EqualsNoCase(candidate, condition_button);
      };

  state.bonus_bar = session.cached_bonus_action_bar_offset();
  state.flyable = IsFlyableAreaForActivePlayer(session);
  const auto* player = session.objects().GetLocalPlayerTyped();
  if (player != nullptr) {

    const auto player_position = player->GetPosition();
    const auto outdoors =
        outdoor_query ? outdoor_query(player_position.x, player_position.y,
                                      player_position.z)
                      : std::nullopt;
    state.outdoors = outdoors.value_or(!environment.IsIndoors());
    state.indoors = !state.outdoors;
    state.combat = player->State().IsInCombat();
    state.stealth = player->State().IsStealth();
    state.swimming = player->Movement().IsSwimming();
    state.mounted = player->Mount().IsMountedStateActive(*player);
    state.flying = player->Movement().IsFlying();
    const auto channel_id = player->Casts().GetChannelSpellId(*player);
    state.channeling = channel_id != 0;
    if (channel_id != 0 && session.GetDbcLoader() != nullptr) {
      if (const auto* spell =
              session.GetDbcLoader()->spell().LookupEntry(channel_id);
          spell != nullptr && !spell->spell_name.empty()) {
        state.channel_spell = std::string(spell->spell_name);
      }
    }
  }
  if (!state.combat) {
    const auto& pet_guids = session.pet().pet_guids();
    if (!pet_guids.empty() && pet_guids.front() != 0) {
      if (const auto* pet =
              session.objects().GetUnit(ObjectGuid(pet_guids.front()))) {
        state.combat = pet->State().IsInCombat();
      }
    }
  }

  const auto normalized_target =
      rules::CopyRetailMacroSpan(target_token, 32);
  const auto unit_token = normalized_target.empty()
                              ? std::string_view("target")
                              : std::string_view(normalized_target);
  const auto guid =
      unit_queries.ResolveToGuid(&session, unit_token);
  if (!guid.IsEmpty()) {
    const auto raw_guid = guid.GetRawValue();
    state.target.exists = session.objects().Get(guid) != nullptr ||
                          groups.IsActivePlayerPartyOrRaidUnitGuid(
                              session.objects(), raw_guid);
    state.target.in_party =
        groups.IsPartyUnitGuid(session.objects(), raw_guid);
    state.target.in_raid =
        state.target.in_party ||
        groups.IsRaidUnitGuid(session.objects(), raw_guid);
    state.target.has_vehicle_ui =
        UnitHasVehicleUi(session, vehicles, guid);
    if (const auto snapshot =
            unit_queries.Query(&session, unit_token)) {
      state.target.dead = snapshot->isDead;
    }
    const auto* active_player = session.objects().GetActivePlayer();
    const auto* target = session.objects().GetUnit(guid);
    if (active_player != nullptr && target != nullptr) {
      state.target.can_help =
          active_player->Interaction().CanAssistSpellTarget(*target, false);
      state.target.can_harm =
          active_player->Interaction().CanAttackSpellTarget(*target);
    }
  }

  const auto& pet_bar = session.pet().pet_bar();
  ObjectGuid pet_guid = pet_bar.guid;
  if (pet_guid.IsEmpty() && !session.pet().pet_guids().empty()) {
    pet_guid = ObjectGuid(session.pet().pet_guids().front());
  }
  if (const auto* pet = pet_guid.IsEmpty()
                            ? nullptr
                            : session.objects().GetUnit(pet_guid)) {
    state.pet_name = pet->GetName();
    if (pet_bar.creature_family != 0 &&
        session.GetDbcLoader() != nullptr) {
      if (const auto* family = session.GetDbcLoader()
                                   ->creature_family()
                                   .LookupEntry(pet_bar.creature_family);
          family != nullptr && !family->name.empty()) {
        state.pet_family = std::string(family->name);
      }
    }
  }
  state.equipped_item_type = [&session](const std::string_view query) {
    return session.objects().GetLocalPlayerTyped() != nullptr &&
           MatchesEquippedItemTypeQuery(
               session.inventory_replica(), query,
               {.query_cache = &session.query_cache(),
                .dbc = session.GetDbcLoader(),
                .localize =
                    [](const std::string_view key) {
                      return Localization::Get().GetString(std::string(key));
                    }});
  };
  return state;
}

}

rules::MacroConditionRules::SnapshotProvider
MakeRetailMacroConditionSnapshotProvider(
    MacroCatalog& macros, WorldSession& session,
    const GroupSystem& groups, VehicleSystem& vehicles,
    const actions::held_cursor::HeldCursor& cursor,
    const TalentInfoStore& talents,
    const UnitQueryBridge& unit_queries, const WorldEnvironmentState& environment,
    OutdoorPositionQuery outdoor_query,
    std::function<std::uint16_t()> modifier_state_query,
    std::function<actions::ActionPage()> action_page_query) {
  return [&macros, &session, &groups, &vehicles, &cursor,
          &talents, &unit_queries, &environment,
          outdoor_query = std::move(outdoor_query),
          modifier_state_query = std::move(modifier_state_query),
          action_page_query = std::move(action_page_query)](
             const std::string_view target) {
    return Capture(macros, session, groups, vehicles, cursor, talents,
                   unit_queries, environment, outdoor_query,
                   modifier_state_query, action_page_query, target);
  };
}

}

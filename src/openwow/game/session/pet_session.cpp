#include "openwow/game/inventory/equipment/adapters/protocol/equipment_set_packet_codec.h"

#include "openwow/game/world_session.h"
#include "openwow/ui/frame_script_events.h"
#include "openwow/ui/game/runtime/world_ui_runtime_context.h"
#include "openwow/data/db_cache_instances.h"
#include "openwow/game/session/handlers/commerce/mail_packets.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/core/client_misc.h"
#include "openwow/core/console.h"
#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_entries_extended.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/game/achievements/adapters/protocol/achievement_protocol.h"
#include "openwow/game/achievements/rules/achievement_category_resolver.h"
#include "openwow/game/barber_shop.h"
#include "openwow/game/calendar/adapters/protocol/calendar_date_fields_packed.h"
#include "openwow/game/calendar/calendar_time.h"
#include "openwow/world/camera/world_camera.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/chat_message_formatters.h"
#include "openwow/game/combat/application/client_control_transition.h"
#include "openwow/game/combat/adapters/ui/auto_attack_activity_presenter.h"
#include "openwow/game/comsat_client.h"
#include "openwow/game/activities/dance/adapters/protocol/dance_protocol.h"
#include "openwow/game/activities/dance/application/dance_studio.h"
#include "openwow/world/environment/day_night.h"
#include "openwow/game/emote_manager.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/player_control_runtime.h"
#include "openwow/game/inventory/equipment/equipment_sets.h"
#include "openwow/game/faction_system.h"
#include "openwow/game/game_misc_utils.h"
#include "openwow/game/group_system.h"
#include "openwow/game/hyperlink.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/knowledge_base.h"
#include "openwow/game/localization.h"
#include "openwow/game/inventory/loot/adapters/protocol/loot_packet_codec.h"
#include "openwow/game/inventory/loot/adapters/ui/loot_roll_result_presenter.h"
#include "openwow/game/inventory/loot/loot_state.h"
#include "openwow/game/minimap_ping.h"
#include "openwow/game/money_display.h"
#include "openwow/game/object_types.h"
#include "openwow/game/packet_reader.h"
#include "openwow/game/quest_dialog_close.h"
#include "openwow/game/quest_log.h"
#include "openwow/game/quest_poi.h"
#include "openwow/game/readable_text.h"
#include "openwow/game/reputation_info.h"
#include "openwow/game/spell_runtime_values.h"
#include "openwow/game/spell_target_resolver.h"
#include "openwow/game/spell_c_internals.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_failure_names.h"
#include "openwow/game/spell_query_bridge.h"
#include "openwow/game/taxi_map_frame.h"
#include "openwow/game/taxi_runtime_slice.h"
#include "openwow/game/taxi_system.h"
#include "openwow/game/tutorial_system.h"
#include "openwow/game/unit_sound_dispatch.h"
#include "openwow/game/voice_chat.h"
#include "openwow/game/world_scene_state.h"
#include "openwow/net/client_services.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/ui/game/api/game_lua_api_guild_roster_view.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/game/combat/adapters/ui/combo_point_presentation.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/minimap_system.h"
#include "openwow/ui/game/quest_log_interleaved.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/ui_error_manager.h"
#include "openwow/foundation/diagnostics/logging.h"

#include "openwow/game/account_data.h"
#include "openwow/game/account_data_runtime_sync.h"
#include "openwow/game/achievements/application/tracked_achievement_state.h"
#include "openwow/game/calendar/calendar_system.h"
#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/game/title_system.h"
#include "openwow/game/talent_info.h"
#include "openwow/core/init_subsystems.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace openwow::game {

namespace {

constexpr std::uint32_t kStableResultNotEnoughMoney = 1;
constexpr std::uint32_t kStableResultRefreshSwap = 8;
constexpr std::uint32_t kStableResultRefreshStable = 9;
constexpr std::uint32_t kStableResultBuySlot = 10;
constexpr std::uint32_t kStableResultUpdateEvent = 11;
constexpr std::uint32_t kStableResultCantControlExotic = 12;
constexpr std::uint32_t kStableInteractionAuraType = 292;
constexpr int kPetFeedbackSystemMessage174 = 174;
constexpr int kPetFeedbackSystemMessage175 = 175;
constexpr int kPetFeedbackSystemMessage358 = 358;
constexpr int kPetFeedbackSystemMessage359 = 359;
constexpr int kPetHelpfulFeedbackSystemMessage637 = 637;

constexpr int kPetBrokenSystemMessage = 452;

constexpr std::size_t kPetNameMaxBytes = 0x50u;
constexpr std::size_t kPetDeclinedNameMaxBytes = 0x60u;
constexpr std::size_t kPetDeclinedNameFormCount = 5u;

constexpr std::uint8_t kUnknownPetTameFailureCode = 0u;

int ResolvePetActionFeedbackSystemMessageId(const WorldSession &session,
                                            const PetActionFeedbackResult &feedback) {
  switch (feedback.feedback) {
  case PetFeedback::kDisplaySystemMessage358:
    return kPetFeedbackSystemMessage358;
  case PetFeedback::kClearCooldownDisplay175:
    return kPetFeedbackSystemMessage175;
  case PetFeedback::kDisplaySystemMessage359:
    return kPetFeedbackSystemMessage359;
  case PetFeedback::kClearCooldownDisplay174Or637: {
    const auto *dbc = session.GetDbcLoader();
    if (dbc == nullptr) {
      return kPetFeedbackSystemMessage174;
    }

    const auto *spell = dbc->spell().LookupEntry(feedback.spell_id);
    if (spell != nullptr &&
        GetHelpfulHarmfulDisposition(*spell) == SpellHelpfulHarmfulDisposition::kHelpful) {
      return kPetHelpfulFeedbackSystemMessage637;
    }

    return kPetFeedbackSystemMessage174;
  }
  case PetFeedback::kNone:
  default:
    return 0;
  }
}

void FirePetSpellCooldownResetUi(WorldSession &session, const bool had_matching_cooldown) {
  auto &dispatch = ui::game::ScriptEventDispatch::Get();
  if (had_matching_cooldown) {
    dispatch.FireActionbarSpellAndShapeshiftCooldownUpdates(session.HasAvailableShapeshiftForms());
  }
  dispatch.FirePetBarUpdateCooldown();
}

bool ActivePlayerHasAuraType(const WorldSession &session, const std::uint32_t aura_type) {
  const auto *dbc = session.GetDbcLoader();
  const auto *player = session.objects().GetLocalPlayer();
  if (dbc == nullptr || player == nullptr) {
    return false;
  }

  const auto &auras = session.aura().GetAuras(player->GetGuid().GetRawValue());
  return std::any_of(auras.begin(), auras.end(), [dbc, aura_type](const AuraSlotInfo &aura) {
    if (aura.spell_id == 0) {
      return false;
    }

    const auto *spell = dbc->spell().LookupEntry(aura.spell_id);
    if (spell == nullptr) {
      return false;
    }

    return Spell_HasAuraType(spell->effect_apply_aura.data(), aura_type);
  });
}

}

void WorldSession::HandlePetSpells(const net::wotlk::WorldPacket &pkt) {
  const auto previous_guid = pet_.pet_bar().guid.GetRawValue();
  const auto previous_active = pet_.pet_bar().active;
  if (!pet_.HandlePetSpells(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto current_guid = pet_.pet_bar().guid.GetRawValue();
  const auto pet_changed =
      previous_active != pet_.pet_bar().active || previous_guid != current_guid;
  if (pet_changed) {
    TalentInfoStore::Get().SetPetTalentCreatureFamily(
        pet_.pet_bar().active ? static_cast<std::uint32_t>(pet_.pet_bar().creature_family) : 0u);
  }

  auto &dispatch = ui::game::ScriptEventDispatch::Get();
  ui::game::detail::RefreshPetActionBarState(*this);
  dispatch.FirePetBarUpdateCooldown();
  dispatch.FireSpellsChanged();
  dispatch.FireEvent(ui::game::events::TRAINER_UPDATE);

  if (!pet_changed) {
    return;
  }

  ui::game::UnitTokenRegistry::Get().SetPet(current_guid);
  const auto *player = map_runtime_.objects().GetLocalPlayer();
  if (player) {
    dispatch.FireUnitPet(player->GetGuid().GetRawValue());
  }

  const auto *active_player = map_runtime_.objects().GetActivePlayer();
  if (active_player != nullptr && active_player->State().GetHealth() == 0 &&
      pet_.pet_bar().active && current_guid != 0) {
    dispatch.FireEvent(ui::game::events::RAISED_AS_GHOUL);
  }
  if (pet_.pet_bar().active) {

    dispatch.FireEvent(ui::game::events::PET_BAR_UPDATE_USABLE);
  }
}

void WorldSession::HandlePetMode(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  std::uint64_t packet_pet_guid = 0;
  const bool targets_primary_pet =
      reader.ReadU64(packet_pet_guid) &&
      ObjectGuid(packet_pet_guid) == pet_.GetPrimaryPetGuid();

  if (!pet_.HandlePetMode(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  if (targets_primary_pet) {
    ui::game::ScriptEventDispatch::Get().FirePetBarUpdate();
  }
}

void WorldSession::HandlePetActionFeedback(const net::wotlk::WorldPacket &pkt) {
  PetActionFeedbackResult feedback{};
  if (!pet_.HandlePetActionFeedback(pkt.payload.data(), pkt.payload.size(), &feedback)) {
    return;
  }

  if (const int system_message_id = ResolvePetActionFeedbackSystemMessageId(*this, feedback);
      system_message_id != 0) {
    ui::game::DisplaySystemMessage(system_message_id);
  }

  if (feedback.feedback != PetFeedback::kClearCooldownDisplay174Or637 &&
      feedback.feedback != PetFeedback::kClearCooldownDisplay175) {
    return;
  }

  FirePetSpellCooldownResetUi(*this, pet_.ClearSpellCooldown(feedback.spell_id));
}

void WorldSession::HandlePetCastFailed(const net::wotlk::WorldPacket &pkt) {
  PetCastFailedResult result{};
  if (!pet_.HandlePetCastFailed(pkt.payload.data(), pkt.payload.size(), &result)) {
    return;
  }

  const auto *dbc = GetDbcLoader();
  if (dbc == nullptr || dbc->spell().LookupEntry(result.spell_id) == nullptr) {
    return;
  }

  FirePetSpellCooldownResetUi(*this, pet_.ClearSpellCooldown(result.spell_id));
}

void WorldSession::HandlePetNameQueryResponse(const net::wotlk::WorldPacket &pkt) {
  pet_.HandlePetNameQueryResponse(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleStabledPets(const net::wotlk::WorldPacket &pkt) {
  const auto previous_guid = pet_.stable_list().npc_guid.GetRawValue();
  (void)pet_.HandleStabledPets(pkt.payload.data(), pkt.payload.size());

  pet_.ResetStablePetSelection();

  auto stable_master_guid = pet_.stable_list().npc_guid.GetRawValue();
  if (stable_master_guid != previous_guid) {
    ui::game::SetNpcInteractionTarget(ObjectGuid(stable_master_guid));
  }

  ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::PET_STABLE_SHOW);
}

void WorldSession::HandlePetGuids(const net::wotlk::WorldPacket &pkt) {
  pet_.HandlePetGuids(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandlePetTameFailure(const net::wotlk::WorldPacket &pkt) {

  std::uint8_t tame_failure = kUnknownPetTameFailureCode;
  if (!pkt.payload.empty()) {
    tame_failure = pkt.payload.front();
  }
  DisplayPetTameFailure(tame_failure);
}

void WorldSession::HandlePetNameInvalid(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());

  std::uint32_t rename_result = 0;
  (void)reader.ReadU32(rename_result);
  ui::game::PetNameCache_HandlePetRenameResult(static_cast<int>(rename_result));

  std::string base_name;
  (void)reader.ReadCString(base_name, kPetNameMaxBytes);

  std::uint8_t has_declined_names = 0;
  (void)reader.ReadU8(has_declined_names);
  if (has_declined_names == 0) {
    return;
  }

  std::array<std::string, kPetDeclinedNameFormCount> declined_names{};
  for (auto &declined_name : declined_names) {
    (void)reader.ReadCString(declined_name, kPetDeclinedNameMaxBytes);
  }

  ui::game::ScriptEventDispatch::Get().FireEventArgs(
      ui::game::events::PET_FORCE_NAME_DECLENSION,
      {base_name, declined_names[0], declined_names[1], declined_names[2],
       declined_names[3], declined_names[4]});
}

void WorldSession::HandlePetBroken(const net::wotlk::WorldPacket &pkt) {
  (void)pkt;
  ui::game::DisplaySystemMessage(kPetBrokenSystemMessage);
}

void WorldSession::HandlePetActionSound(const net::wotlk::WorldPacket &pkt) {
  if (!pet_handler_.HandlePetActionSound(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto& info = pet_handler_.last_pet_action_sound();
  if (info.has_value()) {
    const auto* unit = map_runtime_.objects().GetUnit(ObjectGuid(info->pet_guid));
    if (unit != nullptr) {
      UnitSound_PlayPetActionSound(*unit, info->sound_id);
    }
  }
}

void WorldSession::HandlePetDismissSound(const net::wotlk::WorldPacket &pkt) {
  if (!pet_handler_.HandlePetDismissSound(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto &request = pet_handler_.last_pet_dismiss_sound();
  const auto *const dbc = GetDbcLoader();
  if (!request.has_value() || dbc == nullptr) {
    return;
  }

  const auto *const display =
      dbc->creature_display_info().LookupEntry(request->creature_display_id);
  if (display == nullptr || display->npc_sound_id == 0) {
    return;
  }
  const auto *const sound_data =
      dbc->creature_sound_data().LookupEntry(display->npc_sound_id);
  if (sound_data == nullptr || sound_data->sound_pet_dismiss_id == 0) {
    return;
  }

  const float emitter[3] = {request->x, request->y, request->z + 1.0f};
  (void)sound_runtime_.PlaySoundKit(sound_data->sound_pet_dismiss_id, emitter);
}

void WorldSession::HandlePetUnlearnConfirm(const net::wotlk::WorldPacket &pkt) {
  pet_handler_.HandlePetUnlearnConfirm(pkt.payload.data(), pkt.payload.size());
}

void WorldSession::HandleStableResult(const net::wotlk::WorldPacket &pkt) {
  if (!pet_handler_.HandleStableResult(pkt.payload.data(), pkt.payload.size())) {
    return;
  }

  const auto result = pet_handler_.last_stable_result();
  switch (result) {
  case kStableResultNotEnoughMoney:
    ui::game::DisplaySystemMessage(40);
    return;
  case kStableResultBuySlot:
    pet_.IncrementStableMaxSlots();
    [[fallthrough]];
  case kStableResultRefreshSwap:
  case kStableResultRefreshStable:
    break;
  case kStableResultUpdateEvent:
    ui::game::ScriptEventDispatch::Get().FireEvent(ui::game::events::PET_STABLE_UPDATE);
    return;
  case kStableResultCantControlExotic:
    DisplayPetTameFailure(static_cast<std::uint8_t>(result));
    return;
  default:
    return;
  }

  const auto stable_master_guid = pet_.stable_list().npc_guid.GetRawValue();
  if (stable_master_guid == 0 && !ActivePlayerHasAuraType(*this, kStableInteractionAuraType)) {
    return;
  }

  interaction_.SendListStabledPets(stable_master_guid);
}

void WorldSession::HandlePetRenameable(const net::wotlk::WorldPacket &pkt) {
  (void)pkt;

  ui::game::ScriptEventDispatch::Get().FirePetRenameable();
}

void WorldSession::HandlePetUpdateComboPoints(const net::wotlk::WorldPacket &pkt) {
  PacketReader reader(pkt.payload.data(), pkt.payload.size());
  ObjectGuid packet_pet_guid;
  if (!reader.ReadPackedGuid(packet_pet_guid)) {
    return;
  }

  const ObjectGuid active_pet_guid = pet_.GetPrimaryPetGuid();
  if (packet_pet_guid != active_pet_guid) {
    return;
  }

  if (!pet_handler_.HandlePetUpdateComboPoints(
          pkt.payload.data() + reader.Position(), reader.Remaining())) {
    return;
  }

  spellbook_private_usability().Refresh(*this);
  auto &dispatch = ui::game::ScriptEventDispatch::Get();
  dispatch.FireUnitComboPoints(active_pet_guid.GetRawValue());
  dispatch.FirePetBarUpdateUsable();
}

}

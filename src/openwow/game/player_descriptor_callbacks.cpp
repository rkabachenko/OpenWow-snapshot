
#include "openwow/game/player_descriptor_callbacks.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"

#include <cmath>
#include <cstring>
#include <string>

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/core/console.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/descriptor_callback_registry.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cggameobject.h"
#include "openwow/game/objects/cgobject.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/skill_dbc_helpers.h"
#include "openwow/game/targeting.h"
#include "openwow/game/targeting/application/target_selection_service.h"
#include "openwow/game/tutorial_system.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/world/camera/world_camera.h"
#include "openwow/game/group_system.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/shapeshift_bonus_bar.h"
#include "openwow/game/spell_shapeshift_mask.h"
#include "openwow/game/player_unit_field_event_callbacks.h"
#include "openwow/game/profession_system.h"
#include "openwow/game/skill_info.h"
#include "openwow/game/commerce/trade/trade_interaction.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"

namespace openwow::game {

static const DescriptorCallbackInfo s_descriptor_callbacks[] = {

    {4, 540, 8, "equip_0"},   {4, 548, 8, "equip_1"},
    {4, 556, 8, "equip_2"},   {4, 564, 8, "equip_3"},
    {4, 572, 8, "equip_4"},   {4, 580, 8, "equip_5"},
    {4, 588, 8, "equip_6"},   {4, 596, 8, "equip_7"},
    {4, 604, 8, "equip_8"},   {4, 612, 8, "equip_9"},
    {4, 620, 8, "equip_10"},  {4, 628, 8, "equip_11"},
    {4, 636, 8, "equip_12"},  {4, 644, 8, "equip_13"},
    {4, 652, 8, "equip_14"},  {4, 660, 8, "equip_15"},
    {4, 668, 8, "equip_16"},  {4, 676, 8, "equip_17"},
    {4, 684, 8, "equip_18"},

    {3, 0x00, 16, "combo_target"},
    {4, 0x0C,  4, "guild_id"},
    {4, 0x10,  4, "guild_rank"},
    {4, 0x20,  4, "root_on_unit"},
    {3, 0x18,  8, "unroot_on_player"},
    {4, 0x08,  4, "player_flags"},
    {4, 0x24,  4, "guild_appearance"},
    {4, 0x1E,  1, "anim_byte_1e"},
    {4, 0x1D,  1, "inebriation_byte"},
    {4, 0x1B,  1, "rest_state"},
    {4, 0x2B4, 4, "anim_2b4"},
    {4, 0x17,  1, "hair_color"},
    {4, 0x16,  1, "hair_style"},
    {4, 0x18,  1, "facial_hair"},
    {4, 0x14,  1, "skin_color"},
    {4, 0x770, 8, "active_control_guid"},

    {4, 0x10EC, 100, "combat_ratings"},

    {4, 0x0DEC,   4, "shield_block"},

    {4, 0x105C,   4, "mod_target_resistance"},

    {4, 0x778, 8, "known_titles_0"},

    {4, 0x780, 8, "known_titles_1"},
    {4, 0x788, 8, "known_titles_2"},

    {4, 0xFF4, 4, "xp_exhaustion"},

    {4, 0x798, 4, "player_xp"},

    {4, 0x79C, 4, "player_next_level_xp"},

    {4, 0xFFC, 28, "damage_done_mods_0"},

    {4, 0x1018, 28, "damage_done_mods_1"},

    {4, 0x1034, 28, "damage_done_mods_2"},

    {4, 0x1050, 4, "damage_done_mods_3"},

    {4, 0xDB0, 52, "damage_done_mods_4"},

    {3, 0x88, 56, "damage_done_mods_unit"},

    {4, 0x1060, 4, "damage_done_mods_5"},

    {4, 0x10D4, 2, "pvp_kills_0"},

    {4, 0x10E0, 4, "pvp_kills_1"},

    {4, 0x10D8, 4, "pvp_kills_2"},

    {4, 0x1C, 4, "pvp_rank"},

    {4, 0x10E8, 4, "faction_standing"},

    {4, 0xFF8, 4, "coinage"},

};

const DescriptorCallbackInfo* GetDescriptorCallbackTable(
    std::uint32_t& out_count) {
  out_count = sizeof(s_descriptor_callbacks) / sizeof(s_descriptor_callbacks[0]);
  return s_descriptor_callbacks;
}

namespace {

constexpr std::uint32_t kSkillFlagNoSkillUpMessage = 0x402;

constexpr std::uint32_t kSkillCategorySecondary = 9;
constexpr std::uint32_t kSkillCategoryProfession = 11;

constexpr int kSysMsg_SkillUnlearned = 57;
constexpr int kSysMsg_SkillIncreased = 58;

void RefreshActivePlayerSkillInfo(WorldSession& session) {
  if (auto* player = session.objects().GetActivePlayer()) {
    if (auto* dbc = session.GetDbcLoader()) {
      SkillInfoStore::Get().UpdateFromPlayer(*player, *dbc);
    }
  }
}

}

void OnSkillValueDescriptorChanged(WorldSession& session,
                                   const SkillValueChangeInfo& info) {

  if (info.is_active_player &&
      info.old_raw_value != info.new_adjusted_value) {

    ui::game::ScriptEventDispatch::Get().FirePlayerCombatStatEvents();

    core::ida::ConsoleLog("Skill %d increased from %d to %d",
                          info.skill_line_id, info.old_raw_value,
                          info.new_adjusted_value);

    RefreshActivePlayerSkillInfo(session);

    const openwow::data::dbc::SkillLineEntry* skill_line_entry = nullptr;
    std::int32_t skill_category_id = 0;
    if (auto* dbc = session.GetDbcLoader()) {
      skill_line_entry = dbc->skill_line().LookupEntry(info.skill_line_id);
      if (skill_line_entry != nullptr) {
        skill_category_id = skill_line_entry->category_id;
      }
    }

    const auto* race_class_info =
        SkillDbcHelpers::Get().FindBySkillId(
            info.race, info.player_class, info.skill_line_id);

    if (race_class_info &&
        (race_class_info->flags & kSkillFlagNoSkillUpMessage) == 0) {
      if (info.new_adjusted_value == 0) {

        if (skill_line_entry != nullptr) {
          const std::string name(skill_line_entry->name);
          ui::game::DisplaySystemMessage(kSysMsg_SkillUnlearned, name.c_str());
        } else {
          ui::game::DisplaySystemMessage(kSysMsg_SkillUnlearned);
        }
      } else if (info.old_raw_value > 0) {

        if (skill_line_entry != nullptr) {
          const std::string name(skill_line_entry->name);
          ui::game::DisplaySystemMessage(
              kSysMsg_SkillIncreased, name.c_str(),
              static_cast<int>(info.new_adjusted_value));
        } else {
          ui::game::DisplaySystemMessage(kSysMsg_SkillIncreased);
        }
      }
    }

    if (skill_category_id == kSkillCategorySecondary ||
        skill_category_id == kSkillCategoryProfession) {
      if (session.objects().GetLocalPlayer() != nullptr) {
        auto pkt = net::wotlk::PacketSender::BuildQuestgiverStatusMultipleQuery();
        session.interaction().SendRawPacket(pkt);
      }
    }
  }

  ui::game::ScriptEventDispatch::Get().FireEvent(
      ui::game::events::TRAINER_UPDATE);

  if (ProfessionSystem::Get().HasTradeSkillWindow()) {
    ui::game::ScriptEventDispatch::Get().FireEvent(
        ui::game::events::TRADE_SKILL_UPDATE);
  }

  if (session.mail().HasPendingMail()) {
    ui::game::ScriptEventDispatch::Get().FireMailInboxUpdate();
  }
}

void OnSkillModifierDescriptorChanged(WorldSession& session,
                                      const bool is_active_player) {

  if (is_active_player) {
    ui::game::ScriptEventDispatch::Get().FirePlayerCombatStatEvents();
  }

  RefreshActivePlayerSkillInfo(session);
}

void OnSkillRangeDescriptorChanged(WorldSession& session) {

  ui::game::ScriptEventDispatch::Get().FireEvent(
      ui::game::events::TRAINER_UPDATE);
  if (ProfessionSystem::Get().HasTradeSkillWindow()) {
    ui::game::ScriptEventDispatch::Get().FireEvent(
        ui::game::events::TRADE_SKILL_UPDATE);
  }
  RefreshActivePlayerSkillInfo(session);
}

void OnCombatRatingUpdate(WorldSession& session) {

  ui::game::ScriptEventDispatch::Get().FireCombatRatingUpdate();

  RefreshActivePlayerSkillInfo(session);
}

namespace {

struct RestStateAction {
  int system_message_id;
  std::uint32_t tutorial_id;
};

constexpr int kNoSystemMessage = 730;
constexpr std::uint32_t kNoTutorial = 60;
constexpr std::uint32_t kMaxRestStates = 3;

constexpr RestStateAction kRestStateActions[kMaxRestStates] = {
    {730, 60},
    {369, 25},
    {370, 60},
};

}

void OnRestStateDescriptorChanged(std::uint8_t rest_state) {

  ui::game::ScriptEventDispatch::Get().FireEvent(
      ui::game::events::UPDATE_EXHAUSTION);

  if (rest_state >= kMaxRestStates) {
    return;
  }

  const auto& action = kRestStateActions[rest_state];

  if (action.system_message_id != kNoSystemMessage) {
    ::openwow::ui::game::DisplaySystemMessage(action.system_message_id);
  }

  if (action.tutorial_id != kNoTutorial) {
    TutorialSystem::Instance().TriggerTutorial(action.tutorial_id);
  }
}

void OnInebriationDescriptorChanged(WorldSession& session,
                                    const float normalized_inebriation,
                                    bool is_active_player,
                                    const CGUnit_C *player_unit,
                                    const CGUnit_C *target_unit) {
  if (!is_active_player) {
    return;
  }

  if (target_unit != nullptr && player_unit != nullptr &&
      player_unit->Interaction().CanAttackSpellTarget(*target_unit)) {
    ui::game::ScriptEventDispatch::Get().FireEventArgs(
        ui::game::events::UNIT_LEVEL, {std::string("target")});
  }

  if (auto* const camera = session.world_camera(); camera != nullptr) {
    camera->SetFov(normalized_inebriation * 1.5533429f);
  }
}

void ItemSwapState::Reset() {
  guild_appearance_count = 0;
  initial_spells_count = 0;
  pending_swap_count = 0;
  tabard_save_pending = false;
}

GuildAppearanceChangeResult OnGuildAppearanceDescriptorChanged(
    const std::uint32_t guild_id,
    const std::uint32_t guild_timestamp,
    std::vector<GuildAppearanceEntry>& tracking_entries) {
  for (auto& entry : tracking_entries) {
    if (entry.guild_id == guild_id) {
      if (entry.guild_timestamp == guild_timestamp) {
        return {false, guild_id};
      }
      entry.guild_timestamp = guild_timestamp;
      return {true, guild_id};
    }
  }

  tracking_entries.push_back({guild_id, guild_timestamp});
  return {true, guild_id};
}

bool OnPlayerFlagsDescriptorChanged(std::uint32_t new_flags,
                                    std::uint32_t old_flags,
                                    bool is_active_player,
                                    PlayerFlagsChangeResult& result) {
  result = {};

  const std::uint32_t changed = new_flags ^ old_flags;

  result.refresh_spells = (changed & 0x800Eu) != 0;

  result.latch_pvp_display = result.refresh_spells && is_active_player;
  result.pvp_display_value = new_flags & 2u;

  result.update_pvp_state = (changed & 0x40300u) != 0;

  result.dirty_portrait = (changed & 0xC00u) != 0;

  result.fire_per_unit_399 = true;

  if (!is_active_player) {
    return true;
  }

  result.update_model = (changed & 0x10u) != 0;

  if (changed & 0x20u) {
    result.fire_ffa_pvp_event = true;
    result.trigger_ffa_tutorials = (new_flags & 0x20u) != 0;
  }

  if (changed & 0x200u) {
    result.fire_pvp_toggle = true;
    result.pvp_toggle_on = (new_flags & 0x200u) != 0;
  }

  if (changed & 0x400u) {
    result.dispatch_visual_toggle_400 = true;
    result.visual_400_on = (new_flags & 0x400u) != 0;
  }

  if (changed & 0x800u) {
    result.dispatch_visual_toggle_800 = true;
    result.visual_800_on = (new_flags & 0x800u) != 0;
  }

  result.fire_talent_group = (changed & 0x3000u) != 0;

  if (changed & 0x20000u) {
    result.fire_barber_event = true;
    result.barber_open = (new_flags & 0x20000u) != 0;
  }

  result.refresh_rune_power = (changed & 0x1800000u) != 0;

  if (changed & 0x2000000u) {
    result.fire_rune_type_event = true;
    result.rune_type_set = (new_flags & 0x2000000u) != 0;
  }

  if (changed & 0x10000u) {
    result.fire_raid_toggle = true;
    result.raid_toggle_on = (new_flags & 0x10000u) != 0;
  }

  return true;
}

std::uint32_t ProcessPlayerFlagChanges(const PlayerFlagChangeInfo& info,
                                        FlagChangeEvent* events,
                                        std::uint32_t max_events) {
  std::uint32_t count = 0;
  const auto push = [&](std::uint32_t id, const char* fmt = nullptr,
                        int arg = 0) {
    if (count < max_events) {
      events[count++] = {id, fmt, arg};
    }
  };

  if (!info.is_active_player) return 0;

  if (info.changed_bits & 0x20) {
    push(DescriptorEventId::kPvPDesired);
  }

  if (info.changed_bits & 0x3000) {
    push(DescriptorEventId::kPlaytimeChanged);
  }

  if (info.changed_bits & 0x20000) {
    if (info.new_flags & 0x20000)
      push(DescriptorEventId::kBarberShopOpen);
    else
      push(DescriptorEventId::kBarberShopClose);
  }

  if (info.changed_bits & 0x2000000) {
    if (info.new_flags & 0x2000000)
      push(DescriptorEventId::kRuneTypeAlt);
    else
      push(DescriptorEventId::kRuneType);
  }

  if (info.changed_bits & 0x10000) {
    if (info.new_flags & 0x10000)
      push(DescriptorEventId::kEnableLowLevelRaid);
    else
      push(DescriptorEventId::kDisableLowLevelRaid);
  }

  return count;
}

bool ProcessInventorySlotChange(const InventorySlotChangeInfo& info,
                                 FlagChangeEvent* events,
                                 std::uint32_t max_events,
                                 std::uint32_t& out_count) {
  out_count = 0;
  const auto push = [&](std::uint32_t id, const char* fmt = nullptr,
                        int arg = 0) {
    if (out_count < max_events) {
      events[out_count++] = {id, fmt, arg};
    }
  };

  std::uint32_t slot = info.slot_index;

  if (slot <= 18) {
    std::uint32_t slot_1based = slot + 1;
    if (info.new_guid != 0) {
      push(DescriptorEventId::kEquipSlotChanged, "%d%d",
           static_cast<int>(slot_1based));
    } else {
      push(DescriptorEventId::kEquipSlotChanged, "%d",
           static_cast<int>(slot_1based));
    }
    return true;
  }

  if ((slot >= 39 && slot <= 66) || (slot >= 67 && slot <= 73)) {
    push(DescriptorEventId::kItemLost, "%d",
         static_cast<int>(slot - 38));
    return true;
  }

  return true;
}

bool ProcessQuestLogChange(const QuestLogChangeInfo& info,
                           QuestLogChangeResult& out) {
  out = {};

  if (info.is_active_player) {
    out.send_questgiver_status_multiple_query = true;
  }

  const bool quest_id_changed = info.old_quest_id != info.new_quest_id;
  const bool complete_bit_gained =
      (info.old_state & kQuestLogStateBitComplete) == 0 &&
      (info.new_state & kQuestLogStateBitComplete) != 0;

  if (info.old_quest_id != 0 && (quest_id_changed || complete_bit_gained)) {
    out.notification_quest_id = info.old_quest_id;

    if (info.old_quest_template_available) {
      out.notify_quest_change = true;

      if (info.old_quest_type == 1) {
        out.trigger_tutorial_0x28 = true;
        out.trigger_tutorial_0x26 = true;
      }

      out.increment_completion_counter = true;
      out.updated_completion_counter = info.quest_completion_counter + 1;
      if (!info.tutorial_0x14_completed &&
          out.updated_completion_counter >= 4) {
        out.trigger_tutorial_0x14 = true;
      }

      out.fire_quest_log_update = true;
      out.quest_log_update_visible_index = info.old_quest_visible_index;
    } else {
      out.request_async_template_lookup = true;
      out.async_lookup_quest_id = info.old_quest_id;
    }
  }

  if (info.current_quest_template_available &&
      info.current_quest_visible_index >= 0 &&
      (info.current_quest_flags & kQuestFlagAutoRewarded) != 0 &&
      info.current_quest_is_complete) {
    out.send_auto_complete = true;
    out.auto_complete_guid = info.object_guid;
    out.auto_complete_quest_id = info.old_quest_id;
  }

  if (info.new_quest_id != 0 &&
      info.old_quest_id != info.new_quest_id &&
      info.new_quest_id == info.selected_quest_id) {
    out.reset_quest_tracking = true;
    out.tracking_quest_id = info.new_quest_id;
  }

  return true;
}

bool ProcessEquipmentVisualChange(const EquipmentVisualChangeInfo& info,
                                  EquipmentVisualChangeResult& out) {

  out = {};

  std::uint32_t abs_old = static_cast<std::uint32_t>(std::abs(info.old_entry));
  std::uint32_t abs_new = static_cast<std::uint32_t>(std::abs(info.new_entry));
  if (abs_old == abs_new &&
      (info.old_entry > 0) == (info.new_entry > 0)) {
    out.early_return = true;
    return true;
  }

  out.rebuild_trade_skill = true;
  out.refresh_spell_ui = true;

  out.abs_new_entry = abs_new;
  out.abs_old_entry = abs_old;

  if (info.is_active_player) {
    out.refresh_action_bar = true;

    switch (info.slot_index) {
      case 15:
        out.fire_unit_attack_main = true;
        out.rebuild_spellbook = true;
        break;

      case 16:
        out.fire_unit_attack_off = true;
        break;

      case 17:
        out.fire_ranged_slot_event = true;
        out.rebuild_spellbook = true;
        break;

      default:
        break;
    }
  }

  const bool is_ranged_2h = (info.body_slot_inv_type == 26 ||
                             info.body_slot_inv_type == 25);

  switch (info.slot_index) {
    case 15: {
      out.clear_hand_attachment_0 = true;

      out.dispatch_barber_shop = true;
      break;
    }

    case 16: {
      out.clear_hand_attachment_1 = true;

      out.dispatch_barber_shop = true;
      break;
    }

    case 17: {
      if (info.sheathe_state == 2) {

        out.ranged_update_sheathe = true;
        out.ranged_sheathe_arg = is_ranged_2h ? 1 : 2;
      }

      if (info.has_character_model) {
        out.ranged_clear_char_model = true;
      }

      if (info.has_unit_model && info.sheathe_state != 2) {
        out.ranged_cleanup_unit_model = true;
      }

      break;
    }

    default: {
      out.update_character_model_component = true;

      out.dispatch_barber_shop = true;
      break;
    }
  }

  out.walk_child_attachments = true;

  if (info.sheathe_state == 2 && info.slot_index == 17) {
    out.change_sheathe_to_ranged = true;
    out.refresh_stand_animation = true;
  }

  out.queue_portrait_model_event = true;

  return true;
}

static constexpr std::uint16_t kEquipSlotBaseOffset = 540;
static constexpr std::uint16_t kEquipSlotStride = 8;
static constexpr std::uint32_t kMaxEquipSlotIndex = 18;

bool OnEquipmentSlotDescriptorChanged(
    const EquipmentSlotDescriptorCallbackParams &params,
    const EquipmentVisualChangeInfo &info,
    EquipmentVisualChangeResult &out) {

  if (!params.player_resolved) {
    return true;
  }

  const std::uint32_t slot =
      (static_cast<unsigned>(params.descriptor_offset) - kEquipSlotBaseOffset) /
      kEquipSlotStride;

  if (slot > kMaxEquipSlotIndex) {
    return true;
  }

  ProcessEquipmentVisualChange(info, out);

  return true;
}

constexpr std::uint8_t kUnitVisFlagUntrackable = 0x04u;
constexpr std::uint8_t kUnitVisFlagCreep = 0x02u;
bool ShouldShowOnMinimap(const MinimapTrackInfo& info) {
  if (info.viewer_is_dead_or_ghost) {
    return false;
  }

  if ((info.unit_vis_flags & kUnitVisFlagUntrackable) != 0) {
    return false;
  }

  if ((info.unit_dynamic_flags & kUnitDynFlagTrackUnit) != 0) {
    return true;
  }

  if ((info.unit_vis_flags & kUnitVisFlagCreep) != 0 &&
      info.viewer_has_creep_view_flag) {
    return true;
  }

  if (info.creature_type_id > 0 &&
      (info.player_tracking_mask & (1u << (info.creature_type_id - 1)))) {
    return true;
  }

  return false;
}

void HandlePlayerFieldBytes2Changed(WorldSession &session) {
  auto &dispatch = ui::game::ScriptEventDispatch::Get();

  session.spellbook_private_usability().Refresh(session);

  if (ui::game::detail::RefreshAllActionSlotValidation(session)) {
    dispatch.FireActionbarUpdateUsable();
  }
  dispatch.FirePetBarUpdateUsable();

  dispatch.FireInventoryCooldownsChanged();

  ui::game::detail::RefreshGeneratedActionBarState(session);
}

namespace {

constexpr bool IsFormWithMorphSound(std::uint8_t form_id) {
  return form_id == 1 || form_id == 2 || form_id == 3 || form_id == 4 ||
         form_id == 5 || form_id == 8 || form_id == 29 || form_id == 31;
}

constexpr std::uint32_t kMorphSoundKitId = 10063;

constexpr std::uint32_t kFormFlagSuppressNpcClose = 0x8u;

}

void OnPlayerShapeshiftFormChanged(WorldSession &session,
                                   std::uint8_t new_form_id) {
  auto *player = session.objects().GetActivePlayer();
  if (player == nullptr) {
    return;
  }

  player->State().ClearAutoLearnProcessed();

  if (player->Casts().GetCurrentCast().spell_id != 0) {
    player->Casts().ClearCurrentCast();
  }

  if (player->Interaction().CurrentShapeshiftFormRequiresTurnSensitiveUse()) {

    const auto form_id = player->Animation().GetShapeshiftForm();
    const auto *dbc = session.GetDbcLoader();
    if (dbc != nullptr && form_id != 0u) {
      const auto *form_entry =
          dbc->spell_shapeshift_form().LookupEntry(form_id);
      if (form_entry != nullptr &&
          (form_entry->flags & kFormFlagSuppressNpcClose) == 0u) {

        const auto gossip_npc =
            session.gossip().has_gossip()
                ? session.gossip().gossip().npc_guid.GetRawValue()
                : 0ull;
        if (gossip_npc != 0u) {
          ui::game::HandleNpcInteractionLoss(
              session, ObjectGuid(gossip_npc),
              ui::game::NpcInteractionClosureCause::UnitUnavailable);
        }
      }
    }

    const auto* shapeshift_dbc = session.GetDbcLoader();
    if (shapeshift_dbc != nullptr && new_form_id != 0u) {
      const auto* form_entry =
          shapeshift_dbc->spell_shapeshift_form().LookupEntry(new_form_id);
      if (form_entry != nullptr) {
        const int zero_based_form = static_cast<int>(new_form_id) - 1;

        auto try_cast = [&](std::uint32_t spell_id) -> bool {
          if (spell_id == 0) return false;
          const auto* spell = shapeshift_dbc->spell().LookupEntry(spell_id);
          if (spell == nullptr) return false;
          if (!SpellShapeshiftMaskHasZeroBasedFormIndex(
                  MakeSpellShapeshiftMask(spell->stances,
                                          spell->stances_high),
                  zero_based_form)) {
            return false;
          }
          const auto target = session.objects().GetTargetGuid();
          if (!target.IsEmpty()) {
            session.interaction().SendCastSpell(spell_id, 0,
                                                 target.GetRawValue());
          } else {
            session.interaction().SendCastSpell(spell_id, 0, 0);
          }
          return true;
        };

        if (HasCompleteShapeshiftOverrideBar(form_entry)) {
          for (std::size_t i = 0; i < kShapeshiftOverrideActionCount; ++i) {
            const auto btn =
                GetShapeshiftGeneratedActionButton(*form_entry, i);
            if (btn.type != ActionPresentationKind::kSpell) {
              continue;
            }
            if (try_cast(btn.action)) {
              break;
            }
          }
        } else {

          const std::uint32_t start_slot =
              12u * (form_entry->bonus_action_bar + 5u);
          const std::uint32_t end_slot = start_slot + 12u;
          for (std::uint32_t s = start_slot; s < end_slot; ++s) {
            const auto& btn = session.action_assignments().GetPresentationEntry(s);
            if (btn.type != ActionPresentationKind::kSpell) {
              continue;
            }
            if (try_cast(btn.action)) {
              break;
            }
          }
        }
      }
    }
  } else if (IsFormWithMorphSound(new_form_id)) {

    auto &sound = player->sound_runtime();
    const auto pos = player->GetPosition();
    float pos3[3]{pos.x, pos.y, pos.z};

    const bool is_self = player->IsActivePlayer();
    if (is_self) {
      const bool listener_at_character =
          ui::game::CVarSystem::Instance().GetCVarBool(
              "Sound_ListenerAtCharacter");

      audio::SoundKitPlaybackOptions opts{};
      opts.playback_priority = audio::kSelfUnitSoundPlaybackPriority;
      opts.volume_scale = listener_at_character ? 0.65f : 1.0f;

      sound.PlaySoundKit(kMorphSoundKitId,
                         listener_at_character ? nullptr : pos3, nullptr, opts);
    } else {
      sound.PlaySoundKit(kMorphSoundKitId, pos3);
    }
  }

  RefreshPlayerShapeshiftUiState(session);
}

void RefreshPlayerShapeshiftUiState(WorldSession &session) {
  auto &dispatch = ui::game::ScriptEventDispatch::Get();

  session.spellbook_private_usability().Refresh(session);

  if (ui::game::detail::RefreshAllActionSlotValidation(session)) {
    dispatch.FireActionbarUpdateUsable();
  }
  dispatch.FirePetBarUpdateUsable();

  dispatch.FireInventoryCooldownsChanged();

  ui::game::detail::RefreshGeneratedActionBarState(session);

  dispatch.FireEvent(ui::game::events::UPDATE_SHAPESHIFT_FORM);
}

namespace {

constexpr std::uint32_t kLocalSpellEffectSummon = 28u;
}

void RefreshPossessSpellIdFromPlayerAuras(WorldSession& session,
                                          const CGPlayer_C& player) {
  std::uint32_t found_spell_id = 0;
  const auto *dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    session.pet().mutable_pet_bar().possess_spell_id = 0;
    return;
  }
  const auto &spell_store = dbc->spell();
  const auto &summon_props_store = dbc->summon_properties();
  for (std::size_t i = 0; i < player.Auras().Count(); ++i) {
    const auto *aura = player.Auras().At(i);
    if (aura == nullptr || aura->spell_id == 0) {
      continue;
    }

    constexpr std::uint32_t kAuraFlagNegative = 0x80u;
    if ((aura->flags & kAuraFlagNegative) != 0) {
      continue;
    }
    const auto *spell = spell_store.LookupEntry(aura->spell_id);
    if (spell == nullptr) {
      continue;
    }
    for (int effect_idx = 0; effect_idx < data::dbc::kMaxSpellEffects; ++effect_idx) {
      const auto aura_type = spell->effect_apply_aura[effect_idx];
      if (aura_type == 2 || aura_type == 6 || aura_type == 128 ||
          aura_type == 236) {
        found_spell_id = aura->spell_id;
        break;
      }
      if (spell->effect[effect_idx] == kLocalSpellEffectSummon) {
        const auto props_id = spell->effect_misc_value[effect_idx];
        if (props_id != 0) {
          const auto *props = summon_props_store.LookupEntry(
              static_cast<std::uint32_t>(props_id));
          if (props != nullptr && props->type == 3) {
            found_spell_id = aura->spell_id;
            break;
          }
        }
      }
    }
    if (found_spell_id != 0) {
      break;
    }
  }
  session.pet().mutable_pet_bar().possess_spell_id = found_spell_id;
}

void OnActiveControlGuidChanged(WorldSession &session) {
  auto *player = session.objects().GetActivePlayer();
  if (player == nullptr) {
    return;
  }

  auto &dispatch = ui::game::ScriptEventDispatch::Get();

  const auto controlled_guid = player->GetActiveControlGuid();
  const auto *controlled_unit =
      controlled_guid.IsEmpty()
          ? nullptr
          : session.objects().GetUnit(controlled_guid);

  if (controlled_unit != nullptr) {

    if (player->IsActivePlayer()) {
      auto pkt = net::wotlk::PacketSender::BuildFarSight(true);
      session.interaction().SendRawPacket(pkt);

      const auto target = session.objects().GetTargetGuid();
      if (!target.IsEmpty()) {
        player->EngageTarget(session, target);
      }
    }

    RefreshPossessSpellIdFromPlayerAuras(session, *player);
  } else {

    if (player->IsActivePlayer()) {
      auto pkt = net::wotlk::PacketSender::BuildFarSight(false);
      session.interaction().SendRawPacket(pkt);
    }

    if (player->Animation().IsAutoRepeatActive()) {
      player->Animation().SetAutoRepeatActive(false);
    }
    player->Casts().ClearCurrentCast();

    session.interaction().SendAttackStop();

    player->Interaction().CompleteAutoAttackInteraction(false, true);

    session.pet().mutable_pet_bar().possess_spell_id = 0;
  }

  dispatch.FirePetBarUpdate();

  if (player->State().GetHealth() == 0) {
    dispatch.FireEvent(ui::game::events::UNIT_EXITED_VEHICLE);
    if (session.held_cursor() != nullptr) {
      session.held_cursor()->Clear();
    }
  }
}

void OnComboTargetDescriptorChanged(
    WorldSession& session, const DescriptorFieldChangeView& change) {
  auto &objects = session.objects();
  const auto active_guid = CGObject_C::GetActivePlayerGuid();
  const auto player_guid = change.guid.GetRawValue();

  if (ObjectGuid(player_guid) != active_guid) {
    if (!GroupSystem::Get().IsActivePlayerPartyOrRaidMemberGuid(player_guid)) {
      return;
    }
  }

  const auto *player = objects.GetPlayer(ObjectGuid(player_guid));
  if (player == nullptr) {
    return;
  }

  const auto read_guid = [](const std::span<const std::uint32_t> words,
                            const std::size_t slot) {
    const auto word = slot * 2u;
    if (word + 1u >= words.size()) {
      return ObjectGuid{};
    }
    return ObjectGuid{static_cast<std::uint64_t>(words[word]) |
                      (static_cast<std::uint64_t>(words[word + 1u]) << 32u)};
  };

  for (std::size_t slot = 0; slot < 2; ++slot) {
    const auto new_guid = read_guid(change.new_words, slot);
    const auto old_guid = read_guid(change.old_words, slot);

    if (!new_guid.IsEmpty()) {
      const auto *unit = objects.GetUnit(new_guid);
      if (unit != nullptr) {

        Player_RegisterUnitFieldEventCallbacks(
            const_cast<void *>(static_cast<const void *>(unit)));
      } else if (new_guid != old_guid) {

        if (auto *targeting_system = session.targeting_system();
            targeting_system != nullptr) {
          targeting::TargetSelectionService target_selection(*targeting_system);
          (void)target_selection.ClearTarget(
              session, new_guid,
              targeting::TargetClearMode::kNotifyServer);
          if (targeting_system->focus_guid() == new_guid.GetRawValue()) {
            targeting_system->ClearFocus();
          }
        }
      }
    }

    if (!old_guid.IsEmpty()) {
      if (const auto* unit = objects.GetUnit(old_guid); unit != nullptr) {
        Player_RegisterUnitFieldEventCallbacks(
            const_cast<void*>(static_cast<const void*>(unit)));
      }
    }
  }

  if (ObjectGuid(player_guid) == active_guid) {
    RefreshPossessSpellIdFromPlayerAuras(session, *player);
  }
}

void RefreshAllVisibleGameObjectLootArt(ObjectManager& objects) {
  objects.ForEachObject([](const ObjectGuid & , CGObject_C &obj) {
    if (obj.IsGameObject()) {
      static_cast<CGGameObject_C &>(obj).RefreshLootArtVisualControlState();
    }
  });
}

void PushPlayerUnitToken(const char* event_name) {

  ui::game::ScriptEventDispatch::Get().FireEventArgs(
      event_name, {std::string("player")});
}

void OnCoinageDescriptorChanged(WorldSession& session,
                                std::uint32_t current_money) {

  session.sound_runtime().PlaySoundKitByName("LOOTWINDOWCOINSOUND");

  ui::game::ScriptEventDispatch::Get().FireEvent(
      ui::game::events::TRAINER_UPDATE);

  ui::game::ScriptEventDispatch::Get().FirePlayerMoney();

  const auto* player = session.objects().GetLocalPlayer();
  if (player != nullptr) {
    auto pkt = net::wotlk::PacketSender::BuildQuestgiverStatusMultipleQuery();
    session.interaction().SendRawPacket(pkt);
  }

  auto& trade = session.trade();
  if (trade.is_open() && player != nullptr) {
    const std::uint32_t proposed_gold = trade.own_gold();
    if (proposed_gold > 0 && proposed_gold > current_money) {

      trade.SetPendingOwnGold(current_money);
      session.interaction().SendSetTradeGold(current_money);
    }
  }
}

}


#include "openwow/game/chat_message_formatters.h"
#include "openwow/game/achievements/model/achievement_state_types.h"
#include "openwow/game/chat_display.h"
#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/faction_reaction.h"
#include "openwow/game/combat_log_internal.h"
#include "openwow/game/localization.h"
#include "openwow/game/objects/cggameobject.h"
#include "openwow/game/objects/cgitem.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace openwow::game {

namespace {

void DisplayFormattedChat(const ObjectManager& objects,
                          const std::string& text, int chat_type) {
  ChatFrame_DisplayMessage(
      objects, text.c_str(), chat_type,
      nullptr, 0, nullptr, nullptr, nullptr,
      0, 0, 0, 0, 0, nullptr);
}

std::string GetGlobalString(const char* key) {
  return Localization::Get().GetString(key != nullptr ? key : "");
}

std::string ResolveCombatLogActorName(const WorldSession& session,
                                      const std::uint64_t raw_guid) {
  if (raw_guid == 0) {
    return GetGlobalString("UNKNOWNOBJECT");
  }

  if (const auto* player_name = session.query_cache().GetPlayerName(raw_guid);
      player_name != nullptr && !player_name->name.empty()) {
    return player_name->name;
  }

  const auto object_name = session.objects().GetPlayerName(ObjectGuid(raw_guid));
  if (!object_name.empty()) {
    return object_name;
  }

  return GetGlobalString("UNKNOWNOBJECT");
}

std::string GetGlobalStringOrEmpty(const char* key) {
  return Localization::Get().GetString(key != nullptr ? key : "",
                                       std::string{});
}

inline constexpr std::uint8_t kUnitGenderFemale = 1;

inline constexpr int kPvpRankSideHorde = 0;
inline constexpr int kPvpRankSideAlliance = 1;

std::string ResolvePvpRankTitle(const WorldSession& session,
                                const CGUnit_C& victim, const int rank) {
  const auto* const dbc = session.GetDbcLoader();
  if (dbc == nullptr) return {};

  const auto* const faction_template =
      dbc->faction_template().LookupEntry(victim.State().GetFactionTemplate());
  if (faction_template == nullptr) return {};

  int side = 0;
  if ((faction_template->faction_group &
       data::dbc::kFactionMaskHorde) != 0u) {
    side = kPvpRankSideHorde;
  } else if ((faction_template->faction_group &
              data::dbc::kFactionMaskAlliance) != 0u) {
    side = kPvpRankSideAlliance;
  } else {
    return {};
  }

  char token[0x40];
  std::snprintf(token, sizeof(token), "PVP_RANK_%d_%d", rank, side);

  const auto* const active_player =
      session.objects().GetUnit(session.objects().GetActivePlayerGuid());
  if (active_player != nullptr &&
      active_player->State().GetGender() == kUnitGenderFemale) {
    std::string female = Localization::Get().GetString(
        std::string(token) + "_FEMALE", std::string{});
    if (!female.empty()) return female;
  }
  return Localization::Get().GetString(token, std::string{});
}

}

void FormatServerFirstAchievement(const ObjectManager& objects,
                                  const ServerFirstInfo& info) {
  char buf[kStockMessageFormatBufferBytes];
  if (info.link_type.value != 0) {
    const std::string fmt =
        GetGlobalString("PLAYER_SERVER_FIRST_ACHIEVEMENT");
    FormatRuntimeStringTemplateInto(buf, sizeof(buf), fmt.c_str(),
                  info.name.c_str(), info.name.c_str());
  } else {
    const std::string fmt = GetGlobalString("SERVER_FIRST_ACHIEVEMENT");
    FormatRuntimeStringTemplateInto(buf, sizeof(buf), fmt.c_str(), info.name.c_str());
  }

  ChatFrame_DisplayMessage(
      objects, buf,
      ChatDisplayType::kAchievement,
      info.name.c_str(),
      0,
      nullptr,
      nullptr,
      nullptr,
      info.player_guid.GetRawValue(),
      0,
      info.player_guid.GetRawValue(),
      static_cast<int>(info.achievement_id.value),
      0,
      nullptr);
}

void FormatTargetIconSet(const WorldSession& session,
                         std::uint64_t setter_guid, std::uint64_t target_guid,
                         int icon_index) {
  const auto* target_obj =
      session.objects().GetObjectByGUID(ObjectGuid(target_guid));
  if (target_obj == nullptr) return;

  const std::string setter_name = ResolveCombatLogActorName(session, setter_guid);
  if (setter_name.empty()) return;

  std::string target_name = session.objects().GetPlayerName(ObjectGuid(target_guid));
  if (target_name.empty()) {
    target_name = GetGlobalString("UNKNOWNOBJECT");
  }

  const std::string fmt = GetGlobalString("TARGET_ICON_SET");
  char buf[kStockMessageFormatBufferBytes];
  FormatRuntimeStringTemplateInto(buf, sizeof(buf), fmt.c_str(),
                setter_name.c_str(), setter_name.c_str(),
                icon_index + 1, target_name.c_str());
  DisplayFormattedChat(session.objects(), buf, 52);
}

void FormatXPGainFirstPerson(const ObjectManager& objects,
                             std::uint64_t source_guid, int xp_amount,
                             float group_rate) {
  int adjusted = static_cast<int>(static_cast<double>(xp_amount) / group_rate);
  char buf[kStockMessageFormatBufferBytes];

  if (source_guid != 0) {
    if (adjusted == xp_amount) {
      std::snprintf(buf, sizeof(buf), "You gain %d experience.", xp_amount);
    } else {
      const char* suffix = (adjusted >= xp_amount) ? "raid" : "group";
      int penalty = std::abs(xp_amount - adjusted);
      std::snprintf(buf, sizeof(buf), "You gain %d experience. (%s penalty: -%d)",
                    xp_amount, suffix, penalty);
    }
  } else {
    if (adjusted == xp_amount) {
      std::snprintf(buf, sizeof(buf), "You gain %d experience.", xp_amount);
    } else {
      const char* suffix = (adjusted >= xp_amount) ? "raid" : "group";
      int penalty = std::abs(xp_amount - adjusted);
      std::snprintf(buf, sizeof(buf), "You gain %d experience. (%s penalty: -%d)",
                    xp_amount, suffix, penalty);
    }
  }

  DisplayFormattedChat(objects, buf, ChatDisplayType::kCombatXP);
}

void FormatXPGainDetailed(const ObjectManager& objects,
                          const std::uint64_t source_guid, const int xp_total,
                          const int xp_base, const int xp_type,
                          const float group_rate, const bool rested_flag) {
  char buf[kStockMessageFormatBufferBytes];

  if (rested_flag && (xp_type == 1 || xp_type == 6)) {
    const int base = static_cast<int>(
        std::lround(static_cast<double>(xp_total) * 0.6666666));
    const int bonus = xp_total - base;
    char base_str[16];
    char bonus_str[16];
    std::snprintf(base_str, sizeof(base_str), "%d", base);
    std::snprintf(bonus_str, sizeof(bonus_str), "%s%d",
                  bonus >= 0 ? "+" : "", bonus);
    const std::string fmt = GetGlobalString("COMBATLOG_XPGAIN_QUEST");
    FormatRuntimeStringTemplateInto(buf, sizeof(buf), fmt.c_str(), xp_total,
                                    base_str, bonus_str);
    DisplayFormattedChat(objects, buf, ChatDisplayType::kCombatXP);
    return;
  }

  const int adjusted =
      group_rate != 0.0f
          ? static_cast<int>(static_cast<double>(xp_base) /
                             static_cast<double>(group_rate))
          : xp_base;
  const auto* victim = objects.GetUnit(ObjectGuid(source_guid));
  const std::string victim_name =
      victim != nullptr ? victim->GetName() : std::string{};
  const bool named = !victim_name.empty();

  const char* token = nullptr;
  if (adjusted == xp_total) {
    token = named ? "COMBATLOG_XPGAIN_FIRSTPERSON"
                  : "COMBATLOG_XPGAIN_FIRSTPERSON_UNNAMED";
  } else if (adjusted < xp_total) {
    token = named ? "COMBATLOG_XPGAIN_FIRSTPERSON_GROUP"
                  : "COMBATLOG_XPGAIN_FIRSTPERSON_UNNAMED_GROUP";
  } else {
    token = named ? "COMBATLOG_XPGAIN_FIRSTPERSON_RAID"
                  : "COMBATLOG_XPGAIN_FIRSTPERSON_UNNAMED_RAID";
  }
  const std::string fmt = GetGlobalString(token);
  const int delta = std::abs(xp_total - adjusted);
  if (named) {
    if (adjusted == xp_total) {
      FormatRuntimeStringTemplateInto(buf, sizeof(buf), fmt.c_str(),
                                      victim_name.c_str(), xp_total);
    } else {
      FormatRuntimeStringTemplateInto(buf, sizeof(buf), fmt.c_str(),
                                      victim_name.c_str(), xp_total, delta);
    }
  } else {
    if (adjusted == xp_total) {
      FormatRuntimeStringTemplateInto(buf, sizeof(buf), fmt.c_str(), xp_total);
    } else {
      FormatRuntimeStringTemplateInto(buf, sizeof(buf), fmt.c_str(), xp_total,
                                      delta);
    }
  }
  DisplayFormattedChat(objects, buf, ChatDisplayType::kCombatXP);
}

void FormatXPLoss(const ObjectManager& objects, int xp_amount) {
  char buf[kStockMessageFormatBufferBytes];
  const std::string fmt = GetGlobalString("COMBATLOG_XPLOSS_FIRSTPERSON_UNNAMED");
  FormatRuntimeStringTemplateInto(buf, sizeof(buf), fmt.c_str(), -xp_amount);
  DisplayFormattedChat(objects, buf, ChatDisplayType::kCombatXP);
}

void FormatHonorGain(WorldSession& session, std::uint64_t victim_guid,
                     int rank, int honor_points) {
  const ObjectManager& objects = session.objects();
  char buf[kStockMessageFormatBufferBytes];

  if (victim_guid == 0) {
    const std::string award_format = GetGlobalStringOrEmpty("COMBATLOG_HONORAWARD");
    if (award_format.empty()) return;
    FormatRuntimeStringTemplateInto(buf, sizeof(buf), award_format.c_str(),
                                    honor_points);
    DisplayFormattedChat(objects, buf, ChatDisplayType::kCombatHonor);
    CombatLog_FireCombatTextSD(CombatTextMsgIdx::kHonorGained, honor_points);
    return;
  }

  const auto* const victim = objects.GetUnit(ObjectGuid(victim_guid));
  if (victim == nullptr) return;

  if (honor_points < 1) return;

  const std::string format = GetGlobalStringOrEmpty(
      rank < 0 ? "COMBATLOG_HONORGAIN_NO_RANK" : "COMBATLOG_HONORGAIN");
  if (format.empty()) return;

  const std::string victim_name =
      ResolveCombatLogActorName(session, victim_guid);

  if (rank < 0) {
    FormatRuntimeStringTemplateInto(buf, sizeof(buf), format.c_str(),
                                    victim_name.c_str(), honor_points);
  } else {
    const std::string rank_title = ResolvePvpRankTitle(session, *victim, rank);
    FormatRuntimeStringTemplateInto(buf, sizeof(buf), format.c_str(),
                                    victim_name.c_str(), rank_title.c_str(),
                                    honor_points);

    CombatEvent honor_evt;
    honor_evt.type = CombatEventType::kHonorKill;
    honor_evt.source = objects.GetActivePlayerGuid();
    honor_evt.target = objects.GetActivePlayerGuid();
    honor_evt.amount = static_cast<std::uint32_t>(honor_points);
    honor_evt.honor_rank = static_cast<std::uint32_t>(rank);
    honor_evt.honor_rank_title = rank_title;
    session.combat_log().AddEvent(std::move(honor_evt));
  }

  DisplayFormattedChat(objects, buf, ChatDisplayType::kCombatHonor);
  CombatLog_FireCombatTextSD(CombatTextMsgIdx::kHonorGained, honor_points);
}

void FormatFeedPetLog(WorldSession& session, std::uint64_t caster_guid,
                      int item_entry) {
  const auto* item_template = session.query_cache().GetOrRequestItemTemplate(
      static_cast<std::uint32_t>(item_entry),
      QueryCache::QueryRequestOptions{
          .callback = [&session, caster_guid, item_entry](const bool success) {
            if (!success) {
              return;
            }
            FormatFeedPetLog(session, caster_guid, item_entry);
          }});
  if (item_template == nullptr) {
    return;
  }

  const auto active_player_guid = session.objects().GetActivePlayerGuid().GetRawValue();
  const bool is_first_person = caster_guid == active_player_guid;

  char buf[kStockMessageFormatBufferBytes];
  if (is_first_person) {
    const std::string fmt = GetGlobalString("FEEDPET_LOG_FIRSTPERSON");
    FormatRuntimeStringTemplateInto(buf, sizeof(buf), fmt.c_str(), item_template->name.c_str());
  } else {
    const auto* player_info = session.query_cache().GetPlayerName(caster_guid);
    if (player_info == nullptr) {
      return;
    }
    const std::string fmt = GetGlobalString("FEEDPET_LOG_THIRDPERSON");
    FormatRuntimeStringTemplateInto(buf, sizeof(buf), fmt.c_str(),
                  player_info->name.c_str(), item_template->name.c_str());
  }

  DisplayFormattedChat(session.objects(), buf, ChatDisplayType::kCombatPet);
}

void FormatSpellDismissPet(const WorldSession& session,
                           std::uint64_t caster_guid, std::uint64_t pet_guid) {
  const auto active_player_guid =
      session.objects().GetActivePlayerGuid().GetRawValue();
  const bool is_first_person = caster_guid == active_player_guid;

  const std::string pet_name =
      ResolveCombatLogActorName(session, pet_guid);

  char buf[kStockMessageFormatBufferBytes];
  if (is_first_person) {
    const std::string fmt = GetGlobalString("SPELLDISMISSPETSELF");
    FormatRuntimeStringTemplateInto(buf, sizeof(buf), fmt.c_str(), pet_name.c_str());
  } else {
    const std::string caster_name =
        ResolveCombatLogActorName(session, caster_guid);
    const std::string fmt = GetGlobalString("SPELLDISMISSPETOTHER");
    FormatRuntimeStringTemplateInto(buf, sizeof(buf), fmt.c_str(), caster_name.c_str(),
                  pet_name.c_str());
  }

  DisplayFormattedChat(session.objects(), buf, ChatDisplayType::kCombatPet);
}

void FormatTradeskillLog(WorldSession& session,
                         std::uint64_t caster_guid, const int item_entry) {
  if (item_entry == 0) {
    return;
  }

  const auto* item_template = session.query_cache().GetOrRequestItemTemplate(
      static_cast<std::uint32_t>(item_entry),
      QueryCache::QueryRequestOptions{
          .callback = [&session, caster_guid, item_entry](const bool success) {
            if (!success) {
              return;
            }
            FormatTradeskillLog(session, caster_guid, item_entry);
          }});
  if (item_template == nullptr) {
    return;
  }

  const auto active_player_guid = session.objects().GetActivePlayerGuid().GetRawValue();
  const bool is_first_person = caster_guid == active_player_guid;

  std::vector<std::string> format_args;
  format_args.reserve(is_first_person ? 1u : 2u);

  const std::string format = GetGlobalString(
      is_first_person ? "TRADESKILL_LOG_FIRSTPERSON" : "TRADESKILL_LOG_THIRDPERSON");
  if (!is_first_person) {
    format_args.push_back(ResolveCombatLogActorName(session, caster_guid));
  }
  format_args.push_back(item_template->name);

  DisplayFormattedChat(session.objects(),
                       Localization::Get().FormatString(format, format_args),
                       ChatDisplayType::kCombatTrade);
}

void FormatDurabilityDamageDeath(const ObjectManager& objects) {

  DisplayFormattedChat(objects, GetGlobalString("DURABILITYDAMAGE_DEATH"),
                       ChatDisplayType::kCombatMisc);
}

void FormatResetFailedNotify(const ObjectManager& objects) {
  DisplayFormattedChat(objects, GetGlobalString("RESET_FAILED_NOTIFY"),
                       ChatDisplayType::kSystem);
}

void FormatInstanceSaveCreated(const ObjectManager& objects,
                               const std::uint32_t flag) {
  const std::string instance_saved =
      Localization::Get().GetString("INSTANCE_SAVED", "INSTANCE_SAVED");

  if (flag == 0) {
    DisplayFormattedChat(objects, instance_saved, ChatDisplayType::kSystem);
    return;
  }

  if (flag != 1) {
    return;
  }

  std::array<char, 3000> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "(Debug-Only Lock Notice) %s",
                instance_saved.c_str());
  DisplayFormattedChat(objects, buffer.data(), ChatDisplayType::kSystem);
}

void FormatVoiceChatParentalError(const ObjectManager& objects, int error_code) {

  const char* key = nullptr;
  if (error_code == 588) {
    key = "ERR_VOICE_CHAT_PARENTAL_DISABLE_ALL";
  } else if (error_code == 589) {
    key = "ERR_VOICE_CHAT_PARENTAL_DISABLE_MIC";
  } else {
    return;
  }
  ui::game::DisplaySystemMessage(error_code);
  DisplayFormattedChat(objects, GetGlobalString(key), ChatDisplayType::kSystem);
}

void HandleZoneUnderAttack(const void* ) {

}

void HandleTitleEarned(const void* ) {

}

void HandleXPGainPacket(const WorldSession& session, const void* packet_data,
                        const std::size_t packet_size) {
  if (packet_data == nullptr) {
    diagnostics::Log(diagnostics::LogLevel::kDebug, "HandleXPGainPacket: null packet_data");
    return;
  }

  const auto* raw = static_cast<const std::uint8_t*>(packet_data);
  std::size_t off = 0;
  auto read_u8 = [&](std::uint8_t& v) -> bool {
    if (off + 1 > packet_size) return false;
    v = raw[off++];
    return true;
  };
  auto read_u32 = [&](std::uint32_t& v) -> bool {
    if (off + 4 > packet_size) return false;
    std::memcpy(&v, raw + off, 4);
    off += 4;
    return true;
  };
  auto read_u64 = [&](std::uint64_t& v) -> bool {
    if (off + 8 > packet_size) return false;
    std::memcpy(&v, raw + off, 8);
    off += 8;
    return true;
  };
  auto read_float = [&](float& v) -> bool {
    if (off + 4 > packet_size) return false;
    std::memcpy(&v, raw + off, 4);
    off += 4;
    return true;
  };

  std::uint64_t victim_guid = 0;
  std::uint32_t xp_total = 0;
  std::uint8_t xp_type = 0;
  float group_rate = 1.0f;
  std::uint32_t group_bonus = 0;
  std::uint8_t is_raf = 0;

  if (!read_u64(victim_guid)) return;
  if (!read_u32(xp_total)) return;
  if (!read_u8(xp_type)) return;

  if (xp_type == 0) {
    if (!read_u32(group_bonus)) return;
    if (!read_float(group_rate)) return;
  }
  (void)read_u8(is_raf);

  const auto active_guid = session.objects().GetActivePlayerGuid().GetRawValue();

  if (victim_guid == active_guid) {
    FormatXPLoss(session.objects(), static_cast<int>(xp_total));
  } else {
    FormatXPGainDetailed(
        session.objects(), victim_guid, static_cast<int>(xp_total),
        static_cast<int>(group_bonus),
        static_cast<int>(xp_type), group_rate,
        is_raf != 0);
  }
}

void FormatOpenLockMessage(const WorldSession& session,
                           std::uint64_t caster_guid,
                           const std::string& skill_name,
                           int required_skill) {
  const auto active_guid =
      session.objects().GetActivePlayerGuid().GetRawValue();

  char buf[kStockMessageFormatBufferBytes];
  if (caster_guid == active_guid) {
    const std::string fmt = GetGlobalString("OPEN_LOCK_SELF");
    FormatRuntimeStringTemplateInto(buf, sizeof(buf), fmt.c_str(),
                  skill_name.c_str(), required_skill);
  } else {
    const std::string caster_name =
        ResolveCombatLogActorName(session, caster_guid);
    const std::string fmt = GetGlobalString("OPEN_LOCK_OTHER");
    FormatRuntimeStringTemplateInto(buf, sizeof(buf), fmt.c_str(),
                  caster_name.c_str(), skill_name.c_str(), required_skill);
  }

  DisplayFormattedChat(session.objects(), buf, ChatDisplayType::kCombatSkill);
}

void HandleOpenLockEvent(WorldSession& session,
                         std::uint64_t caster_guid, std::uint64_t target_guid,
                         int spell_index) {
  const auto* const target =
      session.objects().GetObjectByGUID(ObjectGuid(target_guid));
  if (target == nullptr) {
    return;
  }

  const auto* const dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return;
  }

  auto resolve_skill_name = [dbc](std::uint32_t skill_line_id) -> std::string {
    if (skill_line_id != 0) {
      if (const auto* skill = dbc->skill_line().LookupEntry(skill_line_id);
          skill != nullptr && !skill->name.empty()) {
        return std::string(skill->name);
      }
    }
    return Localization::Get().GetString("UNKNOWN", "UNKNOWN");
  };

  if (target->IsItem()) {
    const auto entry = target->GetEntry();
    const auto* item_template = session.query_cache().GetOrRequestItemTemplate(
        entry, QueryCache::QueryRequestOptions{
                   .callback = [&session, caster_guid, target_guid, spell_index](
                                   const bool success) {
                     if (success) {
                       HandleOpenLockEvent(session, caster_guid, target_guid,
                                           spell_index);
                     }
                   }});
    if (item_template == nullptr || item_template->required_skill == 0 ||
        item_template->required_skill_rank == 0) {
      return;
    }

    FormatOpenLockMessage(
        session, caster_guid, resolve_skill_name(item_template->required_skill),
        static_cast<int>(item_template->required_skill_rank));
    return;
  }

  if (!target->IsGameObject()) {
    return;
  }

  const auto* game_object = static_cast<const CGGameObject_C*>(target);
  if (game_object->GetTemplateInfo() == nullptr) {
    const auto entry = game_object->GetEntry();
    const auto* template_info = session.query_cache().GetOrRequestGameObjectTemplate(
        entry, QueryCache::QueryRequestOptions{
                   .context = target_guid,
                   .callback = [&session, caster_guid, target_guid, spell_index](
                                   const bool success) {
                     if (success) {
                       HandleOpenLockEvent(session, caster_guid, target_guid,
                                           spell_index);
                     }
                   }});
    if (template_info == nullptr) {
      return;
    }
  }

  const auto* lock = game_object->GetLockEntry();
  if (lock == nullptr) {
    return;
  }

  std::string skill_name;
  std::uint32_t required_skill = 0;
  for (std::size_t slot = 0; slot < lock->type.size(); ++slot) {
    if (lock->type[slot] == 2u) {
      required_skill = lock->skill[slot] != 0u
                           ? lock->skill[slot]
                           : 5u * game_object->GetLevel();
      if (const auto* lock_type = dbc->lock_type().LookupEntry(lock->index[slot]);
          lock_type != nullptr && !lock_type->name.empty()) {
        skill_name.assign(lock_type->name);
      }
      break;
    }
  }

  if (required_skill == 0) {
    return;
  }

  if (skill_name.empty()) {
    skill_name = Localization::Get().GetString("LOCKED", "Locked");
  }

  FormatOpenLockMessage(session, caster_guid, skill_name,
                        static_cast<int>(required_skill));
}

std::optional<std::string>
BuildRandomRollResultText(const QueryCache& cache,
                          const std::uint64_t roller_guid,
                          const std::uint32_t min_val,
                          const std::uint32_t max_val,
                          const std::uint32_t result) {
  const auto* name_info = cache.GetPlayerName(roller_guid);
  if (!name_info || name_info->name.empty()) {
    return std::nullopt;
  }

  const std::string format =
      Localization::Get().GetString("RANDOM_ROLL_RESULT");
  if (format.empty()) {
    return std::nullopt;
  }

  char buf[kStockMessageFormatBufferBytes];
  FormatRuntimeStringTemplateInto(buf, sizeof(buf), format.c_str(),
                name_info->name.c_str(),
                static_cast<int>(result),
                static_cast<int>(min_val),
                static_cast<int>(max_val));
  return std::string(buf);
}

void FormatRandomRollResult(const ObjectManager& objects,
                            const QueryCache& cache,
                            const std::uint64_t roller_guid,
                            const std::uint32_t min_val,
                            const std::uint32_t max_val,
                            const std::uint32_t result) {
  const auto message = BuildRandomRollResultText(
      cache, roller_guid, min_val, max_val, result);
  if (!message.has_value()) {
    return;
  }

  ChatFrame_DisplayMessage(objects, message->c_str(), ChatDisplayType::kSystem,
                           nullptr, 0, nullptr, nullptr, nullptr,
                           0, 0, 0, 0, 0, nullptr);
}

}

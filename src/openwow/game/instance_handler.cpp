
#include "openwow/game/instance_handler.h"

#include "openwow/core/console.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/debug/diagnostics/debug_console.h"
#include "openwow/game/localization.h"
#include "openwow/net/client_services_packet_sender.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/network/protocol/wotlk/world_packet.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string_view>

namespace openwow::game {

namespace {

constexpr std::string_view kChangePlayerDifficultyCommand = "changePlayerDifficulty";
constexpr std::string_view kInvalidDifficultyMessage = "Invalid difficulty";

std::uint32_t ParseStormSignedDecimalPrefix(std::string_view text) {
  if (text.empty()) {
    return 0;
  }

  std::size_t index = 0;
  const bool negative = text.front() == '-';
  if (negative) {
    index = 1;
    if (index == text.size()) {
      return 0;
    }
  }

  const auto first_digit = static_cast<std::uint32_t>(static_cast<unsigned char>(text[index]) -
                                                      static_cast<unsigned char>('0'));
  if (first_digit >= 10u) {
    return 0;
  }

  std::uint32_t value = first_digit;
  ++index;
  while (index < text.size()) {
    const auto digit = static_cast<std::uint32_t>(static_cast<unsigned char>(text[index]) -
                                                  static_cast<unsigned char>('0'));
    if (digit >= 10u) {
      break;
    }

    value = value * 10u + digit;
    ++index;
  }

  if (negative) {
    return 0u - value;
  }

  return value;
}

std::int32_t GetRemainingWholeSeconds(std::uint32_t deadline_ms, std::uint32_t current_tick_ms) {
  if (deadline_ms == 0) {
    return 0;
  }

  if (static_cast<std::int32_t>(current_tick_ms - deadline_ms) >= 0) {
    return 0;
  }

  return static_cast<std::int32_t>((deadline_ms - current_tick_ms) / 1000);
}

std::string FormatLocalizedCooldownBucket(const char *key, const std::uint32_t value) {
  const auto format = Localization::Get().GetString(key, "%u");
  std::array<char, 128> buffer{};
  FormatRuntimeStringTemplateInto(buffer.data(), buffer.size(), format.c_str(), value);
  return buffer.data();
}

std::uint32_t DivideRoundedUp(const std::uint32_t value, const std::uint32_t divisor) {
  if (value == 0) {
    return 0;
  }

  return ((value - 1u) / divisor) + 1u;
}

void FireInstanceLockEvent(const char *event_name) {
  ui::game::ScriptEventDispatch::Get().FireEvent(event_name);
}

auto FindEncounterObjective(std::vector<EncounterObjective> &objectives, const std::uint8_t id) {
  return std::find_if(objectives.begin(), objectives.end(),
                      [id](const EncounterObjective &objective) { return objective.id == id; });
}

const data::dbc::MapDifficultyEntry *LookupMapDifficultyEntry(const data::dbc::DbcLoader *dbc,
                                                              const std::uint32_t map_id,
                                                              const std::uint32_t difficulty) {
  return data::DBClient_FindMapDifficulty(dbc, map_id, difficulty);
}

std::string ResolveMapDifficultyName(const data::dbc::MapDifficultyEntry *map_difficulty) {
  if (map_difficulty == nullptr || map_difficulty->difficulty_string.empty()) {
    return {};
  }

  const auto key = std::string(map_difficulty->difficulty_string);
  return Localization::Get().GetString(key, key);
}

}

void RegisterInstanceConsoleCommands() {
  auto &console = openwow::debug::DebugConsole::Get();
  if (console.HasCommand(std::string(kChangePlayerDifficultyCommand))) {
    return;
  }

  console.RegisterRawCommand(std::string(kChangePlayerDifficultyCommand), {},
                             [](std::string_view raw_args) -> std::string {
                               const auto difficulty = ParseStormSignedDecimalPrefix(raw_args);
                               if (difficulty > 1u) {
                                 openwow::core::ida::ConsoleAddLine(
                                     std::string(kInvalidDifficultyMessage),
                                     openwow::core::ida::COLOR_ERROR);
                                 return {};
                               }

                               openwow::net::wotlk::WorldPacket pkt(
                                   openwow::net::wotlk::Opcode::CMSG_CHANGEPLAYER_DIFFICULTY);
                               pkt.AppendU32(difficulty);
                               (void)openwow::net::ClientServices__SendPacket(pkt);
                               return {};
                             });
}

void UnregisterInstanceConsoleCommands() {
  openwow::debug::DebugConsole::Get().UnregisterCommand(
      std::string(kChangePlayerDifficultyCommand));
}

std::string FormatRoundedDurationText(const std::uint32_t remaining_ms,
                                      const std::string_view key_prefix) {
  constexpr std::uint32_t kMinuteMs = 60u * 1000u;
  constexpr std::uint32_t kHourMs = 60u * kMinuteMs;
  constexpr std::uint32_t kDayMs = 24u * kHourMs;
  const auto key = [key_prefix](const std::string_view suffix) {
    std::string value(key_prefix);
    value.push_back('_');
    value.append(suffix);
    return value;
  };

  if (remaining_ms >= kDayMs) {
    return FormatLocalizedCooldownBucket(key("DAYS").c_str(),
                                         DivideRoundedUp(remaining_ms, kDayMs));
  }

  if (remaining_ms >= kHourMs) {
    const auto hours = DivideRoundedUp(remaining_ms, kHourMs);
    if (hours == 24u) {
      return FormatLocalizedCooldownBucket(key("DAYS").c_str(), 1u);
    }

    return FormatLocalizedCooldownBucket(key("HOURS").c_str(), hours);
  }

  if (remaining_ms >= kMinuteMs) {
    const auto minutes = DivideRoundedUp(remaining_ms, kMinuteMs);
    if (minutes == 60u) {
      return FormatLocalizedCooldownBucket(key("HOURS").c_str(), 1u);
    }

    return FormatLocalizedCooldownBucket(key("MIN").c_str(), minutes);
  }

  return FormatLocalizedCooldownBucket(key("SEC").c_str(), remaining_ms / 1000u);
}

std::string FormatRoundedSpellDurationText(const std::uint32_t remaining_ms) {
  return FormatRoundedDurationText(remaining_ms, "INT_SPELL_DURATION");
}

std::string FormatRoundedGeneralDurationText(const std::uint32_t remaining_ms) {
  return FormatRoundedDurationText(remaining_ms, "INT_GENERAL_DURATION");
}

std::string FormatDifficultyChangeCooldownText(const std::uint32_t remaining_ms) {
  return FormatRoundedSpellDurationText(remaining_ms);
}

std::string FormatDungeonNameWithDifficulty(const data::dbc::DbcLoader *dbc,
                                            const std::uint32_t map_id,
                                            const std::uint32_t difficulty) {
  if (dbc == nullptr) {
    return {};
  }

  const auto *map_entry = dbc->map().LookupEntry(map_id);
  if (map_entry == nullptr) {
    return {};
  }

  const std::string map_name(map_entry->name);
  const auto *map_difficulty = LookupMapDifficultyEntry(dbc, map_id, difficulty);
  const auto difficulty_name = ResolveMapDifficultyName(map_difficulty);
  if (difficulty_name.empty()) {
    return map_name;
  }

  const std::string format =
      Localization::Get().GetString("DUNGEON_NAME_WITH_DIFFICULTY", "DUNGEON_NAME_WITH_DIFFICULTY");
  return Localization::Get().FormatString(format, {map_name, difficulty_name});
}

bool InstanceHandler::ParseDifficulty(const std::uint8_t *data, std::size_t len,
                                      DifficultyUpdate &out) {
  PacketReader r(data, len);
  if (!r.ReadU32(out.difficulty))
    return false;
  if (!r.ReadU32(out.update_default))
    return false;
  if (!r.ReadU32(out.update_group))
    return false;
  return true;
}

bool InstanceHandler::HandleSetDungeonDifficulty(const std::uint8_t *data, std::size_t len) {
  return ParseDifficulty(data, len, dungeon_diff_);
}

bool InstanceHandler::HandleSetRaidDifficulty(const std::uint8_t *data, std::size_t len) {
  return ParseDifficulty(data, len, raid_diff_);
}

bool InstanceHandler::HandleRaidInstanceInfo(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  std::uint32_t count = 0;
  if (!r.ReadU32(count))
    return false;
  lockouts_.clear();
  lockouts_.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    RaidLockout l;
    if (!r.ReadU32(l.map_id))
      return false;
    if (!r.ReadU32(l.difficulty))
      return false;
    if (!r.ReadU64(l.lockout_id))
      return false;
    if (!r.ReadU8(l.locked))
      return false;
    if (!r.ReadU8(l.extended))
      return false;
    if (!r.ReadU32(l.reset_time))
      return false;
    lockouts_.push_back(l);
  }
  return true;
}

std::optional<RaidLockout> InstanceHandler::SetRaidLockoutExtended(const std::size_t index,
                                                                   const bool extended) {
  if (index >= lockouts_.size()) {
    return std::nullopt;
  }

  auto &lockout = lockouts_[index];
  lockout.extended = extended ? 1u : 0u;
  return lockout;
}

bool InstanceHandler::HandleInstanceReset(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(last_reset_map_))
    return false;
  ClearResetInstanceVisibilityAnchor();
  return true;
}

bool InstanceHandler::HandleInstanceResetFailed(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(last_reset_failed_.reason))
    return false;
  if (!r.ReadU32(last_reset_failed_.map_id))
    return false;
  return true;
}

bool InstanceHandler::HandleEncounterUpdate(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  last_encounter_ = {};
  last_encounter_fires_unit_frame_event_ = false;
  if (!r.ReadU32(last_encounter_.type))
    return false;

  auto ft = static_cast<EncounterFrameType>(last_encounter_.type);
  switch (ft) {
  case EncounterFrameType::kEngage:
  case EncounterFrameType::kDisengage:
  case EncounterFrameType::kUpdatePriority:
    if (!r.ReadPackedGuid(last_encounter_.unit))
      return false;
    if (!r.ReadU8(last_encounter_.param1))
      return false;
    if (ft == EncounterFrameType::kEngage) {
      last_encounter_fires_unit_frame_event_ =
          TryAddEncounterUnitFrame(last_encounter_.unit, last_encounter_.param1);
    } else if (ft == EncounterFrameType::kDisengage) {
      last_encounter_fires_unit_frame_event_ = RemoveEncounterUnitFrame(last_encounter_.unit);
    } else {
      last_encounter_fires_unit_frame_event_ =
          TryUpdateEncounterUnitPriority(last_encounter_.unit, last_encounter_.param1);
    }
    break;
  case EncounterFrameType::kUpdateObjective:
    if (!r.ReadU8(last_encounter_.param1))
      return false;
    if (!r.ReadU8(last_encounter_.param2))
      return false;
    if (auto it = FindEncounterObjective(encounter_objectives_, last_encounter_.param1);
        it != encounter_objectives_.end()) {
      it->progress += last_encounter_.param2;
    }
    break;
  case EncounterFrameType::kAddTimer: {
    if (!r.ReadU8(last_encounter_.param1))
      return false;
    const auto current_tick_ms = core::GameClock::GetTickCount32();
    PurgeExpiredEncounterTimers(current_tick_ms);
    encounter_timers_.push_back(
        EncounterTimer{last_encounter_.param1, current_tick_ms + last_encounter_.param1});
    break;
  }
  case EncounterFrameType::kAddObjective:
    if (!r.ReadU8(last_encounter_.param1))
      return false;
    if (FindEncounterObjective(encounter_objectives_, last_encounter_.param1) ==
        encounter_objectives_.end()) {
      encounter_objectives_.push_back(EncounterObjective{last_encounter_.param1, 0});
    }
    break;
  case EncounterFrameType::kRemoveObjective:
    if (!r.ReadU8(last_encounter_.param1))
      return false;
    encounter_objectives_.erase(std::remove_if(encounter_objectives_.begin(),
                                               encounter_objectives_.end(),
                                               [&](const EncounterObjective &objective) {
                                                 return objective.id == last_encounter_.param1;
                                               }),
                                encounter_objectives_.end());
    break;
  case EncounterFrameType::kRefreshFrames:
    SortEncounterUnitFrames();
    last_encounter_fires_unit_frame_event_ = true;
    [[fallthrough]];
  default:

    break;
  }
  return true;
}

bool InstanceHandler::HandleRaidGroupOnly(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(last_instance_boot_warning_.remaining_ms))
    return false;
  if (!r.ReadU32(last_instance_boot_warning_.reason))
    return false;
  SetInstanceBootCountdownMs(last_instance_boot_warning_.remaining_ms,
                             core::GameClock::GetTickCount32());
  return true;
}

bool InstanceHandler::HandleChangePlayerDifficultyResult(const std::uint8_t *data,
                                                         const std::size_t len,
                                                         const std::uint32_t current_unix_time) {
  PacketReader r(data, len);
  change_player_difficulty_result_ = {};
  if (!r.ReadU32(change_player_difficulty_result_.code))
    return false;

  switch (static_cast<PlayerDifficultyChangeResultCode>(change_player_difficulty_result_.code)) {
  case PlayerDifficultyChangeResultCode::kCurrentDifficulty: {
    std::uint8_t difficulty = 0;
    if (!r.ReadU8(difficulty))
      return false;
    player_difficulty_ = difficulty;
    change_player_difficulty_result_.value = difficulty;
    return true;
  }
  case PlayerDifficultyChangeResultCode::kCooldownMessage:
  case PlayerDifficultyChangeResultCode::kCooldownStarted:
  case PlayerDifficultyChangeResultCode::kChatMessage:
    if (!r.ReadU32(change_player_difficulty_result_.value))
      return false;
    if (static_cast<PlayerDifficultyChangeResultCode>(change_player_difficulty_result_.code) ==
        PlayerDifficultyChangeResultCode::kCooldownStarted) {
      player_difficulty_change_cooldown_end_unix_ =
          current_unix_time + change_player_difficulty_result_.value;
    }
    return true;
  default:
    return true;
  }
}

bool InstanceHandler::HandleInstanceLockWarning(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  InstanceLockWarning w;
  if (!r.ReadU32(w.time_remaining))
    return false;
  if (!r.ReadU32(w.encounter_mask))
    return false;
  if (!r.ReadU8(w.extend_lock))
    return false;
  last_lock_warning_ = w;
  SetActiveInstanceLock(w.time_remaining, w.encounter_mask, w.extend_lock != 0,
                        core::GameClock::GetTickCount32());
  return true;
}

bool InstanceHandler::HandleInstanceSaveCreated(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(last_instance_save_created_))
    return false;
  if (last_instance_save_created_ == 0) {
    ClearActiveInstanceLock();
  }
  return true;
}

bool InstanceHandler::HandleUpdateLastInstance(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(last_instance_map_id_))
    return false;
  return true;
}

bool InstanceHandler::HandleUpdateInstanceOwnership(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  if (!r.ReadU32(instance_save_count_))
    return false;
  return true;
}

void InstanceHandler::SetResetInstanceVisibilityAnchor(const std::uint32_t map_id,
                                                       const std::uint32_t current_unix_time) {
  reset_instance_visibility_state_.anchor_map_id = map_id;
  reset_instance_visibility_state_.anchor_unix_time = current_unix_time;
}

bool InstanceHandler::HandleRaidInstanceMessage(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  RaidInstanceMessage msg{};
  if (!r.ReadU32(msg.type))
    return false;
  if (!r.ReadU32(msg.map_id))
    return false;
  if (!r.ReadU32(msg.difficulty))
    return false;
  if (!r.ReadU32(msg.time_remaining))
    return false;
  if (msg.type == 4) {
    (void)r.ReadU8(msg.welcome_flag1);
    (void)r.ReadU8(msg.welcome_flag2);
  }
  last_raid_instance_msg_ = msg;
  return true;
}

bool InstanceHandler::HandleViewPhaseShift(const std::uint8_t *data, std::size_t len) {
  if (len >= 4) {
    PacketReader r(data, len);
    if (!r.ReadU32(phase_mask_))
      return false;
  } else {
    phase_mask_ = 0;
  }
  return true;
}

std::int32_t
InstanceHandler::GetInstanceBootTimeRemainingSeconds(const std::uint32_t current_tick_ms) const {
  return GetRemainingWholeSeconds(instance_boot_deadline_ms_, current_tick_ms);
}

ActiveInstanceLock
InstanceHandler::GetActiveInstanceLock(const std::uint32_t current_tick_ms) const {
  ActiveInstanceLock lock{};
  if (instance_lock_deadline_ms_ == 0 ||
      static_cast<std::int32_t>(current_tick_ms - instance_lock_deadline_ms_) >= 0) {
    return lock;
  }

  lock.remaining_ms = instance_lock_deadline_ms_ - current_tick_ms;
  lock.remaining_seconds = static_cast<std::int32_t>(lock.remaining_ms / 1000);
  lock.encounter_mask = instance_lock_encounter_mask_;
  lock.extend_lock = instance_lock_extend_;
  return lock;
}

bool InstanceHandler::IsPlayerDifficultyChangeCooldownActive(
    const std::uint32_t current_unix_time) const {
  return player_difficulty_change_cooldown_end_unix_ != 0 &&
         current_unix_time < player_difficulty_change_cooldown_end_unix_;
}

std::uint32_t InstanceHandler::GetPlayerDifficultyChangeCooldownRemainingMs(
    const std::uint32_t current_unix_time) const {
  if (!IsPlayerDifficultyChangeCooldownActive(current_unix_time)) {
    return 0;
  }

  return (player_difficulty_change_cooldown_end_unix_ - current_unix_time) * 1000u;
}

void InstanceHandler::Clear() {
  dungeon_diff_ = {};
  raid_diff_ = {};
  lockouts_.clear();
  last_instance_boot_warning_ = {};
  last_reset_map_ = 0;
  instance_boot_deadline_ms_ = 0;
  last_reset_failed_ = {};
  last_encounter_ = {};
  last_encounter_fires_unit_frame_event_ = false;
  encounter_unit_frames_.clear();
  encounter_timers_.clear();
  encounter_objectives_.clear();

  last_lock_warning_.reset();
  ResetActiveInstanceLockState();
  last_instance_save_created_ = 0;
  last_instance_map_id_ = 0;
  instance_save_count_ = 0;
  ClearResetInstanceVisibilityAnchor();

  last_raid_instance_msg_.reset();
  phase_mask_ = 0;
  change_player_difficulty_result_ = {};
  player_difficulty_ = 0;
  player_difficulty_change_cooldown_end_unix_ = 0;
}

void InstanceHandler::ResetActiveInstanceLockState() {
  instance_lock_deadline_ms_ = 0;
  instance_lock_encounter_mask_ = 0;
  instance_lock_extend_ = false;
}

void InstanceHandler::ClearResetInstanceVisibilityAnchor() {
  reset_instance_visibility_state_ = {};
}

void InstanceHandler::ClearActiveInstanceLock() {
  ResetActiveInstanceLockState();
  FireInstanceLockEvent(ui::game::events::INSTANCE_LOCK_STOP);
}

void InstanceHandler::SetActiveInstanceLock(const std::uint32_t remaining_ms,
                                            const std::uint32_t encounter_mask,
                                            const bool extend_lock,
                                            const std::uint32_t current_tick_ms) {
  if (remaining_ms == 0) {
    ClearActiveInstanceLock();
    return;
  }

  instance_lock_deadline_ms_ = current_tick_ms + remaining_ms;
  instance_lock_encounter_mask_ = encounter_mask;
  instance_lock_extend_ = extend_lock;
  FireInstanceLockEvent(ui::game::events::INSTANCE_LOCK_START);
}

void InstanceHandler::SetInstanceBootCountdownMs(const std::uint32_t remaining_ms,
                                                 const std::uint32_t current_tick_ms) {
  if (remaining_ms == 0) {
    instance_boot_deadline_ms_ = 0;
    return;
  }

  instance_boot_deadline_ms_ = current_tick_ms + remaining_ms;
}

void InstanceHandler::PurgeExpiredEncounterTimers(const std::uint32_t current_tick_ms) {
  encounter_timers_.erase(std::remove_if(encounter_timers_.begin(), encounter_timers_.end(),
                                         [&](const EncounterTimer &timer) {
                                           return static_cast<std::int32_t>(current_tick_ms -
                                                                            timer.deadline_ms) >= 0;
                                         }),
                          encounter_timers_.end());
}

void InstanceHandler::SortEncounterUnitFrames() {
  std::stable_sort(encounter_unit_frames_.begin(), encounter_unit_frames_.end(),
                   [](const EncounterUnitFrame &lhs, const EncounterUnitFrame &rhs) {
                     return lhs.priority < rhs.priority;
                   });
}

bool InstanceHandler::TryAddEncounterUnitFrame(ObjectGuid guid, std::uint8_t priority) {
  auto it = std::find_if(encounter_unit_frames_.begin(), encounter_unit_frames_.end(),
                         [&](const EncounterUnitFrame &frame) {
                           return frame.guid.GetRawValue() == guid.GetRawValue();
                         });
  if (it != encounter_unit_frames_.end()) {
    return false;
  }
  if (encounter_unit_frames_.size() >= kMaxEncounterUnitFrames) {
    SortEncounterUnitFrames();
    return true;
  }
  encounter_unit_frames_.push_back(EncounterUnitFrame{guid, priority});
  SortEncounterUnitFrames();
  return true;
}

bool InstanceHandler::TryUpdateEncounterUnitPriority(ObjectGuid guid, std::uint8_t priority) {
  auto it = std::find_if(encounter_unit_frames_.begin(), encounter_unit_frames_.end(),
                         [&](const EncounterUnitFrame &frame) {
                           return frame.guid.GetRawValue() == guid.GetRawValue();
                         });
  if (it == encounter_unit_frames_.end()) {
    return false;
  }
  it->priority = priority;
  SortEncounterUnitFrames();
  return true;
}

bool InstanceHandler::RemoveEncounterUnitFrame(ObjectGuid guid) {
  const auto old_size = encounter_unit_frames_.size();
  encounter_unit_frames_.erase(
      std::remove_if(encounter_unit_frames_.begin(), encounter_unit_frames_.end(),
                     [&](const EncounterUnitFrame &frame) {
                       return frame.guid.GetRawValue() == guid.GetRawValue();
                     }),
      encounter_unit_frames_.end());
  if (encounter_unit_frames_.size() == old_size) {
    return false;
  }
  SortEncounterUnitFrames();
  return true;
}

}

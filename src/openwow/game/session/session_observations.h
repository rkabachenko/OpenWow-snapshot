#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::game {

struct FeatureSystemStatus {
  std::uint8_t complaint_status = 0;
  std::uint8_t voice_chat_enabled = 0;
};

struct PhaseShiftInfo { std::uint32_t phase_mask = 0; };

struct InstanceDifficultyInfo {
  std::uint32_t difficulty_index = 0;
  std::uint32_t player_difficulty_index = 0;
};

enum class TransferAbortReason : std::uint8_t {
  kNone = 0x00, kError = 0x01, kMaxPlayers = 0x02, kNotFound = 0x03,
  kTooManyInstances = 0x04, kZoneInCombat = 0x06, kInsufExpanLvl = 0x07,
  kDifficulty = 0x08, kUniqueMessage = 0x09, kTooManyRealmInstances = 0x0A,
  kNeedGroup = 0x0B, kNotFound12 = 0x0C, kNotFound13 = 0x0D,
  kNotFound14 = 0x0E, kRealmOnly = 0x0F, kMapNotAllowed = 0x10,
};

struct TransferAbortedInfo {
  std::uint32_t map_id = 0;
  TransferAbortReason reason = TransferAbortReason::kNone;
  std::uint8_t arg = 0;
};

struct QueryTimeResponse {
  std::uint32_t server_time = 0;
  std::uint32_t daily_reset_secs = 0;
  std::int64_t local_time_offset_secs = 0;
  std::int64_t local_daily_reset_deadline_secs = 0;
  std::int64_t local_refresh_deadline_secs = 0;
};

struct GossipPointOfInterest {
  std::uint32_t flags = 0;
  float x = 0.0f, y = 0.0f;
  std::uint32_t icon = 0, importance = 0;
  std::string name;
};

struct CorpseQueryResult {
  bool found = false;
  std::int32_t map_id = -1;
  float x = 0.0f, y = 0.0f, z = 0.0f;
  std::int32_t corpse_map_id = -1;
  std::uint32_t transport_counter = 0;
};

struct RandomRollResult {
  std::uint32_t min_val = 0, max_val = 0, result = 0;
  std::uint64_t roller_guid = 0;
};

struct QuestCompleteInfo {
  std::uint32_t quest_id = 0, xp_reward = 0, money_reward = 0;
  std::uint32_t honor_reward = 0, talent_reward = 0, arena_points = 0;
};

struct QuestListEntry {
  std::uint32_t quest_id = 0, quest_icon = 0;
  std::int32_t quest_level = 0;
  std::uint32_t quest_flags = 0;
  bool is_repeatable = false;
  std::string title;
};

struct QuestListInfo {
  std::uint64_t npc_guid = 0;
  std::string greeting;
  std::uint32_t emote_delay = 0, emote_id = 0;
  std::vector<QuestListEntry> quests;
};

struct ResurrectRequest {
  std::uint64_t caster_guid = 0;
  std::string caster_name;
  bool has_sickness = false, has_timer = false;
};

struct ClientControlUpdate { ObjectGuid guid{ObjectGuid(0)}; bool enabled = false; };
struct CancelAutoRepeatInfo { ObjectGuid target_guid{ObjectGuid(0)}; };
struct RealmSplitInfo { std::uint32_t state = 0; std::string split_date; };

}

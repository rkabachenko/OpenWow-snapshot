#pragma once

#include <cstdint>

namespace openwow::game {

struct GroupLootStartRoll {
  std::uint64_t source_guid = 0;
  std::uint32_t map_id = 0;
  std::uint32_t item_slot = 0;
  std::uint32_t item_id = 0;
  std::uint32_t random_suffix = 0;
  std::uint32_t random_prop_id = 0;
  std::uint32_t item_count = 0;
  std::uint32_t countdown_ms = 0;
  std::uint8_t roll_vote_mask = 0;
};

enum class GroupRollType : std::uint8_t {
  kPass = 0,
  kNeed = 1,
  kGreed = 2,
  kDisenchant = 3,
};

struct GroupLootRollResult {
  std::uint64_t source_guid = 0;
  std::uint32_t item_slot = 0;
  std::uint64_t roller_guid = 0;
  std::uint32_t item_id = 0;
  std::uint32_t random_suffix = 0;
  std::uint32_t random_prop_id = 0;
  std::uint8_t roll_number = 0;
  GroupRollType roll_type = GroupRollType::kPass;
  bool auto_pass = false;
};

struct GroupLootAllPassed {
  std::uint64_t source_guid = 0;
  std::uint32_t item_slot = 0;
  std::uint32_t item_id = 0;
  std::uint32_t random_suffix = 0;
  std::uint32_t random_prop_id = 0;
};

}

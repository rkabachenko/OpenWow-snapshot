#pragma once

#include "openwow/game/object_guid.h"
#include "openwow/game/update_fields.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "openwow/game/world_session_fwd.h"

namespace openwow::game {

class CGObject_C;
class ObjectManager;

struct UnitQuerySnapshot {
  ObjectGuid guid;
  std::string name;
  std::string realm;
  std::uint32_t level{0};
  std::uint32_t health{0};
  std::uint32_t maxHealth{0};
  std::uint32_t power{0};
  std::uint32_t maxPower{0};
  std::uint32_t powerType{0};
  std::uint8_t classId{0};
  std::uint8_t raceId{0};
  std::uint8_t genderId{0};
  std::uint32_t factionTemplate{0};
  bool isPlayer{false};
  bool isDead{false};
  bool isGhost{false};
  bool inCombat{false};
  bool isConnected{true};
  float x{0}, y{0}, z{0};
};

enum class UnitIdKind : std::uint8_t {
  kUnknown = 0,
  kPlayer,
  kTarget,
  kFocus,
  kPet,
  kMouseover,
  kParty,
  kPartyPet,
  kPartyTarget,
  kRaid,
  kRaidPet,
  kRaidTarget,
  kArena,
  kArenaPet,
  kBoss,
  kCommentator,
  kTargetTarget,
  kFocusTarget,
  kPetTarget,
  kPlayerPet,
  kNone,
  kNpc,
  kQuestNpc,
  kVehicle,
};

struct ParsedUnitId {
  UnitIdKind kind{UnitIdKind::kUnknown};
  int index{0};
};

[[nodiscard]] ParsedUnitId ParseUnitId(std::string_view unit_id);

class UnitQueryBridge {
 public:
  UnitQueryBridge() = default;

  [[nodiscard]] ObjectGuid ResolveToGuid(
      WorldSession* session, std::string_view unit_id) const;

  [[nodiscard]] std::optional<UnitQuerySnapshot> Query(
      WorldSession* session, std::string_view unit_id) const;

  [[nodiscard]] static UnitQuerySnapshot SnapshotFromObject(
      const CGObject_C* obj);

  [[nodiscard]] std::optional<UnitQuerySnapshot> GetPlayerInfoByGUID(
      WorldSession* session, std::uint64_t raw_guid) const;

  static UnitQueryBridge& Get();

 private:
  [[nodiscard]] ObjectGuid ResolveRelativeTarget(
      WorldSession* session, ObjectGuid base_guid) const;

  [[nodiscard]] ObjectGuid ResolveChainedSuffix(
      WorldSession* session, ObjectGuid base_guid,
      std::string_view suffix) const;
};

}

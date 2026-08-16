#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>
#include <string>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

class CGUnit_C;
class CGPlayer_C;
class ObjectManager;
class WorldSession;
struct CreatureTemplateInfo;

namespace UnitNameFlag {
inline constexpr std::uint32_t kOwn = 0x0001;
inline constexpr std::uint32_t kNPC = 0x0002;
inline constexpr std::uint32_t kPlayerGuild = 0x0004;
inline constexpr std::uint32_t kPlayerPVPTitle = 0x0008;
inline constexpr std::uint32_t kEnemyPlayer = 0x0010;
inline constexpr std::uint32_t kEnemyPet = 0x0020;
inline constexpr std::uint32_t kEnemyTotem = 0x0040;
inline constexpr std::uint32_t kFriendlyPlayer = 0x0080;
inline constexpr std::uint32_t kFriendlyPet = 0x0100;
inline constexpr std::uint32_t kFriendlyTotem = 0x0200;
inline constexpr std::uint32_t kNonCombatCreature = 0x0400;
inline constexpr std::uint32_t kEnemyGuardian = 0x1000;
inline constexpr std::uint32_t kFriendlyGuardian = 0x2000;
}

struct UnitNameText {
  std::string text;
  std::uint32_t lines{1};
};

struct UnitNameViewerRelation {
  bool attack_resolved{false};
  bool viewer_can_attack_unit{false};
};

[[nodiscard]] bool UnitName_ShouldRender(
    const CGUnit_C &unit, const CGUnit_C &viewer, const ObjectManager &objects,
    std::uint32_t display_flags, bool ui_visible, bool unit_has_nameplate,
    std::uint64_t target_guid, std::uint64_t active_mover_guid,
    UnitNameViewerRelation &relation);

[[nodiscard]] UnitNameText UnitName_BuildText(
    const CGUnit_C &unit, const ObjectManager &objects,
    WorldSession &world_session, std::uint32_t display_flags,
    const CreatureTemplateInfo *creature_template,
    const data::dbc::DbcLoader *dbc);

[[nodiscard]] std::uint32_t UnitName_ResolveColor(
    const CGUnit_C &unit, const CGUnit_C &viewer,
    const WorldSession *world_session, std::uint64_t target_guid,
    std::uint32_t now_ms, UnitNameViewerRelation &relation);

[[nodiscard]] float UnitName_ComputeScale(float height);

}

#pragma once

#include "openwow/game/object_guid.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::net::wotlk {

inline constexpr std::size_t kSpellTargetStringBlockBytes = 128;

enum class SpellCastFlags : std::uint32_t {
  kNone              = 0x00000000,
  kPending           = 0x00000001,
  kHasTrajectory     = 0x00000002,
  kUnknown3          = 0x00000004,
  kUnknown4          = 0x00000008,
  kUnknown5          = 0x00000010,
  kProjectile        = 0x00000020,
  kUnknown7          = 0x00000040,
  kUnknown8          = 0x00000080,
  kUnknown9          = 0x00000100,
  kUnknown10         = 0x00000200,
  kUnknown11         = 0x00000400,
  kPowerLeftSelf     = 0x00000800,
  kUnknown13         = 0x00001000,
  kUnknown14         = 0x00002000,
  kUnknown15         = 0x00004000,
  kUnknown16         = 0x00008000,
  kUnknown17         = 0x00010000,
  kAdjustMissile     = 0x00020000,

  kNoPetGlobalCooldown = 0x00040000,
  kVisualChain       = 0x00080000,
  kUnknown21         = 0x00100000,
  kRuneList          = 0x00200000,
  kUnknown23         = 0x00400000,
  kUnknown24         = 0x00800000,
  kUnknown25         = 0x01000000,
  kUnknown26         = 0x02000000,
  kImmunity          = 0x04000000,
  kUnknown28         = 0x08000000,
  kUnknown29         = 0x10000000,
};

[[nodiscard]] constexpr SpellCastFlags operator|(SpellCastFlags a, SpellCastFlags b) {
  return static_cast<SpellCastFlags>(
      static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
[[nodiscard]] constexpr SpellCastFlags operator&(SpellCastFlags a, SpellCastFlags b) {
  return static_cast<SpellCastFlags>(
      static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}
[[nodiscard]] constexpr bool HasFlag(SpellCastFlags flags, SpellCastFlags test) {
  return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(test)) != 0;
}

enum class SpellCastTargetFlags : std::uint32_t {
  kNone             = 0x00000000,
  kUnused1          = 0x00000001,
  kUnit             = 0x00000002,
  kUnitRaid         = 0x00000004,
  kUnitParty        = 0x00000008,
  kItem             = 0x00000010,
  kSourceLocation   = 0x00000020,
  kDestLocation     = 0x00000040,
  kUnitEnemy        = 0x00000080,
  kUnitAlly         = 0x00000100,
  kCorpseEnemy      = 0x00000200,
  kUnitDead         = 0x00000400,
  kGameObject       = 0x00000800,
  kTradeItem        = 0x00001000,
  kString           = 0x00002000,
  kGameObjectItem   = 0x00004000,
  kCorpseAlly       = 0x00008000,
  kUnitMinipet      = 0x00010000,
  kGlyphSlot        = 0x00020000,
  kDestTarget       = 0x00040000,
  kExtraTargets     = 0x00080000,
  kUnitPassenger    = 0x00100000,
};

[[nodiscard]] constexpr SpellCastTargetFlags operator|(SpellCastTargetFlags a,
                                                        SpellCastTargetFlags b) {
  return static_cast<SpellCastTargetFlags>(
      static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
[[nodiscard]] constexpr SpellCastTargetFlags operator&(SpellCastTargetFlags a,
                                                        SpellCastTargetFlags b) {
  return static_cast<SpellCastTargetFlags>(
      static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}
[[nodiscard]] constexpr bool HasFlag(SpellCastTargetFlags flags,
                                      SpellCastTargetFlags test) {
  return (static_cast<std::uint32_t>(flags) &
          static_cast<std::uint32_t>(test)) != 0;
}

enum class SpellMissInfo : std::uint8_t {
  kNone     = 0,
  kMiss     = 1,
  kResist   = 2,
  kDodge    = 3,
  kParry    = 4,
  kBlock    = 5,
  kEvade    = 6,
  kImmune   = 7,
  kImmune2  = 8,
  kDeflect  = 9,
  kAbsorb   = 10,
  kReflect  = 11,
};

struct SpellPosition {
  game::ObjectGuid transport_guid;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct SpellCastTargets {
  SpellCastTargetFlags target_mask = SpellCastTargetFlags::kNone;
  game::ObjectGuid object_target_guid;
  game::ObjectGuid item_target_guid;
  std::optional<SpellPosition> source_location;
  std::optional<SpellPosition> dest_location;
  std::string target_string;
};

struct AmmoData {
  std::uint32_t display_id       = 0;
  std::uint32_t inventory_type   = 0;
};

struct SpellMissEntry {
  game::ObjectGuid target;
  SpellMissInfo    reason   = SpellMissInfo::kNone;
  SpellMissInfo    reflect  = SpellMissInfo::kNone;
};

struct SpellExtraTarget {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  game::ObjectGuid transport_guid;
};

struct RuneData {
  std::uint8_t mask_before        = 0;
  std::uint8_t mask_after         = 0;
  std::vector<std::uint8_t> cooldowns;
};

struct MissileData {
  float          elevation  = 0.0f;
  std::uint32_t  delay_time = 0;
};

struct SpellImmunityData {
  std::uint32_t school_mask = 0;
  std::uint32_t mechanic_mask = 0;
};

struct SpellStartData {
  game::ObjectGuid  caster_guid;
  game::ObjectGuid  caster_unit_guid;
  std::uint8_t      cast_count  = 0;
  std::uint32_t     spell_id    = 0;
  SpellCastFlags    cast_flags  = SpellCastFlags::kNone;
  std::int32_t      cast_time   = 0;

  SpellCastTargets  targets;

  std::optional<std::uint32_t> current_power;
  std::optional<RuneData>      rune_data;
  std::optional<AmmoData>      ammo;
  std::optional<SpellImmunityData> immunity;
};

struct SpellGoData {
  game::ObjectGuid  caster_guid;
  game::ObjectGuid  caster_unit_guid;
  std::uint8_t      cast_count  = 0;
  std::uint32_t     spell_id    = 0;
  SpellCastFlags    cast_flags  = SpellCastFlags::kNone;
  std::uint32_t     timestamp   = 0;

  std::vector<game::ObjectGuid> hit_targets;
  std::vector<SpellMissEntry>   miss_targets;

  SpellCastTargets  targets;

  std::optional<std::uint32_t>  current_power;
  std::optional<RuneData>       rune_data;
  std::optional<MissileData>    missile;
  std::optional<AmmoData>       ammo;
  std::optional<std::pair<std::uint32_t, std::uint32_t>> visual_chain;
  std::optional<std::uint8_t>   dest_loc_spell_cast_index;
  std::vector<SpellExtraTarget> extra_targets;
};

struct SpellFailureData {
  game::ObjectGuid  caster_guid;
  std::uint8_t      cast_count = 0;
  std::uint32_t     spell_id   = 0;
  std::uint8_t      result     = 0;
};

struct CastFailedData {
  std::uint8_t   cast_count   = 0;
  std::uint32_t  spell_id     = 0;
  std::uint8_t   result       = 0;

  std::vector<std::uint32_t> extra;
};

struct SpellDelayedData {
  game::ObjectGuid  caster_guid;
  std::uint32_t     delay_time = 0;
};

struct ChannelStartData {
  game::ObjectGuid  caster_guid;
  std::uint32_t     spell_id   = 0;
  std::uint32_t     duration   = 0;
};

struct ChannelUpdateData {
  game::ObjectGuid  caster_guid;
  std::uint32_t     remaining  = 0;
};

[[nodiscard]] std::optional<SpellStartData>
ParseSpellStart(const std::uint8_t* data, std::size_t len);

[[nodiscard]] std::optional<SpellGoData>
ParseSpellGo(const std::uint8_t* data, std::size_t len);

[[nodiscard]] std::optional<SpellFailureData>
ParseSpellFailure(const std::uint8_t* data, std::size_t len);

[[nodiscard]] std::optional<CastFailedData>
ParseCastFailed(const std::uint8_t* data, std::size_t len);

[[nodiscard]] std::optional<SpellDelayedData>
ParseSpellDelayed(const std::uint8_t* data, std::size_t len);

[[nodiscard]] std::optional<ChannelStartData>
ParseChannelStart(const std::uint8_t* data, std::size_t len);

[[nodiscard]] std::optional<ChannelUpdateData>
ParseChannelUpdate(const std::uint8_t* data, std::size_t len);

[[nodiscard]] std::size_t ReadPackedGuid(const std::uint8_t* data,
                                          std::size_t len,
                                          std::size_t offset,
                                          game::ObjectGuid& out);

[[nodiscard]] std::size_t ReadSpellCastTargets(const std::uint8_t* data,
                                                std::size_t len,
                                                std::size_t offset,
                                                SpellCastTargets& out);

}

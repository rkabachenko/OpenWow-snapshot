
#include "openwow/game/contested_area.h"

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/objects/cgplayer.h"

namespace openwow::game {

namespace {

constexpr std::uint32_t kAreaFlagPvERealmPvpOverride = 0x00000010u;
constexpr std::uint32_t kAreaFlagCombat = 0x01000000u;

std::string ResolveFactionGroupName(
    const openwow::data::dbc::DbcLoader& dbc,
    const std::uint32_t faction_group_mask) {
  for (const auto& entry : dbc.faction_group()) {
    if (entry.mask_id >= 32u || entry.name.empty()) {
      continue;
    }
    if ((faction_group_mask & (1u << entry.mask_id)) != 0u) {
      return std::string(entry.name);
    }
  }
  return {};
}

}

ZonePvPInfo ResolveRetailZonePvpInfo(
    const openwow::data::dbc::DbcLoader& dbc,
    const CGPlayer_C* const active_player,
    const std::uint32_t zone_id,
    const std::uint32_t sub_zone_id,
    const bool is_pvp_realm) {
  const auto* const sub_zone = dbc.area_table().LookupEntry(sub_zone_id);
  const auto* const zone = dbc.area_table().LookupEntry(zone_id);
  const auto* const pvp_area = sub_zone != nullptr ? sub_zone : zone;
  const bool is_sub_zone_pvp = sub_zone != nullptr;

  const std::uint32_t flags = pvp_area != nullptr ? pvp_area->flags : 0u;
  if ((flags & openwow::data::dbc::kAreaFlagSanctuary) != 0u) {
    return {
        .available = true,
        .type = ZonePvPType::Sanctuary,
        .is_sub_zone_pvp = is_sub_zone_pvp,
    };
  }
  if ((flags & openwow::data::dbc::kAreaFlagArena) != 0u) {
    return {
        .available = true,
        .type = ZonePvPType::FFA,
        .is_sub_zone_pvp = is_sub_zone_pvp,
    };
  }
  if ((flags & kAreaFlagCombat) != 0u) {
    return {
        .available = true,
        .type = ZonePvPType::Combat,
        .is_sub_zone_pvp = is_sub_zone_pvp,
    };
  }

  if (active_player == nullptr || zone == nullptr ||
      (!is_pvp_realm &&
       (zone->flags & kAreaFlagPvERealmPvpOverride) == 0u)) {
    return {};
  }

  ZonePvPType type = ZonePvPType::Contested;
  if ((flags & openwow::data::dbc::kAreaFlagSubZone) == 0u) {
    const auto* const faction_template = dbc.faction_template().LookupEntry(
        active_player->State().GetFactionTemplate());
    if (faction_template == nullptr ||
        (faction_template->friend_group & zone->faction_group_mask) != 0u) {
      type = ZonePvPType::Friendly;
    } else if ((faction_template->enemy_group &
                zone->faction_group_mask) != 0u) {
      type = ZonePvPType::Hostile;
    }
  }

  return {
      .available = true,
      .type = type,
      .is_sub_zone_pvp = false,
      .has_faction_name = true,
      .faction_name =
          ResolveFactionGroupName(dbc, zone->faction_group_mask),
  };
}

void ContestedAreaTracker::RegisterZone(std::uint32_t zoneId,
                                        ZonePvPType type,
                                        const std::string& name) {
  zones_[zoneId] = ZoneEntry{type, name};
}

ZonePvPType ContestedAreaTracker::GetZonePvPType(
    std::uint32_t zoneId) const {
  auto it = zones_.find(zoneId);
  if (it != zones_.end()) return it->second.type;

  return ZonePvPType::Friendly;
}

std::string ContestedAreaTracker::GetZoneName(std::uint32_t zoneId) const {
  auto it = zones_.find(zoneId);
  if (it != zones_.end()) return it->second.name;
  return {};
}

std::string ContestedAreaTracker::GetPvPTypeName(ZonePvPType type) {
  switch (type) {
    case ZonePvPType::Friendly:  return "Friendly";
    case ZonePvPType::Hostile:   return "Hostile";
    case ZonePvPType::Contested: return "Contested";
    case ZonePvPType::Sanctuary: return "Sanctuary";
    case ZonePvPType::FFA:       return "FFA";
    case ZonePvPType::Combat:    return "Combat";
  }
  return "Unknown";
}

std::uint32_t ContestedAreaTracker::GetPvPTypeColor(ZonePvPType type) {
  switch (type) {
    case ZonePvPType::Friendly:  return 0xFF00FF00;
    case ZonePvPType::Hostile:   return 0xFFFF0000;
    case ZonePvPType::Contested: return 0xFFFF8000;
    case ZonePvPType::Sanctuary: return 0xFF69CCF0;
    case ZonePvPType::FFA:       return 0xFFFF0000;
    case ZonePvPType::Combat:    return 0xFFFF0000;
  }
  return 0xFFFFFFFF;
}

void ContestedAreaTracker::Clear() {
  current_type_    = ZonePvPType::Friendly;
  player_faction_  = 0;
  zones_.clear();
}

}

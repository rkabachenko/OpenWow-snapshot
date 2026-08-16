
#include "openwow/game/party_stats.h"

#include "openwow/game/objects/cgunit.h"
#include "openwow/game/world_session.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace openwow::game {

namespace {

using namespace GroupUpdateFlag;

constexpr std::uint32_t kLiveSnapshotMask = kStatus | kCurHp | kMaxHp | kPowerType | kCurPower |
                                            kMaxPower | kLevel | kPosition | kAuras | kVehicleSeat;

std::int16_t ClampPositionToInt16(float value) {
  const auto rounded = static_cast<long>(std::lround(value));
  return static_cast<std::int16_t>(
      std::clamp<long>(rounded, static_cast<long>(std::numeric_limits<std::int16_t>::min()),
                       static_cast<long>(std::numeric_limits<std::int16_t>::max())));
}

void MergeStatsIntoCache(const PartyMemberStats &update, const std::uint32_t update_mask,
                         CachedPartyMemberStats &cache_entry) {
  cache_entry.stats.guid = update.guid;

  if ((update_mask & kStatus) != 0) {
    cache_entry.stats.status = update.status;
    cache_entry.available_mask |= kStatus;
  }
  if ((update_mask & kCurHp) != 0) {
    cache_entry.stats.cur_hp = update.cur_hp;
    cache_entry.available_mask |= kCurHp;
  }
  if ((update_mask & kMaxHp) != 0) {
    cache_entry.stats.max_hp = update.max_hp;
    cache_entry.available_mask |= kMaxHp;
  }
  if ((update_mask & kPowerType) != 0) {
    cache_entry.stats.power_type = update.power_type;
    cache_entry.available_mask |= kPowerType;
  }
  if ((update_mask & kCurPower) != 0) {
    cache_entry.stats.cur_power = update.cur_power;
    cache_entry.available_mask |= kCurPower;
  }
  if ((update_mask & kMaxPower) != 0) {
    cache_entry.stats.max_power = update.max_power;
    cache_entry.available_mask |= kMaxPower;
  }
  if ((update_mask & kLevel) != 0) {
    cache_entry.stats.level = update.level;
    cache_entry.available_mask |= kLevel;
  }
  if ((update_mask & kZone) != 0) {
    cache_entry.stats.zone_id = update.zone_id;
    cache_entry.available_mask |= kZone;
  }
  if ((update_mask & kPosition) != 0) {
    cache_entry.stats.pos_x = update.pos_x;
    cache_entry.stats.pos_y = update.pos_y;
    cache_entry.available_mask |= kPosition;
  }
  if ((update_mask & kAuras) != 0) {
    cache_entry.stats.auras = update.auras;
    cache_entry.available_mask |= kAuras;
  }
  if ((update_mask & kPetGuid) != 0) {
    cache_entry.stats.pet_guid = update.pet_guid;
    cache_entry.available_mask |= kPetGuid;
  }
  if ((update_mask & kPetName) != 0) {
    cache_entry.stats.pet_name = update.pet_name;
    cache_entry.available_mask |= kPetName;
  }
  if ((update_mask & kPetModelId) != 0) {
    cache_entry.stats.pet_display_id = update.pet_display_id;
    cache_entry.available_mask |= kPetModelId;
  }
  if ((update_mask & kPetCurHp) != 0) {
    cache_entry.stats.pet_cur_hp = update.pet_cur_hp;
    cache_entry.available_mask |= kPetCurHp;
  }
  if ((update_mask & kPetMaxHp) != 0) {
    cache_entry.stats.pet_max_hp = update.pet_max_hp;
    cache_entry.available_mask |= kPetMaxHp;
  }
  if ((update_mask & kPetPowerType) != 0) {
    cache_entry.stats.pet_power_type = update.pet_power_type;
    cache_entry.available_mask |= kPetPowerType;
  }
  if ((update_mask & kPetCurPower) != 0) {
    cache_entry.stats.pet_cur_power = update.pet_cur_power;
    cache_entry.available_mask |= kPetCurPower;
  }
  if ((update_mask & kPetMaxPower) != 0) {
    cache_entry.stats.pet_max_power = update.pet_max_power;
    cache_entry.available_mask |= kPetMaxPower;
  }
  if ((update_mask & kPetAuras) != 0) {
    cache_entry.stats.pet_auras = update.pet_auras;
    cache_entry.available_mask |= kPetAuras;
  }
  if ((update_mask & kVehicleSeat) != 0) {
    cache_entry.stats.vehicle_seat = update.vehicle_seat;
    cache_entry.available_mask |= kVehicleSeat;
  }

  cache_entry.stats.update_mask = cache_entry.available_mask;
}

PartyMemberStats BuildLiveSnapshot(const WorldSession& session, const CGUnit_C &unit) {
  auto tooltip = unit.BuildTooltipInfo(session);
  const auto position = unit.GetPosition();
  tooltip.position_x = ClampPositionToInt16(position.x);
  tooltip.position_y = ClampPositionToInt16(position.y);

  PartyMemberStats snapshot;
  snapshot.guid = unit.GetGuid();
  snapshot.update_mask = kLiveSnapshotMask;
  snapshot.status = GroupMemberStatus::kOnline;
  if ((tooltip.flags & UnitTooltipFlag::kPvP) != 0) {
    snapshot.status |= GroupMemberStatus::kPvp;
  }
  if ((tooltip.flags & UnitTooltipFlag::kDead) != 0) {
    snapshot.status |= GroupMemberStatus::kDead;
  }
  snapshot.cur_hp = static_cast<std::uint32_t>(std::max(tooltip.health, 0));
  snapshot.max_hp = static_cast<std::uint32_t>(std::max(tooltip.max_health, 0));
  snapshot.power_type = tooltip.power_type_display;
  snapshot.cur_power = tooltip.power;
  snapshot.max_power = tooltip.max_power;
  snapshot.level = tooltip.level;
  snapshot.pos_x = tooltip.position_x;
  snapshot.pos_y = tooltip.position_y;
  snapshot.vehicle_seat = tooltip.vehicle_seat_spell_id;

  snapshot.auras.clear();
  snapshot.auras.reserve(tooltip.GetAuraCount());
  for (std::size_t i = 0; i < UnitTooltipInfo::kMaxTooltipAuras; ++i) {
    if (tooltip.aura_spell_ids[i] == 0) {
      break;
    }
    GroupAuraInfo aura_info;
    aura_info.spell_id = tooltip.aura_spell_ids[i];
    aura_info.flags = tooltip.aura_flags[i];
    snapshot.auras.push_back(aura_info);
  }

  return snapshot;
}

}

bool PartyStatsManager::ReadAuraMask(PacketReader &r, std::vector<GroupAuraInfo> &out) {
  std::uint64_t mask;
  if (!r.ReadU64(mask))
    return false;

  out.clear();
  for (int i = 0; i < 64; ++i) {
    if (mask & (std::uint64_t(1) << i)) {
      GroupAuraInfo ai;
      if (!r.ReadU32(ai.spell_id))
        return false;
      if (!r.ReadU8(ai.flags))
        return false;
      out.push_back(ai);
    }
  }
  return true;
}

bool PartyStatsManager::ParseStats(PacketReader &r, PartyMemberStats &out) {
  if (!r.ReadPackedGuid(out.guid))
    return false;
  if (!r.ReadU32(out.update_mask))
    return false;

  using namespace GroupUpdateFlag;
  auto m = out.update_mask;

  if (m & kStatus) {
    if (!r.ReadU16(out.status))
      return false;
  }
  if (m & kCurHp) {
    if (!r.ReadU32(out.cur_hp))
      return false;
  }
  if (m & kMaxHp) {
    if (!r.ReadU32(out.max_hp))
      return false;
  }
  if (m & kPowerType) {
    if (!r.ReadU8(out.power_type))
      return false;
  }
  if (m & kCurPower) {
    if (!r.ReadU16(out.cur_power))
      return false;
  }
  if (m & kMaxPower) {
    if (!r.ReadU16(out.max_power))
      return false;
  }
  if (m & kLevel) {
    if (!r.ReadU16(out.level))
      return false;
  }
  if (m & kZone) {
    if (!r.ReadU16(out.zone_id))
      return false;
  }

  if (m & kPosition) {
    std::uint16_t px, py;
    if (!r.ReadU16(px))
      return false;
    if (!r.ReadU16(py))
      return false;
    out.pos_x = static_cast<std::int16_t>(px);
    out.pos_y = static_cast<std::int16_t>(py);
  }

  if (m & kAuras) {
    if (!ReadAuraMask(r, out.auras))
      return false;
  }

  if (m & kPetGuid) {
    if (!r.ReadU64(out.pet_guid))
      return false;
  }
  if (m & kPetName) {
    if (!r.ReadCString(out.pet_name))
      return false;
  }
  if (m & kPetModelId) {
    if (!r.ReadU16(out.pet_display_id))
      return false;
  }
  if (m & kPetCurHp) {
    if (!r.ReadU32(out.pet_cur_hp))
      return false;
  }
  if (m & kPetMaxHp) {
    if (!r.ReadU32(out.pet_max_hp))
      return false;
  }
  if (m & kPetPowerType) {
    if (!r.ReadU8(out.pet_power_type))
      return false;
  }
  if (m & kPetCurPower) {
    if (!r.ReadU16(out.pet_cur_power))
      return false;
  }
  if (m & kPetMaxPower) {
    if (!r.ReadU16(out.pet_max_power))
      return false;
  }

  if (m & kPetAuras) {
    if (!ReadAuraMask(r, out.pet_auras))
      return false;
  }

  if (m & kVehicleSeat) {
    if (!r.ReadU32(out.vehicle_seat))
      return false;
  }

  return true;
}

bool PartyStatsManager::HandlePartyMemberStats(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  PartyMemberStats st;
  if (!ParseStats(r, st))
    return false;
  last_ = std::move(st);
  CachePacketUpdate(last_);
  ++count_;
  return true;
}

bool PartyStatsManager::HandlePartyMemberStatsFull(const std::uint8_t *data, std::size_t len) {
  PacketReader r(data, len);
  std::uint8_t arena_opponent_marker;
  if (!r.ReadU8(arena_opponent_marker))
    return false;

  PartyMemberStats st;
  if (!ParseStats(r, st))
    return false;
  st.is_arena_opponent_marker = arena_opponent_marker != 0;
  last_ = std::move(st);
  CachePacketUpdate(last_);
  ++count_;
  return true;
}

std::optional<CachedPartyMemberStats>
PartyStatsManager::GetCachedMember(const std::uint64_t guid) const {
  if (guid == 0) {
    return std::nullopt;
  }

  const auto it = cached_members_.find(guid);
  if (it == cached_members_.end()) {
    return std::nullopt;
  }
  return it->second;
}

void PartyStatsManager::CaptureLiveMemberSnapshot(const WorldSession& session,
                                                  const CGUnit_C &unit) {
  const auto raw_guid = unit.GetGuid().GetRawValue();
  if (raw_guid == 0) {
    return;
  }

  auto snapshot = BuildLiveSnapshot(session, unit);
  auto &cache_entry = cached_members_[raw_guid];
  MergeStatsIntoCache(snapshot, kLiveSnapshotMask, cache_entry);
}

bool PartyStatsManager::ClearCachedReferAFriendFlag(const std::uint64_t guid) {
  if (guid == 0) {
    return false;
  }

  auto it = cached_members_.find(guid);
  if (it == cached_members_.end()) {
    return false;
  }

  auto &cache_entry = it->second;
  if ((cache_entry.available_mask & GroupUpdateFlag::kStatus) == 0 ||
      (cache_entry.stats.status & GroupMemberStatus::kReferAFriendLinked) == 0) {
    return false;
  }

  cache_entry.stats.status &= ~GroupMemberStatus::kReferAFriendLinked;
  if (last_.guid.GetRawValue() == guid && (last_.update_mask & GroupUpdateFlag::kStatus) != 0) {
    last_.status &= ~GroupMemberStatus::kReferAFriendLinked;
  }
  return true;
}

void PartyStatsManager::CachePacketUpdate(const PartyMemberStats &update) {
  const auto raw_guid = update.guid.GetRawValue();
  if (raw_guid == 0) {
    return;
  }

  auto &cache_entry = cached_members_[raw_guid];
  MergeStatsIntoCache(update, update.update_mask, cache_entry);
}

void PartyStatsManager::Clear() {
  last_ = PartyMemberStats{};
  count_ = 0;
  cached_members_.clear();
}

}

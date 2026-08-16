
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "openwow/game/object_guid.h"
#include "openwow/game/packet_reader.h"

namespace openwow::game {

class CGUnit_C;
class WorldSession;

namespace GroupUpdateFlag {
inline constexpr std::uint32_t kStatus = 0x00000001;
inline constexpr std::uint32_t kCurHp = 0x00000002;
inline constexpr std::uint32_t kMaxHp = 0x00000004;
inline constexpr std::uint32_t kPowerType = 0x00000008;
inline constexpr std::uint32_t kCurPower = 0x00000010;
inline constexpr std::uint32_t kMaxPower = 0x00000020;
inline constexpr std::uint32_t kLevel = 0x00000040;
inline constexpr std::uint32_t kZone = 0x00000080;
inline constexpr std::uint32_t kPosition = 0x00000100;
inline constexpr std::uint32_t kAuras = 0x00000200;
inline constexpr std::uint32_t kPetGuid = 0x00000400;
inline constexpr std::uint32_t kPetName = 0x00000800;
inline constexpr std::uint32_t kPetModelId = 0x00001000;
inline constexpr std::uint32_t kPetCurHp = 0x00002000;
inline constexpr std::uint32_t kPetMaxHp = 0x00004000;
inline constexpr std::uint32_t kPetPowerType = 0x00008000;
inline constexpr std::uint32_t kPetCurPower = 0x00010000;
inline constexpr std::uint32_t kPetMaxPower = 0x00020000;
inline constexpr std::uint32_t kPetAuras = 0x00040000;
inline constexpr std::uint32_t kVehicleSeat = 0x00080000;
}

namespace GroupMemberStatus {
inline constexpr std::uint16_t kOnline = 0x0001;
inline constexpr std::uint16_t kPvp = 0x0002;
inline constexpr std::uint16_t kDead = 0x0004;
inline constexpr std::uint16_t kGhost = 0x0008;
inline constexpr std::uint16_t kPvpFfa = 0x0010;
inline constexpr std::uint16_t kAfk = 0x0040;
inline constexpr std::uint16_t kDnd = 0x0080;
inline constexpr std::uint16_t kReferAFriendLinked = 0x0100;
}

struct GroupAuraInfo {
  std::uint32_t spell_id = 0;
  std::uint8_t flags = 0;
};

struct PartyMemberStats {
  ObjectGuid guid{ObjectGuid(0)};
  std::uint32_t update_mask = 0;

  std::uint16_t status = 0;
  std::uint32_t cur_hp = 0;
  std::uint32_t max_hp = 0;
  std::uint8_t power_type = 0;
  std::uint16_t cur_power = 0;
  std::uint16_t max_power = 0;
  std::uint16_t level = 0;
  std::uint16_t zone_id = 0;
  std::int16_t pos_x = 0;
  std::int16_t pos_y = 0;
  std::vector<GroupAuraInfo> auras;

  std::uint64_t pet_guid = 0;
  std::string pet_name;
  std::uint16_t pet_display_id = 0;
  std::uint32_t pet_cur_hp = 0;
  std::uint32_t pet_max_hp = 0;
  std::uint8_t pet_power_type = 0;
  std::uint16_t pet_cur_power = 0;
  std::uint16_t pet_max_power = 0;
  std::vector<GroupAuraInfo> pet_auras;

  std::uint32_t vehicle_seat = 0;

  bool is_arena_opponent_marker = false;
};

struct CachedPartyMemberStats {
  PartyMemberStats stats{};
  std::uint32_t available_mask = 0;
};

class PartyStatsManager {
public:

  bool HandlePartyMemberStats(const std::uint8_t *data, std::size_t len);
  bool HandlePartyMemberStatsFull(const std::uint8_t *data, std::size_t len);

  [[nodiscard]] const PartyMemberStats &last_update() const {
    return last_;
  }
  [[nodiscard]] std::size_t update_count() const {
    return count_;
  }
  [[nodiscard]] std::optional<CachedPartyMemberStats> GetCachedMember(std::uint64_t guid) const;

  void CaptureLiveMemberSnapshot(const WorldSession& session, const CGUnit_C &unit);
  bool ClearCachedReferAFriendFlag(std::uint64_t guid);
  void Clear();

private:
  bool ParseStats(PacketReader &r, PartyMemberStats &out);
  static bool ReadAuraMask(PacketReader &r, std::vector<GroupAuraInfo> &out);
  void CachePacketUpdate(const PartyMemberStats &update);

  PartyMemberStats last_{};
  std::size_t count_ = 0;
  std::unordered_map<std::uint64_t, CachedPartyMemberStats> cached_members_{};
};

}

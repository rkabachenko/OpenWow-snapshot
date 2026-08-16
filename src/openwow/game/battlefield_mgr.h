
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "openwow/game/packet_reader.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

namespace openwow::game {

struct BattlefieldEntryInvite {
  std::uint32_t battle_id = 0;
  std::uint8_t  accept_flag = 0;
};

struct BattlefieldEntered {
  std::uint32_t battle_id = 0;
  std::uint32_t area_id = 0;
  std::uint8_t status_flag = 0;
  std::uint8_t secondary_flag = 0;
  std::uint8_t cleared_afk = 0;
};

struct BattlefieldQueueInvite {
  std::uint32_t queue_id = 0;
  std::uint8_t invite_flag = 0;
  std::uint8_t warmup = 0;
  std::uint8_t cleared_afk = 0;
};

struct GroupJoinedBattleground {
  std::int32_t result = 0;
  std::uint64_t player_guid = 0;
  bool has_guid = false;
};

struct BattlefieldEjected {
  std::uint32_t queue_id = 0;
  std::uint32_t reason = 0;
};

struct BattlefieldEjectPending {
  std::uint32_t queue_id = 0;
  std::uint8_t reason = 0;
  std::uint8_t relocate_flag = 0;
  std::uint8_t battleground_flag = 0;
};

struct BattlefieldQueueResponse {
  std::uint32_t queue_id = 0;
  std::uint8_t  accepted = 0;
};

struct BattlefieldStateChange {
  std::uint32_t battlefield_id = 0;
  std::uint32_t area_id = 0;
  std::uint32_t expiry_time = 0;
};

class BattlefieldMgrHandler {
 public:
  bool HandleEntryInvite(const std::uint8_t* data, std::size_t len);
  bool HandleEntered(const std::uint8_t* data, std::size_t len);
  bool HandleQueueInvite(const std::uint8_t* data, std::size_t len);
  bool HandleGroupJoinedBattleground(const std::uint8_t* data, std::size_t len);
  bool HandleEjected(const std::uint8_t* data, std::size_t len);
  bool HandleEjectPending(const std::uint8_t* data, std::size_t len);
  bool HandleQueueRequestResponse(const std::uint8_t* data, std::size_t len);
  bool HandleStateChange(const std::uint8_t* data, std::size_t len);

  [[nodiscard]] static net::wotlk::WorldPacket BuildEntryInviteResponse(
      std::uint32_t battle_id, bool accepted);

  [[nodiscard]] static net::wotlk::WorldPacket BuildQueueRequest(
      std::uint32_t battlefield_id);

  [[nodiscard]] static net::wotlk::WorldPacket BuildQueueInviteResponse(
      std::uint32_t battlefield_id, bool accepted);

  [[nodiscard]] static net::wotlk::WorldPacket BuildExitRequest(
      std::uint32_t battlefield_id);

  [[nodiscard]] const std::optional<BattlefieldEntryInvite>& last_entry_invite() const {
    return last_entry_invite_;
  }
  [[nodiscard]] const std::optional<BattlefieldEntered>& last_entered() const {
    return last_entered_;
  }
  [[nodiscard]] const std::optional<BattlefieldQueueInvite>& last_queue_invite() const {
    return last_queue_invite_;
  }
  [[nodiscard]] const std::optional<GroupJoinedBattleground>& last_group_joined() const {
    return last_group_joined_;
  }
  [[nodiscard]] const std::optional<BattlefieldEjected>& last_ejected() const {
    return last_ejected_;
  }
  [[nodiscard]] const std::optional<BattlefieldEjectPending>&
  last_eject_pending() const {
    return last_eject_pending_;
  }
  [[nodiscard]] const std::optional<BattlefieldQueueResponse>& last_queue_response() const {
    return last_queue_response_;
  }
  [[nodiscard]] const std::optional<BattlefieldStateChange>& last_state_change() const {
    return last_state_change_;
  }

  void Clear();

 private:
  std::optional<BattlefieldEntryInvite> last_entry_invite_;
  std::optional<BattlefieldEntered> last_entered_;
  std::optional<BattlefieldQueueInvite> last_queue_invite_;
  std::optional<GroupJoinedBattleground> last_group_joined_;
  std::optional<BattlefieldEjected> last_ejected_;
  std::optional<BattlefieldEjectPending> last_eject_pending_;
  std::optional<BattlefieldQueueResponse> last_queue_response_;
  std::optional<BattlefieldStateChange> last_state_change_;
};

}

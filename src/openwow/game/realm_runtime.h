#pragma once

#include "openwow/game/creep.h"
#include "openwow/game/tumor.h"
#include "openwow/game/warden_client.h"
#include "openwow/net/wotlk/main_thread_packet_dispatcher.h"
#include "openwow/net/wotlk/protocol/world_protocol.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace openwow::game {

class RealmRuntime final {
 public:
  RealmRuntime();
  ~RealmRuntime();

  RealmRuntime(const RealmRuntime&) = delete;
  RealmRuntime& operator=(const RealmRuntime&) = delete;

  [[nodiscard]] std::array<std::uint8_t, 20> BuildBotDetectedDigest(
      std::span<const std::uint8_t> probe) const;

  void InitWarden(std::span<const std::uint8_t> session_key);

  net::wotlk::MainThreadPacketDispatcher packet_dispatcher;
  WardenClient warden;
  Creep creep;
  TumorManager tumor;
  net::wotlk::RealmSession session;

 private:
  bool DeliverWardenPacket(const std::uint8_t* payload, std::size_t length);

  net::wotlk::MainThreadPacketDispatcher::Registration
      warden_packet_registration_;

  std::vector<std::vector<std::uint8_t>> pending_warden_packets_;
};

}

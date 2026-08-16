#include "openwow/game/realm_runtime.h"

#include "openwow/foundation/hashing/retail_sha1.h"
#include "openwow/network/protocol/wotlk/world_packet.h"

namespace openwow::game {

RealmRuntime::RealmRuntime() {
  creep.RegisterEventBinding("SendEvent", &TumorManager::ScriptSendEvent,
                             &tumor);
  session.SetDisconnectedCallback([this]() {
    warden.Reset();
    pending_warden_packets_.clear();
  });
  warden_packet_registration_ = packet_dispatcher.Register(
      net::wotlk::Opcode::SMSG_WARDEN_DATA, "realm_warden",
      [this](const net::wotlk::WorldPacket& packet) {
        if (packet.payload.empty()) {
          return true;
        }

        if (!warden.IsInitialized()) {
          pending_warden_packets_.emplace_back(packet.payload.begin(),
                                              packet.payload.end());
          return true;
        }
        return DeliverWardenPacket(packet.payload.data(),
                                   packet.payload.size());
      });
}

void RealmRuntime::InitWarden(const std::span<const std::uint8_t> session_key) {
  if (warden.IsInitialized()) {
    return;
  }
  warden.Init(session_key.data(), session_key.size());
  auto buffered = std::move(pending_warden_packets_);
  pending_warden_packets_.clear();
  for (const auto& payload : buffered) {
    if (payload.empty()) {
      continue;
    }
    (void)DeliverWardenPacket(payload.data(), payload.size());
  }
}

bool RealmRuntime::DeliverWardenPacket(const std::uint8_t* payload,
                                       const std::size_t length) {
  warden.HandleWardenData(payload, length);

  bool sent_all = true;
  while (warden.HasPendingResponse()) {
    auto response = warden.BuildResponse();
    if (response.empty()) {
      break;
    }
    net::wotlk::WorldPacket response_packet(
        net::wotlk::Opcode::CMSG_WARDEN_DATA);
    response_packet.AppendBytes(response.data(), response.size());
    if (!session.SendPacket(response_packet)) {
      sent_all = false;
      break;
    }
  }
  return sent_all;
}

RealmRuntime::~RealmRuntime() {
  session.SetDisconnectedCallback({});
  session.Disconnect();
  warden.Reset();
  pending_warden_packets_.clear();
  creep.UnregisterEventBinding("SendEvent", &TumorManager::ScriptSendEvent,
                               &tumor);
}

std::array<std::uint8_t, 20> RealmRuntime::BuildBotDetectedDigest(
    const std::span<const std::uint8_t> probe) const {
  std::array<std::uint8_t, 20> digest{};
  const auto& session_key = session.session_key();
  foundation::hashing::RetailSha1State sha{};
  foundation::hashing::InitializeRetailSha1(sha);
  foundation::hashing::UpdateRetailSha1(
      sha, session_key.data(),
      static_cast<std::uint32_t>(session_key.size()));
  foundation::hashing::UpdateRetailSha1(
      sha, probe.data(), static_cast<std::uint32_t>(probe.size()));
  foundation::hashing::UpdateRetailSha1(
      sha, session_key.data(),
      static_cast<std::uint32_t>(session_key.size()));
  foundation::hashing::FinalizeRetailSha1(sha, digest.data());
  return digest;
}

}

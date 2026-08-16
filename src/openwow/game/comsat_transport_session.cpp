
#include "openwow/game/comsat_transport_session.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace openwow::game {

void ComSatTransportCoordinator::Initialize(const std::uint32_t session_id_low,
                                            const std::uint32_t session_id_high,
                                            ComSatTransportPacketSink *packet_sink) noexcept {
  session_id_low_ = session_id_low;
  session_id_high_ = session_id_high;
  routing_id_ = 0;
  last_dispatch_tick_ = 0;
  packet_sink_ = packet_sink;
  routes_.clear();
  endpoints_.clear();
  transport_members_.clear();
}

bool ComSatTransportCoordinator::DispatchVoiceBatch(const std::uint32_t dest_id_low,
                                                    const std::uint32_t dest_id_high,
                                                    const void *buffer,
                                                    const std::size_t buffer_size) {

  const ComSatTransportRoute *route = FindRoute(dest_id_low, dest_id_high);
  if (route == nullptr) {
    return false;
  }

  for (const auto *endpoint : endpoints_) {
    if (endpoint != nullptr) {
      SendPacketToEndpoint(route->routing_byte, *endpoint, buffer, buffer_size);
    }
  }

  return true;
}

bool ComSatTransportCoordinator::RegisterSessionMember(
    const std::uint32_t member_id_low, const std::uint32_t member_id_high,
    const std::uint8_t routing_byte, const std::uint8_t *address_16,
    const std::uint8_t *key_32) {
  if (address_16 == nullptr || key_32 == nullptr) {
    return false;
  }

  ComSatTransportEndpoint *existing = nullptr;
  for (auto *ep : endpoints_) {
    if (ep != nullptr && ep->MatchesKey(key_32)) {

      std::memcpy(ep->address, address_16, ComSatTransportEndpoint::kAddressSize);
      ++ep->ref_count;
      existing = ep;
      break;
    }
  }

  if (existing == nullptr) {

    auto *ep = new ComSatTransportEndpoint();
    std::memcpy(ep->key, key_32, ComSatTransportEndpoint::kKeySize);
    std::memcpy(ep->address, address_16, ComSatTransportEndpoint::kAddressSize);
    ep->ref_count = 1;
    endpoints_.push_back(ep);

    for (const auto &r : routes_) {
      SendPacketToEndpoint(r.routing_byte, *ep, nullptr, 0);
    }

    existing = ep;
  }

  routes_.push_back({member_id_low, member_id_high, routing_byte, existing});
  return true;
}

void ComSatTransportCoordinator::RemoveSessionMember(
    const std::uint32_t member_id_low, const std::uint32_t member_id_high) {

  auto it = std::find_if(routes_.begin(), routes_.end(),
                         [=](const ComSatTransportRoute &r) {
                           return r.destination_id_low == member_id_low &&
                                  r.destination_id_high == member_id_high;
                         });
  if (it == routes_.end()) {

    RemoveTransportMemberEntry(member_id_low, member_id_high);
    return;
  }

  ComSatTransportEndpoint *ep = it->endpoint;
  if (ep != nullptr) {
    if (ep->ref_count > 0) {
      --ep->ref_count;
    }

    if (ep->ref_count == 0) {
      auto ep_it = std::find(endpoints_.begin(), endpoints_.end(), ep);
      if (ep_it != endpoints_.end()) {
        endpoints_.erase(ep_it);
      }
      delete ep;
    }
  }

  routes_.erase(it);
}

bool ComSatTransportCoordinator::AddTransportMember(
    const std::uint32_t member_id_low, const std::uint32_t member_id_high) {

  for (const auto &entry : transport_members_) {
    if (entry.member_id_low == member_id_low &&
        entry.member_id_high == member_id_high) {
      return false;
    }
  }
  transport_members_.push_back({member_id_low, member_id_high});
  return true;
}

bool ComSatTransportCoordinator::RemoveTransportMemberEntry(
    const std::uint32_t member_id_low, const std::uint32_t member_id_high) {
  auto it = std::find_if(
      transport_members_.begin(), transport_members_.end(),
      [=](const ComSatTransportMemberEntry &e) {
        return e.member_id_low == member_id_low &&
               e.member_id_high == member_id_high;
      });
  if (it == transport_members_.end()) {
    return false;
  }
  transport_members_.erase(it);
  return true;
}

void ComSatTransportCoordinator::SetRoutingId(const std::uint16_t routing_id) noexcept {
  routing_id_ = routing_id;
}

void ComSatTransportCoordinator::Heartbeat(const std::uint32_t current_tick_ms) {
  static constexpr std::uint32_t kHeartbeatIntervalMs = 10000u;

  if (routes_.empty()) {
    return;
  }

  if (current_tick_ms - last_dispatch_tick_ <= kHeartbeatIntervalMs) {
    return;
  }

  if (!routes_.empty() && !endpoints_.empty()) {
    const std::uint8_t routing_byte = routes_.front().routing_byte;
    for (const auto *ep : endpoints_) {
      if (ep != nullptr) {
        SendPacketToEndpoint(routing_byte, *ep, nullptr, 0);
      }
    }
  }

  last_dispatch_tick_ = current_tick_ms;
}

void ComSatTransportCoordinator::SendPacketToEndpoint(
    const std::uint8_t routing_byte, const ComSatTransportEndpoint &endpoint,
    const void *voice_data, const std::size_t voice_size) {
  static constexpr std::size_t kMaxPacketPayload = 0x400;
  const std::size_t header_size = 3;
  const std::size_t total_payload = voice_size + header_size;

  if (total_payload > kMaxPacketPayload) {
    return;

  }

  std::array<std::uint8_t, kMaxPacketPayload> payload{};
  payload[0] = routing_byte;
  payload[1] = static_cast<std::uint8_t>(routing_id_ & 0xFF);
  payload[2] = static_cast<std::uint8_t>((routing_id_ >> 8) & 0xFF);
  if (voice_data != nullptr && voice_size > 0) {
    std::memcpy(&payload[header_size], voice_data, voice_size);
  }

  const std::uint32_t hmac =
      ComputePacketHmac(endpoint.address, payload.data(), total_payload);

  const std::size_t packet_size = 4 + total_payload;
  std::array<std::uint8_t, 4 + kMaxPacketPayload> packet{};
  std::memcpy(packet.data(), &hmac, 4);
  std::memcpy(packet.data() + 4, payload.data(), total_payload);

  if (packet_sink_ != nullptr) {
    packet_sink_->SendPacket(endpoint, packet.data(), packet_size);
  }
}

std::uint32_t ComSatTransportCoordinator::ComputePacketHmac(
    const std::uint8_t * , const std::uint8_t *data,
    const std::size_t data_size) {

  std::uint32_t hash = 0;
  for (std::size_t i = 0; i < data_size; ++i) {
    hash ^= static_cast<std::uint32_t>(data[i]) << ((i & 3u) * 8u);
  }
  return hash;
}

const ComSatTransportRoute *
ComSatTransportCoordinator::FindRoute(const std::uint32_t dest_id_low,
                                      const std::uint32_t dest_id_high) const noexcept {

  for (const auto &route : routes_) {
    if (route.destination_id_low == dest_id_low &&
        route.destination_id_high == dest_id_high) {
      return &route;
    }
  }
  return nullptr;
}

void ComSatTransportSession::Initialize(const std::uint32_t session_id_low,
                                        const std::uint32_t session_id_high) noexcept {
  session_id_low_ = session_id_low;
  session_id_high_ = session_id_high;
  coordinator_ = nullptr;
}

void ComSatTransportSession::Send(const std::uint32_t destination_id_low,
                                  const std::uint32_t destination_id_high,
                                  const std::uint8_t *buffer,
                                  const std::size_t buffer_size) {
  if (coordinator_ == nullptr) {
    return;
  }

  (void)coordinator_->DispatchVoiceBatch(destination_id_low, destination_id_high,
                                         buffer, buffer_size);
}

void ComSatTransportSession::RemoveTransportMember(
    const std::uint32_t member_id_low, const std::uint32_t member_id_high) {
  if (coordinator_ == nullptr) {
    return;
  }
  coordinator_->RemoveSessionMember(member_id_low, member_id_high);
}

bool ComSatTransportSession::AddTransportMember(
    const std::uint32_t member_id_low, const std::uint32_t member_id_high) {
  if (coordinator_ == nullptr) {
    return false;
  }
  return coordinator_->AddTransportMember(member_id_low, member_id_high);
}

void ComSatTransportSession::SetCoordinator(
    ComSatTransportCoordinator *coordinator) noexcept {
  coordinator_ = coordinator;
}

ComSatTransportCoordinator *ComSatTransportSession::GetCoordinator() const noexcept {
  return coordinator_;
}

ComSatSoundIOSocketTransport::ComSatSoundIOSocketTransport(const std::uint16_t bind_port)
    : datagram_socket_(ComSatSoundIO_CreateSocketWrapper()) {
  if (datagram_socket_) {

    (void)datagram_socket_->Bind(bind_port);
  }
}

ComSatSoundIOSocketTransport::~ComSatSoundIOSocketTransport() {
  DestroyOwnedState();
}

void ComSatSoundIOSocketTransport::Update(const std::uint32_t tick_count_ms) {
  last_update_tick_ = tick_count_ms;

  if (!datagram_socket_ || !datagram_socket_->IsOpen()) {
    return;
  }

  static constexpr int kMaxReceivesPerUpdate = 10;
  static constexpr std::size_t kPacketBufferSize = 1024;

  static constexpr std::size_t kMinPacketSize = 7;

  ComSatDatagramEndpoint sender;
  char buffer[kPacketBufferSize];
  std::size_t received_size = kPacketBufferSize;

  for (int i = 0; i < kMaxReceivesPerUpdate; ++i) {
    received_size = kPacketBufferSize;
    if (!datagram_socket_->ReceiveFrom(sender, buffer, received_size)) {
      break;
    }
    if (received_size < kMinPacketSize) {
      continue;
    }

    const auto *packet = reinterpret_cast<const std::uint8_t *>(buffer);
    const std::uint16_t routing_id = static_cast<std::uint16_t>(packet[5]) |
                                     (static_cast<std::uint16_t>(packet[6]) << 8);

    for (auto &coord : coordinators_) {
      if (coord && coord->routing_id() == routing_id) {

        const std::size_t voice_offset = 7;
        if (received_size > voice_offset) {

        }
        break;
      }
    }
  }

  for (auto &coord : coordinators_) {
    if (coord) {
      coord->Heartbeat(tick_count_ms);
    }
  }
}

ComSatTransportCoordinator *ComSatSoundIOSocketTransport::CreateCoordinator(
    const std::uint32_t session_id_low, const std::uint32_t session_id_high) {
  auto coordinator = std::make_unique<ComSatTransportCoordinator>();
  coordinator->Initialize(session_id_low, session_id_high, this);
  auto *raw = coordinator.get();
  coordinators_.push_back(std::move(coordinator));
  return raw;
}

void ComSatSoundIOSocketTransport::RemoveCoordinator(
    ComSatTransportCoordinator *coordinator) {
  if (coordinator == nullptr) {
    return;
  }
  for (auto it = coordinators_.begin(); it != coordinators_.end(); ++it) {
    if (it->get() == coordinator) {
      coordinators_.erase(it);
      return;
    }
  }
}

void ComSatSoundIOSocketTransport::DestroyOwnedState() {
  datagram_socket_.reset();
  coordinators_.clear();
}

bool ComSatSoundIOSocketTransport::RegisterMemberEndpoint(
    ComSatTransportCoordinator *coordinator,
    const std::uint32_t member_id_low, const std::uint32_t member_id_high,
    const std::uint8_t routing_byte,
    const std::uint8_t *address_16, const std::uint8_t *key_32) {
  if (coordinator == nullptr) {
    return false;
  }
  return coordinator->RegisterSessionMember(member_id_low, member_id_high,
                                            routing_byte, address_16, key_32);
}

void ComSatSoundIOSocketTransport::SendPacket(
    const ComSatTransportEndpoint &endpoint,
    const std::uint8_t *packet_data, const std::size_t packet_size) {
  if (!datagram_socket_ || !datagram_socket_->IsOpen()) {
    return;
  }

  ComSatDatagramEndpoint dest;
  static_assert(ComSatTransportEndpoint::kAddressSize <= sizeof(dest.storage));
  std::memcpy(&dest.storage, endpoint.address, ComSatTransportEndpoint::kAddressSize);
  dest.length = ComSatDatagramEndpoint::kIpv4SockaddrLength;

  (void)datagram_socket_->SendTo(dest, reinterpret_cast<const char *>(packet_data),
                                 static_cast<int>(packet_size));
}

bool ComSatSoundIOSocketTransport::ValidateInboundPacket(
    ComSatTransportCoordinator * , std::uint8_t ,
    const ComSatDatagramEndpoint & ,
    ComSatDatagramEndpoint * ,
    const std::uint8_t ** ) {

  return true;
}

}

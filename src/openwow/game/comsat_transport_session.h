
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "openwow/game/comsat_sound_io.h"

namespace openwow::game {

class ComSatTransportCoordinator;
struct ComSatTransportEndpoint;

struct ComSatTransportMemberEntry {
  std::uint32_t member_id_low{0};
  std::uint32_t member_id_high{0};
};

struct ComSatTransportRoute {
  std::uint32_t destination_id_low{0};
  std::uint32_t destination_id_high{0};
  std::uint8_t routing_byte{0};
  ComSatTransportEndpoint *endpoint{nullptr};

};

struct ComSatTransportEndpoint {
  static constexpr std::size_t kKeySize = 32;
  static constexpr std::size_t kAddressSize = 16;

  std::uint8_t key[kKeySize]{};
  std::uint8_t address[kAddressSize]{};
  std::uint32_t ref_count{0};

  [[nodiscard]] bool MatchesKey(const void *other_key) const noexcept {
    return std::memcmp(key, other_key, kKeySize) == 0;
  }
};

class ComSatTransportPacketSink {
public:
  virtual ~ComSatTransportPacketSink() = default;

  virtual void SendPacket(const ComSatTransportEndpoint &endpoint,
                          const std::uint8_t *packet_data,
                          std::size_t packet_size) = 0;
};

class ComSatTransportCoordinator {
public:
  ComSatTransportCoordinator() = default;
  ~ComSatTransportCoordinator() = default;

  ComSatTransportCoordinator(const ComSatTransportCoordinator &) = delete;
  ComSatTransportCoordinator &operator=(const ComSatTransportCoordinator &) = delete;

  void Initialize(std::uint32_t session_id_low, std::uint32_t session_id_high,
                  ComSatTransportPacketSink *packet_sink) noexcept;

  [[nodiscard]] bool DispatchVoiceBatch(std::uint32_t dest_id_low,
                                        std::uint32_t dest_id_high,
                                        const void *buffer,
                                        std::size_t buffer_size);

  [[nodiscard]] bool RegisterSessionMember(std::uint32_t member_id_low,
                                           std::uint32_t member_id_high,
                                           std::uint8_t routing_byte,
                                           const std::uint8_t *address_16,
                                           const std::uint8_t *key_32);

  void RemoveSessionMember(std::uint32_t member_id_low,
                           std::uint32_t member_id_high);

  [[nodiscard]] bool AddTransportMember(std::uint32_t member_id_low,
                                        std::uint32_t member_id_high);

  void SetRoutingId(std::uint16_t routing_id) noexcept;

  [[nodiscard]] std::uint16_t routing_id() const noexcept { return routing_id_; }

  void Heartbeat(std::uint32_t current_tick_ms);

private:
  void SendPacketToEndpoint(std::uint8_t routing_byte,
                            const ComSatTransportEndpoint &endpoint,
                            const void *voice_data, std::size_t voice_size);

  [[nodiscard]] static std::uint32_t ComputePacketHmac(
      const std::uint8_t *key_16, const std::uint8_t *data,
      std::size_t data_size);

  [[nodiscard]] const ComSatTransportRoute *
  FindRoute(std::uint32_t dest_id_low, std::uint32_t dest_id_high) const noexcept;

  std::uint32_t session_id_low_{0};
  std::uint32_t session_id_high_{0};
  std::uint16_t routing_id_{0};
  std::uint32_t last_dispatch_tick_{0};
  ComSatTransportPacketSink *packet_sink_{nullptr};

  bool RemoveTransportMemberEntry(std::uint32_t member_id_low,
                                  std::uint32_t member_id_high);

  std::vector<ComSatTransportMemberEntry> transport_members_;
  std::vector<ComSatTransportRoute> routes_;
  std::vector<ComSatTransportEndpoint *> endpoints_;
};

class ComSatTransportSession : public ComSatVoiceBatchSender {
public:
  ComSatTransportSession() = default;
  ~ComSatTransportSession() override = default;

  ComSatTransportSession(const ComSatTransportSession &) = delete;
  ComSatTransportSession &operator=(const ComSatTransportSession &) = delete;

  void Initialize(std::uint32_t session_id_low, std::uint32_t session_id_high) noexcept;

  void Send(std::uint32_t destination_id_low, std::uint32_t destination_id_high,
            const std::uint8_t *buffer, std::size_t buffer_size) override;

  void RemoveTransportMember(std::uint32_t member_id_low,
                             std::uint32_t member_id_high);

  [[nodiscard]] bool AddTransportMember(std::uint32_t member_id_low,
                                        std::uint32_t member_id_high);

  void SetCoordinator(ComSatTransportCoordinator *coordinator) noexcept;
  [[nodiscard]] ComSatTransportCoordinator *GetCoordinator() const noexcept;

  [[nodiscard]] std::uint32_t session_id_low() const noexcept { return session_id_low_; }
  [[nodiscard]] std::uint32_t session_id_high() const noexcept { return session_id_high_; }

private:
  std::uint32_t session_id_low_{0};
  std::uint32_t session_id_high_{0};
  ComSatTransportCoordinator *coordinator_{nullptr};

};

class ComSatSoundIOSocketTransport : public ComSatTransportPacketSink {
public:

  explicit ComSatSoundIOSocketTransport(std::uint16_t bind_port);

  ~ComSatSoundIOSocketTransport() override;

  ComSatSoundIOSocketTransport(const ComSatSoundIOSocketTransport &) = delete;
  ComSatSoundIOSocketTransport &operator=(const ComSatSoundIOSocketTransport &) = delete;

  void Update(std::uint32_t tick_count_ms);

  ComSatTransportCoordinator *CreateCoordinator(std::uint32_t session_id_low,
                                                std::uint32_t session_id_high);

  void RemoveCoordinator(ComSatTransportCoordinator *coordinator);

  void DestroyOwnedState();

  [[nodiscard]] bool RegisterMemberEndpoint(ComSatTransportCoordinator *coordinator,
                                            std::uint32_t member_id_low,
                                            std::uint32_t member_id_high,
                                            std::uint8_t routing_byte,
                                            const std::uint8_t *address_16,
                                            const std::uint8_t *key_32);

  void SendPacket(const ComSatTransportEndpoint &endpoint,
                  const std::uint8_t *packet_data,
                  std::size_t packet_size) override;

  [[nodiscard]] ComSatDatagramSocket *datagram_socket() const noexcept {
    return datagram_socket_.get();
  }
  [[nodiscard]] std::size_t coordinator_count() const noexcept {
    return coordinators_.size();
  }

private:

  [[nodiscard]] static bool ValidateInboundPacket(
      ComSatTransportCoordinator *coordinator, std::uint8_t packet_flags,
      const ComSatDatagramEndpoint &sender, ComSatDatagramEndpoint *out_validated_sender,
      const std::uint8_t **out_payload);

  std::unique_ptr<ComSatDatagramSocket> datagram_socket_;
  std::vector<std::unique_ptr<ComSatTransportCoordinator>> coordinators_;
  std::uint32_t last_update_tick_{0};
};

}

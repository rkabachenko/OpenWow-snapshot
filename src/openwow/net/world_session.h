#pragma once

#include <cstdint>
#include <string>

namespace openwow::net {

enum class WorldSessionState {
  kDisconnected,
  kConnecting,
  kLoading,
  kInWorld,
  kFailed,
};

class WorldSession {
 public:
  void BeginConnect();
  void BeginLoading();
  void EnterWorld();
  void MarkFailed();
  void Disconnect();

  [[nodiscard]] WorldSessionState state() const;
  [[nodiscard]] std::string GetStateName() const;
  [[nodiscard]] bool IsInWorld() const;
  [[nodiscard]] bool IsConnected() const;

  void SetCharacterName(const std::string& name);
  [[nodiscard]] const std::string& GetCharacterName() const;
  void SetRealmName(const std::string& name);
  [[nodiscard]] const std::string& GetRealmName() const;
  void SetMapId(std::uint32_t mapId);
  [[nodiscard]] std::uint32_t GetMapId() const;

  void SetLatency(std::uint32_t ms);
  [[nodiscard]] std::uint32_t GetLatency() const;

  void AddPacketsSent(std::uint32_t count = 1);
  void AddPacketsReceived(std::uint32_t count = 1);
  [[nodiscard]] std::uint64_t GetPacketsSent() const;
  [[nodiscard]] std::uint64_t GetPacketsReceived() const;

  void SetErrorMessage(const std::string& msg);
  [[nodiscard]] const std::string& GetErrorMessage() const;
  [[nodiscard]] bool HasError() const;

  [[nodiscard]] float GetSessionDuration() const;

  void Update(float dt);
  void Reset();

 private:
  WorldSessionState state_{WorldSessionState::kDisconnected};
  std::string character_name_;
  std::string realm_name_;
  std::uint32_t map_id_{0};
  std::uint32_t latency_ms_{0};
  std::uint64_t packets_sent_{0};
  std::uint64_t packets_received_{0};
  std::string error_message_;
  float session_duration_{0.0f};
};

}

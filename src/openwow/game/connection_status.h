#pragma once

#include <cstdint>
#include <string>

namespace openwow::game {

enum class ConnectionPhase : std::uint8_t {
  Disconnected,
  Connecting,
  Authenticating,
  Connected,
  RealmHandshake,
  WorldHandshake,
  CharacterList,
  InWorld,
  Disconnecting,
};

class ConnectionStatusDisplay {
 public:
  ConnectionStatusDisplay() = default;

  void SetPhase(ConnectionPhase phase);
  [[nodiscard]] ConnectionPhase GetPhase() const;

  [[nodiscard]] bool IsConnected() const;
  [[nodiscard]] bool IsInWorld() const;
  [[nodiscard]] bool IsDisconnected() const;
  [[nodiscard]] bool IsConnecting() const;

  void SetRealmName(const std::string& name);
  [[nodiscard]] const std::string& GetRealmName() const;

  void SetCharacterName(const std::string& name);
  [[nodiscard]] const std::string& GetCharacterName() const;

  void SetConnectionTime(float seconds);
  [[nodiscard]] float GetConnectionTime() const;
  [[nodiscard]] std::string GetUptime() const;

  void SetDisconnectReason(const std::string& reason);
  [[nodiscard]] const std::string& GetDisconnectReason() const;

  [[nodiscard]] static std::string GetPhaseName(ConnectionPhase phase);
  [[nodiscard]] static std::uint32_t GetPhaseColor(ConnectionPhase phase);

  void Update(float dt);
  void Reset();

 private:
  ConnectionPhase phase_ = ConnectionPhase::Disconnected;
  std::string realm_name_;
  std::string character_name_;
  float connection_time_ = 0.0f;
  std::string disconnect_reason_;
};

}

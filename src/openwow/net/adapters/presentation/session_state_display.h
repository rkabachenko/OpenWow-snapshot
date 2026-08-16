#pragma once

#include <cstdint>
#include <string>

namespace openwow::net {

enum class SessionDisplayPhase : std::uint8_t {
  Disconnected,
  Connecting,
  Authenticating,
  CharacterSelect,
  EnteringWorld,
  InWorld,
  Disconnecting,
};

struct SessionDisplayInfo {
  SessionDisplayPhase phase{SessionDisplayPhase::Disconnected};
  std::string serverName;
  std::string characterName;
  std::uint32_t latencyMs{0};
  float uptimeSeconds{0.0f};
  std::string lastError;
};

class SessionStateDisplay {
 public:
  SessionStateDisplay() = default;

  void SetPhase(SessionDisplayPhase phase);
  [[nodiscard]] SessionDisplayPhase GetPhase() const;

  [[nodiscard]] std::string GetPhaseName() const;

  void SetServerName(std::string name);
  [[nodiscard]] const std::string& GetServerName() const;

  void SetCharacterName(std::string name);
  [[nodiscard]] const std::string& GetCharacterName() const;

  void SetLatency(std::uint32_t ms);
  [[nodiscard]] std::uint32_t GetLatency() const;

  void UpdateUptime(float dt);
  [[nodiscard]] float GetUptime() const;

  void SetLastError(std::string error);
  [[nodiscard]] const std::string& GetLastError() const;
  [[nodiscard]] bool HasError() const;

  [[nodiscard]] bool IsConnected() const;

  [[nodiscard]] bool IsInWorld() const;

  [[nodiscard]] SessionDisplayInfo GetDisplayInfo() const;

  void Reset();

 private:
  SessionDisplayPhase phase_{SessionDisplayPhase::Disconnected};
  std::string server_name_;
  std::string character_name_;
  std::uint32_t latency_ms_{0};
  float uptime_seconds_{0.0f};
  std::string last_error_;
};

}

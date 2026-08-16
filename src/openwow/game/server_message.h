
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class ServerMessageType : std::uint8_t {
  Shutdown      = 0,
  Restart       = 1,
  Custom        = 2,
  ServerMessage = 3,
  InstanceReset = 4,
  Maintenance   = 5,
};

struct ServerMessageEntry {
  ServerMessageType messageType{ServerMessageType::Custom};
  std::string       text;
  std::uint32_t     timeRemaining{0};
  float             timestamp{0.0f};
  bool              isUrgent{false};
};

class ServerMessageSystem {
 public:
  ServerMessageSystem() = default;

  void AddMessage(ServerMessageEntry entry);

  [[nodiscard]] const std::vector<ServerMessageEntry>& GetMessages() const;

  [[nodiscard]] std::optional<ServerMessageEntry> GetLatestMessage() const;

  [[nodiscard]] std::vector<ServerMessageEntry> GetMessagesByType(
      ServerMessageType type) const;

  [[nodiscard]] std::vector<ServerMessageEntry> GetUrgentMessages() const;

  [[nodiscard]] bool HasShutdownWarning() const;
  [[nodiscard]] std::uint32_t GetShutdownTime() const;

  [[nodiscard]] bool HasRestartWarning() const;
  [[nodiscard]] std::uint32_t GetRestartTime() const;

  [[nodiscard]] bool HasMaintenanceWarning() const;

  [[nodiscard]] std::uint32_t GetMessageCount() const;

  void ClearMessages();

  void SetMaxMessages(std::uint32_t max);

  [[nodiscard]] static std::string FormatTimeRemaining(std::uint32_t seconds);

  void Update(float dt);

  void Reset();

 private:
  std::vector<ServerMessageEntry> messages_;
  std::uint32_t max_messages_{50};
};

}

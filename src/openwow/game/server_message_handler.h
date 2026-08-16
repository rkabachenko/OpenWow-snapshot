
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class ServerMsgType : std::uint8_t {
  ShutdownTime      = 0,
  RestartTime       = 1,
  Custom            = 2,
  ShutdownCancelled = 3,
  RestartCancelled  = 4,
};

struct ServerMessageHandlerEntry {
  ServerMsgType type{ServerMsgType::Custom};
  std::string   message;
  std::uint32_t timeRemaining{0};
  double        timestamp{0.0};
  bool          read{false};
};

class ServerMessageHandler {
 public:
  ServerMessageHandler() = default;

  void PushMessage(ServerMsgType type, const std::string& message,
                   std::uint32_t timeRemaining = 0);

  [[nodiscard]] std::vector<ServerMessageHandlerEntry> GetMessages() const;
  [[nodiscard]] std::optional<ServerMessageHandlerEntry> GetLastMessage() const;
  [[nodiscard]] bool HasShutdownPending() const;
  [[nodiscard]] std::uint32_t GetShutdownTime() const;
  [[nodiscard]] std::string GetShutdownFormatted() const;

  void Update(float deltaTime);

  [[nodiscard]] bool IsShutdownCancelled() const;
  void CancelShutdown();

  [[nodiscard]] std::uint32_t GetMessageCount() const;
  void ClearAll();

  [[nodiscard]] std::vector<ServerMessageHandlerEntry> GetMessagesByType(
      ServerMsgType type) const;

  [[nodiscard]] bool HasUnreadMessages() const;
  void MarkAllRead();

  static constexpr std::uint32_t kMaxStored = 30;

 private:
  std::vector<ServerMessageHandlerEntry> messages_;
  bool                                   shutdownCancelled_{false};
  double                                 clock_{0.0};
};

}

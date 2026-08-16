#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

namespace openwow::net {

enum class ConnectionState : std::uint8_t {
  Disconnected,
  Connecting,
  Connected,
  Authenticating,
  WorldConnecting,
  WorldConnected,
  Disconnecting,
  Reconnecting,
};

enum class DisconnectReason : std::uint8_t {
  None,
  Timeout,
  ServerKick,
  AuthFailed,
  NetworkError,
  UserDisconnect,
  ServerShutdown,
};

class ConnectionManager {
 public:

  using DisconnectCallback = std::function<void(DisconnectReason)>;
  using ReconnectAttemptCallback = std::function<void(std::uint32_t attempt)>;
  using ReconnectSuccessCallback = std::function<void()>;

  ConnectionManager();
  ~ConnectionManager() = default;

  ConnectionManager(const ConnectionManager&) = delete;
  ConnectionManager& operator=(const ConnectionManager&) = delete;
  ConnectionManager(ConnectionManager&&) noexcept = default;
  ConnectionManager& operator=(ConnectionManager&&) noexcept = default;

  bool Connect(const std::string& host, std::uint16_t port);

  void Disconnect(DisconnectReason reason = DisconnectReason::UserDisconnect);

  bool Reconnect();

  [[nodiscard]] ConnectionState GetState() const { return state_; }

  [[nodiscard]] bool IsConnected() const;

  [[nodiscard]] DisconnectReason GetDisconnectReason() const {
    return disconnect_reason_;
  }

  [[nodiscard]] std::uint32_t GetLatency() const { return latency_ms_; }

  void UpdateLatency(std::uint32_t ms);

  [[nodiscard]] const std::string& GetServerAddress() const { return host_; }

  [[nodiscard]] std::uint16_t GetServerPort() const { return port_; }

  [[nodiscard]] float GetConnectionUptime() const;

  void SetTimeout(float seconds) { timeout_seconds_ = seconds; }
  [[nodiscard]] float GetTimeout() const { return timeout_seconds_; }

  [[nodiscard]] std::uint32_t GetReconnectAttempts() const {
    return reconnect_attempts_;
  }
  [[nodiscard]] std::uint32_t GetMaxReconnectAttempts() const {
    return max_reconnect_attempts_;
  }
  void SetMaxReconnectAttempts(std::uint32_t n) {
    max_reconnect_attempts_ = n;
  }

  [[nodiscard]] float GetReconnectDelay() const;

  void Update(float dt);

  void SetState(ConnectionState new_state) { state_ = new_state; }

  void SetOnDisconnect(DisconnectCallback cb) {
    on_disconnect_ = std::move(cb);
  }
  void SetOnReconnectAttempt(ReconnectAttemptCallback cb) {
    on_reconnect_attempt_ = std::move(cb);
  }
  void SetOnReconnectSuccess(ReconnectSuccessCallback cb) {
    on_reconnect_success_ = std::move(cb);
  }

  void Reset();

 private:
  ConnectionState state_{ConnectionState::Disconnected};
  DisconnectReason disconnect_reason_{DisconnectReason::None};

  std::string host_;
  std::uint16_t port_{0};

  std::uint32_t latency_ms_{0};
  float timeout_seconds_{30.0f};

  float connecting_elapsed_{0.0f};
  float connected_uptime_{0.0f};

  std::uint32_t reconnect_attempts_{0};
  std::uint32_t max_reconnect_attempts_{5};
  float reconnect_timer_{0.0f};

  static constexpr float kBaseReconnectDelay = 1.0f;
  static constexpr float kMaxReconnectDelay = 30.0f;

  DisconnectCallback on_disconnect_;
  ReconnectAttemptCallback on_reconnect_attempt_;
  ReconnectSuccessCallback on_reconnect_success_;
};

}

#include "openwow/net/transport/connection_manager.h"

#include <algorithm>
#include <cmath>

namespace openwow::net {

ConnectionManager::ConnectionManager() = default;

bool ConnectionManager::Connect(const std::string& host, std::uint16_t port) {
  if (state_ != ConnectionState::Disconnected &&
      state_ != ConnectionState::Reconnecting) {
    return false;
  }
  host_ = host;
  port_ = port;
  state_ = ConnectionState::Connecting;
  connecting_elapsed_ = 0.0f;
  disconnect_reason_ = DisconnectReason::None;
  return true;
}

void ConnectionManager::Disconnect(DisconnectReason reason) {
  if (state_ == ConnectionState::Disconnected) {
    return;
  }
  state_ = ConnectionState::Disconnecting;
  disconnect_reason_ = reason;

  state_ = ConnectionState::Disconnected;
  connected_uptime_ = 0.0f;
  connecting_elapsed_ = 0.0f;

  if (on_disconnect_) {
    on_disconnect_(reason);
  }
}

bool ConnectionManager::Reconnect() {
  if (state_ != ConnectionState::Disconnected) {
    return false;
  }
  if (host_.empty() || port_ == 0) {
    return false;
  }
  state_ = ConnectionState::Reconnecting;
  reconnect_attempts_ = 0;
  reconnect_timer_ = 0.0f;
  return true;
}

bool ConnectionManager::IsConnected() const {
  switch (state_) {
    case ConnectionState::Connected:
    case ConnectionState::Authenticating:
    case ConnectionState::WorldConnecting:
    case ConnectionState::WorldConnected:
      return true;
    default:
      return false;
  }
}

void ConnectionManager::UpdateLatency(std::uint32_t ms) {
  latency_ms_ = ms;
}

float ConnectionManager::GetConnectionUptime() const {
  if (!IsConnected()) {
    return 0.0f;
  }
  return connected_uptime_;
}

float ConnectionManager::GetReconnectDelay() const {
  if (reconnect_attempts_ == 0) {
    return kBaseReconnectDelay;
  }

  float delay = kBaseReconnectDelay *
                std::pow(2.0f, static_cast<float>(reconnect_attempts_ - 1));
  return std::min(delay, kMaxReconnectDelay);
}

void ConnectionManager::Update(float dt) {
  switch (state_) {
    case ConnectionState::Connecting: {
      connecting_elapsed_ += dt;
      if (connecting_elapsed_ >= timeout_seconds_) {
        Disconnect(DisconnectReason::Timeout);
      }
      break;
    }
    case ConnectionState::Connected:
    case ConnectionState::Authenticating:
    case ConnectionState::WorldConnecting:
    case ConnectionState::WorldConnected: {
      connected_uptime_ += dt;
      break;
    }
    case ConnectionState::Reconnecting: {
      reconnect_timer_ += dt;
      float delay = GetReconnectDelay();
      if (reconnect_timer_ >= delay) {
        reconnect_timer_ = 0.0f;
        reconnect_attempts_++;
        if (on_reconnect_attempt_) {
          on_reconnect_attempt_(reconnect_attempts_);
        }
        if (reconnect_attempts_ > max_reconnect_attempts_) {
          Disconnect(DisconnectReason::Timeout);
        } else {

          state_ = ConnectionState::Connecting;
          connecting_elapsed_ = 0.0f;
        }
      }
      break;
    }
    default:
      break;
  }
}

void ConnectionManager::Reset() {
  state_ = ConnectionState::Disconnected;
  disconnect_reason_ = DisconnectReason::None;
  host_.clear();
  port_ = 0;
  latency_ms_ = 0;
  timeout_seconds_ = 30.0f;
  connecting_elapsed_ = 0.0f;
  connected_uptime_ = 0.0f;
  reconnect_attempts_ = 0;
  max_reconnect_attempts_ = 5;
  reconnect_timer_ = 0.0f;
  on_disconnect_ = nullptr;
  on_reconnect_attempt_ = nullptr;
  on_reconnect_success_ = nullptr;
}

}

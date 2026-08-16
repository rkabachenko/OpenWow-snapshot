#include "openwow/game/connection_status.h"

#include <cstdint>

namespace openwow::game {

void ConnectionStatusDisplay::SetPhase(ConnectionPhase phase) {
  phase_ = phase;
}

ConnectionPhase ConnectionStatusDisplay::GetPhase() const { return phase_; }

bool ConnectionStatusDisplay::IsConnected() const {
  switch (phase_) {
    case ConnectionPhase::Connected:
    case ConnectionPhase::RealmHandshake:
    case ConnectionPhase::WorldHandshake:
    case ConnectionPhase::CharacterList:
    case ConnectionPhase::InWorld:
      return true;
    default:
      return false;
  }
}

bool ConnectionStatusDisplay::IsInWorld() const {
  return phase_ == ConnectionPhase::InWorld;
}

bool ConnectionStatusDisplay::IsDisconnected() const {
  return phase_ == ConnectionPhase::Disconnected;
}

bool ConnectionStatusDisplay::IsConnecting() const {
  switch (phase_) {
    case ConnectionPhase::Connecting:
    case ConnectionPhase::Authenticating:
    case ConnectionPhase::RealmHandshake:
    case ConnectionPhase::WorldHandshake:
      return true;
    default:
      return false;
  }
}

void ConnectionStatusDisplay::SetRealmName(const std::string& name) {
  realm_name_ = name;
}

const std::string& ConnectionStatusDisplay::GetRealmName() const {
  return realm_name_;
}

void ConnectionStatusDisplay::SetCharacterName(const std::string& name) {
  character_name_ = name;
}

const std::string& ConnectionStatusDisplay::GetCharacterName() const {
  return character_name_;
}

void ConnectionStatusDisplay::SetConnectionTime(float seconds) {
  connection_time_ = seconds;
}

float ConnectionStatusDisplay::GetConnectionTime() const {
  return connection_time_;
}

std::string ConnectionStatusDisplay::GetUptime() const {
  auto total = static_cast<std::uint32_t>(connection_time_);
  std::uint32_t hours = total / 3600;
  std::uint32_t minutes = (total % 3600) / 60;
  if (hours > 0) {
    return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
  }
  return std::to_string(minutes) + "m";
}

void ConnectionStatusDisplay::SetDisconnectReason(const std::string& reason) {
  disconnect_reason_ = reason;
}

const std::string& ConnectionStatusDisplay::GetDisconnectReason() const {
  return disconnect_reason_;
}

std::string ConnectionStatusDisplay::GetPhaseName(ConnectionPhase phase) {
  switch (phase) {
    case ConnectionPhase::Disconnected:   return "Disconnected";
    case ConnectionPhase::Connecting:     return "Connecting";
    case ConnectionPhase::Authenticating: return "Authenticating";
    case ConnectionPhase::Connected:      return "Connected";
    case ConnectionPhase::RealmHandshake: return "Realm Handshake";
    case ConnectionPhase::WorldHandshake: return "World Handshake";
    case ConnectionPhase::CharacterList:  return "Character List";
    case ConnectionPhase::InWorld:        return "In World";
    case ConnectionPhase::Disconnecting:  return "Disconnecting";
  }
  return "Unknown";
}

std::uint32_t ConnectionStatusDisplay::GetPhaseColor(ConnectionPhase phase) {
  switch (phase) {
    case ConnectionPhase::Disconnected:   return 0xFFFF0000;
    case ConnectionPhase::Connecting:     return 0xFFFFFF00;
    case ConnectionPhase::Authenticating: return 0xFFFFFF00;
    case ConnectionPhase::Connected:      return 0xFF00FF00;
    case ConnectionPhase::RealmHandshake: return 0xFFFFFF00;
    case ConnectionPhase::WorldHandshake: return 0xFFFFFF00;
    case ConnectionPhase::CharacterList:  return 0xFF00FF00;
    case ConnectionPhase::InWorld:        return 0xFF00FF00;
    case ConnectionPhase::Disconnecting:  return 0xFFFFA500;
  }
  return 0xFFFFFFFF;
}

void ConnectionStatusDisplay::Update(float dt) {
  if (IsConnected()) {
    connection_time_ += dt;
  }
}

void ConnectionStatusDisplay::Reset() {
  phase_ = ConnectionPhase::Disconnected;
  realm_name_.clear();
  character_name_.clear();
  connection_time_ = 0.0f;
  disconnect_reason_.clear();
}

}

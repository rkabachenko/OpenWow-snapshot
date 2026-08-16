#include "openwow/net/adapters/presentation/session_state_display.h"

namespace openwow::net {

void SessionStateDisplay::SetPhase(SessionDisplayPhase phase) {
  phase_ = phase;
}

SessionDisplayPhase SessionStateDisplay::GetPhase() const {
  return phase_;
}

std::string SessionStateDisplay::GetPhaseName() const {
  switch (phase_) {
    case SessionDisplayPhase::Disconnected:   return "Disconnected";
    case SessionDisplayPhase::Connecting:      return "Connecting";
    case SessionDisplayPhase::Authenticating:  return "Authenticating";
    case SessionDisplayPhase::CharacterSelect: return "Character Select";
    case SessionDisplayPhase::EnteringWorld:   return "Entering World";
    case SessionDisplayPhase::InWorld:         return "In World";
    case SessionDisplayPhase::Disconnecting:   return "Disconnecting";
  }
  return "Unknown";
}

void SessionStateDisplay::SetServerName(std::string name) {
  server_name_ = std::move(name);
}

const std::string& SessionStateDisplay::GetServerName() const {
  return server_name_;
}

void SessionStateDisplay::SetCharacterName(std::string name) {
  character_name_ = std::move(name);
}

const std::string& SessionStateDisplay::GetCharacterName() const {
  return character_name_;
}

void SessionStateDisplay::SetLatency(std::uint32_t ms) {
  latency_ms_ = ms;
}

std::uint32_t SessionStateDisplay::GetLatency() const {
  return latency_ms_;
}

void SessionStateDisplay::UpdateUptime(float dt) {
  uptime_seconds_ += dt;
}

float SessionStateDisplay::GetUptime() const {
  return uptime_seconds_;
}

void SessionStateDisplay::SetLastError(std::string error) {
  last_error_ = std::move(error);
}

const std::string& SessionStateDisplay::GetLastError() const {
  return last_error_;
}

bool SessionStateDisplay::HasError() const {
  return !last_error_.empty();
}

bool SessionStateDisplay::IsConnected() const {
  switch (phase_) {
    case SessionDisplayPhase::Connecting:
    case SessionDisplayPhase::Authenticating:
    case SessionDisplayPhase::CharacterSelect:
    case SessionDisplayPhase::EnteringWorld:
    case SessionDisplayPhase::InWorld:
      return true;
    default:
      return false;
  }
}

bool SessionStateDisplay::IsInWorld() const {
  return phase_ == SessionDisplayPhase::InWorld;
}

SessionDisplayInfo SessionStateDisplay::GetDisplayInfo() const {
  return SessionDisplayInfo{
      .phase = phase_,
      .serverName = server_name_,
      .characterName = character_name_,
      .latencyMs = latency_ms_,
      .uptimeSeconds = uptime_seconds_,
      .lastError = last_error_,
  };
}

void SessionStateDisplay::Reset() {
  phase_ = SessionDisplayPhase::Disconnected;
  server_name_.clear();
  character_name_.clear();
  latency_ms_ = 0;
  uptime_seconds_ = 0.0f;
  last_error_.clear();
}

}

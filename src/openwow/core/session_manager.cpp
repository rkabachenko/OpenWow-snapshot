
#include "openwow/core/session_manager.h"

#include <cstdio>

namespace openwow::core {

namespace {

std::string FormatDuration(float seconds) {
  const int total = static_cast<int>(seconds);
  const int h = total / 3600;
  const int m = (total % 3600) / 60;
  const int s = total % 60;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
  return buf;
}

}

void SessionManager::SetState(SessionState state) {
  state_ = state;
}

std::string SessionManager::GetStateName() const {
  switch (state_) {
    case SessionState::Disconnected:    return "Disconnected";
    case SessionState::Connecting:      return "Connecting";
    case SessionState::Authenticating:  return "Authenticating";
    case SessionState::CharacterSelect: return "CharacterSelect";
    case SessionState::EnteringWorld:   return "EnteringWorld";
    case SessionState::InWorld:         return "InWorld";
    case SessionState::Loading:         return "Loading";
    case SessionState::Cinematic:       return "Cinematic";
  }
  return "Unknown";
}

bool SessionManager::IsConnected() const {
  return state_ != SessionState::Disconnected;
}

void SessionManager::SetAccountName(const std::string& name) {
  account_name_ = name;
}

const std::string& SessionManager::GetAccountName() const {
  return account_name_;
}

void SessionManager::SetRealmName(const std::string& name) {
  realm_name_ = name;
}

const std::string& SessionManager::GetRealmName() const {
  return realm_name_;
}

void SessionManager::SetCharacterName(const std::string& name) {
  character_name_ = name;
}

const std::string& SessionManager::GetCharacterName() const {
  return character_name_;
}

void SessionManager::SetLocalPlayerGuid(openwow::game::ObjectGuid guid) {
  local_guid_ = guid;
}

openwow::game::ObjectGuid SessionManager::GetLocalPlayerGuid() const {
  return local_guid_;
}

float SessionManager::GetSessionDuration() const {
  return session_duration_;
}

float SessionManager::GetInWorldDuration() const {
  return in_world_duration_;
}

std::string SessionManager::GetFormattedSessionDuration() const {
  return FormatDuration(session_duration_);
}

std::string SessionManager::GetFormattedInWorldDuration() const {
  return FormatDuration(in_world_duration_);
}

float SessionManager::GetDownloadBandwidth() const {
  if (session_duration_ <= 0.0f) return 0.0f;
  return static_cast<float>(download_bytes_) / session_duration_;
}

float SessionManager::GetUploadBandwidth() const {
  if (session_duration_ <= 0.0f) return 0.0f;
  return static_cast<float>(upload_bytes_) / session_duration_;
}

std::uint32_t SessionManager::GetDisconnectCount() const {
  return disconnect_count_;
}

void SessionManager::IncrementDisconnects() {
  ++disconnect_count_;
}

void SessionManager::SetLastDisconnectReason(const std::string& reason) {
  disconnect_reason_ = reason;
}

const std::string& SessionManager::GetLastDisconnectReason() const {
  return disconnect_reason_;
}

bool SessionManager::ShouldReconnect() const {
  return should_reconnect_;
}

void SessionManager::SetShouldReconnect(bool v) {
  should_reconnect_ = v;
}

float SessionManager::GetFPS() const {
  return fps_;
}

void SessionManager::SetFPS(float fps) {
  fps_ = fps;
}

std::uint32_t SessionManager::GetLatency() const {
  return latency_ms_;
}

void SessionManager::SetLatency(std::uint32_t ms) {
  latency_ms_ = ms;
}

std::uint64_t SessionManager::GetDownloadBytes() const {
  return download_bytes_;
}

void SessionManager::AddDownloadBytes(std::uint64_t bytes) {
  download_bytes_ += bytes;
}

std::uint64_t SessionManager::GetUploadBytes() const {
  return upload_bytes_;
}

void SessionManager::AddUploadBytes(std::uint64_t bytes) {
  upload_bytes_ += bytes;
}

void SessionManager::Update(float dt) {
  if (IsConnected()) {
    session_duration_ += dt;
  }
  if (IsInWorld()) {
    in_world_duration_ += dt;
  }
}

void SessionManager::Reset() {
  state_ = SessionState::Disconnected;
  account_name_.clear();
  realm_name_.clear();
  character_name_.clear();
  local_guid_ = openwow::game::ObjectGuid{};
  session_duration_ = 0.0f;
  in_world_duration_ = 0.0f;
  disconnect_count_ = 0;
  disconnect_reason_.clear();
  should_reconnect_ = false;
  fps_ = 0.0f;
  latency_ms_ = 0;
  download_bytes_ = 0;
  upload_bytes_ = 0;
}

}

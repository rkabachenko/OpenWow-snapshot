
#include "openwow/net/world_session.h"

namespace openwow::net {

void WorldSession::BeginConnect() {
  state_ = WorldSessionState::kConnecting;
  session_duration_ = 0.0f;
}

void WorldSession::BeginLoading() {
  state_ = WorldSessionState::kLoading;
}

void WorldSession::EnterWorld() {
  state_ = WorldSessionState::kInWorld;
}

void WorldSession::MarkFailed() {
  state_ = WorldSessionState::kFailed;
}

void WorldSession::Disconnect() {
  state_ = WorldSessionState::kDisconnected;
}

WorldSessionState WorldSession::state() const {
  return state_;
}

std::string WorldSession::GetStateName() const {
  switch (state_) {
    case WorldSessionState::kDisconnected: return "Disconnected";
    case WorldSessionState::kConnecting:   return "Connecting";
    case WorldSessionState::kLoading:      return "Loading";
    case WorldSessionState::kInWorld:      return "InWorld";
    case WorldSessionState::kFailed:       return "Failed";
  }
  return "Unknown";
}

bool WorldSession::IsInWorld() const {
  return state_ == WorldSessionState::kInWorld;
}

bool WorldSession::IsConnected() const {
  return state_ != WorldSessionState::kDisconnected &&
         state_ != WorldSessionState::kFailed;
}

void WorldSession::SetCharacterName(const std::string& name) {
  character_name_ = name;
}

const std::string& WorldSession::GetCharacterName() const {
  return character_name_;
}

void WorldSession::SetRealmName(const std::string& name) {
  realm_name_ = name;
}

const std::string& WorldSession::GetRealmName() const {
  return realm_name_;
}

void WorldSession::SetMapId(std::uint32_t mapId) {
  map_id_ = mapId;
}

std::uint32_t WorldSession::GetMapId() const {
  return map_id_;
}

void WorldSession::SetLatency(std::uint32_t ms) {
  latency_ms_ = ms;
}

std::uint32_t WorldSession::GetLatency() const {
  return latency_ms_;
}

void WorldSession::AddPacketsSent(std::uint32_t count) {
  packets_sent_ += count;
}

void WorldSession::AddPacketsReceived(std::uint32_t count) {
  packets_received_ += count;
}

std::uint64_t WorldSession::GetPacketsSent() const {
  return packets_sent_;
}

std::uint64_t WorldSession::GetPacketsReceived() const {
  return packets_received_;
}

void WorldSession::SetErrorMessage(const std::string& msg) {
  error_message_ = msg;
}

const std::string& WorldSession::GetErrorMessage() const {
  return error_message_;
}

bool WorldSession::HasError() const {
  return !error_message_.empty();
}

float WorldSession::GetSessionDuration() const {
  return session_duration_;
}

void WorldSession::Update(float dt) {
  if (IsConnected()) {
    session_duration_ += dt;
  }
}

void WorldSession::Reset() {
  state_ = WorldSessionState::kDisconnected;
  character_name_.clear();
  realm_name_.clear();
  map_id_ = 0;
  latency_ms_ = 0;
  packets_sent_ = 0;
  packets_received_ = 0;
  error_message_.clear();
  session_duration_ = 0.0f;
}

}

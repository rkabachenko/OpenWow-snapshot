#pragma once

#include <cstdint>
#include <string>

#include "openwow/game/object_guid.h"

namespace openwow::core {

enum class SessionState : std::uint8_t {
  Disconnected,
  Connecting,
  Authenticating,
  CharacterSelect,
  EnteringWorld,
  InWorld,
  Loading,
  Cinematic,
};

class SessionManager {
 public:
  SessionManager() = default;

  void SetState(SessionState state);
  [[nodiscard]] SessionState GetState() const { return state_; }
  [[nodiscard]] std::string GetStateName() const;
  [[nodiscard]] bool IsInWorld() const { return state_ == SessionState::InWorld; }
  [[nodiscard]] bool IsConnected() const;
  [[nodiscard]] bool IsLoading() const { return state_ == SessionState::Loading; }

  void SetAccountName(const std::string& name);
  [[nodiscard]] const std::string& GetAccountName() const;
  void SetRealmName(const std::string& name);
  [[nodiscard]] const std::string& GetRealmName() const;
  void SetCharacterName(const std::string& name);
  [[nodiscard]] const std::string& GetCharacterName() const;
  void SetLocalPlayerGuid(openwow::game::ObjectGuid guid);
  [[nodiscard]] openwow::game::ObjectGuid GetLocalPlayerGuid() const;

  [[nodiscard]] float GetSessionDuration() const;
  [[nodiscard]] float GetInWorldDuration() const;

  [[nodiscard]] std::string GetFormattedSessionDuration() const;

  [[nodiscard]] std::string GetFormattedInWorldDuration() const;

  [[nodiscard]] float GetDownloadBandwidth() const;

  [[nodiscard]] float GetUploadBandwidth() const;

  [[nodiscard]] std::uint32_t GetDisconnectCount() const;
  void IncrementDisconnects();
  void SetLastDisconnectReason(const std::string& reason);
  [[nodiscard]] const std::string& GetLastDisconnectReason() const;
  [[nodiscard]] bool ShouldReconnect() const;
  void SetShouldReconnect(bool v);

  [[nodiscard]] float GetFPS() const;
  void SetFPS(float fps);
  [[nodiscard]] std::uint32_t GetLatency() const;
  void SetLatency(std::uint32_t ms);

  [[nodiscard]] std::uint64_t GetDownloadBytes() const;
  void AddDownloadBytes(std::uint64_t bytes);
  [[nodiscard]] std::uint64_t GetUploadBytes() const;
  void AddUploadBytes(std::uint64_t bytes);

  void Update(float dt);
  void Reset();

 private:
  SessionState state_ = SessionState::Disconnected;

  std::string account_name_;
  std::string realm_name_;
  std::string character_name_;
  openwow::game::ObjectGuid local_guid_;

  float session_duration_ = 0.0f;
  float in_world_duration_ = 0.0f;

  std::uint32_t disconnect_count_ = 0;
  std::string disconnect_reason_;
  bool should_reconnect_ = false;

  float fps_ = 0.0f;
  std::uint32_t latency_ms_ = 0;

  std::uint64_t download_bytes_ = 0;
  std::uint64_t upload_bytes_ = 0;
};

}

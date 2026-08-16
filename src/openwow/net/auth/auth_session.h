#pragma once

#include "openwow/auth/srp6.h"
#include "openwow/net/transport/tcp_client.h"
#include "openwow/net/wotlk/realm_list.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace openwow::net::auth {

enum class AuthResult : uint8_t {
  Success              = 0x00,
  FailBanned           = 0x03,
  FailUnknownAccount   = 0x04,
  FailIncorrectPassword = 0x05,
  FailAlreadyOnline    = 0x06,
  FailNoTime           = 0x07,
  FailDBBusy           = 0x08,
  FailVersionInvalid   = 0x09,
  FailVersionUpdate    = 0x0A,
  FailSuspended        = 0x0C,
  FailTrialEnded       = 0x0E,
  FailConnectLater     = 0x10,
};

enum class AuthState {
  Disconnected,
  Connecting,
  ChallengeSent,
  ProofSent,
  ReconnectChallengeSent,
  ReconnectProofSent,
  Authenticated,
  RealmListReceived,
  Failed,
};

using RealmInfo = openwow::net::wotlk::RealmInfo;

class AuthSession {
 public:
  AuthSession() = default;

  static AuthSession& Get();

  void Connect(const std::string& host, uint16_t port);

  void Authenticate(const std::string& username, const std::string& password);

  void RequestRealmList();

  void SendFileTransferResponse(bool transfer_needed,
                                std::uint64_t resume_offset);

  void Reconnect();

  [[nodiscard]] bool IsAuthServerConnected() const;

  void SelectRealm(uint8_t realmId);

  [[nodiscard]] AuthState GetState() const;
  [[nodiscard]] AuthResult GetLastError() const;
  [[nodiscard]] std::string GetUsername() const;
  [[nodiscard]] const std::vector<RealmInfo>& GetRealmList() const;
  [[nodiscard]] const RealmInfo* GetSelectedRealm() const;
  [[nodiscard]] std::array<uint8_t, 40> GetSessionKey() const;

  void ProcessPacket(const uint8_t* data, size_t size);

  void Reset();

 private:
  void HandleLogonChallenge(const uint8_t* data, size_t size);
  void HandleLogonProof(const uint8_t* data, size_t size);
  void HandleReconnectChallenge(const uint8_t* data, size_t size);
  void HandleReconnectProof(const uint8_t* data, size_t size);
  void HandleRealmList(const uint8_t* data, size_t size);

  [[nodiscard]] std::vector<uint8_t> BuildLogonChallenge() const;

  [[nodiscard]] static std::vector<uint8_t> BuildRealmListRequest();

  openwow::auth::SRP6Client srp_;
  TcpClient tcp_;

  std::string username_;
  std::string last_host_;

  std::uint16_t last_port_{0};
  AuthState state_      = AuthState::Disconnected;
  AuthResult last_error_ = AuthResult::Success;
  std::vector<RealmInfo> realms_;
  int selected_realm_   = -1;
  std::array<uint8_t, 40> session_key_{};

  std::array<uint8_t, 16> reconnect_challenge_{};

  mutable std::mutex mutex_;
};

}

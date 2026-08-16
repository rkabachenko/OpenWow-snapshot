
#include "openwow/net/auth/auth_session.h"

#include "openwow/net/auth/login_file_transfer_packet.h"
#include "openwow/net/auth/logon_challenge_packet.h"
#include "openwow/net/wotlk/protocol/auth_protocol.h"
#include "openwow/platform/diagnostics/crash_handler.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>

namespace openwow::net::auth {

AuthSession& AuthSession::Get() {
  static AuthSession instance;
  return instance;
}

void AuthSession::Connect(const std::string& host, uint16_t port) {
  std::lock_guard lock(mutex_);
  last_host_ = host;
  last_port_ = port;
  state_ = AuthState::Connecting;
  if (!tcp_.Connect(host, port)) {
    state_ = AuthState::Failed;
    last_error_ = AuthResult::FailConnectLater;
    return;
  }
  state_ = AuthState::Connecting;
}

void AuthSession::Authenticate(const std::string& username, const std::string& password) {
  {
    std::lock_guard lock(mutex_);

    username_ = username;
    for (auto& c : username_)
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    std::string srp_username = username_;
    const auto hash_pos = srp_username.find('#');
    if (hash_pos != std::string::npos)
      srp_username.resize(hash_pos);

    srp_.Initialize(srp_username, password);
  }

  auto pkt = BuildLogonChallenge();
  if (tcp_.IsConnected() && tcp_.Write(pkt)) {
    std::lock_guard lock(mutex_);
    state_ = AuthState::ChallengeSent;
  }
}

void AuthSession::RequestRealmList() {

  {
    std::lock_guard lock(mutex_);
    if (state_ != AuthState::Authenticated) {
      return;
    }
  }

  auto pkt = BuildRealmListRequest();
  if (tcp_.IsConnected()) {
    (void)tcp_.Write(pkt);
  }
}

void AuthSession::SendFileTransferResponse(const bool transfer_needed,
                                           const std::uint64_t resume_offset) {
  const auto packet =
      BuildLoginFileTransferResponsePacket(transfer_needed, resume_offset);
  if (tcp_.IsConnected()) {
    (void)tcp_.Write(packet);
  }
}

void AuthSession::Reconnect() {
  std::lock_guard lock(mutex_);
  if (last_host_.empty() || last_port_ == 0) {
    return;
  }
  state_ = AuthState::Connecting;
  if (!tcp_.Connect(last_host_, last_port_)) {
    state_ = AuthState::Failed;
    last_error_ = AuthResult::FailConnectLater;
  }
}

bool AuthSession::IsAuthServerConnected() const {
  return tcp_.IsConnected();
}

void AuthSession::SelectRealm(uint8_t realmId) {
  std::lock_guard lock(mutex_);
  for (size_t i = 0; i < realms_.size(); ++i) {
    if (realms_[i].id == static_cast<int>(realmId)) {
      selected_realm_ = static_cast<int>(i);
      openwow::platform::CrashHandler::Get().SetRealmInfo(
          realms_[i].name, RealmTypeToString(realms_[i].type));
      return;
    }
  }
  selected_realm_ = -1;
  openwow::platform::CrashHandler::Get().ClearRealmInfo();
}

AuthState AuthSession::GetState() const {
  std::lock_guard lock(mutex_);
  return state_;
}

AuthResult AuthSession::GetLastError() const {
  std::lock_guard lock(mutex_);
  return last_error_;
}

std::string AuthSession::GetUsername() const {
  std::lock_guard lock(mutex_);
  return username_;
}

const std::vector<RealmInfo>& AuthSession::GetRealmList() const {

  return realms_;
}

const RealmInfo* AuthSession::GetSelectedRealm() const {
  std::lock_guard lock(mutex_);
  if (selected_realm_ < 0 || selected_realm_ >= static_cast<int>(realms_.size()))
    return nullptr;
  return &realms_[static_cast<size_t>(selected_realm_)];
}

std::array<uint8_t, 40> AuthSession::GetSessionKey() const {
  std::lock_guard lock(mutex_);
  return session_key_;
}

void AuthSession::ProcessPacket(const uint8_t* data, size_t size) {
  if (size < 1) return;

  switch (data[0]) {
    case 0x00:
      HandleLogonChallenge(data, size);
      break;
    case 0x01:
      HandleLogonProof(data, size);
      break;
    case 0x02:
      HandleReconnectChallenge(data, size);
      break;
    case 0x03:
      HandleReconnectProof(data, size);
      break;
    case 0x10:
      HandleRealmList(data, size);
      break;
    default:
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                          "AuthSession: unknown opcode 0x" +
                              std::to_string(data[0]));
      break;
  }
}

void AuthSession::Reset() {
  std::lock_guard lock(mutex_);
  tcp_.Disconnect();
  srp_ = openwow::auth::SRP6Client{};
  username_.clear();
  last_host_.clear();
  last_port_ = 0;
  state_ = AuthState::Disconnected;
  last_error_ = AuthResult::Success;
  realms_.clear();
  selected_realm_ = -1;
  session_key_ = {};
  reconnect_challenge_ = {};
  openwow::platform::CrashHandler::Get().ClearRealmInfo();
}

void AuthSession::HandleLogonChallenge(const uint8_t* data, size_t size) {

  if (size < 3) return;

  if (data[2] != 0x00) {
    std::lock_guard lock(mutex_);
    state_ = AuthState::Failed;
    uint8_t result = data[2];

    if (result >= 0x21u) result = 0xFFu;
    last_error_ = static_cast<AuthResult>(result);
    return;
  }
  if (size < 119) {
    std::lock_guard lock(mutex_);
    state_ = AuthState::Failed;
    last_error_ = AuthResult::FailConnectLater;
    return;
  }

  size_t p = 3;
  const uint8_t* B_le  = data + p; p += 32;
  const uint8_t  g_len = data[p++];
  const uint8_t  g_val = data[p];
  p += g_len;
  const uint8_t  N_len = data[p++];
  const uint8_t* N_data = data + p;
  p += N_len;
  const uint8_t* s_le = data + p;

  bool ok = false;
  {
    std::lock_guard lock(mutex_);
    ok = srp_.ProcessChallenge(B_le, 32, g_val, N_data, N_len, s_le, 32);
    if (!ok) {
      state_ = AuthState::Failed;
      last_error_ = AuthResult::FailConnectLater;
      return;
    }
    session_key_ = srp_.GetSessionKey();
  }

  auto A  = srp_.GetPublicKey();
  auto M1 = srp_.GetClientProof();

  std::vector<uint8_t> proof;
  proof.reserve(75);
  proof.push_back(0x01);
  proof.insert(proof.end(), A.begin(), A.end());
  proof.insert(proof.end(), M1.begin(), M1.end());
  for (int i = 0; i < 20; ++i) proof.push_back(0x00);
  proof.push_back(0x00);
  proof.push_back(0x00);

  if (tcp_.IsConnected()) {
    tcp_.Write(proof);
  }

  std::lock_guard lock(mutex_);
  state_ = AuthState::ProofSent;
}

void AuthSession::HandleLogonProof(const uint8_t* data, size_t size) {

  if (size < 2) return;

  if (data[1] != 0x00) {
    std::lock_guard lock(mutex_);
    state_ = AuthState::Failed;
    last_error_ = static_cast<AuthResult>(data[1]);
    return;
  }

  if (size < 22 || !srp_.VerifyServerProof(data + 2, 20)) {
    std::lock_guard lock(mutex_);
    state_ = AuthState::Failed;
    last_error_ = AuthResult::FailConnectLater;
    return;
  }

  std::lock_guard lock(mutex_);
  state_ = AuthState::Authenticated;
  last_error_ = AuthResult::Success;
}

void AuthSession::HandleReconnectChallenge(const uint8_t* data, size_t size) {

  if (size < 2) return;

  const uint8_t result = data[1];
  if (result != 0x00) {
    std::lock_guard lock(mutex_);
    state_ = AuthState::Failed;
    uint8_t effective = result;
    if (effective >= 0x21) effective = 0xFF;
    last_error_ = static_cast<AuthResult>(effective);
    return;
  }

  if (size < 34) {
    std::lock_guard lock(mutex_);
    state_ = AuthState::Failed;
    last_error_ = AuthResult::FailConnectLater;
    return;
  }

  std::memcpy(reconnect_challenge_.data(), data + 2, 16);

  std::lock_guard lock(mutex_);
  state_ = AuthState::ReconnectChallengeSent;
  last_error_ = AuthResult::Success;
}

void AuthSession::HandleReconnectProof(const uint8_t* data, size_t size) {

  if (size < 2) return;

  const uint8_t result = data[1];

  if (result != 0x00) {
    std::lock_guard lock(mutex_);
    state_ = AuthState::Failed;
    uint8_t effective = result;
    if (effective >= 0x21) effective = 0xFF;
    last_error_ = static_cast<AuthResult>(effective);
    return;
  }

  if (size < 4) {
    return;
  }

  std::lock_guard lock(mutex_);
  state_ = AuthState::Authenticated;
  last_error_ = AuthResult::Success;
}

void AuthSession::HandleRealmList(const uint8_t* data, size_t size) {
  const std::vector<uint8_t> payload(data, data + size);
  auto parsed = wotlk::ParseRealmListBinaryDetailed(payload);

  std::lock_guard lock(mutex_);
  if (!parsed.ok) {
    realms_.clear();
    selected_realm_ = -1;
    state_ = AuthState::Failed;
    last_error_ = AuthResult::FailConnectLater;
    openwow::platform::CrashHandler::Get().ClearRealmInfo();
    return;
  }
  realms_ = std::move(parsed.realms);
  state_ = AuthState::RealmListReceived;
  last_error_ = AuthResult::Success;
}

std::vector<uint8_t> AuthSession::BuildLogonChallenge() const {
  return BuildRetailLogonChallengePacket(username_);
}

std::vector<uint8_t> AuthSession::BuildRealmListRequest() {
  return {0x10, 0x00, 0x00, 0x00, 0x00};
}

}

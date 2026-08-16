#pragma once

#include "openwow/net/wotlk/realm_list.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace openwow::net {
class TcpClient;
}

namespace openwow::net::wotlk {

enum class AuthStatus {
  kSuccess,
  kInvalidCredentials,
  kNetworkError,
  kAuthRejected,
  kProtocolError,
  kCancelled,
  kPatchRequired,
  kPatchTransferComplete,
};

struct AuthFileTransfer {
  std::string filename;
  std::array<std::uint8_t, 16> digest{};
  std::uint64_t expected_size{0};
};

struct LoginStatusInfo {
  std::uint32_t state_code{5};
  std::uint32_t result_code{11};
  std::string state_key;
  std::string result_key;
};

struct AuthResult {
  AuthStatus status{AuthStatus::kProtocolError};
  std::string message;
  std::string session_token;
  std::array<std::uint8_t, 40> session_key_raw{};
  LoginStatusInfo login_status;

  std::uint32_t account_flags{0};
  std::uint16_t login_flags{0};
  bool account_messages_available{false};

  std::optional<AuthFileTransfer> file_transfer;

  std::vector<RealmInfo> realms;
};

struct RealmListFetchResult {
  bool ok{false};
  std::string message;
  std::vector<RealmInfo> realms;
  AuthStatus status{AuthStatus::kProtocolError};
};

enum class RealmListVariant {
  kGrunt = 0,
  kBattleNet = 1,
};

struct ParsedRealmList {
  bool ok{false};
  std::vector<RealmInfo> realms;
};

class AuthProtocol {
public:
  AuthProtocol();
  ~AuthProtocol();

  AuthProtocol(const AuthProtocol &) = delete;
  AuthProtocol &operator=(const AuthProtocol &) = delete;

  bool ValidateCredentials(const std::string &username, const std::string &password) const;
  AuthResult Login(const std::string &host, std::uint16_t port, const std::string &username,
                   const std::string &password, std::uint32_t timeout_ms = 5000,
                   const std::function<bool()> &should_cancel = {});

  AuthResult Reconnect(std::uint32_t timeout_ms = 5000,
                       const std::function<bool()> &should_cancel = {});

  RealmListFetchResult RequestRealmList(std::uint32_t timeout_ms = 5000,
                                        const std::function<bool()> &should_cancel = {});

  [[nodiscard]] bool CanRequestRealmList() const;

  void CancelPendingIo();

  void CloseTransport();

  [[nodiscard]] bool SendFileTransferResponse(bool transfer_needed, std::uint64_t resume_offset);

  AuthResult
  ReceiveFileTransfer(const std::function<bool(std::span<const std::uint8_t>)> &receive_chunk,
                      std::uint32_t timeout_ms = 0,
                      const std::function<bool()> &should_cancel = {});

  void Disconnect();
  [[nodiscard]] bool IsAuthenticated() const;

private:
  std::vector<std::uint8_t> BuildLogonChallengePacket(const std::string &username) const;
  std::vector<std::uint8_t> BuildReconnectChallengePacket() const;
  std::vector<std::uint8_t> BuildRealmListRequestPacket() const;

  AuthResult ReconnectLocked(
      std::uint32_t timeout_ms,
      const std::function<bool()>& should_cancel);
  void ResetLocked();

  struct ReconnectContext {
    std::string host;
    std::uint16_t port{0};
    std::string uppercase_account;
    std::array<std::uint8_t, 40> session_key{};
    std::uint32_t account_flags{0};
    std::uint16_t login_flags{0};
    bool valid{false};

    void Clear();
  };

  std::unique_ptr<openwow::net::TcpClient> client_;
  std::atomic_bool authenticated_{false};
  std::atomic_bool file_transfer_accepted_{false};
  std::atomic_uint64_t file_transfer_expected_size_{0};
  std::atomic_uint64_t file_transfer_resume_offset_{0};
  mutable std::mutex operation_mutex_;
  ReconnectContext reconnect_;
};

[[nodiscard]] ParsedRealmList ParseRealmListBinaryDetailed(
    const std::vector<std::uint8_t>& data,
    RealmListVariant variant = RealmListVariant::kGrunt);

[[nodiscard]] std::vector<RealmInfo> ParseRealmListBinary(
    const std::vector<std::uint8_t>& data,
    RealmListVariant variant = RealmListVariant::kGrunt);

[[nodiscard]] std::string ResolveLoginStateKey(std::uint32_t state_code);
[[nodiscard]] std::string ResolveLoginResultKey(std::uint32_t result_code);

[[nodiscard]] LoginStatusInfo ResolveLoginStatusInfo(
    std::uint8_t server_result_code,
    bool security_flag = false,
    std::uint8_t security_failures = 0);

}

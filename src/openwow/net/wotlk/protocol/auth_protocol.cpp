
#include "openwow/net/wotlk/protocol/auth_protocol.h"

#include "openwow/auth/srp6.h"
#include "openwow/net/auth/logon_challenge_packet.h"
#include "openwow/net/auth/login_file_transfer_packet.h"
#include "openwow/net/auth/login_matrix_challenge.h"
#include "openwow/net/auth/login_pin_challenge.h"
#include "openwow/net/auth/login_token_challenge.h"
#include "openwow/net/transport/tcp_client.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <openssl/evp.h>
#include <openssl/rand.h>

namespace openwow::net::wotlk {

namespace {

constexpr std::uint8_t kPinSecurityFlag = 0x01u;
constexpr std::uint8_t kMatrixSecurityFlag = 0x02u;
constexpr std::uint8_t kTokenSecurityFlag = 0x04u;
constexpr std::uint8_t kKnownSecurityFlags =
    kPinSecurityFlag | kMatrixSecurityFlag | kTokenSecurityFlag;

constexpr std::array<const char*, 18> kLoginStateKeys = {
    "LOGIN_STATE_INITIALIZED",
    "LOGIN_STATE_CONNECTING",
    "LOGIN_STATE_HANDSHAKING",
    "LOGIN_STATE_AUTHENTICATING",
    "LOGIN_STATE_AUTHENTICATED",
    "LOGIN_STATE_FAILED",
    "LOGIN_STATE_DOWNLOADFILE",
    "LOGIN_STATE_FIRST_SECURITY",
    "LOGIN_STATE_PIN",
    "LOGIN_STATE_PIN_WAIT",
    "LOGIN_STATE_MATRIX",
    "LOGIN_STATE_MATRIX_WAIT",
    "LOGIN_STATE_TOKEN",
    "LOGIN_STATE_TOKEN_WAIT",
    "LOGIN_STATE_CHECKINGVERSIONS",
    "RESPONSE_CONNECTED",
    "LOGIN_STATE_DISCONNECTED",
    "LOGIN_STATE_SURVEY",
};

constexpr std::array<const char*, 41> kLoginResultKeys = {
    "LOGIN_OK",
    "LOGIN_INVALID_CHALLENGE_MESSAGE",
    "LOGIN_SRP_ERROR",
    "LOGIN_INVALID_PROOF_MESSAGE",
    "LOGIN_BAD_SERVER_PROOF",
    "LOGIN_INVALID_RECODE_MESSAGE",
    "LOGIN_BAD_SERVER_RECODE_PROOF",
    "LOGIN_UNKNOWN_ACCOUNT",
    "LOGIN_UNKNOWN_ACCOUNT_PIN",
    "LOGIN_UNKNOWN_ACCOUNT_CALL",
    "LOGIN_INCORRECT_PASSWORD",
    "LOGIN_FAILED",
    "LOGIN_SERVER_DOWN",
    "LOGIN_BANNED",
    "LOGIN_BADVERSION",
    "LOGIN_ALREADYONLINE",
    "LOGIN_NOTIME",
    "LOGIN_DBBUSY",
    "LOGIN_SUSPENDED",
    "LOGIN_PARENTALCONTROL",
    "LOGIN_LOCKED_ENFORCED",
    "DISCONNECTED",
    "LOGIN_ACCOUNT_CONVERTED",
    "LOGIN_ANTI_INDULGENCE",
    "LOGIN_EXPIRED",
    "LOGIN_TRIAL_EXPIRED",
    "LOGIN_NO_GAME_ACCOUNT",
    "LOGIN_AUTH_OUTAGE",
    "LOGIN_GAME_ACCOUNT_LOCKED",
    "LOGIN_NO_BATTLENET_MANAGER",
    "LOGIN_NO_BATTLENET_APPLICATION",
    "LOGIN_MALFORMED_ACCOUNT_NAME",
    "LOGIN_USE_GRUNT",
    "LOGIN_TOO_FAST",
    "LOGIN_CHARGEBACK",
    "LOGIN_IGR_WITHOUT_BNET",
    "LOGIN_UNLOCKABLE_LOCK",
    "LOGIN_CONVERSION_REQUIRED",
    "LOGIN_UNABLE_TO_DOWNLOAD_MODULE",
    "LOGIN_NO_GAME_ACCOUNTS_IN_REGION",
    "LOGIN_ACCOUNT_LOCKED",
};

LoginStatusInfo MakeLoginStatusInfo(const std::uint32_t state_code,
                                    const std::uint32_t result_code) {
  return {
      .state_code = state_code,
      .result_code = result_code,
      .state_key = ResolveLoginStateKey(state_code),
      .result_key = ResolveLoginResultKey(result_code),
  };
}

[[nodiscard]] std::string FormatUnknownLoginKey(
    const std::string_view prefix,
    const std::uint32_t code) {
  return std::string(prefix) + std::to_string(static_cast<std::int32_t>(code));
}

void SanitizeRealmName(std::string& name) {
  static constexpr std::string_view kReplaceWithSpace = "\"*/:<>?\\|";
  for (char& ch : name) {
    if (kReplaceWithSpace.find(ch) != std::string_view::npos) {
      ch = ' ';
    }
  }

  static constexpr std::string_view kTrimTail = " \"*./:<>?\\|";
  while (!name.empty() && kTrimTail.find(name.back()) != std::string_view::npos) {
    name.pop_back();
  }
}

[[nodiscard]] std::string ToUpperAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](const unsigned char character) {
                   return static_cast<char>(std::toupper(character));
                 });
  return value;
}

}

namespace {

class RealmListPacketReader {
 public:
  explicit RealmListPacketReader(const std::vector<std::uint8_t>& data) : data_(data) {}

  bool ReadU8(std::uint8_t& value) {
    if (position_ + 1 > data_.size()) {
      return false;
    }
    value = data_[position_++];
    return true;
  }

  bool ReadU16(std::uint16_t& value) {
    if (position_ + 2 > data_.size()) {
      return false;
    }
    value = static_cast<std::uint16_t>(data_[position_]
        | (static_cast<std::uint16_t>(data_[position_ + 1]) << 8));
    position_ += 2;
    return true;
  }

  bool ReadU32(std::uint32_t& value) {
    if (position_ + 4 > data_.size()) {
      return false;
    }
    value = static_cast<std::uint32_t>(data_[position_])
        | (static_cast<std::uint32_t>(data_[position_ + 1]) << 8)
        | (static_cast<std::uint32_t>(data_[position_ + 2]) << 16)
        | (static_cast<std::uint32_t>(data_[position_ + 3]) << 24);
    position_ += 4;
    return true;
  }

  bool ReadFloat(float& value) {
    if (position_ + 4 > data_.size()) {
      return false;
    }
    std::memcpy(&value, data_.data() + position_, sizeof(value));
    position_ += sizeof(value);
    return true;
  }

  bool ReadCString(std::string& value) {
    value.clear();
    while (position_ < data_.size()) {
      const auto byte = data_[position_++];
      if (byte == 0) {
        return true;
      }
      value.push_back(static_cast<char>(byte));
    }
    value.clear();
    return false;
  }

  [[nodiscard]] bool AtEnd() const {
    return position_ == data_.size();
  }

 private:
  const std::vector<std::uint8_t>& data_;
  std::size_t position_{0};
};

void ApplyPopulationSentinel(RealmInfo& realm, const float raw_population) {
  if (raw_population == 600.0F) {
    realm.population = 0.0F;
    realm.is_new = true;
    return;
  }
  if (raw_population == 200.0F) {
    realm.population = 0.001F;
    realm.is_recommended = true;
    return;
  }
  if (raw_population == 400.0F) {
    realm.population = 8.0F;
    realm.is_full = true;
    return;
  }
  realm.population = raw_population;
}

}

ParsedRealmList ParseRealmListBinaryDetailed(const std::vector<std::uint8_t>& data,
                                            const RealmListVariant variant) {
  ParsedRealmList parsed;
  RealmListPacketReader reader(data);

  std::uint8_t opcode = 0;
  std::uint16_t payload_size = 0;
  std::uint32_t unused_header = 0;
  std::uint16_t count = 0;
  if (!reader.ReadU8(opcode) || opcode != 0x10 || !reader.ReadU16(payload_size)
      || !reader.ReadU32(unused_header) || !reader.ReadU16(count)) {
    return parsed;
  }
  if (payload_size < 8u || data.size() != static_cast<std::size_t>(payload_size) + 3u) {
    return parsed;
  }
  (void)unused_header;

  parsed.realms.reserve(count);
  for (std::uint16_t i = 0; i < count; ++i) {
    RealmInfo realm;
    realm.id = static_cast<int>(i) + 1;
    realm.cache_index = i;

    std::uint8_t type = 0;
    std::uint8_t locked = 0;
    std::uint8_t flags = 0;
    float raw_population = 0.0F;
    if (!reader.ReadU8(type) || !reader.ReadU8(locked) || !reader.ReadU8(flags)
        || !reader.ReadCString(realm.name) || !reader.ReadCString(realm.address)
        || !reader.ReadFloat(raw_population)) {
      parsed.realms.clear();
      return parsed;
    }

    realm.type = static_cast<RealmType>(type);
    realm.locked = (locked != 0);
    realm.is_offline = ((flags & 0x01u) != 0);
    realm.is_pvp_flag = ((flags & 0x02u) != 0);
    realm.has_version_data = ((flags & 0x04u) != 0);
    SanitizeRealmName(realm.name);
    ApplyPopulationSentinel(realm, raw_population);

    std::uint8_t num_characters = 0;
    std::uint8_t timezone = 0;
    if (!reader.ReadU8(num_characters) || !reader.ReadU8(timezone)) {
      parsed.realms.clear();
      return parsed;
    }
    realm.num_characters = static_cast<int>(num_characters);
    realm.timezone = timezone;

    if (variant == RealmListVariant::kBattleNet) {
      for (auto& tail_word : realm.tail_words) {
        if (!reader.ReadU32(tail_word)) {
          parsed.realms.clear();
          return parsed;
        }
      }
    } else {
      std::uint8_t grunt_tail_byte = 0;
      if (!reader.ReadU8(grunt_tail_byte)) {
        parsed.realms.clear();
        return parsed;
      }
      realm.tail_words[2] = grunt_tail_byte;
    }

    if (realm.has_version_data) {
      if (!reader.ReadU8(realm.version_major) || !reader.ReadU8(realm.version_minor)
          || !reader.ReadU8(realm.version_revision) || !reader.ReadU16(realm.version_build)) {
        parsed.realms.clear();
        return parsed;
      }
    }

    parsed.realms.push_back(std::move(realm));
  }
  std::uint16_t footer = 0;
  if (!reader.ReadU16(footer) || !reader.AtEnd()) {
    parsed.realms.clear();
    return parsed;
  }
  (void)footer;

  parsed.ok = true;
  return parsed;
}

std::vector<RealmInfo> ParseRealmListBinary(const std::vector<std::uint8_t>& data,
                                            const RealmListVariant variant) {
  auto parsed = ParseRealmListBinaryDetailed(data, variant);
  return parsed.ok ? std::move(parsed.realms) : std::vector<RealmInfo>{};
}

std::string ResolveLoginStateKey(const std::uint32_t state_code) {
  if (state_code < kLoginStateKeys.size()) {
    return kLoginStateKeys[state_code];
  }
  return FormatUnknownLoginKey("LOGIN_STATE_UNKNOWN_", state_code);
}

std::string ResolveLoginResultKey(const std::uint32_t result_code) {
  if (result_code < kLoginResultKeys.size()) {
    return kLoginResultKeys[result_code];
  }
  return FormatUnknownLoginKey("LOGIN_UNKNOWN_", result_code);
}

LoginStatusInfo ResolveLoginStatusInfo(const std::uint8_t server_result_code,
                                       const bool security_flag,
                                       const std::uint8_t security_failures) {
  switch (server_result_code) {
    case 0:
      return MakeLoginStatusInfo(4, 0);
    case 3:
      return MakeLoginStatusInfo(5, 13);
    case 4:
    case 5:
      if (security_flag) {
        const auto security_result =
            (static_cast<std::uint8_t>(security_failures + 1) < 3u) ? 8u : 9u;
        return MakeLoginStatusInfo(5, security_result);
      }
      return MakeLoginStatusInfo(5, 7);
    case 6:
      return MakeLoginStatusInfo(5, 15);
    case 7:
      return MakeLoginStatusInfo(5, 16);
    case 8:
      return MakeLoginStatusInfo(5, 17);
    case 9:
      return MakeLoginStatusInfo(5, 14);
    case 10:
      return MakeLoginStatusInfo(6, 0);
    case 12:
      return MakeLoginStatusInfo(5, 18);
    case 14:
      return MakeLoginStatusInfo(17, 0);
    case 15:
      return MakeLoginStatusInfo(5, 19);
    case 16:
      return MakeLoginStatusInfo(5, 20);
    case 17:
      return MakeLoginStatusInfo(5, 25);
    case 18:
      return MakeLoginStatusInfo(5, 22);
    case 22:
      return MakeLoginStatusInfo(5, 34);
    case 23:
      return MakeLoginStatusInfo(5, 35);
    case 24:
      return MakeLoginStatusInfo(5, 28);
    case 25:
      return MakeLoginStatusInfo(5, 36);
    case 32:
      return MakeLoginStatusInfo(5, 37);
    case 255:
      return MakeLoginStatusInfo(5, 21);
    default:
      return MakeLoginStatusInfo(5, 11);
  }
}

namespace {

std::string KToHex(const std::array<std::uint8_t, 40>& K) {
  static const char kHex[] = "0123456789abcdef";
  std::string s;
  s.reserve(80);
  for (const auto b : K) {
    s += kHex[(b >> 4) & 0xF];
    s += kHex[b & 0xF];
  }
  return s;
}

[[nodiscard]] bool CancellationRequested(
    const std::function<bool()>& should_cancel) {
  return should_cancel && should_cancel();
}

[[nodiscard]] AuthResult CancelledAuthResult(std::string message) {
  return {
      .status = AuthStatus::kCancelled,
      .message = std::move(message),
  };
}

struct BytePart {
  const void* data{nullptr};
  std::size_t size{0};
};

[[nodiscard]] std::optional<std::array<std::uint8_t, 20>> Sha1(
    const std::initializer_list<BytePart> parts) {
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(
      EVP_MD_CTX_new(), &EVP_MD_CTX_free);
  if (!context || EVP_DigestInit_ex(context.get(), EVP_sha1(), nullptr) != 1) {
    return std::nullopt;
  }
  for (const auto& part : parts) {
    if (part.size != 0 &&
        EVP_DigestUpdate(context.get(), part.data, part.size) != 1) {
      return std::nullopt;
    }
  }
  std::array<std::uint8_t, 20> digest{};
  unsigned int digest_size = 0;
  if (EVP_DigestFinal_ex(context.get(), digest.data(), &digest_size) != 1 ||
      digest_size != digest.size()) {
    return std::nullopt;
  }
  return digest;
}

class OperationDeadline {
 public:
  explicit OperationDeadline(const std::uint32_t timeout_ms)
      : expires_at_(Clock::now() + std::chrono::milliseconds(timeout_ms)) {}

  [[nodiscard]] std::uint32_t RemainingMilliseconds() const {
    const auto now = Clock::now();
    if (now >= expires_at_) {
      return 0;
    }
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(expires_at_ - now).count() + 1;
    return static_cast<std::uint32_t>(std::min<std::int64_t>(
        remaining, std::numeric_limits<std::uint32_t>::max()));
  }

 private:
  using Clock = std::chrono::steady_clock;
  Clock::time_point expires_at_;
};

std::vector<std::uint8_t> ReadExact(
    openwow::net::TcpClient& client,
    const std::size_t size,
    const OperationDeadline& deadline,
    const std::function<bool()>& should_cancel) {
  if (should_cancel && should_cancel()) {
    return {};
  }
  const auto remaining_ms = deadline.RemainingMilliseconds();
  if (remaining_ms == 0) {
    return {};
  }
  return client.ReadExact(size, remaining_ms, should_cancel);
}

template <typename Packet>
struct WireReadResult {
  std::optional<Packet> packet;
  AuthResult failure;
};

template <typename Packet>
WireReadResult<Packet> WireFailure(const AuthStatus status,
                                   std::string message,
                                   LoginStatusInfo login_status = {}) {
  return {
      .packet = std::nullopt,
      .failure = {
          .status = status,
          .message = std::move(message),
          .login_status = std::move(login_status),
      },
  };
}

struct AuthChallengePacket {
  struct PinChallenge {
    std::uint32_t shuffle_seed{0};
    std::array<std::uint8_t, 16> server_salt{};
  };

  struct MatrixChallenge {
    std::uint8_t columns{0};
    std::uint8_t rows{0};
    std::uint8_t digits_per_entry{0};
    std::uint8_t entry_count{0};
    std::uint64_t selection_seed{0};
  };

  std::array<std::uint8_t, 32> server_public_key{};
  std::uint8_t generator{0};
  std::vector<std::uint8_t> modulus;
  std::array<std::uint8_t, 32> salt{};
  std::uint8_t security_flags{0};
  std::optional<PinChallenge> pin_challenge;
  std::optional<MatrixChallenge> matrix_challenge;
  bool token_challenge{false};
};

WireReadResult<AuthChallengePacket> ReadAuthChallenge(
    openwow::net::TcpClient& client,
    const std::uint32_t timeout_ms,
    const std::function<bool()>& should_cancel) {
  const OperationDeadline deadline(timeout_ms);
  const auto header = ReadExact(client, 3, deadline, should_cancel);
  if (header.empty()) {
    return WireFailure<AuthChallengePacket>(
        AuthStatus::kNetworkError, "No auth challenge response received.");
  }
  if (header[0] != 0x00 || header[1] != 0x00) {
    return WireFailure<AuthChallengePacket>(
        AuthStatus::kProtocolError, "Invalid auth challenge response header.");
  }
  if (header[2] != 0x00) {
    const auto login_status = ResolveLoginStatusInfo(header[2]);
    return WireFailure<AuthChallengePacket>(AuthStatus::kAuthRejected,
                                            login_status.result_key,
                                            login_status);
  }

  AuthChallengePacket packet;
  const auto public_key_and_generator_size =
      ReadExact(client, packet.server_public_key.size() + 1u, deadline, should_cancel);
  if (public_key_and_generator_size.empty()) {
    return WireFailure<AuthChallengePacket>(
        AuthStatus::kNetworkError, "Auth challenge ended before the SRP public key.");
  }
  std::copy_n(public_key_and_generator_size.begin(), packet.server_public_key.size(),
              packet.server_public_key.begin());
  const auto generator_size = public_key_and_generator_size.back();
  if (generator_size == 0 || generator_size > 32) {
    return WireFailure<AuthChallengePacket>(
        AuthStatus::kProtocolError, "Invalid SRP generator length.");
  }

  const auto generator = ReadExact(client, generator_size, deadline, should_cancel);
  const auto modulus_size_bytes = ReadExact(client, 1, deadline, should_cancel);
  if (generator.empty() || modulus_size_bytes.empty()) {
    return WireFailure<AuthChallengePacket>(
        AuthStatus::kNetworkError, "Auth challenge ended in the SRP parameters.");
  }
  if (generator_size != 1) {
    return WireFailure<AuthChallengePacket>(
        AuthStatus::kProtocolError, "Multi-byte SRP generators are unsupported.");
  }
  packet.generator = generator.front();

  const auto modulus_size = modulus_size_bytes.front();
  if (modulus_size == 0 || modulus_size > 32) {
    return WireFailure<AuthChallengePacket>(
        AuthStatus::kProtocolError, "Invalid SRP modulus length.");
  }
  packet.modulus = ReadExact(client, modulus_size, deadline, should_cancel);
  const auto salt_and_security =
      ReadExact(client, packet.salt.size() + 16u + 1u, deadline, should_cancel);
  if (packet.modulus.empty() || salt_and_security.empty()) {
    return WireFailure<AuthChallengePacket>(
        AuthStatus::kNetworkError, "Auth challenge ended before the SRP salt.");
  }
  std::copy_n(salt_and_security.begin(), packet.salt.size(), packet.salt.begin());

  const std::uint8_t security_flags = salt_and_security.back();
  std::size_t security_payload_size = 0;
  if ((security_flags & kPinSecurityFlag) != 0) security_payload_size += 20;
  if ((security_flags & kMatrixSecurityFlag) != 0) security_payload_size += 12;
  if ((security_flags & kTokenSecurityFlag) != 0) security_payload_size += 1;
  if ((security_flags & ~kKnownSecurityFlags) != 0) {
    return WireFailure<AuthChallengePacket>(
        AuthStatus::kProtocolError, "Unknown account-security challenge flags.");
  }
  const auto security_payload =
      ReadExact(client, security_payload_size, deadline, should_cancel);
  if (security_payload_size != 0 && security_payload.empty()) {
    return WireFailure<AuthChallengePacket>(
        AuthStatus::kNetworkError, "Account-security challenge payload was incomplete.");
  }
  packet.security_flags = security_flags;
  std::size_t security_offset = 0;
  if ((security_flags & kPinSecurityFlag) != 0) {
    AuthChallengePacket::PinChallenge pin;
    for (std::size_t byte_index = 0; byte_index < 4; ++byte_index) {
      pin.shuffle_seed |= static_cast<std::uint32_t>(
          security_payload[security_offset++]) << (byte_index * 8u);
    }
    std::copy_n(security_payload.data() + security_offset,
                pin.server_salt.size(), pin.server_salt.begin());
    security_offset += pin.server_salt.size();
    packet.pin_challenge = pin;
  }
  if ((security_flags & kMatrixSecurityFlag) != 0) {
    AuthChallengePacket::MatrixChallenge matrix;
    matrix.columns = security_payload[security_offset++];
    matrix.rows = security_payload[security_offset++];
    matrix.digits_per_entry = security_payload[security_offset++];
    matrix.entry_count = security_payload[security_offset++];
    for (std::size_t byte_index = 0; byte_index < 8; ++byte_index) {
      matrix.selection_seed |= static_cast<std::uint64_t>(
          security_payload[security_offset++]) << (byte_index * 8u);
    }
    packet.matrix_challenge = matrix;
  }

  if ((security_flags & kTokenSecurityFlag) != 0) {

    ++security_offset;
    packet.token_challenge = true;
  }

  return {.packet = std::move(packet)};
}

struct AuthProofPacket {
  std::array<std::uint8_t, 20> server_proof{};
  std::uint32_t account_flags{0};
  std::uint32_t survey_id{0};
  std::uint16_t login_flags{0};
};

WireReadResult<AuthFileTransfer>
ReadFileTransferInitiate(openwow::net::TcpClient &client, const std::uint32_t timeout_ms,
                         const std::function<bool()> &should_cancel) {
  const OperationDeadline deadline(timeout_ms);
  const auto header = ReadExact(client, 2, deadline, should_cancel);
  if (header.empty()) {
    return WireFailure<AuthFileTransfer>(AuthStatus::kNetworkError,
                                         "Patch transfer initiation was incomplete.");
  }
  if (header[0] != 0x30u || header[1] == 0u) {
    return WireFailure<AuthFileTransfer>(AuthStatus::kProtocolError,
                                         "Invalid patch transfer initiation header.");
  }

  const auto filename_bytes = ReadExact(client, header[1], deadline, should_cancel);
  const auto size_and_digest = ReadExact(client, 24, deadline, should_cancel);
  if (filename_bytes.empty() || size_and_digest.empty()) {
    return WireFailure<AuthFileTransfer>(AuthStatus::kNetworkError,
                                         "Patch transfer initiation payload was incomplete.");
  }

  AuthFileTransfer transfer;
  transfer.filename.assign(filename_bytes.begin(), filename_bytes.end());
  for (std::size_t byte_index = 0; byte_index < 8; ++byte_index) {
    transfer.expected_size |= static_cast<std::uint64_t>(size_and_digest[byte_index])
                              << (byte_index * 8u);
  }
  std::copy_n(size_and_digest.begin() + 8, transfer.digest.size(), transfer.digest.begin());
  return {.packet = std::move(transfer)};
}

WireReadResult<AuthProofPacket> ReadAuthProof(openwow::net::TcpClient &client,
                                              const std::uint32_t timeout_ms,
                                              const std::function<bool()> &should_cancel) {
  const OperationDeadline deadline(timeout_ms);
  const auto header = ReadExact(client, 2, deadline, should_cancel);
  if (header.empty()) {
    return WireFailure<AuthProofPacket>(AuthStatus::kNetworkError,
                                        "No auth proof response received.");
  }
  if (header[0] != 0x01) {
    return WireFailure<AuthProofPacket>(AuthStatus::kProtocolError,
                                        "Invalid auth proof response header.");
  }
  if (header[1] != 0x00) {
    if (header[1] == 0x04 && ReadExact(client, 2, deadline, should_cancel).empty()) {
      return WireFailure<AuthProofPacket>(AuthStatus::kNetworkError,
                                          "Auth rejection response was incomplete.");
    }
    const auto login_status = ResolveLoginStatusInfo(header[1]);
    return WireFailure<AuthProofPacket>(header[1] == 0x0au ? AuthStatus::kPatchRequired
                                                           : AuthStatus::kAuthRejected,
                                        login_status.result_key, login_status);
  }

  const auto body = ReadExact(client, 30, deadline, should_cancel);
  if (body.empty()) {
    return WireFailure<AuthProofPacket>(AuthStatus::kNetworkError,
                                        "Auth proof response was incomplete.");
  }

  AuthProofPacket packet;
  std::copy_n(body.begin(), packet.server_proof.size(), packet.server_proof.begin());
  packet.account_flags =
      static_cast<std::uint32_t>(body[20]) | (static_cast<std::uint32_t>(body[21]) << 8U) |
      (static_cast<std::uint32_t>(body[22]) << 16U) | (static_cast<std::uint32_t>(body[23]) << 24U);
  packet.survey_id =
      static_cast<std::uint32_t>(body[24]) | (static_cast<std::uint32_t>(body[25]) << 8U) |
      (static_cast<std::uint32_t>(body[26]) << 16U) | (static_cast<std::uint32_t>(body[27]) << 24U);
  packet.login_flags = static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(body[28])
      | (static_cast<std::uint16_t>(body[29]) << 8));
  return {.packet = std::move(packet)};
}

struct ReconnectChallengePacket {
  std::array<std::uint8_t, 16> server_challenge{};
};

WireReadResult<ReconnectChallengePacket> ReadReconnectChallenge(
    openwow::net::TcpClient& client,
    const std::uint32_t timeout_ms,
    const std::function<bool()>& should_cancel) {
  const OperationDeadline deadline(timeout_ms);
  const auto header = ReadExact(client, 2, deadline, should_cancel);
  if (header.empty()) {
    return WireFailure<ReconnectChallengePacket>(
        CancellationRequested(should_cancel) ? AuthStatus::kCancelled
                                             : AuthStatus::kNetworkError,
        CancellationRequested(should_cancel)
            ? "Auth reconnect cancelled."
            : "No auth reconnect challenge received.");
  }
  if (header[0] != 0x02) {
    return WireFailure<ReconnectChallengePacket>(
        AuthStatus::kProtocolError,
        "Invalid auth reconnect challenge header.");
  }
  if (header[1] != 0) {
    const auto login_status = ResolveLoginStatusInfo(header[1]);
    return WireFailure<ReconnectChallengePacket>(
        AuthStatus::kAuthRejected, login_status.result_key, login_status);
  }

  const auto body = ReadExact(client, 32, deadline, should_cancel);
  if (body.empty()) {
    return WireFailure<ReconnectChallengePacket>(
        CancellationRequested(should_cancel) ? AuthStatus::kCancelled
                                             : AuthStatus::kNetworkError,
        CancellationRequested(should_cancel)
            ? "Auth reconnect cancelled."
            : "Auth reconnect challenge was incomplete.");
  }

  ReconnectChallengePacket packet;
  std::copy_n(body.begin(), packet.server_challenge.size(),
              packet.server_challenge.begin());
  return {.packet = packet};
}

WireReadResult<std::uint16_t> ReadReconnectProof(
    openwow::net::TcpClient& client,
    const std::uint32_t timeout_ms,
    const std::function<bool()>& should_cancel) {
  const OperationDeadline deadline(timeout_ms);
  const auto header = ReadExact(client, 2, deadline, should_cancel);
  if (header.empty()) {
    return WireFailure<std::uint16_t>(
        CancellationRequested(should_cancel) ? AuthStatus::kCancelled
                                             : AuthStatus::kNetworkError,
        CancellationRequested(should_cancel)
            ? "Auth reconnect cancelled."
            : "No auth reconnect proof response received.");
  }
  if (header[0] != 0x03) {
    return WireFailure<std::uint16_t>(
        AuthStatus::kProtocolError,
        "Invalid auth reconnect proof response header.");
  }
  if (header[1] != 0) {

    const std::uint8_t effective_result =
        header[1] < 0x21u ? header[1] : static_cast<std::uint8_t>(0xFFu);
    const auto login_status = ResolveLoginStatusInfo(effective_result);
    return WireFailure<std::uint16_t>(
        AuthStatus::kAuthRejected, login_status.result_key, login_status);
  }
  const auto flags = ReadExact(client, 2, deadline, should_cancel);
  if (flags.empty()) {
    return WireFailure<std::uint16_t>(
        CancellationRequested(should_cancel) ? AuthStatus::kCancelled
                                             : AuthStatus::kNetworkError,
        CancellationRequested(should_cancel)
            ? "Auth reconnect cancelled."
            : "Auth reconnect proof response was incomplete.");
  }
  const auto login_flags = static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(flags[0]) |
      (static_cast<std::uint16_t>(flags[1]) << 8U));
  return {.packet = login_flags};
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>> BuildReconnectProof(
    const std::string& uppercase_account,
    const std::array<std::uint8_t, 40>& session_key,
    const std::array<std::uint8_t, 16>& server_challenge) {
  std::array<std::uint8_t, 16> client_salt{};
  if (RAND_bytes(client_salt.data(),
                 static_cast<int>(client_salt.size())) != 1) {
    return std::nullopt;
  }
  const std::array<std::uint8_t, 20> zero_checksum{};
  const auto proof = Sha1({
      {uppercase_account.data(), uppercase_account.size()},
      {client_salt.data(), client_salt.size()},
      {server_challenge.data(), server_challenge.size()},
      {session_key.data(), session_key.size()},
  });
  const auto checksum = Sha1({
      {client_salt.data(), client_salt.size()},
      {zero_checksum.data(), zero_checksum.size()},
  });
  if (!proof || !checksum) {
    return std::nullopt;
  }

  std::vector<std::uint8_t> packet;
  packet.reserve(58);
  packet.push_back(0x03);
  packet.insert(packet.end(), client_salt.begin(), client_salt.end());
  packet.insert(packet.end(), proof->begin(), proof->end());
  packet.insert(packet.end(), checksum->begin(), checksum->end());
  packet.push_back(0);
  return packet;
}

WireReadResult<std::vector<std::uint8_t>> ReadRealmListPacket(
    openwow::net::TcpClient& client,
    const std::uint32_t timeout_ms,
    const std::function<bool()>& should_cancel) {
  const OperationDeadline deadline(timeout_ms);
  auto header = ReadExact(client, 3, deadline, should_cancel);
  if (header.empty()) {
    return WireFailure<std::vector<std::uint8_t>>(
        AuthStatus::kNetworkError, "No realm list response received.");
  }
  if (header[0] != 0x10) {
    return WireFailure<std::vector<std::uint8_t>>(
        AuthStatus::kProtocolError, "Invalid realm list response header.");
  }
  const auto payload_size = static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(header[1])
      | (static_cast<std::uint16_t>(header[2]) << 8));
  if (payload_size < 8) {
    return WireFailure<std::vector<std::uint8_t>>(
        AuthStatus::kProtocolError, "Invalid realm list payload length.");
  }
  auto payload = ReadExact(client, payload_size, deadline, should_cancel);
  if (payload.empty()) {
    return WireFailure<std::vector<std::uint8_t>>(
        AuthStatus::kNetworkError, "Realm list response was incomplete.");
  }
  header.insert(header.end(), payload.begin(), payload.end());
  return {.packet = std::move(header)};
}

struct RealmListExchangeResult {
  RealmListFetchResult result;
  AuthStatus failure_status{AuthStatus::kNetworkError};
};

RealmListExchangeResult ExchangeRealmList(
    openwow::net::TcpClient& client,
    const std::vector<std::uint8_t>& request,
    const std::uint32_t timeout_ms,
    const std::function<bool()>& should_cancel) {
  if (!client.Write(request, timeout_ms, should_cancel)) {
    const bool cancelled = CancellationRequested(should_cancel);
    return {
        .result = {.ok = false,
                   .message = cancelled
                       ? "Realm list request cancelled."
                       : "Unable to send realm list request.",
                   .status = cancelled ? AuthStatus::kCancelled
                                       : AuthStatus::kNetworkError},
        .failure_status = cancelled ? AuthStatus::kCancelled
                                    : AuthStatus::kNetworkError,
    };
  }

  const auto response = ReadRealmListPacket(client, timeout_ms, should_cancel);
  if (!response.packet.has_value()) {
    const bool cancelled = CancellationRequested(should_cancel);
    return {
        .result = {.ok = false,
                   .message = cancelled
                       ? "Realm list request cancelled."
                       : response.failure.message,
                   .status = cancelled
                       ? AuthStatus::kCancelled
                       : response.failure.status},
        .failure_status = cancelled ? AuthStatus::kCancelled
                                    : response.failure.status,
    };
  }

  auto parsed = ParseRealmListBinaryDetailed(*response.packet);
  if (!parsed.ok) {
    return {
        .result = {.ok = false,
                   .message = "Malformed realm list response.",
                   .status = AuthStatus::kProtocolError},
        .failure_status = AuthStatus::kProtocolError,
    };
  }

  const auto realm_count = parsed.realms.size();
  return {
      .result = {
          .ok = true,
          .message = "Realm list loaded (" + std::to_string(realm_count)
                     + " realm(s)).",
          .realms = std::move(parsed.realms),
          .status = AuthStatus::kSuccess,
      },
      .failure_status = AuthStatus::kSuccess,
  };
}

}

void AuthProtocol::ReconnectContext::Clear() {
  host.clear();
  port = 0;
  uppercase_account.clear();
  session_key.fill(0);
  account_flags = 0;
  login_flags = 0;
  valid = false;
}

AuthProtocol::AuthProtocol()
    : client_(std::make_unique<openwow::net::TcpClient>()) {}

AuthProtocol::~AuthProtocol() {
  Disconnect();
}

bool AuthProtocol::ValidateCredentials(const std::string& username,
                                       const std::string& password) const {
  return !username.empty() && !password.empty();
}

AuthResult AuthProtocol::Login(const std::string& host,
                               std::uint16_t port,
                               const std::string& username,
                               const std::string& password,
                               std::uint32_t timeout_ms,
                               const std::function<bool()>& should_cancel) {
  std::unique_lock operation_lock(operation_mutex_);
  ResetLocked();
  const auto fail = [this](AuthResult result) {
    ResetLocked();
    return result;
  };
  if (!ValidateCredentials(username, password)) {
    return {.status = AuthStatus::kInvalidCredentials,
            .message = "Username/password cannot be empty."};
  }

  if (!client_->Connect(host, port, timeout_ms, should_cancel)) {
    return fail(CancellationRequested(should_cancel)
                    ? CancelledAuthResult("Authentication cancelled.")
                    : AuthResult{.status = AuthStatus::kNetworkError,
                                 .message =
                                     "Unable to connect to auth server."});
  }

  if (!client_->Write(BuildLogonChallengePacket(username), timeout_ms,
                      should_cancel)) {
    return fail(CancellationRequested(should_cancel)
                    ? CancelledAuthResult("Authentication cancelled.")
                    : AuthResult{.status = AuthStatus::kNetworkError,
                                 .message =
                                     "Unable to send auth challenge."});
  }

  const auto challenge_result =
      ReadAuthChallenge(*client_, timeout_ms, should_cancel);
  if (!challenge_result.packet.has_value()) {
    return fail(CancellationRequested(should_cancel)
                    ? CancelledAuthResult("Authentication cancelled.")
                    : challenge_result.failure);
  }
  const auto& challenge = *challenge_result.packet;

  openwow::auth::SRP6Client srp_client;
  srp_client.Initialize(username, password);
  if (!srp_client.ProcessChallenge(
          challenge.server_public_key.data(), challenge.server_public_key.size(),
          challenge.generator,
          challenge.modulus.data(), challenge.modulus.size(),
          challenge.salt.data(), challenge.salt.size())) {
    return fail({.status = AuthStatus::kProtocolError,
                 .message = "SRP-6a computation failed."});
  }

  const auto A  = srp_client.GetPublicKey();
  const auto M1 = srp_client.GetClientProof();
  const auto K  = srp_client.GetSessionKey();

  std::optional<openwow::net::LoginPinProof> pin_proof;
  if (challenge.pin_challenge.has_value()) {
    const auto& pin = *challenge.pin_challenge;
    auto& bridge = openwow::net::LoginPinChallengeBridge::Get();
    bridge.ConfigureChallenge(true, pin.shuffle_seed, pin.server_salt);
    pin_proof = bridge.WaitForSubmission(should_cancel);
    if (!pin_proof.has_value()) {
      bridge.Reset();
      return fail(CancelledAuthResult("Authentication cancelled."));
    }
  }

  std::optional<std::array<std::uint8_t, 20>> matrix_proof;
  if (challenge.matrix_challenge.has_value()) {
    const auto& matrix = *challenge.matrix_challenge;
    auto& bridge = openwow::net::LoginMatrixChallengeBridge::Get();
    if (!bridge.ConfigureGeneratedChallenge(
            true,
            matrix.columns,
            matrix.rows,
            matrix.digits_per_entry,
            matrix.entry_count,
            matrix.selection_seed,
            K)) {
      return fail({.status = AuthStatus::kProtocolError,
                   .message = "Invalid matrix-card challenge parameters."});
    }
    matrix_proof = bridge.WaitForSubmission(should_cancel);
    if (!matrix_proof.has_value()) {
      bridge.Reset();
      return fail(CancelledAuthResult("Authentication cancelled."));
    }
  }

  std::optional<std::string> token_proof;
  if (challenge.token_challenge) {
    auto& bridge = openwow::net::LoginTokenChallengeBridge::Get();
    bridge.ConfigureChallenge(true);
    token_proof = bridge.WaitForSubmission(should_cancel);
    if (!token_proof.has_value()) {
      bridge.Reset();
      return fail(CancelledAuthResult("Authentication cancelled."));
    }
  }

  std::vector<std::uint8_t> proof_pkt;
  proof_pkt.reserve(75u + (pin_proof.has_value() ? 36u : 0u)
                    + (matrix_proof.has_value() ? matrix_proof->size() : 0u)
                    + (token_proof.has_value() ? token_proof->size() + 1u : 0u));
  proof_pkt.push_back(0x01);
  proof_pkt.insert(proof_pkt.end(), A.begin(),  A.end());
  proof_pkt.insert(proof_pkt.end(), M1.begin(), M1.end());
  for (int i = 0; i < 20; ++i) proof_pkt.push_back(0x00);
  proof_pkt.push_back(0x00);
  std::uint8_t submitted_security_flags = 0;
  if (pin_proof.has_value()) submitted_security_flags |= kPinSecurityFlag;
  if (matrix_proof.has_value()) submitted_security_flags |= kMatrixSecurityFlag;
  if (token_proof.has_value()) submitted_security_flags |= kTokenSecurityFlag;
  proof_pkt.push_back(submitted_security_flags);
  if (pin_proof.has_value()) {
    proof_pkt.insert(proof_pkt.end(), pin_proof->client_salt.begin(),
                     pin_proof->client_salt.end());
    proof_pkt.insert(proof_pkt.end(), pin_proof->proof_hash.begin(),
                     pin_proof->proof_hash.end());
  }
  if (matrix_proof.has_value()) {
    proof_pkt.insert(proof_pkt.end(), matrix_proof->begin(), matrix_proof->end());
  }
  if (token_proof.has_value()) {
    proof_pkt.push_back(static_cast<std::uint8_t>(token_proof->size()));
    proof_pkt.insert(proof_pkt.end(), token_proof->begin(), token_proof->end());
  }
  if (!client_->Write(proof_pkt, timeout_ms, should_cancel)) {
    return fail(CancellationRequested(should_cancel)
                    ? CancelledAuthResult("Authentication cancelled.")
                    : AuthResult{.status = AuthStatus::kNetworkError,
                                 .message = "Unable to send auth proof."});
  }

  const auto proof_result = ReadAuthProof(*client_, timeout_ms, should_cancel);
  if (!proof_result.packet.has_value()) {
    if (proof_result.failure.status == AuthStatus::kPatchRequired &&
        !CancellationRequested(should_cancel)) {
      const auto transfer_result = ReadFileTransferInitiate(*client_, timeout_ms, should_cancel);
      if (!transfer_result.packet.has_value()) {
        return fail(transfer_result.failure);
      }
      file_transfer_expected_size_.store(transfer_result.packet->expected_size,
                                         std::memory_order_release);
      file_transfer_resume_offset_.store(0, std::memory_order_release);
      file_transfer_accepted_.store(false, std::memory_order_release);
      return {
          .status = AuthStatus::kPatchRequired,
          .message = proof_result.failure.login_status.result_key,
          .login_status = proof_result.failure.login_status,
          .file_transfer = std::move(transfer_result.packet),
      };
    }
    return fail(CancellationRequested(should_cancel)
                    ? CancelledAuthResult("Authentication cancelled.")
                    : proof_result.failure);
  }
  const auto &proof = *proof_result.packet;
  const auto login_status = ResolveLoginStatusInfo(0);

  if (!srp_client.VerifyServerProof(proof.server_proof.data(), proof.server_proof.size())) {
    const auto failed_status = MakeLoginStatusInfo(5, 11);
    return fail({
        .status = AuthStatus::kAuthRejected,
        .message = failed_status.result_key,
        .login_status = failed_status,
    });
  }

  reconnect_.host = host;
  reconnect_.port = port;
  reconnect_.uppercase_account = ToUpperAscii(username);
  reconnect_.session_key = K;
  reconnect_.account_flags = proof.account_flags;
  reconnect_.login_flags = proof.login_flags;
  reconnect_.valid = true;
  authenticated_.store(true, std::memory_order_release);

  auto realms = ExchangeRealmList(*client_, BuildRealmListRequestPacket(),
                                  timeout_ms, should_cancel);
  if (!realms.result.ok &&
      realms.failure_status == AuthStatus::kNetworkError &&
      !CancellationRequested(should_cancel)) {
    const auto reconnect_result = ReconnectLocked(timeout_ms, should_cancel);
    if (reconnect_result.status == AuthStatus::kSuccess) {
      realms = ExchangeRealmList(*client_, BuildRealmListRequestPacket(),
                                 timeout_ms, should_cancel);
    } else {
      return fail(std::move(reconnect_result));
    }
  }
  if (!realms.result.ok) {
    return fail({.status = realms.result.status,
                 .message = std::move(realms.result.message)});
  }

  return {.status = AuthStatus::kSuccess,
          .message = login_status.result_key,
          .session_token = KToHex(K),
          .session_key_raw = K,
          .login_status = login_status,
          .account_flags = reconnect_.account_flags,
          .login_flags = reconnect_.login_flags,
          .account_messages_available =
              (reconnect_.login_flags & 0x1u) != 0,
          .realms = std::move(realms.result.realms)};
}

AuthResult AuthProtocol::Reconnect(
    const std::uint32_t timeout_ms,
    const std::function<bool()>& should_cancel) {
  std::unique_lock operation_lock(operation_mutex_);
  return ReconnectLocked(timeout_ms, should_cancel);
}

AuthResult AuthProtocol::ReconnectLocked(
    const std::uint32_t timeout_ms,
    const std::function<bool()>& should_cancel) {
  if (!reconnect_.valid) {
    return {.status = AuthStatus::kAuthRejected,
            .message = "No Grunt reconnect context is available."};
  }
  if (CancellationRequested(should_cancel)) {
    return CancelledAuthResult("Auth reconnect cancelled.");
  }

  CloseTransport();
  const auto fail = [this](AuthResult result) {
    CloseTransport();
    if (result.status == AuthStatus::kAuthRejected ||
        result.status == AuthStatus::kProtocolError) {
      reconnect_.Clear();
    }
    return result;
  };

  if (!client_->Connect(reconnect_.host, reconnect_.port, timeout_ms,
                        should_cancel)) {
    return fail(CancellationRequested(should_cancel)
                    ? CancelledAuthResult("Auth reconnect cancelled.")
                    : AuthResult{.status = AuthStatus::kNetworkError,
                                 .message =
                                     "Unable to reconnect to auth server."});
  }
  if (!client_->Write(BuildReconnectChallengePacket(), timeout_ms,
                      should_cancel)) {
    return fail(CancellationRequested(should_cancel)
                    ? CancelledAuthResult("Auth reconnect cancelled.")
                    : AuthResult{.status = AuthStatus::kNetworkError,
                                 .message =
                                     "Unable to send auth reconnect challenge."});
  }

  const auto challenge =
      ReadReconnectChallenge(*client_, timeout_ms, should_cancel);
  if (!challenge.packet) {
    return fail(std::move(challenge.failure));
  }
  const auto proof = BuildReconnectProof(
      reconnect_.uppercase_account, reconnect_.session_key,
      challenge.packet->server_challenge);
  if (!proof) {
    return fail({.status = AuthStatus::kProtocolError,
                 .message = "Unable to generate auth reconnect proof."});
  }
  if (!client_->Write(*proof, timeout_ms, should_cancel)) {
    return fail(CancellationRequested(should_cancel)
                    ? CancelledAuthResult("Auth reconnect cancelled.")
                    : AuthResult{.status = AuthStatus::kNetworkError,
                                 .message =
                                     "Unable to send auth reconnect proof."});
  }

  const auto response = ReadReconnectProof(*client_, timeout_ms,
                                           should_cancel);
  if (!response.packet) {
    return fail(std::move(response.failure));
  }

  reconnect_.login_flags = *response.packet;
  authenticated_.store(true, std::memory_order_release);
  const auto login_status = ResolveLoginStatusInfo(0);
  return {
      .status = AuthStatus::kSuccess,
      .message = login_status.result_key,
      .session_token = KToHex(reconnect_.session_key),
      .session_key_raw = reconnect_.session_key,
      .login_status = login_status,
      .account_flags = reconnect_.account_flags,
      .login_flags = reconnect_.login_flags,
      .account_messages_available =
          (reconnect_.login_flags & 0x1u) != 0,
  };
}

RealmListFetchResult AuthProtocol::RequestRealmList(
    const std::uint32_t timeout_ms,
    const std::function<bool()>& should_cancel) {
  std::unique_lock operation_lock(operation_mutex_);
  if (!reconnect_.valid) {
    return {.ok = false,
            .message =
                "Realm list request requires an authenticated auth session.",
            .status = AuthStatus::kAuthRejected};
  }

  bool reconnect_attempted = false;
  if (!IsAuthenticated()) {
    reconnect_attempted = true;
    const auto reconnect_result = ReconnectLocked(timeout_ms, should_cancel);
    if (reconnect_result.status != AuthStatus::kSuccess) {
      return {.ok = false,
              .message = std::move(reconnect_result.message),
              .status = reconnect_result.status};
    }
  }

  auto exchange = ExchangeRealmList(*client_, BuildRealmListRequestPacket(),
                                    timeout_ms, should_cancel);
  if (!exchange.result.ok && !reconnect_attempted &&
      exchange.failure_status == AuthStatus::kNetworkError &&
      !CancellationRequested(should_cancel)) {
    const auto reconnect_result = ReconnectLocked(timeout_ms, should_cancel);
    if (reconnect_result.status == AuthStatus::kSuccess) {
      exchange = ExchangeRealmList(*client_, BuildRealmListRequestPacket(),
                                   timeout_ms, should_cancel);
    } else {
      return {.ok = false,
              .message = std::move(reconnect_result.message),
              .status = reconnect_result.status};
    }
  }
  if (!exchange.result.ok) {
    CloseTransport();
  }
  return std::move(exchange.result);
}

bool AuthProtocol::CanRequestRealmList() const {
  std::scoped_lock operation_lock(operation_mutex_);
  return reconnect_.valid;
}

void AuthProtocol::CancelPendingIo() {

  openwow::net::LoginPinChallengeBridge::Get().Reset();
  openwow::net::LoginMatrixChallengeBridge::Get().Reset();
  openwow::net::LoginTokenChallengeBridge::Get().Reset();
  CloseTransport();
}

void AuthProtocol::CloseTransport() {
  if (client_ != nullptr) {
    client_->Disconnect();
  }
  authenticated_.store(false, std::memory_order_release);
}

bool AuthProtocol::SendFileTransferResponse(const bool transfer_needed,
                                            const std::uint64_t resume_offset) {
  if (client_ == nullptr || !client_->IsConnected()) {
    return false;
  }
  const bool sent = client_->Write(
      openwow::net::auth::BuildLoginFileTransferResponsePacket(transfer_needed, resume_offset));
  if (sent) {
    file_transfer_resume_offset_.store(resume_offset, std::memory_order_release);
    file_transfer_accepted_.store(transfer_needed, std::memory_order_release);
  }
  return sent;
}

AuthResult AuthProtocol::ReceiveFileTransfer(
    const std::function<bool(std::span<const std::uint8_t>)> &receive_chunk,
    const std::uint32_t timeout_ms, const std::function<bool()> &should_cancel) {
  std::unique_lock operation_lock(operation_mutex_);
  const auto fail = [this](AuthResult result) {
    ResetLocked();
    return result;
  };
  if (!receive_chunk || client_ == nullptr || !client_->IsConnected() ||
      !file_transfer_accepted_.load(std::memory_order_acquire)) {
    return fail(
        {.status = AuthStatus::kProtocolError, .message = "Patch transfer was not accepted."});
  }

  const std::uint64_t expected_size = file_transfer_expected_size_.load(std::memory_order_acquire);
  std::uint64_t received_size = file_transfer_resume_offset_.load(std::memory_order_acquire);
  if (received_size > expected_size) {
    return fail({.status = AuthStatus::kProtocolError,
                 .message = "Patch resume offset exceeds the file size."});
  }

  const auto read_transfer_exact = [this, timeout_ms,
                                    &should_cancel](const std::size_t byte_count) {
    if (timeout_ms == 0u) {
      return client_->ReadExactUntilCancelled(byte_count, should_cancel);
    }
    return client_->ReadExact(byte_count, timeout_ms, should_cancel);
  };

  while (received_size < expected_size) {
    const auto header = read_transfer_exact(3);
    if (header.empty()) {
      return fail(CancellationRequested(should_cancel)
                      ? CancelledAuthResult("Patch transfer cancelled.")
                      : AuthResult{.status = AuthStatus::kNetworkError,
                                   .message = "Patch transfer packet was incomplete."});
    }
    if (header[0] != 0x31u) {
      return fail(
          {.status = AuthStatus::kProtocolError, .message = "Invalid patch transfer data opcode."});
    }
    const std::uint16_t chunk_size = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(header[1]) | (static_cast<std::uint16_t>(header[2]) << 8u));
    if (chunk_size == 0u ||
        static_cast<std::uint64_t>(chunk_size) > expected_size - received_size) {
      return fail(
          {.status = AuthStatus::kProtocolError, .message = "Invalid patch transfer chunk size."});
    }
    const auto chunk = read_transfer_exact(chunk_size);
    if (chunk.empty()) {
      return fail(CancellationRequested(should_cancel)
                      ? CancelledAuthResult("Patch transfer cancelled.")
                      : AuthResult{.status = AuthStatus::kNetworkError,
                                   .message = "Patch transfer chunk was incomplete."});
    }
    if (!receive_chunk(chunk)) {
      return fail({.status = AuthStatus::kProtocolError,
                   .message = "Patch transfer cache rejected a chunk."});
    }
    received_size += chunk.size();
  }

  file_transfer_accepted_.store(false, std::memory_order_release);
  return {
      .status = AuthStatus::kPatchTransferComplete,
      .message = "LOGIN_OK",
      .login_status = MakeLoginStatusInfo(6, 0),
  };
}

void AuthProtocol::Disconnect() {

  CloseTransport();
  std::unique_lock operation_lock(operation_mutex_);
  ResetLocked();
}

void AuthProtocol::ResetLocked() {
  CloseTransport();
  reconnect_.Clear();
  openwow::net::LoginPinChallengeBridge::Get().Reset();
  openwow::net::LoginMatrixChallengeBridge::Get().Reset();
  openwow::net::LoginTokenChallengeBridge::Get().Reset();
  file_transfer_accepted_.store(false, std::memory_order_release);
  file_transfer_expected_size_.store(0, std::memory_order_release);
  file_transfer_resume_offset_.store(0, std::memory_order_release);
}

bool AuthProtocol::IsAuthenticated() const {
  return authenticated_.load(std::memory_order_acquire) &&
         client_ != nullptr && client_->IsConnected();
}

std::vector<std::uint8_t> AuthProtocol::BuildLogonChallengePacket(
    const std::string& username) const {
  return BuildRetailLogonChallengePacket(username);
}

std::vector<std::uint8_t> AuthProtocol::BuildReconnectChallengePacket() const {
  auto packet = BuildRetailLogonChallengePacket(
      reconnect_.uppercase_account);
  if (!packet.empty()) {
    packet.front() = 0x02;
  }
  return packet;
}

std::vector<std::uint8_t> AuthProtocol::BuildRealmListRequestPacket() const {

  return {0x10, 0x00, 0x00, 0x00, 0x00};
}

}

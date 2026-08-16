
#include "openwow/net/wotlk/login_object.h"

#include "openwow/net/auth/login_file_transfer_packet.h"
#include "openwow/net/login_survey_result.h"
#include "openwow/net/wotlk/protocol/auth_protocol.h"

#include <cstring>
#include <utility>

namespace openwow::net::wotlk {

namespace {

constexpr std::size_t kLoginBufferBytes = 1280;

[[nodiscard]] std::string CopyStormBufferString(const std::string& value,
                                                const std::size_t buffer_bytes) {
  if (buffer_bytes == 0) {
    return {};
  }

  const std::size_t max_chars = buffer_bytes - 1;
  if (value.size() <= max_chars) {
    return value;
  }

  return value.substr(0, max_chars);
}

void SecureClearString(std::string& value) {
  if (value.empty()) {
    return;
  }

  volatile char* bytes = value.data();
  for (std::size_t i = 0; i < value.size(); ++i) {
    bytes[i] = '\0';
  }
}

void SecureReleaseString(std::string& value) {
  std::string owned_buffer;
  owned_buffer.swap(value);
  SecureClearString(owned_buffer);
}

}

Login::Login()
    : byte_4_(0),
      byte_5_(0),
      byte_12_(0),
      state_(0),
      result_(0),
      field_2_(-1),
      srp_state_{},
      srp_state2_{},
      pin_data_{},
      extra_state_{},
      extra_state2_{},
      field_1082_(0) {

}

Login::~Login() {
  SecureReleaseString(password_);
  ClearAuxiliaryCallbacks();
}

void Login::SetCredentials(const std::string& account_name,
                           const std::string& password) {
  formatted_account_name_ = CopyStormBufferString(account_name, kLoginBufferBytes);
  SecureReleaseString(password_);
  password_ = password;
}

void Login::SetRawAccountName(const std::string& account_name) {
  raw_account_name_ = CopyStormBufferString(account_name, kLoginBufferBytes);
}

void Login::SetStateAndResult(const std::uint32_t state,
                              const std::uint32_t result,
                              const std::int32_t error_code,
                              const std::int32_t extra) {
  state_ = state;
  result_ = result;

  DispatchAuxiliaryStateResult(state, result, error_code, extra);
}

void Login::SetStateResultCallback(LoginStateResultCallback cb) {
  state_result_callback_ = std::move(cb);
}

void Login::SetAuxiliaryCallbacks(LoginAuxiliaryCallback vf12_callback,
                                  LoginAuxiliaryCallback vf16_callback) {
  vf12_callback_ = std::move(vf12_callback);
  vf16_callback_ = std::move(vf16_callback);
}

void Login::ClearAuxiliaryCallbacks() {
  state_result_callback_ = nullptr;
  vf12_callback_ = nullptr;
  vf16_callback_ = nullptr;
}

void Login::DispatchAuxiliaryStateResult(const std::uint32_t state,
                                         const std::uint32_t result,
                                         const std::int32_t error_code,
                                         const std::int32_t extra) const {
  if (!state_result_callback_) {
    return;
  }

  state_result_callback_(state,
                         result,
                         error_code,
                         ResolveLoginStateKey(state),
                         ResolveLoginResultKey(result),
                         extra);
}

GruntLogin::GruntLogin()
    : pin_crypto_state_(std::make_unique<GruntLoginCryptoState>()),
      matrix_crypto_state_(std::make_unique<GruntLoginCryptoState>()) {}

GruntLogin::~GruntLogin() {
  matrix_slots_.clear();
  pin_crypto_state_.reset();
  matrix_crypto_state_.reset();
  connect_callback_ = nullptr;
  transport_initialized_ = false;

}

void GruntLogin::SetRealmListCache(void* ) {

  transport_initialized_ = true;

  login_proof_result_ = 0;
  account_flags_ = 0;
  survey_proof_token_ = 0;
}

bool GruntLogin::OnConnected(const std::string& ) {
  byte_5_ = 1;
  SetStateAndResult(15, 0);
  return true;
}

bool GruntLogin::OnDisconnected() {
  SetStateAndResult(16, 0);

  if (!byte_4_) {
    field_2_ = 1;
  }

  byte_5_ = 0;
  byte_4_ = 0;
  return false;
}

bool GruntLogin::OnConnectionError() {
  SetStateAndResult(5, 12);
  field_2_ = 1;
  byte_5_ = 0;
  byte_4_ = 0;
  return false;
}

bool GruntLogin::SetByte4Flag() {
  byte_4_ = 1;
  return true;
}

std::uint32_t GruntLogin::GetLoginProofResult() const {
  return login_proof_result_;
}

void GruntLogin::BeginDownloadFile(const std::string& filename,
                                   const std::uint32_t download_param1,
                                   const std::uint32_t download_param2,
                                   const std::uint8_t digest[16]) {
  download_param1_ = download_param1;
  download_param2_ = download_param2;

  download_filename_ = filename;
  if (download_filename_.size() > 260) {
    download_filename_.resize(260);
  }

  std::memcpy(download_digest_.data(), digest, download_digest_.size());
  DispatchAuxiliaryStateResult(6, 0, 0, 0);
}

int GruntLogin::ForwardConnect() {
  if (!vf12_callback_) {
    return 0;
  }

  return vf12_callback_();
}

int GruntLogin::ForwardDisconnect() {
  if (!vf16_callback_) {
    return 0;
  }

  return vf16_callback_();
}

void GruntLogin::InitiateConnection(const char* server_address) {

  if (byte_5_) {
    return;
  }

  byte_12_ = 0;
  field_2_ = -1;
  grunt_flag_4352_ = 0;
  matrix_challenge_active_ = 0;
  grunt_flag_4468_ = 0;
  grunt_flag_4470_ = 0;

  SetStateAndResult(1, 0);

  const char* address =
      (server_address && server_address[0] != '\0') ? server_address
                                                    : kDefaultLoginAddress;

  if (connect_callback_) {
    connect_callback_(address);
  }
}

void GruntLogin::SetConnectCallback(GruntLoginConnectCallback cb) {
  connect_callback_ = std::move(cb);
}

bool GruntLogin::WaitForLoginResult(LoginResultData& out) {

  while (field_2_ == -1) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  if (field_2_ == 0) {

    out.account_name = formatted_account_name_;
    out.session_key = session_key_;
    return true;
  }

  out.account_name.clear();
  out.session_key.fill(0);
  return false;
}

void GruntLogin::BeginHandshake(const LoginResultData& login_data) {
  if (!byte_5_) {
    return;
  }

  if (handshake_send_fn_) {
    handshake_send_fn_(login_data);
  }

  SetStateAndResult(2, 0);
}

void GruntLogin::SetHandshakeSendFn(HandshakeSendFn fn) {
  handshake_send_fn_ = std::move(fn);
}

std::optional<MatrixCardConfig> GruntLogin::GetMatrixConfig() const {
  if (!matrix_challenge_active_) {
    return std::nullopt;
  }

  return MatrixCardConfig{
      .columns = matrix_columns_,
      .rows = matrix_rows_,
      .digit_count = matrix_digit_count_,
      .challenge_count = matrix_challenge_count_,
      .flags = matrix_flags_,
      .entry_count = matrix_entry_count_,
  };
}

std::optional<GruntLogin::MatrixCoordinate> GruntLogin::GetMatrixCoordinates(
    const std::uint32_t entry_index) const {
  if (!matrix_challenge_active_) {
    return std::nullopt;
  }

  if (matrix_slots_.empty() || entry_index >= matrix_entry_count_) {
    return std::nullopt;
  }

  if (matrix_columns_ == 0) {
    return std::nullopt;
  }

  const std::uint32_t slot = matrix_slots_[entry_index];
  return MatrixCoordinate{
      .column = slot % matrix_columns_,
      .row = slot / matrix_columns_,
  };
}

bool GruntLogin::HasRidBlockFlag() const {
  return (account_flags_ >> 3) & 1;
}

bool GruntLogin::BypassesRealmCategoryLocaleValidation() const {
  return account_flags_ & 1;
}

bool GruntLogin::BypassesTournamentRealmCategoryValidation() const {
  return (account_flags_ >> 23) & 1;
}

std::uint32_t GruntLogin::GetSurveyProofToken() const {
  return survey_proof_token_;
}

void GruntLogin::SendSurveyResult(
    const std::uint16_t len,
    const std::span<const std::uint8_t> payload) {
  if (!auth_send_fn_) {
    return;
  }

  const auto effective_len = static_cast<std::size_t>(len);
  const std::span<const std::uint8_t> clamped =
      payload.size() >= effective_len
          ? payload.subspan(0, effective_len)
          : payload;

  auto packet = net::BuildLoginSurveyResultPacket(survey_proof_token_, clamped);
  auth_send_fn_(packet);
}

void GruntLogin::CancelFileTransfer() {
  if (!auth_send_fn_) {
    return;
  }

  const std::vector<std::uint8_t> packet{0x34};
  auth_send_fn_(packet);
}

void GruntLogin::SendSurveyResultError() {
  if (!auth_send_fn_) {
    return;
  }

  auto packet = net::BuildLoginSurveyResultErrorPacket(survey_proof_token_);
  auth_send_fn_(packet);
}

void GruntLogin::SendFileTransferResponse(const bool transfer_needed,
                                          const std::uint64_t resume_offset) {
  if (!auth_send_fn_) {
    return;
  }

  auth_send_fn_(auth::BuildLoginFileTransferResponsePacket(
      transfer_needed, resume_offset));
}

void GruntLogin::SetAuthPacketSendFn(AuthPacketSendFn fn) {
  auth_send_fn_ = std::move(fn);
}

}

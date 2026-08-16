
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace openwow::net::wotlk {

struct MatrixCardConfig {
  std::uint32_t columns{0};

  std::uint32_t rows{0};

  std::uint32_t digit_count{0};

  std::uint32_t challenge_count{0};

  std::uint8_t flags{0};

  std::uint32_t entry_count{0};

};

struct LoginResultData {
  std::string account_name;
  std::array<std::uint8_t, 40> session_key{};
};

constexpr std::uint32_t kMaxLoginState = 17;

constexpr std::uint32_t kMaxLoginResult = 40;

using LoginStateResultCallback =
    std::function<void(std::uint32_t state,
                       std::uint32_t result,
                       std::int32_t error_code,
                       const std::string& state_name,
                       const std::string& result_name,
                       std::int32_t extra)>;
using LoginAuxiliaryCallback = std::function<int()>;

class Login {
 public:

  Login();

  virtual ~Login();

  void SetCredentials(const std::string& account_name,
                      const std::string& password);

  void SetRawAccountName(const std::string& account_name);

  void SetStateAndResult(std::uint32_t state,
                         std::uint32_t result,
                         std::int32_t error_code = 0,
                         std::int32_t extra = 0);

  [[nodiscard]] std::uint8_t GetByte12() const { return byte_12_; }

  [[nodiscard]] std::uint8_t GetByte5() const { return byte_5_; }

  [[nodiscard]] virtual std::uint32_t GetLoginProofResult() const = 0;

  virtual void SetRealmListCache(void* cache) = 0;

  void SetStateResultCallback(LoginStateResultCallback cb);

  void SetAuxiliaryCallbacks(LoginAuxiliaryCallback vf12_callback,
                             LoginAuxiliaryCallback vf16_callback);

  [[nodiscard]] const std::string& account_name() const {
    return formatted_account_name_;
  }
  [[nodiscard]] const std::string& raw_account_name() const {
    return raw_account_name_;
  }
  [[nodiscard]] std::uint32_t login_state() const { return state_; }
  [[nodiscard]] std::uint32_t login_result() const { return result_; }

  struct SRP6SHA1Context {
    bool active{false};
    std::array<std::uint8_t, 20> hash{};
    std::array<std::uint8_t, 64> key_segment{};
  };

 protected:
  std::uint8_t byte_4_{0};
  std::uint8_t byte_5_{0};
  std::uint8_t byte_12_{0};
  std::string formatted_account_name_;
  std::string raw_account_name_;
  std::string extra_buffer_;
  std::string password_;
  std::uint32_t state_{0};
  std::uint32_t result_{0};
  std::int32_t field_2_{-1};

  std::array<std::uint32_t, 10> srp_state_{};
  std::array<std::uint32_t, 6> srp_state2_{};
  std::array<std::uint8_t, 260> pin_data_{};
  std::array<std::uint32_t, 13> extra_state_{};
  std::array<std::uint32_t, 12> extra_state2_{};
  std::uint32_t field_1082_{0};

  LoginStateResultCallback state_result_callback_;
  LoginAuxiliaryCallback vf12_callback_;
  LoginAuxiliaryCallback vf16_callback_;

  void ClearAuxiliaryCallbacks();
  void DispatchAuxiliaryStateResult(std::uint32_t state,
                                    std::uint32_t result,
                                    std::int32_t error_code = 0,
                                    std::int32_t extra = 0) const;
};

using GruntLoginConnectCallback =
    std::function<void(const std::string& address)>;

using AuthPacketSendFn =
    std::function<bool(const std::vector<std::uint8_t>&)>;

struct GruntLoginCryptoState {
  bool active{false};
};

class GruntLogin : public Login {
 public:
  GruntLogin();
  ~GruntLogin() override;

  void SetRealmListCache(void* cache) override;

  bool OnConnected(const std::string& server_address);

  bool OnDisconnected();

  bool OnConnectionError();

  bool SetByte4Flag();

  [[nodiscard]] std::uint32_t GetLoginProofResult() const override;

  void BeginDownloadFile(const std::string& filename,
                         std::uint32_t download_param1,
                         std::uint32_t download_param2,
                         const std::uint8_t digest[16]);

  int ForwardConnect();

  int ForwardDisconnect();

  void InitiateConnection(const char* server_address = nullptr);

  bool WaitForLoginResult(LoginResultData& out);

  using HandshakeSendFn = std::function<void(const LoginResultData&)>;
  void BeginHandshake(const LoginResultData& login_data);

  void SetHandshakeSendFn(HandshakeSendFn fn);

  void SetSessionKey(const std::array<std::uint8_t, 40>& key) {
    session_key_ = key;
  }
  [[nodiscard]] const std::array<std::uint8_t, 40>& session_key() const {
    return session_key_;
  }

  [[nodiscard]] std::optional<MatrixCardConfig> GetMatrixConfig() const;

  struct MatrixCoordinate {
    std::uint32_t column{0};
    std::uint32_t row{0};
  };
  [[nodiscard]] std::optional<MatrixCoordinate> GetMatrixCoordinates(
      std::uint32_t entry_index) const;

  [[nodiscard]] bool HasRidBlockFlag() const;

  [[nodiscard]] bool BypassesRealmCategoryLocaleValidation() const;

  [[nodiscard]] bool BypassesTournamentRealmCategoryValidation() const;

  [[nodiscard]] std::uint32_t GetSurveyProofToken() const;

  void SendSurveyResult(std::uint16_t len,
                        std::span<const std::uint8_t> payload);

  void SendSurveyResultError();

  void SendFileTransferResponse(bool transfer_needed,
                                std::uint64_t resume_offset);

  void CancelFileTransfer();

  void SetConnectCallback(GruntLoginConnectCallback cb);

  void SetAuthPacketSendFn(AuthPacketSendFn fn);

  void SetAccountFlags(std::uint32_t flags) { account_flags_ = flags; }

  void SetSurveyProofToken(std::uint32_t token) {
    survey_proof_token_ = token;
  }

  [[nodiscard]] std::uint32_t download_param1() const { return download_param1_; }
  [[nodiscard]] std::uint32_t download_param2() const { return download_param2_; }
  [[nodiscard]] const std::array<std::uint8_t, 16>& download_digest() const {
    return download_digest_;
  }
  [[nodiscard]] const std::string& download_filename() const {
    return download_filename_;
  }

  [[nodiscard]] bool transport_initialized() const {
    return transport_initialized_;
  }

 private:

  static constexpr const char* kDefaultLoginAddress =
      "us.logon.worldofwarcraft.com:3724";

  std::uint32_t download_param1_{0};
  std::uint32_t download_param2_{0};
  std::array<std::uint8_t, 16> download_digest_{};
  std::string download_filename_;

  std::array<std::uint8_t, 40> session_key_{};

  std::uint8_t grunt_flag_4352_{0};

  std::uint8_t matrix_digit_count_{0};
  std::uint8_t matrix_challenge_count_{0};
  std::uint8_t matrix_flags_{0};
  std::uint8_t matrix_challenge_active_{0};
  std::uint8_t matrix_columns_{0};
  std::uint8_t matrix_rows_{0};
  std::uint8_t matrix_entry_count_{0};
  std::vector<std::uint32_t> matrix_slots_;

  std::unique_ptr<GruntLoginCryptoState> pin_crypto_state_;
  std::unique_ptr<GruntLoginCryptoState> matrix_crypto_state_;

  std::uint8_t grunt_flag_4468_{0};
  std::uint8_t grunt_flag_4470_{0};

  GruntLoginConnectCallback connect_callback_;

  bool transport_initialized_{false};

  std::uint32_t login_proof_result_{0};

  std::uint32_t account_flags_{0};

  std::uint32_t survey_proof_token_{0};

  AuthPacketSendFn auth_send_fn_;

  HandshakeSendFn handshake_send_fn_;
};

}

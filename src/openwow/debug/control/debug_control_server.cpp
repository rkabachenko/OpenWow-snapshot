#include "openwow/debug/control/debug_control_server.h"

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/write.hpp>
#include <boost/system/error_code.hpp>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace openwow::debug::control {
namespace {

using Tcp = boost::asio::ip::tcp;

constexpr std::size_t kCapabilityTokenBytes = 32U;
constexpr std::size_t kCapabilityTokenHexCharacters =
    kCapabilityTokenBytes * 2U;
constexpr std::size_t kReadChunkBytes = 4096U;
constexpr std::size_t kFrameDelimiterBytes = 1U;
constexpr std::size_t kMinimumRequestBytes = 1024U;
constexpr std::size_t kMinimumResponseBytes = 1024U;
constexpr std::size_t kHardMaxRequestBytes = 4U * 1024U * 1024U;
constexpr std::size_t kHardMaxResponseBytes = 64U * 1024U * 1024U;
constexpr std::size_t kHardMaxScreenshotBytes = 32U * 1024U * 1024U;
constexpr std::size_t kHardMaxArtifactPathBytes = 64U * 1024U;
constexpr std::size_t kHardMaxStringBytes = 64U * 1024U;
constexpr std::size_t kHardMaxConnections = 64U;
constexpr std::size_t kHardMaxRequestsPerConnection = 100000U;
constexpr std::size_t kHardMaxJsonDepth = 128U;
constexpr std::size_t kHardMaxJsonMembers = 65536U;
constexpr std::size_t kMaxErrorCodeBytes = 64U;
constexpr std::size_t kMaxErrorMessageBytes = 512U;
constexpr std::size_t kMaxMediaTypeBytes = 128U;

constexpr std::string_view kErrorAlreadyRunning = "already_running";
constexpr std::string_view kErrorInvalidOptions = "invalid_options";
constexpr std::string_view kErrorCodecMissing = "protocol_codec_missing";
constexpr std::string_view kErrorTokenGeneration = "token_generation_failed";
constexpr std::string_view kErrorBindFailed = "bind_failed";
constexpr std::string_view kErrorListenFailed = "listen_failed";
constexpr std::string_view kErrorAcceptFailed = "accept_failed";
constexpr std::string_view kErrorRequestTooLarge = "request_too_large";
constexpr std::string_view kErrorRequestLimitReached =
    "request_limit_reached";
constexpr std::string_view kErrorUnauthorized = "unauthorized";
constexpr std::string_view kErrorCapabilityUnavailable =
    "capability_unavailable";
constexpr std::string_view kErrorCancelled = "cancelled";
constexpr std::string_view kErrorResponseTooLarge = "response_too_large";
constexpr std::string_view kErrorInvalidScreenshot =
    "invalid_screenshot_result";
constexpr std::string_view kErrorAdapterError = "adapter_error";
constexpr std::string_view kErrorCodecEncode = "codec_encode_failed";
constexpr std::string_view kErrorCodecOutput = "codec_output_invalid";
constexpr std::string_view kErrorInternal = "internal_error";

constexpr std::array<char, 16> kHexDigits{
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
};

[[nodiscard]] std::string CopyBoundedText(const std::string_view text,
                                          const std::size_t max_bytes,
                                          const std::string_view fallback) {
  if (text.empty()) {
    return std::string(fallback);
  }
  return std::string(text.substr(0, std::min(text.size(), max_bytes)));
}

[[nodiscard]] DebugControlError NormalizeError(
    DebugControlError error,
    const std::string_view fallback_code,
    const std::string_view fallback_message) {
  error.code = CopyBoundedText(error.code, kMaxErrorCodeBytes, fallback_code);
  error.message =
      CopyBoundedText(error.message, kMaxErrorMessageBytes, fallback_message);
  return error;
}

[[nodiscard]] DebugControlError MakeError(const std::string_view code,
                                          const std::string_view message,
                                          const bool retryable = false) {
  return DebugControlError{std::string(code), std::string(message), retryable};
}

[[nodiscard]] bool ContainsControlCharacter(const std::string_view text) {
  return std::any_of(text.begin(), text.end(), [](const char value) {
    return static_cast<unsigned char>(value) < 0x20U;
  });
}

[[nodiscard]] bool GenerateCapabilityToken(std::string* const token) {
  std::array<unsigned char, kCapabilityTokenBytes> random_bytes{};
  if (RAND_bytes(random_bytes.data(),
                 static_cast<int>(random_bytes.size())) != 1) {
    return false;
  }

  token->clear();
  token->reserve(kCapabilityTokenHexCharacters);
  for (const unsigned char value : random_bytes) {
    token->push_back(kHexDigits[(value >> 4U) & 0x0fU]);
    token->push_back(kHexDigits[value & 0x0fU]);
  }
  return token->size() == kCapabilityTokenHexCharacters;
}

[[nodiscard]] bool ConstantTimeEqual(const std::string_view left,
                                     const std::string_view right) noexcept {
  const std::size_t comparison_size = std::max(left.size(), right.size());
  std::uint64_t difference = left.size() ^ right.size();
  for (std::size_t index = 0; index < comparison_size; ++index) {
    const auto left_value =
        index < left.size() ? static_cast<unsigned char>(left[index]) : 0U;
    const auto right_value =
        index < right.size() ? static_cast<unsigned char>(right[index]) : 0U;
    difference |= static_cast<std::uint64_t>(left_value ^ right_value);
  }
  return difference == 0U;
}

[[nodiscard]] bool IsFrameSized(const std::string_view frame,
                                const std::size_t max_response_bytes) {
  return !frame.empty() &&
         frame.find_first_of("\r\n") == std::string_view::npos &&
         max_response_bytes >= kFrameDelimiterBytes &&
         frame.size() <= max_response_bytes - kFrameDelimiterBytes;
}

[[nodiscard]] DebugControlResponse FailureResponse(
    const RequestId request_id,
    DebugControlError error) {
  const auto normalized = NormalizeError(
      std::move(error), kErrorInternal, "debug-control request failed");
  CapabilityResult<DebugControlSuccess> payload{
      std::in_place_type<DebugControlError>, normalized};
  return DebugControlResponse{request_id, std::move(payload)};
}

template <typename Success>
[[nodiscard]] DebugControlResponse SuccessResponse(const RequestId request_id,
                                                   Success success) {
  DebugControlSuccess value{std::in_place_type<Success>, std::move(success)};
  CapabilityResult<DebugControlSuccess> payload{
      std::in_place_type<DebugControlSuccess>, std::move(value)};
  return DebugControlResponse{request_id, std::move(payload)};
}

[[nodiscard]] bool ValidateLimits(const DebugControlServerLimits& limits) {
  return limits.max_request_bytes >= kMinimumRequestBytes &&
         limits.max_request_bytes <= kHardMaxRequestBytes &&
         limits.max_response_bytes >= kMinimumResponseBytes &&
         limits.max_response_bytes <= kHardMaxResponseBytes &&
         limits.max_screenshot_bytes > 0U &&
         limits.max_screenshot_bytes <= kHardMaxScreenshotBytes &&
         limits.max_screenshot_bytes <= limits.max_response_bytes &&
         limits.max_artifact_path_bytes > 0U &&
         limits.max_artifact_path_bytes <= kHardMaxArtifactPathBytes &&
         limits.max_string_bytes > 0U &&
         limits.max_string_bytes <= kHardMaxStringBytes &&
         limits.max_connections > 0U &&
         limits.max_connections <= kHardMaxConnections &&
         limits.max_requests_per_connection > 0U &&
         limits.max_requests_per_connection <= kHardMaxRequestsPerConnection &&
         limits.max_json_depth > 0U && limits.max_json_depth <= kHardMaxJsonDepth &&
         limits.max_json_members > 0U &&
         limits.max_json_members <= kHardMaxJsonMembers;
}

[[nodiscard]] std::optional<DebugControlError> ValidateScreenshot(
    const ScreenshotResult& result,
    const DebugControlServerLimits& limits) {
  std::optional<DebugControlError> failure;
  std::visit(
      [&failure, &limits](const auto& payload) {
        using Payload = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<Payload, ScreenshotBytes>) {
          if (payload.bytes.size() > limits.max_screenshot_bytes ||
              payload.media_type.empty() ||
              payload.media_type.size() > kMaxMediaTypeBytes ||
              ContainsControlCharacter(payload.media_type)) {
            failure = MakeError(kErrorInvalidScreenshot,
                                "screenshot bytes or media type is invalid");
          }
        } else if constexpr (std::is_same_v<Payload, ScreenshotArtifact>) {
          if (payload.path.empty() ||
              payload.path.size() > limits.max_artifact_path_bytes ||
              payload.media_type.empty() ||
              payload.media_type.size() > kMaxMediaTypeBytes ||
              ContainsControlCharacter(payload.path) ||
              ContainsControlCharacter(payload.media_type)) {
            failure = MakeError(kErrorInvalidScreenshot,
                                "screenshot artifact or media type is invalid");
          }
        }
      },
      result);
  return failure;
}

}

class DebugControlServer::Impl final {
 public:
  class Session;

  struct EncodedResponse {
    std::string bytes;
    bool close_after_write{false};
  };

  Impl(std::shared_ptr<const DebugControlCodec> codec,
       DebugControlCapabilities capabilities,
       DebugControlServerOptions options)
      : codec_(std::move(codec)),
        capabilities_(std::move(capabilities)),
        options_(std::move(options)) {}

  ~Impl();

  [[nodiscard]] DebugControlStartResult Start();
  void Stop() noexcept;

  [[nodiscard]] bool IsRunning() const noexcept {
    std::lock_guard lock(lifecycle_mutex_);
    return state_ == LifecycleState::kRunning;
  }

  [[nodiscard]] std::optional<DebugControlEndpoint> Endpoint() const {
    std::lock_guard lock(lifecycle_mutex_);
    if (state_ != LifecycleState::kRunning || !endpoint_.has_value()) {
      return std::nullopt;
    }
    return endpoint_;
  }

  [[nodiscard]] std::optional<DebugControlError> LastError() const {
    std::lock_guard lock(lifecycle_mutex_);
    return last_error_;
  }

  [[nodiscard]] RequestId NextRequestId() noexcept {
    auto request_id = next_request_id_++;
    if (request_id == 0U) {
      request_id = next_request_id_++;
    }
    return request_id;
  }

  [[nodiscard]] const DebugControlServerLimits& limits() const noexcept {
    return options_.limits;
  }

  [[nodiscard]] bool IsStopping() const noexcept {
    return stop_requested_->load(std::memory_order_acquire);
  }

  void RemoveSession(Session* session);
  void AddSession(Tcp::socket socket);

 private:
  friend class Session;

  enum class LifecycleState : std::uint8_t {
    kStopped,
    kRunning,
    kStopping,
  };

  [[nodiscard]] static DebugControlError ValidateOptions(
      const DebugControlServerOptions& options) {
    if (!ValidateLimits(options.limits)) {
      return MakeError(kErrorInvalidOptions,
                       "debug-control server options are outside safe bounds");
    }
    return {};
  }

  [[nodiscard]] DebugControlResponse HandleRequest(
      Session& session,
      const DebugControlRequest& request);

  [[nodiscard]] DebugControlResponse HandleRequestPayload(
      Session& session,
      const DebugControlRequest& request);

  [[nodiscard]] EncodedResponse HandleFrame(Session& session,
                                            std::string_view frame);

  [[nodiscard]] EncodedResponse HandleFramingError(std::string_view code,
                                                   std::string_view message);

  [[nodiscard]] EncodedResponse EncodeResponse(
      const DebugControlResponse& response,
      bool close_after_write);

  void AcceptNext();
  void CloseAllOnIoThread();
  void RunIoContext();
  void HandleIoThreadExit();

  std::shared_ptr<const DebugControlCodec> codec_;
  DebugControlCapabilities capabilities_;
  DebugControlServerOptions options_;

  mutable std::mutex lifecycle_mutex_;
  std::mutex stop_mutex_;
  LifecycleState state_{LifecycleState::kStopped};
  std::thread worker_;

  boost::asio::io_context io_context_;
  using WorkGuard =
      boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
  std::optional<WorkGuard> work_guard_;
  std::unique_ptr<Tcp::acceptor> acceptor_;
  std::vector<std::shared_ptr<Session>> sessions_;

  std::shared_ptr<std::atomic_bool> stop_requested_{
      std::make_shared<std::atomic_bool>(false)};
  RequestId next_request_id_{1U};
  std::string capability_token_;
  std::optional<DebugControlEndpoint> endpoint_;
  std::optional<DebugControlError> last_error_;
};

class DebugControlServer::Impl::Session final
    : public std::enable_shared_from_this<DebugControlServer::Impl::Session> {
 public:
  Session(Impl& owner, Tcp::socket socket)
      : owner_(owner), socket_(std::move(socket)) {}

  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  void Start() {
    ReadNext();
  }

  void Close() {
    if (closed_) {
      return;
    }
    closed_ = true;
    cancelled_->store(true, std::memory_order_release);
    boost::system::error_code ignored;
    socket_.shutdown(Tcp::socket::shutdown_both, ignored);
    socket_.close(ignored);
    owner_.RemoveSession(this);
  }

  [[nodiscard]] bool IsCancelled() const noexcept {
    return cancelled_->load(std::memory_order_acquire);
  }

  [[nodiscard]] std::shared_ptr<const std::atomic_bool> CancellationState()
      const noexcept {
    return cancelled_;
  }

 private:
  void ReadNext() {
    if (closed_ || owner_.IsStopping() || write_in_progress_) {
      return;
    }
    const auto remaining_bytes =
        owner_.limits().max_request_bytes - input_buffer_.size() + 1U;
    socket_.async_read_some(
        boost::asio::buffer(
            read_buffer_, std::min(read_buffer_.size(), remaining_bytes)),
        [self = shared_from_this()](const boost::system::error_code& error,
                                    const std::size_t bytes_read) {
          self->HandleRead(error, bytes_read);
        });
  }

  void HandleRead(const boost::system::error_code& error,
                  const std::size_t bytes_read) {
    if (closed_) {
      return;
    }
    if (error) {
      Close();
      return;
    }
    if (bytes_read == 0U) {
      Close();
      return;
    }
    input_buffer_.append(read_buffer_.data(), bytes_read);
    ProcessNextFrame();
  }

  void ProcessNextFrame() {
    if (closed_ || write_in_progress_) {
      return;
    }

    const auto delimiter = input_buffer_.find('\n');
    if (delimiter == std::string::npos) {
      if (input_buffer_.size() > owner_.limits().max_request_bytes) {
        auto response = owner_.HandleFramingError(
            kErrorRequestTooLarge, "request frame exceeds configured bounds");
        Send(std::move(response.bytes), true);
      } else {
        ReadNext();
      }
      return;
    }

    std::string frame = input_buffer_.substr(0, delimiter);
    input_buffer_.erase(0, delimiter + kFrameDelimiterBytes);
    if (!frame.empty() && frame.back() == '\r') {
      frame.pop_back();
    }

    if (frame.size() > owner_.limits().max_request_bytes) {
      auto response = owner_.HandleFramingError(
          kErrorRequestTooLarge, "request frame exceeds configured bounds");
      Send(std::move(response.bytes), true);
      return;
    }
    if (request_count_ >= owner_.limits().max_requests_per_connection) {
      auto response = owner_.HandleFramingError(
          kErrorRequestLimitReached,
          "connection request limit has been reached");
      Send(std::move(response.bytes), true);
      return;
    }

    ++request_count_;
    auto response = owner_.HandleFrame(*this, frame);
    Send(std::move(response.bytes), response.close_after_write);
  }

  void Send(std::string response, const bool close_after_write) {
    if (closed_ || write_in_progress_) {
      return;
    }
    if (response.empty()) {
      Close();
      return;
    }

    write_in_progress_ = true;
    close_after_write_ = close_after_write;
    auto response_storage =
        std::make_shared<std::string>(std::move(response));
    boost::asio::async_write(
        socket_, boost::asio::buffer(*response_storage),
        [self = shared_from_this(), response_storage](
            const boost::system::error_code& error,
            const std::size_t ) {
          self->HandleWrite(response_storage, error);
        });
  }

  void HandleWrite(const std::shared_ptr<std::string>& ,
                   const boost::system::error_code& error) {
    if (closed_) {
      return;
    }
    write_in_progress_ = false;
    if (error || close_after_write_) {
      Close();
      return;
    }
    close_after_write_ = false;
    ProcessNextFrame();
  }

  Impl& owner_;
  Tcp::socket socket_;
  std::array<char, kReadChunkBytes> read_buffer_{};
  std::string input_buffer_;
  std::shared_ptr<std::atomic_bool> cancelled_{
      std::make_shared<std::atomic_bool>(false)};
  std::size_t request_count_{0};
  bool closed_{false};
  bool write_in_progress_{false};
  bool close_after_write_{false};
};

DebugControlServer::Impl::~Impl() {
  Stop();
  sessions_.clear();
  acceptor_.reset();
}

void DebugControlServer::Impl::RemoveSession(Session* const session) {
  sessions_.erase(
      std::remove_if(sessions_.begin(), sessions_.end(),
                     [session](const auto& candidate) {
                       return candidate.get() == session;
                     }),
      sessions_.end());
}

void DebugControlServer::Impl::AddSession(Tcp::socket socket) {
  if (sessions_.size() >= limits().max_connections || IsStopping()) {
    boost::system::error_code ignored;
    socket.close(ignored);
    return;
  }

  boost::system::error_code endpoint_error;
  const auto remote_endpoint = socket.remote_endpoint(endpoint_error);
  if (endpoint_error || !remote_endpoint.address().is_loopback()) {
    boost::system::error_code ignored;
    socket.close(ignored);
    return;
  }

  auto session = std::make_shared<Session>(*this, std::move(socket));
  sessions_.push_back(session);
  session->Start();
}

DebugControlStartResult DebugControlServer::Impl::Start() {
  std::unique_lock stop_lock(stop_mutex_);
  std::unique_lock lifecycle_lock(lifecycle_mutex_);
  if (state_ != LifecycleState::kStopped) {
    return MakeError(kErrorAlreadyRunning,
                      "debug-control server must be stopped before starting");
  }
  if (worker_.joinable()) {
    auto finished_worker = std::move(worker_);
    lifecycle_lock.unlock();
    finished_worker.join();
    lifecycle_lock.lock();
  }

  const auto options_error = ValidateOptions(options_);
  if (!options_error.code.empty()) {
    return options_error;
  }
  if (!codec_) {
    return MakeError(kErrorCodecMissing,
                     "a maintained JSON protocol codec is required");
  }
  std::string capability_token;
  if (!GenerateCapabilityToken(&capability_token)) {
    return MakeError(kErrorTokenGeneration,
                     "the process capability token could not be generated");
  }

  boost::system::error_code error;
  io_context_.restart();
  while (io_context_.poll() != 0U) {
  }
  io_context_.restart();
  work_guard_.emplace(io_context_.get_executor());
  stop_requested_->store(false, std::memory_order_release);
  next_request_id_ = 1U;
  acceptor_ = std::make_unique<Tcp::acceptor>(io_context_);
  const Tcp::endpoint endpoint =
      options_.loopback_address == DebugControlLoopbackAddress::kIpv6
          ? Tcp::endpoint(boost::asio::ip::address_v6::loopback(),
                          options_.port)
          : Tcp::endpoint(boost::asio::ip::address_v4::loopback(),
                          options_.port);

  acceptor_->open(endpoint.protocol(), error);
  if (error) {
    acceptor_.reset();
    work_guard_.reset();
    return MakeError(kErrorBindFailed,
                     std::string("debug-control socket open failed: ") +
                         error.message());
  }
  acceptor_->bind(endpoint, error);
  if (error) {
    acceptor_->close(error);
    acceptor_.reset();
    work_guard_.reset();
    return MakeError(kErrorBindFailed,
                     std::string("debug-control socket bind failed: ") +
                         error.message());
  }
  acceptor_->listen(static_cast<int>(limits().max_connections), error);
  if (error) {
    acceptor_->close(error);
    acceptor_.reset();
    work_guard_.reset();
    return MakeError(kErrorListenFailed,
                     std::string("debug-control socket listen failed: ") +
                         error.message());
  }

  const auto local_endpoint = acceptor_->local_endpoint(error);
  if (error || local_endpoint.port() == 0U) {
    acceptor_->close(error);
    acceptor_.reset();
    work_guard_.reset();
    return MakeError(kErrorListenFailed,
                     "the debug-control endpoint could not be discovered");
  }

  capability_token_ = std::move(capability_token);
  endpoint_ = DebugControlEndpoint{local_endpoint.address().to_string(),
                                    local_endpoint.port(), capability_token_};
  last_error_.reset();
  const auto start_result = *endpoint_;
  state_ = LifecycleState::kRunning;

  try {
    worker_ = std::thread([this] { RunIoContext(); });
  } catch (...) {
    state_ = LifecycleState::kStopped;
    stop_requested_->store(true, std::memory_order_release);
    boost::system::error_code ignored;
    acceptor_->close(ignored);
    acceptor_.reset();
    work_guard_.reset();
    io_context_.stop();
    endpoint_.reset();
    return MakeError(kErrorListenFailed,
                     "the debug-control worker thread could not start");
  }

  try {
    boost::asio::post(io_context_, [this] { AcceptNext(); });
  } catch (...) {
    stop_requested_->store(true, std::memory_order_release);
    boost::system::error_code ignored;
    acceptor_->close(ignored);
    work_guard_->reset();
    io_context_.stop();
    state_ = LifecycleState::kStopping;
    endpoint_.reset();
    lifecycle_lock.unlock();
    if (worker_.joinable()) {
      worker_.join();
    }
    lifecycle_lock.lock();
    acceptor_.reset();
    state_ = LifecycleState::kStopped;
    return MakeError(kErrorListenFailed,
                     "the debug-control accept operation could not start");
  }

  return start_result;
}

void DebugControlServer::Impl::Stop() noexcept {
  {
    std::lock_guard lifecycle_lock(lifecycle_mutex_);
    if (state_ == LifecycleState::kStopping && worker_.joinable() &&
        worker_.get_id() == std::this_thread::get_id()) {
      return;
    }
  }
  std::lock_guard stop_lock(stop_mutex_);

  std::thread worker_to_join;
  bool called_from_worker = false;

  {
    std::lock_guard lifecycle_lock(lifecycle_mutex_);
    if (state_ == LifecycleState::kStopped) {
      if (worker_.joinable() &&
          worker_.get_id() != std::this_thread::get_id()) {
        worker_to_join = std::move(worker_);
      }
    } else {
      state_ = LifecycleState::kStopping;
      stop_requested_->store(true, std::memory_order_release);
      called_from_worker =
          worker_.joinable() &&
          worker_.get_id() == std::this_thread::get_id();

      if (called_from_worker) {
        CloseAllOnIoThread();
      } else {
        try {
          boost::asio::post(io_context_, [this] { CloseAllOnIoThread(); });
        } catch (...) {
          io_context_.stop();
        }
      }
    }
  }

  if (called_from_worker) {
    return;
  }

  {
    std::lock_guard lifecycle_lock(lifecycle_mutex_);
    if (worker_.joinable() &&
        worker_.get_id() != std::this_thread::get_id()) {
      worker_to_join = std::move(worker_);
    }
  }
  if (worker_to_join.joinable()) {
    worker_to_join.join();
  }

  std::lock_guard lifecycle_lock(lifecycle_mutex_);
  boost::system::error_code ignored;
  if (acceptor_) {
    acceptor_->close(ignored);
    acceptor_.reset();
  }
  work_guard_.reset();
  endpoint_.reset();
  state_ = LifecycleState::kStopped;
}

DebugControlResponse DebugControlServer::Impl::HandleRequest(
    Session& session,
    const DebugControlRequest& request) {
  if (session.IsCancelled() || IsStopping()) {
    return FailureResponse(request.request_id,
                           MakeError(kErrorCancelled,
                                     "request cancelled during shutdown"));
  }
  return HandleRequestPayload(session, request);
}

DebugControlResponse DebugControlServer::Impl::HandleRequestPayload(
    Session& session,
    const DebugControlRequest& request) {
  const auto make_request_context =
      [this, &session, request_id = request.request_id] {
        const auto cancellation = session.CancellationState();
        const auto stopping = stop_requested_;
        return RequestContext{
            request_id,
            limits().max_response_bytes,
            [cancellation, stopping] {
              return cancellation->load(std::memory_order_acquire) ||
                     stopping->load(std::memory_order_acquire);
            }};
      };

  try {
    return std::visit(
        [this, &request, &make_request_context](const auto& payload)
            -> DebugControlResponse {
          using Payload = std::decay_t<decltype(payload)>;
          if constexpr (std::is_same_v<Payload, HealthRequest>) {
            return SuccessResponse(request.request_id, HealthResponse{});
          } else if constexpr (std::is_same_v<Payload, CapabilitiesRequest>) {
            CapabilitiesResponse response;
            response.methods = {DebugControlMethod::kHealth,
                                DebugControlMethod::kCapabilities};
            if (capabilities_.inspect_frame) {
              response.methods.push_back(DebugControlMethod::kInspectFrame);
            }
            if (capabilities_.inspect_ui) {
              response.methods.push_back(DebugControlMethod::kInspectUi);
            }
            if (capabilities_.capture_screenshot) {
              response.methods.push_back(
                  DebugControlMethod::kCaptureScreenshot);
            }
            if (capabilities_.submit_input) {
              response.methods.push_back(DebugControlMethod::kSubmitInput);
            }
            return SuccessResponse(request.request_id, std::move(response));
          } else if constexpr (std::is_same_v<Payload, InspectFrameRequest>) {
            if (!capabilities_.inspect_frame) {
              return FailureResponse(
                  request.request_id,
                  MakeError(kErrorCapabilityUnavailable,
                            "frame inspection is not attached"));
            }
            auto result =
                capabilities_.inspect_frame(make_request_context(), payload);
            if (const auto* error = std::get_if<DebugControlError>(&result)) {
              return FailureResponse(request.request_id, *error);
            }
            auto document = std::get<SerializedJson>(std::move(result));
            if (document.value.size() > limits().max_response_bytes) {
              return FailureResponse(
                  request.request_id,
                  MakeError(kErrorResponseTooLarge,
                            "frame snapshot exceeds response bounds"));
            }
            return SuccessResponse(request.request_id, std::move(document));
          } else if constexpr (std::is_same_v<Payload, InspectUiRequest>) {
            if (!capabilities_.inspect_ui) {
              return FailureResponse(
                  request.request_id,
                  MakeError(kErrorCapabilityUnavailable,
                            "UI inspection is not attached"));
            }
            auto result =
                capabilities_.inspect_ui(make_request_context(), payload);
            if (const auto* error = std::get_if<DebugControlError>(&result)) {
              return FailureResponse(request.request_id, *error);
            }
            auto document = std::get<SerializedJson>(std::move(result));
            if (document.value.size() > limits().max_response_bytes) {
              return FailureResponse(
                  request.request_id,
                  MakeError(kErrorResponseTooLarge,
                            "UI snapshot exceeds response bounds"));
            }
            return SuccessResponse(request.request_id, std::move(document));
          } else if constexpr (std::is_same_v<Payload,
                                              CaptureScreenshotRequest>) {
            if (!capabilities_.capture_screenshot) {
              return FailureResponse(
                  request.request_id,
                  MakeError(kErrorCapabilityUnavailable,
                            "screenshot capture is not attached"));
            }
            auto result = capabilities_.capture_screenshot(
                make_request_context(), payload);
            if (const auto* error = std::get_if<DebugControlError>(&result)) {
              return FailureResponse(request.request_id, *error);
            }
            auto screenshot = std::get<ScreenshotResult>(std::move(result));
            if (const auto invalid = ValidateScreenshot(screenshot, limits());
                invalid.has_value()) {
              return FailureResponse(request.request_id, *invalid);
            }
            return SuccessResponse(request.request_id, std::move(screenshot));
          } else if constexpr (std::is_same_v<Payload,
                                              SubmitInputRequest>) {
            if (!capabilities_.submit_input) {
              return FailureResponse(
                  request.request_id,
                  MakeError(kErrorCapabilityUnavailable,
                            "input submission is not attached"));
            }
            auto result = capabilities_.submit_input(
                make_request_context(), payload.event);
            if (const auto* error = std::get_if<DebugControlError>(&result)) {
              return FailureResponse(request.request_id, *error);
            }
            return SuccessResponse(
                request.request_id,
                std::get<InputSubmissionResult>(std::move(result)));
          }
        },
        request.payload);
  } catch (const std::exception&) {
    return FailureResponse(
        request.request_id,
        MakeError(kErrorAdapterError, "capability callback raised an exception"));
  } catch (...) {
    return FailureResponse(request.request_id,
                           MakeError(kErrorInternal,
                                     "capability callback failed unexpectedly"));
  }
}

DebugControlServer::Impl::EncodedResponse
DebugControlServer::Impl::HandleFrame(Session& session,
                                      const std::string_view frame) {
  const auto request_id = NextRequestId();
  if (frame.size() > limits().max_request_bytes) {
    return EncodeResponse(
        FailureResponse(request_id,
                        MakeError(kErrorRequestTooLarge,
                                  "request frame exceeds configured bounds")),
        true);
  }

  DebugControlDecodeResult decoded;
  try {
    decoded = codec_->DecodeRequest(frame, limits());
  } catch (...) {
    return EncodeResponse(
        FailureResponse(request_id,
                        MakeError(kErrorInternal,
                                  "protocol codec raised an exception")),
        true);
  }

  if (const auto* failure = std::get_if<DebugControlDecodeFailure>(&decoded)) {
    return EncodeResponse(
        FailureResponse(failure->request_id.value_or(request_id),
                        failure->error),
        failure->close_connection);
  }

  auto request = std::get<DebugControlRequest>(std::move(decoded));
  if (request.request_id == 0U) {
    request.request_id = request_id;
  }
  if (!ConstantTimeEqual(request.capability_token, capability_token_)) {
    return EncodeResponse(
        FailureResponse(
            request.request_id,
            MakeError(kErrorUnauthorized, "capability token rejected")),
        true);
  }
  return EncodeResponse(HandleRequest(session, request), false);
}

DebugControlServer::Impl::EncodedResponse
DebugControlServer::Impl::HandleFramingError(const std::string_view code,
                                             const std::string_view message) {
  return EncodeResponse(
      FailureResponse(NextRequestId(), MakeError(code, message)), true);
}

DebugControlServer::Impl::EncodedResponse
DebugControlServer::Impl::EncodeResponse(const DebugControlResponse& response,
                                          const bool close_after_write) {
  const auto encode = [this](const DebugControlResponse& value) {
    try {
      return codec_->EncodeResponse(value, limits());
    } catch (...) {
      return CapabilityResult<std::string>{
          std::in_place_type<DebugControlError>,
          MakeError(kErrorCodecEncode,
                    "protocol codec raised while encoding a response")};
    }
  };

  auto encoded = encode(response);
  if (auto* value = std::get_if<std::string>(&encoded);
      value != nullptr && IsFrameSized(*value, limits().max_response_bytes)) {
    value->push_back('\n');
    return EncodedResponse{std::move(*value), close_after_write};
  }

  const bool response_was_error =
      std::holds_alternative<DebugControlError>(response.payload);
  const auto* codec_error = std::get_if<DebugControlError>(&encoded);
  const auto fallback_code =
      codec_error != nullptr && codec_error->code == kErrorResponseTooLarge
          ? kErrorResponseTooLarge
          : (response_was_error ? kErrorCodecOutput : kErrorCodecEncode);
  const auto fallback_message =
      fallback_code == kErrorResponseTooLarge
          ? "encoded response exceeds configured bounds"
          : "protocol codec could not encode the response";
  const auto fallback = FailureResponse(
      response.request_id,
      MakeError(fallback_code, fallback_message,
                fallback_code == kErrorResponseTooLarge));
  auto fallback_encoded = encode(fallback);
  if (auto* value = std::get_if<std::string>(&fallback_encoded);
      value != nullptr && IsFrameSized(*value, limits().max_response_bytes)) {
    value->push_back('\n');
    return EncodedResponse{std::move(*value), true};
  }

  return EncodedResponse{"", true};
}

void DebugControlServer::Impl::AcceptNext() {
  if (IsStopping() || !acceptor_ || !acceptor_->is_open()) {
    return;
  }

  auto socket = std::make_shared<Tcp::socket>(io_context_);
  acceptor_->async_accept(
      *socket, [this, socket](const boost::system::error_code& error) {
        if (IsStopping()) {
          return;
        }
        if (error) {
          {
            std::lock_guard lifecycle_lock(lifecycle_mutex_);
            if (state_ == LifecycleState::kRunning) {
              last_error_ = MakeError(
                  kErrorAcceptFailed,
                  std::string("debug-control accept failed: ") +
                      error.message(),
                  true);
            }
          }
          stop_requested_->store(true, std::memory_order_release);
          CloseAllOnIoThread();
          return;
        }

        AddSession(std::move(*socket));
        AcceptNext();
      });
}

void DebugControlServer::Impl::CloseAllOnIoThread() {
  if (acceptor_) {
    boost::system::error_code ignored;
    acceptor_->cancel(ignored);
    acceptor_->close(ignored);
  }

  auto sessions = std::move(sessions_);
  for (const auto& session : sessions) {
    session->Close();
  }
  if (work_guard_.has_value()) {
    work_guard_->reset();
  }
  io_context_.stop();
}

void DebugControlServer::Impl::RunIoContext() {
  try {
    io_context_.run();
  } catch (...) {
    {
      std::lock_guard lifecycle_lock(lifecycle_mutex_);
      if (state_ == LifecycleState::kRunning) {
        last_error_ = MakeError(kErrorInternal,
                                "debug-control I/O worker failed");
      }
    }
    stop_requested_->store(true, std::memory_order_release);
  }
  CloseAllOnIoThread();
  HandleIoThreadExit();
}

void DebugControlServer::Impl::HandleIoThreadExit() {
  std::lock_guard lifecycle_lock(lifecycle_mutex_);
  if (state_ == LifecycleState::kRunning ||
      state_ == LifecycleState::kStopping) {
    state_ = LifecycleState::kStopped;
    endpoint_.reset();
  }
}

DebugControlServer::DebugControlServer(
    std::shared_ptr<const DebugControlCodec> codec,
    DebugControlCapabilities capabilities,
    DebugControlServerOptions options)
    : impl_(std::make_unique<Impl>(std::move(codec), std::move(capabilities),
                                   std::move(options))) {}

DebugControlServer::~DebugControlServer() = default;

DebugControlStartResult DebugControlServer::Start() {
  return impl_->Start();
}

void DebugControlServer::Stop() noexcept {
  impl_->Stop();
}

bool DebugControlServer::IsRunning() const noexcept {
  return impl_->IsRunning();
}

std::optional<DebugControlEndpoint> DebugControlServer::Endpoint() const {
  return impl_->Endpoint();
}

std::optional<DebugControlError> DebugControlServer::LastError() const {
  return impl_->LastError();
}

}

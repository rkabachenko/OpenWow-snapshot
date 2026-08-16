#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace openwow::debug::control {

inline constexpr std::string_view kDebugControlHealthMethod = "health";
inline constexpr std::string_view kDebugControlCapabilitiesMethod =
    "capabilities";
inline constexpr std::string_view kDebugControlInspectFrameMethod =
    "inspect.frame";
inline constexpr std::string_view kDebugControlInspectUiMethod = "inspect.ui";
inline constexpr std::string_view kDebugControlCaptureScreenshotMethod =
    "capture.screenshot";
inline constexpr std::string_view kDebugControlSubmitInputMethod =
    "input.submit";

using RequestId = std::uint64_t;

struct SerializedJson {
  std::string value;
};

struct DebugControlError {
  std::string code;
  std::string message;
  bool retryable{false};
};

template <typename Value>
using CapabilityResult = std::variant<Value, DebugControlError>;

using CancellationCheck = std::function<bool()>;

struct RequestContext {
  RequestId request_id{0};
  std::size_t max_result_bytes{0};

  CancellationCheck is_cancellation_requested;
};

struct MouseMotionInput {
  std::uint32_t timestamp_ms{0};
  std::uint32_t window_id{0};
  std::uint32_t device_id{0};
  std::uint32_t button_mask{0};
  std::int32_t x_pixels{0};
  std::int32_t y_pixels{0};
  std::int32_t relative_x_pixels{0};
  std::int32_t relative_y_pixels{0};
};

struct MouseButtonInput {
  std::uint32_t timestamp_ms{0};
  std::uint32_t window_id{0};
  std::uint32_t device_id{0};
  std::uint8_t button{0};
  bool pressed{false};
  std::uint8_t click_count{0};
  std::int32_t x_pixels{0};
  std::int32_t y_pixels{0};
};

struct MouseWheelInput {
  std::uint32_t timestamp_ms{0};
  std::uint32_t window_id{0};
  std::uint32_t device_id{0};
  std::int32_t scroll_x_lines{0};
  std::int32_t scroll_y_lines{0};
  float precise_scroll_x_lines{0.0F};
  float precise_scroll_y_lines{0.0F};
  bool direction_flipped{false};
};

struct KeyboardInput {
  std::uint32_t timestamp_ms{0};
  std::uint32_t window_id{0};
  std::uint32_t scancode{0};
  std::int32_t keycode{0};
  std::uint16_t modifiers{0};
  bool pressed{false};
  bool repeat{false};
};

struct TextInput {
  std::uint32_t timestamp_ms{0};
  std::uint32_t window_id{0};
  std::string utf8_text;
};

enum class WindowEventKind : std::uint8_t {
  kShown,
  kHidden,
  kExposed,
  kMoved,
  kResized,
  kSizeChanged,
  kMinimized,
  kMaximized,
  kRestored,
  kMouseEnter,
  kMouseLeave,
  kFocusGained,
  kFocusLost,
  kClose,
  kTakeFocus,
  kHitTest,
};

struct WindowInput {
  std::uint32_t timestamp_ms{0};
  std::uint32_t window_id{0};
  WindowEventKind event{WindowEventKind::kShown};
  std::int32_t data1{0};
  std::int32_t data2{0};
};

using InputEvent = std::variant<MouseMotionInput, MouseButtonInput,
                                MouseWheelInput, KeyboardInput, TextInput,
                                WindowInput>;

struct InputSubmissionResult {
  bool accepted{false};
};

struct ScreenshotBytes {
  std::vector<std::uint8_t> bytes;
  std::string media_type;
};

struct ScreenshotArtifact {
  std::string path;
  std::string media_type;
};

using ScreenshotResult = std::variant<ScreenshotBytes, ScreenshotArtifact>;

struct HealthRequest {};
struct CapabilitiesRequest {};
struct InspectFrameRequest {};
struct InspectUiRequest {

  std::string selector;
  std::size_t max_results{128U};
  bool include_lua{true};
  bool include_ancestors{true};

  std::optional<std::int32_t> x_pixels;
  std::optional<std::int32_t> y_pixels;
};
struct CaptureScreenshotRequest {};
struct SubmitInputRequest {
  InputEvent event;
};

struct DebugControlCapabilities {

  std::function<CapabilityResult<SerializedJson>(
      const RequestContext&, const InspectFrameRequest&)>
      inspect_frame;
  std::function<CapabilityResult<SerializedJson>(
      const RequestContext&, const InspectUiRequest&)>
      inspect_ui;
  std::function<CapabilityResult<ScreenshotResult>(
      const RequestContext&, const CaptureScreenshotRequest&)>
      capture_screenshot;
  std::function<CapabilityResult<InputSubmissionResult>(
      const RequestContext&, const InputEvent&)>
      submit_input;
};

enum class DebugControlMethod : std::uint8_t {
  kHealth,
  kCapabilities,
  kInspectFrame,
  kInspectUi,
  kCaptureScreenshot,
  kSubmitInput,
};

using DebugControlRequestPayload =
    std::variant<HealthRequest, CapabilitiesRequest, InspectFrameRequest,
                 InspectUiRequest, CaptureScreenshotRequest,
                 SubmitInputRequest>;

struct DebugControlRequest {

  RequestId request_id{0};
  std::string capability_token;
  DebugControlRequestPayload payload;
};

struct DebugControlDecodeFailure {
  std::optional<RequestId> request_id;
  DebugControlError error;
  bool close_connection{false};
};

using DebugControlDecodeResult =
    std::variant<DebugControlRequest, DebugControlDecodeFailure>;

struct HealthResponse {
  bool healthy{true};
};

struct CapabilitiesResponse {
  std::vector<DebugControlMethod> methods;
};

using DebugControlSuccess =
    std::variant<HealthResponse, CapabilitiesResponse, SerializedJson,
                 ScreenshotResult, InputSubmissionResult>;

struct DebugControlResponse {
  RequestId request_id{0};
  CapabilityResult<DebugControlSuccess> payload;
};

struct DebugControlServerLimits;

class DebugControlCodec {
 public:
  virtual ~DebugControlCodec() = default;

  [[nodiscard]] virtual DebugControlDecodeResult DecodeRequest(
      std::string_view frame,
      const DebugControlServerLimits& limits) const = 0;

  [[nodiscard]] virtual CapabilityResult<std::string> EncodeResponse(
      const DebugControlResponse& response,
      const DebugControlServerLimits& limits) const = 0;
};

inline constexpr std::size_t kDefaultMaxRequestBytes = 64U * 1024U;
inline constexpr std::size_t kDefaultMaxResponseBytes = 16U * 1024U * 1024U;
inline constexpr std::size_t kDefaultMaxScreenshotBytes = 8U * 1024U * 1024U;
inline constexpr std::size_t kDefaultMaxArtifactPathBytes = 4U * 1024U;
inline constexpr std::size_t kDefaultMaxStringBytes = 4U * 1024U;
inline constexpr std::size_t kDefaultMaxConnections = 8U;
inline constexpr std::size_t kDefaultMaxRequestsPerConnection = 4096U;
inline constexpr std::size_t kDefaultMaxJsonDepth = 32U;
inline constexpr std::size_t kDefaultMaxJsonMembers = 4096U;

struct DebugControlServerLimits {
  std::size_t max_request_bytes{kDefaultMaxRequestBytes};
  std::size_t max_response_bytes{kDefaultMaxResponseBytes};
  std::size_t max_screenshot_bytes{kDefaultMaxScreenshotBytes};
  std::size_t max_artifact_path_bytes{kDefaultMaxArtifactPathBytes};
  std::size_t max_string_bytes{kDefaultMaxStringBytes};
  std::size_t max_connections{kDefaultMaxConnections};
  std::size_t max_requests_per_connection{
      kDefaultMaxRequestsPerConnection};
  std::size_t max_json_depth{kDefaultMaxJsonDepth};
  std::size_t max_json_members{kDefaultMaxJsonMembers};
};

enum class DebugControlLoopbackAddress : std::uint8_t {
  kIpv4,
  kIpv6,
};

struct DebugControlServerOptions {
  DebugControlLoopbackAddress loopback_address{
      DebugControlLoopbackAddress::kIpv4};
  std::uint16_t port{0};
  DebugControlServerLimits limits;
};

struct DebugControlEndpoint {
  std::string address;
  std::uint16_t port{0};
  std::string capability_token;
};

using DebugControlStartResult =
    std::variant<DebugControlEndpoint, DebugControlError>;

class DebugControlServer {
 public:
  explicit DebugControlServer(
      std::shared_ptr<const DebugControlCodec> codec,
      DebugControlCapabilities capabilities,
      DebugControlServerOptions options = {});
  ~DebugControlServer();

  DebugControlServer(const DebugControlServer&) = delete;
  DebugControlServer& operator=(const DebugControlServer&) = delete;
  DebugControlServer(DebugControlServer&&) = delete;
  DebugControlServer& operator=(DebugControlServer&&) = delete;

  [[nodiscard]] DebugControlStartResult Start();
  void Stop() noexcept;

  [[nodiscard]] bool IsRunning() const noexcept;
  [[nodiscard]] std::optional<DebugControlEndpoint> Endpoint() const;
  [[nodiscard]] std::optional<DebugControlError> LastError() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}

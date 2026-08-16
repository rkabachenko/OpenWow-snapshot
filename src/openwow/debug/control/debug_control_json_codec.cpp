#include "openwow/debug/control/debug_control_json_codec.h"

#include <boost/json.hpp>
#include <boost/json/basic_parser_impl.hpp>
#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace openwow::debug::control {
namespace {

namespace json = boost::json;
using JsonObject = json::object;
using JsonValue = json::value;

constexpr std::size_t kMaxErrorCodeBytes = 64U;
constexpr std::size_t kMaxErrorMessageBytes = 512U;
constexpr std::size_t kMaxMediaTypeBytes = 128U;

constexpr std::string_view kErrorRequestTooLarge = "request_too_large";
constexpr std::string_view kErrorInvalidJson = "invalid_json";
constexpr std::string_view kErrorJsonDepthExceeded = "json_depth_exceeded";
constexpr std::string_view kErrorJsonMembersExceeded =
    "json_members_exceeded";
constexpr std::string_view kErrorDuplicateField = "duplicate_field";
constexpr std::string_view kErrorUnknownField = "unknown_field";
constexpr std::string_view kErrorMissingField = "missing_field";
constexpr std::string_view kErrorInvalidFieldType = "invalid_field_type";
constexpr std::string_view kErrorInvalidFieldValue = "invalid_field_value";
constexpr std::string_view kErrorUnknownMethod = "unknown_method";
constexpr std::string_view kErrorInvalidRequest = "invalid_request";
constexpr std::string_view kErrorInvalidInputEvent = "invalid_input_event";
constexpr std::string_view kErrorInvalidSnapshot = "invalid_snapshot";
constexpr std::string_view kErrorInvalidScreenshot =
    "invalid_screenshot_result";
constexpr std::string_view kErrorInvalidResponse = "invalid_response";
constexpr std::string_view kErrorResponseTooLarge = "response_too_large";
constexpr std::string_view kErrorCodecFailure = "codec_failure";

constexpr std::string_view kInputMouseMotion = "mouse_motion";
constexpr std::string_view kInputMouseButton = "mouse_button";
constexpr std::string_view kInputMouseWheel = "mouse_wheel";
constexpr std::string_view kInputKey = "key";
constexpr std::string_view kInputText = "text";
constexpr std::string_view kInputWindow = "window";

constexpr std::array<std::pair<std::string_view, WindowEventKind>, 16>
    kWindowEventNames{{
        {"shown", WindowEventKind::kShown},
        {"hidden", WindowEventKind::kHidden},
        {"exposed", WindowEventKind::kExposed},
        {"moved", WindowEventKind::kMoved},
        {"resized", WindowEventKind::kResized},
        {"size_changed", WindowEventKind::kSizeChanged},
        {"minimized", WindowEventKind::kMinimized},
        {"maximized", WindowEventKind::kMaximized},
        {"restored", WindowEventKind::kRestored},
        {"mouse_enter", WindowEventKind::kMouseEnter},
        {"mouse_leave", WindowEventKind::kMouseLeave},
        {"focus_gained", WindowEventKind::kFocusGained},
        {"focus_lost", WindowEventKind::kFocusLost},
        {"close", WindowEventKind::kClose},
        {"take_focus", WindowEventKind::kTakeFocus},
        {"hit_test", WindowEventKind::kHitTest},
    }};

[[nodiscard]] DebugControlError MakeError(const std::string_view code,
                                          const std::string_view message,
                                          const bool retryable = false) {
  return DebugControlError{std::string(code), std::string(message), retryable};
}

[[nodiscard]] DebugControlDecodeResult DecodeFailure(
    std::optional<RequestId> request_id,
    DebugControlError error,
    const bool close_connection = false) {
  return DebugControlDecodeFailure{std::move(request_id), std::move(error),
                                   close_connection};
}

template <typename Value>
[[nodiscard]] CapabilityResult<Value> ValueFailure(DebugControlError error) {
  return CapabilityResult<Value>{std::in_place_type<DebugControlError>,
                                 std::move(error)};
}

template <typename Value>
[[nodiscard]] CapabilityResult<Value> ValueSuccess(Value value) {
  return CapabilityResult<Value>{std::in_place_type<Value>, std::move(value)};
}

[[nodiscard]] std::string_view ToStringView(const json::string_view value) {
  return std::string_view(value.data(), value.size());
}

[[nodiscard]] std::string_view ToStringView(const json::string& value) {
  return std::string_view(value.data(), value.size());
}

[[nodiscard]] bool ContainsControlCharacter(const std::string_view value) {
  return std::any_of(value.begin(), value.end(), [](const char character) {
    return static_cast<unsigned char>(character) < 0x20U;
  });
}

[[nodiscard]] bool IsValidUtf8(const std::string_view value) {
  const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
  std::size_t index = 0;
  while (index < value.size()) {
    const auto first = bytes[index++];
    if (first <= 0x7fU) {
      continue;
    }
    if (first >= 0xc2U && first <= 0xdfU) {
      if (index >= value.size() || (bytes[index++] & 0xc0U) != 0x80U) {
        return false;
      }
      continue;
    }
    if (first >= 0xe0U && first <= 0xefU) {
      if (index + 1U >= value.size()) {
        return false;
      }
      const auto second = bytes[index++];
      const auto third = bytes[index++];
      if ((third & 0xc0U) != 0x80U ||
          (first == 0xe0U ? second < 0xa0U || second > 0xbfU
                          : first == 0xedU ? second < 0x80U || second > 0x9fU
                                           : (second & 0xc0U) != 0x80U)) {
        return false;
      }
      continue;
    }
    if (first >= 0xf0U && first <= 0xf4U) {
      if (index + 2U >= value.size()) {
        return false;
      }
      const auto second = bytes[index++];
      const auto third = bytes[index++];
      const auto fourth = bytes[index++];
      if ((third & 0xc0U) != 0x80U || (fourth & 0xc0U) != 0x80U ||
          (first == 0xf0U ? second < 0x90U || second > 0xbfU
                          : first == 0xf4U ? second < 0x80U || second > 0x8fU
                                           : (second & 0xc0U) != 0x80U)) {
        return false;
      }
      continue;
    }
    return false;
  }
  return true;
}

[[nodiscard]] bool IsBoundedString(const std::string_view value,
                                   const std::size_t max_bytes,
                                   const bool allow_empty,
                                   const bool reject_controls) {
  return (allow_empty || !value.empty()) && value.size() <= max_bytes &&
         IsValidUtf8(value) &&
         (!reject_controls || !ContainsControlCharacter(value));
}

[[nodiscard]] const JsonValue* FindValue(const JsonObject& object,
                                         const std::string_view name) {
  const auto iterator = object.find(json::string_view(name.data(), name.size()));
  return iterator == object.end() ? nullptr : &iterator->value();
}

[[nodiscard]] std::optional<DebugControlError> ValidateObjectShape(
    const JsonObject& object,
    const std::initializer_list<std::string_view> fields,
    const std::initializer_list<std::string_view> optional_fields = {}) {
  for (const auto& member : object) {
    const auto name = ToStringView(member.key());
    const auto allowed = std::any_of(
        fields.begin(), fields.end(),
        [name](const std::string_view field) { return field == name; });
    if (!allowed) {
      return MakeError(kErrorUnknownField, "object contains an unknown field");
    }
  }

  for (const auto field : fields) {
    const auto optional = std::any_of(
        optional_fields.begin(), optional_fields.end(),
        [field](const std::string_view candidate) { return candidate == field; });
    if (!optional && FindValue(object, field) == nullptr) {
      return MakeError(kErrorMissingField, "object is missing a required field");
    }
  }
  return std::nullopt;
}

enum class ParseViolation : std::uint8_t {
  kNone,
  kDuplicateField,
  kMemberLimit,
  kStringLimit,
};

class StrictJsonHandler final {
 public:
  static constexpr std::size_t max_object_size =
      std::numeric_limits<std::size_t>::max();
  static constexpr std::size_t max_array_size =
      std::numeric_limits<std::size_t>::max();
  static constexpr std::size_t max_key_size =
      std::numeric_limits<std::size_t>::max();
  static constexpr std::size_t max_string_size =
      std::numeric_limits<std::size_t>::max();

  StrictJsonHandler(const std::size_t max_string_bytes,
                    const std::size_t max_members)
      : max_string_bytes_(max_string_bytes), max_members_(max_members) {}

  [[nodiscard]] ParseViolation violation() const noexcept {
    return violation_;
  }

  bool on_document_begin(boost::system::error_code&) { return true; }
  bool on_document_end(boost::system::error_code&) { return true; }

  bool on_array_begin(boost::system::error_code&) { return true; }
  bool on_array_end(const std::size_t size, boost::system::error_code& error) {
    if (size > max_members_ - std::min(member_count_, max_members_)) {
      return Fail(ParseViolation::kMemberLimit, json::error::array_too_large,
                  error);
    }
    member_count_ += size;
    return true;
  }

  bool on_object_begin(boost::system::error_code&) {
    object_keys_.emplace_back();
    return true;
  }

  bool on_object_end(const std::size_t, boost::system::error_code&) {
    if (!object_keys_.empty()) {
      object_keys_.pop_back();
    }
    return true;
  }

  bool on_key_part(const json::string_view value,
                  const std::size_t total_size,
                  boost::system::error_code& error) {
    if (total_size > max_string_bytes_) {
      return Fail(ParseViolation::kStringLimit, json::error::key_too_large,
                  error);
    }
    key_buffer_.append(value.data(), value.size());
    return true;
  }

  bool on_key(const json::string_view value,
              const std::size_t total_size,
              boost::system::error_code& error) {
    if (total_size > max_string_bytes_) {
      return Fail(ParseViolation::kStringLimit, json::error::key_too_large,
                  error);
    }
    key_buffer_.append(value.data(), value.size());
    if (member_count_ >= max_members_) {
      return Fail(ParseViolation::kMemberLimit, json::error::object_too_large,
                  error);
    }
    ++member_count_;

    if (object_keys_.empty()) {
      key_buffer_.clear();
      return true;
    }
    if (!object_keys_.back().emplace(key_buffer_).second) {
      return Fail(ParseViolation::kDuplicateField, json::error::unknown_name,
                  error);
    }
    key_buffer_.clear();
    return true;
  }

  bool on_string_part(const json::string_view,
                      const std::size_t total_size,
                      boost::system::error_code& error) {
    return CheckStringSize(total_size, error);
  }

  bool on_string(const json::string_view,
                 const std::size_t total_size,
                 boost::system::error_code& error) {
    return CheckStringSize(total_size, error);
  }

  bool on_number_part(const json::string_view, boost::system::error_code&) {
    return true;
  }
  bool on_int64(const std::int64_t,
                const json::string_view,
                boost::system::error_code&) {
    return true;
  }
  bool on_uint64(const std::uint64_t,
                 const json::string_view,
                 boost::system::error_code&) {
    return true;
  }
  bool on_double(const double,
                 const json::string_view,
                 boost::system::error_code&) {
    return true;
  }
  bool on_bool(const bool, boost::system::error_code&) { return true; }
  bool on_null(boost::system::error_code&) { return true; }

  bool on_comment_part(const json::string_view, boost::system::error_code&) {
    return true;
  }
  bool on_comment(const json::string_view, boost::system::error_code&) {
    return true;
  }

 private:
  bool CheckStringSize(const std::size_t total_size,
                       boost::system::error_code& error) {
    if (total_size > max_string_bytes_) {
      return Fail(ParseViolation::kStringLimit, json::error::string_too_large,
                  error);
    }
    return true;
  }

  bool Fail(const ParseViolation violation,
            const json::error parse_error,
            boost::system::error_code& error) {
    violation_ = violation;
    error = json::make_error_code(parse_error);
    return false;
  }

  const std::size_t max_string_bytes_;
  const std::size_t max_members_;
  std::size_t member_count_{0};
  ParseViolation violation_{ParseViolation::kNone};
  std::string key_buffer_;
  std::vector<std::unordered_set<std::string>> object_keys_;
};

[[nodiscard]] bool IsJsonError(const boost::system::error_code& error,
                               const json::error expected) {
  return error == json::make_error_code(expected);
}

[[nodiscard]] DebugControlError ParseError(
    const ParseViolation violation,
    const boost::system::error_code& parser_error) {
  switch (violation) {
    case ParseViolation::kDuplicateField:
      return MakeError(kErrorDuplicateField,
                       "JSON objects must not contain duplicate fields");
    case ParseViolation::kMemberLimit:
      return MakeError(kErrorJsonMembersExceeded,
                       "JSON member count exceeds configured bounds");
    case ParseViolation::kStringLimit:
      return MakeError(kErrorInvalidFieldValue,
                       "JSON string exceeds configured bounds");
    case ParseViolation::kNone:
      break;
  }

  if (IsJsonError(parser_error, json::error::too_deep)) {
    return MakeError(kErrorJsonDepthExceeded,
                     "JSON nesting depth exceeds configured bounds");
  }
  if (IsJsonError(parser_error, json::error::object_too_large) ||
      IsJsonError(parser_error, json::error::array_too_large)) {
    return MakeError(kErrorJsonMembersExceeded,
                     "JSON member count exceeds configured bounds");
  }
  if (IsJsonError(parser_error, json::error::key_too_large) ||
      IsJsonError(parser_error, json::error::string_too_large)) {
    return MakeError(kErrorInvalidFieldValue,
                     "JSON string exceeds configured bounds");
  }
  return MakeError(kErrorInvalidJson, "frame is not strict JSON");
}

[[nodiscard]] std::optional<DebugControlError> ParseJsonDocument(
    const std::string_view text,
    const DebugControlServerLimits& limits,
    JsonValue* const document) {
  if (text.empty()) {
    return MakeError(kErrorInvalidJson, "JSON document is empty");
  }

  json::parse_options options;
  options.max_depth = limits.max_json_depth;

  json::basic_parser<StrictJsonHandler> validator(options,
                                                   limits.max_string_bytes,
                                                   limits.max_json_members);
  boost::system::error_code parser_error;
  const auto consumed = validator.write_some(
      false, text.data(), text.size(), parser_error);
  if (!parser_error && consumed != text.size()) {
    parser_error = json::make_error_code(json::error::extra_data);
  }
  if (!parser_error && !validator.done()) {
    parser_error = json::make_error_code(json::error::incomplete);
  }
  if (parser_error || validator.handler().violation() != ParseViolation::kNone) {
    return ParseError(validator.handler().violation(), parser_error);
  }

  boost::system::error_code dom_error;
  *document = json::parse(json::string_view(text.data(), text.size()),
                          dom_error, {}, options);
  if (dom_error) {
    return ParseError(ParseViolation::kNone, dom_error);
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<DebugControlMethod> ParseMethod(
    const std::string_view method) {
  if (method == kDebugControlHealthMethod) {
    return DebugControlMethod::kHealth;
  }
  if (method == kDebugControlCapabilitiesMethod) {
    return DebugControlMethod::kCapabilities;
  }
  if (method == kDebugControlInspectFrameMethod) {
    return DebugControlMethod::kInspectFrame;
  }
  if (method == kDebugControlInspectUiMethod) {
    return DebugControlMethod::kInspectUi;
  }
  if (method == kDebugControlCaptureScreenshotMethod) {
    return DebugControlMethod::kCaptureScreenshot;
  }
  if (method == kDebugControlSubmitInputMethod) {
    return DebugControlMethod::kSubmitInput;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string_view> MethodName(
    const DebugControlMethod method) {
  switch (method) {
    case DebugControlMethod::kHealth:
      return kDebugControlHealthMethod;
    case DebugControlMethod::kCapabilities:
      return kDebugControlCapabilitiesMethod;
    case DebugControlMethod::kInspectFrame:
      return kDebugControlInspectFrameMethod;
    case DebugControlMethod::kInspectUi:
      return kDebugControlInspectUiMethod;
    case DebugControlMethod::kCaptureScreenshot:
      return kDebugControlCaptureScreenshotMethod;
    case DebugControlMethod::kSubmitInput:
      return kDebugControlSubmitInputMethod;
  }
  return std::nullopt;
}

[[nodiscard]] bool ReadRequestId(const JsonValue& value, RequestId* const id) {
  if (value.is_uint64()) {
    if (value.as_uint64() == 0U) {
      return false;
    }
    *id = value.as_uint64();
    return true;
  }
  if (value.is_int64() && value.as_int64() > 0) {
    *id = static_cast<RequestId>(value.as_int64());
    return true;
  }
  return false;
}

[[nodiscard]] bool ReadStringField(const JsonObject& object,
                                   const std::string_view name,
                                   const std::size_t max_bytes,
                                   const bool allow_empty,
                                   const bool reject_controls,
                                   std::string_view* const output) {
  const auto* value = FindValue(object, name);
  if (value == nullptr || !value->is_string()) {
    return false;
  }
  const auto text = ToStringView(value->as_string());
  if (!IsBoundedString(text, max_bytes, allow_empty, reject_controls)) {
    return false;
  }
  *output = text;
  return true;
}

template <typename Unsigned>
[[nodiscard]] bool ReadUnsignedField(const JsonObject& object,
                                     const std::string_view name,
                                     Unsigned* const output) {
  static_assert(std::is_unsigned_v<Unsigned>);
  const auto* value = FindValue(object, name);
  if (value == nullptr) {
    return false;
  }
  std::uint64_t number = 0;
  if (value->is_uint64()) {
    number = value->as_uint64();
  } else if (value->is_int64() && value->as_int64() >= 0) {
    number = static_cast<std::uint64_t>(value->as_int64());
  } else {
    return false;
  }
  if (number >
      static_cast<std::uint64_t>(std::numeric_limits<Unsigned>::max())) {
    return false;
  }
  *output = static_cast<Unsigned>(number);
  return true;
}

template <typename Signed>
[[nodiscard]] bool ReadSignedField(const JsonObject& object,
                                   const std::string_view name,
                                   Signed* const output) {
  static_assert(std::is_signed_v<Signed>);
  const auto* value = FindValue(object, name);
  if (value == nullptr) {
    return false;
  }

  const auto minimum = static_cast<std::int64_t>(
      std::numeric_limits<Signed>::lowest());
  const auto maximum =
      static_cast<std::int64_t>(std::numeric_limits<Signed>::max());
  if (value->is_int64()) {
    const auto number = value->as_int64();
    if (number < minimum || number > maximum) {
      return false;
    }
    *output = static_cast<Signed>(number);
    return true;
  }
  if (value->is_uint64() && value->as_uint64() <=
                                 static_cast<std::uint64_t>(maximum)) {
    *output = static_cast<Signed>(value->as_uint64());
    return true;
  }
  return false;
}

[[nodiscard]] bool ReadFloatField(const JsonObject& object,
                                  const std::string_view name,
                                  float* const output) {
  const auto* value = FindValue(object, name);
  if (value == nullptr) {
    return false;
  }

  long double number = 0.0L;
  if (value->is_double()) {
    number = static_cast<long double>(value->as_double());
  } else if (value->is_int64()) {
    number = static_cast<long double>(value->as_int64());
  } else if (value->is_uint64()) {
    number = static_cast<long double>(value->as_uint64());
  } else {
    return false;
  }

  constexpr auto maximum =
      static_cast<long double>(std::numeric_limits<float>::max());
  if (!std::isfinite(number) || number < -maximum || number > maximum) {
    return false;
  }
  const auto converted = static_cast<float>(number);
  if (!std::isfinite(converted) || (converted == 0.0F && number != 0.0L)) {
    return false;
  }
  *output = converted;
  return true;
}

[[nodiscard]] bool ReadBooleanField(const JsonObject& object,
                                    const std::string_view name,
                                    bool* const output) {
  const auto* value = FindValue(object, name);
  if (value == nullptr || !value->is_bool()) {
    return false;
  }
  *output = value->as_bool();
  return true;
}

[[nodiscard]] std::optional<WindowEventKind> ParseWindowEvent(
    const std::string_view name) {
  const auto iterator = std::find_if(
      kWindowEventNames.begin(), kWindowEventNames.end(),
      [name](const auto& entry) { return entry.first == name; });
  return iterator == kWindowEventNames.end()
             ? std::nullopt
             : std::optional<WindowEventKind>(iterator->second);
}

[[nodiscard]] CapabilityResult<InputEvent> InputEventFailure(
    const std::string_view message) {
  return ValueFailure<InputEvent>(MakeError(kErrorInvalidInputEvent, message));
}

[[nodiscard]] CapabilityResult<InputEvent> ParseInputEvent(
    const JsonObject& object,
    const DebugControlServerLimits& limits) {
  std::string_view type;
  if (!ReadStringField(object, "type", limits.max_string_bytes, false, true,
                       &type)) {
    return InputEventFailure("input event type is invalid");
  }

  if (type == kInputMouseMotion) {
    if (const auto shape = ValidateObjectShape(
            object,
            {"type", "timestamp_ms", "window_id", "device_id",
             "button_mask", "x_pixels", "y_pixels", "relative_x_pixels",
             "relative_y_pixels"});
        shape.has_value()) {
      return ValueFailure<InputEvent>(*shape);
    }
    MouseMotionInput event;
    if (!ReadUnsignedField(object, "timestamp_ms", &event.timestamp_ms) ||
        !ReadUnsignedField(object, "window_id", &event.window_id) ||
        !ReadUnsignedField(object, "device_id", &event.device_id) ||
        !ReadUnsignedField(object, "button_mask", &event.button_mask) ||
        !ReadSignedField(object, "x_pixels", &event.x_pixels) ||
        !ReadSignedField(object, "y_pixels", &event.y_pixels) ||
        !ReadSignedField(object, "relative_x_pixels",
                         &event.relative_x_pixels) ||
        !ReadSignedField(object, "relative_y_pixels",
                         &event.relative_y_pixels)) {
      return InputEventFailure("mouse motion field is invalid");
    }
    return ValueSuccess<InputEvent>(InputEvent{std::move(event)});
  }

  if (type == kInputMouseButton) {
    if (const auto shape = ValidateObjectShape(
            object,
            {"type", "timestamp_ms", "window_id", "device_id", "button",
             "pressed", "click_count", "x_pixels", "y_pixels"});
        shape.has_value()) {
      return ValueFailure<InputEvent>(*shape);
    }
    MouseButtonInput event;
    if (!ReadUnsignedField(object, "timestamp_ms", &event.timestamp_ms) ||
        !ReadUnsignedField(object, "window_id", &event.window_id) ||
        !ReadUnsignedField(object, "device_id", &event.device_id) ||
        !ReadUnsignedField(object, "button", &event.button) ||
        !ReadBooleanField(object, "pressed", &event.pressed) ||
        !ReadUnsignedField(object, "click_count", &event.click_count) ||
        !ReadSignedField(object, "x_pixels", &event.x_pixels) ||
        !ReadSignedField(object, "y_pixels", &event.y_pixels)) {
      return InputEventFailure("mouse button field is invalid");
    }
    return ValueSuccess<InputEvent>(InputEvent{std::move(event)});
  }

  if (type == kInputMouseWheel) {
    if (const auto shape = ValidateObjectShape(
            object,
            {"type", "timestamp_ms", "window_id", "device_id",
             "scroll_x_lines", "scroll_y_lines", "precise_scroll_x_lines",
             "precise_scroll_y_lines", "direction_flipped"});
        shape.has_value()) {
      return ValueFailure<InputEvent>(*shape);
    }
    MouseWheelInput event;
    if (!ReadUnsignedField(object, "timestamp_ms", &event.timestamp_ms) ||
        !ReadUnsignedField(object, "window_id", &event.window_id) ||
        !ReadUnsignedField(object, "device_id", &event.device_id) ||
        !ReadSignedField(object, "scroll_x_lines", &event.scroll_x_lines) ||
        !ReadSignedField(object, "scroll_y_lines", &event.scroll_y_lines) ||
        !ReadFloatField(object, "precise_scroll_x_lines",
                        &event.precise_scroll_x_lines) ||
        !ReadFloatField(object, "precise_scroll_y_lines",
                        &event.precise_scroll_y_lines) ||
        !ReadBooleanField(object, "direction_flipped",
                          &event.direction_flipped)) {
      return InputEventFailure("mouse wheel field is invalid");
    }
    return ValueSuccess<InputEvent>(InputEvent{std::move(event)});
  }

  if (type == kInputKey) {
    if (const auto shape = ValidateObjectShape(
            object,
            {"type", "timestamp_ms", "window_id", "scancode", "keycode",
             "modifiers", "pressed", "repeat"});
        shape.has_value()) {
      return ValueFailure<InputEvent>(*shape);
    }
    KeyboardInput event;
    if (!ReadUnsignedField(object, "timestamp_ms", &event.timestamp_ms) ||
        !ReadUnsignedField(object, "window_id", &event.window_id) ||
        !ReadUnsignedField(object, "scancode", &event.scancode) ||
        !ReadSignedField(object, "keycode", &event.keycode) ||
        !ReadUnsignedField(object, "modifiers", &event.modifiers) ||
        !ReadBooleanField(object, "pressed", &event.pressed) ||
        !ReadBooleanField(object, "repeat", &event.repeat)) {
      return InputEventFailure("key event field is invalid");
    }
    return ValueSuccess<InputEvent>(InputEvent{std::move(event)});
  }

  if (type == kInputText) {
    if (const auto shape = ValidateObjectShape(
            object, {"type", "timestamp_ms", "window_id", "text"});
        shape.has_value()) {
      return ValueFailure<InputEvent>(*shape);
    }
    TextInput event;
    std::string_view text;
    if (!ReadUnsignedField(object, "timestamp_ms", &event.timestamp_ms) ||
        !ReadUnsignedField(object, "window_id", &event.window_id) ||
        !ReadStringField(object, "text", limits.max_string_bytes, true, false,
                         &text)) {
      return InputEventFailure("text event field is invalid");
    }
    event.utf8_text = std::string(text);
    return ValueSuccess<InputEvent>(InputEvent{std::move(event)});
  }

  if (type == kInputWindow) {
    if (const auto shape = ValidateObjectShape(
            object,
            {"type", "timestamp_ms", "window_id", "event", "data1",
             "data2"});
        shape.has_value()) {
      return ValueFailure<InputEvent>(*shape);
    }
    WindowInput event;
    std::string_view event_name;
    if (!ReadUnsignedField(object, "timestamp_ms", &event.timestamp_ms) ||
        !ReadUnsignedField(object, "window_id", &event.window_id) ||
        !ReadStringField(object, "event", limits.max_string_bytes, false, true,
                         &event_name) ||
        !ReadSignedField(object, "data1", &event.data1) ||
        !ReadSignedField(object, "data2", &event.data2)) {
      return InputEventFailure("window event field is invalid");
    }
    const auto event_kind = ParseWindowEvent(event_name);
    if (!event_kind.has_value()) {
      return InputEventFailure("window event name is invalid");
    }
    event.event = *event_kind;
    return ValueSuccess<InputEvent>(InputEvent{std::move(event)});
  }

  return InputEventFailure("input event type is unknown");
}

[[nodiscard]] DebugControlDecodeResult DecodeRequestDocument(
    const JsonValue& document,
    const DebugControlServerLimits& limits) {
  if (!document.is_object()) {
    return DecodeFailure(
        std::nullopt,
        MakeError(kErrorInvalidRequest, "request document must be an object"));
  }
  const auto& root = document.as_object();

  std::optional<RequestId> request_id;
  if (const auto* id = FindValue(root, "id"); id != nullptr) {
    RequestId parsed_id = 0;
    if (!ReadRequestId(*id, &parsed_id)) {
      return DecodeFailure(
          std::nullopt,
          MakeError(kErrorInvalidFieldValue,
                    "request id must be a positive unsigned integer"));
    }
    request_id = parsed_id;
  }

  if (const auto shape = ValidateObjectShape(
          root, {"id", "capability", "method", "params"}, {"id", "params"});
      shape.has_value()) {
    return DecodeFailure(request_id, *shape);
  }

  std::string_view capability;
  if (!ReadStringField(root, "capability", limits.max_string_bytes, false, true,
                       &capability)) {
    return DecodeFailure(
        request_id,
        MakeError(kErrorInvalidFieldValue, "capability field is invalid"));
  }

  std::string_view method_name;
  if (!ReadStringField(root, "method", limits.max_string_bytes, false, true,
                       &method_name)) {
    return DecodeFailure(
        request_id, MakeError(kErrorInvalidFieldValue, "method field is invalid"));
  }
  const auto method = ParseMethod(method_name);
  if (!method.has_value()) {
    return DecodeFailure(request_id,
                         MakeError(kErrorUnknownMethod, "method is not supported"));
  }

  const JsonObject* params = nullptr;
  if (const auto* params_value = FindValue(root, "params");
      params_value != nullptr) {
    if (!params_value->is_object()) {
      return DecodeFailure(
          request_id,
          MakeError(kErrorInvalidFieldType, "params field must be an object"));
    }
    params = &params_value->as_object();
  }

  const bool accepts_input_params = *method == DebugControlMethod::kSubmitInput;
  const bool accepts_ui_params = *method == DebugControlMethod::kInspectUi;
  if (!accepts_input_params && !accepts_ui_params && params != nullptr &&
      !params->empty()) {
    return DecodeFailure(
        request_id,
        MakeError(kErrorUnknownField, "method does not accept parameter fields"));
  }
  if (accepts_input_params && params == nullptr) {
    return DecodeFailure(
        request_id, MakeError(kErrorMissingField, "input params are required"));
  }

  DebugControlRequest request;
  request.request_id = request_id.value_or(0U);
  request.capability_token = std::string(capability);
  switch (*method) {
    case DebugControlMethod::kHealth:
      request.payload = HealthRequest{};
      break;
    case DebugControlMethod::kCapabilities:
      request.payload = CapabilitiesRequest{};
      break;
    case DebugControlMethod::kInspectFrame:
      request.payload = InspectFrameRequest{};
      break;
    case DebugControlMethod::kInspectUi:
      {
        InspectUiRequest inspection;
        if (params != nullptr) {
          if (const auto shape = ValidateObjectShape(
                  *params,
                  {"selector", "max_results", "include_lua",
                   "include_ancestors", "x_pixels", "y_pixels"},
                  {"selector", "max_results", "include_lua",
                   "include_ancestors", "x_pixels", "y_pixels"});
              shape.has_value()) {
            return DecodeFailure(request_id, *shape);
          }
          if (const auto* value = FindValue(*params, "selector"); value != nullptr) {
            std::string_view selector;
            if (!ReadStringField(*params, "selector", limits.max_string_bytes,
                                 true, true, &selector)) {
              return DecodeFailure(
                  request_id,
                  MakeError(kErrorInvalidFieldValue,
                            "UI selector field is invalid"));
            }
            inspection.selector.assign(selector);
          }
          if (const auto* value = FindValue(*params, "max_results");
              value != nullptr) {
            std::uint64_t max_results = 0;
            if (!ReadUnsignedField(*params, "max_results", &max_results) ||
                max_results == 0U || max_results > 4096U) {
              return DecodeFailure(
                  request_id,
                  MakeError(kErrorInvalidFieldValue,
                            "UI max_results must be between 1 and 4096"));
            }
            inspection.max_results = static_cast<std::size_t>(max_results);
          }
          if (const auto* value = FindValue(*params, "include_lua");
              value != nullptr &&
              !ReadBooleanField(*params, "include_lua",
                                &inspection.include_lua)) {
            return DecodeFailure(
                request_id,
                MakeError(kErrorInvalidFieldValue,
                          "UI include_lua field is invalid"));
          }
          if (const auto* value = FindValue(*params, "include_ancestors");
              value != nullptr &&
              !ReadBooleanField(*params, "include_ancestors",
                                &inspection.include_ancestors)) {
            return DecodeFailure(
                request_id,
                MakeError(kErrorInvalidFieldValue,
                          "UI include_ancestors field is invalid"));
          }
          const bool has_x = FindValue(*params, "x_pixels") != nullptr;
          const bool has_y = FindValue(*params, "y_pixels") != nullptr;
          if (has_x != has_y) {
            return DecodeFailure(
                request_id,
                MakeError(kErrorInvalidFieldValue,
                          "UI point requires both x_pixels and y_pixels"));
          }
          if (has_x) {
            std::int32_t x = 0;
            std::int32_t y = 0;
            if (!ReadSignedField(*params, "x_pixels", &x) ||
                !ReadSignedField(*params, "y_pixels", &y)) {
              return DecodeFailure(
                  request_id,
                  MakeError(kErrorInvalidFieldValue,
                            "UI point coordinates must be int32 values"));
            }
            inspection.x_pixels = x;
            inspection.y_pixels = y;
          }
        }
        request.payload = std::move(inspection);
      }
      break;
    case DebugControlMethod::kCaptureScreenshot:
      request.payload = CaptureScreenshotRequest{};
      break;
    case DebugControlMethod::kSubmitInput: {
      const auto event = ParseInputEvent(*params, limits);
      if (const auto* error = std::get_if<DebugControlError>(&event);
          error != nullptr) {
        return DecodeFailure(request_id, *error);
      }
      request.payload = SubmitInputRequest{
          std::get<InputEvent>(std::move(event))};
      break;
    }
  }
  return request;
}

[[nodiscard]] bool IsSafeErrorText(const std::string_view value,
                                   const std::size_t max_bytes) {
  return IsBoundedString(value, max_bytes, false, true);
}

[[nodiscard]] std::optional<DebugControlError> ValidateMediaType(
    const std::string_view media_type,
    const DebugControlServerLimits& limits) {
  const auto maximum = std::min(kMaxMediaTypeBytes, limits.max_string_bytes);
  if (!IsBoundedString(media_type, maximum, false, true)) {
    return MakeError(kErrorInvalidScreenshot, "screenshot media type is invalid");
  }
  return std::nullopt;
}

[[nodiscard]] CapabilityResult<JsonValue> EncodeScreenshot(
    const ScreenshotResult& screenshot,
    const DebugControlServerLimits& limits) {
  return std::visit(
      [&limits](const auto& value) -> CapabilityResult<JsonValue> {
        using Result = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Result, ScreenshotBytes>) {
          if (value.bytes.size() > limits.max_screenshot_bytes) {
            return ValueFailure<JsonValue>(MakeError(
                kErrorResponseTooLarge,
                "screenshot bytes exceed configured response bounds", true));
          }
          if (const auto media_error =
                  ValidateMediaType(value.media_type, limits);
              media_error.has_value()) {
            return ValueFailure<JsonValue>(*media_error);
          }

          std::string encoded;
          if (!value.bytes.empty()) {
            if (value.bytes.size() >
                static_cast<std::size_t>(std::numeric_limits<int>::max())) {
              return ValueFailure<JsonValue>(MakeError(
                  kErrorResponseTooLarge,
                  "screenshot encoding exceeds supported bounds", true));
            }
            const auto encoded_size =
                (value.bytes.size() / 3U +
                 (value.bytes.size() % 3U == 0U ? 0U : 1U)) *
                4U;
            if (encoded_size >
                    static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
                encoded_size >= limits.max_response_bytes) {
              return ValueFailure<JsonValue>(MakeError(
                  kErrorResponseTooLarge,
                  "screenshot encoding exceeds supported bounds", true));
            }
            encoded.resize(encoded_size);
            const auto written = EVP_EncodeBlock(
                reinterpret_cast<unsigned char*>(encoded.data()),
                value.bytes.data(), static_cast<int>(value.bytes.size()));
            if (written < 0) {
              return ValueFailure<JsonValue>(MakeError(
                  kErrorCodecFailure, "screenshot encoding failed"));
            }
            encoded.resize(static_cast<std::size_t>(written));
          }

          JsonObject result;
          result.emplace("data", std::move(encoded));
          result.emplace("encoding", "base64");
          result.emplace("media_type", value.media_type);
          return ValueSuccess<JsonValue>(JsonValue(std::move(result)));
        } else {
          if (!IsBoundedString(value.path, limits.max_artifact_path_bytes, false,
                               true)) {
            return ValueFailure<JsonValue>(MakeError(
                kErrorInvalidScreenshot, "screenshot artifact path is invalid"));
          }
          if (const auto media_error =
                  ValidateMediaType(value.media_type, limits);
              media_error.has_value()) {
            return ValueFailure<JsonValue>(*media_error);
          }

          JsonObject result;
          result.emplace("media_type", value.media_type);
          result.emplace("path", value.path);
          return ValueSuccess<JsonValue>(JsonValue(std::move(result)));
        }
      },
      screenshot);
}

[[nodiscard]] CapabilityResult<JsonValue> EncodeSuccess(
    const DebugControlSuccess& success,
    const DebugControlServerLimits& limits) {
  return std::visit(
      [&limits](const auto& value) -> CapabilityResult<JsonValue> {
        using Result = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Result, HealthResponse>) {
          JsonObject result;
          result.emplace("healthy", value.healthy);
          return ValueSuccess<JsonValue>(JsonValue(std::move(result)));
        } else if constexpr (std::is_same_v<Result, CapabilitiesResponse>) {
          JsonObject result;
          json::array methods;
          std::array<bool, 6> seen{};
          for (const auto method : value.methods) {
            const auto name = MethodName(method);
            if (!name.has_value()) {
              return ValueFailure<JsonValue>(MakeError(
                  kErrorInvalidResponse, "capabilities contain an unknown method"));
            }
            const auto index = static_cast<std::size_t>(method);
            if (index >= seen.size() || seen[index]) {
              return ValueFailure<JsonValue>(MakeError(
                  kErrorInvalidResponse,
                  "capabilities contain a duplicate method"));
            }
            seen[index] = true;
            methods.emplace_back(std::string(*name));
          }
          result.emplace("methods", std::move(methods));
          return ValueSuccess<JsonValue>(JsonValue(std::move(result)));
        } else if constexpr (std::is_same_v<Result, SerializedJson>) {
          if (value.value.size() >= limits.max_response_bytes) {
            return ValueFailure<JsonValue>(MakeError(
                kErrorResponseTooLarge,
                "snapshot document exceeds configured response bounds", true));
          }
          JsonValue document;
          if (const auto error =
                   ParseJsonDocument(value.value, limits, &document);
              error.has_value()) {
            return ValueFailure<JsonValue>(MakeError(
                kErrorInvalidSnapshot,
                "snapshot document is not valid within configured bounds"));
          }
          return ValueSuccess<JsonValue>(std::move(document));
        } else if constexpr (std::is_same_v<Result, ScreenshotResult>) {
          return EncodeScreenshot(value, limits);
        } else {
          JsonObject result;
          result.emplace("accepted", value.accepted);
          return ValueSuccess<JsonValue>(JsonValue(std::move(result)));
        }
      },
      success);
}

[[nodiscard]] JsonObject EncodeErrorObject(const DebugControlError& error) {
  JsonObject object;
  object.emplace("code", error.code);
  object.emplace("message", error.message);
  object.emplace("retryable", error.retryable);
  return object;
}

}

DebugControlDecodeResult BoostJsonDebugControlCodec::DecodeRequest(
    const std::string_view frame,
    const DebugControlServerLimits& limits) const {
  if (frame.size() > limits.max_request_bytes) {
    return DecodeFailure(
        std::nullopt,
        MakeError(kErrorRequestTooLarge,
                  "request frame exceeds configured bounds"),
        true);
  }
  if (frame.find_first_of("\r\n") != std::string_view::npos) {
    return DecodeFailure(
        std::nullopt,
        MakeError(kErrorInvalidRequest, "request frame contains a line break"));
  }

  try {
    JsonValue document;
    if (const auto error = ParseJsonDocument(frame, limits, &document);
        error.has_value()) {
      return DecodeFailure(std::nullopt, *error);
    }
    return DecodeRequestDocument(document, limits);
  } catch (...) {
    return DecodeFailure(
        std::nullopt,
        MakeError(kErrorCodecFailure, "request codec failed while parsing"));
  }
}

CapabilityResult<std::string> BoostJsonDebugControlCodec::EncodeResponse(
    const DebugControlResponse& response,
    const DebugControlServerLimits& limits) const {
  if (response.request_id == 0U) {
    return ValueFailure<std::string>(
        MakeError(kErrorInvalidResponse, "response id must be positive"));
  }

  try {
    JsonObject wire_response;
    wire_response.emplace("id", response.request_id);

    if (const auto* error = std::get_if<DebugControlError>(&response.payload);
        error != nullptr) {
      if (!IsSafeErrorText(error->code,
                           std::min(kMaxErrorCodeBytes,
                                    limits.max_string_bytes)) ||
          !IsSafeErrorText(error->message,
                           std::min(kMaxErrorMessageBytes,
                                    limits.max_string_bytes))) {
        return ValueFailure<std::string>(MakeError(
            kErrorInvalidResponse, "response error text is invalid"));
      }
      wire_response.emplace("ok", false);
      wire_response.emplace("error", EncodeErrorObject(*error));
    } else {
      const auto* success = std::get_if<DebugControlSuccess>(&response.payload);
      if (success == nullptr) {
        return ValueFailure<std::string>(
            MakeError(kErrorInvalidResponse, "response payload is invalid"));
      }
      auto result = EncodeSuccess(*success, limits);
      if (const auto* encode_error = std::get_if<DebugControlError>(&result);
          encode_error != nullptr) {
        return ValueFailure<std::string>(*encode_error);
      }
      wire_response.emplace("ok", true);
      wire_response.emplace("result", std::get<JsonValue>(std::move(result)));
    }

    auto serialized = json::serialize(JsonValue(std::move(wire_response)));
    if (limits.max_response_bytes <= 1U ||
        serialized.size() > limits.max_response_bytes - 1U) {
      return ValueFailure<std::string>(MakeError(
          kErrorResponseTooLarge,
          "encoded response exceeds configured frame bounds", true));
    }
    return ValueSuccess<std::string>(std::move(serialized));
  } catch (...) {
    return ValueFailure<std::string>(
        MakeError(kErrorCodecFailure, "response codec failed while encoding"));
  }
}

}

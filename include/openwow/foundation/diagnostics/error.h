#pragma once

#include <string>

namespace openwow::diagnostics {

enum class ErrorCode {
  kNone,
  kInvalidGameData,
  kServerUnreachable,
  kAuthFailed,
  kRealmUnavailable,
  kDisconnected,
  kProtocolMismatch,
  kConfigurationError,
};

struct Error {
  ErrorCode code{ErrorCode::kNone};
  std::string details;
};

std::string ToUserMessage(const Error& error);

}

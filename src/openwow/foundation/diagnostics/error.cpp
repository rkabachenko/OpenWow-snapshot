#include "openwow/foundation/diagnostics/error.h"

namespace openwow::diagnostics {

std::string ToUserMessage(const Error& error) {
  switch (error.code) {
    case ErrorCode::kInvalidGameData:
      return "Game data path is invalid or incompatible.";
    case ErrorCode::kServerUnreachable:
      return "Server is unreachable. Check host/port and try again.";
    case ErrorCode::kAuthFailed:
      return "Authentication failed. Verify credentials.";
    case ErrorCode::kRealmUnavailable:
      return "Selected realm is unavailable.";
    case ErrorCode::kDisconnected:
      return "Disconnected from server. You can retry.";
    case ErrorCode::kProtocolMismatch:
      return "Client/server protocol mismatch detected.";
    case ErrorCode::kConfigurationError:
      return "Configuration is invalid. Review settings and retry.";
    case ErrorCode::kNone:
    default:
      return error.details.empty() ? "Unknown error." : error.details;
  }
}

}

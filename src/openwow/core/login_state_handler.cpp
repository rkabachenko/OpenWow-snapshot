
#include "openwow/core/login_state_handler.h"
#include "openwow/core/console.h"
#include "openwow/core/legacy_buffered_log_file.h"
#include "openwow/core/storm_string.h"
#include "openwow/net/wotlk/protocol/auth_protocol.h"

#include <array>
#include <cstdarg>
#include <mutex>
#include <vector>

namespace openwow::core {
namespace {

struct ConnectionLoggerState {
    std::mutex mutex;
    LegacyBufferedLogFile file;
};

struct LoginConsoleDiagnosticsState {
    std::mutex mutex;
    bool enabled = false;
    std::vector<std::string> queued_lines;
};

ConnectionLoggerState& GetConnectionLoggerState() {
    static ConnectionLoggerState state;
    return state;
}

LoginConsoleDiagnosticsState& GetLoginConsoleDiagnosticsState() {
    static LoginConsoleDiagnosticsState state;
    return state;
}

std::size_t CountUtf8Codepoints(const std::string_view text) {
    std::size_t count = 0;
    const char* cursor = text.data();
    const char* const end = cursor + text.size();
    while (cursor < end && *cursor != '\0') {
        std::uint32_t raw_codepoint = 0;
        std::uint32_t upper_codepoint = 0;
        const char* next = cursor;
        const int consumed =
            SStrGetNextUTF8Char_ToUpper(&raw_codepoint, &next, &upper_codepoint);
        if (consumed <= 0 || next <= cursor) {
            ++cursor;
        } else {
            cursor = next;
        }
        ++count;
    }
    return count;
}

bool ContainsUtf8NoCase(const std::string_view haystack,
                        const std::string_view needle) {
    if (haystack.empty() || needle.empty()) {
        return false;
    }

    const std::size_t needle_codepoints = CountUtf8Codepoints(needle);
    if (needle_codepoints == 0) {
        return false;
    }

    const char* cursor = haystack.data();
    const char* const end = cursor + haystack.size();
    while (cursor < end && *cursor != '\0') {
        if (SStrCmpUTF8NoCase(cursor, needle.data(), needle_codepoints) == 0) {
            return true;
        }

        std::uint32_t raw_codepoint = 0;
        std::uint32_t upper_codepoint = 0;
        const char* next = cursor;
        const int consumed =
            SStrGetNextUTF8Char_ToUpper(&raw_codepoint, &next, &upper_codepoint);
        if (consumed <= 0 || next <= cursor) {
            ++cursor;
        } else {
            cursor = next;
        }
    }

    return false;
}

std::string ResolveLoginDialogString(
    const LoginStateDialogHandler::ResolveStringFn& resolve_string,
    const std::string_view key) {
    if (!resolve_string || key.empty()) {
        return std::string(key);
    }

    const std::string resolved = resolve_string(key);
    if (!resolved.empty()) {
        return resolved;
    }

    return std::string(key);
}

}

void LoginStateDialogHandler::Reset() {
    last_state_ = -1;
    last_result_ = -1;
}

std::optional<LoginDialogEvent> LoginStateDialogHandler::Poll(
    const std::int32_t current_state,
    const std::int32_t current_result,
    const ResolveStringFn& resolve_string) {
    if (last_state_ == current_state && last_result_ == current_result) {
        return std::nullopt;
    }

    last_state_ = current_state;
    last_result_ = current_result;

    if (current_state == 5) {
        if (current_result < 0 || current_result > 0x28) {
            return std::nullopt;
        }

        if (current_result == 19) {
            return LoginDialogEvent{{
                "PARENTAL_CONTROL",
                ResolveLoginDialogString(resolve_string, "LOGIN_PARENTALCONTROL"),
                "AUTH_PARENTAL_CONTROL_URL",
            }};
        }

        const std::string result_key =
            openwow::net::wotlk::ResolveLoginResultKey(
                static_cast<std::uint32_t>(current_result));
        const std::string localized_result =
            ResolveLoginDialogString(resolve_string, result_key);

        if (ContainsUtf8NoCase(localized_result, "<HTML>")) {
            if (current_result == 11) {
                return LoginDialogEvent{{"CONNECTION_HELP_HTML", localized_result}};
            }
            return LoginDialogEvent{{"OKAY_HTML", localized_result}};
        }

        if (current_result == 11) {
            return LoginDialogEvent{{"CONNECTION_HELP", localized_result}};
        }

        return LoginDialogEvent{{"OKAY", localized_result}};
    }

    const bool state_is_cancellable =
        current_state >= 0
        && current_state < 0x12
        && current_state != 10
        && current_state != 11
        && current_state != 12
        && current_state != 13
        && current_state != 16
        && current_state != 17;
    if (!state_is_cancellable) {
        return std::nullopt;
    }

    const std::string state_key =
        openwow::net::wotlk::ResolveLoginStateKey(
            static_cast<std::uint32_t>(current_state));
    return LoginDialogEvent{{
        "CANCEL",
        ResolveLoginDialogString(resolve_string, state_key),
    }};
}

ConnectionLogger& ConnectionLogger::Instance() {
    static ConnectionLogger instance;
    return instance;
}

void ConnectionLogger::Log(const char* message) {
    if (message == nullptr) {
        return;
    }

    auto& state = GetConnectionLoggerState();
    std::lock_guard lock(state.mutex);
    if (!state.file.IsOpen()
        && !state.file.Open("Logs\\connection.log",
                            LegacyBufferedLogOpenMode::kTruncate)) {
        return;
    }

    state.file.AppendLine(message);
    state.file.FlushPending();
}

void ConnectionLogger::Shutdown() {
    auto& state = GetConnectionLoggerState();
    std::lock_guard lock(state.mutex);
    state.file.Close();
}

LoginConsoleDiagnostics& LoginConsoleDiagnostics::Instance() {
    static LoginConsoleDiagnostics instance;
    return instance;
}

void LoginConsoleDiagnostics::SetEnabled(const bool enabled) {
    auto& state = GetLoginConsoleDiagnosticsState();
    std::lock_guard lock(state.mutex);
    state.enabled = enabled;
}

bool LoginConsoleDiagnostics::IsEnabled() const {
    auto& state = GetLoginConsoleDiagnosticsState();
    std::lock_guard lock(state.mutex);
    return state.enabled;
}

void LoginConsoleDiagnostics::EnqueueFormattedLine(const char* format, ...) {
    if (format == nullptr || *format == '\0') {
        return;
    }

    std::array<char, 0x100> buffer{};
    va_list args;
    va_start(args, format);
    SStrPrintfV(buffer.data(), buffer.size(), format, args);
    va_end(args);

    auto& state = GetLoginConsoleDiagnosticsState();
    {
        std::lock_guard lock(state.mutex);
        if (!state.enabled || buffer.front() == '\0') {
            return;
        }
        state.queued_lines.emplace_back(buffer.data());
    }

    ConnectionLogger::Instance().Log(buffer.data());
}

void LoginConsoleDiagnostics::DrainToConsole() {
    std::vector<std::string> pending_lines;
    auto& state = GetLoginConsoleDiagnosticsState();
    {
        std::lock_guard lock(state.mutex);
        pending_lines.swap(state.queued_lines);
    }

    for (const std::string& line : pending_lines) {
        ida::ConsoleAddLine(line, ida::COLOR_DEFAULT);
    }
}

void LoginConsoleDiagnostics::Shutdown() {
    auto& state = GetLoginConsoleDiagnosticsState();
    {
        std::lock_guard lock(state.mutex);
        state.enabled = false;
        state.queued_lines.clear();
    }
    ConnectionLogger::Instance().Shutdown();
}

std::uint64_t GetAccountDataTimestamp() {

    return 0;
}

}

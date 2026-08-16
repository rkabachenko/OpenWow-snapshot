
#include "openwow/platform/adapters/clipboard/os_clipboard.h"

#include <SDL2/SDL.h>

#include <cstring>
#include <optional>
#include <utility>

#include "openwow/core/storm_string.h"

namespace openwow::platform {

namespace {

constexpr const char* kOsClipboardSourceFile = ".\\W32\\OsClipboard.cpp";
constexpr int kOsClipboardReadAllocLine = 0x80;
constexpr int kOsClipboardFreeLine = 145;

std::optional<std::string>& ClipboardTextOverride() {
    static std::optional<std::string> text;
    return text;
}

}

std::optional<std::string> TryGetSystemClipboardText() {
    if (auto& override_text = ClipboardTextOverride(); override_text.has_value()) {
        return override_text;
    }

    char* const clipboard_text = SDL_GetClipboardText();
    if (clipboard_text == nullptr) {
        return std::nullopt;
    }

    std::string copied_text = clipboard_text;
    SDL_free(clipboard_text);
    return copied_text;
}

bool SetSystemClipboardText(std::string_view text) {
    if (auto& override_text = ClipboardTextOverride(); override_text.has_value()) {
        override_text = std::string(text);
        return true;
    }

    const std::string clipboard_text(text);
    return SDL_SetClipboardText(clipboard_text.c_str()) == 0;
}

void SetSystemClipboardTextOverrideForTests(std::string text) {
    ClipboardTextOverride() = std::move(text);
}

void ClearSystemClipboardTextOverrideForTests() {
    ClipboardTextOverride().reset();
}

static std::string NormalizeClipboardLineEndings(const std::string& text) {
    std::string result;
    result.reserve(text.size() + text.size() / 80);

    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            result += '\r';
            i += 2;
        } else {
            result += text[i];
            ++i;
        }
    }

    for (auto& ch : result) {
        if (ch == '\n')
            ch = '\r';
    }

    std::string final_result;
    final_result.reserve(result.size() + result.size() / 80);
    for (auto ch : result) {
        if (ch == '\r') {
            final_result += '\r';
            final_result += '\n';
        } else {
            final_result += ch;
        }
    }

    return final_result;
}

char* OsClipboard_GetText(void* hwnd) {
    (void)hwnd;

    const auto clipboard_text = TryGetSystemClipboardText();
    if (!clipboard_text.has_value()) {
        return nullptr;
    }

    const std::string normalized = NormalizeClipboardLineEndings(*clipboard_text);

    auto* const storm_buffer = static_cast<char*>(
        openwow::core::SMemAlloc(normalized.size() + 1,
                                 kOsClipboardSourceFile,
                                 kOsClipboardReadAllocLine,
                                 0));
    if (storm_buffer == nullptr) {
        return nullptr;
    }

    std::memcpy(storm_buffer, normalized.data(), normalized.size());
    storm_buffer[normalized.size()] = '\0';
    return storm_buffer;
}

void OsClipboard_FreeText(void* text) {
    if (text == nullptr) {
        return;
    }

    (void)openwow::core::SMemFree(text, kOsClipboardSourceFile, kOsClipboardFreeLine, 0);
}

int OsClipboard_SetText(const char* text, void* hwnd) {
    (void)hwnd;

    if (text == nullptr) {
        return 0;
    }

    return SetSystemClipboardText(text) ? 1 : 0;
}

char* OsClipboard_GetTextForActiveWindow() {
    return OsClipboard_GetText(nullptr);
}

int OsClipboard_SetTextForActiveWindow(const char* text) {
    return OsClipboard_SetText(text, nullptr);
}

}

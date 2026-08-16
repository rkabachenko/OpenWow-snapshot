#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace openwow::platform {

char* OsClipboard_GetTextForActiveWindow();

int OsClipboard_SetTextForActiveWindow(const char* text);

char* OsClipboard_GetText(void* hwnd);

void OsClipboard_FreeText(void* text);

int OsClipboard_SetText(const char* text, void* hwnd);

[[nodiscard]] std::optional<std::string> TryGetSystemClipboardText();
[[nodiscard]] bool SetSystemClipboardText(std::string_view text);

void SetSystemClipboardTextOverrideForTests(std::string text);
void ClearSystemClipboardTextOverrideForTests();

}

#pragma once

#include <cstddef>
#include <cstdint>

namespace openwow::ui {

void CopyCStringWithoutPipes(const char* src, char* dst, int dstSize);

bool TruncateAtNewlineOrPipeN(char* text);

bool ValidateUtf8String(const char* str);

bool Script_SanitizeBoundedText(const char* src, char* dst,
                                int maxCodepoints, int dstBufSize,
                                uint8_t flags);

}

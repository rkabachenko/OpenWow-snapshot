
#include "script_text_sanitize.h"

#include "../core/storm_string.h"

#include <cstdint>

namespace openwow::ui {

void CopyCStringWithoutPipes(const char* src, char* dst, int dstSize) {
    const auto* s = reinterpret_cast<const uint8_t*>(src);
    auto*       d = reinterpret_cast<uint8_t*>(dst);
    const auto* dEnd = d + (dstSize - 1);

    uint8_t ch = *s;
    while (ch) {
        if (d >= dEnd)
            break;
        if (ch != '|')
            *d++ = ch;
        ch = *++s;
    }
    *d = 0;
}

bool TruncateAtNewlineOrPipeN(char* text) {
    if (!text)
        return false;

    for (char* p = text; *p; ++p) {
        const char ch = *p;

        if (ch == '\\' || ch == '|') {
            if (p[1] == 'n') {
                *p = '\0';
                return true;
            }
        } else if (ch == '\r') {
            *p = '\0';
            return true;
        } else if (ch == '\n') {
            *p = '\0';
            return true;
        }
    }
    return false;
}

bool ValidateUtf8String(const char* str) {
    const char* cursor = str;
    uint32_t    consumed = 0;

    int32_t cp = core::DecodeNextLegacyUtf8Codepoint(cursor, &consumed);
    if (cp == -1)
        return true;

    while (cp != static_cast<int32_t>(0x80000000u)) {
        cursor += consumed;
        cp = core::DecodeNextLegacyUtf8Codepoint(cursor, &consumed);
        if (cp == -1)
            return true;
    }
    return false;
}

bool Script_SanitizeBoundedText(const char* src, char* dst,
                                int maxCodepoints, int dstBufSize,
                                uint8_t flags) {
    if (!dst)
        return false;

    *dst = '\0';

    if (!src)
        return true;

    bool valid = ValidateUtf8String(src);

    CopyCStringWithoutPipes(src, dst, dstBufSize);

    if ((flags & 1) && TruncateAtNewlineOrPipeN(dst))
        valid = false;

    int cpCount = 0;
    if (*dst) {
        auto* p = reinterpret_cast<uint8_t*>(dst);
        for (;;) {
            if ((*p & 0xC0) != 0x80)
                ++cpCount;
            if (cpCount == maxCodepoints) {
                *p = 0;
                return false;
            }
            ++p;
            if (!*p)
                return valid;
        }
    }
    return valid;
}

}

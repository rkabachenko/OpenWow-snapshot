
#include "storm_string.h"

#include "openwow/core/storm_error.h"
#include "openwow/core/storm_sync.h"
#include "openwow/core/storm_utf8.h"
#include "openwow/platform/adapters/win32/storm_registry.h"
#include "openwow/platform/adapters/win32/win32_compat.h"
#include "storm_big.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include <openssl/sha.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <mutex>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace openwow::core {

namespace {

void NoOpDiagnosticTrace() {}

detail::WoWPreCrtInitDependencies& MutableWoWPreCrtInitDependencies() {
  static detail::WoWPreCrtInitDependencies deps{
      .init_critical_sections = &InitCriticalSections,
      .diagnostic_trace = &NoOpDiagnosticTrace,
  };
  return deps;
}

}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

static int g_suppressErrorDisplay = 0;

static int g_logRealloc = 0;

static int g_memoryInitialized = 0;

static int g_sErrLastError = 0;

static int g_stormLastError = 0;

static const uint32_t kValidAuctionDurations[3] = {720, 1440, 2880};

void SErrSetLastError(int code) {
  g_sErrLastError = code;
  openwow::platform::SetPlatformLastError(static_cast<std::uint32_t>(code));
}

int SErrGetLastError() {
  return g_sErrLastError;
}

int GetStormLastError() {
  return g_stormLastError;
}

void StormSetLastError(int code) {
  g_stormLastError = code;
}

void *SMemAlloc(size_t size, const char * , int , int flags) {
  size_t aligned = (size + 7) & ~static_cast<size_t>(7);
  void *ptr;
  if (flags & 0x8) {
    ptr = std::calloc(1, aligned);
  } else {
    ptr = std::malloc(aligned);
  }
  if (!ptr) {
    SMemAlloc_ErrorHandler(size, nullptr, 0);
    return nullptr;
  }
  return ptr;
}

bool SMemFree(void *ptr, const char * , int , int ) {
  if (ptr) {
    std::free(ptr);
  }
  return true;
}

void *SMemReAlloc(void *ptr, size_t size, const char *file, int line, int flags) {
  if (flags == static_cast<int>(0xAFAFAFAB))
    return nullptr;

  if (!ptr)
    return SMemAlloc(size, file, line, flags);

  if (flags & 0x10)
    return nullptr;

  size_t oldSize = 0;
#ifdef _WIN32
  oldSize = _msize(ptr);
#else

  oldSize = 0;
#endif

  size_t aligned = (size + 7) & ~static_cast<size_t>(7);
  void *newPtr = std::realloc(ptr, aligned);
  if (!newPtr) {
    if (aligned != 0)
      SMemAlloc_ErrorHandler(size, file, line);
    return nullptr;
  }

  if (oldSize < size && (flags & 0x8)) {
    std::memset(static_cast<char *>(newPtr) + oldSize, 0, size - oldSize);
  }
  return newPtr;
}

void SMemAlloc_ErrorHandler(size_t requestedBytes, const char *file, int line) {
  char buf[128];

  std::snprintf(buf, sizeof(buf), "Requested %u bytes of memory",
                static_cast<unsigned int>(requestedBytes));

  SErrDisplayError(8, file, line, buf,
                   g_suppressErrorDisplay == 0 ? 1 : 0, 1, 0x11111111);

  if (g_suppressErrorDisplay) {
    ExitProcessWithCode(1);
  }
}

void *SStrDup(const char *src, const char *file, int line) {
  if (!src) {
    SErrSetLastError(87);
    return nullptr;
  }

  const char *p = src;
  while (*p) {
    ++p;
  }
  size_t len = static_cast<size_t>(p - src) + 1;

  void *dst = SMemAlloc(len, file, line, 0);
  std::memcpy(dst, src, len);
  return dst;
}

void InitMemorySystem() {

  g_memoryInitialized = 1;
}

char *SStrChr(char *str, int c) {
  if (!str) {
    SErrSetLastError(87);
    return nullptr;
  }
  const char character = static_cast<char>(c);
  return character != '\0' ? std::strchr(str, character) : nullptr;
}

const char *SStrChr(const char *str, int c) {
  return SStrChr(const_cast<char *>(str), c);
}

char *SStrRChr(char *str, int c) {
  if (!str) {
    SErrSetLastError(87);
    return nullptr;
  }
  const char character = static_cast<char>(c);
  return character != '\0' ? std::strrchr(str, character) : nullptr;
}

const char *SStrRChr(const char *str, int c) {
  return SStrRChr(const_cast<char *>(str), c);
}

size_t SStrCopy(char *dst, const char *src, size_t maxChars) {
  if (!dst || !src) {
    SErrSetLastError(87);
    return 0;
  }

  char *out = dst;
  if (maxChars == 0x7FFFFFFFu) {
    while (*src) {
      *out++ = *src++;
    }
    *out = '\0';
    return static_cast<size_t>(out - dst);
  }

  if (maxChars == 0) {
    *dst = '\0';
    return 0;
  }

  const char *input = src;
  char *const limit = dst + maxChars - 1;
  while (*input && out < limit) {
    *out++ = *input++;
  }

  *out = '\0';
  return static_cast<size_t>(out - dst);
}

size_t SStrCopyUTF8(char *dst, const char *src, size_t maxChars, size_t maxCodepoints) {
  if (!dst || !src) {
    SErrSetLastError(87);
    return 0;
  }

  if (maxChars == 0) {
    *dst = '\0';
    return 0;
  }

  char *cursor = dst;
  char *lastLeadBoundary = dst;
  const std::intptr_t sourceOffset =
      reinterpret_cast<std::intptr_t>(src) - reinterpret_cast<std::intptr_t>(dst);
  char *const limit = dst + maxChars - 1;

  const auto sourceByteAt = [sourceOffset](const char *out) -> unsigned char {
    const auto source = reinterpret_cast<const unsigned char *>(
        reinterpret_cast<std::intptr_t>(out) + sourceOffset);
    return *source;
  };

  if (*src != '\0') {
    while (cursor < limit) {
      if (maxCodepoints == 0) {
        break;
      }

      const unsigned char current = sourceByteAt(cursor);
      *cursor = static_cast<char>(current);
      if ((current & 0xC0u) != 0x80u) {
        --maxCodepoints;
        lastLeadBoundary = cursor + 1;
      }

      ++cursor;
      if (sourceByteAt(cursor) == '\0') {
        break;
      }
    }
  }

  if (cursor < limit) {
    *cursor = '\0';
    return static_cast<size_t>(cursor - dst);
  }

  *lastLeadBoundary = '\0';
  return static_cast<size_t>(cursor - dst);
}

size_t SStrLen(const char *str) {
  if (!str) {
    SErrSetLastError(87);
    return 0;
  }
  return std::strlen(str);
}

size_t SStrLenW(const std::uint16_t *str) {
  if (!str) {
    SErrSetLastError(87);
    return 0;
  }

  const std::uint16_t *cursor = str;
  while (*cursor != 0) {
    ++cursor;
  }
  return static_cast<size_t>(cursor - str);
}

size_t CountLegacyUtf8Codepoints(const char *str) {
  if (!str) {
    SErrSetLastError(87);
    return 0;
  }

  size_t count = 0;
  while (*str != '\0') {
    if ((static_cast<unsigned char>(*str) & 0xC0u) != 0x80u) {
      ++count;
    }
    ++str;
  }
  return count;
}

size_t SStrCountUtf8CodepointsBounded(const char *str, size_t maxBytes) {
  if (!str) {
    SErrSetLastError(87);
    return 0;
  }

  size_t count = 0;
  const auto *cursor = reinterpret_cast<const unsigned char *>(str);
  while (*cursor != '\0' && maxBytes-- != 0) {
    if ((*cursor & 0xC0u) != 0x80u) {
      ++count;
    }
    ++cursor;
  }
  return count;
}

const char *AdvanceLegacyUtf8Codepoints(const char *str, size_t codepoints) {
  if (!str) {
    SErrSetLastError(87);
    return nullptr;
  }

  while (*str != '\0' && codepoints != 0) {
    if ((static_cast<unsigned char>(*str) & 0xC0u) != 0x80u) {
      --codepoints;
    }
    ++str;
  }
  return str;
}

size_t SStrCat(char *dst, const char *src, size_t maxChars) {
  if (!dst || !src) {
    SErrSetLastError(87);
    return 0;
  }
  if (!maxChars)
    return 0;

  char *d = dst;
  while (*d)
    ++d;

  if (maxChars == 0x7FFFFFFF) {
    const char *s = src;
    while (*s) {
      *d++ = *s++;
    }
  } else {
    const char *s = src;
    while (*s && d < dst + maxChars - 1) {
      *d++ = *s++;
    }
  }
  *d = '\0';
  return static_cast<size_t>(d - dst);
}

namespace {

int SStrPrintf_ParseFormatSpec(int *outArgType, const char **fmtPos, int *argIndex) {
  const char *p = *fmtPos;
  int positionalValue = 0;
  *outArgType = 1;

  for (;;) {
    char ch = *p++;

    if (static_cast<unsigned char>(ch - '0') <= 9) {
      positionalValue = positionalValue * 10 + (ch - '0');
      continue;
    }

    if (ch == '$') {
      if (positionalValue <= 0)
        return -1;
      *argIndex = positionalValue - 1;
      positionalValue = 0;
      continue;
    }

    positionalValue = 0;

    switch (ch) {

    case ' ':
    case '#':
    case '+':
    case '-':
    case '.':
      continue;

    case '%': {
      *outArgType = 5;
      int specLen = static_cast<int>(p - *fmtPos) + 1;
      if (specLen >= 64)
        return -1;
      *fmtPos = p;
      return 0;
    }

    case 'C': case 'D': case 'O': case 'U': case 'X':
    case 'c': case 'd': case 'i': case 'o': case 'u': case 'x': {
      int specLen = static_cast<int>(p - *fmtPos) + 1;
      if (specLen >= 64)
        return -1;
      *fmtPos = p;
      return 1;
    }

    case 'P': case 'S': case 'p': case 's': {
      *outArgType = 2;
      int specLen = static_cast<int>(p - *fmtPos) + 1;
      if (specLen >= 64)
        return -1;
      *fmtPos = p;
      return 1;
    }

    case 'E': case 'F': case 'G':
    case 'e': case 'f': case 'g': {
      *outArgType = 4;
      int specLen = static_cast<int>(p - *fmtPos) + 1;
      if (specLen >= 64)
        return -1;
      *fmtPos = p;
      return 1;
    }

    case 'I':
      if (*p != '6' || *(p + 1) != '4')
        return -1;
      p += 2;
      *outArgType = 3;
      continue;

    case 'l':
      if (*p != 'l')
        return -1;
      ++p;
      *outArgType = 3;
      continue;

    default:
      return -1;
    }
  }
}

void SStrPrintf_ConvertLLtoI64([[maybe_unused]] char *spec) {
#ifdef _MSC_VER
  char *pos = std::strstr(spec, "ll");
  if (pos) {

    std::memmove(pos + 3, pos + 2, std::strlen(pos + 2) + 1);
    pos[0] = 'I';
    pos[1] = '6';
    pos[2] = '4';
  }
#endif
}

}

namespace {

constexpr int kMaxFormatSpecs = 64;
constexpr int kMaxArgs = 64;
constexpr int kArgTypeNone = 0;
constexpr int kArgTypeInt = 1;
constexpr int kArgTypePtr = 2;
constexpr int kArgTypeInt64 = 3;
constexpr int kArgTypeDouble = 4;
constexpr int kArgTypeLiteral = 5;

struct FormatSpec {
  const char *start;
  int length;
  int argIndex;
};

union ArgValue {
  uint32_t intVal;
  uint64_t int64Val;
  double doubleVal;
};

size_t SStrPrintf_Dispatch(char *buffer, size_t maxChars, const char *format, va_list args) {
  if (!maxChars)
    return 0;

  char *const bufStart = buffer;
  char *const bufEnd = buffer + maxChars - 1;

  FormatSpec specs[kMaxFormatSpecs] = {};
  int argTypes[kMaxArgs + 1] = {};
  ArgValue argValues[kMaxArgs] = {};
  argTypes[kMaxArgs] = kArgTypeLiteral;

  int specCount = 0;
  int autoArgIndex = 0;
  int totalArgs = 0;

  const char *p = format;
  while (true) {
    char ch = *p++;
    if (ch == '\0')
      break;
    if (ch != '%')
      continue;
    if (specCount >= kMaxFormatSpecs)
      goto done;

    const char *specStart = p - 1;
    int argType = kArgTypeInt;
    int parseResult = SStrPrintf_ParseFormatSpec(&argType, &p, &autoArgIndex);
    if (parseResult == -1) {

      *buffer = '\0';
      return static_cast<size_t>(buffer - bufStart);
    }

    specs[specCount].start = specStart;
    specs[specCount].length = static_cast<int>(p - specStart);

    if (parseResult <= 0) {

      specs[specCount].argIndex = kMaxArgs;
    } else {
      int idx = autoArgIndex;

      if (argTypes[idx] != kArgTypeNone && argTypes[idx] != argType) {
        *buffer = '\0';
        return static_cast<size_t>(buffer - bufStart);
      }
      specs[specCount].argIndex = idx;
      argTypes[idx] = argType;
      ++autoArgIndex;
      if (totalArgs < autoArgIndex)
        totalArgs = autoArgIndex;
    }
    ++specCount;
  }

  for (int i = 0; i < totalArgs; ++i) {
    switch (argTypes[i]) {
    case kArgTypeNone:
      goto done;
    case kArgTypeInt:
      argValues[i].intVal = va_arg(args, uint32_t);
      break;
    case kArgTypePtr:

      argValues[i].int64Val = static_cast<uint64_t>(va_arg(args, uintptr_t));
      break;
    case kArgTypeInt64:
      argValues[i].int64Val = va_arg(args, uint64_t);
      break;
    case kArgTypeDouble:
      argValues[i].doubleVal = va_arg(args, double);
      break;
    default:
      argValues[i].int64Val = 0;
      break;
    }
  }

  if (buffer < bufEnd) {
    p = format;
    int specIdx = 0;
    while (true) {
      char ch = *p;
      if (ch == '\0')
        break;
      ++p;

      if (ch != '%') {
        *buffer++ = ch;
      } else {

        const FormatSpec &spec = specs[specIdx];
        char specBuf[64];
        int specLen = spec.length;
        std::memcpy(specBuf, spec.start, static_cast<size_t>(specLen));
        specBuf[specLen] = '\0';

        char *dollar = std::strchr(specBuf, '$');
        char *fmtStr = specBuf;
        if (dollar) {
          *dollar = '%';
          fmtStr = dollar;
        }

        int ai = spec.argIndex;
        int remaining = static_cast<int>(bufEnd - buffer + 1);
        int written = 0;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
        switch (argTypes[ai]) {
        case kArgTypeInt:
          written = std::snprintf(buffer, static_cast<size_t>(remaining), fmtStr,
                                  argValues[ai].intVal);
          break;
        case kArgTypePtr:
          written = std::snprintf(buffer, static_cast<size_t>(remaining), fmtStr,
                                  static_cast<uintptr_t>(argValues[ai].int64Val));
          break;
        case kArgTypeInt64: {
          SStrPrintf_ConvertLLtoI64(fmtStr);
          written = std::snprintf(buffer, static_cast<size_t>(remaining), fmtStr,
                                  argValues[ai].int64Val);
          break;
        }
        case kArgTypeDouble:
          written = std::snprintf(buffer, static_cast<size_t>(remaining), fmtStr,
                                  argValues[ai].doubleVal);
          break;
        case kArgTypeLiteral:
          written = std::snprintf(buffer, static_cast<size_t>(remaining), "%s",
                                  fmtStr);
          break;
        default:
          break;
        }
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

        if (written < 0)
          written = remaining - 1;
        if (remaining <= written)
          written = remaining - 1;
        buffer += written;

        specIdx++;
        p = spec.start + specLen;
      }

      if (buffer >= bufEnd) {
        *buffer = '\0';
        return static_cast<size_t>(buffer - bufStart);
      }
    }
  }

done:
  *buffer = '\0';
  return static_cast<size_t>(buffer - bufStart);
}

}

namespace {
bool g_usePositionalPrintf = true;
}

size_t SStrPrintf_Internal(char *dst, size_t maxChars, const char *format, va_list args) {
  if (!maxChars)
    return 0;

  int result;
  if (maxChars == 0x7FFFFFFF) {
    if (g_usePositionalPrintf) {
      return SStrPrintf_Dispatch(dst, 0x100000, format, args);
    }
    result = std::vsprintf(dst, format, args);
  } else {
    if (g_usePositionalPrintf) {
      return SStrPrintf_Dispatch(dst, maxChars, format, args);
    }
    result = std::vsnprintf(dst, maxChars, format, args);
    if (static_cast<size_t>(result) >= maxChars) {
      dst[maxChars - 1] = '\0';
      return maxChars - 1;
    }
  }
  return (result >= 0) ? static_cast<size_t>(result) : 0;
}

size_t SStrPrintfV(char *dst, size_t maxChars, const char *format, va_list args) {
  if (!dst || !format) {
    SErrSetLastError(87);
    return 0;
  }

  return SStrPrintf_Internal(dst, maxChars, format, args);
}

size_t SStrPrintf(char *dst, size_t maxChars, const char *format, ...) {
  va_list args;
  va_start(args, format);

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
  size_t result = SStrPrintfV(dst, maxChars, format, args);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
  va_end(args);
  return result;
}

int SStrCmpI(const char *s1, const char *s2, size_t maxCount) {
  return std::strncmp(s1, s2, maxCount);
}

int SStrCmpNoCase(const char *s1, const char *s2, size_t maxCount) {
#ifdef _WIN32
  return _strnicmp(s1, s2, maxCount);
#else
  return strncasecmp(s1, s2, maxCount);
#endif
}

namespace {

char ToLowerWildcardByte(const char value) {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

bool SStrWildcardMatchImpl(const char *input, const char *pattern) {
  const char *pattern_cursor = pattern;
  char pattern_char = *pattern_cursor;
  if (pattern_char == '\0') {
    return *input == '\0';
  }

  while (true) {
    const char input_char = *input;
    if (input_char == '\0' && pattern_char != '*') {
      return false;
    }

    char expected_char = pattern_char;
    ++pattern_cursor;
    if (pattern_char == '*') {
      break;
    }

    if (pattern_char == '?') {
      if (input_char == '\0') {
        return false;
      }
    } else {
      if (pattern_char == '\\' && *pattern_cursor != '\0') {
        expected_char = *pattern_cursor;
        ++pattern_cursor;
      }

      if (expected_char != input_char &&
          ToLowerWildcardByte(expected_char) != ToLowerWildcardByte(input_char)) {
        return false;
      }
    }

    pattern_char = *pattern_cursor;
    ++input;
    if (pattern_char == '\0') {
      return *input == '\0';
    }
  }

  while (*pattern_cursor == '*') {
    ++pattern_cursor;
  }

  const char next_pattern_char = *pattern_cursor;
  if (next_pattern_char == '\0') {
    return true;
  }

  bool anchoredOnLiteral = false;
  if (next_pattern_char != '?' && next_pattern_char != '\\') {
    char input_char = *input;
    if (input_char == '\0') {
      return false;
    }

    while (next_pattern_char != input_char) {
      input_char = *++input;
      if (input_char == '\0') {
        return false;
      }
    }
    anchoredOnLiteral = true;
  }

  if (*input == '\0') {
    return false;
  }

  if (anchoredOnLiteral) {
    return SStrWildcardMatchImpl(input, pattern_cursor);
  }

  while (true) {
    if (SStrWildcardMatchImpl(input, pattern_cursor)) {
      return true;
    }

    ++input;
    if (*input == '\0') {
      return false;
    }
  }
}

}

bool SStrWildcardMatch(const char *input, const char *pattern) {
  return SStrWildcardMatchImpl(input, pattern);
}

char *EncodeLegacyUtf8Codepoint(std::uint32_t codepoint, char *output) {
  char *cursor = output;
  if (!cursor) {
    return cursor;
  }

  char tail = static_cast<char>(codepoint);
  if (codepoint >= 0x80u) {
    if (codepoint >= 0x800u) {
      if (codepoint >= 0x10000u) {
        if (codepoint >= 0x200000u) {
          if (codepoint >= 0x400000u) {
            if (codepoint >= 0x80000000u) {
              *cursor = '\0';
              return cursor;
            }

            *cursor++ = static_cast<char>((codepoint >> 30) | 0xFCu);
            *cursor++ = static_cast<char>(((codepoint >> 24) & 0x3Fu) | 0x80u);
          } else {
            *cursor++ = static_cast<char>((codepoint >> 24) | 0xF8u);
          }

          *cursor++ = static_cast<char>(((codepoint >> 18) & 0x3Fu) | 0x80u);
        } else {
          *cursor++ = static_cast<char>((codepoint >> 18) | 0xF0u);
        }

        *cursor++ = static_cast<char>(((codepoint >> 12) & 0x3Fu) | 0x80u);
      } else {
        *cursor++ = static_cast<char>((codepoint >> 12) | 0xE0u);
      }

      *cursor++ = static_cast<char>(((codepoint >> 6) & 0x3Fu) | 0x80u);
      tail = static_cast<char>((codepoint & 0x3Fu) | 0x80u);
    } else {
      *cursor++ = static_cast<char>((codepoint >> 6) | 0xC0u);
      tail = static_cast<char>((codepoint & 0x3Fu) | 0x80u);
    }
  }

  *cursor++ = tail;
  *cursor = '\0';
  return cursor;
}

std::int32_t DecodeNextLegacyUtf8Codepoint(const char *text, std::uint32_t *bytesConsumed) {
  std::uint32_t consumed = 0;
  const auto finish = [&](const std::int32_t result) {
    if (bytesConsumed) {
      *bytesConsumed = consumed;
    }
    return result;
  };

  if (!text) {
    return finish(kLegacyUtf8DecodeEnd);
  }

  const auto *cursor = reinterpret_cast<const std::uint8_t *>(text);
  std::uint32_t codepoint = *cursor++;
  if (codepoint == 0) {
    return finish(kLegacyUtf8DecodeEnd);
  }

  consumed = 1;
  int expectedContinuationBytes = 0;
  if ((codepoint & 0xFEu) == 0xFCu) {
    codepoint &= 0x1u;
    expectedContinuationBytes = 5;
  } else if ((codepoint & 0xFCu) == 0xF8u) {
    codepoint &= 0x3u;
    expectedContinuationBytes = 4;
  } else if ((codepoint & 0xF8u) == 0xF0u) {
    codepoint &= 0x7u;
    expectedContinuationBytes = 3;
  } else if ((codepoint & 0xF0u) == 0xE0u) {
    codepoint &= 0xFu;
    expectedContinuationBytes = 2;
  } else if ((codepoint & 0xE0u) == 0xC0u) {
    codepoint &= 0x1Fu;
    expectedContinuationBytes = 1;
  } else {
    if ((codepoint & 0x80u) != 0) {
      return finish(kLegacyUtf8DecodeInvalid);
    }
    return finish(static_cast<std::int32_t>(codepoint));
  }

  for (int index = 0; index < expectedContinuationBytes; ++index) {
    const std::uint32_t next = *cursor++;
    if (next == 0) {
      return finish(kLegacyUtf8DecodeEnd);
    }

    ++consumed;
    if ((next & 0xC0u) != 0x80u) {
      return finish(kLegacyUtf8DecodeInvalid);
    }

    codepoint = (codepoint << 6) | (next & 0x3Fu);
  }

  return finish(std::bit_cast<std::int32_t>(codepoint));
}

int SStrGetNextUTF8Char_ToUpper(uint32_t *rawCodepoint, const char **strPtr,
                                uint32_t *upperCodepoint) {
  const auto *p = reinterpret_cast<const uint8_t *>(*strPtr);
  const int byteClass = detail::LegacyUtf8SequenceLength(*p);

  *rawCodepoint = 0;
  *upperCodepoint = 0;

  uint32_t cp = 0;
  switch (byteClass) {
  case 6:
    cp += *p++;
    if (!*p)
      return 0;
    cp <<= 6;
    [[fallthrough]];
  case 5:
    cp += *p++;
    if (!*p)
      return 0;
    cp <<= 6;
    [[fallthrough]];
  case 4:
    cp += *p++;
    if (!*p)
      return 0;
    cp <<= 6;
    [[fallthrough]];
  case 3:
    cp += *p++;
    if (!*p)
      return 0;
    cp <<= 6;
    [[fallthrough]];
  case 2:
    cp += *p++;
    if (!*p)
      return 0;
    cp <<= 6;
    [[fallthrough]];
  case 1:
    cp += *p++;
    break;
  default:
    break;
  }
  cp -= detail::kLegacyUtf8Offsets[byteClass];

  if (cp > 0xFFFF)
    cp = 0xFFFD;

  *rawCodepoint = cp;
  *strPtr = reinterpret_cast<const char *>(p);

  uint32_t upper = cp;
  constexpr uint32_t kUppercaseOffset = U'a' - U'A';

  if (byteClass == 1) {
    if (cp >= U'a' && cp <= U'z')
      upper -= kUppercaseOffset;
  } else if (byteClass == 2) {
    const bool uses_uniform_case_offset =
        (cp >= U'à' && cp <= U'þ') || (cp >= U'а' && cp <= U'я');
    if (uses_uniform_case_offset)
      upper -= kUppercaseOffset;
    else if (cp == U'œ')
      upper = U'Œ';
    else if (cp == U'ё')
      upper = U'Ё';
  }

  *upperCodepoint = upper;
  return byteClass;
}

int SStrCmpUTF8NoCase(const char *s1, const char *s2, size_t maxCount) {
  size_t remaining = maxCount;

  while (*s1 != '\0' || *s2 != '\0') {
    if (remaining == 0) {
      break;
    }
    --remaining;

    uint32_t rawLeft = 0;
    uint32_t rawRight = 0;
    uint32_t upperLeft = 0;
    uint32_t upperRight = 0;
    SStrGetNextUTF8Char_ToUpper(&rawLeft, &s1, &upperLeft);
    SStrGetNextUTF8Char_ToUpper(&rawRight, &s2, &upperRight);
    if (upperLeft != upperRight) {
      return static_cast<int>(upperLeft) - static_cast<int>(upperRight);
    }
  }

  return 0;
}

namespace {

uint32_t FoldCollationCodepoint(uint32_t codepoint) {
  if (codepoint >= 0xC0 && codepoint <= 0xDF) {
    if (codepoint <= 0xC6) {
      return 'A';
    }
    if (codepoint == 0xC7) {
      return 'C';
    }
    if (codepoint >= 0xC8 && codepoint <= 0xCB) {
      return 'E';
    }
    if (codepoint >= 0xCC && codepoint <= 0xCF) {
      return 'I';
    }
    if (codepoint == 0xD1) {
      return 'N';
    }
    if (codepoint >= 0xD2 && codepoint <= 0xD6) {
      return 'O';
    }
    if (codepoint >= 0xD9 && codepoint <= 0xDC) {
      return 'U';
    }
    if (codepoint == 0xDF) {
      return 'S';
    }
    return codepoint;
  }

  if (codepoint == 0x152) {
    return 'O';
  }
  if (codepoint == 0x401) {
    return 0x415;
  }
  return codepoint;
}

uint32_t PeekFoldedCollationCodepoint(const char *cursor, int *byteCount) {
  uint32_t raw = 0;
  uint32_t upper = 0;
  const int consumed = SStrGetNextUTF8Char_ToUpper(&raw, &cursor, &upper);
  *byteCount = consumed;
  return FoldCollationCodepoint(upper);
}

bool MatchCollationEquivalent(uint32_t rightUpper, uint32_t leftUpper, const char **leftCursor,
                              const char **rightCursor) {
  if (FoldCollationCodepoint(leftUpper) != FoldCollationCodepoint(rightUpper)) {
    return false;
  }

  if (leftUpper == rightUpper) {
    return true;
  }

  if (leftUpper == 0xC6) {
    int consumed = 0;
    if (PeekFoldedCollationCodepoint(*rightCursor, &consumed) != 'E') {
      return false;
    }
    *rightCursor += consumed;
    return true;
  }

  if (rightUpper == 0xC6) {
    int consumed = 0;
    if (PeekFoldedCollationCodepoint(*leftCursor, &consumed) != 'E') {
      return false;
    }
    *leftCursor += consumed;
    return true;
  }

  if (leftUpper == 0xDF) {
    int consumed = 0;
    if (PeekFoldedCollationCodepoint(*rightCursor, &consumed) != 'S') {
      return false;
    }
    *rightCursor += consumed;
    return true;
  }

  if (rightUpper == 0xDF) {
    int consumed = 0;
    if (PeekFoldedCollationCodepoint(*leftCursor, &consumed) != 'S') {
      return false;
    }
    *leftCursor += consumed;
    return true;
  }

  if (leftUpper == 0x152) {
    int consumed = 0;
    if (PeekFoldedCollationCodepoint(*rightCursor, &consumed) != 'E') {
      return false;
    }
    *rightCursor += consumed;
    return true;
  }

  if (rightUpper == 0x152) {
    int consumed = 0;
    if (PeekFoldedCollationCodepoint(*leftCursor, &consumed) != 'E') {
      return false;
    }
    *leftCursor += consumed;
    return true;
  }

  return true;
}

}

int SStrCmpNoCaseCollate(const char *s1, const char *s2, size_t maxCount) {
  if (s1 == s2) {
    return 0;
  }
  if (!s1) {
    return -1;
  }
  if (!s2) {
    return 1;
  }

  int rawDifference = 0;
  while (*s1 || *s2) {
    if (maxCount-- == 0) {
      break;
    }

    uint32_t rawLeft = 0;
    uint32_t rawRight = 0;
    uint32_t upperLeft = 0;
    uint32_t upperRight = 0;
    SStrGetNextUTF8Char_ToUpper(&rawLeft, &s1, &upperLeft);
    SStrGetNextUTF8Char_ToUpper(&rawRight, &s2, &upperRight);

    if (!MatchCollationEquivalent(upperRight, upperLeft, &s1, &s2)) {
      return static_cast<int>(FoldCollationCodepoint(upperLeft)) -
             static_cast<int>(FoldCollationCodepoint(upperRight));
    }

    if (rawDifference == 0) {
      rawDifference = static_cast<int>(rawLeft) - static_cast<int>(rawRight);
    }
  }

  return rawDifference;
}

bool SearchLocaleIsNotLower(uint16_t cp) {
  return static_cast<uint16_t>(cp - 97) > 25u
      && static_cast<uint16_t>(cp - 224) > 30u
      && static_cast<uint16_t>(cp - 1072) > 31u
      && cp != 1105;
}

uint16_t SearchLocaleToUpper(uint16_t cp) {
  if (static_cast<uint16_t>(cp - 97) <= 25u ||
      static_cast<uint16_t>(cp - 224) <= 30u) {
    return cp - 32;
  }
  if (cp == 339) {
    return 338;
  }
  if (static_cast<uint16_t>(cp - 1072) <= 31u) {
    return cp - 32;
  }
  if (cp == 1105) {
    return 1025;
  }
  return cp;
}

uint16_t SearchLocaleToLower(uint16_t cp) {
  if (static_cast<uint16_t>(cp - 65) <= 25u ||
      static_cast<uint16_t>(cp - 192) <= 30u) {
    return cp + 32;
  }
  if (cp == 338) {
    return 339;
  }
  if (static_cast<uint16_t>(cp - 1040) <= 31u) {
    return cp + 32;
  }
  if (cp == 1025) {
    return 1105;
  }
  return cp;
}

namespace {

inline constexpr size_t kSearchNormMaxCodepoints = 512;

void DecodeUTF8ToCodepoints(const char *src, size_t srcSize,
                            uint16_t *out, size_t outCapacity) {
  size_t outIdx = 0;
  const char *cursor = src;
  const char *end = src + srcSize;
  while (cursor < end && outIdx < outCapacity) {
    uint32_t consumed = 0;
    int32_t cp = DecodeNextLegacyUtf8Codepoint(cursor, &consumed);
    if (cp == kLegacyUtf8DecodeEnd) {
      break;
    }
    if (cp == kLegacyUtf8DecodeInvalid || consumed == 0) {
      cursor++;
      continue;
    }
    out[outIdx++] = (cp <= 0xFFFF) ? static_cast<uint16_t>(cp) : 0xFFFD;
    cursor += consumed;
  }
}

size_t EncodeCodepointsToUTF8(char *dst, size_t dstSize,
                              const uint16_t *codepoints, size_t count) {
  size_t written = 0;
  for (size_t i = 0; i < count; ++i) {
    uint16_t cp = codepoints[i];
    if (cp == 0) break;
    char tmp[8] = {};
    char *tmpEnd = EncodeLegacyUtf8Codepoint(cp, tmp);
    if (!tmpEnd) break;
    size_t len = static_cast<size_t>(tmpEnd - tmp);
    if (written + len >= dstSize) break;
    std::memcpy(dst + written, tmp, len);
    written += len;
  }
  if (written < dstSize) {
    dst[written] = '\0';
  }
  return written;
}

}

int SStrNormalizeUTF8ForSearchLocale(char *buffer, size_t bufferSize, int locale) {
  uint16_t decoded[kSearchNormMaxCodepoints] = {};
  uint16_t folded[kSearchNormMaxCodepoints] = {};

  std::memset(decoded, 0, sizeof(uint16_t) * 256);
  std::memset(folded, 0, sizeof(uint16_t) * 256);

  switch (locale) {
    case 0: {
      DecodeUTF8ToCodepoints(buffer, bufferSize, decoded, kSearchNormMaxCodepoints);
      for (size_t i = 0; i < kSearchNormMaxCodepoints; ++i) {
        folded[i] = SearchLocaleToLower(decoded[i]);
      }
      std::memset(buffer, 0, bufferSize);
      EncodeCodepointsToUTF8(buffer, bufferSize, folded, kSearchNormMaxCodepoints);
      break;
    }

    case 2: {
      DecodeUTF8ToCodepoints(buffer, bufferSize, decoded, kSearchNormMaxCodepoints);
      size_t outIdx = 0;
      for (size_t i = 0; i < kSearchNormMaxCodepoints && outIdx < kSearchNormMaxCodepoints; ++i) {
        uint16_t lc = SearchLocaleToLower(decoded[i]);
        decoded[i] = lc;
        if (lc == 0xE0 || lc == 0xE2 || lc == 0xE4) {
          folded[outIdx] = 'a';
        } else if (lc == 0xE6) {
          folded[outIdx++] = 'a';
          if (outIdx < kSearchNormMaxCodepoints) folded[outIdx] = 'e';
        } else if (lc == 0xE7) {
          folded[outIdx] = 'c';
        } else if (lc >= 0xE8 && lc <= 0xEB) {
          folded[outIdx] = 'e';
        } else if (lc == 0xEE || lc == 0xEF) {
          folded[outIdx] = 'i';
        } else if ((lc >= 0xF2 && lc <= 0xF6) && lc != 0xF5) {
          folded[outIdx] = 'o';
        } else if (lc == 339) {
          folded[outIdx++] = 'o';
          if (outIdx < kSearchNormMaxCodepoints) folded[outIdx] = 'e';
        } else if (lc >= 0xF9 && lc <= 0xFC) {
          folded[outIdx] = 'u';
        } else {
          folded[outIdx] = lc;
        }
        ++outIdx;
      }
      std::memset(buffer, 0, bufferSize);
      EncodeCodepointsToUTF8(buffer, bufferSize, folded, kSearchNormMaxCodepoints);
      break;
    }

    case 3: {
      DecodeUTF8ToCodepoints(buffer, bufferSize, decoded, kSearchNormMaxCodepoints);
      size_t outIdx = 0;
      for (size_t i = 0; i < kSearchNormMaxCodepoints && outIdx < kSearchNormMaxCodepoints; ++i) {
        uint16_t lc = SearchLocaleToLower(decoded[i]);
        decoded[i] = lc;
        if (lc == 0xDF) {
          folded[outIdx++] = 's';
          if (outIdx < kSearchNormMaxCodepoints) folded[outIdx] = 's';
        } else {
          folded[outIdx] = lc;
        }
        ++outIdx;
      }
      std::memset(buffer, 0, bufferSize);
      EncodeCodepointsToUTF8(buffer, bufferSize, folded, kSearchNormMaxCodepoints);
      break;
    }

    case 6:
    case 7: {
      DecodeUTF8ToCodepoints(buffer, bufferSize, decoded, kSearchNormMaxCodepoints);
      for (size_t i = 0; i < kSearchNormMaxCodepoints; ++i) {
        uint16_t lc = SearchLocaleToLower(decoded[i]);
        decoded[i] = lc;
        switch (lc) {
          case 0xE1: folded[i] = 'a';  break;
          case 0xE9: folded[i] = 'e';  break;
          case 0xED: folded[i] = 'i';  break;
          case 0xF3: folded[i] = 'o';  break;
          case 0xFA:
          case 0xFC: folded[i] = 'u';  break;
          case 0xF1: folded[i] = 'n';  break;
          default:   folded[i] = lc;   break;
        }
      }
      std::memset(buffer, 0, bufferSize);
      EncodeCodepointsToUTF8(buffer, bufferSize, folded, kSearchNormMaxCodepoints);
      break;
    }

    case 8: {
      DecodeUTF8ToCodepoints(buffer, bufferSize, decoded, kSearchNormMaxCodepoints);
      for (size_t i = 0; i < kSearchNormMaxCodepoints; ++i) {
        uint16_t lc = SearchLocaleToLower(decoded[i]);
        decoded[i] = lc;
        if (lc == 1105) {
          folded[i] = 1077;
        } else {
          folded[i] = lc;
        }
      }
      std::memset(buffer, 0, bufferSize);
      EncodeCodepointsToUTF8(buffer, bufferSize, folded, kSearchNormMaxCodepoints);
      break;
    }

    default:
      return locale;
  }
  return 0;
}

uint32_t SStrHash_JenkinsLookup2(const uint8_t *data, uint32_t length, uint32_t initVal) {
  uint32_t a, b, c;
  a = b = 0x9e3779b9;
  c = initVal;
  uint32_t remaining = length;

  while (remaining >= 12) {
    a += data[0] + (static_cast<uint32_t>(data[1]) << 8) + (static_cast<uint32_t>(data[2]) << 16) +
         (static_cast<uint32_t>(data[3]) << 24);
    b += data[4] + (static_cast<uint32_t>(data[5]) << 8) + (static_cast<uint32_t>(data[6]) << 16) +
         (static_cast<uint32_t>(data[7]) << 24);
    c += data[8] + (static_cast<uint32_t>(data[9]) << 8) + (static_cast<uint32_t>(data[10]) << 16) +
         (static_cast<uint32_t>(data[11]) << 24);

    a -= b; a -= c; a ^= (c >> 13);
    b -= c; b -= a; b ^= (a << 8);
    c -= a; c -= b; c ^= (b >> 13);
    a -= b; a -= c; a ^= (c >> 12);
    b -= c; b -= a; b ^= (a << 16);
    c -= a; c -= b; c ^= (b >> 5);
    a -= b; a -= c; a ^= (c >> 3);
    b -= c; b -= a; b ^= (a << 10);
    c -= a; c -= b; c ^= (b >> 15);

    data += 12;
    remaining -= 12;
  }

  c += length;

  switch (remaining) {
  case 11:
    c += static_cast<uint32_t>(data[10]) << 24;
    [[fallthrough]];
  case 10:
    c += static_cast<uint32_t>(data[9]) << 16;
    [[fallthrough]];
  case 9:
    c += static_cast<uint32_t>(data[8]) << 8;
    [[fallthrough]];
  case 8:
    b += static_cast<uint32_t>(data[7]) << 24;
    [[fallthrough]];
  case 7:
    b += static_cast<uint32_t>(data[6]) << 16;
    [[fallthrough]];
  case 6:
    b += static_cast<uint32_t>(data[5]) << 8;
    [[fallthrough]];
  case 5:
    b += data[4];
    [[fallthrough]];
  case 4:
    a += static_cast<uint32_t>(data[3]) << 24;
    [[fallthrough]];
  case 3:
    a += static_cast<uint32_t>(data[2]) << 16;
    [[fallthrough]];
  case 2:
    a += static_cast<uint32_t>(data[1]) << 8;
    [[fallthrough]];
  case 1:
    a += data[0];
    break;
  default:
    break;
  }

  a -= b; a -= c; a ^= (c >> 13);
  b -= c; b -= a; b ^= (a << 8);
  c -= a; c -= b; c ^= (b >> 13);
  a -= b; a -= c; a ^= (c >> 12);
  b -= c; b -= a; b ^= (a << 16);
  c -= a; c -= b; c ^= (b >> 5);
  a -= b; a -= c; a ^= (c >> 3);
  b -= c; b -= a; b ^= (a << 10);
  c -= a; c -= b; c ^= (b >> 15);

  return c;
}

uint32_t SStrHashCI(const char *str) {
  if (!str || !*str)
    return SStrHash_JenkinsLookup2(nullptr, 0, 0);

  uint8_t buf[1024];
  uint32_t len = 0;
  const char *p = str;
  uint32_t raw = 0, upper = 0;

  while (*p && len < 0x3FC) {
    SStrGetNextUTF8Char_ToUpper(&raw, &p, &upper);

    if (upper == 47)
      upper = 92;

    uint32_t cp = upper;
    do {
      buf[len++] = static_cast<uint8_t>(cp & 0xFF);
      cp >>= 8;
    } while (cp);

    upper = 0;
  }
  buf[len] = 0;
  return SStrHash_JenkinsLookup2(buf, len, 0);
}

char *SStrStrI(const char *haystack, const char *needle) {
  if (!haystack || !needle) {
    SErrSetLastError(87);
    return nullptr;
  }

  if (*haystack == '\0') {
    return nullptr;
  }
  return const_cast<char *>(std::strstr(haystack, needle));
}

const char *SStrCaseStrBounded(const char *haystack, const char *needle,
                               const size_t maxHaystackBytes) {
  if (!haystack || !needle) {
    SErrSetLastError(87);
    return nullptr;
  }

  const size_t needleLength = std::strlen(needle);
  const size_t searchLength = std::min(std::strlen(haystack), maxHaystackBytes);
  if (needleLength > searchLength) {
    return nullptr;
  }

  const char *const searchEnd = haystack + searchLength - needleLength + 1u;
  for (const char *candidate = haystack; candidate != searchEnd; ++candidate) {
    if (SStrCmpNoCase(candidate, needle, needleLength) == 0) {
      return candidate;
    }
  }
  return nullptr;
}

char *SStrCaseStr(const char *haystack, const char *needle) {
  if (!haystack || !needle) {
    SErrSetLastError(87);
    return nullptr;
  }
  if (*haystack == '\0') {
    return nullptr;
  }
  return const_cast<char *>(
      SStrCaseStrBounded(haystack, needle, std::strlen(haystack)));
}

uint64_t SStrToUInt64(const char *str) {
  if (!str) {
    SErrSetLastError(87);
    return 0;
  }

  const char *p = str;
  bool negative = false;

  if (*p == '-') {
    negative = true;
    ++p;
  }

  uint32_t digit = static_cast<uint8_t>(*p) - 48;
  if (digit >= 10)
    return 0;

  uint64_t accumHigh = 0;
  uint32_t accumLow = digit;
  const char *segStart = p;
  ++p;

  while (static_cast<uint32_t>(static_cast<uint8_t>(*p) - 48) < 10) {
    accumLow = static_cast<uint32_t>(static_cast<uint8_t>(*p) - 48) + 10 * accumLow;
    ++p;

    if (accumLow >= 0x19999999) {
      uint32_t dist = static_cast<uint32_t>(p - segStart);
      double scale = std::pow(10.0, static_cast<double>(dist));
      accumHigh = accumLow + static_cast<uint64_t>(scale + 0.5) * accumHigh;
      accumLow = 0;
      segStart = p;
    }
  }

  uint64_t result;
  if (accumHigh) {
    uint32_t dist = static_cast<uint32_t>(p - segStart);
    double scale = std::pow(10.0, static_cast<double>(dist));
    result = accumLow + static_cast<uint64_t>(scale + 0.5) * accumHigh;
  } else {
    result = accumLow;
  }

  return negative ? static_cast<uint64_t>(-static_cast<int64_t>(result)) : result;
}

void ReadPackedGUID(const uint8_t **dataPtr, uint64_t *outGuid) {
  const uint8_t *p = *dataPtr;
  *outGuid = 0;

  uint8_t mask = *p++;
  for (uint32_t i = 0; i < 64; i += 8) {
    if (mask & (1 << (i / 8))) {
      *outGuid |= static_cast<uint64_t>(*p++) << i;
    }
  }
  *dataPtr = p;
}

void WoW_PreCRTInit() {
  const auto deps = MutableWoWPreCrtInitDependencies();
  if (deps.init_critical_sections != nullptr) {
    deps.init_critical_sections();
  }
  if (deps.diagnostic_trace != nullptr) {
    deps.diagnostic_trace();
  }
}

void detail::SetWoWPreCrtInitDependenciesForTests(const WoWPreCrtInitDependencies& deps) {
  MutableWoWPreCrtInitDependencies() = deps;
}

void detail::ResetWoWPreCrtInitDependenciesForTests() {
  MutableWoWPreCrtInitDependencies() = {
      .init_critical_sections = &InitCriticalSections,
      .diagnostic_trace = &NoOpDiagnosticTrace,
  };
}

void SetLogFlags(uint32_t flags, uint8_t mask) {
  if (mask & 0x10) {
    g_suppressErrorDisplay = (flags & 0x10) == 0 ? 1 : 0;
  }
  if (mask & 0x20) {
    g_logRealloc = (flags >> 5) & 1;
  }
}

bool IsStormReallocLoggingEnabledForTests() {
  return g_logRealloc != 0;
}

void ResetStormLogFlagsForTests() {
  g_suppressErrorDisplay = 0;
  g_logRealloc = 0;
}

int DllMain_Storm() {
  return 1;
}

uint32_t CalculateAuctionDeposit(int basePrice, int stackCount, uint32_t duration) {

  bool valid = false;
  for (int i = 0; i < 3; ++i) {
    if (duration == kValidAuctionDurations[i]) {
      valid = true;
      break;
    }
  }
  if (!valid)
    return 100;

  uint32_t baseDeposit =
      static_cast<uint32_t>(basePrice) * static_cast<uint32_t>(stackCount) / 100u;
  uint32_t result = static_cast<uint32_t>(static_cast<double>(baseDeposit) *
                                          static_cast<double>(duration) * 0.0041666669);

  return (result < 100) ? 100 : result;
}

uint32_t Storm_GetCurrentThreadId() {
#ifdef _WIN32
  return ::GetCurrentThreadId();
#else

  return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pthread_self()));
#endif
}

uint32_t Storm_GetCurrentProcessId() {
#ifdef _WIN32
  return ::GetCurrentProcessId();
#else
  return static_cast<uint32_t>(getpid());
#endif
}

int SThread_GetCurrentPriority() {
#ifdef _WIN32
  return GetThreadPriority(GetCurrentThread());
#else
  return 0;
#endif
}

bool SThread_SetCurrentPriority(int priority) {
#ifdef _WIN32
  return SetThreadPriority(GetCurrentThread(), priority) != 0;
#else
  (void)priority;
  return true;
#endif
}

bool Storm_SetProcessAffinity(uint32_t mask) {
#ifdef _WIN32
  return SetProcessAffinityMask(GetCurrentProcess(), static_cast<DWORD_PTR>(mask)) != 0;
#else
  (void)mask;
  return true;
#endif
}

int SLock_InitAndEnter(SLockObj* lock) {

  bool was_initialized = lock->initialized.exchange(true, std::memory_order_acq_rel);
  lock->mutex.lock();
  return was_initialized ? 0 : 1;
}

void SLock_Leave(SLockObj* lock) {
  lock->mutex.unlock();
}

namespace {

struct StormSignatureContext {
  int signature_size = 0;
  int exponent_size = 0;
  int buffered_size = 0;
  int trailer_window_size = 0;
  std::vector<std::uint8_t> trailer_window;
  SHA_CTX sha1{};
};

constexpr std::uint32_t kStormSignatureTag = 0x5349474Eu;

bool VerifyStormSignatureDigest(const StormSignatureContext &ctx, const std::uint8_t *modulus_bytes,
                                const std::uint8_t *exponent_bytes) {
  if (ctx.signature_size <= 0 || ctx.exponent_size <= 0 ||
      ctx.trailer_window.size() < sizeof(std::uint32_t)) {
    return false;
  }

  std::uint32_t tag = 0;
  std::memcpy(&tag, ctx.trailer_window.data(), sizeof(tag));
  if (tag != kStormSignatureTag) {
    return false;
  }

  std::vector<std::uint8_t> digest_block(static_cast<std::size_t>(ctx.signature_size), 0xBB);
  digest_block.back() = 0x0B;

  SHA_CTX sha_copy = ctx.sha1;
  SHA1_Final(digest_block.data(), &sha_copy);

  const auto *signature_bytes = ctx.trailer_window.data() + sizeof(tag);
  BigNum modulus{};
  BigNum exponent{};
  SSignature_LoadKeyPair(&modulus, modulus_bytes, ctx.signature_size, &exponent, exponent_bytes,
                         ctx.exponent_size);

  std::vector<std::uint8_t> decrypted(
      signature_bytes, signature_bytes + static_cast<std::size_t>(ctx.signature_size));
  SSignature_RSAVerify(&modulus, &exponent, decrypted.data(),
                       static_cast<std::uint32_t>(decrypted.size()));
  return std::memcmp(decrypted.data(), digest_block.data(), digest_block.size()) == 0;
}

}

void SSignature_Create(void **outCtx, int signatureSize, int exponentSize) {
  if (!outCtx) {
    return;
  }
  if (signatureSize < SHA_DIGEST_LENGTH ||
      signatureSize > static_cast<int>(kBigNumLimbs * sizeof(std::uint32_t)) ||
      exponentSize <= 0 ||
      exponentSize > static_cast<int>(kBigNumLimbs * sizeof(std::uint32_t))) {
    *outCtx = nullptr;
    return;
  }

  auto *ctx = new (std::nothrow) StormSignatureContext();
  if (!ctx) {
    *outCtx = nullptr;
    return;
  }

  ctx->signature_size = signatureSize;
  ctx->exponent_size = exponentSize;
  ctx->trailer_window_size = signatureSize + 4;
  ctx->trailer_window.assign(static_cast<std::size_t>(ctx->trailer_window_size), 0);
  SHA1_Init(&ctx->sha1);
  *outCtx = ctx;
}

int SSignature_GetTrailingSize(void *ctx) {
  if (!ctx) {
    return 0;
  }
  return static_cast<StormSignatureContext *>(ctx)->trailer_window_size;
}

void SSignature_Update(void *ctx, const void *data, size_t size) {
  if (!ctx || (!data && size != 0)) {
    return;
  }

  if (size == 0) {
    return;
  }

  auto &signature_ctx = *static_cast<StormSignatureContext *>(ctx);
  const auto *source = static_cast<const std::uint8_t *>(data);
  const std::size_t window_size = signature_ctx.trailer_window.size();
  const std::size_t buffered =
      static_cast<std::size_t>(signature_ctx.buffered_size);
  if (size <= window_size - buffered) {
    std::memcpy(signature_ctx.trailer_window.data() + buffered, source, size);
    signature_ctx.buffered_size += static_cast<int>(size);
    return;
  }

  const std::size_t hash_size = buffered + size - window_size;
  const std::size_t buffered_hash_size = std::min(hash_size, buffered);
  if (buffered_hash_size != 0) {
    SHA1_Update(&signature_ctx.sha1, signature_ctx.trailer_window.data(),
                buffered_hash_size);
  }

  if (hash_size < buffered) {
    const std::size_t retained = buffered - hash_size;
    std::memmove(signature_ctx.trailer_window.data(),
                 signature_ctx.trailer_window.data() + hash_size, retained);
    std::memcpy(signature_ctx.trailer_window.data() + retained, source, size);
  } else {
    const std::size_t source_hash_size = hash_size - buffered;
    if (source_hash_size != 0) {
      SHA1_Update(&signature_ctx.sha1, source, source_hash_size);
    }
    std::memcpy(signature_ctx.trailer_window.data(),
                source + source_hash_size, window_size);
  }
  signature_ctx.buffered_size = static_cast<int>(window_size);
}

bool SSignature_Verify(void *ctx, const void *modulusBytes, const void *exponentBytes) {
  if (!ctx) {
    return false;
  }

  const auto *signature_ctx = static_cast<StormSignatureContext *>(ctx);
  const bool verified =
      signature_ctx->buffered_size == signature_ctx->trailer_window_size &&
      modulusBytes != nullptr && exponentBytes != nullptr &&
      VerifyStormSignatureDigest(*signature_ctx, static_cast<const std::uint8_t *>(modulusBytes),
                                 static_cast<const std::uint8_t *>(exponentBytes));

  delete signature_ctx;
  return verified;
}

bool SSignature_VerifyData(const void *data, size_t dataSize, const void *modulusBytes,
                           int signatureSize, const void *exponentBytes, int exponentSize) {
  void *ctx = nullptr;
  SSignature_Create(&ctx, signatureSize, exponentSize);
  if (!ctx) {
    return false;
  }
  SSignature_Update(ctx, data, dataSize);
  return SSignature_Verify(ctx, modulusBytes, exponentBytes);
}

bool ReadRegistryValue(const char *subKey, const char *valueName, uint8_t flags,
                       uint32_t *outValue) {
  return openwow::platform::ReadRegistryValue(subKey, valueName, flags,
                                               outValue) != 0;
}

bool WriteRegistryValue(const char *subKey, const char *valueName, uint8_t flags, uint32_t value) {
  return openwow::platform::WriteRegistryValue(subKey, valueName, flags,
                                                value) != 0;
}

}

#pragma GCC diagnostic pop

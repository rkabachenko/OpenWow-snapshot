
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string_view>
#include "openwow/foundation/compiler/printf_format.h"

namespace openwow::core {

void *SMemAlloc(size_t size, const char *file, int line, int flags);

bool SMemFree(void *ptr, const char *file, int line, int flags);

void *SMemReAlloc(void *ptr, size_t size, const char *file, int line, int flags);

void SMemAlloc_ErrorHandler(size_t requestedBytes, const char *file, int line);

void InitMemorySystem();

void *SStrDup(const char *src, const char *file, int line);

size_t SStrCopy(char *dst, const char *src, size_t maxChars);

char *SStrChr(char *str, int c);
const char *SStrChr(const char *str, int c);

char *SStrRChr(char *str, int c);
const char *SStrRChr(const char *str, int c);

size_t SStrCopyUTF8(char *dst, const char *src, size_t maxChars, size_t maxCodepoints);

size_t SStrLen(const char *str);

size_t SStrLenW(const std::uint16_t *str);

size_t CountLegacyUtf8Codepoints(const char *str);

size_t SStrCountUtf8CodepointsBounded(const char *str, size_t maxBytes);

const char *AdvanceLegacyUtf8Codepoints(const char *str, size_t codepoints);

size_t SStrCat(char *dst, const char *src, size_t maxChars);

size_t SStrPrintf(char *dst, size_t maxChars, const char *format, ...);

OPENWOW_PRINTF_FORMAT(3, 0) size_t SStrPrintfV(char *dst, size_t maxChars,
                                                          const char *format, va_list args);

OPENWOW_PRINTF_FORMAT(3, 0) size_t SStrPrintf_Internal(char *dst, size_t maxChars,
                                                                  const char *format,
                                                                  va_list args);

int SStrCmpI(const char *s1, const char *s2, size_t maxCount);

int SStrCmpNoCase(const char *s1, const char *s2, size_t maxCount);

int SStrCmpUTF8NoCase(const char *s1, const char *s2, size_t maxCount);

int SStrCmpNoCaseCollate(const char *s1, const char *s2, size_t maxCount);

bool SStrWildcardMatch(const char *input, const char *pattern);

bool SearchLocaleIsNotLower(uint16_t codepoint);

uint16_t SearchLocaleToUpper(uint16_t codepoint);

uint16_t SearchLocaleToLower(uint16_t codepoint);

int SStrNormalizeUTF8ForSearchLocale(char *buffer, size_t bufferSize, int locale);

int SStrGetNextUTF8Char_ToUpper(uint32_t *rawCodepoint, const char **strPtr,
                                uint32_t *upperCodepoint);

inline constexpr std::int32_t kLegacyUtf8DecodeEnd = -1;
inline constexpr std::int32_t kLegacyUtf8DecodeInvalid = std::numeric_limits<std::int32_t>::min();

char *EncodeLegacyUtf8Codepoint(std::uint32_t codepoint, char *output);

std::int32_t DecodeNextLegacyUtf8Codepoint(const char *text, std::uint32_t *bytesConsumed);

[[nodiscard]] inline size_t CountLegacyUtf8Codepoints(const std::string_view text) {
  size_t count = 0;
  for (const unsigned char byte : text) {
    if ((byte & 0xC0u) != 0x80u) {
      ++count;
    }
  }
  return count;
}

[[nodiscard]] inline size_t AdvanceLegacyUtf8CodepointBytes(const std::string_view text,
                                                            size_t codepoints) {
  size_t offset = 0;
  while (offset < text.size() && codepoints != 0) {
    if ((static_cast<unsigned char>(text[offset]) & 0xC0u) != 0x80u) {
      --codepoints;
    }
    ++offset;
  }
  return offset;
}

[[nodiscard]] inline bool LegacyUtf8CursorContainsNoCase(const std::string_view candidate,
                                                         const std::string_view query,
                                                         size_t cursorCodepoints) {
  const char *const candidate_ptr = candidate.empty() ? "" : candidate.data();
  const char *const query_ptr = query.empty() ? "" : query.data();
  const auto query_codepoints = CountLegacyUtf8Codepoints(query);
  cursorCodepoints = std::min(cursorCodepoints, query_codepoints);
  if (SStrCmpUTF8NoCase(query_ptr, candidate_ptr, cursorCodepoints) != 0) {
    return false;
  }
  const auto candidate_codepoints = CountLegacyUtf8Codepoints(candidate);
  if (candidate_codepoints < query_codepoints) {
    return false;
  }
  if (query_codepoints == cursorCodepoints) {
    return true;
  }

  size_t candidate_slots = candidate_codepoints - query_codepoints;
  if (candidate_slots == 0) {
    return false;
  }

  const auto suffix_codepoints = query_codepoints - cursorCodepoints;
  const auto query_suffix = query_ptr + AdvanceLegacyUtf8CodepointBytes(query, cursorCodepoints);
  const char *candidate_suffix =
      candidate_ptr + AdvanceLegacyUtf8CodepointBytes(candidate, cursorCodepoints);

  while (candidate_slots != 0) {
    if (SStrCmpUTF8NoCase(candidate_suffix, query_suffix, suffix_codepoints) == 0) {
      return true;
    }
    candidate_suffix = AdvanceLegacyUtf8Codepoints(candidate_suffix, 1);
    --candidate_slots;
  }

  return false;
}

uint32_t SStrHash_JenkinsLookup2(const uint8_t *data, uint32_t length, uint32_t initVal);

uint32_t SStrHashCI(const char *str);

char *SStrStrI(const char *haystack, const char *needle);

char *SStrCaseStr(const char *haystack, const char *needle);

const char *SStrCaseStrBounded(const char *haystack, const char *needle,
                               size_t maxHaystackBytes);

inline constexpr std::size_t kStormBoundedHashLookupNameLimit = 0x104u;

[[nodiscard]] inline bool StormHashLookupKeyEqualsNoCase(uint32_t entryHash, const char *entryName,
                                                         uint32_t queryHash, const char *queryName,
                                                         size_t maxCount) {
  return entryName != nullptr && queryName != nullptr && entryHash == queryHash &&
         SStrCmpNoCase(entryName, queryName, maxCount) == 0;
}

uint64_t SStrToUInt64(const char *str);

void ReadPackedGUID(const uint8_t **dataPtr, uint64_t *outGuid);

void WoW_PreCRTInit();

namespace detail {

using WoWPreCrtInitCallback = void (*)();

struct WoWPreCrtInitDependencies {
  WoWPreCrtInitCallback init_critical_sections = nullptr;
  WoWPreCrtInitCallback diagnostic_trace = nullptr;
};

void SetWoWPreCrtInitDependenciesForTests(const WoWPreCrtInitDependencies& deps);
void ResetWoWPreCrtInitDependenciesForTests();

}

void SetLogFlags(uint32_t flags, uint8_t mask);

[[nodiscard]] bool IsStormReallocLoggingEnabledForTests();
void ResetStormLogFlagsForTests();

int DllMain_Storm();

uint32_t CalculateAuctionDeposit(int basePrice, int stackCount, uint32_t duration);

uint32_t Storm_GetCurrentThreadId();

uint32_t Storm_GetCurrentProcessId();

int SThread_GetCurrentPriority();

bool SThread_SetCurrentPriority(int priority);

bool Storm_SetProcessAffinity(uint32_t mask);

struct SLockObj {
    std::atomic<bool> initialized{false};
    std::recursive_mutex mutex;
};

int SLock_InitAndEnter(SLockObj* lock);

void SLock_Leave(SLockObj* lock);

void SSignature_Create(void **outCtx, int signatureSize, int exponentSize);

int SSignature_GetTrailingSize(void *ctx);

void SSignature_Update(void *ctx, const void *data, size_t size);

bool SSignature_Verify(void *ctx, const void *modulusBytes, const void *exponentBytes);

bool SSignature_VerifyData(const void *data, size_t dataSize, const void *modulusBytes,
                           int signatureSize, const void *exponentBytes, int exponentSize);

bool ReadRegistryValue(const char *subKey, const char *valueName, uint8_t type, uint32_t *outValue);

bool WriteRegistryValue(const char *subKey, const char *valueName, uint8_t flags, uint32_t value);

}

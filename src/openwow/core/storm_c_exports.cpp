
#include "openwow/core/storm_string.h"
#include "openwow/core/storm_error.h"

extern "C" {

void *SMemAlloc(size_t size, const char *file, int line, int flags) {
  return openwow::core::SMemAlloc(size, file, line, flags);
}

bool SMemFree(void *ptr, const char *file, int line, int flags) {
  return openwow::core::SMemFree(ptr, file, line, flags);
}

size_t SStrCopy(char *dst, const char *src, size_t maxChars) {
  return openwow::core::SStrCopy(dst, src, maxChars);
}

int SStrCmpNoCase(const char *s1, const char *s2, size_t maxCount) {
  return openwow::core::SStrCmpNoCase(s1, s2, maxCount);
}

void SErrSetLastError(int code) {
  openwow::core::SErrSetLastError(code);
}

}

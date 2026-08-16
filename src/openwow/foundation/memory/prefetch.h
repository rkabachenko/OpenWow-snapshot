#pragma once

#include <cstddef>

namespace openwow::memory {

enum class PrefetchLocality : int {

  kNonTemporal = 0,
  kLow = 1,
  kModerate = 2,

  kHigh = 3,
};

template <PrefetchLocality kLocality = PrefetchLocality::kHigh>
inline void PrefetchForRead([[maybe_unused]] const void *const address) noexcept {
#if defined(__GNUC__) || defined(__clang__)
  __builtin_prefetch(address, 0, static_cast<int>(kLocality));
#elif defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
  _mm_prefetch(static_cast<const char *>(address),
               static_cast<int>(kLocality) == 0 ? 0
                                                : 3 );
#else

#endif
}

template <PrefetchLocality kLocality = PrefetchLocality::kHigh>
inline void PrefetchForWrite([[maybe_unused]] const void *const address) noexcept {
#if defined(__GNUC__) || defined(__clang__)
  __builtin_prefetch(address, 1, static_cast<int>(kLocality));
#else
  PrefetchForRead<kLocality>(address);
#endif
}

template <std::size_t kDistance = 2u, typename Pointer>
inline void PrefetchAheadForRead(const Pointer *const entries,
                                 const std::size_t index,
                                 const std::size_t count) noexcept {
  const std::size_t ahead = index + kDistance;
  if (ahead < count) {
    PrefetchForRead(static_cast<const void *>(entries[ahead]));
  }
}

}

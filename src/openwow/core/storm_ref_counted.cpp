#include "openwow/core/storm_ref_counted.h"

#include "openwow/core/storm_error.h"

#include <cstddef>

namespace openwow::core {

namespace {

constexpr int kErrorInvalidParameter = 87;
constexpr std::size_t kStormRefCountOffset = sizeof(void*);
using StormDestroyMethod = int (*)(void*, int);

std::uint32_t& StormRefCount(void* object) {
  auto* const bytes = static_cast<std::byte*>(object);
  return *reinterpret_cast<std::uint32_t*>(bytes + kStormRefCountOffset);
}

StormDestroyMethod StormDestroy(void* object) {
  return (*reinterpret_cast<StormDestroyMethod* const*>(object))[0];
}

}

std::uintptr_t StormRefCounted_AddRefChecked(void* object) {
  if (!object) {
    SErrSetLastError(kErrorInvalidParameter);
    return 0;
  }

  ++StormRefCount(object);
  return reinterpret_cast<std::uintptr_t>(object);
}

std::uintptr_t StormRefCounted_AddRefIfNonNull(void* object) {
  if (!object) {
    return 0;
  }

  ++StormRefCount(object);
  return reinterpret_cast<std::uintptr_t>(object);
}

void StormRefCounted_Release(void* object) {
  auto& ref_count = StormRefCount(object);
  --ref_count;
  if (ref_count == 0) {
    StormDestroy(object)(object, 1);
  }
}

}

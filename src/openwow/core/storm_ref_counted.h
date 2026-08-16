#pragma once

#include <cstdint>

namespace openwow::core {

std::uintptr_t StormRefCounted_AddRefChecked(void* object);

std::uintptr_t StormRefCounted_AddRefIfNonNull(void* object);

void StormRefCounted_Release(void* object);

}

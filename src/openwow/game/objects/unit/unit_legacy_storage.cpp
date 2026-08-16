#include "openwow/game/objects/cgunit.h"

#include <cstddef>
#include <cstdint>

namespace openwow::game {

void CGUnit_C::InitVf0_ScalarDeletingDestructor(void *block, bool free_mem) {
  if (!block)
    return;
  if (free_mem) {
    delete[] static_cast<char *>(block);
  }
}

void *CGUnit_C::InitVf67_AllocInventoryArt(int extra_bytes, bool ) {
  auto *block = new char[static_cast<std::size_t>(extra_bytes + 28)]();

  return block;
}

void CGUnit_C::CleanupInventoryArtBlock(void *block) {
  auto *p = static_cast<std::uint32_t *>(block);

  p[4] = 0;

  if (p[7]) {
    delete[] reinterpret_cast<char *>(static_cast<std::uintptr_t>(p[7]));
    p[7] = 0;
  }
  p[5] = 0;
  p[6] = 0;

}

void CGUnit_C::InitVf68_DestroyInventoryArt(void *block, bool free_mem) {
  if (!block)
    return;
  CleanupInventoryArtBlock(block);
  if (free_mem) {
    delete[] static_cast<char *>(block);
  }
}

void CGUnit_C::InitVf69_ResetInventoryArt(void *block) {
  if (!block)
    return;
  auto *p = static_cast<std::uint32_t *>(block);
  p[4] = 0;

  if (p[7]) {
    delete[] reinterpret_cast<char *>(static_cast<std::uintptr_t>(p[7]));
    p[7] = 0;
  }

  p[9] = static_cast<std::uint32_t>(-1);
  p[5] = 0;
  p[6] = 0;
  p[7] = 0;
}

void CGUnit_C::InitVf70_FreeItemByName(void *block) {
  delete[] static_cast<char *>(block);
}

void *CGUnit_C::InitVf71_AllocItemByName(int extra_bytes, bool ) {
  auto *block = new char[static_cast<std::size_t>(extra_bytes + 28)]();
  return block;
}

void CGUnit_C::CleanupItemByNameBlock(void *block) {
  auto *p = static_cast<std::uint32_t *>(block);

  p[4] = 0;

  if (p[7]) {
    delete[] reinterpret_cast<char *>(static_cast<std::uintptr_t>(p[7]));
    p[7] = 0;
  }
  p[5] = 0;
  p[6] = 0;

}

void CGUnit_C::InitVf72_DestroyItemByName(void *block, bool free_mem) {
  if (!block)
    return;
  CleanupItemByNameBlock(block);
  if (free_mem) {
    delete[] static_cast<char *>(block);
  }
}

void CGUnit_C::InitVf73_ResetItemByName(void *block) {
  if (!block)
    return;
  auto *p = static_cast<std::uint32_t *>(block);

  p[4] = 0;

  p[9] = static_cast<std::uint32_t>(-1);

  p[5] = 0;
  p[6] = 0;
  p[7] = 0;
}

}

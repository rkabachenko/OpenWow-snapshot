#pragma once

#include "openwow/net/serialization/cdatastore_vtable.h"
#include "openwow/net/serialization/wdatastore.h"

#include <cstdint>

namespace openwow::net {

inline constexpr std::uint32_t kVtable_CDataStore_TempWorldPacket = 0x9E2148;

struct CDataStoreTempWorldPacket {
  CDataStore store{};

  std::uintptr_t pooled_block_base = 0;
  std::uint32_t reserved_1c = 0;
  std::uint32_t growth_quantum = 0;
};

using CDataStoreTempPacket = CDataStoreTempWorldPacket;

inline int CDataStore__WriteTempWorldPacketWindow(
    CDataStore* store, std::uint32_t offset, std::uint32_t length,
    std::uint8_t** data, std::uint32_t* window_base,
    std::uint32_t* window_size, const char* ,
    std::uint32_t ) {
  if (!store || !data || !window_base || !window_size) {
    return 0;
  }

  auto* packet = reinterpret_cast<CDataStoreTempWorldPacket*>(store);
  const auto required_size = offset + length;
  if (required_size <= *window_size) {
    return 1;
  }

  auto* grown = WDataStore_GrowBuffer(*data, *window_size, required_size,
                                      *window_size, packet->growth_quantum);
  if (!grown) {
    return 0;
  }

  *data = grown;
  *window_base = 0;
  *window_size = static_cast<std::uint32_t>(WDataStore_ResolveGrowthCapacity(
      *window_size, required_size, packet->growth_quantum));
  if (*window_size <= kWDataStoreSuperBufferCapacity) {
    packet->pooled_block_base =
        reinterpret_cast<std::uintptr_t>(*data) - kWDataStorePooledHeaderSize;
  } else {
    packet->pooled_block_base = 0;
  }
  return 1;
}

[[nodiscard]] inline const CDataStoreVTable* CDataStore_TempWorldPacketVTable() {
  static const CDataStoreVTable kTempWorldPacketVTable{
      nullptr,
      nullptr,
      nullptr,
      &CDataStore__WriteTempWorldPacketWindow,
  };
  return &kTempWorldPacketVTable;
}

inline std::uint32_t* CDataStore__InitTempWorldPacketStorage(
    CDataStoreTempWorldPacket* packet, std::uint8_t** data,
    std::uint32_t* window_base, int* window_size) {
  packet->pooled_block_base = 0;
  if (window_size) {
    *window_size = 0;
  }
  if (data) {
    *data = nullptr;
  }
  if (window_base) {
    *window_base = 0;
  }
  packet->growth_quantum = kWDataStoreDefaultGrowthQuantum;
  return window_base;
}

[[nodiscard]] inline std::uint8_t*
CDataStore__GetTempWorldPacketPrefixPointer(CDataStoreTempWorldPacket* packet,
                                            std::uint32_t prefix_bytes) {
  if (!packet || packet->pooled_block_base == 0 ||
      prefix_bytes > kWDataStorePooledPrefixWritableBytes) {
    return nullptr;
  }

  return reinterpret_cast<std::uint8_t*>(packet->pooled_block_base +
                                         kWDataStorePooledHeaderSize -
                                         prefix_bytes);
}

inline int* CDataStore__CleanupTempWorldPacketStorage(
    CDataStoreTempWorldPacket* packet, std::uint8_t** data,
    std::uint32_t* window_base, int* window_size) {
  if (!packet || !data || !window_base || !window_size) {
    return window_size;
  }

  if (packet->pooled_block_base != 0) {
    switch (static_cast<std::uint32_t>(*window_size)) {
    case kWDataStoreSmallBufferCapacity:
    case kWDataStoreLargeBufferCapacity:
    case kWDataStoreSuperBufferCapacity:
      WDataStore_FreeBuffer(*data, static_cast<std::uint32_t>(*window_size));
      break;
    default:
      break;
    }
    packet->pooled_block_base = 0;
  } else if (*data != nullptr) {
    core::SMemFree(*data, kCDataStoreSourceFile, 263, 0);
  }

  *data = nullptr;
  *window_base = 0;
  *window_size = 0;
  packet->pooled_block_base = 0;
  return window_size;
}

}

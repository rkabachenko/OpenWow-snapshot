#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>

#include "openwow/network/serialization/packed_guid_codec.h"

namespace openwow::net {

[[maybe_unused]] static constexpr std::uint32_t kVtable_CDataStore_Base =
    0x9E0E24;

struct CDataStore;

using CDataStoreWriteWindowFn =
    int (*)(CDataStore* store, std::uint32_t offset, std::uint32_t length,
            std::uint8_t** data, std::uint32_t* window_base,
            std::uint32_t* window_size, const char* source_file,
            std::uint32_t source_line);
using CDataStoreReadWindowFn =
    int (*)(CDataStore* store, std::uint32_t offset, std::uint32_t length,
            std::uint8_t** data, std::uint32_t* window_base,
            std::uint32_t* window_size);

struct CDataStoreVTable {
  void* slot0_init = nullptr;
  void* slot1_cleanup = nullptr;
  CDataStoreReadWindowFn slot2_read_window = nullptr;
  CDataStoreWriteWindowFn slot3_write_window = nullptr;
};

struct CDataStore {
  const CDataStoreVTable* vtable = nullptr;
  std::uint8_t* data = nullptr;
  std::uint32_t window_base = 0;
  std::uint32_t window_size = 0;
  std::uint32_t write_pos = 0;
  std::uint32_t read_pos = 0;
};

[[nodiscard]] inline bool CDataStore_AtEnd(const CDataStore& store) {
  return store.read_pos == store.write_pos;
}

[[nodiscard]] inline bool CDataStore_EnsureReadSpan(CDataStore& store,
                                                    std::uint32_t offset,
                                                    std::uint32_t length) {
  const std::uint32_t end = offset + length;
  if (end > store.write_pos) {
    store.read_pos = store.write_pos + 1;
    return false;
  }

  const std::uint32_t window_end = store.window_base + store.window_size;
  if (offset >= store.window_base && end <= window_end) {
    return true;
  }

  if (store.vtable && store.vtable->slot2_read_window &&
      store.vtable->slot2_read_window(&store, offset, length, &store.data,
                                      &store.window_base,
                                      &store.window_size) != 0) {
    return true;
  }

  store.read_pos = store.write_pos + 1;
  return false;
}

bool CDataStore_EnsureWriteSpan(CDataStore& store, std::uint32_t offset,
                                std::uint32_t length,
                                const char* source_file = nullptr,
                                std::uint32_t source_line = 0);

namespace detail {

[[nodiscard]] inline bool CDataStore_HasResidentWriteSpan(
    const CDataStore& store, const std::uint32_t offset,
    const std::uint32_t length) {
  const std::uint32_t window_end = store.window_base + store.window_size;
  return offset >= store.window_base && offset + length <= window_end;
}

inline void CDataStore_RequestWriteSpan(CDataStore& store,
                                        const std::uint32_t offset,
                                        const std::uint32_t length) {
  if (CDataStore_HasResidentWriteSpan(store, offset, length)) {
    return;
  }

  if (store.vtable && store.vtable->slot3_write_window) {
    store.vtable->slot3_write_window(&store, offset, length, &store.data,
                                     &store.window_base, &store.window_size,
                                     nullptr, 0);
  }
}

[[nodiscard]] inline std::uint8_t* CDataStore_RawWritePtr(
    CDataStore& store, const std::uint32_t offset) {
  const auto base = reinterpret_cast<std::intptr_t>(store.data);
  const auto delta = static_cast<std::intptr_t>(offset) -
                     static_cast<std::intptr_t>(store.window_base);
  return reinterpret_cast<std::uint8_t*>(base + delta);
}

[[nodiscard]] inline const std::uint8_t* CDataStore_RawReadPtr(
    const CDataStore& store, const std::uint32_t offset) {
  const auto base = reinterpret_cast<std::intptr_t>(store.data);
  const auto delta = static_cast<std::intptr_t>(offset) -
                     static_cast<std::intptr_t>(store.window_base);
  return reinterpret_cast<const std::uint8_t*>(base + delta);
}

template <typename T>
inline bool CDataStore_PutFixedWidthAt(CDataStore& store,
                                       std::uint32_t offset,
                                       const T& value) {
  static_assert(std::is_trivially_copyable_v<T>);

  const auto byte_count = static_cast<std::uint32_t>(sizeof(T));
  CDataStore_RequestWriteSpan(store, offset, byte_count);

  std::memcpy(CDataStore_RawWritePtr(store, offset), &value, sizeof(T));
  return true;
}

template <typename T>
inline bool CDataStore_PutFixedWidth(CDataStore& store, const T& value) {
  static_assert(std::is_trivially_copyable_v<T>);

  const auto byte_count = static_cast<std::uint32_t>(sizeof(T));
  const auto write_pos = store.write_pos;
  CDataStore_RequestWriteSpan(store, write_pos, byte_count);

  std::memcpy(CDataStore_RawWritePtr(store, write_pos), &value, sizeof(T));
  store.write_pos = write_pos + byte_count;
  return true;
}

}

inline void CDataStore_PutUInt8At(CDataStore& store, std::uint32_t offset,
                                  std::uint8_t value) {
  detail::CDataStore_PutFixedWidthAt(store, offset, value);
}

inline void CDataStore_PutUInt16At(CDataStore& store, std::uint32_t offset,
                                   std::uint16_t value) {
  detail::CDataStore_PutFixedWidthAt(store, offset, value);
}

inline void CDataStore_PutUInt32At(CDataStore& store, std::uint32_t offset,
                                   std::uint32_t value) {
  detail::CDataStore_PutFixedWidthAt(store, offset, value);
}

inline void CDataStore_PutInt8(CDataStore& store, std::int8_t value) {
  const auto write_pos = store.write_pos;
  detail::CDataStore_RequestWriteSpan(store, write_pos, 1);
  *detail::CDataStore_RawWritePtr(store, write_pos) =
      static_cast<std::uint8_t>(value);
  store.write_pos = write_pos + 1;
}

inline void CDataStore_PutUInt16(CDataStore& store, std::uint16_t value) {
  detail::CDataStore_PutFixedWidth(store, value);
}

inline void CDataStore_PutUInt32(CDataStore& store, std::uint32_t value) {
  detail::CDataStore_PutFixedWidth(store, value);
}

inline void CDataStore_PutUInt64(CDataStore& store, std::uint64_t value) {
  detail::CDataStore_PutFixedWidth(store, value);
}

inline void CDataStore_PutFloat(CDataStore& store, float value) {
  detail::CDataStore_PutFixedWidth(store, value);
}

inline void CDataStore_PutFloat3(CDataStore& store, const float* xyz) {
  if (!xyz) {
    return;
  }

  CDataStore_PutFloat(store, xyz[0]);
  CDataStore_PutFloat(store, xyz[1]);
  CDataStore_PutFloat(store, xyz[2]);
}

void CDataStore_PutString(CDataStore& store, const char* str);

inline void CDataStore_GetUInt8(CDataStore& store, std::uint8_t* out) {
  const std::uint32_t read_pos = store.read_pos;
  if (!CDataStore_EnsureReadSpan(store, read_pos, 1)) {
    return;
  }

  *out = *detail::CDataStore_RawReadPtr(store, read_pos);
  store.read_pos = read_pos + 1;
}

[[nodiscard]] inline std::uint32_t CDataStore_GetUInt8Value(
    CDataStore& store) {
  std::uint8_t value = 0;
  CDataStore_GetUInt8(store, &value);
  return value;
}

inline void CDataStore_GetUInt8AsUInt32(CDataStore& store,
                                        std::uint32_t* out) {
  std::uint8_t value = 0;
  CDataStore_GetUInt8(store, &value);
  *out = value;
}

inline void CDataStore_GetUInt16(CDataStore& store, std::uint16_t* out) {
  const std::uint32_t read_pos = store.read_pos;
  if (!CDataStore_EnsureReadSpan(store, read_pos, 2)) {
    return;
  }

  std::memcpy(out, detail::CDataStore_RawReadPtr(store, read_pos),
              sizeof(*out));
  store.read_pos = read_pos + 2;
}

inline void CDataStore_GetUInt32(CDataStore& store, std::uint32_t* out) {
  const std::uint32_t read_pos = store.read_pos;
  if (!CDataStore_EnsureReadSpan(store, read_pos, 4)) {
    return;
  }

  std::memcpy(out, detail::CDataStore_RawReadPtr(store, read_pos),
              sizeof(*out));
  store.read_pos = read_pos + 4;
}

inline void CDataStore_GetUInt64(CDataStore& store, std::uint64_t* out) {
  const std::uint32_t read_pos = store.read_pos;
  if (!CDataStore_EnsureReadSpan(store, read_pos, 8)) {
    return;
  }

  std::memcpy(out, detail::CDataStore_RawReadPtr(store, read_pos),
              sizeof(*out));
  store.read_pos = read_pos + 8;
}

inline void CDataStore_GetFloat(CDataStore& store, float* out) {
  const std::uint32_t read_pos = store.read_pos;
  if (!CDataStore_EnsureReadSpan(store, read_pos, 4)) {
    return;
  }

  std::memcpy(out, detail::CDataStore_RawReadPtr(store, read_pos),
              sizeof(*out));
  store.read_pos = read_pos + 4;
}

inline void CDataStore_GetFloat3(CDataStore& store, float* out_xyz) {
  if (!out_xyz) {
    return;
  }

  CDataStore_GetFloat(store, out_xyz);
  CDataStore_GetFloat(store, out_xyz + 1);
  CDataStore_GetFloat(store, out_xyz + 2);
}

inline bool CDataStore_GetReadSpanPointer(CDataStore& store,
                                          const std::uint8_t*& out,
                                          std::uint32_t length) {
  out = nullptr;

  const std::uint32_t read_pos = store.read_pos;
  if (!CDataStore_EnsureReadSpan(store, read_pos, length)) {
    return false;
  }

  out = detail::CDataStore_RawReadPtr(store, read_pos);
  store.read_pos = read_pos + length;
  return true;
}

void CDataStore_GetString(CDataStore& store, char* out,
                          std::uint32_t length);

void CDataStore_GetUInt32Array(CDataStore& store, std::uint32_t* out,
                               std::uint32_t count);

void CDataStore_GetBytes(CDataStore& store, void* out, std::uint32_t length);

void CDataStore_PutBytes(CDataStore& store, const void* data,
                         std::uint32_t length);

inline void CDataStore_PutPackedGuid(CDataStore& store, std::uint64_t guid) {
  const std::uint32_t mask_offset = store.write_pos;
  CDataStore_PutInt8(store, 0);

  const std::uint8_t mask = PackedGuidMask(guid);
  for (std::uint8_t i = 0; i < 8; ++i) {
    const auto byte = PackedGuidSourceByte(guid, i);
    if (byte != 0) {
      CDataStore_PutInt8(store, static_cast<std::int8_t>(byte));
    }
  }

  CDataStore_PutUInt8At(store, mask_offset, mask);
}

inline void CDataStore_GetPackedGuid(CDataStore& store, std::uint64_t* out) {
  *out = 0;

  std::uint8_t mask = 0;
  CDataStore_GetUInt8(store, &mask);

  for (int i = 0; i < 8; ++i) {
    if (mask & (1u << i)) {
      std::uint8_t byte = 0;
      CDataStore_GetUInt8(store, &byte);
      *out |= static_cast<std::uint64_t>(byte) << (i * 8);
    }
  }
}

}

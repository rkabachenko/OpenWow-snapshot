#pragma once

#include "openwow/core/shared_stubs.h"
#include "openwow/core/storm_string.h"
#include "openwow/net/serialization/cdatastore_ops.h"

namespace openwow::net {

inline constexpr char kCDataStoreSourceFile[] =
    "d:\\buildserver\\wow\\1\\work\\wow-code\\branches\\wow-patch-3_3_5_a-bnet\\engine\\source\\base\\CDataStore.h";

inline bool CDataStore_GrowWithSource(std::uint8_t*& data, std::uint32_t offset,
                                      std::uint32_t length,
                                      std::uint32_t& window_size,
                                      const char* source_file,
                                      std::uint32_t source_line) {
  const std::uint32_t new_alloc = (offset + length + 255) & 0xFFFFFF00u;
  window_size = new_alloc;
  const char* resolved_source =
      source_file != nullptr ? source_file : kCDataStoreSourceFile;
  const int resolved_line =
      source_file != nullptr ? static_cast<int>(source_line) : 280;
  data = static_cast<std::uint8_t*>(
      core::SMemReAlloc(data, new_alloc, resolved_source, resolved_line, 0));
  return true;
}

inline int CDataStore__vf3_WriteWindow(
    CDataStore* , std::uint32_t offset, std::uint32_t length,
    std::uint8_t** data, std::uint32_t* ,
    std::uint32_t* window_size, const char* source_file,
    std::uint32_t source_line) {
  CDataStore_GrowWithSource(*data, offset, length, *window_size, source_file,
                            source_line);
  return 1;
}

[[nodiscard]] inline const CDataStoreVTable* CDataStore_BaseVTable() {
  static const CDataStoreVTable kBaseVTable{
      nullptr,
      nullptr,
      nullptr,
      &CDataStore__vf3_WriteWindow,
  };
  return &kBaseVTable;
}

inline void CDataStore_Destruct(CDataStore& store);

inline bool CDataStore_IsComplete(const CDataStore& store) {
  return CDataStore_AtEnd(store);
}

inline void CDataStore_Reset(CDataStore& store) {
  if (store.window_size == 0xFFFFFFFFu) {
    store.data = nullptr;
    store.window_size = 0;
  }

  const auto write_window =
      store.vtable ? store.vtable->slot3_write_window : nullptr;
  if (store.window_base != 0 && write_window) {
    write_window(&store, 0, 0, &store.data, &store.window_base,
                 &store.window_size, nullptr, 0);
  }

  store.read_pos = 0xFFFFFFFFu;
  store.write_pos = 0;
}

inline void CDataStore_ResetReadPos(CDataStore& store) {
  store.read_pos = 0;
}

inline void CDataStore_GetState(const CDataStore& store,
                                std::uint8_t** out_data,
                                std::uint32_t* out_size,
                                std::uint32_t* out_alloc) {
  if (out_data) *out_data = store.data;
  if (out_size) *out_size = store.write_pos;
  if (out_alloc) *out_alloc = store.window_size;
}

inline void CDataStore_GetAndDetach(CDataStore& store, std::uint8_t** out_data,
                                    std::uint32_t* out_size,
                                    std::uint32_t* out_alloc) {
  CDataStore_GetState(store, out_data, out_size, out_alloc);
  store.data = nullptr;
  store.window_size = 0;
  CDataStore_Reset(store);
}

inline void CDataStore_Free(std::uint8_t*& data, std::uint32_t& window_base,
                            std::uint32_t& window_size) {
  if (window_size != 0 && data != nullptr) {
    core::SMemFree(data, kCDataStoreSourceFile, 263, 0);
  }
  data = nullptr;
  window_base = 0;
  window_size = 0;
}

inline bool CDataStore_Grow(std::uint8_t*& data, std::uint32_t current_size,
                            std::uint32_t needed_size,
                            std::uint32_t& window_size) {
  return CDataStore_GrowWithSource(data, current_size, needed_size,
                                   window_size, nullptr, 0);
}

inline CDataStore* CDataStore_Dtor(CDataStore* store) {
  CDataStore_Destruct(*store);
  return store;
}

inline void CDataStore_Destruct(CDataStore& store) {
  store.vtable = CDataStore_BaseVTable();
  if (store.window_size != 0xFFFFFFFFu) {
    CDataStore_Free(store.data, store.window_base, store.window_size);
  }
}

inline CDataStore* CDataStore_DeleteDtor(CDataStore* store, char flags) {
  CDataStore_Dtor(store);
  if ((flags & 1) != 0) {
    core::SMemFree(store, "delete", -1, 0);
  }
  return store;
}

inline int CDataStore_Vf10_ReturnZero(int a1) {
  return core::IdaSharedReturnZeroArg1(a1);
}

inline bool CDataStore_GetUInt32(CDataStore& store,
                                 std::uint32_t& out_value) {
  const std::uint32_t read_pos = store.read_pos;
  if (!CDataStore_EnsureReadSpan(store, read_pos, 4)) {
    return false;
  }

  std::memcpy(&out_value, detail::CDataStore_RawReadPtr(store, read_pos),
              sizeof(out_value));
  store.read_pos = read_pos + 4;
  return true;
}

[[nodiscard]] inline std::uint32_t CDataStore_GetUInt32Value(
    CDataStore& store) {
  std::uint32_t value = 0;
  CDataStore_GetUInt32(store, value);
  return value;
}

}

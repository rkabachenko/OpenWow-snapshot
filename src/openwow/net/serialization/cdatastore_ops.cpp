
#include "openwow/net/serialization/cdatastore_ops.h"

#include "openwow/core/storm_error.h"

#include <algorithm>
#include <cstring>

namespace openwow::net {

namespace {

constexpr int kErrorInvalidParameter = 87;

}

bool CDataStore_EnsureWriteSpan(CDataStore& store, std::uint32_t offset,
                                std::uint32_t length,
                                const char* source_file,
                                std::uint32_t source_line) {
  const std::uint32_t window_end = store.window_base + store.window_size;
  if (offset >= store.window_base && offset + length <= window_end) {
    return true;
  }

  return store.vtable && store.vtable->slot3_write_window &&
         store.vtable->slot3_write_window(
             &store, offset, length, &store.data, &store.window_base,
             &store.window_size, source_file, source_line) != 0;
}

void CDataStore_GetString(CDataStore& store, char* out, std::uint32_t length) {
  if (!out) {
    if (length != 0) {
      openwow::core::SErrSetLastError(kErrorInvalidParameter);
    }
    return;
  }

  if (length == 0) {
    return;
  }

  if (store.read_pos <= store.write_pos &&
      CDataStore_EnsureReadSpan(store, store.read_pos, 1)) {
    std::uint32_t copied = 0;
    do {
      const std::uint32_t window_end =
          std::min(store.window_base + store.window_size, store.write_pos);
      std::uint32_t chunk = window_end - store.read_pos;
      const std::uint32_t remaining = length - copied;
      if (chunk >= remaining) {
        chunk = remaining;
      }

      const auto* src = reinterpret_cast<const char*>(
          detail::CDataStore_RawReadPtr(store, store.read_pos));
      std::uint32_t chunk_read = 0;
      while (chunk_read < chunk) {
        const char value = src[chunk_read];
        out[copied++] = value;
        ++chunk_read;
        if (value == '\0') {
          store.read_pos += chunk_read;
          return;
        }
      }

      store.read_pos += chunk_read;
      if (copied >= length) {
        store.read_pos = store.write_pos + 1;
        break;
      }
    } while (CDataStore_EnsureReadSpan(store, store.read_pos, 1));
  }

  if (store.read_pos > store.write_pos) {
    out[0] = '\0';
  }
}

void CDataStore_GetUInt32Array(CDataStore& store, std::uint32_t* out,
                               std::uint32_t count) {
  if (!out) {
    if (count != 0) {
      openwow::core::SErrSetLastError(kErrorInvalidParameter);
    }
    return;
  }

  if (count == 0 || store.read_pos > store.write_pos) {
    return;
  }

  auto* dst = reinterpret_cast<std::uint8_t*>(out);
  std::uint32_t remaining = count * 4u;
  while (remaining != 0) {
    const std::uint32_t read_pos = store.read_pos;
    std::uint32_t chunk = store.write_pos - read_pos;
    if (chunk >= remaining) {
      chunk = remaining;
    }
    if (chunk >= store.window_size) {
      chunk = store.window_size;
    }
    if (chunk <= 4) {
      chunk = 4;
    }
    chunk &= 0xFFFFFFFCu;

    if (!CDataStore_EnsureReadSpan(store, read_pos, chunk)) {
      break;
    }

    std::memcpy(dst, detail::CDataStore_RawReadPtr(store, read_pos), chunk);
    dst += chunk;
    store.read_pos = read_pos + chunk;
    remaining -= chunk;
  }
}

void CDataStore_GetBytes(CDataStore& store, void* out, std::uint32_t length) {
  if (!out) {
    if (length != 0) {
      openwow::core::SErrSetLastError(kErrorInvalidParameter);
    }
    return;
  }

  if (length == 0 || store.read_pos > store.write_pos) {
    return;
  }

  auto* dst = static_cast<std::uint8_t*>(out);
  std::uint32_t remaining = length;
  while (remaining != 0) {
    const std::uint32_t read_pos = store.read_pos;
    std::uint32_t chunk = store.write_pos - read_pos;
    if (chunk >= remaining) {
      chunk = remaining;
    }
    if (chunk >= store.window_size) {
      chunk = store.window_size;
    }
    if (chunk <= 1) {
      chunk = 1;
    }

    if (!CDataStore_EnsureReadSpan(store, read_pos, chunk)) {
      break;
    }

    auto* src = detail::CDataStore_RawReadPtr(store, read_pos);
    if (dst != src) {
      std::memcpy(dst, src, chunk);
    }

    dst += chunk;
    store.read_pos = read_pos + chunk;
    remaining -= chunk;
  }
}

void CDataStore_PutBytes(CDataStore& store, const void* data,
                         std::uint32_t length) {
  if (!data) {
    if (length != 0) {
      openwow::core::SErrSetLastError(kErrorInvalidParameter);
    }
    return;
  }

  if (length == 0) {
    return;
  }

  CDataStore_EnsureWriteSpan(store, store.write_pos, length, nullptr, 0);

  auto* src = static_cast<const std::uint8_t*>(data);
  std::uint32_t remaining = length;
  while (remaining != 0) {
    std::uint32_t chunk = remaining;
    if (chunk >= store.window_size) {
      chunk = store.window_size;
    }
    if (chunk <= 1) {
      chunk = 1;
    }

    const std::uint32_t write_pos = store.write_pos;
    const std::uint32_t window_end = store.window_base + store.window_size;
    if (write_pos < store.window_base || write_pos + chunk > window_end) {
      CDataStore_EnsureWriteSpan(store, write_pos, chunk, nullptr, 0);
    }

    auto* dst = store.data + store.write_pos - store.window_base;
    if (dst != src) {
      std::memcpy(dst, src, chunk);
    }

    src += chunk;
    store.write_pos += chunk;
    remaining -= chunk;
  }
}

void CDataStore_PutString(CDataStore& store, const char* str) {
  if (!str) {
    openwow::core::SErrSetLastError(kErrorInvalidParameter);
    return;
  }

  const auto length = static_cast<std::uint32_t>(std::strlen(str));
  CDataStore_PutBytes(store, str, length + 1);
}

}

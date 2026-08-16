#include "openwow/data/texture_cache.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <unordered_set>
#include <unordered_map>
#include <utility>

#include "openwow/core/storm_string.h"
#include "openwow/data/async_file_read.h"

namespace openwow::data {

namespace {

constexpr std::array<std::uint32_t, 16> kStormNibbleHashTable = {
    1215178478u, 3702134451u, 3784412911u, 539865051u,
    874282439u,  473322243u,  1089416503u, 1711103561u,
    3590680951u, 2421083795u, 473432655u,  2566730299u,
    3808828135u, 2744848289u, 2543317599u, 3829679459u,
};

constexpr std::uint32_t kInitialPathHash = 2146271213u;

constexpr std::uint32_t kInitialCollisionMix = 0xEEEEEEEEu;
constexpr std::size_t kTextureCacheRowHeaderPrefixSize = 20u;
constexpr std::uint32_t kBlp2Magic = 0x32504C42u;

[[nodiscard]] std::string MakeRetailTextureSourcePath(
    const std::string_view path, const std::string_view suffix) {
  const std::size_t separator = path.find_last_of("\\/");
  const std::size_t extension = path.find_last_of('.');
  const std::size_t suffix_offset =
      extension != std::string_view::npos &&
              (separator == std::string_view::npos || extension > separator)
          ? extension
          : path.size();
  std::string source(path.substr(0u, suffix_offset));
  source.append(suffix);
  return source;
}

[[nodiscard]] std::uint64_t NextGeneration(const std::uint64_t generation) {
  return generation == std::numeric_limits<std::uint64_t>::max()
             ? 1u
             : generation + 1u;
}

std::uint16_t ReadU16LE(const std::uint8_t* data, const std::size_t offset) {
  return static_cast<std::uint16_t>(data[offset] |
                                    (static_cast<std::uint16_t>(data[offset + 1]) << 8));
}

std::uint32_t ReadU32LE(const std::uint8_t* data, const std::size_t offset) {
  return static_cast<std::uint32_t>(data[offset]) |
         (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
         (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
         (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

std::uint8_t CountTextureCacheRowMipLevels(std::uint16_t width,
                                           std::uint16_t height) {
  std::uint32_t current_width = width;
  std::uint32_t current_height = height;
  std::uint8_t count = 1;

  while (current_width > 1u || current_height > 1u) {
    current_width = std::max(current_width >> 1, 1u);
    current_height = std::max(current_height >> 1, 1u);
    ++count;
  }

  return count;
}

}

std::string MakeRetailTextureCacheBlpPath(
    const std::string_view stored_path) {
  return MakeRetailTextureSourcePath(stored_path, ".blp");
}

std::string MakeRetailTextureCacheTgaPath(
    const std::string_view stored_path) {
  return MakeRetailTextureSourcePath(stored_path, ".tga");
}

std::uint32_t HashTextureCachePath(const std::string_view path) {
  std::uint32_t hash = kInitialPathHash;
  std::uint32_t mix = kInitialCollisionMix;

  for (unsigned char value : path) {
    if (value == 0) {
      break;
    }
    if (value >= 'a' && value <= 'z') {
      value = static_cast<unsigned char>(value - ('a' - 'A'));
    }
    if (value == '/') {
      value = '\\';
    }

    hash = (kStormNibbleHashTable[value >> 4] -
            kStormNibbleHashTable[value & 0x0Fu]) ^
           (mix + hash);
    mix += value + 32u * mix + hash + 3u;
  }

  return hash == 0 ? 1u : hash;
}

bool TextureCachePathsAlias(const std::string_view lhs,
                            const std::string_view rhs) noexcept {
  const auto folded = [](unsigned char value) {
    if (value >= 'a' && value <= 'z') {
      value = static_cast<unsigned char>(value - ('a' - 'A'));
    }
    return value == '/' ? static_cast<unsigned char>('\\') : value;
  };

  std::size_t index = 0u;
  while (true) {
    const unsigned char left =
        index < lhs.size() ? static_cast<unsigned char>(lhs[index]) : 0u;
    const unsigned char right =
        index < rhs.size() ? static_cast<unsigned char>(rhs[index]) : 0u;
    if (left == 0u || right == 0u) {
      return left == right;
    }
    if (folded(left) != folded(right)) {
      return false;
    }
    ++index;
  }
}

std::string CopyTextureCacheRowPath(const std::string_view path) {
  const std::size_t nul = path.find('\0');
  const std::size_t c_string_size =
      nul == std::string_view::npos ? path.size() : nul;
  const std::size_t stored_size = std::min(
      c_string_size, kTextureCacheRowPathCapacity - 1u);
  return std::string(path.substr(0, stored_size));
}

struct TextureCacheRowStore::Impl {
  struct Row {
    explicit Row(std::string stored_path) : path(std::move(stored_path)) {}

    std::string path;
    std::shared_ptr<const std::vector<std::uint8_t>> bytes;
    std::condition_variable ready;
    bool loading{false};
    bool terminal_failure{false};
  };

  mutable std::mutex mutex;
  std::unordered_map<std::uint32_t, std::shared_ptr<Row>> rows;
  std::uint64_t generation{1};

  [[nodiscard]] std::shared_ptr<Row> FindOrCreateRow(
      const std::uint32_t row_hash, const std::string_view path) {
    if (const auto found = rows.find(row_hash); found != rows.end()) {
      return found->second;
    }
    auto row = std::make_shared<Row>(CopyTextureCacheRowPath(path));
    rows.emplace(row_hash, row);
    return row;
  }
};

TextureCacheRowStore::TextureCacheRowStore()
    : impl_(std::make_unique<Impl>()) {}

TextureCacheRowStore::~TextureCacheRowStore() = default;

TextureCacheRowIdentity TextureCacheRowStore::Resolve(
    const std::string_view path) {
  const std::uint32_t row_hash = HashTextureCachePath(path);
  std::lock_guard lock(impl_->mutex);
  const auto row = impl_->FindOrCreateRow(row_hash, path);
  return TextureCacheRowIdentity{
      .hash = row_hash,
      .path = row->path,
      .generation = impl_->generation,
  };
}

TextureCacheRowSource TextureCacheRowStore::Load(
    const TextureCacheRowIdentity& identity, const SourceLoader& loader) {
  std::shared_ptr<Impl::Row> row;
  std::uint64_t load_generation = 0;
  {
    std::unique_lock lock(impl_->mutex);
    if (identity.generation != 0u &&
        identity.generation != impl_->generation) {
      return TextureCacheRowSource{.identity = identity};
    }
    row = impl_->FindOrCreateRow(identity.hash, identity.path);
    while (row->loading) {
      row->ready.wait(lock);
    }
    if (identity.generation != 0u &&
        identity.generation != impl_->generation) {
      return TextureCacheRowSource{.identity = identity};
    }
    load_generation = impl_->generation;
    if (row->terminal_failure) {
      return TextureCacheRowSource{
          .identity = {.hash = identity.hash,
                       .path = row->path,
                       .generation = impl_->generation},
      };
    }
    if (row->bytes != nullptr && !row->bytes->empty()) {
      return TextureCacheRowSource{
          .identity = {.hash = identity.hash,
                       .path = row->path,
                       .generation = impl_->generation},
          .bytes = row->bytes,
      };
    }
    if (!loader) {
      row->terminal_failure = true;
      return TextureCacheRowSource{
          .identity = {.hash = identity.hash,
                       .path = row->path,
                       .generation = impl_->generation},
      };
    }
    row->loading = true;
  }

  const auto try_load = [&loader](const std::string& path) {
    try {
      return loader(path);
    } catch (...) {
      return std::vector<std::uint8_t>{};
    }
  };

  std::vector<std::uint8_t> loaded;
  try {
    loaded = try_load(MakeRetailTextureCacheBlpPath(row->path));
    if (loaded.empty()) {
      loaded = try_load(MakeRetailTextureCacheTgaPath(row->path));
    }
  } catch (...) {

    loaded.clear();
  }

  std::shared_ptr<const std::vector<std::uint8_t>> loaded_bytes;
  if (!loaded.empty()) {
    try {
      loaded_bytes = std::make_shared<const std::vector<std::uint8_t>>(
          std::move(loaded));
    } catch (...) {
      loaded_bytes.reset();
    }
  }

  std::shared_ptr<const std::vector<std::uint8_t>> result_bytes;
  {
    std::lock_guard lock(impl_->mutex);
    row->loading = false;
    if (impl_->generation != load_generation) {
      row->bytes.reset();
    } else if (row->terminal_failure || loaded_bytes == nullptr) {
      row->bytes.reset();
      row->terminal_failure = true;
    } else {
      row->bytes = std::move(loaded_bytes);
    }
    result_bytes = row->bytes;
  }
  row->ready.notify_all();
  return TextureCacheRowSource{
      .identity = {.hash = identity.hash,
                   .path = row->path,
                   .generation = load_generation},
      .bytes = std::move(result_bytes),
  };
}

bool TextureCacheRowStore::IsTerminalFailure(
    const std::uint32_t row_hash) const {
  std::lock_guard lock(impl_->mutex);
  const auto row = impl_->rows.find(row_hash);
  return row != impl_->rows.end() && row->second->terminal_failure;
}

bool TextureCacheRowStore::IsCurrent(
    const TextureCacheRowIdentity& identity) const {
  std::lock_guard lock(impl_->mutex);
  if (identity.generation != 0u &&
      identity.generation != impl_->generation) {
    return false;
  }
  const auto row = impl_->rows.find(identity.hash);
  return row != impl_->rows.end() && row->second->path == identity.path;
}

void TextureCacheRowStore::MarkTerminalFailure(
    const TextureCacheRowIdentity& identity) {
  std::lock_guard lock(impl_->mutex);
  if (identity.generation != 0u &&
      identity.generation != impl_->generation) {
    return;
  }
  const auto row = impl_->rows.find(identity.hash);
  if (row == impl_->rows.end() || row->second->path != identity.path) {
    return;
  }
  row->second->bytes.reset();
  row->second->terminal_failure = true;
}

void TextureCacheRowStore::ClearTerminalFailure(
    const TextureCacheRowIdentity& identity) {
  std::lock_guard lock(impl_->mutex);
  if (identity.generation != 0u &&
      identity.generation != impl_->generation) {
    return;
  }
  const auto row = impl_->rows.find(identity.hash);
  if (row == impl_->rows.end() || row->second->path != identity.path) {
    return;
  }
  row->second->terminal_failure = false;
}

void TextureCacheRowStore::ResetTerminalFailures() {
  std::lock_guard lock(impl_->mutex);
  for (const auto& [hash, row] : impl_->rows) {
    (void)hash;
    if (!row->loading && row->terminal_failure) {
      row->terminal_failure = false;
    }
  }
}

void TextureCacheRowStore::InvalidateSources(
    const std::span<const std::uint32_t> retained_row_hashes) {
  const std::unordered_set<std::uint32_t> retained(
      retained_row_hashes.begin(), retained_row_hashes.end());
  std::lock_guard lock(impl_->mutex);
  impl_->generation = NextGeneration(impl_->generation);
  for (auto row = impl_->rows.begin(); row != impl_->rows.end();) {
    if (!retained.contains(row->first)) {
      row = impl_->rows.erase(row);
      continue;
    }
    row->second->bytes.reset();
    row->second->terminal_failure = false;
    ++row;
  }
}

void TextureCacheRowStore::Clear() {
  std::lock_guard lock(impl_->mutex);
  impl_->generation = NextGeneration(impl_->generation);
  impl_->rows.clear();
}

std::size_t TextureCacheRowStore::RowCount() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->rows.size();
}

std::size_t TextureCacheRowStore::TerminalFailureCount() const {
  std::lock_guard lock(impl_->mutex);
  return static_cast<std::size_t>(std::count_if(
      impl_->rows.begin(), impl_->rows.end(), [](const auto& entry) {
        return entry.second->terminal_failure;
      }));
}

void CTextureCacheRow_FreeAsyncReadBuffer(
    const std::uint32_t async_read_object_token) {
  auto* const async_read = static_cast<AsyncFileReadObject*>(
      AsyncFileRead_ResolvePointerToken(async_read_object_token));
  if (async_read == nullptr) {
    return;
  }

  void* const read_buffer = async_read->readBuffer;
  AsyncFileRead_DestroyObject(async_read);
  (void)core::SMemFree(read_buffer, "..\\..\\Common\\TextureCache.cpp", 100, 0);
}

std::optional<TextureCacheRowHeaderState> ParseTextureCacheRowHeaderState(
    const std::uint8_t* data, const std::size_t size,
    const std::uint32_t existing_status) {
  if (data == nullptr || size < kTextureCacheRowHeaderPrefixSize) {
    return std::nullopt;
  }

  TextureCacheRowHeaderState state = MakeInitialTextureCacheRowHeaderState();
  state.is_blp2_version_1 =
      ReadU32LE(data, 0) == kBlp2Magic && ReadU32LE(data, 4) == 1u;
  state.alpha_depth = data[9];
  state.width = ReadU16LE(data, 12);
  state.height = ReadU16LE(data, 16);
  state.mip_count = CountTextureCacheRowMipLevels(state.width, state.height);

  state.packed_status =
      (existing_status & 0xFFFF00FFu) | (static_cast<std::uint32_t>(state.alpha_depth) << 8);
  if (state.alpha_depth == 0) {
    state.packed_status |= 0x10000u;
  } else {
    state.packed_status &= ~0x10000u;
  }
  state.packed_status =
      (state.packed_status & ~0xFFu) | static_cast<std::uint32_t>(state.mip_count);
  return state;
}

}

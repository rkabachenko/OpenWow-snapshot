#include "openwow/render/resources/textures/texture_slice_arrays.h"

#include "openwow/data/blp/blp_texture_loader.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/render/resources/textures/texture_manager.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openwow::render {

namespace detail {

struct ResidentSliceKey {
  std::uint32_t row_hash{0};
  TextureSliceBucket bucket{};

  [[nodiscard]] friend bool operator==(const ResidentSliceKey &,
                                       const ResidentSliceKey &) = default;
};

struct ResidentSliceKeyHash {
  [[nodiscard]] std::size_t operator()(const ResidentSliceKey &key) const noexcept {

    const std::uint64_t bucket = static_cast<std::uint64_t>(key.bucket.width) |
                                 (static_cast<std::uint64_t>(key.bucket.height) << 16u) |
                                 (static_cast<std::uint64_t>(key.bucket.mip_count) << 32u) |
                                 (static_cast<std::uint64_t>(key.bucket.format) << 40u);
    std::uint64_t mixed = (static_cast<std::uint64_t>(key.row_hash) << 32u) ^ bucket;
    mixed *= 0x9E3779B97F4A7C15ull;
    return static_cast<std::size_t>(mixed ^ (mixed >> 32u));
  }
};

class TextureSlicePool {
 public:
  struct ArrayTexture {
    TextureSliceBucket bucket{};
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    std::uint16_t capacity{0};

    std::uint16_t next_unused{0};
    std::vector<std::uint16_t> free_slices;
    std::uint32_t live_slices{0};
  };

  mutable std::mutex mutex;

  std::vector<ArrayTexture> arrays;
  std::unordered_map<ResidentSliceKey, std::weak_ptr<TextureSliceRef>, ResidentSliceKeyHash>
      resident;

  struct SolidVerdict {
    std::uint32_t upload_size{0};
    BlpUploadFormat format{BlpUploadFormat::kRgba8};
    SolidTextureUnit unit{};
  };
  std::unordered_map<std::uint32_t, SolidVerdict> solid_units;
  bool supported{false};
  std::uint16_t slices_per_array{TextureSliceArrays::kSlicesPerArray};

  void ReleaseSlice(const ResidentSliceKey &key,
                    const TextureSliceLocation location) noexcept {
    const std::lock_guard lock(mutex);
    if (const auto entry = resident.find(key); entry != resident.end()) {

      if (entry->second.expired()) {
        resident.erase(entry);
      }
    }
    if (location.array >= arrays.size()) {
      return;
    }
    ArrayTexture &array = arrays[location.array];
    if (array.live_slices == 0u) {
      return;
    }
    --array.live_slices;
    array.free_slices.push_back(location.slice);
  }

  const SolidTextureUnit &SolidUnitLocked(const std::uint32_t row_hash,
                                          const PreparedTextureUpload &upload);
};

struct TextureSliceRef {
  std::shared_ptr<TextureSlicePool> pool;
  ResidentSliceKey key{};
  TextureSliceLocation location{};

  ~TextureSliceRef() {
    if (pool != nullptr) {
      pool->ReleaseSlice(key, location);
    }
  }
};

}

namespace {

bool UploadSliceChain(const bgfx::TextureHandle handle, const std::uint16_t slice,
                      const TextureSliceBucket &bucket,
                      const PreparedTextureUpload &upload) {
  std::size_t offset = 0;
  for (std::uint8_t level = 0; level < bucket.mip_count; ++level) {
    const auto size = BlpUploadMipSize(bucket.width, bucket.height, level, bucket.format);
    const auto dims =
        data::BLPTextureLoader::GetMipDimensions(bucket.width, bucket.height, level);
    const auto pitch = BlpUploadMipRowPitch(dims.first, bucket.format);
    if (!size.has_value() || !pitch.has_value() ||
        *pitch > std::numeric_limits<std::uint16_t>::max() ||
        offset > upload.upload_size || *size > upload.upload_size - offset) {
      return false;
    }

    bgfx::updateTexture2D(handle, slice, level, 0u, 0u,
                          static_cast<std::uint16_t>(dims.first),
                          static_cast<std::uint16_t>(dims.second),
                          bgfx::copy(upload.rgba_bytes.data() + offset, *size),
                          static_cast<std::uint16_t>(*pitch));
    offset += *size;
  }
  return offset == upload.upload_size;
}

[[nodiscard]] bool BlockColorIsUniform(const std::uint8_t *const color) noexcept {
  const auto endpoint0 =
      static_cast<std::uint16_t>(color[0] | (static_cast<std::uint16_t>(color[1]) << 8u));
  const auto endpoint1 =
      static_cast<std::uint16_t>(color[2] | (static_cast<std::uint16_t>(color[3]) << 8u));
  const std::uint32_t selectors = static_cast<std::uint32_t>(color[4]) |
                                  (static_cast<std::uint32_t>(color[5]) << 8u) |
                                  (static_cast<std::uint32_t>(color[6]) << 16u) |
                                  (static_cast<std::uint32_t>(color[7]) << 24u);
  bool uses_endpoint0 = false;
  bool uses_endpoint1 = false;
  for (unsigned texel = 0; texel < 16u; ++texel) {
    switch ((selectors >> (2u * texel)) & 3u) {
      case 0u:
        uses_endpoint0 = true;
        break;
      case 1u:
        uses_endpoint1 = true;
        break;
      default:
        return false;
    }
  }
  return !(uses_endpoint0 && uses_endpoint1) || endpoint0 == endpoint1;
}

[[nodiscard]] bool Bc2AlphaIsUniform(const std::uint8_t *const alpha) noexcept {
  const std::uint8_t first = alpha[0];
  if ((first & 0x0Fu) != (first >> 4u)) {
    return false;
  }
  for (unsigned byte = 1; byte < 8u; ++byte) {
    if (alpha[byte] != first) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool Bc3AlphaIsUniform(const std::uint8_t *const alpha) noexcept {
  std::uint64_t selectors = 0;
  for (unsigned byte = 0; byte < 6u; ++byte) {
    selectors |= static_cast<std::uint64_t>(alpha[2u + byte]) << (8u * byte);
  }
  const std::uint64_t first = selectors & 7u;
  for (unsigned texel = 1; texel < 16u; ++texel) {
    if (((selectors >> (3u * texel)) & 7u) != first) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool UnitIsSingleColor(const BlpUploadFormat format,
                                     const std::uint8_t *const unit) noexcept {
  switch (format) {
    case BlpUploadFormat::kRgba8:

      return true;
    case BlpUploadFormat::kBc1:
      return BlockColorIsUniform(unit);
    case BlpUploadFormat::kBc2:
      return Bc2AlphaIsUniform(unit) && BlockColorIsUniform(unit + 8u);
    case BlpUploadFormat::kBc3:
      return Bc3AlphaIsUniform(unit) && BlockColorIsUniform(unit + 8u);
  }
  return false;
}

[[nodiscard]] constexpr std::uint32_t SolidUnitBytes(
    const BlpUploadFormat format) noexcept {
  return format == BlpUploadFormat::kRgba8 ? 4u : BlpUploadFormatBytesPerBlock(format);
}

bool UploadSolidChain(const bgfx::TextureHandle handle, const std::uint16_t slice,
                      const TextureSliceBucket &bucket, const SolidTextureUnit &unit) {
  if (!unit.valid() || unit.format != bucket.format) {
    return false;
  }
  for (std::uint8_t level = 0; level < bucket.mip_count; ++level) {
    const auto size = BlpUploadMipSize(bucket.width, bucket.height, level, bucket.format);
    const auto dims =
        data::BLPTextureLoader::GetMipDimensions(bucket.width, bucket.height, level);
    const auto pitch = BlpUploadMipRowPitch(dims.first, bucket.format);
    if (!size.has_value() || !pitch.has_value() ||
        *pitch > std::numeric_limits<std::uint16_t>::max() || *size % unit.size != 0u) {
      return false;
    }
    const bgfx::Memory *const memory = bgfx::alloc(*size);
    if (memory == nullptr) {
      return false;
    }
    for (std::uint32_t offset = 0; offset < *size; offset += unit.size) {
      std::memcpy(memory->data + offset, unit.bytes.data(), unit.size);
    }
    bgfx::updateTexture2D(handle, slice, level, 0u, 0u,
                          static_cast<std::uint16_t>(dims.first),
                          static_cast<std::uint16_t>(dims.second), memory,
                          static_cast<std::uint16_t>(*pitch));
  }
  return true;
}

}

namespace detail {

const SolidTextureUnit &TextureSlicePool::SolidUnitLocked(
    const std::uint32_t row_hash, const PreparedTextureUpload &upload) {
  SolidVerdict &verdict = solid_units[row_hash];
  if (verdict.upload_size == upload.upload_size && verdict.format == upload.upload_format &&
      verdict.upload_size != 0u) {
    return verdict.unit;
  }
  verdict.upload_size = upload.upload_size;
  verdict.format = upload.upload_format;
  verdict.unit = SolidTextureUnit{};
  static_cast<void>(TextureSliceArrays::TryMakeSolidUnit(upload, verdict.unit));
  return verdict.unit;
}

}

TextureSliceLocation TextureSliceLease::location() const noexcept {
  return ref_ != nullptr ? ref_->location : TextureSliceLocation{};
}

TextureSliceArrays::TextureSliceArrays()
    : pool_(std::make_shared<detail::TextureSlicePool>()) {}

TextureSliceArrays::~TextureSliceArrays() {
  Shutdown();
}

std::uint8_t TextureSliceArrays::FullMipChainLength(const std::uint16_t width,
                                                    const std::uint16_t height) noexcept {

  std::uint32_t extent = std::max<std::uint32_t>(width, height);
  std::uint8_t levels = 1u;
  while (extent > 1u) {
    extent >>= 1u;
    ++levels;
  }
  return levels;
}

bool TextureSliceArrays::TryMakeBucket(const PreparedTextureUpload &upload,
                                       TextureSliceBucket &out) noexcept {
  if (!upload.valid || upload.is_cube || upload.width == 0u || upload.height == 0u ||
      upload.width > std::numeric_limits<std::uint16_t>::max() ||
      upload.height > std::numeric_limits<std::uint16_t>::max()) {
    return false;
  }
  const auto width = static_cast<std::uint16_t>(upload.width);
  const auto height = static_cast<std::uint16_t>(upload.height);

  if (!upload.complete_mip_chain ||
      upload.mip_count != FullMipChainLength(width, height)) {
    return false;
  }

  std::uint64_t chain_bytes = 0;
  for (std::uint8_t level = 0; level < upload.mip_count; ++level) {
    const auto size = BlpUploadMipSize(width, height, level, upload.upload_format);
    const auto dims = data::BLPTextureLoader::GetMipDimensions(width, height, level);
    const auto pitch = BlpUploadMipRowPitch(dims.first, upload.upload_format);
    if (!size.has_value() || !pitch.has_value() ||
        *pitch > std::numeric_limits<std::uint16_t>::max()) {
      return false;
    }
    chain_bytes += *size;
  }
  if (chain_bytes != upload.upload_size || chain_bytes > upload.rgba_bytes.size()) {
    return false;
  }

  out = TextureSliceBucket{
      .width = width,
      .height = height,
      .mip_count = upload.mip_count,
      .format = upload.upload_format,
  };
  return true;
}

bool TextureSliceArrays::TryMakeSolidUnit(const PreparedTextureUpload &upload,
                                          SolidTextureUnit &out) noexcept {
  out = SolidTextureUnit{};
  if (!upload.valid || upload.is_cube || upload.upload_size == 0u ||
      upload.upload_size > upload.rgba_bytes.size()) {
    return false;
  }

  if (!upload.complete_mip_chain) {
    return false;
  }
  const std::uint32_t unit_size = SolidUnitBytes(upload.upload_format);
  if (unit_size > SolidTextureUnit::kMaxBytes || upload.upload_size % unit_size != 0u) {
    return false;
  }
  const std::uint8_t *const bytes = upload.rgba_bytes.data();
  if (!UnitIsSingleColor(upload.upload_format, bytes)) {
    return false;
  }
  for (std::uint32_t offset = unit_size; offset < upload.upload_size; offset += unit_size) {
    if (std::memcmp(bytes + offset, bytes, unit_size) != 0) {
      return false;
    }
  }
  std::memcpy(out.bytes.data(), bytes, unit_size);
  out.size = static_cast<std::uint8_t>(unit_size);
  out.format = upload.upload_format;
  return true;
}

bool TextureSliceArrays::IsSolidColor(const std::uint32_t row_hash,
                                      const PreparedTextureUpload &upload) const {
  const std::lock_guard lock(pool_->mutex);
  return pool_->SolidUnitLocked(row_hash, upload).valid();
}

bool TextureSliceArrays::Initialize() {
  const bgfx::Caps *const caps = bgfx::getCaps();
  const std::lock_guard lock(pool_->mutex);
  pool_->supported = caps != nullptr &&
                     (caps->supported & BGFX_CAPS_TEXTURE_2D_ARRAY) != 0u &&
                     caps->limits.maxTextureLayers > 1u;
  if (pool_->supported) {
    pool_->slices_per_array = static_cast<std::uint16_t>(
        std::min<std::uint32_t>(kSlicesPerArray, caps->limits.maxTextureLayers));
  }
  return pool_->supported;
}

bool TextureSliceArrays::supported() const noexcept {
  const std::lock_guard lock(pool_->mutex);
  return pool_->supported;
}

bgfx::TextureHandle TextureSliceArrays::ArrayTexture(
    const std::uint16_t array) const noexcept {
  const std::lock_guard lock(pool_->mutex);
  if (array >= pool_->arrays.size()) {
    return BGFX_INVALID_HANDLE;
  }
  return pool_->arrays[array].handle;
}

std::size_t TextureSliceArrays::array_count() const noexcept {
  const std::lock_guard lock(pool_->mutex);
  std::size_t live = 0;
  for (const auto &array : pool_->arrays) {
    live += bgfx::isValid(array.handle) ? 1u : 0u;
  }
  return live;
}

std::size_t TextureSliceArrays::live_slice_count() const noexcept {
  const std::lock_guard lock(pool_->mutex);
  std::size_t live = 0;
  for (const auto &array : pool_->arrays) {
    live += array.live_slices;
  }
  return live;
}

TextureSliceLease TextureSliceArrays::Acquire(const std::uint32_t row_hash,
                                              const PreparedTextureUpload *const upload,
                                              const TextureSliceBucket *const promote_to) {
  if (row_hash == 0u || upload == nullptr) {
    return {};
  }

  TextureSliceBucket bucket{};
  SolidTextureUnit solid_unit{};
  bool promoted = false;
  std::uint16_t array_index = kInvalidTextureSliceArray;
  std::uint16_t slice = 0u;
  std::uint16_t new_array_slot = kInvalidTextureSliceArray;
  std::uint16_t capacity = 0u;
  {
    const std::lock_guard lock(pool_->mutex);
    if (!pool_->supported) {
      return {};
    }

    TextureSliceBucket natural{};
    const bool has_natural = TryMakeBucket(*upload, natural);
    bool bucket_chosen = false;
    if (promote_to != nullptr && promote_to->format == upload->upload_format &&
        promote_to->width != 0u && promote_to->height != 0u &&
        promote_to->mip_count == FullMipChainLength(promote_to->width, promote_to->height)) {
      const SolidTextureUnit &unit = pool_->SolidUnitLocked(row_hash, *upload);
      if (unit.valid()) {
        bucket = *promote_to;
        bucket_chosen = true;

        promoted = !has_natural || natural != *promote_to;
        solid_unit = unit;
      }
    }
    if (!bucket_chosen) {
      if (!has_natural) {
        return {};
      }
      bucket = natural;
    }

    const detail::ResidentSliceKey key{.row_hash = row_hash, .bucket = bucket};
    if (const auto entry = pool_->resident.find(key); entry != pool_->resident.end()) {
      if (auto existing = entry->second.lock(); existing != nullptr) {
        return TextureSliceLease{std::move(existing)};
      }
      pool_->resident.erase(entry);
    }

    for (std::size_t index = 0; index < pool_->arrays.size(); ++index) {
      detail::TextureSlicePool::ArrayTexture &array = pool_->arrays[index];
      if (array.bucket != bucket || !bgfx::isValid(array.handle)) {
        continue;
      }
      if (!array.free_slices.empty()) {
        slice = array.free_slices.back();
        array.free_slices.pop_back();
      } else if (array.next_unused < array.capacity) {
        slice = array.next_unused++;
      } else {
        continue;
      }
      ++array.live_slices;
      array_index = static_cast<std::uint16_t>(index);
      break;
    }

    if (array_index == kInvalidTextureSliceArray) {
      capacity = pool_->slices_per_array;

      for (std::size_t index = 0; index < pool_->arrays.size(); ++index) {
        if (!bgfx::isValid(pool_->arrays[index].handle) &&
            pool_->arrays[index].live_slices == 0u) {
          new_array_slot = static_cast<std::uint16_t>(index);
          break;
        }
      }
      if (new_array_slot == kInvalidTextureSliceArray) {
        if (pool_->arrays.size() >= kInvalidTextureSliceArray) {
          return {};
        }
        new_array_slot = static_cast<std::uint16_t>(pool_->arrays.size());
        pool_->arrays.emplace_back();
      }
    }
  }

  if (new_array_slot != kInvalidTextureSliceArray) {

    const bgfx::TextureFormat::Enum format = ToBgfxTextureFormat(bucket.format);
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    if (bgfx::isTextureValid(0, false, capacity, format, BGFX_TEXTURE_NONE)) {
      handle = bgfx::createTexture2D(bucket.width, bucket.height, true, capacity, format,
                                     BGFX_TEXTURE_NONE);
    }
    if (!bgfx::isValid(handle)) {
      diagnostics::Log(diagnostics::LogLevel::kWarn,
                       "TextureSliceArrays: array creation failed for " +
                           std::to_string(bucket.width) + "x" +
                           std::to_string(bucket.height) + " x" +
                           std::to_string(capacity));
      return {};
    }
    const std::lock_guard lock(pool_->mutex);
    detail::TextureSlicePool::ArrayTexture &array = pool_->arrays[new_array_slot];
    array.bucket = bucket;
    array.handle = handle;
    array.capacity = capacity;
    array.next_unused = 1u;
    array.free_slices.clear();
    array.live_slices = 1u;
    array_index = new_array_slot;
    slice = 0u;
  }

  const detail::ResidentSliceKey key{.row_hash = row_hash, .bucket = bucket};
  const bgfx::TextureHandle handle = ArrayTexture(array_index);
  if (!bgfx::isValid(handle) ||
      !(promoted ? UploadSolidChain(handle, slice, bucket, solid_unit)
                 : UploadSliceChain(handle, slice, bucket, *upload))) {
    pool_->ReleaseSlice(key, TextureSliceLocation{array_index, slice});
    return {};
  }

  auto ref = std::make_shared<detail::TextureSliceRef>();
  ref->pool = pool_;
  ref->key = key;
  ref->location = TextureSliceLocation{array_index, slice};
  {
    const std::lock_guard lock(pool_->mutex);
    pool_->resident[key] = ref;
  }
  return TextureSliceLease{std::move(ref)};
}

void TextureSliceArrays::ReclaimEmptyArrays() {
  std::vector<bgfx::TextureHandle> doomed;
  {
    const std::lock_guard lock(pool_->mutex);
    for (auto &array : pool_->arrays) {
      if (array.live_slices != 0u || !bgfx::isValid(array.handle)) {
        continue;
      }
      doomed.push_back(array.handle);
      array.handle = BGFX_INVALID_HANDLE;
      array.next_unused = 0u;
      array.free_slices.clear();
    }
  }
  for (const auto handle : doomed) {
    bgfx::destroy(handle);
  }
}

void TextureSliceArrays::Shutdown() {
  std::vector<bgfx::TextureHandle> doomed;
  {
    const std::lock_guard lock(pool_->mutex);
    for (auto &array : pool_->arrays) {
      if (bgfx::isValid(array.handle)) {
        doomed.push_back(array.handle);
      }
      array.handle = BGFX_INVALID_HANDLE;
      array.next_unused = 0u;
      array.free_slices.clear();
      array.live_slices = 0u;
    }
    pool_->resident.clear();
    pool_->solid_units.clear();
    pool_->supported = false;
  }
  for (const auto handle : doomed) {
    bgfx::destroy(handle);
  }
}

}

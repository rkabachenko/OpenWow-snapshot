#include "openwow/data/derived_texture_cache.h"

#include "openwow/data/texture_cache.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <sstream>

namespace openwow::data {

namespace {

constexpr std::uint32_t kMagic = 0x5854574fu;
constexpr std::uint16_t kVersion = 1u;
constexpr std::size_t kFixedHeaderBytes = 36u;
constexpr std::uint8_t kCompleteMipChainFlag = 1u << 0u;
constexpr std::uint8_t kOpaqueFlag = 1u << 1u;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void SetFailure(std::string* const reason, std::string message) {
  if (reason != nullptr) {
    *reason = std::move(message);
  }
}

void AppendU16(std::vector<std::uint8_t>& bytes,
               const std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8u));
}

void AppendU32(std::vector<std::uint8_t>& bytes,
               const std::uint32_t value) {
  for (unsigned shift = 0u; shift < 32u; shift += 8u) {
    bytes.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

void AppendU64(std::vector<std::uint8_t>& bytes,
               const std::uint64_t value) {
  for (unsigned shift = 0u; shift < 64u; shift += 8u) {
    bytes.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

std::uint16_t ReadU16(const std::vector<std::uint8_t>& bytes,
                      const std::size_t offset) {
  return static_cast<std::uint16_t>(bytes[offset]) |
         static_cast<std::uint16_t>(bytes[offset + 1u] << 8u);
}

std::uint32_t ReadU32(const std::vector<std::uint8_t>& bytes,
                      const std::size_t offset) {
  std::uint32_t value = 0u;
  for (unsigned index = 0u; index < 4u; ++index) {
    value |= static_cast<std::uint32_t>(bytes[offset + index]) << (index * 8u);
  }
  return value;
}

std::uint64_t ReadU64(const std::vector<std::uint8_t>& bytes,
                      const std::size_t offset) {
  std::uint64_t value = 0u;
  for (unsigned index = 0u; index < 8u; ++index) {
    value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8u);
  }
  return value;
}

std::uint64_t HashNormalizedPath(const std::string_view path) noexcept {
  std::uint64_t hash = kFnvOffset;
  for (unsigned char value : path) {
    if (value == 0u) {
      break;
    }
    if (value >= 'a' && value <= 'z') {
      value = static_cast<unsigned char>(value - ('a' - 'A'));
    }
    if (value == '/') {
      value = '\\';
    }
    hash = (hash ^ value) * kFnvPrime;
  }
  return hash;
}

std::string CanonicalSourcePath(std::string_view path) {
  while (!path.empty() && (path.front() == '/' || path.front() == '\\')) {
    path.remove_prefix(1u);
  }
  return CopyTextureCacheRowPath(path);
}

std::uint32_t AstcMipSize(std::uint32_t width, std::uint32_t height,
                          const std::uint8_t level) noexcept {
  width = std::max(width >> level, 1u);
  height = std::max(height >> level, 1u);
  const std::uint64_t size =
      static_cast<std::uint64_t>((width + 3u) / 4u) *
      static_cast<std::uint64_t>((height + 3u) / 4u) * 16u;
  return size <= std::numeric_limits<std::uint32_t>::max()
             ? static_cast<std::uint32_t>(size)
             : 0u;
}

bool ValidatePayloadExtent(const DerivedTextureCacheImage& image) noexcept {
  if (image.width == 0u || image.height == 0u || image.mip_count == 0u ||
      image.mip_count > 16u || image.payload.empty()) {
    return false;
  }
  std::uint64_t expected = 0u;
  for (std::uint8_t level = 0u; level < image.mip_count; ++level) {
    const std::uint32_t size = AstcMipSize(image.width, image.height, level);
    if (size == 0u) {
      return false;
    }
    expected += size;
  }
  return expected == image.payload.size();
}

}

std::uint64_t HashDerivedTextureSource(
    const std::vector<std::uint8_t>& source_bytes) noexcept {
  std::uint64_t hash = kFnvOffset;
  for (const std::uint8_t value : source_bytes) {
    hash = (hash ^ value) * kFnvPrime;
  }
  return hash;
}

std::string MakeDerivedTextureCacheVirtualPath(
    const std::string_view source_path) {
  const std::string stored_path = CanonicalSourcePath(source_path);
  std::ostringstream name;
  name << "/OpenWoWDerived/iOS/Textures/" << std::hex << std::setfill('0')
       << std::setw(8) << HashTextureCachePath(stored_path) << '-'
       << std::setw(16) << HashNormalizedPath(stored_path) << ".owtx";
  return name.str();
}

std::string MakeDerivedTextureCachePackFilename(
    const std::size_t shard_index) {
  if (shard_index >= kDerivedTextureCachePackShardCount) {
    return {};
  }
  std::ostringstream name;
  name << "texture-cache-" << std::setfill('0') << std::setw(2)
       << shard_index << ".MPQ";
  return name.str();
}

std::vector<std::uint8_t> SerializeDerivedTextureCache(
    const std::string_view source_path,
    const std::vector<std::uint8_t>& source_bytes,
    const DerivedTextureCacheImage& image) {
  const std::string stored_path = CanonicalSourcePath(source_path);
  if (source_bytes.empty() || stored_path.empty() ||
      stored_path.size() > std::numeric_limits<std::uint16_t>::max() ||
      source_bytes.size() > std::numeric_limits<std::uint32_t>::max() ||
      image.payload.size() > std::numeric_limits<std::uint32_t>::max() ||
      image.format != DerivedTextureFormat::kAstc4x4 ||
      !ValidatePayloadExtent(image)) {
    return {};
  }

  const std::size_t header_size = kFixedHeaderBytes + stored_path.size();
  if (header_size > std::numeric_limits<std::uint16_t>::max()) {
    return {};
  }

  std::vector<std::uint8_t> bytes;
  bytes.reserve(header_size + image.payload.size());
  AppendU32(bytes, kMagic);
  AppendU16(bytes, kVersion);
  AppendU16(bytes, static_cast<std::uint16_t>(header_size));
  bytes.push_back(static_cast<std::uint8_t>(image.format));
  bytes.push_back(static_cast<std::uint8_t>(
      (image.complete_mip_chain ? kCompleteMipChainFlag : 0u) |
      (image.is_opaque ? kOpaqueFlag : 0u)));
  bytes.push_back(image.mip_count);
  bytes.push_back(0u);
  AppendU16(bytes, image.width);
  AppendU16(bytes, image.height);
  AppendU32(bytes, static_cast<std::uint32_t>(source_bytes.size()));
  AppendU32(bytes, static_cast<std::uint32_t>(image.payload.size()));
  AppendU64(bytes, HashDerivedTextureSource(source_bytes));
  AppendU16(bytes, static_cast<std::uint16_t>(stored_path.size()));
  AppendU16(bytes, 0u);
  bytes.insert(bytes.end(), stored_path.begin(), stored_path.end());
  bytes.insert(bytes.end(), image.payload.begin(), image.payload.end());
  return bytes;
}

bool ParseDerivedTextureCache(
    const std::vector<std::uint8_t>& cache_bytes,
    const std::string_view source_path,
    const std::vector<std::uint8_t>& source_bytes,
    DerivedTextureCacheImage& image,
    std::string* const failure_reason) {
  image = {};
  if (cache_bytes.size() < kFixedHeaderBytes) {
    SetFailure(failure_reason, "cache header is truncated");
    return false;
  }
  if (ReadU32(cache_bytes, 0u) != kMagic ||
      ReadU16(cache_bytes, 4u) != kVersion) {
    SetFailure(failure_reason, "cache magic or version does not match");
    return false;
  }

  const std::size_t header_size = ReadU16(cache_bytes, 6u);
  const std::size_t path_size = ReadU16(cache_bytes, 32u);
  const std::size_t payload_size = ReadU32(cache_bytes, 20u);
  if (header_size != kFixedHeaderBytes + path_size ||
      header_size > cache_bytes.size() ||
      payload_size != cache_bytes.size() - header_size) {
    SetFailure(failure_reason, "cache extents are inconsistent");
    return false;
  }
  if (source_bytes.size() != ReadU32(cache_bytes, 16u) ||
      HashDerivedTextureSource(source_bytes) != ReadU64(cache_bytes, 24u)) {
    SetFailure(failure_reason, "cache source fingerprint is stale");
    return false;
  }

  const std::string cached_path(
      cache_bytes.begin() + static_cast<std::ptrdiff_t>(kFixedHeaderBytes),
      cache_bytes.begin() + static_cast<std::ptrdiff_t>(header_size));
  if (!TextureCachePathsAlias(cached_path,
                              CanonicalSourcePath(source_path))) {
    SetFailure(failure_reason, "cache source path does not match");
    return false;
  }

  image.format = static_cast<DerivedTextureFormat>(cache_bytes[8u]);
  image.complete_mip_chain =
      (cache_bytes[9u] & kCompleteMipChainFlag) != 0u;
  image.is_opaque = (cache_bytes[9u] & kOpaqueFlag) != 0u;
  image.mip_count = cache_bytes[10u];
  image.width = ReadU16(cache_bytes, 12u);
  image.height = ReadU16(cache_bytes, 14u);
  image.payload.assign(
      cache_bytes.begin() + static_cast<std::ptrdiff_t>(header_size),
      cache_bytes.end());
  if (image.format != DerivedTextureFormat::kAstc4x4 ||
      !ValidatePayloadExtent(image)) {
    image = {};
    SetFailure(failure_reason, "cache texture payload is invalid");
    return false;
  }
  return true;
}

}

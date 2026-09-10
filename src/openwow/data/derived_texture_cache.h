#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::data {

inline constexpr std::size_t kDerivedTextureCachePackShardCount = 16u;

enum class DerivedTextureFormat : std::uint8_t {
  kAstc4x4 = 1u,
};

struct DerivedTextureCacheImage {
  DerivedTextureFormat format{DerivedTextureFormat::kAstc4x4};
  std::uint16_t width{0u};
  std::uint16_t height{0u};
  std::uint8_t mip_count{0u};
  bool complete_mip_chain{false};
  bool is_opaque{false};
  std::vector<std::uint8_t> payload;
};

[[nodiscard]] std::uint64_t HashDerivedTextureSource(
    const std::vector<std::uint8_t>& source_bytes) noexcept;

[[nodiscard]] std::string MakeDerivedTextureCacheVirtualPath(
    std::string_view source_path);

[[nodiscard]] std::string MakeDerivedTextureCachePackFilename(
    std::size_t shard_index);

[[nodiscard]] std::vector<std::uint8_t> SerializeDerivedTextureCache(
    std::string_view source_path,
    const std::vector<std::uint8_t>& source_bytes,
    const DerivedTextureCacheImage& image);

[[nodiscard]] bool ParseDerivedTextureCache(
    const std::vector<std::uint8_t>& cache_bytes,
    std::string_view source_path,
    const std::vector<std::uint8_t>& source_bytes,
    DerivedTextureCacheImage& image,
    std::string* failure_reason = nullptr);

}

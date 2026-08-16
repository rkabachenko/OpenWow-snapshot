#pragma once

#include "openwow/render/platform/renderer_backend_selection.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace openwow::render {

enum class TextureCacheBackendClass : std::uint8_t {
  kOpenGl,
  kModern,

  kDirect3D9Ex,
};

struct TextureCacheBudgetContext {
  std::uint64_t physical_memory_bytes{0};
  TextureCacheBackendClass backend{TextureCacheBackendClass::kModern};
};

struct TextureCacheSizeValidationResult {
  bool accepted{false};
  std::int32_t requested_megabytes{0};
  std::int32_t requested_bytes{0};
  std::uint32_t max_bytes{0};
  std::uint32_t effective_bytes{0};
  std::string console_message;
};

inline constexpr std::size_t kTextureCacheEvictionsPerSweep = 16u;
inline constexpr std::size_t kTextureCacheUrgentEvictionsPerSweep = 32u;

inline constexpr std::size_t kTextureCacheMaxEntriesVisitedPerSweep = 64u;

[[nodiscard]] constexpr std::size_t TextureCacheEvictionQuota(
    const std::uint64_t memory_usage_bytes,
    const std::uint64_t memory_budget_bytes) noexcept {
  return (memory_budget_bytes != 0u &&
          memory_usage_bytes - memory_budget_bytes > memory_budget_bytes)
             ? kTextureCacheUrgentEvictionsPerSweep
             : kTextureCacheEvictionsPerSweep;
}

std::uint32_t GetTextureCacheBudgetLimitBytes(
    const TextureCacheBudgetContext& context);

std::uint32_t ResolveTextureCacheBudgetBytes(
    std::int32_t requested_bytes,
    const TextureCacheBudgetContext& context);

std::int32_t TextureCacheMegabytesToBytes(std::uint32_t requested_megabytes_raw);

TextureCacheSizeValidationResult ValidateTextureCacheSizeChange(
    std::uint32_t requested_megabytes_raw,
    const TextureCacheBudgetContext& context);

TextureCacheBackendClass TextureCacheBackendForRenderer(
    api::RendererBackend backend);

}

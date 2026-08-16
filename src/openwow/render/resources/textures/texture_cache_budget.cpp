#include "openwow/render/resources/textures/texture_cache_budget.h"

#include <algorithm>

namespace openwow::render {
namespace {

std::int32_t ArithmeticShiftRight20(const std::int32_t value) {
  const std::uint32_t raw = static_cast<std::uint32_t>(value);
  return (raw & 0x80000000u) == 0u
             ? static_cast<std::int32_t>(raw >> 20u)
             : static_cast<std::int32_t>((raw >> 20u) | 0xfff00000u);
}

constexpr std::uint32_t kTextureCacheBudgetCeilingBytes = 0x04000000u;
constexpr std::uint32_t kTextureCacheBudgetDisabledBytes = 0u;

}

std::uint32_t GetTextureCacheBudgetLimitBytes(
    const TextureCacheBudgetContext& context) {
  return context.backend == TextureCacheBackendClass::kDirect3D9Ex
             ? kTextureCacheBudgetDisabledBytes
             : kTextureCacheBudgetCeilingBytes;
}

std::uint32_t ResolveTextureCacheBudgetBytes(
    const std::int32_t requested_bytes,
    const TextureCacheBudgetContext& context) {
  if (requested_bytes < 0) return 0;
  return std::min(static_cast<std::uint32_t>(requested_bytes),
                  GetTextureCacheBudgetLimitBytes(context));
}

std::int32_t TextureCacheMegabytesToBytes(
    const std::uint32_t requested_megabytes) {
  return static_cast<std::int32_t>(requested_megabytes << 20u);
}

TextureCacheSizeValidationResult ValidateTextureCacheSizeChange(
    const std::uint32_t requested_megabytes,
    const TextureCacheBudgetContext& context) {
  TextureCacheSizeValidationResult result;
  result.requested_bytes =
      TextureCacheMegabytesToBytes(requested_megabytes);
  result.requested_megabytes =
      ArithmeticShiftRight20(result.requested_bytes);
  result.max_bytes = GetTextureCacheBudgetLimitBytes(context);
  if (result.requested_bytes < 0 ||
      static_cast<std::uint32_t>(result.requested_bytes) >
          result.max_bytes) {
    result.console_message =
        "Texture cache size (" +
        std::to_string(result.requested_megabytes) +
        " meg) greater than maximum allowed for your system (" +
        std::to_string(result.max_bytes >> 20u) + " meg).";
    return result;
  }
  result.accepted = true;
  result.effective_bytes =
      ResolveTextureCacheBudgetBytes(result.requested_bytes, context);
  result.console_message =
      "Texture cache size set to " +
      std::to_string(result.requested_megabytes) + " meg.";
  return result;
}

TextureCacheBackendClass TextureCacheBackendForRenderer(
    const api::RendererBackend backend) {
  return backend == api::RendererBackend::OpenGL
             ? TextureCacheBackendClass::kOpenGl
             : TextureCacheBackendClass::kModern;
}

}

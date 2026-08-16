#pragma once

#include <cstdint>
#include <memory>

namespace openwow::render {

struct TextureAllocation;

class TextureLease {
 public:
  TextureLease() = default;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] explicit operator bool() const noexcept {
    return allocation_ != nullptr;
  }

 private:
  friend class BgfxTextureLeaseAccess;
  friend class TextureManager;
  explicit TextureLease(std::shared_ptr<TextureAllocation> allocation)
      : allocation_(std::move(allocation)) {}

  std::shared_ptr<TextureAllocation> allocation_;
};

enum class TextureLoadFailurePolicy : std::uint8_t {
  kStrict,
  kCheckerPlaceholder,
};

enum class TextureLoadPriority : std::uint8_t {
  kPrefetch,
  kDemand,
};

}

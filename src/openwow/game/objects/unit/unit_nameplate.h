#pragma once

#include <cstdint>

namespace openwow::game {

class CGUnit_C;
class ObjectManager;

class UnitNameplateComponent final {
public:
  static constexpr std::uint8_t kHighlightTypeTarget = 0;
  static constexpr std::uint8_t kHighlightTypeMouseover = 1;
  static constexpr std::uint8_t kHighlightTypeAlphaPreserving = 2;
  static constexpr std::uint32_t kHighlightBitBase = 24u;
  static constexpr std::uint32_t kHighlightMask = 0x07000000u;
  static constexpr std::uint32_t kAlphaPreservingOnlyMask = 0x04000000u;

  UnitNameplateComponent() = default;
  UnitNameplateComponent(const UnitNameplateComponent &) = delete;
  UnitNameplateComponent &operator=(const UnitNameplateComponent &) = delete;
  UnitNameplateComponent(UnitNameplateComponent &&) noexcept = default;
  UnitNameplateComponent &operator=(UnitNameplateComponent &&) noexcept = default;
  ~UnitNameplateComponent() = default;

  [[nodiscard]] bool HasAny() const noexcept {
    return (flags_ & kHighlightMask) != 0u;
  }
  [[nodiscard]] static constexpr bool IsValidHighlightType(
      const std::uint8_t type) noexcept {
    return type <= kHighlightTypeAlphaPreserving;
  }
  [[nodiscard]] bool Has(std::uint8_t type) const noexcept {
    if (!IsValidHighlightType(type)) {
      return false;
    }
    return (flags_ & (1u << (type + kHighlightBitBase))) != 0u;
  }

  [[nodiscard]] bool ShouldShow(const CGUnit_C &unit,
                                 const CGUnit_C &viewer,
                                 const ObjectManager &objects,
                                 float distance_squared,
                                 bool range_exempt_map = false) const;
  [[nodiscard]] bool PassesHardEligibility(const CGUnit_C &unit,
                                            const CGUnit_C &viewer) const;

  [[nodiscard]] static bool IsFriendlyForNameplate(const CGUnit_C &unit,
                                                   const CGUnit_C &viewer);

  [[nodiscard]] bool PassesRange(float distance_squared,
                                 bool range_exempt_map) const;
  [[nodiscard]] bool PassesCvarVisibility(const CGUnit_C &unit,
                                          const CGUnit_C &viewer,
                                          const ObjectManager &objects) const;

  void Set(CGUnit_C &unit, std::uint8_t highlight_type);

  void Clear(CGUnit_C &unit, std::uint8_t highlight_type);

  [[nodiscard]] static std::uint64_t HighlightedGuid() noexcept {
    return s_highlighted_guid_;
  }
  static void ClearHighlightedGuidIfMatch(std::uint64_t guid) noexcept {
    if (s_highlighted_guid_ != 0 && s_highlighted_guid_ == guid) {
      s_highlighted_guid_ = 0;
    }
  }

private:
  std::uint32_t flags_{0};
  static inline std::uint64_t s_highlighted_guid_{0};
};

}

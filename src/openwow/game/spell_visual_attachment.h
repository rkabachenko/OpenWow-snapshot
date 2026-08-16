#pragma once

#include <cstdint>

namespace openwow::game {

[[nodiscard]] constexpr std::uint32_t ResolveSpellAttachmentLookupIndex(
    const std::uint32_t attachment_index, const bool use_raw_index) noexcept {
  if (use_raw_index) {
    return attachment_index;
  }

  switch (attachment_index) {
    case 0: return 0u;
    case 1: return 1u;
    case 2: return 2u;
    case 3: return 3u;
    case 4: return 4u;
    case 5: return 5u;
    case 6: return 6u;
    case 7: return 7u;
    case 8: return 8u;
    case 9: return 9u;
    case 10: return 10u;
    case 11: return 11u;
    case 12: return 12u;
    case 17: return 17u;
    case 19: return 19u;
    case 20: return 20u;
    case 21: return 21u;
    case 22: return 22u;
    case 23: return 23u;
    case 24: return 24u;
    case 25: return 25u;
    case 34: return 34u;
    default: return 19u;
  }
}

}

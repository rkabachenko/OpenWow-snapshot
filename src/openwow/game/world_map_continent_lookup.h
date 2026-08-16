#pragma once

#include <cstddef>
#include <cstdint>

namespace openwow::game {

template <typename ContinentRange>
[[nodiscard]] std::int32_t FindLastWorldMapContinentIndexByMapId(const ContinentRange &continents,
                                                                 std::uint32_t map_id) {
  for (std::size_t index = continents.size(); index > 0; --index) {
    if (continents[index - 1].map_id == map_id) {
      return static_cast<std::int32_t>(index - 1);
    }
  }

  return -1;
}

}

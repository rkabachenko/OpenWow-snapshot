#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "openwow/data/formats/blp/texture_path.h"
#include "openwow/vfs/sfile_core.h"

namespace openwow::ui::glue {

struct GlueStreamingCounters {
  std::uint64_t current{0};
  std::uint64_t total{0};

  [[nodiscard]] bool Complete() const { return current == total; }

  [[nodiscard]] float Ratio() const {
    if (total == 0) return 0.0f;
    return static_cast<float>(static_cast<double>(current) / static_cast<double>(total));
  }
};

inline void AccumulateGlueStreamingReadinessProgress(
    const bool ready, GlueStreamingCounters& counters) {
  ++counters.total;
  if (ready) {
    ++counters.current;
  }
}

inline void AccumulateGlueStreamingPathProgress(std::string_view path,
                                                const bool ready,
                                                GlueStreamingCounters& counters) {
  std::uint64_t current = counters.current;
  std::uint64_t total = counters.total;

  if (!path.empty()) {
    const std::string owned_path(path);
    (void)openwow::vfs::AccumulateDataPreloadPathProgress(owned_path.c_str(), &current, &total);
  }

  counters.current = current;
  counters.total = total;
  AccumulateGlueStreamingReadinessProgress(ready, counters);
}

inline void AccumulateGlueStreamingTextureProgress(std::string_view texture_base_path,
                                                   const bool ready,
                                                   GlueStreamingCounters& counters) {
  AccumulateGlueStreamingPathProgress(
      openwow::data::blp::NormalizeTexturePath(std::string(texture_base_path)),
      ready,
      counters);
}

}

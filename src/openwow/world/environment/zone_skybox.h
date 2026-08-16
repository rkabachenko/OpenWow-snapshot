#pragma once

#include <cstdint>
#include <string>

namespace openwow::world {

struct ZoneSkyboxEntry {
  std::uint32_t light_skybox_id{0};
  std::string model_path;
  std::uint32_t flags{0};

  [[nodiscard]] bool operator==(const ZoneSkyboxEntry& other) const {
    return light_skybox_id == other.light_skybox_id &&
           model_path == other.model_path && flags == other.flags;
  }
};

}

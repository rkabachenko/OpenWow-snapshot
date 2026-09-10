#pragma once
#include "openwow/data/formats/dbc/dbc_loader.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace openwow::render {

struct CreatureDisplayParticleColors {
  std::array<std::uint32_t, 3> start{};
  std::array<std::uint32_t, 3> mid{};
  std::array<std::uint32_t, 3> end{};
};

struct CreatureDisplayVisual {
  bool resolved{false};
  std::string model_path;
  float model_scale{1.0f};
  float model_opacity{1.0f};
  float mount_height{0.0f};
  std::array<std::string, 3> texture_paths{};
  std::optional<CreatureDisplayParticleColors> particle_colors;
  std::uint32_t geoset_data{0u};
};

class DisplayInfoResolver {
 public:
  DisplayInfoResolver() = default;

  void BindDbc(const openwow::data::dbc::DbcLoader* dbc);

  [[nodiscard]] bool IsReady() const { return dbc_ != nullptr; }

  [[nodiscard]] CreatureDisplayVisual ResolveCreatureDisplay(
      std::uint32_t display_id,
      std::string_view effective_model_path = {}) const;

  [[nodiscard]] std::string ResolveCreatureModel(std::uint32_t display_id) const;

  [[nodiscard]] std::string ResolvePlayerModel(std::uint8_t race,
                                               std::uint8_t gender) const;

  [[nodiscard]] std::string ResolveGameObjectModel(std::uint32_t display_id) const;

  [[nodiscard]] std::string ResolveItemModelLeft(std::uint32_t display_id) const;

  [[nodiscard]] std::string ResolveItemModelRight(std::uint32_t display_id) const;

 private:
  const openwow::data::dbc::DbcLoader* dbc_{nullptr};

};

}

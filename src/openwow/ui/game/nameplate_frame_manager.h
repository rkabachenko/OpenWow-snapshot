#pragma once

#include "openwow/ui/game/nameplate_system.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct lua_State;

namespace openwow::ui::game::runtime {
class FrameMaterializer;
class FrameStore;
}

namespace openwow::ui::game {

class NameplateFrameManager final {
 public:
  NameplateFrameManager(runtime::FrameMaterializer& materializer,
                        runtime::FrameStore& frames);

  NameplateFrameManager(const NameplateFrameManager&) = delete;
  NameplateFrameManager& operator=(const NameplateFrameManager&) = delete;

  void BindLuaState(lua_State* state);

  void Update();

  [[nodiscard]] std::size_t pool_size() const noexcept {
    return plates_.size();
  }
  [[nodiscard]] std::size_t active_plate_count() const noexcept {
    return active_plates_;
  }

 private:

  struct PlateState {
    int lua_ref{-1};
    std::string key;
    bool shown{false};
    bool geometry_valid{false};
    float applied_units_per_stored{0.0f};
    std::uint64_t guid{0};
    std::string name;
    std::uint32_t name_color_argb{0};
    std::uint32_t level_color_argb{0};
    std::uint32_t bar_color_argb{0};
    std::uint32_t threat_color_argb{0};
    std::string cast_icon_texture;
    std::uint32_t cast_color_argb{0};
    int level_text{-1};
    float health_pct{-1.0f};
    float cast_pct{-2.0f};
    float alpha{-1.0f};
    int frame_level{-1};
    int raid_target_icon_index{-1};
    float raid_icon_alpha{-1.0f};
    bool show_level{false};
    bool show_skull{false};
    bool show_elite{false};
    bool show_threat{false};
    bool show_glow{false};
    bool show_cast{false};
    bool show_shield{false};
  };

  void ReleasePlates();
  [[nodiscard]] bool EnsurePlate(std::size_t index);
  [[nodiscard]] bool CreatePlate(PlateState& plate);
  void ApplyGeometry(PlateState& plate, int plate_index,
                     const NameplateScreenLayout& layout);
  void ApplyPlacement(PlateState& plate, int plate_index,
                      const NameplateScreenPlacement& placement,
                      const NameplateScreenLayout& layout);
  void HidePlate(PlateState& plate);

  runtime::FrameMaterializer& materializer_;
  runtime::FrameStore& frames_;
  lua_State* lua_{nullptr};
  std::vector<PlateState> plates_;
  std::size_t active_plates_{0};
  bool world_frame_missing_logged_{false};
};

}

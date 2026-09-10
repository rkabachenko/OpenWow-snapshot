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
class FrameTraversalIndex;
class RetainedLayout;
}

namespace openwow::ui::game {

class NameplateFrameManager final {
 public:
  NameplateFrameManager(runtime::FrameMaterializer& materializer,
                        runtime::FrameStore& frames,
                        runtime::RetainedLayout& layout,
                        runtime::FrameTraversalIndex& traversal);

  NameplateFrameManager(const NameplateFrameManager&) = delete;
  NameplateFrameManager& operator=(const NameplateFrameManager&) = delete;

  void BindLuaState(lua_State* state);

  void Update();

  // Read the applied widgets and committed geometry without forcing layout.
  [[nodiscard]] std::uint64_t HitTestCommitted(
      float x, float y, std::uint64_t excluded_guid) const;

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
    float depth{-1.0f};
    std::size_t applied_order{0};
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
                      const NameplateScreenLayout& layout,
                      float world_frame_depth);
  void HidePlate(PlateState& plate);

  runtime::FrameMaterializer& materializer_;
  runtime::FrameStore& frames_;
  runtime::RetainedLayout& layout_;
  runtime::FrameTraversalIndex& traversal_;
  lua_State* lua_{nullptr};
  std::vector<PlateState> plates_;
  std::size_t active_plates_{0};
  bool world_frame_missing_logged_{false};
  bool lua_stack_failure_logged_{false};
  unsigned plate_failure_reports_remaining_{8u};
};

}

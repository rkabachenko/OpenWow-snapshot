#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace openwow::ui {

struct NameplateInfo {
  std::uint64_t guid{0};
  std::string name;
  float world_x{0.0f};
  float world_y{0.0f};
  float world_z{0.0f};
  std::array<float, 6> world_bounds{};
  bool has_world_bounds{false};
  std::uint32_t name_color_argb{0xFFFFFFFFu};
  std::uint8_t frame_alpha{0xFFu};
  std::uint8_t raid_target_icon_index{0xFFu};
  std::uint32_t level{0};
  std::uint32_t level_color_argb{0xFFFFFF00u};
  bool show_level{true};
  bool show_skull{false};
  bool show_elite{false};
  float health_pct{1.0f};
  std::uint8_t reaction{0};
  std::uint32_t health_bar_color_argb{0xFFFFFF00u};
  bool is_player{false};
  bool is_npc{false};
  bool is_target{false};
  bool is_mouseover{false};
  bool is_dead{false};
  float cast_pct{-1.0f};
  float cast_alpha{0.0f};
  std::uint32_t cast_color_argb{0xFFFFB300u};
  std::string cast_name;
  std::string cast_icon_texture;
  bool cast_not_interruptible{false};
  std::uint8_t threat_status{1u};
  std::uint32_t threat_color_argb{0xFFB0B0B0u};
  bool show_threat{false};
};

class NameplateSystem {
 public:
  using Snapshot = std::vector<NameplateInfo>;
  using SnapshotLease = std::shared_ptr<const Snapshot>;

  NameplateSystem();

  void ReplaceSnapshot(Snapshot nameplates);
  void RemoveNameplate(std::uint64_t guid);
  void ClearAll();

  [[nodiscard]] SnapshotLease AcquireSnapshot() const noexcept;
  [[nodiscard]] std::size_t GetNumNameplates() const noexcept;

 private:
  void PublishLocked(Snapshot next);

  std::shared_ptr<const Snapshot> snapshot_;
  mutable std::mutex writer_mutex_;
};

struct NameplatePixelRect {
  float x{0.0f};
  float y{0.0f};
  float width{0.0f};
  float height{0.0f};
};

struct NameplatePixelGeometry {
  float frame_width{0.0f};
  float frame_height{0.0f};
  NameplatePixelRect threat_flash;
  NameplatePixelRect health_bar;
  NameplatePixelRect cast_border;
  NameplatePixelRect cast_bar;
  NameplatePixelRect cast_icon;
  NameplatePixelRect elite_icon;
  float name_font_height{0.0f};
  float level_font_height{0.0f};
  float cast_bar_width{0.0f};
  float cast_bar_height{0.0f};
  float cast_icon_size{0.0f};
  float skull_size{0.0f};
  float elite_width{0.0f};
  float elite_height{0.0f};
  float raid_target_icon_size{0.0f};
  float pixels_per_stored{0.0f};
};

struct NameplateScreenPlacement {
  NameplateInfo info;
  float screen_x{0.0f};
  float screen_y{0.0f};
  float camera_distance{0.0f};

  std::uint8_t frame_level{10u};
};

struct NameplateScreenLayout {
  std::vector<NameplateScreenPlacement> plates;
  NameplatePixelGeometry geometry;
  float framebuffer_width{0.0f};
  float framebuffer_height{0.0f};

  float ui_pixel_scale{1.0f};
  std::uint64_t generation{0};
};

class NameplateFrameChannel {
 public:
  struct Evidence {
    std::uint64_t generation{0};
    std::size_t plates{0};
    std::size_t named_plates{0};
    bool widgets_ready{false};
  };

  using LayoutLease = std::shared_ptr<const NameplateScreenLayout>;

  static NameplateFrameChannel& Get();

  void PublishLayout(NameplateScreenLayout layout);
  [[nodiscard]] LayoutLease AcquireLayout() const noexcept;

  void RecordWidgetUpdate(Evidence evidence) noexcept;
  [[nodiscard]] Evidence widget_evidence() const noexcept;

  void Reset();

 private:
  NameplateFrameChannel();

  std::shared_ptr<const NameplateScreenLayout> layout_;
  mutable std::mutex writer_mutex_;
  Evidence evidence_{};
  mutable std::mutex evidence_mutex_;
};

}

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "openwow/render/resources/textures/texture_lease.h"
#include "openwow/ui/game/minimap_arrows.h"
#include "openwow/ui/game/minimap_surface_submission.h"

namespace openwow::render {
class TextureManager;
}
namespace openwow::game {
class MinimapSystem;
}
namespace openwow::ui {
class MinimapSystem;
}

namespace openwow::ui {

enum class MinimapIconType : uint8_t {
  kUnit = 0,
  kQuest = 1,
  kResource = 2,
  kParty = 3,
  kRaid = 4,
  kPing = 5,
};

enum class MinimapIconTextureKind : uint8_t {
  kSolidColor = 0,
  kPoiAtlas = 1,
};

struct MinimapIcon {
  float world_x{0.0f};
  float world_y{0.0f};
  uint32_t color{0xFFFFFFFFu};
  MinimapIconType type{MinimapIconType::kUnit};

  float quad_span_pixels{0.0f};
  MinimapIconTextureKind texture_kind{MinimapIconTextureKind::kSolidColor};
  uint32_t atlas_icon_index{0};
  std::string texture_path;
};

struct MinimapBackgroundVertex {
  float x{0.0f};
  float y{0.0f};
  float u{0.0f};
  float v{0.0f};
};

struct MinimapBackgroundTile {
  std::string texture_path;

  openwow::render::TextureLease texture_lease;
  std::array<MinimapBackgroundVertex, 4> vertices{};
  std::uint32_t color{0xFFFFFFFFu};
};

class Minimap {
 public:
  Minimap(openwow::render::TextureManager& texture_manager,
          openwow::ui::MinimapSystem& minimap_state,
          openwow::game::MinimapSystem& minimap_content);
  ~Minimap();

  Minimap(const Minimap&) = delete;
  Minimap& operator=(const Minimap&) = delete;

  bool Initialize(float screen_x, float screen_y, float radius);
  void ReleaseRendererDeviceResources();
  bool RestoreRendererDeviceResources();
  void Update(float player_x, float player_y, float player_z, float facing);

  void SetRotating(bool rotating) { rotating_ = rotating; }
  [[nodiscard]] bool IsRotating() const { return rotating_; }

  void SetZoom(float zoom);
  void SetVisibleRadius(float visible_radius);
  void SetScreenPosition(float screen_x, float screen_y, float radius);

  void ClearIcons();
  void AddIcon(const MinimapIcon& icon);

  void ClearRotatingArrows();
  void AddRotatingArrow(const MinimapRotatingArrow& arrow);
  void SetRotatingArrowTexturePath(MinimapRotatingArrowKind kind,
                                   const std::string& texture_path);
  void SetIndoorMinimapActive(bool active);

  void SetPlayerArrowTexturePath(const std::string& texture_path);
  void SetPlayerArrowSize(float width, float height);

  void SetUiUnitScale(float scale);

  void SetTexture(const uint8_t* rgba_data, uint32_t width, uint32_t height);
  void ClearBackgroundTiles();
  void AddBackgroundTile(MinimapBackgroundTile tile);
  void SetMaskTexturePath(std::string texture_path);
  [[nodiscard]] const std::string& GetMaskTexturePath() const;

  void InvalidateTextureLeases();
  [[nodiscard]] bool HasTexture() const;

  void Render(uint8_t view_id, float screen_width, float screen_height);

  void RenderToTexture(uint8_t view_id);
  [[nodiscard]] MinimapSurfaceSubmitter SurfaceSubmitter() const;

  bool HandleClick(float screen_x, float screen_y,
                   float& out_world_x, float& out_world_y);
  void ProjectWorldToScreen(float world_x, float world_y, float& screen_x,
                            float& screen_y) const;
  void ProjectWorldToScreenUnrotated(float world_x, float world_y,
                                     float& screen_x, float& screen_y) const;

  void Shutdown();

  [[nodiscard]] float zoom() const { return zoom_; }
  [[nodiscard]] float radius() const { return radius_; }

  [[nodiscard]] float ui_unit_scale() const { return ui_unit_scale_; }
  [[nodiscard]] float player_x() const { return player_x_; }
  [[nodiscard]] float player_y() const { return player_y_; }
  [[nodiscard]] std::size_t background_tile_count() const {
    return background_tiles_.size();
  }

  [[nodiscard]] std::size_t last_render_terrain_submission_count() const {
    return last_render_terrain_submission_count_;
  }

 private:
  struct RenderState;

  openwow::render::TextureManager& texture_manager_;
  openwow::ui::MinimapSystem& minimap_state_;
  openwow::game::MinimapSystem& minimap_content_;
  void RenderMaskPass(uint8_t view_id, std::uint16_t mask_texture_index) const;
  void ClearMaskAlpha(uint8_t view_id) const;
  void RenderBackground(uint8_t view_id, bool backbuffer_masked);
  void RenderRotatingArrows(uint8_t view_id);
  void RenderPlayerArrow(uint8_t view_id);
  void RenderIcons(uint8_t view_id, float screen_width, float screen_height);
  void RenderSubmittedBatches(uint8_t view_id);
  bool SubmitQuad(uint8_t view_id, std::uint16_t texture_index,
                  const std::array<MinimapBackgroundVertex, 4>& vertices,
                  std::uint32_t color, std::uint64_t state) const;
  bool SubmitQuads(
      uint8_t view_id, std::uint16_t texture_index,
      const std::vector<std::array<MinimapBackgroundVertex, 4>>& quads,
      const std::vector<std::uint32_t>& colors, std::uint64_t state) const;
  [[nodiscard]] std::uint16_t AcquireRetainedTexture(
      std::string_view texture_path);
  void RequestRetainedTexture(
      std::string_view texture_path,
      openwow::render::TextureLoadFailurePolicy failure_policy,
      openwow::render::TextureLoadPriority priority);
  [[nodiscard]] std::array<MinimapBackgroundVertex, 4>
  BuildBackgroundQuad() const;
  [[nodiscard]] const std::string& GetRotatingArrowTexturePath(
      MinimapRotatingArrowKind kind) const;

  float pos_x_{0.0f};
  float pos_y_{0.0f};
  float radius_{70.0f};

  float player_x_{0.0f};
  float player_y_{0.0f};
  float player_z_{0.0f};
  float facing_{0.0f};
  float zoom_{1.0f};
  float visible_radius_{83.333328f};
  bool rotating_{true};

  std::vector<MinimapIcon> icons_;
  std::vector<MinimapBackgroundTile> background_tiles_;
  std::vector<MinimapRotatingArrow> rotating_arrows_;
  std::size_t last_render_terrain_submission_count_{0u};
  bool indoor_minimap_active_{false};
  std::string mask_texture_path_{"Textures\\MinimapMask"};
  std::string static_poi_arrow_texture_{
      "Interface\\Minimap\\Rotating-MinimapArrow"};
  std::string guide_poi_arrow_texture_{
      "Interface\\Minimap\\Rotating-MinimapGuideArrow"};
  std::string corpse_poi_arrow_texture_{
      "Interface\\Minimap\\Rotating-MinimapCorpseArrow"};
  std::string group_member_arrow_texture_{
      "Interface\\Minimap\\Rotating-MinimapGroupArrow"};
  std::string player_arrow_texture_{"Interface\\Minimap\\MinimapArrow.tga"};

  float player_arrow_width_{0.0f};
  float player_arrow_height_{0.0f};

  float player_arrow_natural_width_{0.0f};
  float player_arrow_natural_height_{0.0f};
  float ui_unit_scale_{1.0f};

  std::unique_ptr<RenderState> render_;
  bool initialized_{false};
  static constexpr std::uint16_t kRenderTargetExtent = 256u;
};

}

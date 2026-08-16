
#pragma once

#include <cstdint>
#include <mutex>

namespace openwow::world {
class WorldCamera;
}

namespace openwow::game {

class WorldSession;

struct BarberAppearance {
  std::uint8_t hair_style = 0;
  std::uint8_t hair_color = 0;
  std::uint8_t facial_hair = 0;
  std::uint8_t skin_color = 0;
  std::uint8_t face = 0;
};

class BarberShop {
 public:
  static BarberShop& Get();
  void BindWorldCamera(openwow::world::WorldCamera* world_camera) noexcept {
    world_camera_ = world_camera;
  }

  BarberShop(const BarberShop&) = delete;
  BarberShop& operator=(const BarberShop&) = delete;

  void Open(const WorldSession& session);
  void Close();
  [[nodiscard]] bool IsOpen() const;

  void SetPreviewHairStyle(std::uint8_t style);
  void SetPreviewHairColor(std::uint8_t color);
  void SetPreviewFacialHair(std::uint8_t facial);
  void SetPreviewSkinColor(std::uint8_t skin);
  void SetPreviewFace(std::uint8_t face);
  void SetPreviewAppearance(const BarberAppearance& appearance);

  void ResetPreviewStylesFromActivePlayerAppearance(const BarberAppearance& appearance);

  [[nodiscard]] std::uint8_t GetPreviewHairStyle() const;
  [[nodiscard]] std::uint8_t GetPreviewHairColor() const;
  [[nodiscard]] std::uint8_t GetPreviewFacialHair() const;
  [[nodiscard]] std::uint8_t GetPreviewSkinColor() const;
  [[nodiscard]] std::uint8_t GetPreviewFace() const;
  [[nodiscard]] BarberAppearance GetPreviewAppearance() const;
  [[nodiscard]] std::uint8_t GetSelectedHairStyle() const;
  [[nodiscard]] std::uint8_t GetSelectedFacialHair() const;
  [[nodiscard]] std::uint8_t GetSelectedSkinColor() const;

  [[nodiscard]] std::uint32_t GetCost() const;
  void SetCost(std::uint32_t copper);

  [[nodiscard]] std::uint8_t GetNumHairStyles() const;
  [[nodiscard]] std::uint8_t GetNumHairColors() const;
  [[nodiscard]] std::uint8_t GetNumFacialHairStyles() const;

  void SetNumOptions(std::uint8_t hair_styles, std::uint8_t hair_colors,
                      std::uint8_t facial_hair);

  void ApplyChanges();
  [[nodiscard]] bool Cancel(WorldSession& session);
  void MarkDeferredActivePlayerAppearanceRefresh();
  void RevertChanges();
  void SetPreviewModelBoundsRadius(float radius);
  [[nodiscard]] float GetPreviewModelBoundsRadius() const;
  [[nodiscard]] float GetWorldCameraMinZoomDistance() const;
  [[nodiscard]] float GetWorldCameraZoomStepScale() const;

  void Reset();

 private:
  BarberShop() = default;

  [[nodiscard]] BarberAppearance CurrentAppearanceUnlocked() const;
  void SyncSelectedStyleStateUnlocked(const BarberAppearance& appearance);
  void SyncAppearanceStateUnlocked(const BarberAppearance& appearance);

  bool open_ = false;
  std::uint8_t preview_hair_style_ = 0;
  std::uint8_t preview_hair_color_ = 0;
  std::uint8_t preview_facial_hair_ = 0;
  std::uint8_t preview_skin_color_ = 0;
  std::uint8_t preview_face_ = 0;
  std::uint8_t selected_hair_style_ = 0;
  std::uint8_t selected_facial_hair_ = 0;
  std::uint8_t selected_skin_color_ = 0;
  std::uint8_t num_hair_styles_ = 1;
  std::uint8_t num_hair_colors_ = 1;
  std::uint8_t num_facial_hair_ = 1;
  std::uint32_t cost_ = 0;
  float preview_model_bounds_radius_ = 1.0f;
  float preview_camera_distance_scale_ = 2.0f;
  int previous_camera_view_ = 2;
  bool has_saved_camera_view_ = false;
  openwow::world::WorldCamera* world_camera_{nullptr};

  std::uint8_t saved_hair_style_ = 0;
  std::uint8_t saved_hair_color_ = 0;
  std::uint8_t saved_facial_hair_ = 0;
  std::uint8_t saved_skin_color_ = 0;
  std::uint8_t saved_face_ = 0;
  bool deferred_active_player_appearance_refresh_ = false;

  mutable std::mutex mutex_;
};

}

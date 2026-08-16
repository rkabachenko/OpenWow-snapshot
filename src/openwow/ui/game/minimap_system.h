
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "openwow/ui/game/minimap_arrows.h"

namespace openwow::ui {

enum class MinimapTrackingType : std::uint8_t {
  None = 0,
  Herbs = 1,
  Minerals = 2,
  Beasts = 3,
  Humanoids = 4,
  Undead = 5,
  Demons = 6,
  Elementals = 7,
  Giants = 8,
  Dragonkin = 9,
  GameObjects = 10,
  Mailbox = 11,
  Repair = 12,
  AuctionHouse = 13,
  Bank = 14,
  ClassTrainer = 15,
  ProfessionTrainer = 16,
};

struct MinimapPin {
  std::uint64_t guid = 0;
  float x = 0.0f, y = 0.0f;
  std::uint32_t icon_id = 0;
  std::string tooltip;
  bool is_party = false;
  bool is_raid = false;
  bool is_tracking = false;
  bool is_poi = false;
};

struct MinimapGuidePointOfInterest {
  float x = 0.0f;
  float y = 0.0f;
  std::uint32_t icon_id = 0;
  std::string title;
};

class MinimapSystem {
 public:
  MinimapSystem() = default;

  static constexpr std::uint32_t kMinZoomLevel = 0;
  static constexpr std::uint32_t kMaxZoomLevel = 5;
  static constexpr std::uint32_t kDefaultZoomLevel = 3;

  void SetZoom(float zoom);
  void SetZoomLevel(std::uint32_t level);
  [[nodiscard]] float GetZoom() const;
  [[nodiscard]] std::uint32_t GetZoomLevel() const;
  [[nodiscard]] std::uint32_t GetOutdoorZoomLevel() const;
  [[nodiscard]] std::uint32_t GetIndoorZoomLevel() const;

  [[nodiscard]] float GetVisibleRadius() const;

  void SetTracking(MinimapTrackingType type, bool enabled);
  [[nodiscard]] bool IsTracking(MinimapTrackingType type) const;
  [[nodiscard]] std::uint32_t GetTrackingMask() const;

  void ClearPins();
  void AddPin(const MinimapPin& pin);
  void RemovePin(std::uint64_t guid);
  [[nodiscard]] const std::vector<MinimapPin>& GetPins() const;
  [[nodiscard]] std::vector<MinimapPin> GetPinsSnapshot() const;
  void SetGossipPointOfInterest(float x, float y, std::uint32_t icon_id,
                                std::string tooltip);
  void SetGuidePointOfInterest(float x, float y, std::uint32_t icon_id,
                               std::string title);
  void ClearGuidePointOfInterest();
  [[nodiscard]] std::optional<MinimapGuidePointOfInterest>
  GetGuidePointOfInterest() const;

  void ClearRotatingArrows();
  void AddRotatingArrow(const MinimapRotatingArrow& arrow);
  [[nodiscard]] std::vector<MinimapRotatingArrow> GetRotatingArrows() const;

  void SetRotatingArrowTexturePath(MinimapRotatingArrowKind kind,
                                   std::string texture_path);
  [[nodiscard]] std::string GetRotatingArrowTexturePath(
      MinimapRotatingArrowKind kind) const;

  void SetPlayerArrowTexturePath(std::string texture_path);
  void ClearPlayerArrowTexturePath();
  [[nodiscard]] std::string GetPlayerArrowTexturePath() const;
  void SetPlayerArrowSize(float width, float height);
  [[nodiscard]] float GetPlayerArrowWidth() const;
  [[nodiscard]] float GetPlayerArrowHeight() const;

  void SetIndoorMinimapActive(bool active);
  [[nodiscard]] bool IsIndoorMinimapActive() const;
  void SetMaskTexturePath(std::string texture_path);
  void ClearMaskTexturePath();
  [[nodiscard]] std::string GetMaskTexturePath() const;
  void SetPoiIconTexturePath(std::string texture_path);
  void ClearPoiIconTexturePath();
  [[nodiscard]] std::string GetPoiIconTexturePath() const;
  void SetObjectIconTexturePath(std::string texture_path);
  void ClearObjectIconTexturePath();
  [[nodiscard]] std::string GetObjectIconTexturePath() const;
  void SetPartyRaidBlipsTexturePath(std::string texture_path);
  void ClearPartyRaidBlipsTexturePath();
  [[nodiscard]] std::string GetPartyRaidBlipsTexturePath() const;

  void MarkExplorationOverlayDirty();
  [[nodiscard]] bool ConsumeExplorationOverlayDirty();

  void UpdatePartyMemberPosition(std::uint8_t index, float x, float y);

  void SetPlayerPosition(float x, float y, float orientation);
  [[nodiscard]] float GetPlayerX() const;
  [[nodiscard]] float GetPlayerY() const;
  [[nodiscard]] float GetPlayerOrientation() const;

  enum class Mode { Rotate, Fixed };

  void SetMode(Mode mode);
  [[nodiscard]] Mode GetMode() const;

  void SetZoneText(const std::string& zone, const std::string& subzone);
  [[nodiscard]] const std::string& GetZoneText() const;
  [[nodiscard]] const std::string& GetSubzoneText() const;

  void Reset();

 private:

  std::uint32_t outdoor_zoom_level_ = kDefaultZoomLevel;
  std::uint32_t indoor_zoom_level_ = kDefaultZoomLevel;
  std::uint32_t tracking_mask_ = 0;
  std::vector<MinimapPin> pins_;
  std::vector<MinimapRotatingArrow> rotating_arrows_;
  std::optional<MinimapGuidePointOfInterest> guide_point_of_interest_;
  float player_x_ = 0.0f, player_y_ = 0.0f, player_orient_ = 0.0f;
  Mode mode_ = Mode::Rotate;
  bool indoor_minimap_active_ = false;
  std::string mask_texture_path_{"Textures\\MinimapMask"};
  std::string poi_icon_texture_path_{"Interface\\Minimap\\POIIcons"};
  std::string object_icon_texture_path_{"Interface\\Minimap\\ObjectIcons"};
  std::string party_raid_blips_texture_path_{
      "Interface\\Minimap\\PartyRaidBlips"};
  std::string static_poi_arrow_texture_{
      "Interface\\Minimap\\Rotating-MinimapArrow"};
  std::string guide_poi_arrow_texture_{
      "Interface\\Minimap\\Rotating-MinimapGuideArrow"};
  std::string corpse_poi_arrow_texture_{
      "Interface\\Minimap\\Rotating-MinimapCorpseArrow"};
  std::string group_member_arrow_texture_{
      "Interface\\Minimap\\Rotating-MinimapGroupArrow"};
  std::string player_arrow_texture_{"Interface\\Minimap\\MinimapArrow.tga"};
  float player_arrow_width_ = 0.0f;
  float player_arrow_height_ = 0.0f;
  bool exploration_overlay_dirty_ = false;
  std::string zone_text_;
  std::string subzone_text_;
  mutable std::mutex mutex_;
};

}

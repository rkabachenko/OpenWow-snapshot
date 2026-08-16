
#include "openwow/ui/game/minimap_system.h"

#include "openwow/world/environment/day_night.h"
#include "openwow/game/minimap_system.h"
#include "openwow/game/minimap_terrain.h"

#include <algorithm>
#include <cmath>

namespace openwow::ui {

namespace {

constexpr char kDefaultStaticPoiArrowTexture[] =
    "Interface\\Minimap\\Rotating-MinimapArrow";
constexpr char kDefaultGuidePoiArrowTexture[] =
    "Interface\\Minimap\\Rotating-MinimapGuideArrow";
constexpr char kDefaultCorpsePoiArrowTexture[] =
    "Interface\\Minimap\\Rotating-MinimapCorpseArrow";
constexpr char kDefaultGroupMemberArrowTexture[] =
    "Interface\\Minimap\\Rotating-MinimapGroupArrow";
constexpr char kDefaultPlayerArrowTexture[] =
    "Interface\\Minimap\\MinimapArrow.tga";

constexpr float kDefaultPlayerArrowSize = 0.0f;
constexpr char kDefaultMaskTexture[] = "Textures\\MinimapMask";
constexpr char kDefaultPoiIconTexture[] = "Interface\\Minimap\\POIIcons";
constexpr char kDefaultObjectIconTexture[] = "Interface\\Minimap\\ObjectIcons";
constexpr char kDefaultPartyRaidBlipsTexture[] =
    "Interface\\Minimap\\PartyRaidBlips";
constexpr std::uint64_t kGossipPointOfInterestGuid = 0xFF10000000000000ULL;

std::uint32_t ClampZoomLevel(const float zoom) {
  if (!std::isfinite(zoom)) {
    return MinimapSystem::kDefaultZoomLevel;
  }

  const auto truncated = static_cast<int>(std::trunc(zoom));
  return static_cast<std::uint32_t>(
      std::clamp(truncated,
                 static_cast<int>(MinimapSystem::kMinZoomLevel),
                 static_cast<int>(MinimapSystem::kMaxZoomLevel)));
}

}

void MinimapSystem::SetZoom(float zoom) {
  SetZoomLevel(ClampZoomLevel(zoom));
}

void MinimapSystem::SetZoomLevel(const std::uint32_t level) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto clamped =
      std::min(level, static_cast<std::uint32_t>(kMaxZoomLevel));
  if (indoor_minimap_active_) {
    indoor_zoom_level_ = clamped;
  } else {
    outdoor_zoom_level_ = clamped;
  }
}

float MinimapSystem::GetZoom() const {
  return static_cast<float>(GetZoomLevel());
}

std::uint32_t MinimapSystem::GetZoomLevel() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return indoor_minimap_active_ ? indoor_zoom_level_ : outdoor_zoom_level_;
}

std::uint32_t MinimapSystem::GetOutdoorZoomLevel() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return outdoor_zoom_level_;
}

std::uint32_t MinimapSystem::GetIndoorZoomLevel() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return indoor_zoom_level_;
}

float MinimapSystem::GetVisibleRadius() const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto zoom_level =
      indoor_minimap_active_ ? indoor_zoom_level_ : outdoor_zoom_level_;
  if (indoor_minimap_active_) {
    return openwow::game::kIndoorZoomRadii[zoom_level];
  }

  return static_cast<float>(openwow::game::kOutdoorZoomSizes[zoom_level]) *
         0.5f * 33.333332f;
}

void MinimapSystem::SetTracking(MinimapTrackingType type, bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto bit = static_cast<std::uint32_t>(1u << static_cast<unsigned>(type));
  if (enabled) {
    tracking_mask_ |= bit;
  } else {
    tracking_mask_ &= ~bit;
  }
}

bool MinimapSystem::IsTracking(MinimapTrackingType type) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto bit = static_cast<std::uint32_t>(1u << static_cast<unsigned>(type));
  return (tracking_mask_ & bit) != 0;
}

std::uint32_t MinimapSystem::GetTrackingMask() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return tracking_mask_;
}

void MinimapSystem::ClearPins() {
  std::lock_guard<std::mutex> lock(mutex_);
  pins_.clear();
}

void MinimapSystem::AddPin(const MinimapPin& pin) {
  std::lock_guard<std::mutex> lock(mutex_);

  for (auto& existing : pins_) {
    if (existing.guid == pin.guid) {
      existing = pin;
      return;
    }
  }
  pins_.push_back(pin);
}

void MinimapSystem::RemovePin(std::uint64_t guid) {
  std::lock_guard<std::mutex> lock(mutex_);
  pins_.erase(
      std::remove_if(pins_.begin(), pins_.end(),
                     [guid](const MinimapPin& p) { return p.guid == guid; }),
      pins_.end());
}

const std::vector<MinimapPin>& MinimapSystem::GetPins() const {
  return pins_;
}

std::vector<MinimapPin> MinimapSystem::GetPinsSnapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pins_;
}

void MinimapSystem::SetGossipPointOfInterest(float x, float y,
                                             const std::uint32_t icon_id,
                                             std::string tooltip) {
  MinimapPin pin;
  pin.guid = kGossipPointOfInterestGuid;
  pin.x = x;
  pin.y = y;
  pin.icon_id = icon_id;
  pin.tooltip = std::move(tooltip);
  pin.is_poi = true;
  AddPin(pin);
}

void MinimapSystem::SetGuidePointOfInterest(float x, float y,
                                            const std::uint32_t icon_id,
                                            std::string title) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (x == 0.0f && y == 0.0f) {
    guide_point_of_interest_.reset();
    return;
  }

  guide_point_of_interest_ = MinimapGuidePointOfInterest{
      .x = x,
      .y = y,
      .icon_id = icon_id,
      .title = std::move(title),
  };
}

void MinimapSystem::ClearGuidePointOfInterest() {
  std::lock_guard<std::mutex> lock(mutex_);
  guide_point_of_interest_.reset();
}

std::optional<MinimapGuidePointOfInterest> MinimapSystem::GetGuidePointOfInterest() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return guide_point_of_interest_;
}

void MinimapSystem::ClearRotatingArrows() {
  std::lock_guard<std::mutex> lock(mutex_);
  rotating_arrows_.clear();
}

void MinimapSystem::AddRotatingArrow(const MinimapRotatingArrow& arrow) {
  std::lock_guard<std::mutex> lock(mutex_);
  rotating_arrows_.push_back(arrow);
}

std::vector<MinimapRotatingArrow> MinimapSystem::GetRotatingArrows() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return rotating_arrows_;
}

void MinimapSystem::SetRotatingArrowTexturePath(
    MinimapRotatingArrowKind kind, std::string texture_path) {
  std::lock_guard<std::mutex> lock(mutex_);
  switch (kind) {
    case MinimapRotatingArrowKind::kStaticPoi:
      static_poi_arrow_texture_ = texture_path;
      break;
    case MinimapRotatingArrowKind::kGroupMember:
      group_member_arrow_texture_ = texture_path;
      break;
    case MinimapRotatingArrowKind::kCorpsePoi:
      corpse_poi_arrow_texture_ = texture_path;
      break;
    case MinimapRotatingArrowKind::kGuidePoi:
    default:
      guide_poi_arrow_texture_ = texture_path;
      break;
  }
}

std::string MinimapSystem::GetRotatingArrowTexturePath(
    MinimapRotatingArrowKind kind) const {
  std::lock_guard<std::mutex> lock(mutex_);
  switch (kind) {
    case MinimapRotatingArrowKind::kStaticPoi:
      return static_poi_arrow_texture_;
    case MinimapRotatingArrowKind::kGroupMember:
      return group_member_arrow_texture_;
    case MinimapRotatingArrowKind::kCorpsePoi:
      return corpse_poi_arrow_texture_;
    case MinimapRotatingArrowKind::kGuidePoi:
    default:
      return guide_poi_arrow_texture_;
  }
}

void MinimapSystem::SetPlayerArrowTexturePath(std::string texture_path) {
  if (texture_path.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  player_arrow_texture_ = std::move(texture_path);
}

void MinimapSystem::ClearPlayerArrowTexturePath() {
  std::lock_guard<std::mutex> lock(mutex_);
  player_arrow_texture_.clear();
}

std::string MinimapSystem::GetPlayerArrowTexturePath() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return player_arrow_texture_;
}

void MinimapSystem::SetPlayerArrowSize(const float width, const float height) {
  std::lock_guard<std::mutex> lock(mutex_);
  player_arrow_width_ = width;
  player_arrow_height_ = height;
}

float MinimapSystem::GetPlayerArrowWidth() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return player_arrow_width_;
}

float MinimapSystem::GetPlayerArrowHeight() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return player_arrow_height_;
}

void MinimapSystem::SetIndoorMinimapActive(bool active) {
  std::lock_guard<std::mutex> lock(mutex_);
  indoor_minimap_active_ = active;
}

bool MinimapSystem::IsIndoorMinimapActive() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return indoor_minimap_active_;
}

void MinimapSystem::SetMaskTexturePath(std::string texture_path) {
  if (texture_path.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  mask_texture_path_ = std::move(texture_path);
}

void MinimapSystem::ClearMaskTexturePath() {
  std::lock_guard<std::mutex> lock(mutex_);
  mask_texture_path_.clear();
}

std::string MinimapSystem::GetMaskTexturePath() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return mask_texture_path_;
}

void MinimapSystem::SetPoiIconTexturePath(std::string texture_path) {
  if (texture_path.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  poi_icon_texture_path_ = std::move(texture_path);
}

void MinimapSystem::ClearPoiIconTexturePath() {
  std::lock_guard<std::mutex> lock(mutex_);
  poi_icon_texture_path_.clear();
}

std::string MinimapSystem::GetPoiIconTexturePath() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return poi_icon_texture_path_;
}

void MinimapSystem::SetObjectIconTexturePath(std::string texture_path) {
  if (texture_path.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  object_icon_texture_path_ = std::move(texture_path);
}

void MinimapSystem::ClearObjectIconTexturePath() {
  std::lock_guard<std::mutex> lock(mutex_);
  object_icon_texture_path_.clear();
}

std::string MinimapSystem::GetObjectIconTexturePath() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return object_icon_texture_path_;
}

void MinimapSystem::SetPartyRaidBlipsTexturePath(std::string texture_path) {
  if (texture_path.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  party_raid_blips_texture_path_ = std::move(texture_path);
}

void MinimapSystem::ClearPartyRaidBlipsTexturePath() {
  std::lock_guard<std::mutex> lock(mutex_);
  party_raid_blips_texture_path_.clear();
}

std::string MinimapSystem::GetPartyRaidBlipsTexturePath() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return party_raid_blips_texture_path_;
}

void MinimapSystem::MarkExplorationOverlayDirty() {
  std::lock_guard<std::mutex> lock(mutex_);
  exploration_overlay_dirty_ = true;
}

bool MinimapSystem::ConsumeExplorationOverlayDirty() {
  std::lock_guard<std::mutex> lock(mutex_);
  const bool dirty = exploration_overlay_dirty_;
  exploration_overlay_dirty_ = false;
  return dirty;
}

void MinimapSystem::UpdatePartyMemberPosition(std::uint8_t index, float x,
                                               float y) {

  std::lock_guard<std::mutex> lock(mutex_);
  auto guid = static_cast<std::uint64_t>(0xFF00000000000000ULL) + index;

  for (auto& pin : pins_) {
    if (pin.guid == guid) {
      pin.x = x;
      pin.y = y;
      return;
    }
  }

  MinimapPin pin;
  pin.guid = guid;
  pin.x = x;
  pin.y = y;
  pin.is_party = true;
  pins_.push_back(pin);
}

void MinimapSystem::SetPlayerPosition(float x, float y, float orientation) {
  std::lock_guard<std::mutex> lock(mutex_);
  player_x_ = x;
  player_y_ = y;
  player_orient_ = orientation;
}

float MinimapSystem::GetPlayerX() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return player_x_;
}

float MinimapSystem::GetPlayerY() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return player_y_;
}

float MinimapSystem::GetPlayerOrientation() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return player_orient_;
}

void MinimapSystem::SetMode(Mode mode) {
  std::lock_guard<std::mutex> lock(mutex_);
  mode_ = mode;
}

MinimapSystem::Mode MinimapSystem::GetMode() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return mode_;
}

void MinimapSystem::SetZoneText(const std::string& zone,
                                 const std::string& subzone) {
  std::lock_guard<std::mutex> lock(mutex_);
  zone_text_ = zone;
  subzone_text_ = subzone;
}

const std::string& MinimapSystem::GetZoneText() const {
  return zone_text_;
}

const std::string& MinimapSystem::GetSubzoneText() const {
  return subzone_text_;
}

void MinimapSystem::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  outdoor_zoom_level_ = kDefaultZoomLevel;
  indoor_zoom_level_ = kDefaultZoomLevel;
  tracking_mask_ = 0;
  pins_.clear();
  rotating_arrows_.clear();
  guide_point_of_interest_.reset();
  player_x_ = 0.0f;
  player_y_ = 0.0f;
  player_orient_ = 0.0f;
  mode_ = Mode::Rotate;
  indoor_minimap_active_ = false;
  mask_texture_path_ = kDefaultMaskTexture;
  poi_icon_texture_path_ = kDefaultPoiIconTexture;
  object_icon_texture_path_ = kDefaultObjectIconTexture;
  party_raid_blips_texture_path_ = kDefaultPartyRaidBlipsTexture;
  static_poi_arrow_texture_ = kDefaultStaticPoiArrowTexture;
  guide_poi_arrow_texture_ = kDefaultGuidePoiArrowTexture;
  corpse_poi_arrow_texture_ = kDefaultCorpsePoiArrowTexture;
  group_member_arrow_texture_ = kDefaultGroupMemberArrowTexture;
  player_arrow_texture_ = kDefaultPlayerArrowTexture;
  player_arrow_width_ = kDefaultPlayerArrowSize;
  player_arrow_height_ = kDefaultPlayerArrowSize;
  exploration_overlay_dirty_ = false;
  zone_text_.clear();
  subzone_text_.clear();
}

}

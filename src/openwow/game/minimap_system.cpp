
#include "openwow/game/minimap_system.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "openwow/foundation/math/row_major_mat4x4.h"

namespace openwow::game {

namespace {

constexpr float kVisibleObjectRadiusScale = 0.8f;
constexpr std::uint32_t kCategoryAttackablePlayer = 2;
constexpr std::uint32_t kCategoryOtherPvPRankFactionPlayer = 3;
constexpr std::uint32_t kCategorySamePvPRankFactionPlayer = 4;
constexpr std::uint32_t kCategoryHostileReaction = 5;
constexpr std::uint32_t kCategoryUnfriendlyReaction = 6;
constexpr std::uint32_t kCategoryFriendlyReaction = 7;
constexpr std::uint32_t kCategoryTracked = 8;
constexpr std::uint32_t kCategoryQuestState8OrTrivial2 = 9;
constexpr std::uint32_t kCategoryQuestState10 = 10;
constexpr std::uint32_t kCategoryQuestState7OrTrivial4 = 11;
constexpr std::uint32_t kCategoryOverlay13 = 13;
constexpr std::uint32_t kCategoryControlledByActivePlayer = 15;
constexpr std::uint32_t kProfessionTrainerNpcFlag = 0x40;
constexpr std::uint32_t kRaidPartyBlipCategoryCount = 2;
constexpr std::uint32_t kRaidBlipCategoryIndex = 0;
constexpr std::uint32_t kFirstObjectBlipCategoryIndex = 2;
constexpr std::uint32_t kLastObjectBlipCategoryIndex = 15;
constexpr std::uint32_t kFirstVehicleStateBlipCategoryIndex = 16;
constexpr std::uint32_t kVehicleStateBlipTextureCount = 7;
constexpr std::uint32_t kLastVehicleStateBlipCategoryIndex =
    kFirstVehicleStateBlipCategoryIndex + kVehicleStateBlipTextureCount - 1;
constexpr std::uint32_t kObjectIconAtlasColumns = 8;
constexpr std::uint32_t kObjectIconAtlasRows = 2;
constexpr std::uint32_t kPartyRaidBlipAtlasColumns = 8;
constexpr std::uint32_t kPartyRaidBlipAtlasRows = 4;
constexpr std::uint32_t kPartyRaidBlipBandSize = 16;

constexpr std::array<std::pair<float, float>, 4> kMinimapBlipQuadOffsets{{
    {-1.0f, 1.0f},
    {1.0f, 1.0f},
    {-1.0f, -1.0f},
    {1.0f, -1.0f},
}};
constexpr std::array<std::uint16_t, 6> kPartyRaidQuadIndices{{0, 1, 2, 2, 1, 3}};

constexpr float kVehicleStateUvPhaseShift = 2.3561944901923448f;

struct VisibleObjectResolution {
  std::uint32_t categoryIndex{0};
  bool professionTrainerHighlight{false};
  bool category13Overlay{false};
};

struct MinimapAtlasUvQuad {
  float lowerLeftU{0.0f};
  float lowerLeftV{0.0f};
  float lowerRightU{0.0f};
  float lowerRightV{0.0f};
  float upperLeftU{0.0f};
  float upperLeftV{0.0f};
  float upperRightU{0.0f};
  float upperRightV{0.0f};
};

template <typename Slot, std::uint32_t LockedGrowthQuantum>
[[nodiscard]] std::uint32_t AppendSubmittedBuffer(
    openwow::core::TSGrowableArray<Slot, LockedGrowthQuantum> &buffer,
    const std::uint32_t appendedCount,
    const Slot *const source) {
  const std::uint32_t startIndex = buffer.GetCount();
  if (appendedCount == 0) {
    return startIndex;
  }

  buffer.SetCountUninitialized(startIndex + appendedCount);
  std::copy_n(source, appendedCount, buffer.GetData() + startIndex);
  return startIndex;
}

template <typename Slot, std::uint32_t LockedGrowthQuantum>
[[nodiscard]] const Slot *SubmittedBufferSlotAt(
    const openwow::core::TSGrowableArray<Slot, LockedGrowthQuantum> &buffer,
    const std::uint32_t index) noexcept {
  if (index >= buffer.GetCapacity()) {
    return nullptr;
  }
  return buffer.GetData() + index;
}

template <typename Slot, std::uint32_t LockedGrowthQuantum>
void ResetSubmittedBuffer(
    openwow::core::TSGrowableArray<Slot, LockedGrowthQuantum> &buffer) {

  buffer.Clear();
}

[[nodiscard]] bool IsWithinCallbackRange(const MinimapVisibleObjectContext &context,
                                         const MinimapVisibleObjectSnapshot &object) noexcept {
  const float visibleRadius = context.visibleRange * kVisibleObjectRadiusScale;
  const float dx = object.worldX - context.centerX;
  const float dy = object.worldY - context.centerY;
  return (dx * dx) + (dy * dy) <= (visibleRadius * visibleRadius);
}

[[nodiscard]] bool TransportKeysMatch(const MinimapTransportKey &lhs,
                                      const MinimapTransportKey &rhs) noexcept {
  return lhs.low == rhs.low && lhs.high == rhs.high;
}

[[nodiscard]] bool MatchesTrackedNpcSelection(
    const std::optional<MinimapTrackedInfoSelection> &trackedInfo,
    const MinimapVisibleObjectSnapshot &object) noexcept {
  return trackedInfo.has_value() &&
         trackedInfo->type == MinimapTrackedInfoType::kNpcFlags &&
         (object.npcTrackingFlags & trackedInfo->value) != 0;
}

[[nodiscard]] bool MatchesTrackedGameObjectSelection(
    const std::optional<MinimapTrackedInfoSelection> &trackedInfo,
    const MinimapVisibleObjectSnapshot &object) noexcept {
  return trackedInfo.has_value() &&
         trackedInfo->type == MinimapTrackedInfoType::kGameObjectType &&
         trackedInfo->value == object.gameObjectTypeId;
}

[[nodiscard]] bool IsQuestStateCategory9(const MinimapVisibleObjectContext &context,
                                         const std::uint32_t questMarkerState) noexcept {
  return questMarkerState == 8 ||
         (questMarkerState == 2 && context.showTrivialQuestMarkers);
}

[[nodiscard]] bool IsQuestStateCategory11(const MinimapVisibleObjectContext &context,
                                          const std::uint32_t questMarkerState) noexcept {
  return questMarkerState == 7 ||
         (questMarkerState == 4 && context.showTrivialQuestMarkers);
}

[[nodiscard]] std::optional<VisibleObjectResolution>
ResolveVisibleObjectBlip(const MinimapVisibleObjectContext &context,
                         const MinimapVisibleObjectSnapshot &object) {
  if (object.guid == context.activePlayerGuid || !IsWithinCallbackRange(context, object)) {
    return std::nullopt;
  }

  VisibleObjectResolution resolution{};

  switch (object.objectType) {
  case MinimapVisibleObjectType::kPlayer:
    if (object.isActivePlayerOrPartyOrRaidMember) {
      return std::nullopt;
    }
    [[fallthrough]];
  case MinimapVisibleObjectType::kUnit:
    if (object.controllingGuid == context.activePlayerGuid) {
      resolution.categoryIndex = kCategoryControlledByActivePlayer;
      return resolution;
    }

    if (!object.passesInteractionGate) {
      return std::nullopt;
    }

    if (MatchesTrackedNpcSelection(context.trackedInfo, object) &&
        object.passesTrackedInfoTargetFilter) {
      resolution.categoryIndex = kCategoryTracked;
      resolution.professionTrainerHighlight =
          (context.trackedInfo->value & kProfessionTrainerNpcFlag) != 0;
      return resolution;
    }

    if (object.questMarkerState == 10) {
      resolution.categoryIndex = kCategoryQuestState10;
      return resolution;
    }

    if (IsQuestStateCategory9(context, object.questMarkerState)) {
      resolution.categoryIndex = kCategoryQuestState8OrTrivial2;
      return resolution;
    }

    if (IsQuestStateCategory11(context, object.questMarkerState)) {
      resolution.categoryIndex = kCategoryQuestState7OrTrivial4;
      return resolution;
    }

    if (object.hasCategory13Overlay) {
      resolution.categoryIndex = kCategoryOverlay13;
      resolution.category13Overlay = true;
      return resolution;
    }

    if (!ShouldShowOnMinimap(object.minimapTrackInfo)) {
      return std::nullopt;
    }

    if (object.isPlayerControlledTarget) {
      if (object.canAttackSpellTarget) {
        resolution.categoryIndex = kCategoryAttackablePlayer;
      } else if (object.pvpRankFactionSelector ==
                 object.activePlayerPvpRankFactionSelector) {
        resolution.categoryIndex = kCategorySamePvPRankFactionPlayer;
      } else {
        resolution.categoryIndex = kCategoryOtherPvPRankFactionPlayer;
      }
      return resolution;
    }

    if (object.reactionLevel >= 4) {
      resolution.categoryIndex = kCategoryFriendlyReaction;
    } else if (object.reactionLevel > 1) {
      resolution.categoryIndex = kCategoryUnfriendlyReaction;
    } else {
      resolution.categoryIndex = kCategoryHostileReaction;
    }
    return resolution;

  case MinimapVisibleObjectType::kGameObject:
    if (MatchesTrackedGameObjectSelection(context.trackedInfo, object) &&
        object.passesTrackedInfoTargetFilter) {
      resolution.categoryIndex = kCategoryTracked;
      return resolution;
    }

    if (object.questMarkerState == 10) {
      resolution.categoryIndex = kCategoryQuestState10;
      return resolution;
    }

    if (IsQuestStateCategory9(context, object.questMarkerState)) {
      resolution.categoryIndex = kCategoryQuestState8OrTrivial2;
      return resolution;
    }

    if (IsQuestStateCategory11(context, object.questMarkerState)) {
      resolution.categoryIndex = kCategoryQuestState7OrTrivial4;
      return resolution;
    }

    if (!object.passesGameObjectMinimapFilter) {
      return std::nullopt;
    }

    resolution.categoryIndex = kCategoryTracked;
    return resolution;
  }

  return std::nullopt;
}

[[nodiscard]] MinimapAtlasUvQuad ComputePartyRaidBlipAtlasUvQuad(
    const std::uint32_t atlasIndex) noexcept {
  const std::uint32_t column = atlasIndex % kPartyRaidBlipAtlasColumns;
  const std::uint32_t row = atlasIndex / kPartyRaidBlipAtlasColumns;
  const float cellWidth = 1.0f / static_cast<float>(kPartyRaidBlipAtlasColumns);
  const float cellHeight = 1.0f / static_cast<float>(kPartyRaidBlipAtlasRows);
  const float left = static_cast<float>(column) * cellWidth;
  const float top = static_cast<float>(row) * cellHeight;
  const float right = left + cellWidth;
  const float bottom = top + cellHeight;
  return {left, bottom, right, bottom, left, top, right, top};
}

[[nodiscard]] MinimapAtlasUvQuad ComputeObjectIconAtlasUvQuad(
    const std::uint32_t atlasIndex) noexcept {
  const std::uint32_t column = atlasIndex % kObjectIconAtlasColumns;
  const std::uint32_t row = atlasIndex / kObjectIconAtlasColumns;
  const float cellWidth = 1.0f / static_cast<float>(kObjectIconAtlasColumns);
  const float cellHeight = 1.0f / static_cast<float>(kObjectIconAtlasRows);
  const float left = static_cast<float>(column) * cellWidth;
  const float top = static_cast<float>(row) * cellHeight;
  const float right = left + cellWidth;
  const float bottom = top + cellHeight;
  return {left, bottom, right, bottom, left, top, right, top};
}

void TransformPreservedCenter(std::array<float, 4> &renderQuad, const float *const viewMatrix,
                              const bool applyViewTransform, float &centerX,
                              float &centerY) {
  centerX = renderQuad[0];
  centerY = renderQuad[1];
  if (!applyViewTransform || viewMatrix == nullptr) {
    return;
  }

  const std::array<float, 3> sourcePoint{
      renderQuad[2], renderQuad[3], 0.0f};
  std::array<float, 3> transformedPoint{};
  openwow::math::row_major_mat4x4::TransformPointByRowMajorAffine4x4(
      transformedPoint.data(), sourcePoint.data(), viewMatrix);
  renderQuad[0] = transformedPoint[0];
  renderQuad[1] = transformedPoint[1];
  centerX = transformedPoint[0];
  centerY = transformedPoint[1];
}

void TransformBlipSlotCenter(MinimapObjectInfoSlot &slot, const float *const viewMatrix,
                             const bool applyViewTransform, float &centerX,
                             float &centerY) {
  TransformPreservedCenter(slot.renderQuad, viewMatrix, applyViewTransform, centerX, centerY);
}

[[nodiscard]] std::array<MinimapBatchVertex, 4> BuildBlipQuadVertices(
    const float centerX, const float centerY, const float blipHalfExtent) {
  std::array<MinimapBatchVertex, 4> vertices{};
  for (std::size_t vertexIndex = 0; vertexIndex < vertices.size(); ++vertexIndex) {
    vertices[vertexIndex].x =
        centerX + blipHalfExtent * kMinimapBlipQuadOffsets[vertexIndex].first;
    vertices[vertexIndex].y =
        centerY + blipHalfExtent * kMinimapBlipQuadOffsets[vertexIndex].second;
    vertices[vertexIndex].z = 0.0f;
  }
  return vertices;
}

[[nodiscard]] std::pair<float, float> BuildDirectionalArrowCenter(
    const MinimapDirectionalArrowLayout &layout,
    const float angleRadians) noexcept {
  return {
      layout.centerX - std::sin(angleRadians) * layout.ringRadius,
      layout.centerY - std::cos(angleRadians) * layout.ringRadius,
  };
}

void TransformDirectionalArrowCenter(MinimapDirectionalArrowSlot &slot,
                                     const MinimapDirectionalArrowLayout &layout,
                                     const float angleRadians,
                                     const float *const viewMatrix,
                                     const bool applyViewTransform,
                                     float &centerX,
                                     float &centerY) {
  const auto [baseCenterX, baseCenterY] = BuildDirectionalArrowCenter(layout, angleRadians);
  centerX = baseCenterX;
  centerY = baseCenterY;

  if (applyViewTransform && viewMatrix != nullptr) {
    const std::array<float, 3> sourcePoint{baseCenterX, baseCenterY, 0.0f};
    std::array<float, 3> transformedPoint{};
    openwow::math::row_major_mat4x4::TransformPointByRowMajorAffine4x4(
        transformedPoint.data(), sourcePoint.data(), viewMatrix);
    centerX = transformedPoint[0];
    centerY = transformedPoint[1];
  }

  slot.SetSubmittedCenter(centerX, centerY);
}

[[nodiscard]] std::array<float, 16> BuildDirectionalViewMatrix(
    const MinimapDirectionalArrowLayout &layout,
    const float angleRadians) noexcept {
  std::array<float, 16> matrix{};
  openwow::math::row_major_mat4x4::SetIdentity(matrix.data());

  const float cosValue = std::cos(angleRadians);
  const float sinValue = std::sin(angleRadians);
  matrix[0] = cosValue;
  matrix[1] = sinValue;
  matrix[4] = -sinValue;
  matrix[5] = cosValue;
  matrix[12] = layout.centerX - layout.centerX * cosValue + layout.centerY * sinValue;
  matrix[13] = layout.centerY - layout.centerX * sinValue - layout.centerY * cosValue;
  return matrix;
}

[[nodiscard]] std::array<std::uint16_t, 6> BuildBlipQuadIndices(
    const std::uint32_t baseVertex) {
  return {{
      static_cast<std::uint16_t>(baseVertex + kPartyRaidQuadIndices[0]),
      static_cast<std::uint16_t>(baseVertex + kPartyRaidQuadIndices[1]),
      static_cast<std::uint16_t>(baseVertex + kPartyRaidQuadIndices[2]),
      static_cast<std::uint16_t>(baseVertex + kPartyRaidQuadIndices[3]),
      static_cast<std::uint16_t>(baseVertex + kPartyRaidQuadIndices[4]),
      static_cast<std::uint16_t>(baseVertex + kPartyRaidQuadIndices[5]),
  }};
}

[[nodiscard]] std::array<MinimapBatchTexCoord, 4> BuildAtlasTexCoordQuad(
    const MinimapAtlasUvQuad &uvQuad) {
  return {{
      {uvQuad.lowerLeftU, uvQuad.lowerLeftV},
      {uvQuad.lowerRightU, uvQuad.lowerRightV},
      {uvQuad.upperLeftU, uvQuad.upperLeftV},
      {uvQuad.upperRightU, uvQuad.upperRightV},
  }};
}

[[nodiscard]] std::array<MinimapBatchTexCoord, 4> BuildVehicleStateTexCoordQuad(
    const float facingRadians) {
  const float adjustedAngle = facingRadians - kVehicleStateUvPhaseShift;
  const float sinValue = std::sin(adjustedAngle);
  const float cosValue = std::cos(adjustedAngle);
  return {{
      {sinValue + 0.5f, 0.5f - cosValue},
      {0.5f - cosValue, 0.5f - sinValue},
      {cosValue + 0.5f, sinValue + 0.5f},
      {0.5f - sinValue, cosValue + 0.5f},
  }};
}

}

void MinimapSystem::SetPlayerPosition(float x, float y, float facing) {
  playerX_ = x;
  playerY_ = y;
  playerFacing_ = facing;
}

MinimapSystem::PlayerPosition MinimapSystem::GetPlayerPosition() const {
  return {playerX_, playerY_, playerFacing_};
}

void MinimapSystem::SetDirectionalArrowLayout(const float centerX,
                                              const float centerY,
                                              const float ringRadius,
                                              const float iconHalfExtent) noexcept {
  directionalArrowLayout_ = {
      centerX,
      centerY,
      std::max(ringRadius, 0.0f),
      std::max(iconHalfExtent, 0.0f),
  };
}

MinimapDirectionalArrowLayout MinimapSystem::GetDirectionalArrowLayout() const noexcept {
  return directionalArrowLayout_;
}

void MinimapSystem::SetBlipHalfExtents(
    const float blipHalfExtent,
    const float vehicleStateBlipHalfExtent) noexcept {
  blipHalfExtent_ = std::max(blipHalfExtent, 0.0f);
  vehicleStateBlipHalfExtent_ = std::max(vehicleStateBlipHalfExtent, 0.0f);
}

void MinimapSystem::SetZoom(float level) {
  if (!std::isfinite(level)) {
    zoom_ = kDefaultZoom;
    return;
  }
  zoom_ = std::clamp(std::trunc(level), kMinZoom, kMaxZoom);
}

float MinimapSystem::GetZoom() const {
  return zoom_;
}

void MinimapSystem::AddIcon(const MinimapIcon &icon) {
  icons_[icon.guid.GetRawValue()] = icon;
}

void MinimapSystem::RemoveIcon(ObjectGuid guid) {
  icons_.erase(guid.GetRawValue());
}

std::vector<MinimapIcon> MinimapSystem::GetVisibleIcons() const {
  const float radius = GetMinimapRadius();
  const float r2 = radius * radius;
  std::vector<MinimapIcon> result;
  result.reserve(icons_.size());
  for (const auto &[raw, icon] : icons_) {
    const float dx = icon.worldX - playerX_;
    const float dy = icon.worldY - playerY_;
    if (dx * dx + dy * dy <= r2) {
      result.push_back(icon);
    }
  }
  return result;
}

std::size_t MinimapSystem::GetIconCount() const {
  return icons_.size();
}

void MinimapSystem::SetTrackingEnabled(MinimapIconType type, bool enabled) {
  if (enabled) {
    enabledTracking_.insert(type);
  } else {
    enabledTracking_.erase(type);
  }
}

bool MinimapSystem::IsTrackingEnabled(MinimapIconType type) const {
  return enabledTracking_.count(type) > 0;
}

float MinimapSystem::GetMinimapRadius() const {
  static constexpr std::array<float, 6> kOutdoorZoomRadii{
      233.333328f, 183.333328f, 133.333328f,
      83.333328f, 50.0f, 33.333332f};
  return kOutdoorZoomRadii[static_cast<std::size_t>(zoom_)];
}

MinimapSystem::MinimapCoord MinimapSystem::WorldToMinimap(float wx, float wy) const {
  const float radius = GetMinimapRadius();
  const float dx = wx - playerX_;
  const float dy = wy - playerY_;
  float screenX = -dy;
  float screenY = dx;

  if (rotating_) {
    const float c = std::cos(-playerFacing_);
    const float s = std::sin(-playerFacing_);
    const float rotatedX = screenX * c - screenY * s;
    const float rotatedY = screenX * s + screenY * c;
    screenX = rotatedX;
    screenY = rotatedY;
  }

  return {screenX / radius, screenY / radius};
}

void MinimapSystem::SetRotating(bool rotating) {
  rotating_ = rotating;
}

bool MinimapSystem::IsRotating() const {
  return rotating_;
}

void MinimapSystem::Reset() {
  playerX_ = 0.0f;
  playerY_ = 0.0f;
  playerFacing_ = 0.0f;
  zoom_ = kDefaultZoom;
  rotating_ = true;
  icons_.clear();
  enabledTracking_.clear();
  for (auto &category : objectInfoCategories_) {
    category.Clear();
  }
  directionalArrowLayout_ = {};
  blipHalfExtent_ = 0.0f;
  vehicleStateBlipHalfExtent_ = 0.0f;
  for (auto &slot : vehicleIconSlots_) {
    slot.Reset();
  }
  poiInfo_.Clear();
  ClearSubmittedGeometry();
}

std::uint8_t ComputeMinimapFogAlpha(std::uint32_t packed_argb) noexcept {
  const std::uint32_t blue = packed_argb & 0xFFu;
  const std::uint32_t green = (packed_argb >> 8) & 0xFFu;
  const std::uint32_t red = (packed_argb >> 16) & 0xFFu;
  return static_cast<std::uint8_t>(((77u * red) + (151u * green) + (28u * blue)) >> 8);
}

std::uint8_t ComputeMinimapFogBlendAlpha(std::uint32_t packed_argb) noexcept {
  const std::uint32_t alpha = static_cast<std::uint32_t>(ComputeMinimapFogAlpha(packed_argb)) + 96u;
  return static_cast<std::uint8_t>(std::min(alpha, 255u));
}

std::uint32_t BuildMinimapTerrainTint(const std::uint32_t packed_argb) noexcept {
  return (static_cast<std::uint32_t>(ComputeMinimapFogBlendAlpha(packed_argb))
          << 24) |
         0x00FFFFFFu;
}

void MinimapSystem::UpdateDirection(float ) {}

MinimapRenderBuffer<MinimapObjectInfoSlot> *
MinimapSystem::ObjectInfoCategoryBuffer(const std::uint32_t categoryIndex) noexcept {
  if (categoryIndex >= objectInfoCategories_.size()) {
    return nullptr;
  }
  return &objectInfoCategories_[categoryIndex];
}

const MinimapRenderBuffer<MinimapObjectInfoSlot> *
MinimapSystem::ObjectInfoCategoryBuffer(const std::uint32_t categoryIndex) const noexcept {
  if (categoryIndex >= objectInfoCategories_.size()) {
    return nullptr;
  }
  return &objectInfoCategories_[categoryIndex];
}

void MinimapSystem::SetObjectInfoCategoryCapacity(const std::uint32_t categoryIndex,
                                                  const std::uint32_t newCapacity) {
  auto *const buffer = ObjectInfoCategoryBuffer(categoryIndex);
  if (buffer == nullptr) {
    return;
  }
  buffer->SetCapacity(newCapacity);
}

std::uint32_t
MinimapSystem::GetObjectInfoCategoryCapacity(const std::uint32_t categoryIndex) const noexcept {
  const auto *const buffer = ObjectInfoCategoryBuffer(categoryIndex);
  return buffer != nullptr ? buffer->Capacity() : 0;
}

void MinimapSystem::SetObjectInfoCategoryCount(const std::uint32_t categoryIndex,
                                               const std::uint32_t newCount) {
  auto *const buffer = ObjectInfoCategoryBuffer(categoryIndex);
  if (buffer == nullptr) {
    return;
  }

  buffer->ResizeCount(newCount, [](MinimapObjectInfoSlot &slot) { slot.ClearRenderQuad(); });
}

std::uint32_t
MinimapSystem::GetObjectInfoCategoryCount(const std::uint32_t categoryIndex) const noexcept {
  const auto *const buffer = ObjectInfoCategoryBuffer(categoryIndex);
  return buffer != nullptr ? buffer->Count() : 0;
}

MinimapObjectInfoSlot *
MinimapSystem::GetObjectInfoCategorySlot(const std::uint32_t categoryIndex,
                                         const std::uint32_t index) noexcept {
  auto *const buffer = ObjectInfoCategoryBuffer(categoryIndex);
  return buffer != nullptr ? buffer->SlotAt(index) : nullptr;
}

const MinimapObjectInfoSlot *
MinimapSystem::GetObjectInfoCategorySlot(const std::uint32_t categoryIndex,
                                         const std::uint32_t index) const noexcept {
  const auto *const buffer = ObjectInfoCategoryBuffer(categoryIndex);
  return buffer != nullptr ? buffer->SlotAt(index) : nullptr;
}

MinimapObjectInfoSlot *
MinimapSystem::AllocateObjectInfoSlot(const std::uint32_t categoryIndex) noexcept {
  auto *const buffer = ObjectInfoCategoryBuffer(categoryIndex);
  if (buffer == nullptr) {
    return nullptr;
  }

  return buffer->AllocateSlot([](MinimapObjectInfoSlot &slot) { slot.ClearRenderQuad(); });
}

void MinimapSystem::ClearObjectInfoCategories() noexcept {
  for (auto &category : objectInfoCategories_) {
    category.SetCount(0);
  }
}

void MinimapSystem::ClearDirectionalIconSlots() noexcept {
  for (auto &slot : vehicleIconSlots_) {
    slot.Reset();
  }
}

bool MinimapSystem::AppendVisibleObjectBlip(const MinimapVisibleObjectContext &context,
                                            const MinimapVisibleObjectSnapshot &object) {
  const auto resolution = ResolveVisibleObjectBlip(context, object);
  if (!resolution.has_value()) {
    return false;
  }

  if (!context.viewerTransport.IsEmpty() &&
      !TransportKeysMatch(context.viewerTransport, object.transport)) {
    return false;
  }

  auto *const slot = AllocateObjectInfoSlot(resolution->categoryIndex);
  if (slot == nullptr) {
    return false;
  }

  slot->SetGuid(object.guid);
  slot->SetProjectedCoords(object.minimapX, object.minimapY);
  slot->SetTransportLayerMismatch(
      !TransportKeysMatch(object.transport, context.viewerTransport));
  slot->SetProfessionTrainerHighlight(resolution->professionTrainerHighlight);
  slot->SetCategory13Overlay(resolution->category13Overlay);
  return true;
}

void MinimapSystem::ResizeBlipData(std::uint32_t newCount) {
  poiInfo_.ResizeCount(newCount, [](MinimapPoiInfoSlot &slot) {
    slot.ClearRenderData();
  });
}

std::uint32_t MinimapSystem::SubmitVertices(const std::uint32_t vertexCount,
                                            const MinimapBatchVertex *vertices) {
  return AppendSubmittedBuffer(submittedVertices_, vertexCount, vertices);
}

std::uint32_t MinimapSystem::SubmitTexCoords(
    const std::uint32_t texCoordCount,
    const MinimapBatchTexCoord *texCoords) {
  return AppendSubmittedBuffer(submittedTexCoords_, texCoordCount, texCoords);
}

void MinimapSystem::SetTexCoordCount(const std::uint32_t newCount) {
  submittedTexCoords_.SetCountZeroInit(newCount);
}

std::uint32_t MinimapSystem::SubmitIndices(const std::uint32_t indexCount,
                                           const std::uint16_t *indices) {
  return AppendSubmittedBuffer(submittedIndices_, indexCount, indices);
}

bool MinimapSystem::SubmitPartyRaidBlipBatch(const float blipHalfExtent,
                                             const float *viewMatrix,
                                             const bool applyViewTransform) {
  bool submittedAny = false;
  const std::uint32_t vertexStart = submittedVertices_.GetCount();
  const std::uint32_t texCoordStart = submittedTexCoords_.GetCount();
  const std::uint32_t indexStart = submittedIndices_.GetCount();

  for (std::uint32_t categoryIndex = 0; categoryIndex < kRaidPartyBlipCategoryCount;
       ++categoryIndex) {
    auto *const buffer = ObjectInfoCategoryBuffer(categoryIndex);
    if (buffer == nullptr) {
      continue;
    }

    for (std::uint32_t slotIndex = 0; slotIndex < buffer->Count(); ++slotIndex) {
      auto *const slot = buffer->SlotAt(slotIndex);
      if (slot == nullptr || slot->flags != 0 || slot->GetBlipTextureCellPlusOne() == 0) {
        continue;
      }

      float centerX = 0.0f;
      float centerY = 0.0f;
      TransformBlipSlotCenter(*slot, viewMatrix, applyViewTransform, centerX, centerY);

      const auto vertices = BuildBlipQuadVertices(centerX, centerY, blipHalfExtent);

      const std::uint32_t baseVertex =
          SubmitVertices(static_cast<std::uint32_t>(vertices.size()), vertices.data());
      const auto indices = BuildBlipQuadIndices(baseVertex);
      (void)SubmitIndices(static_cast<std::uint32_t>(indices.size()), indices.data());

      std::uint32_t atlasIndex = slot->GetBlipTextureCellPlusOne() - 1;

      if (categoryIndex == kRaidBlipCategoryIndex) {
        atlasIndex += kPartyRaidBlipBandSize;
      }
      const auto uvQuad = ComputePartyRaidBlipAtlasUvQuad(atlasIndex);
      const auto texCoords = BuildAtlasTexCoordQuad(uvQuad);
      (void)SubmitTexCoords(static_cast<std::uint32_t>(texCoords.size()), texCoords.data());
      submittedAny = true;
    }
  }

  if (submittedAny) {
    submittedBatches_.push_back(MinimapSubmittedBatch{
        MinimapSubmittedBatchKind::kPartyRaidAtlas,
        kRaidBlipCategoryIndex,
        vertexStart,
        submittedVertices_.GetCount() - vertexStart,
        texCoordStart,
        submittedTexCoords_.GetCount() - texCoordStart,
        indexStart,
        submittedIndices_.GetCount() - indexStart,
    });
  }

  return submittedAny;
}

bool MinimapSystem::SubmitObjectBlipBatch(const float blipHalfExtent,
                                          const float *viewMatrix,
                                          const bool applyViewTransform,
                                          const bool transportLayerMismatch) {
  bool submittedAny = false;
  const std::uint32_t vertexStart = submittedVertices_.GetCount();
  const std::uint32_t texCoordStart = submittedTexCoords_.GetCount();
  const std::uint32_t indexStart = submittedIndices_.GetCount();

  for (std::uint32_t categoryIndex = kFirstObjectBlipCategoryIndex;
       categoryIndex <= kLastObjectBlipCategoryIndex; ++categoryIndex) {
    auto *const buffer = ObjectInfoCategoryBuffer(categoryIndex);
    if (buffer == nullptr) {
      continue;
    }

    for (std::uint32_t slotIndex = 0; slotIndex < buffer->Count(); ++slotIndex) {
      auto *const slot = buffer->SlotAt(slotIndex);
      if (slot == nullptr || slot->HasTransportLayerMismatch() != transportLayerMismatch) {
        continue;
      }

      float centerX = 0.0f;
      float centerY = 0.0f;
      TransformBlipSlotCenter(*slot, viewMatrix, applyViewTransform, centerX, centerY);

      const auto vertices = BuildBlipQuadVertices(centerX, centerY, blipHalfExtent);

      const std::uint32_t baseVertex =
          SubmitVertices(static_cast<std::uint32_t>(vertices.size()), vertices.data());
      const auto indices = BuildBlipQuadIndices(baseVertex);
      (void)SubmitIndices(static_cast<std::uint32_t>(indices.size()), indices.data());

      const auto uvQuad = ComputeObjectIconAtlasUvQuad(categoryIndex);
      const auto texCoords = BuildAtlasTexCoordQuad(uvQuad);
      (void)SubmitTexCoords(static_cast<std::uint32_t>(texCoords.size()), texCoords.data());
      submittedAny = true;
    }
  }

  if (submittedAny) {
    submittedBatches_.push_back(MinimapSubmittedBatch{
        MinimapSubmittedBatchKind::kObjectAtlas,
        transportLayerMismatch ? 1u : 0u,
        vertexStart,
        submittedVertices_.GetCount() - vertexStart,
        texCoordStart,
        submittedTexCoords_.GetCount() - texCoordStart,
        indexStart,
        submittedIndices_.GetCount() - indexStart,
    });
  }

  return submittedAny;
}

bool MinimapSystem::SubmitVehicleStateBlipBatch(const float blipHalfExtent,
                                                const float *viewMatrix,
                                                const bool applyViewTransform) {
  bool submittedAny = false;

  for (std::uint32_t categoryIndex = kFirstVehicleStateBlipCategoryIndex;
       categoryIndex <= kLastVehicleStateBlipCategoryIndex; ++categoryIndex) {
    auto *const buffer = ObjectInfoCategoryBuffer(categoryIndex);
    if (buffer == nullptr || buffer->Count() == 0) {
      continue;
    }

    const std::uint32_t vertexStart = submittedVertices_.GetCount();
    const std::uint32_t texCoordStart = submittedTexCoords_.GetCount();
    const std::uint32_t indexStart = submittedIndices_.GetCount();
    bool submittedCategory = false;

    for (std::uint32_t slotIndex = 0; slotIndex < buffer->Count(); ++slotIndex) {
      auto *const slot = buffer->SlotAt(slotIndex);
      if (slot == nullptr || slot->flags != 0) {
        continue;
      }

      float centerX = 0.0f;
      float centerY = 0.0f;
      TransformBlipSlotCenter(*slot, viewMatrix, applyViewTransform, centerX, centerY);

      const auto vertices = BuildBlipQuadVertices(centerX, centerY, blipHalfExtent);
      const std::uint32_t baseVertex =
          SubmitVertices(static_cast<std::uint32_t>(vertices.size()), vertices.data());
      const auto indices = BuildBlipQuadIndices(baseVertex);
      (void)SubmitIndices(static_cast<std::uint32_t>(indices.size()), indices.data());

      const auto texCoords = BuildVehicleStateTexCoordQuad(slot->GetFacingRadians());
      (void)SubmitTexCoords(static_cast<std::uint32_t>(texCoords.size()), texCoords.data());
      submittedCategory = true;
      submittedAny = true;
    }

    if (submittedCategory) {
      submittedBatches_.push_back(MinimapSubmittedBatch{
          MinimapSubmittedBatchKind::kVehicleStateTexture,
          categoryIndex,
          vertexStart,
          submittedVertices_.GetCount() - vertexStart,
          texCoordStart,
          submittedTexCoords_.GetCount() - texCoordStart,
          indexStart,
          submittedIndices_.GetCount() - indexStart,
      });
    }
  }

  return submittedAny;
}

MinimapDirectionalArrowSlot *
MinimapSystem::GetVehicleIconSlot(const std::uint32_t index) noexcept {
  if (index >= vehicleIconSlots_.size()) {
    return nullptr;
  }
  return &vehicleIconSlots_[index];
}

const MinimapDirectionalArrowSlot *
MinimapSystem::GetVehicleIconSlot(const std::uint32_t index) const noexcept {
  if (index >= vehicleIconSlots_.size()) {
    return nullptr;
  }
  return &vehicleIconSlots_[index];
}

bool MinimapSystem::SubmitDirectionalArrowBatch(
    std::array<MinimapDirectionalArrowSlot, kVehicleIconSlotCount> &slots,
    const MinimapSubmittedBatchKind batchKind,
    const float *const viewMatrix,
    const bool applyViewTransform) {
  return SubmitDirectionalArrowBatchImpl(
      slots.data(),
      static_cast<std::uint32_t>(slots.size()),
      batchKind,
      viewMatrix,
      applyViewTransform);
}

bool MinimapSystem::SubmitDirectionalArrowBatchImpl(
    MinimapDirectionalArrowSlot *slots,
    const std::uint32_t slotCount,
    const MinimapSubmittedBatchKind batchKind,
    const float *const viewMatrix,
    const bool applyViewTransform) {
  if (slots == nullptr || slotCount == 0 || directionalArrowLayout_.iconHalfExtent <= 0.0f) {
    return false;
  }

  bool submittedAny = false;
  const std::uint32_t vertexStart = submittedVertices_.GetCount();
  const std::uint32_t texCoordStart = submittedTexCoords_.GetCount();
  const std::uint32_t indexStart = submittedIndices_.GetCount();

  for (std::uint32_t index = 0; index < slotCount; ++index) {
    auto &slot = slots[index];
    if (!slot.IsVisible()) {
      continue;
    }

    float centerX = 0.0f;
    float centerY = 0.0f;
    TransformDirectionalArrowCenter(slot,
                                    directionalArrowLayout_,
                                    slot.GetFacingRadians(),
                                    viewMatrix,
                                    applyViewTransform,
                                    centerX,
                                    centerY);

    const auto vertices = BuildBlipQuadVertices(
        centerX, centerY, directionalArrowLayout_.iconHalfExtent);
    const std::uint32_t baseVertex =
        SubmitVertices(static_cast<std::uint32_t>(vertices.size()), vertices.data());
    const auto indices = BuildBlipQuadIndices(baseVertex);
    (void)SubmitIndices(static_cast<std::uint32_t>(indices.size()), indices.data());

    const float uvFacingRadians =
        slot.GetFacingRadians() - (applyViewTransform ? playerFacing_ : 0.0f);
    const auto texCoords = BuildVehicleStateTexCoordQuad(uvFacingRadians);
    (void)SubmitTexCoords(static_cast<std::uint32_t>(texCoords.size()), texCoords.data());
    submittedAny = true;
  }

  if (submittedAny) {
    submittedBatches_.push_back(MinimapSubmittedBatch{
        batchKind,
        0,
        vertexStart,
        submittedVertices_.GetCount() - vertexStart,
        texCoordStart,
        submittedTexCoords_.GetCount() - texCoordStart,
        indexStart,
        submittedIndices_.GetCount() - indexStart,
    });
  }

  return submittedAny;
}

bool MinimapSystem::SubmitVehicleIconBatch(const float *const viewMatrix,
                                           const bool applyViewTransform) {
  return SubmitDirectionalArrowBatch(
      vehicleIconSlots_,
      MinimapSubmittedBatchKind::kVehicleIconTexture,
      viewMatrix,
      applyViewTransform);
}

void MinimapSystem::ClearSubmittedGeometry() noexcept {
  ResetSubmittedBuffer(submittedVertices_);
  ResetSubmittedBuffer(submittedTexCoords_);
  ResetSubmittedBuffer(submittedIndices_);
  submittedBatches_.clear();
}

std::uint32_t MinimapSystem::GetSubmittedVertexCount() const noexcept {
  return submittedVertices_.GetCount();
}

std::uint32_t MinimapSystem::GetSubmittedVertexCapacity() const noexcept {
  return submittedVertices_.GetCapacity();
}

const MinimapBatchVertex *
MinimapSystem::GetSubmittedVertex(const std::uint32_t index) const noexcept {
  return SubmittedBufferSlotAt(submittedVertices_, index);
}

std::uint32_t MinimapSystem::GetSubmittedTexCoordCount() const noexcept {
  return submittedTexCoords_.GetCount();
}

std::uint32_t MinimapSystem::GetSubmittedTexCoordCapacity() const noexcept {
  return submittedTexCoords_.GetCapacity();
}

const MinimapBatchTexCoord *
MinimapSystem::GetSubmittedTexCoord(const std::uint32_t index) const noexcept {
  return SubmittedBufferSlotAt(submittedTexCoords_, index);
}

std::uint32_t MinimapSystem::GetSubmittedIndexCount() const noexcept {
  return submittedIndices_.GetCount();
}

std::uint32_t MinimapSystem::GetSubmittedIndexCapacity() const noexcept {
  return submittedIndices_.GetCapacity();
}

const std::uint16_t *
MinimapSystem::GetSubmittedIndex(const std::uint32_t index) const noexcept {
  return SubmittedBufferSlotAt(submittedIndices_, index);
}

std::uint32_t MinimapSystem::GetSubmittedBatchCount() const noexcept {
  return static_cast<std::uint32_t>(submittedBatches_.size());
}

const MinimapSubmittedBatch *
MinimapSystem::GetSubmittedBatch(const std::uint32_t index) const noexcept {
  if (index >= submittedBatches_.size()) {
    return nullptr;
  }
  return &submittedBatches_[index];
}

void MinimapSystem::RenderMinimapContent() {
  ClearSubmittedGeometry();

  const float blipHalfExtent = blipHalfExtent_ > 0.0f
                                   ? blipHalfExtent_
                                   : directionalArrowLayout_.iconHalfExtent;
  const float vehicleStateBlipHalfExtent =
      vehicleStateBlipHalfExtent_ > 0.0f ? vehicleStateBlipHalfExtent_
                                         : blipHalfExtent;
  if (blipHalfExtent <= 0.0f) {
    return;
  }

  const bool applyViewTransform = rotating_;

  std::array<float, 16> viewMatrix{};
  const float *viewMatrixPtr = nullptr;
  if (applyViewTransform) {

    viewMatrix = BuildDirectionalViewMatrix(directionalArrowLayout_, playerFacing_);
    viewMatrixPtr = viewMatrix.data();
  }

  (void)SubmitPartyRaidBlipBatch(blipHalfExtent, viewMatrixPtr, applyViewTransform);
  (void)SubmitVehicleStateBlipBatch(vehicleStateBlipHalfExtent, viewMatrixPtr,
                                    applyViewTransform);
  (void)SubmitObjectBlipBatch(blipHalfExtent, viewMatrixPtr, applyViewTransform, false);
  (void)SubmitObjectBlipBatch(blipHalfExtent, viewMatrixPtr, applyViewTransform, true);
  (void)SubmitVehicleIconBatch(viewMatrixPtr, applyViewTransform);
}

}

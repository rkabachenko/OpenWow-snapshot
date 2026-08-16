
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "openwow/core/storm_containers.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/player_descriptor_callbacks.h"

namespace openwow::game {

enum class MinimapIconType : std::uint8_t {
  Player = 0,
  PartyMember,
  RaidMember,
  NPC,
  Vendor,
  FlightMaster,
  Innkeeper,
  Banker,
  Mailbox,
  Questgiver,
  TrackHerb,
  TrackMine,
  TrackFish,
};

struct MinimapIcon {
  MinimapIconType type{MinimapIconType::NPC};
  float worldX{0.0f};
  float worldY{0.0f};
  ObjectGuid guid;
  std::string tooltip;
  float rotation{0.0f};
};

[[nodiscard]] std::uint8_t ComputeMinimapFogAlpha(std::uint32_t packed_argb) noexcept;

[[nodiscard]] std::uint8_t ComputeMinimapFogBlendAlpha(std::uint32_t packed_argb) noexcept;

[[nodiscard]] std::uint32_t BuildMinimapTerrainTint(std::uint32_t packed_argb) noexcept;

struct MinimapObjectInfoSlot {
  std::uint32_t firstWord{0};
  std::uint32_t secondWord{0};
  std::array<float, 4> renderQuad{};
  std::uint8_t flags{0};
  std::uint8_t professionTrainerHighlight{0};
  std::uint8_t category13Overlay{0};
  std::uint8_t reserved27{0};
  std::uint32_t blipTextureCellPlusOne{0};
  float facingRadians{0.0f};
  std::array<std::byte, 4> reservedTail{};

  void SetGuid(const ObjectGuid &guid) noexcept {
    const std::uint64_t raw = guid.GetRawValue();
    firstWord = static_cast<std::uint32_t>(raw & 0xFFFFFFFFu);
    secondWord = static_cast<std::uint32_t>(raw >> 32);
  }

  [[nodiscard]] ObjectGuid GetGuid() const noexcept {
    return ObjectGuid((static_cast<std::uint64_t>(secondWord) << 32) | firstWord);
  }

  void SetProjectedCoords(float x, float y) noexcept {
    renderQuad[0] = x;
    renderQuad[1] = y;
    renderQuad[2] = x;
    renderQuad[3] = y;
  }

  [[nodiscard]] float GetProjectedX() const noexcept {
    return renderQuad[0];
  }

  [[nodiscard]] float GetProjectedY() const noexcept {
    return renderQuad[1];
  }

  void SetTransportLayerMismatch(bool value) noexcept {
    flags = value ? 1u : 0u;
  }

  [[nodiscard]] bool HasTransportLayerMismatch() const noexcept {
    return flags != 0;
  }

  void SetProfessionTrainerHighlight(bool value) noexcept {
    professionTrainerHighlight = value ? 1u : 0u;
  }

  [[nodiscard]] bool HasProfessionTrainerHighlight() const noexcept {
    return professionTrainerHighlight != 0;
  }

  void SetCategory13Overlay(bool value) noexcept {
    category13Overlay = value ? 1u : 0u;
  }

  [[nodiscard]] bool HasCategory13Overlay() const noexcept {
    return category13Overlay != 0;
  }

  void SetBlipTextureCellPlusOne(std::uint32_t value) noexcept {
    blipTextureCellPlusOne = value;
  }

  [[nodiscard]] std::uint32_t GetBlipTextureCellPlusOne() const noexcept {
    return blipTextureCellPlusOne;
  }

  void SetFacingRadians(float value) noexcept {
    facingRadians = value;
  }

  [[nodiscard]] float GetFacingRadians() const noexcept {
    return facingRadians;
  }

  void ClearRenderQuad() noexcept {
    renderQuad.fill(0.0f);
  }
};

static_assert(sizeof(MinimapObjectInfoSlot) == 40);

struct MinimapPoiInfoSlot {
  std::uint32_t header{0};
  std::array<float, 14> renderData{};
  std::array<std::byte, 4> tail{};

  void ClearRenderData() noexcept {
    renderData.fill(0.0f);
  }
};

static_assert(sizeof(MinimapPoiInfoSlot) == 64);

struct MinimapDirectionalArrowSlot {
  float lastSubmittedCenterX{0.0f};
  float lastSubmittedCenterY{0.0f};
  float angleRadians{0.0f};
  bool visible{false};
  std::array<std::byte, 3> reserved{};

  void SetProjectedCoords(const float x, const float y) noexcept {
    SetSubmittedCenter(x, y);
  }

  [[nodiscard]] float GetProjectedX() const noexcept {
    return lastSubmittedCenterX;
  }

  [[nodiscard]] float GetProjectedY() const noexcept {
    return lastSubmittedCenterY;
  }

  void SetSubmittedCenter(const float x, const float y) noexcept {
    lastSubmittedCenterX = x;
    lastSubmittedCenterY = y;
  }

  [[nodiscard]] float GetSubmittedCenterX() const noexcept {
    return lastSubmittedCenterX;
  }

  [[nodiscard]] float GetSubmittedCenterY() const noexcept {
    return lastSubmittedCenterY;
  }

  void SetAngleRadians(const float value) noexcept {
    angleRadians = value;
  }

  [[nodiscard]] float GetAngleRadians() const noexcept {
    return angleRadians;
  }

  void SetFacingRadians(const float value) noexcept {
    SetAngleRadians(value);
  }

  [[nodiscard]] float GetFacingRadians() const noexcept {
    return GetAngleRadians();
  }

  void SetVisible(const bool value) noexcept {
    visible = value;
  }

  [[nodiscard]] bool IsVisible() const noexcept {
    return visible;
  }

  void Reset() noexcept {
    lastSubmittedCenterX = 0.0f;
    lastSubmittedCenterY = 0.0f;
    angleRadians = 0.0f;
    visible = false;
    reserved.fill(std::byte{0});
  }
};

struct MinimapDirectionalArrowLayout {
  float centerX{0.0f};
  float centerY{0.0f};
  float ringRadius{0.0f};
  float iconHalfExtent{0.0f};
};

enum class MinimapTrackedInfoType : std::uint32_t {
  kNpcFlags = 1,
  kGameObjectType = 2,
  kTrivialQuests = 3,
};

struct MinimapTrackedInfoSelection {
  MinimapTrackedInfoType type{MinimapTrackedInfoType::kNpcFlags};
  std::uint32_t value{0};
};

struct MinimapTransportKey {
  std::uint32_t low{0};
  std::uint32_t high{0};

  [[nodiscard]] bool IsEmpty() const noexcept {
    return low == 0 && high == 0;
  }
};

enum class MinimapVisibleObjectType : std::uint32_t {
  kUnit = 9,
  kPlayer = 25,
  kGameObject = 33,
};

struct MinimapVisibleObjectContext {
  ObjectGuid activePlayerGuid;
  float centerX{0.0f};
  float centerY{0.0f};
  float visibleRange{0.0f};
  MinimapTransportKey viewerTransport{};
  bool showTrivialQuestMarkers{false};
  std::optional<MinimapTrackedInfoSelection> trackedInfo;
};

struct MinimapVisibleObjectSnapshot {
  ObjectGuid guid;
  MinimapVisibleObjectType objectType{MinimapVisibleObjectType::kUnit};
  float worldX{0.0f};
  float worldY{0.0f};
  float minimapX{0.0f};
  float minimapY{0.0f};
  MinimapTransportKey transport{};
  std::uint32_t questMarkerState{0};
  bool hasCategory13Overlay{false};
  bool isActivePlayerOrPartyOrRaidMember{false};
  ObjectGuid controllingGuid;
  bool passesInteractionGate{false};

  std::uint32_t npcTrackingFlags{0};

  std::uint32_t gameObjectTypeId{0};

  bool passesTrackedInfoTargetFilter{false};

  bool passesGameObjectMinimapFilter{false};

  MinimapTrackInfo minimapTrackInfo{};
  bool isPlayerControlledTarget{false};
  bool canAttackSpellTarget{false};
  std::int32_t pvpRankFactionSelector{0};
  std::int32_t activePlayerPvpRankFactionSelector{0};
  std::int32_t reactionLevel{3};
};

template <typename Slot, std::uint32_t LockedGrowthQuantum = 8>
class MinimapRenderBuffer {
public:
  void SetCapacity(std::uint32_t newCapacity) {
    if (newCapacity == slots_.size()) {
      return;
    }

    const std::uint32_t logicalCount = count_;
    std::vector<Slot> resized(newCapacity);
    const std::size_t copied = std::min<std::size_t>(
        {count_, slots_.size(), static_cast<std::size_t>(newCapacity)});
    std::copy_n(slots_.begin(), copied, resized.begin());
    slots_.swap(resized);

    count_ = logicalCount;
  }

  [[nodiscard]] std::uint32_t Capacity() const noexcept {
    return static_cast<std::uint32_t>(slots_.size());
  }

  [[nodiscard]] std::uint32_t Count() const noexcept {
    return count_;
  }

  void SetCount(std::uint32_t newCount) noexcept {
    count_ = newCount;
  }

  [[nodiscard]] Slot *SlotAt(std::uint32_t index) noexcept {
    if (index >= slots_.size()) {
      return nullptr;
    }
    return &slots_[index];
  }

  [[nodiscard]] const Slot *SlotAt(std::uint32_t index) const noexcept {
    if (index >= slots_.size()) {
      return nullptr;
    }
    return &slots_[index];
  }

  void Clear() noexcept {
    slots_.clear();
    count_ = 0;
    growthQuantum_ = 0;
  }

  template <typename SlotInitializer> Slot *AllocateSlot(SlotInitializer &&initializer) {
    const std::uint32_t index = count_;
    EnsureCapacityForCount(index + 1);
    if (index >= slots_.size()) {
      return nullptr;
    }

    Slot *const slot = &slots_[index];
    ++count_;
    initializer(*slot);
    return slot;
  }

  [[nodiscard]] std::uint32_t Append(std::uint32_t appendedCount,
                                     const Slot *source) {
    const std::uint32_t startIndex = count_;
    if (appendedCount == 0) {
      return startIndex;
    }

    EnsureCapacityForCount(startIndex + appendedCount);
    if (startIndex + appendedCount > slots_.size()) {
      return startIndex;
    }

    std::copy_n(source, appendedCount, slots_.begin() + startIndex);
    count_ = startIndex + appendedCount;
    return startIndex;
  }

  template <typename SlotInitializer>
  void ResizeCount(std::uint32_t newCount, SlotInitializer &&initializer) {
    if (newCount <= count_) {
      count_ = newCount;
      return;
    }

    EnsureCapacityForCount(newCount);
    const std::uint32_t initializedCount =
        std::min<std::uint32_t>(newCount, static_cast<std::uint32_t>(slots_.size()));
    for (std::uint32_t index = count_; index < initializedCount; ++index) {
      initializer(slots_[index]);
    }
    count_ = newCount;
  }

private:
  [[nodiscard]] static std::uint32_t ResolveGrowthQuantum(std::uint32_t requestedCount) noexcept {
    if (requestedCount >= LockedGrowthQuantum) {
      return LockedGrowthQuantum;
    }

    std::uint32_t result = requestedCount;
    for (std::uint32_t candidate = requestedCount & (requestedCount - 1); candidate != 0;
         candidate &= (candidate - 1)) {
      result = candidate;
    }
    return std::max<std::uint32_t>(result, 1);
  }

  void EnsureCapacityForCount(std::uint32_t requestedCount) {
    if (requestedCount <= slots_.size()) {
      return;
    }

    std::uint32_t quantum = growthQuantum_;
    if (quantum == 0) {
      quantum = ResolveGrowthQuantum(requestedCount);
      if (requestedCount >= LockedGrowthQuantum) {
        growthQuantum_ = quantum;
      }
    }

    std::uint32_t newCapacity = requestedCount;
    const std::uint32_t remainder = requestedCount % quantum;
    if (remainder != 0) {
      newCapacity += quantum - remainder;
    }
    SetCapacity(newCapacity);
  }

  std::vector<Slot> slots_;
  std::uint32_t count_{0};
  std::uint32_t growthQuantum_{0};
};

struct MinimapBatchVertex {
  float x{0.0f};
  float y{0.0f};
  float z{0.0f};
};

struct MinimapBatchTexCoord {
  float u{0.0f};
  float v{0.0f};
};

static_assert(sizeof(MinimapBatchVertex) == 12);
static_assert(sizeof(MinimapBatchTexCoord) == 8);

enum class MinimapSubmittedBatchKind : std::uint8_t {
  kPartyRaidAtlas = 0,
  kObjectAtlas = 1,
  kVehicleStateTexture = 2,
  kVehicleIconTexture = 3,
};

struct MinimapSubmittedBatch {
  MinimapSubmittedBatchKind kind{MinimapSubmittedBatchKind::kPartyRaidAtlas};
  std::uint32_t sourceCategoryIndex{0};
  std::uint32_t vertexStart{0};
  std::uint32_t vertexCount{0};
  std::uint32_t texCoordStart{0};
  std::uint32_t texCoordCount{0};
  std::uint32_t indexStart{0};
  std::uint32_t indexCount{0};
};

class MinimapSystem {
public:
  MinimapSystem() = default;

  void SetPlayerPosition(float x, float y, float facing);

  struct PlayerPosition {
    float x{0.0f};
    float y{0.0f};
    float facing{0.0f};
  };
  [[nodiscard]] PlayerPosition GetPlayerPosition() const;

  void SetZoom(float level);
  [[nodiscard]] float GetZoom() const;

  static constexpr float kMinZoom = 0.0f;
  static constexpr float kMaxZoom = 5.0f;
  static constexpr float kDefaultZoom = 3.0f;
  static constexpr float kZoomStep = 1.0f;

  void AddIcon(const MinimapIcon &icon);
  void RemoveIcon(ObjectGuid guid);

  [[nodiscard]] std::vector<MinimapIcon> GetVisibleIcons() const;
  [[nodiscard]] std::size_t GetIconCount() const;

  void SetTrackingEnabled(MinimapIconType type, bool enabled);
  [[nodiscard]] bool IsTrackingEnabled(MinimapIconType type) const;

  [[nodiscard]] float GetMinimapRadius() const;

  static constexpr float kMinRadius = 33.333332f;
  static constexpr float kMaxRadius = 233.333328f;
  static constexpr std::uint32_t kObjectInfoCategoryCount = 23;
  static constexpr std::uint32_t kVehicleIconSlotCount = 5;

  struct MinimapCoord {
    float mx{0.0f};
    float my{0.0f};
  };

  [[nodiscard]] MinimapCoord WorldToMinimap(float wx, float wy) const;

  void SetRotating(bool rotating);
  [[nodiscard]] bool IsRotating() const;

  void Reset();

  void UpdateDirection(float dt);

  void SetObjectInfoCategoryCapacity(std::uint32_t categoryIndex, std::uint32_t newCapacity);
  [[nodiscard]] std::uint32_t
  GetObjectInfoCategoryCapacity(std::uint32_t categoryIndex) const noexcept;

  void SetObjectInfoCategoryCount(std::uint32_t categoryIndex, std::uint32_t newCount);
  [[nodiscard]] std::uint32_t
  GetObjectInfoCategoryCount(std::uint32_t categoryIndex) const noexcept;
  [[nodiscard]] MinimapObjectInfoSlot *GetObjectInfoCategorySlot(std::uint32_t categoryIndex,
                                                                 std::uint32_t index) noexcept;
  [[nodiscard]] const MinimapObjectInfoSlot *
  GetObjectInfoCategorySlot(std::uint32_t categoryIndex, std::uint32_t index) const noexcept;

  [[nodiscard]] MinimapObjectInfoSlot *AllocateObjectInfoSlot(std::uint32_t categoryIndex) noexcept;

  void ClearObjectInfoCategories() noexcept;

  void ClearDirectionalIconSlots() noexcept;

  [[nodiscard]] bool AppendVisibleObjectBlip(const MinimapVisibleObjectContext &context,
                                             const MinimapVisibleObjectSnapshot &object);

  void ResizeBlipData(std::uint32_t newCount);

  void SetBlipCapacity(std::uint32_t newCount) { ResizeBlipData(newCount); }

  [[nodiscard]] std::uint32_t SubmitVertices(std::uint32_t vertexCount,
                                             const MinimapBatchVertex *vertices);

  [[nodiscard]] std::uint32_t SubmitTexCoords(std::uint32_t texCoordCount,
                                              const MinimapBatchTexCoord *texCoords);

  void SetTexCoordCount(std::uint32_t newCount);

  [[nodiscard]] std::uint32_t SubmitIndices(std::uint32_t indexCount,
                                            const std::uint16_t *indices);

  [[nodiscard]] bool SubmitPartyRaidBlipBatch(float blipHalfExtent,
                                              const float *viewMatrix,
                                              bool applyViewTransform);

  [[nodiscard]] bool SubmitObjectBlipBatch(float blipHalfExtent,
                                           const float *viewMatrix,
                                           bool applyViewTransform,
                                           bool transportLayerMismatch);

  [[nodiscard]] bool SubmitVehicleStateBlipBatch(float blipHalfExtent,
                                                 const float *viewMatrix,
                                                 bool applyViewTransform);

  [[nodiscard]] bool SubmitVehicleIconBatch(const float *viewMatrix,
                                            bool applyViewTransform);

  void SetDirectionalArrowLayout(float centerX,
                                 float centerY,
                                 float ringRadius,
                                 float iconHalfExtent) noexcept;
  [[nodiscard]] MinimapDirectionalArrowLayout GetDirectionalArrowLayout() const noexcept;

  void SetBlipHalfExtents(float blipHalfExtent,
                          float vehicleStateBlipHalfExtent) noexcept;

  [[nodiscard]] MinimapDirectionalArrowSlot *GetVehicleIconSlot(std::uint32_t index) noexcept;
  [[nodiscard]] const MinimapDirectionalArrowSlot *
  GetVehicleIconSlot(std::uint32_t index) const noexcept;

  void ClearSubmittedGeometry() noexcept;

  [[nodiscard]] std::uint32_t GetSubmittedVertexCount() const noexcept;
  [[nodiscard]] std::uint32_t GetSubmittedVertexCapacity() const noexcept;
  [[nodiscard]] const MinimapBatchVertex *
  GetSubmittedVertex(std::uint32_t index) const noexcept;

  [[nodiscard]] std::uint32_t GetSubmittedTexCoordCount() const noexcept;
  [[nodiscard]] std::uint32_t GetSubmittedTexCoordCapacity() const noexcept;
  [[nodiscard]] const MinimapBatchTexCoord *
  GetSubmittedTexCoord(std::uint32_t index) const noexcept;

  [[nodiscard]] std::uint32_t GetSubmittedIndexCount() const noexcept;
  [[nodiscard]] std::uint32_t GetSubmittedIndexCapacity() const noexcept;
  [[nodiscard]] const std::uint16_t *GetSubmittedIndex(std::uint32_t index) const noexcept;

  [[nodiscard]] std::uint32_t GetSubmittedBatchCount() const noexcept;
  [[nodiscard]] const MinimapSubmittedBatch *GetSubmittedBatch(std::uint32_t index) const noexcept;

  void RenderMinimapContent();

private:
  float playerX_{0.0f};
  float playerY_{0.0f};
  float playerFacing_{0.0f};
  float zoom_{kDefaultZoom};
  bool rotating_{true};

  std::unordered_map<std::uint64_t, MinimapIcon> icons_;

  std::set<MinimapIconType> enabledTracking_;

  [[nodiscard]] MinimapRenderBuffer<MinimapObjectInfoSlot> *
  ObjectInfoCategoryBuffer(std::uint32_t categoryIndex) noexcept;
  [[nodiscard]] const MinimapRenderBuffer<MinimapObjectInfoSlot> *
  ObjectInfoCategoryBuffer(std::uint32_t categoryIndex) const noexcept;

  [[nodiscard]] bool SubmitDirectionalArrowBatch(
      std::array<MinimapDirectionalArrowSlot, kVehicleIconSlotCount> &slots,
      MinimapSubmittedBatchKind batchKind,
      const float *viewMatrix,
      bool applyViewTransform);
  [[nodiscard]] bool SubmitDirectionalArrowBatchImpl(
      MinimapDirectionalArrowSlot *slots,
      std::uint32_t slotCount,
      MinimapSubmittedBatchKind batchKind,
      const float *viewMatrix,
      bool applyViewTransform);

  std::array<MinimapRenderBuffer<MinimapObjectInfoSlot>, kObjectInfoCategoryCount>
      objectInfoCategories_;
  MinimapDirectionalArrowLayout directionalArrowLayout_{};
  float blipHalfExtent_{0.0f};
  float vehicleStateBlipHalfExtent_{0.0f};
  std::array<MinimapDirectionalArrowSlot, kVehicleIconSlotCount> vehicleIconSlots_{};
  MinimapRenderBuffer<MinimapPoiInfoSlot> poiInfo_;
  openwow::core::TSGrowableArray<MinimapBatchVertex, 21> submittedVertices_;
  openwow::core::TSGrowableArray<MinimapBatchTexCoord, 32> submittedTexCoords_;
  openwow::core::TSGrowableArray<std::uint16_t, 128> submittedIndices_;
  std::vector<MinimapSubmittedBatch> submittedBatches_;
};

struct MinimapPixelCoord {
  float x{0.0f};
  float y{0.0f};
};

inline MinimapPixelCoord WorldToMinimapCoords(
    float centerX, float centerY,
    float range,
    float targetX, float targetY,
    float minimapDim, float scale) {
  const float halfSize = minimapDim * scale * 0.5f;
  const float invRange = 1.0f / range;
  return {
      halfSize - (targetY - centerY) * halfSize * invRange,
      halfSize + (targetX - centerX) * halfSize * invRange,
  };
}

}

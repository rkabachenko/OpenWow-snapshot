
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openwow::game {

struct QuestPOIPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct QuestPOIEntry {
    uint32_t questId = 0;
    int32_t objectiveIndex = -1;
    uint32_t mapId = 0;
    uint32_t areaId = 0;
    uint32_t floorId = 0;
    uint32_t flags = 0;
    std::vector<QuestPOIPoint> points;
    uint32_t priority = 0;
};

struct QuestPOIBoundingBox {
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
};

struct QuestPOIRenderVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 1.0f;
};

struct QuestPOIRenderTexCoord {
    float u = 0.0f;
    float v = 0.0f;
};

struct QuestPOIRenderData {
    std::vector<QuestPOIPoint> fillPoints;
    std::vector<QuestPOIPoint> borderStripPoints;
    std::vector<QuestPOIRenderVertex> fillVertices;
    std::vector<QuestPOIRenderTexCoord> fillTexCoords;
    std::vector<std::uint16_t> fillIndices;
    std::vector<QuestPOIRenderVertex> borderVertices;
    std::vector<QuestPOIRenderTexCoord> borderTexCoords;
    std::vector<std::uint16_t> borderIndices;
    QuestPOIBoundingBox fillBounds;
    QuestPOIBoundingBox borderBounds;
    bool active = false;
};

struct QuestPOIWorldMapIconLayout {
    bool turnInReady = false;
    std::int32_t objectiveIndex = 0;
    std::uint32_t mapId = 0;
    std::int32_t worldX = 0;
    std::int32_t worldY = 0;
};

std::vector<QuestPOIPoint> SampleQuestPOISpline(
    const std::vector<QuestPOIPoint>& controlPoints, int sampleCount);

QuestPOIRenderData BuildQuestPOIRenderDataFromMapPoints(
    const std::vector<QuestPOIPoint>& mapPoints, float borderScalar);

class QuestPOIData {
public:
    static constexpr std::size_t kMaxQuerySlots = 25;
    static constexpr float kDefaultIconOverlapDistance = 12.0f;
    static constexpr float kDefaultIconOverlapPushDistance = 18.0f;

    static QuestPOIData& Get();

    void AddPOI(const QuestPOIEntry& poi);
    void ReplaceQuestQueryResult(uint32_t questId, std::vector<QuestPOIEntry> pois,
                                 const std::vector<uint32_t>& activeQuestIds);

    std::vector<QuestPOIEntry> GetPOIsForQuest(uint32_t questId) const;
    std::vector<QuestPOIEntry> GetPOIsForMap(uint32_t mapId) const;
    std::vector<QuestPOIEntry> GetPOIsForObjective(uint32_t questId,
                                                    int32_t objectiveIndex) const;

    bool HasPOI(uint32_t questId) const;
    [[nodiscard]] std::optional<std::size_t> FindQuerySlotIndex(uint32_t questId) const;
    [[nodiscard]] bool HasQuestQueryResult(uint32_t questId) const;
    [[nodiscard]] uint32_t GetQuestIdByQuerySlot(std::size_t oneBasedIndex) const;
    void SetVisibleWorldMapQuestIds(std::vector<uint32_t> questIds);
    [[nodiscard]] uint32_t GetQuestIdByVisibleWorldMapIndex(std::size_t oneBasedIndex) const;
    void SetWorldMapIconLayout(uint32_t questId, const QuestPOIWorldMapIconLayout& layout);
    [[nodiscard]] std::optional<QuestPOIWorldMapIconLayout> GetWorldMapIconLayout(
        uint32_t questId) const;
    void ReplaceWorldMapIconLayouts(
        std::unordered_map<uint32_t, QuestPOIWorldMapIconLayout> layouts);
    void ClearWorldMapIconLayout(uint32_t questId);

    std::vector<QuestPOIEntry> GetAllPOIs() const;
    uint32_t GetPOICount() const;

    void RemovePOI(uint32_t questId);
    void SetIconOverlapDistance(float distance);
    void SetIconOverlapPushDistance(float distance);
    [[nodiscard]] float GetIconOverlapDistance() const;
    [[nodiscard]] float GetIconOverlapPushDistance() const;

    static QuestPOIPoint GetCentroid(const QuestPOIEntry& poi);

    static bool ContainsPoint(const QuestPOIEntry& poi, float x, float y);

    static QuestPOIBoundingBox GetBoundingBox(const QuestPOIEntry& poi);

    static float GetArea(const QuestPOIEntry& poi);

    static float GetPerimeter(const QuestPOIEntry& poi);

    static float DistanceToEdge(const QuestPOIEntry& poi, float x, float y);

    void SetActiveQuest(uint32_t questId, bool active);
    [[nodiscard]] bool IsQuestActive(uint32_t questId) const;
    [[nodiscard]] std::vector<QuestPOIEntry> GetActivePOIs() const;
    [[nodiscard]] std::vector<QuestPOIEntry> GetActivePOIsForMap(uint32_t mapId) const;
    void ClearActiveQuests();

    void Clear();

private:
    QuestPOIData() = default;

    mutable std::mutex mutex_;

    std::unordered_map<uint32_t, std::vector<QuestPOIEntry>> pois_;
    std::unordered_set<uint32_t> activeQuests_;
    std::array<uint32_t, kMaxQuerySlots> querySlots_ = {};
    std::vector<uint32_t> visibleWorldMapQuestIds_;
    std::unordered_map<uint32_t, QuestPOIWorldMapIconLayout> worldMapIconLayouts_;
    float iconOverlapDistance_ = kDefaultIconOverlapDistance;
    float iconOverlapPushDistance_ = kDefaultIconOverlapPushDistance;
};

}

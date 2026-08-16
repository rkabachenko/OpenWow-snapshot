
#include "openwow/game/quest_poi.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include "openwow/foundation/math/float_compare.h"
#include "openwow/foundation/math/vec3_normalize_if_length_squared_exceeds_client_epsilon.h"
#include "openwow/render/models/animation/spline.h"

namespace openwow::game {

namespace {

constexpr float kQuestPoiSplineStartT = 0.0099999998f;
constexpr float kQuestPoiBorderScaleFactor = 0.0099999998f;
constexpr float kQuestPoiMapCoordProductThreshold = 0.001f;

bool IsQuestInActiveLog(const std::vector<uint32_t>& activeQuestIds, const uint32_t questId) {
    return questId != 0 &&
           std::find(activeQuestIds.begin(), activeQuestIds.end(), questId) != activeQuestIds.end();
}

std::optional<std::size_t> FindQuerySlotIndexInTable(
    const std::array<uint32_t, QuestPOIData::kMaxQuerySlots>& querySlots, const uint32_t questId) {
    if (questId == 0) {
        return std::nullopt;
    }

    const auto it = std::find(querySlots.begin(), querySlots.end(), questId);
    if (it == querySlots.end()) {
        return std::nullopt;
    }

    return static_cast<std::size_t>(std::distance(querySlots.begin(), it));
}

std::vector<openwow::render::C3Vector> BuildQuestPoiSplineControlPoints(
    const std::vector<QuestPOIPoint>& controlPoints) {
    std::vector<openwow::render::C3Vector> splinePoints;
    if (controlPoints.empty()) {
        return splinePoints;
    }

    splinePoints.reserve(controlPoints.size() + 3u);
    const auto makePoint = [](const QuestPOIPoint& point) {
        return openwow::render::C3Vector{point.x, point.y, 0.0f};
    };

    splinePoints.push_back(makePoint(controlPoints.back()));
    for (const auto& point : controlPoints) {
        splinePoints.push_back(makePoint(point));
    }
    splinePoints.push_back(makePoint(controlPoints.front()));
    splinePoints.push_back(makePoint(controlPoints[1]));
    return splinePoints;
}

float RoundQuestPoiSplineComponent(const float value) {
    return std::nearbyintf(value);
}

QuestPOIBoundingBox ComputeBoundingBoxFromPoints(const std::vector<QuestPOIPoint>& points) {
    QuestPOIBoundingBox bounds;
    if (points.empty()) {
        return bounds;
    }

    bounds.minX = bounds.maxX = points.front().x;
    bounds.minY = bounds.maxY = points.front().y;
    for (std::size_t index = 1; index < points.size(); ++index) {
        bounds.minX = std::min(bounds.minX, points[index].x);
        bounds.maxX = std::max(bounds.maxX, points[index].x);
        bounds.minY = std::min(bounds.minY, points[index].y);
        bounds.maxY = std::max(bounds.maxY, points[index].y);
    }

    return bounds;
}

QuestPOIPoint ComputeAveragePoint(const std::vector<QuestPOIPoint>& points) {
    if (points.empty()) {
        return {};
    }

    float sumX = 0.0f;
    float sumY = 0.0f;
    for (const auto& point : points) {
        sumX += point.x;
        sumY += point.y;
    }

    const float pointCount = static_cast<float>(points.size());
    return {sumX / pointCount, sumY / pointCount};
}

std::optional<QuestPOIPoint> IntersectQuestPoiOffsetLines2D(
    const QuestPOIPoint& lhsStart,
    const QuestPOIPoint& lhsEnd,
    const QuestPOIPoint& rhsStart,
    const QuestPOIPoint& rhsEnd) {
    const float lhsDeltaX = lhsStart.x - lhsEnd.x;
    const float rhsDeltaX = rhsStart.x - rhsEnd.x;
    const float determinant =
        (rhsStart.y - rhsEnd.y) * lhsDeltaX - (lhsStart.y - lhsEnd.y) * rhsDeltaX;
    if (std::fabs(determinant) < openwow::math::float_compare::kClientFloatEpsilon) {
        return std::nullopt;
    }

    const float lhsCross = lhsStart.x * lhsEnd.y - lhsEnd.x * lhsStart.y;
    const float rhsCross = rhsStart.x * rhsEnd.y - rhsEnd.x * rhsStart.y;
    const float inverseDeterminant = 1.0f / determinant;
    return QuestPOIPoint{
        (rhsDeltaX * lhsCross - lhsDeltaX * rhsCross) * inverseDeterminant,
        ((rhsStart.y - rhsEnd.y) * lhsCross - (lhsStart.y - lhsEnd.y) * rhsCross) *
            inverseDeterminant,
    };
}

}

QuestPOIData& QuestPOIData::Get() {
    static QuestPOIData instance;
    return instance;
}

std::vector<QuestPOIPoint> SampleQuestPOISpline(
    const std::vector<QuestPOIPoint>& controlPoints, const int sampleCount) {
    if (controlPoints.size() < 3u || sampleCount <= 0) {
        return controlPoints;
    }

    const auto splineControlPoints = BuildQuestPoiSplineControlPoints(controlPoints);
    openwow::render::CSpline spline{openwow::render::CSpline::CurveType::kCatmullRom};
    spline.SetControlPoints(splineControlPoints.data(),
                            static_cast<std::uint32_t>(splineControlPoints.size()));

    std::vector<QuestPOIPoint> sampledPoints;
    sampledPoints.reserve(static_cast<std::size_t>(sampleCount));
    const float step = 1.0f / static_cast<float>(sampleCount);
    float parameter = kQuestPoiSplineStartT;
    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex, parameter += step) {
        const auto sample =
            spline.Evaluate(parameter, openwow::render::CSpline::kArcLengthParameterMode);
        sampledPoints.push_back(
            {RoundQuestPoiSplineComponent(sample.x), RoundQuestPoiSplineComponent(sample.y)});
    }

    return sampledPoints;
}

QuestPOIRenderData BuildQuestPOIRenderDataFromMapPoints(
    const std::vector<QuestPOIPoint>& mapPoints, const float borderScalar) {
    QuestPOIRenderData renderData;
    if (mapPoints.size() < 3) {
        return renderData;
    }

    for (const auto& point : mapPoints) {
        if (std::fabs(point.x * point.y) < kQuestPoiMapCoordProductThreshold) {
            return renderData;
        }
    }

    renderData.fillPoints = mapPoints;
    renderData.fillBounds = ComputeBoundingBoxFromPoints(renderData.fillPoints);

    const auto center = ComputeAveragePoint(renderData.fillPoints);
    const std::size_t pointCount = renderData.fillPoints.size();
    renderData.fillVertices.reserve(pointCount + 1);
    renderData.fillTexCoords.reserve(pointCount + 1);
    renderData.fillIndices.reserve(pointCount * 3);
    for (std::size_t index = 0; index < pointCount; ++index) {
        const auto& point = renderData.fillPoints[index];
        renderData.fillVertices.push_back({point.x, point.y, 1.0f});
        renderData.fillTexCoords.push_back({1.0f, 1.0f});
        renderData.fillIndices.push_back(static_cast<std::uint16_t>(index));
        renderData.fillIndices.push_back(
            static_cast<std::uint16_t>((index + 1) % pointCount));
        renderData.fillIndices.push_back(static_cast<std::uint16_t>(pointCount));
    }
    renderData.fillVertices.push_back({center.x, center.y, 1.0f});
    renderData.fillTexCoords.push_back({});

    const float borderOffsetScale = borderScalar * kQuestPoiBorderScaleFactor;
    std::vector<QuestPOIPoint> offsetStarts(pointCount);
    std::vector<QuestPOIPoint> offsetEnds(pointCount);
    renderData.borderStripPoints.resize(pointCount * 2);
    for (std::size_t index = 0; index < pointCount; ++index) {
        const auto& current = renderData.fillPoints[index];
        const auto& next = renderData.fillPoints[(index + 1) % pointCount];

        float outwardNormal[3] = {
            -(next.y - current.y),
            next.x - current.x,
            0.0f,
        };
        openwow::math::vec3::NormalizeInPlaceIfLengthSquaredExceedsClientEpsilon(outwardNormal);
        outwardNormal[0] *= borderOffsetScale;
        outwardNormal[1] *= borderOffsetScale;

        if ((center.x - current.x) * outwardNormal[0] + (center.y - current.y) * outwardNormal[1] >
            0.0f) {
            outwardNormal[0] *= -1.0f;
            outwardNormal[1] *= -1.0f;
        }

        offsetStarts[index] = {current.x + outwardNormal[0], current.y + outwardNormal[1]};
        offsetEnds[index] = {next.x + outwardNormal[0], next.y + outwardNormal[1]};
        renderData.borderStripPoints[index * 2] = current;
    }

    for (std::size_t index = 0; index < pointCount; ++index) {
        const std::size_t nextIndex = (index + 1) % pointCount;
        const auto joinedPoint =
            IntersectQuestPoiOffsetLines2D(offsetStarts[index], offsetEnds[index],
                                           offsetStarts[nextIndex], offsetEnds[nextIndex]);
        renderData.borderStripPoints[index * 2 + 1] = joinedPoint.value_or(offsetStarts[index]);
    }

    renderData.borderVertices.reserve(pointCount * 2);
    renderData.borderTexCoords.reserve(pointCount * 2);
    renderData.borderIndices.reserve(pointCount * 2 + 2);
    for (std::size_t index = 0; index < pointCount; ++index) {
        const auto& inner = renderData.borderStripPoints[index * 2];
        const auto& outer = renderData.borderStripPoints[index * 2 + 1];
        renderData.borderVertices.push_back({inner.x, inner.y, 1.0f});
        renderData.borderVertices.push_back({outer.x, outer.y, 1.0f});
        renderData.borderTexCoords.push_back({});
        renderData.borderTexCoords.push_back({1.0f, 1.0f});
        renderData.borderIndices.push_back(static_cast<std::uint16_t>(index * 2));
        renderData.borderIndices.push_back(
            static_cast<std::uint16_t>(index * 2 + 1));
    }
    renderData.borderIndices.push_back(0);
    renderData.borderIndices.push_back(1);

    renderData.borderBounds = ComputeBoundingBoxFromPoints(renderData.borderStripPoints);
    renderData.active = true;
    return renderData;
}

void QuestPOIData::AddPOI(const QuestPOIEntry& poi) {
    std::lock_guard lock(mutex_);
    pois_[poi.questId].push_back(poi);
}

void QuestPOIData::ReplaceQuestQueryResult(const uint32_t questId,
                                           std::vector<QuestPOIEntry> pois,
                                           const std::vector<uint32_t>& activeQuestIds) {
    if (questId == 0) {
        return;
    }

    std::lock_guard lock(mutex_);
    const bool questAlreadyPresent =
        std::find(querySlots_.begin(), querySlots_.end(), questId) != querySlots_.end();
    std::size_t slotIndex = querySlots_.size();
    for (std::size_t index = 0; index < querySlots_.size(); ++index) {
        const auto slotQuestId = querySlots_[index];
        if (slotQuestId == questId) {
            break;
        }

        if (!IsQuestInActiveLog(activeQuestIds, slotQuestId)) {
            if (slotQuestId != 0) {
                pois_.erase(slotQuestId);
                activeQuests_.erase(slotQuestId);
                worldMapIconLayouts_.erase(slotQuestId);
            }
            querySlots_[index] = 0;
            slotIndex = index;
            break;
        }
    }

    if (!questAlreadyPresent) {
        if (slotIndex == querySlots_.size()) {
            return;
        }
        querySlots_[slotIndex] = questId;
    }

    if (pois.empty()) {
        pois_.erase(questId);
        worldMapIconLayouts_.erase(questId);
    } else {
        pois_[questId] = std::move(pois);
    }
}

std::vector<QuestPOIEntry> QuestPOIData::GetPOIsForQuest(
    uint32_t questId) const {
    std::lock_guard lock(mutex_);
    auto it = pois_.find(questId);
    if (it == pois_.end()) return {};
    return it->second;
}

std::vector<QuestPOIEntry> QuestPOIData::GetPOIsForMap(uint32_t mapId) const {
    std::lock_guard lock(mutex_);
    std::vector<QuestPOIEntry> result;
    for (auto& [_, entries] : pois_) {
        for (auto& e : entries) {
            if (e.mapId == mapId) result.push_back(e);
        }
    }
    return result;
}

std::vector<QuestPOIEntry> QuestPOIData::GetPOIsForObjective(
    uint32_t questId, int32_t objectiveIndex) const {
    std::lock_guard lock(mutex_);
    std::vector<QuestPOIEntry> result;
    auto it = pois_.find(questId);
    if (it == pois_.end()) return result;
    for (auto& e : it->second) {
        if (e.objectiveIndex == objectiveIndex) result.push_back(e);
    }
    return result;
}

bool QuestPOIData::HasPOI(uint32_t questId) const {
    std::lock_guard lock(mutex_);
    return pois_.count(questId) > 0;
}

std::optional<std::size_t> QuestPOIData::FindQuerySlotIndex(const uint32_t questId) const {
    std::lock_guard lock(mutex_);
    return FindQuerySlotIndexInTable(querySlots_, questId);
}

bool QuestPOIData::HasQuestQueryResult(const uint32_t questId) const {
    std::lock_guard lock(mutex_);
    return FindQuerySlotIndexInTable(querySlots_, questId).has_value();
}

uint32_t QuestPOIData::GetQuestIdByQuerySlot(const std::size_t oneBasedIndex) const {
    std::lock_guard lock(mutex_);
    if (oneBasedIndex == 0 || oneBasedIndex > querySlots_.size()) {
        return 0;
    }
    return querySlots_[oneBasedIndex - 1];
}

void QuestPOIData::SetVisibleWorldMapQuestIds(std::vector<uint32_t> questIds) {
    std::lock_guard lock(mutex_);
    visibleWorldMapQuestIds_ = std::move(questIds);
}

uint32_t QuestPOIData::GetQuestIdByVisibleWorldMapIndex(const std::size_t oneBasedIndex) const {
    std::lock_guard lock(mutex_);
    if (oneBasedIndex == 0 || oneBasedIndex > visibleWorldMapQuestIds_.size()) {
        return 0;
    }
    return visibleWorldMapQuestIds_[oneBasedIndex - 1];
}

void QuestPOIData::SetWorldMapIconLayout(const uint32_t questId,
                                         const QuestPOIWorldMapIconLayout& layout) {
    if (questId == 0) {
        return;
    }

    std::lock_guard lock(mutex_);
    worldMapIconLayouts_[questId] = layout;
}

std::optional<QuestPOIWorldMapIconLayout> QuestPOIData::GetWorldMapIconLayout(
    const uint32_t questId) const {
    std::lock_guard lock(mutex_);
    const auto it = worldMapIconLayouts_.find(questId);
    if (it == worldMapIconLayouts_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void QuestPOIData::ReplaceWorldMapIconLayouts(
    std::unordered_map<uint32_t, QuestPOIWorldMapIconLayout> layouts) {
    std::lock_guard lock(mutex_);
    worldMapIconLayouts_ = std::move(layouts);
}

void QuestPOIData::ClearWorldMapIconLayout(const uint32_t questId) {
    if (questId == 0) {
        return;
    }

    std::lock_guard lock(mutex_);
    worldMapIconLayouts_.erase(questId);
}

std::vector<QuestPOIEntry> QuestPOIData::GetAllPOIs() const {
    std::lock_guard lock(mutex_);
    std::vector<QuestPOIEntry> result;
    for (auto& [_, entries] : pois_) {
        result.insert(result.end(), entries.begin(), entries.end());
    }
    return result;
}

uint32_t QuestPOIData::GetPOICount() const {
    std::lock_guard lock(mutex_);
    uint32_t count = 0;
    for (auto& [_, entries] : pois_) {
        count += static_cast<uint32_t>(entries.size());
    }
    return count;
}

void QuestPOIData::RemovePOI(uint32_t questId) {
    std::lock_guard lock(mutex_);
    pois_.erase(questId);
    activeQuests_.erase(questId);
    worldMapIconLayouts_.erase(questId);
    for (auto& slotQuestId : querySlots_) {
        if (slotQuestId == questId) {
            slotQuestId = 0;
        }
    }
}

void QuestPOIData::SetIconOverlapDistance(const float distance) {
    std::lock_guard lock(mutex_);
    iconOverlapDistance_ = distance;
}

void QuestPOIData::SetIconOverlapPushDistance(const float distance) {
    std::lock_guard lock(mutex_);
    if (distance >= iconOverlapDistance_) {
        iconOverlapPushDistance_ = distance;
    }
}

float QuestPOIData::GetIconOverlapDistance() const {
    std::lock_guard lock(mutex_);
    return iconOverlapDistance_;
}

float QuestPOIData::GetIconOverlapPushDistance() const {
    std::lock_guard lock(mutex_);
    return iconOverlapPushDistance_;
}

QuestPOIPoint QuestPOIData::GetCentroid(const QuestPOIEntry& poi) {
    if (poi.points.empty()) return {0.0f, 0.0f};

    float sumX = 0.0f, sumY = 0.0f;
    for (auto& p : poi.points) {
        sumX += p.x;
        sumY += p.y;
    }
    float n = static_cast<float>(poi.points.size());
    return {sumX / n, sumY / n};
}

bool QuestPOIData::ContainsPoint(const QuestPOIEntry& poi, float x, float y) {
    const auto& pts = poi.points;
    if (pts.size() < 3) return false;

    bool inside = false;
    size_t n = pts.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        float yi = pts[i].y, yj = pts[j].y;
        float xi = pts[i].x, xj = pts[j].x;

        if (((yi > y) != (yj > y)) &&
            (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) {
            inside = !inside;
        }
    }
    return inside;
}

QuestPOIBoundingBox QuestPOIData::GetBoundingBox(const QuestPOIEntry& poi) {
    return ComputeBoundingBoxFromPoints(poi.points);
}

float QuestPOIData::GetArea(const QuestPOIEntry& poi) {
    const auto& pts = poi.points;
    if (pts.size() < 3) return 0.0f;

    float sum = 0.0f;
    size_t n = pts.size();
    for (size_t i = 0; i < n; ++i) {
        size_t j = (i + 1) % n;
        sum += pts[i].x * pts[j].y;
        sum -= pts[j].x * pts[i].y;
    }
    return std::abs(sum) * 0.5f;
}

float QuestPOIData::GetPerimeter(const QuestPOIEntry& poi) {
    const auto& pts = poi.points;
    if (pts.size() < 2) return 0.0f;

    float perimeter = 0.0f;
    size_t n = pts.size();
    for (size_t i = 0; i < n; ++i) {
        size_t j = (i + 1) % n;
        float dx = pts[j].x - pts[i].x;
        float dy = pts[j].y - pts[i].y;
        perimeter += std::sqrt(dx * dx + dy * dy);
    }
    return perimeter;
}

float QuestPOIData::DistanceToEdge(const QuestPOIEntry& poi, float x, float y) {
    const auto& pts = poi.points;
    if (pts.empty()) return std::numeric_limits<float>::max();
    if (pts.size() == 1) {
        float dx = x - pts[0].x;
        float dy = y - pts[0].y;
        return std::sqrt(dx * dx + dy * dy);
    }

    float minDist = std::numeric_limits<float>::max();
    size_t n = pts.size();
    for (size_t i = 0; i < n; ++i) {
        size_t j = (i + 1) % n;
        float ax = pts[i].x, ay = pts[i].y;
        float bx = pts[j].x, by = pts[j].y;

        float abx = bx - ax, aby = by - ay;
        float apx = x - ax, apy = y - ay;

        float ab2 = abx * abx + aby * aby;
        float t = 0.0f;
        if (ab2 > 1e-12f) {
            t = (apx * abx + apy * aby) / ab2;
            t = std::max(0.0f, std::min(1.0f, t));
        }

        float closestX = ax + t * abx;
        float closestY = ay + t * aby;
        float dx = x - closestX;
        float dy = y - closestY;
        float dist = std::sqrt(dx * dx + dy * dy);
        minDist = std::min(minDist, dist);
    }
    return minDist;
}

void QuestPOIData::SetActiveQuest(uint32_t questId, bool active) {
    std::lock_guard lock(mutex_);
    if (active) {
        activeQuests_.insert(questId);
    } else {
        activeQuests_.erase(questId);
    }
}

bool QuestPOIData::IsQuestActive(uint32_t questId) const {
    std::lock_guard lock(mutex_);
    return activeQuests_.count(questId) > 0;
}

std::vector<QuestPOIEntry> QuestPOIData::GetActivePOIs() const {
    std::lock_guard lock(mutex_);
    std::vector<QuestPOIEntry> result;
    for (uint32_t qid : activeQuests_) {
        auto it = pois_.find(qid);
        if (it != pois_.end()) {
            result.insert(result.end(), it->second.begin(), it->second.end());
        }
    }
    return result;
}

std::vector<QuestPOIEntry> QuestPOIData::GetActivePOIsForMap(uint32_t mapId) const {
    std::lock_guard lock(mutex_);
    std::vector<QuestPOIEntry> result;
    for (uint32_t qid : activeQuests_) {
        auto it = pois_.find(qid);
        if (it != pois_.end()) {
            for (const auto& e : it->second) {
                if (e.mapId == mapId) result.push_back(e);
            }
        }
    }
    return result;
}

void QuestPOIData::ClearActiveQuests() {
    std::lock_guard lock(mutex_);
    activeQuests_.clear();
}

void QuestPOIData::Clear() {
    std::lock_guard lock(mutex_);
    pois_.clear();
    activeQuests_.clear();
    querySlots_.fill(0);
    visibleWorldMapQuestIds_.clear();
    worldMapIconLayouts_.clear();
    iconOverlapDistance_ = kDefaultIconOverlapDistance;
    iconOverlapPushDistance_ = kDefaultIconOverlapPushDistance;
}

}

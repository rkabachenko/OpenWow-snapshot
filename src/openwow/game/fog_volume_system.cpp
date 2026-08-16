#include "openwow/game/fog_volume_system.h"

#include <algorithm>
#include <cmath>

namespace openwow::game {

uint32_t FogVolumeSystem::AddVolume(FogVolumeInfo info) {
    if (volumes_.size() >= maxVolumes_) return 0;
    uint32_t id   = nextId_++;
    info.volumeId = id;
    volumes_[id]  = std::move(info);
    return id;
}

void FogVolumeSystem::RemoveVolume(uint32_t volumeId) {
    volumes_.erase(volumeId);
}

std::optional<FogVolumeInfo> FogVolumeSystem::GetVolume(
    uint32_t volumeId) const {
    auto it = volumes_.find(volumeId);
    if (it == volumes_.end()) return std::nullopt;
    return it->second;
}

bool FogVolumeSystem::IsPointInVolume(uint32_t volumeId, float x, float y,
                                       float z) const {
    auto it = volumes_.find(volumeId);
    if (it == volumes_.end()) return false;
    return PointInsideVolume(it->second, x, y, z);
}

float FogVolumeSystem::GetVolumeBlendFactor(uint32_t volumeId, float x,
                                              float y, float z) const {
    auto it = volumes_.find(volumeId);
    if (it == volumes_.end()) return 0.0f;
    return ComputeBlendFactor(it->second, x, y, z);
}

FogVolumeResult FogVolumeSystem::QueryFogAtPoint(float x, float y,
                                                   float z) const {

    const FogVolumeInfo* best = nullptr;
    float bestBlend = 0.0f;
    for (auto& [id, vol] : volumes_) {
        if (!PointInsideVolume(vol, x, y, z)) continue;
        float blend = ComputeBlendFactor(vol, x, y, z);
        if (!best || vol.priority > best->priority ||
            (vol.priority == best->priority && blend > bestBlend)) {
            best      = &vol;
            bestBlend = blend;
        }
    }
    if (!best) return {};

    FogVolumeResult res;
    res.inFog      = true;
    res.fogColor   = {best->color.r, best->color.g, best->color.b};
    res.fogDensity = best->density * bestBlend;
    res.fogStart   = best->startDistance;
    res.fogEnd     = best->endDistance;
    return res;
}

std::vector<FogVolumeInfo> FogVolumeSystem::GetActiveVolumes() const {
    std::vector<FogVolumeInfo> out;
    out.reserve(volumes_.size());
    for (auto& [id, vol] : volumes_) out.push_back(vol);
    return out;
}

std::vector<FogVolumeInfo> FogVolumeSystem::GetVolumesAtPoint(
    float x, float y, float z) const {
    std::vector<FogVolumeInfo> out;
    for (auto& [id, vol] : volumes_) {
        if (PointInsideVolume(vol, x, y, z)) out.push_back(vol);
    }
    return out;
}

uint32_t FogVolumeSystem::GetActiveCount() const {
    return static_cast<uint32_t>(volumes_.size());
}

void FogVolumeSystem::ClearAll() {
    volumes_.clear();
    nextId_ = 1;
}

void FogVolumeSystem::SetMaxVolumes(uint32_t max) { maxVolumes_ = max; }

bool FogVolumeSystem::PointInsideVolume(const FogVolumeInfo& vol, float x,
                                         float y, float z) {
    float dx = x - vol.position.x;
    float dy = y - vol.position.y;
    float dz = z - vol.position.z;

    switch (vol.shape) {
        case FogVolumeShape::Sphere: {
            float distSq = dx * dx + dy * dy + dz * dz;
            return distSq <= vol.radius * vol.radius;
        }
        case FogVolumeShape::Box: {

            return std::abs(dx) <= vol.extents.x &&
                   std::abs(dy) <= vol.extents.y &&
                   std::abs(dz) <= vol.extents.z;
        }
        case FogVolumeShape::Cylinder: {

            float dist2d = std::sqrt(dx * dx + dy * dy);
            float halfH  = vol.height * 0.5f;
            return dist2d <= vol.radius &&
                   std::abs(dz) <= halfH;
        }
    }
    return false;
}

float FogVolumeSystem::DistanceToVolume(const FogVolumeInfo& vol, float x,
                                         float y, float z) {
    float dx = x - vol.position.x;
    float dy = y - vol.position.y;
    float dz = z - vol.position.z;

    switch (vol.shape) {
        case FogVolumeShape::Sphere: {
            float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            return std::max(0.0f, dist - vol.radius);
        }
        case FogVolumeShape::Box: {
            float ex = std::max(0.0f, std::abs(dx) - vol.extents.x);
            float ey = std::max(0.0f, std::abs(dy) - vol.extents.y);
            float ez = std::max(0.0f, std::abs(dz) - vol.extents.z);
            return std::sqrt(ex * ex + ey * ey + ez * ez);
        }
        case FogVolumeShape::Cylinder: {
            float dist2d   = std::sqrt(dx * dx + dy * dy);
            float radialD  = std::max(0.0f, dist2d - vol.radius);
            float halfH    = vol.height * 0.5f;
            float verticalD = std::max(0.0f, std::abs(dz) - halfH);
            return std::sqrt(radialD * radialD + verticalD * verticalD);
        }
    }
    return 0.0f;
}

float FogVolumeSystem::ComputeBlendFactor(const FogVolumeInfo& vol, float x,
                                            float y, float z) {
    if (!PointInsideVolume(vol, x, y, z)) return 0.0f;

    float dx = x - vol.position.x;
    float dy = y - vol.position.y;
    float dz = z - vol.position.z;
    float normDist = 0.0f;

    switch (vol.shape) {
        case FogVolumeShape::Sphere: {
            float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            normDist = (vol.radius > 0.0f) ? dist / vol.radius : 0.0f;
            break;
        }
        case FogVolumeShape::Box: {
            float nx = (vol.extents.x > 0.0f) ? std::abs(dx) / vol.extents.x : 0.0f;
            float ny = (vol.extents.y > 0.0f) ? std::abs(dy) / vol.extents.y : 0.0f;
            float nz = (vol.extents.z > 0.0f) ? std::abs(dz) / vol.extents.z : 0.0f;
            normDist = std::max({nx, ny, nz});
            break;
        }
        case FogVolumeShape::Cylinder: {
            float dist2d = std::sqrt(dx * dx + dy * dy);
            float nr = (vol.radius > 0.0f) ? dist2d / vol.radius : 0.0f;
            float halfH = vol.height * 0.5f;
            float nz = (halfH > 0.0f) ? std::abs(dz) / halfH : 0.0f;
            normDist = std::max(nr, nz);
            break;
        }
    }

    float innerFrac = 0.0f;
    switch (vol.shape) {
        case FogVolumeShape::Sphere:
            innerFrac = (vol.radius > 0.0f) ? vol.innerRadius / vol.radius : 0.0f;
            break;
        case FogVolumeShape::Box: {
            float maxExtent = std::max({vol.extents.x, vol.extents.y, vol.extents.z});
            innerFrac = (maxExtent > 0.0f) ? vol.innerRadius / maxExtent : 0.0f;
            break;
        }
        case FogVolumeShape::Cylinder:
            innerFrac = (vol.radius > 0.0f) ? vol.innerRadius / vol.radius : 0.0f;
            break;
    }
    innerFrac = std::clamp(innerFrac, 0.0f, 1.0f);

    if (normDist <= innerFrac) return 1.0f;

    float range = 1.0f - innerFrac;
    if (range <= 0.0f) return 1.0f;
    return 1.0f - std::clamp((normDist - innerFrac) / range, 0.0f, 1.0f);
}

}

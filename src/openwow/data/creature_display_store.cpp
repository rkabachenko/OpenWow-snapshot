
#include "openwow/data/creature_display_store.h"

#include <utility>

namespace openwow::data {

bool CreatureDisplayStore::AddDisplayEntry(CreatureDisplayEntry entry) {
    if (entry.id == 0) return false;
    auto [_, ok] = displays_.emplace(entry.id, std::move(entry));
    return ok;
}

bool CreatureDisplayStore::AddModelEntry(CreatureModelEntry entry) {
    if (entry.id == 0) return false;
    auto [_, ok] = models_.emplace(entry.id, std::move(entry));
    return ok;
}

std::optional<CreatureDisplayEntry> CreatureDisplayStore::GetDisplayEntry(uint32_t displayId) const {
    auto it = displays_.find(displayId);
    if (it == displays_.end()) return std::nullopt;
    return it->second;
}

std::optional<CreatureModelEntry> CreatureDisplayStore::GetModelEntry(uint32_t modelId) const {
    auto it = models_.find(modelId);
    if (it == models_.end()) return std::nullopt;
    return it->second;
}

std::string CreatureDisplayStore::GetModelPath(uint32_t displayId) const {
    auto dit = displays_.find(displayId);
    if (dit == displays_.end()) return {};
    auto mit = models_.find(dit->second.modelId);
    if (mit == models_.end()) return {};
    return mit->second.modelPath;
}

float CreatureDisplayStore::GetModelScale(uint32_t displayId) const {
    auto dit = displays_.find(displayId);
    if (dit == displays_.end()) return 1.0f;
    auto mit = models_.find(dit->second.modelId);
    if (mit == models_.end()) return dit->second.creatureModelScale;
    return dit->second.creatureModelScale * mit->second.modelScale;
}

float CreatureDisplayStore::GetBoundingRadius(uint32_t displayId) const {
    auto dit = displays_.find(displayId);
    if (dit == displays_.end()) return 0.0f;
    auto mit = models_.find(dit->second.modelId);
    if (mit == models_.end()) return 0.0f;
    return mit->second.boundingRadius * dit->second.creatureModelScale * mit->second.modelScale;
}

float CreatureDisplayStore::GetCombatReach(uint32_t displayId) const {
    auto dit = displays_.find(displayId);
    if (dit == displays_.end()) return 0.0f;
    auto mit = models_.find(dit->second.modelId);
    if (mit == models_.end()) return 0.0f;
    return mit->second.combatReach * dit->second.creatureModelScale * mit->second.modelScale;
}

size_t CreatureDisplayStore::GetDisplayCount() const { return displays_.size(); }
size_t CreatureDisplayStore::GetModelCount()   const { return models_.size(); }

void CreatureDisplayStore::Clear() {
    displays_.clear();
    models_.clear();
}

}

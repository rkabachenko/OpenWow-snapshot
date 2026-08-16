
#include "openwow/game/collection_display.h"

#include <algorithm>
#include <cctype>
#include <random>

namespace openwow::game {

CollectionDisplayEntry* CollectionDisplay::FindEntry(uint32_t spellId) {
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [spellId](const auto& e) { return e.spellId == spellId; });
    return it != entries_.end() ? &(*it) : nullptr;
}

const CollectionDisplayEntry* CollectionDisplay::FindEntry(uint32_t spellId) const {
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [spellId](const auto& e) { return e.spellId == spellId; });
    return it != entries_.end() ? &(*it) : nullptr;
}

void CollectionDisplay::AddEntry(const CollectionDisplayEntry& entry) {
    if (FindEntry(entry.spellId) != nullptr) return;
    entries_.push_back(entry);
}

bool CollectionDisplay::RemoveEntry(uint32_t spellId) {
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [spellId](const auto& e) { return e.spellId == spellId; });
    if (it == entries_.end()) return false;
    entries_.erase(it);
    return true;
}

std::optional<CollectionDisplayEntry> CollectionDisplay::GetEntry(uint32_t spellId) const {
    const auto* e = FindEntry(spellId);
    if (!e) return std::nullopt;
    return *e;
}

std::vector<const CollectionDisplayEntry*> CollectionDisplay::GetMounts() const {
    std::vector<const CollectionDisplayEntry*> result;
    for (const auto& e : entries_)
        if (e.type == CollectionItemType::Mount) result.push_back(&e);
    return result;
}

std::vector<const CollectionDisplayEntry*> CollectionDisplay::GetCompanions() const {
    std::vector<const CollectionDisplayEntry*> result;
    for (const auto& e : entries_)
        if (e.type == CollectionItemType::Companion) result.push_back(&e);
    return result;
}

std::vector<const CollectionDisplayEntry*> CollectionDisplay::GetUsableMounts() const {
    std::vector<const CollectionDisplayEntry*> result;
    for (const auto& e : entries_)
        if (e.type == CollectionItemType::Mount && e.isUsable) result.push_back(&e);
    return result;
}

std::vector<const CollectionDisplayEntry*> CollectionDisplay::GetFavorites() const {
    std::vector<const CollectionDisplayEntry*> result;
    for (const auto& e : entries_)
        if (e.isFavorite) result.push_back(&e);
    return result;
}

bool CollectionDisplay::SetFavorite(uint32_t spellId, bool fav) {
    auto* e = FindEntry(spellId);
    if (!e) return false;
    e->isFavorite = fav;
    return true;
}

size_t CollectionDisplay::GetMountCount() const {
    return static_cast<size_t>(
        std::count_if(entries_.begin(), entries_.end(),
                      [](const auto& e) { return e.type == CollectionItemType::Mount; }));
}

size_t CollectionDisplay::GetCompanionCount() const {
    return static_cast<size_t>(
        std::count_if(entries_.begin(), entries_.end(),
                      [](const auto& e) { return e.type == CollectionItemType::Companion; }));
}

size_t CollectionDisplay::GetTotalCount() const {
    return entries_.size();
}

std::optional<uint32_t> CollectionDisplay::SummonRandom(CollectionItemType type) const {
    std::vector<uint32_t> candidates;
    for (const auto& e : entries_) {
        if (e.type == type && e.isUsable) candidates.push_back(e.spellId);
    }
    if (candidates.empty()) return std::nullopt;

    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
    return candidates[dist(rng)];
}

static std::string ToLower(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

std::vector<const CollectionDisplayEntry*> CollectionDisplay::SearchByName(
    const std::string& query) const {
    std::vector<const CollectionDisplayEntry*> result;
    const auto lq = ToLower(query);
    for (const auto& e : entries_) {
        if (ToLower(e.name).find(lq) != std::string::npos) result.push_back(&e);
    }
    return result;
}

std::vector<const CollectionDisplayEntry*> CollectionDisplay::FilterByCategory(
    CollectionMountCategory category) const {
    std::vector<const CollectionDisplayEntry*> result;
    for (const auto& e : entries_) {
        if (e.type == CollectionItemType::Mount && e.mountCategory == category)
            result.push_back(&e);
    }
    return result;
}

void CollectionDisplay::SetActiveMountSpell(uint32_t spellId) {
    activeMountSpell_ = spellId;
}

std::optional<uint32_t> CollectionDisplay::GetActiveMountSpell() const {
    return activeMountSpell_;
}

void CollectionDisplay::SetActiveCompanionSpell(uint32_t spellId) {
    activeCompanionSpell_ = spellId;
}

std::optional<uint32_t> CollectionDisplay::GetActiveCompanionSpell() const {
    return activeCompanionSpell_;
}

void CollectionDisplay::Clear() {
    entries_.clear();
    activeMountSpell_.reset();
    activeCompanionSpell_.reset();
}

}

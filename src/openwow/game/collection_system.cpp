
#include "openwow/game/collection_system.h"

#include <algorithm>
#include <random>

namespace openwow::game {

CollectionSystem& CollectionSystem::Get() {
    static CollectionSystem instance;
    return instance;
}

void CollectionSystem::AddMount(const MountEntry& entry) {
    std::lock_guard lock(mutex_);

    for (const auto& m : mounts_) {
        if (m.mountId == entry.mountId) return;
    }
    mounts_.push_back(entry);
}

void CollectionSystem::RemoveMount(uint32_t mountId) {
    std::lock_guard lock(mutex_);
    mounts_.erase(
        std::remove_if(mounts_.begin(), mounts_.end(),
                       [mountId](const MountEntry& m) {
                           return m.mountId == mountId;
                       }),
        mounts_.end());
    if (active_mount_id_ == mountId) active_mount_id_ = 0;
}

std::vector<MountEntry> CollectionSystem::GetMounts() const {
    std::lock_guard lock(mutex_);
    return mounts_;
}

uint32_t CollectionSystem::GetMountCount() const {
    std::lock_guard lock(mutex_);
    return static_cast<uint32_t>(mounts_.size());
}

bool CollectionSystem::HasMount(uint32_t mountId) const {
    std::lock_guard lock(mutex_);
    return std::any_of(mounts_.begin(), mounts_.end(),
                       [mountId](const MountEntry& m) {
                           return m.mountId == mountId;
                       });
}

std::vector<MountEntry> CollectionSystem::GetMountsByType(
    const std::string& type) const {
    std::lock_guard lock(mutex_);
    std::vector<MountEntry> result;
    for (const auto& m : mounts_) {
        if (m.mountType == type) result.push_back(m);
    }
    return result;
}

void CollectionSystem::SetMountFavorite(uint32_t mountId, bool favorite) {
    std::lock_guard lock(mutex_);
    for (auto& m : mounts_) {
        if (m.mountId == mountId) {
            m.isFavorite = favorite;
            return;
        }
    }
}

std::vector<MountEntry> CollectionSystem::GetFavoriteMounts() const {
    std::lock_guard lock(mutex_);
    std::vector<MountEntry> result;
    for (const auto& m : mounts_) {
        if (m.isFavorite) result.push_back(m);
    }
    return result;
}

uint32_t CollectionSystem::GetActiveMountId() const {
    std::lock_guard lock(mutex_);
    return active_mount_id_;
}

void CollectionSystem::SetActiveMountId(uint32_t mountId) {
    std::lock_guard lock(mutex_);
    active_mount_id_ = mountId;
}

void CollectionSystem::AddCompanion(const CompanionEntry& entry) {
    std::lock_guard lock(mutex_);
    for (const auto& c : companions_) {
        if (c.companionId == entry.companionId) return;
    }
    companions_.push_back(entry);
}

void CollectionSystem::RemoveCompanion(uint32_t companionId) {
    std::lock_guard lock(mutex_);
    companions_.erase(
        std::remove_if(companions_.begin(), companions_.end(),
                       [companionId](const CompanionEntry& c) {
                           return c.companionId == companionId;
                       }),
        companions_.end());
    if (active_companion_id_ == companionId) active_companion_id_ = 0;
}

std::vector<CompanionEntry> CollectionSystem::GetCompanions() const {
    std::lock_guard lock(mutex_);
    return companions_;
}

uint32_t CollectionSystem::GetCompanionCount() const {
    std::lock_guard lock(mutex_);
    return static_cast<uint32_t>(companions_.size());
}

bool CollectionSystem::HasCompanion(uint32_t companionId) const {
    std::lock_guard lock(mutex_);
    return std::any_of(companions_.begin(), companions_.end(),
                       [companionId](const CompanionEntry& c) {
                           return c.companionId == companionId;
                       });
}

void CollectionSystem::SetCompanionFavorite(uint32_t companionId,
                                            bool favorite) {
    std::lock_guard lock(mutex_);
    for (auto& c : companions_) {
        if (c.companionId == companionId) {
            c.isFavorite = favorite;
            return;
        }
    }
}

std::vector<CompanionEntry> CollectionSystem::GetFavoriteCompanions() const {
    std::lock_guard lock(mutex_);
    std::vector<CompanionEntry> result;
    for (const auto& c : companions_) {
        if (c.isFavorite) result.push_back(c);
    }
    return result;
}

uint32_t CollectionSystem::GetActiveCompanionId() const {
    std::lock_guard lock(mutex_);
    return active_companion_id_;
}

void CollectionSystem::SetActiveCompanionId(uint32_t companionId) {
    std::lock_guard lock(mutex_);
    active_companion_id_ = companionId;
}

uint32_t CollectionSystem::SummonRandomMount() const {
    std::lock_guard lock(mutex_);

    std::vector<uint32_t> pool;
    for (const auto& m : mounts_) {
        if (m.isFavorite) pool.push_back(m.mountId);
    }
    if (pool.empty()) {
        for (const auto& m : mounts_) {
            pool.push_back(m.mountId);
        }
    }
    if (pool.empty()) return 0;
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<size_t> dist(0, pool.size() - 1);
    return pool[dist(rng)];
}

uint32_t CollectionSystem::SummonRandomCompanion() const {
    std::lock_guard lock(mutex_);
    std::vector<uint32_t> pool;
    for (const auto& c : companions_) {
        if (c.isFavorite) pool.push_back(c.companionId);
    }
    if (pool.empty()) {
        for (const auto& c : companions_) {
            pool.push_back(c.companionId);
        }
    }
    if (pool.empty()) return 0;
    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<size_t> dist(0, pool.size() - 1);
    return pool[dist(rng)];
}

void CollectionSystem::Reset() {
    std::lock_guard lock(mutex_);
    mounts_.clear();
    companions_.clear();
    active_mount_id_ = 0;
    active_companion_id_ = 0;
}

}

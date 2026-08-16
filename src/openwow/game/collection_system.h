
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace openwow::game {

struct MountEntry {
    uint32_t mountId = 0;
    std::string name;
    uint32_t displayId = 0;
    std::string mountType;
    uint32_t speed = 0;
    bool isFavorite = false;
};

struct CompanionEntry {
    uint32_t companionId = 0;
    std::string name;
    uint32_t displayId = 0;
    uint32_t creatureId = 0;
    bool isFavorite = false;
};

class CollectionSystem {
 public:
    static CollectionSystem& Get();

    void AddMount(const MountEntry& entry);
    void RemoveMount(uint32_t mountId);
    [[nodiscard]] std::vector<MountEntry> GetMounts() const;
    [[nodiscard]] uint32_t GetMountCount() const;
    [[nodiscard]] bool HasMount(uint32_t mountId) const;
    [[nodiscard]] std::vector<MountEntry> GetMountsByType(
        const std::string& type) const;
    void SetMountFavorite(uint32_t mountId, bool favorite);
    [[nodiscard]] std::vector<MountEntry> GetFavoriteMounts() const;
    [[nodiscard]] uint32_t GetActiveMountId() const;
    void SetActiveMountId(uint32_t mountId);

    void AddCompanion(const CompanionEntry& entry);
    void RemoveCompanion(uint32_t companionId);
    [[nodiscard]] std::vector<CompanionEntry> GetCompanions() const;
    [[nodiscard]] uint32_t GetCompanionCount() const;
    [[nodiscard]] bool HasCompanion(uint32_t companionId) const;
    void SetCompanionFavorite(uint32_t companionId, bool favorite);
    [[nodiscard]] std::vector<CompanionEntry> GetFavoriteCompanions() const;
    [[nodiscard]] uint32_t GetActiveCompanionId() const;
    void SetActiveCompanionId(uint32_t companionId);

    [[nodiscard]] uint32_t SummonRandomMount() const;
    [[nodiscard]] uint32_t SummonRandomCompanion() const;

    void Reset();

 private:
    CollectionSystem() = default;

    std::vector<MountEntry> mounts_;
    std::vector<CompanionEntry> companions_;
    uint32_t active_mount_id_ = 0;
    uint32_t active_companion_id_ = 0;
    mutable std::mutex mutex_;
};

}

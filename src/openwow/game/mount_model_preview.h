
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace openwow::game {

enum class MountPreviewType : uint8_t {
    Ground          = 0,
    Flying          = 1,
    Aquatic         = 2,
    GroundAndFlying = 3,
};

struct MountModelInfo {
    uint32_t         mountId    = 0;
    uint32_t         spellId    = 0;
    uint32_t         displayId  = 0;
    std::string      name;
    MountPreviewType type       = MountPreviewType::Ground;
    uint32_t         iconId     = 0;
    bool             isFavorite  = false;
    bool             isCollected = false;
    float            speed       = 1.0f;
};

class MountModelPreview {
 public:

    void SetMount(const MountModelInfo& info);

    [[nodiscard]] std::optional<MountModelInfo> GetCurrentMount() const;

    void  SetRotation(float angle);
    [[nodiscard]] float GetRotation() const;

    void SetAnimating(bool animating);
    [[nodiscard]] bool IsAnimating() const;

    void SetMountList(const std::vector<MountModelInfo>& list);

    [[nodiscard]] const std::vector<MountModelInfo>& GetMountList() const;

    [[nodiscard]] uint32_t GetMountCount() const;

    [[nodiscard]] std::vector<MountModelInfo> FilterByType(MountPreviewType type) const;

    [[nodiscard]] uint32_t GetCollectedCount() const;

    [[nodiscard]] std::vector<MountModelInfo> GetFavorites() const;

    [[nodiscard]] std::optional<MountModelInfo> SelectRandom();

    [[nodiscard]] std::optional<MountModelInfo> SelectRandomFavorite();

    [[nodiscard]] std::vector<MountModelInfo> Search(const std::string& query) const;

    void Clear();

 private:
    std::vector<MountModelInfo>        mounts_;
    std::optional<MountModelInfo>      current_;
    float                              rotation_  = 0.0f;
    bool                               animating_ = false;
    std::mt19937                       rng_{std::random_device{}()};
};

}

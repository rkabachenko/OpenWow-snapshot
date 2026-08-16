
#include "openwow/game/mount_model_preview.h"

#include <cctype>

namespace openwow::game {

namespace {

bool CaseInsensitiveContains(const std::string& haystack,
                             const std::string& needle) {
    if (needle.empty()) return true;
    if (needle.size() > haystack.size()) return false;
    auto it = std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        });
    return it != haystack.end();
}

}

void MountModelPreview::SetMount(const MountModelInfo& info) {
    current_ = info;
}

std::optional<MountModelInfo> MountModelPreview::GetCurrentMount() const {
    return current_;
}

void MountModelPreview::SetRotation(float angle) { rotation_ = angle; }
float MountModelPreview::GetRotation() const { return rotation_; }

void MountModelPreview::SetAnimating(bool animating) { animating_ = animating; }
bool MountModelPreview::IsAnimating() const { return animating_; }

void MountModelPreview::SetMountList(const std::vector<MountModelInfo>& list) {
    mounts_ = list;
}

const std::vector<MountModelInfo>& MountModelPreview::GetMountList() const {
    return mounts_;
}

uint32_t MountModelPreview::GetMountCount() const {
    return static_cast<uint32_t>(mounts_.size());
}

std::vector<MountModelInfo> MountModelPreview::FilterByType(
    MountPreviewType type) const {
    std::vector<MountModelInfo> out;
    for (const auto& m : mounts_) {
        if (m.type == type) out.push_back(m);
    }
    return out;
}

uint32_t MountModelPreview::GetCollectedCount() const {
    uint32_t n = 0;
    for (const auto& m : mounts_) {
        if (m.isCollected) ++n;
    }
    return n;
}

std::vector<MountModelInfo> MountModelPreview::GetFavorites() const {
    std::vector<MountModelInfo> out;
    for (const auto& m : mounts_) {
        if (m.isFavorite) out.push_back(m);
    }
    return out;
}

std::optional<MountModelInfo> MountModelPreview::SelectRandom() {
    std::vector<const MountModelInfo*> collected;
    for (const auto& m : mounts_) {
        if (m.isCollected) collected.push_back(&m);
    }
    if (collected.empty()) return std::nullopt;
    std::uniform_int_distribution<size_t> dist(0, collected.size() - 1);
    return *collected[dist(rng_)];
}

std::optional<MountModelInfo> MountModelPreview::SelectRandomFavorite() {
    std::vector<const MountModelInfo*> favs;
    std::vector<const MountModelInfo*> collected;
    for (const auto& m : mounts_) {
        if (m.isCollected) {
            collected.push_back(&m);
            if (m.isFavorite) favs.push_back(&m);
        }
    }
    auto& pool = favs.empty() ? collected : favs;
    if (pool.empty()) return std::nullopt;
    std::uniform_int_distribution<size_t> dist(0, pool.size() - 1);
    return *pool[dist(rng_)];
}

std::vector<MountModelInfo> MountModelPreview::Search(
    const std::string& query) const {
    std::vector<MountModelInfo> out;
    for (const auto& m : mounts_) {
        if (CaseInsensitiveContains(m.name, query)) out.push_back(m);
    }
    return out;
}

void MountModelPreview::Clear() {
    mounts_.clear();
    current_.reset();
    rotation_  = 0.0f;
    animating_ = false;
}

}

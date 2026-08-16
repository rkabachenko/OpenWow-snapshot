
#include "openwow/game/inspect_detail_display.h"

#include <algorithm>
#include <numeric>

namespace openwow::game {

void InspectDetailDisplay::StartInspect(uint64_t targetGuid,
                                        const std::string& targetName) {
    inspecting_       = true;
    targetGuid_       = targetGuid;
    targetName_       = targetName;
    items_.clear();
    talents_.clear();
    glyphs_.clear();
    achievementPoints_ = 0;
    achievements_.clear();
}

void InspectDetailDisplay::Close() {
    inspecting_ = false;
}

bool InspectDetailDisplay::IsInspecting() const {
    return inspecting_;
}

void InspectDetailDisplay::SetItems(const std::vector<InspectItemInfo>& items) {
    items_.clear();
    for (const auto& item : items) {
        if (item.slotId < kMaxEquipSlots) {
            items_.push_back(item);
        }
    }
}

void InspectDetailDisplay::SetTalents(const std::vector<InspectTalentInfo>& talents) {
    talents_.clear();
    for (const auto& t : talents) {
        if (t.tabIndex < kMaxTalentTabs) {
            talents_.push_back(t);
        }
    }
}

void InspectDetailDisplay::SetGlyphs(const std::vector<InspectGlyphInfo>& glyphs) {
    glyphs_.clear();
    for (const auto& g : glyphs) {
        if (g.slotIndex < kMaxGlyphSlots) {
            glyphs_.push_back(g);
        }
    }
}

void InspectDetailDisplay::SetAchievementPoints(uint32_t total) {
    achievementPoints_ = total;
}

void InspectDetailDisplay::AddAchievement(const InspectAchievementInfo& info) {
    achievements_.push_back(info);
}

uint64_t InspectDetailDisplay::GetTargetGuid() const {
    return targetGuid_;
}

std::string InspectDetailDisplay::GetTargetName() const {
    return targetName_;
}

const std::vector<InspectItemInfo>& InspectDetailDisplay::GetItems() const {
    return items_;
}

std::optional<InspectItemInfo> InspectDetailDisplay::GetItemInSlot(uint8_t slotId) const {
    for (const auto& item : items_) {
        if (item.slotId == slotId) return item;
    }
    return std::nullopt;
}

const std::vector<InspectTalentInfo>& InspectDetailDisplay::GetTalents() const {
    return talents_;
}

const std::vector<InspectGlyphInfo>& InspectDetailDisplay::GetGlyphs() const {
    return glyphs_;
}

uint8_t InspectDetailDisplay::GetTotalTalentPoints() const {
    uint8_t total = 0;
    for (const auto& t : talents_) {
        total += t.pointsSpent;
    }
    return total;
}

uint32_t InspectDetailDisplay::GetAchievementPoints() const {
    return achievementPoints_;
}

const std::vector<InspectAchievementInfo>& InspectDetailDisplay::GetAchievements() const {
    return achievements_;
}

float InspectDetailDisplay::GetAverageItemLevel() const {
    if (items_.empty()) return 0.0f;
    const float sum = std::accumulate(items_.begin(), items_.end(), 0.0f,
        [](float acc, const InspectItemInfo& i) {
            return acc + static_cast<float>(i.itemLevel);
        });
    return sum / static_cast<float>(items_.size());
}

}

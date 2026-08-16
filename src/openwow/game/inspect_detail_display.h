
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

struct InspectItemInfo {
    uint8_t                 slotId    = 0;
    uint32_t                itemId    = 0;
    uint32_t                enchantId = 0;
    std::array<uint32_t, 3> gemIds    = {0, 0, 0};
    uint16_t                itemLevel = 0;
    uint8_t                 quality   = 0;
};

struct InspectTalentInfo {
    uint8_t     tabIndex    = 0;
    std::string tabName;
    uint8_t     pointsSpent = 0;
    uint32_t    iconId      = 0;
};

struct InspectGlyphInfo {
    uint8_t  slotIndex = 0;
    uint32_t glyphId   = 0;
    uint32_t spellId   = 0;
    uint8_t  glyphType = 0;
};

struct InspectAchievementInfo {
    uint32_t achieveId          = 0;
    uint32_t completedTimestamp = 0;
    uint16_t points             = 0;
};

class InspectDetailDisplay {
 public:
    static constexpr uint8_t kMaxEquipSlots  = 19;
    static constexpr uint8_t kMaxTalentTabs  = 3;
    static constexpr uint8_t kMaxGlyphSlots  = 6;

    InspectDetailDisplay() = default;

    void StartInspect(uint64_t targetGuid, const std::string& targetName);
    void Close();
    [[nodiscard]] bool IsInspecting() const;

    void SetItems(const std::vector<InspectItemInfo>& items);
    void SetTalents(const std::vector<InspectTalentInfo>& talents);
    void SetGlyphs(const std::vector<InspectGlyphInfo>& glyphs);
    void SetAchievementPoints(uint32_t total);
    void AddAchievement(const InspectAchievementInfo& info);

    [[nodiscard]] uint64_t    GetTargetGuid() const;
    [[nodiscard]] std::string GetTargetName() const;

    [[nodiscard]] const std::vector<InspectItemInfo>& GetItems() const;
    [[nodiscard]] std::optional<InspectItemInfo> GetItemInSlot(uint8_t slotId) const;

    [[nodiscard]] const std::vector<InspectTalentInfo>& GetTalents() const;
    [[nodiscard]] const std::vector<InspectGlyphInfo>&  GetGlyphs() const;

    [[nodiscard]] uint8_t  GetTotalTalentPoints() const;
    [[nodiscard]] uint32_t GetAchievementPoints() const;
    [[nodiscard]] const std::vector<InspectAchievementInfo>& GetAchievements() const;

    [[nodiscard]] float GetAverageItemLevel() const;

 private:
    bool        inspecting_       = false;
    uint64_t    targetGuid_       = 0;
    std::string targetName_;

    std::vector<InspectItemInfo>        items_;
    std::vector<InspectTalentInfo>      talents_;
    std::vector<InspectGlyphInfo>       glyphs_;
    uint32_t                            achievementPoints_ = 0;
    std::vector<InspectAchievementInfo> achievements_;
};

}

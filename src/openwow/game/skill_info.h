
#pragma once

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace openwow::data::dbc {
class DbcLoader;
struct SkillLineEntry;
struct SkillRaceClassInfoEntry;
}

namespace openwow::game {

struct SkillLineInfoEntry {
    uint32_t skill_id       = 0;
    uint32_t category_id    = 0;
    uint32_t is_visible     = 1;
    uint32_t is_untrained   = 0;
    uint32_t queued_points  = 0;
};

struct SkillCategoryInfoEntry {
    uint32_t category_id  = 0;
    uint32_t is_collapsed = 0;
};

[[nodiscard]] uint32_t ResolveSkillRankCost(
    const openwow::data::dbc::DbcLoader& dbc,
    const openwow::data::dbc::SkillLineEntry& skill_line,
    const openwow::data::dbc::SkillRaceClassInfoEntry& race_class_info,
    uint32_t rank);

class SkillInfoStore {
 public:
    static SkillInfoStore& Get();

    void AllocateArrays(uint32_t num_skill_lines);

    void UpdateFromPlayer(const class CGPlayer_C& player,
                          const openwow::data::dbc::DbcLoader& dbc);

    void RebuildDisplayList();

    bool CollapseCategory(uint32_t skill_index);

    bool ExpandCategory(uint32_t skill_index);

    void CollapseAllCategories();
    void ExpandAllCategories();

    [[nodiscard]] bool AddQueuedPoint(
        uint32_t skill_index,
        const class CGPlayer_C& player,
        const openwow::data::dbc::DbcLoader& dbc);

    [[nodiscard]] bool RemoveQueuedPoint(
        uint32_t skill_index,
        const class CGPlayer_C& player,
        const openwow::data::dbc::DbcLoader& dbc);

    void ClearQueuedPoints();
    [[nodiscard]] uint32_t GetQueuedPointsForSkill(uint32_t skill_id) const;

    [[nodiscard]] std::vector<std::pair<uint32_t, uint32_t>>
    CollectQueuedPoints() const;

    [[nodiscard]] int32_t GetSelectedSkillIndex() const;

    void SetSelectedSkillEntryIndex(uint32_t entry_index);

    [[nodiscard]] uint32_t GetCategoryCollapseState(uint32_t skill_index) const;

    [[nodiscard]] int16_t GetSkillModifier(uint32_t slot_index,
                                            const uint16_t* modifier_fields) const;

    [[nodiscard]] uint32_t GetNumSkillLines() const { return num_skill_lines_; }
    [[nodiscard]] uint32_t GetNumCategories() const { return num_categories_; }
    [[nodiscard]] uint32_t GetNumVisibleLines() const { return num_visible_; }
    [[nodiscard]] uint32_t GetSelectedSkillId() const { return selected_skill_id_; }
    [[nodiscard]] uint32_t GetTotalPointsUsed() const { return total_points_used_; }

    [[nodiscard]] const SkillLineInfoEntry* GetSkillEntry(uint32_t index) const;
    [[nodiscard]] const SkillCategoryInfoEntry* GetCategoryEntry(uint32_t index) const;

    void FreeAllEntries();

    void Reset();

 private:
    SkillInfoStore() = default;

    [[nodiscard]] std::optional<uint32_t> FindCategoryIndexForSkillEntry(
        uint32_t skill_index) const;
    void RecalculateQueuedPointCosts(
        const class CGPlayer_C& player,
        const openwow::data::dbc::DbcLoader& dbc);

    std::vector<SkillLineInfoEntry> skill_entries_;
    uint32_t num_skill_lines_ = 0;
    uint32_t skill_line_capacity_ = 0;

    std::vector<SkillCategoryInfoEntry> category_entries_;
    uint32_t num_categories_ = 0;
    uint32_t category_capacity_ = 0;

    uint32_t num_visible_ = 0;
    uint32_t selected_skill_id_ = 0;
    uint32_t total_points_used_ = 0;
    uint32_t expand_mask_ = 0xFFFFFFFF;
    uint64_t owner_guid_ = 0;

    const openwow::data::dbc::DbcLoader* dbc_ = nullptr;
    bool hide_untrained_skills_ = false;
};

}

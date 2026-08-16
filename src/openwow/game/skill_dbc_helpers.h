#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace openwow::game {

struct SkillRaceClassInfoRec {
    uint32_t id              = 0;
    uint32_t skill_line_id   = 0;
    uint32_t race_mask       = 0;
    uint32_t class_mask      = 0;
    uint32_t flags           = 0;
    uint32_t min_level       = 0;
    uint32_t skill_tier_id   = 0;
    uint32_t skill_cost_index = 0;
};

static constexpr uint32_t kMaxSkillTiers = 16;

struct SkillTiersRec {
    uint32_t id = 0;
    uint32_t max_values[kMaxSkillTiers] = {};
};

struct SkillLineRec {
    uint32_t id = 0;
    uint32_t category_id = 0;
    uint32_t skill_cost_id = 0;
};

struct SkillCostsDataRec {
    uint32_t id = 0;
    uint32_t skill_costs_id = 0;
    uint32_t cost_data[3] = {};
};

struct SkillRaceClassInfoRun {
    const SkillRaceClassInfoRec* first_entry = nullptr;
    uint32_t count = 0;
};

struct SkillCostsDataRun {
    const SkillCostsDataRec* first_entry = nullptr;
    uint32_t count = 0;
};

class SkillDbcHelpers {
 public:
    static SkillDbcHelpers& Get();

    SkillDbcHelpers(const SkillDbcHelpers&) = delete;
    SkillDbcHelpers& operator=(const SkillDbcHelpers&) = delete;

    void SetSkillRaceClassInfoData(const SkillRaceClassInfoRec* data,
                                   uint32_t count);
    void SetSkillLineData(const SkillLineRec* data, uint32_t count);
    void SetSkillTiersData(const SkillTiersRec* data, uint32_t count);
    void SetSkillCostsData(const SkillCostsDataRec* data, uint32_t count);

    void BuildFromDBC();

    void Reset();

    [[nodiscard]] uint32_t GetMaxValueForTier(
        const SkillRaceClassInfoRec* rec, uint32_t tier_index) const;

    [[nodiscard]] const SkillRaceClassInfoRec* FindBySkillId(
        uint8_t race, uint8_t player_class, uint32_t skill_line_id) const;

    [[nodiscard]] uint32_t GetPointCost(
        const SkillRaceClassInfoRec* race_class_rec, uint32_t rank) const;

    static void RegisterSpellOpcodeHandlers();

    [[nodiscard]] bool IsDirty() const { return dirty_; }

 private:
    SkillDbcHelpers() = default;

    void RebuildSkillTiersIndex();

    const SkillRaceClassInfoRec* race_class_info_data_ = nullptr;
    uint32_t race_class_info_count_ = 0;

    const SkillLineRec* skill_line_data_ = nullptr;
    uint32_t skill_line_count_ = 0;

    const SkillTiersRec* skill_tiers_data_ = nullptr;
    uint32_t skill_tiers_count_ = 0;
    uint32_t skill_tiers_min_id_ = 0;
    uint32_t skill_tiers_max_id_ = 0;
    std::vector<const SkillTiersRec*> skill_tiers_index_;

    const SkillCostsDataRec* skill_costs_data_ = nullptr;
    uint32_t skill_costs_count_ = 0;

    std::unordered_map<uint32_t, SkillRaceClassInfoRun> race_class_runs_;

    std::unordered_map<uint32_t, const SkillLineRec*> skill_line_map_;

    std::unordered_map<uint32_t, SkillCostsDataRun> cost_runs_;

    bool dirty_ = false;
};

}

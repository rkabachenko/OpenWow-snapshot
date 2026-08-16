
#include "openwow/game/skill_dbc_helpers.h"
#include "openwow/game/skill_line_ability_lookup.h"

namespace openwow::game {
namespace {

template <typename Record, typename RunMap, typename KeyFn>
void BuildLatestContiguousRuns(const Record* data, uint32_t count, RunMap& runs,
                               KeyFn key_fn) {
    if (!data || count == 0) {
        return;
    }

    const Record* run_begin = data;
    uint32_t run_count = 1;
    uint32_t run_key = key_fn(*data);

    const auto flush_run = [&]() {
        runs[run_key] = typename RunMap::mapped_type{run_begin, run_count};
    };

    for (uint32_t i = 1; i < count; ++i) {
        const Record& record = data[i];
        const uint32_t key = key_fn(record);
        if (key == run_key) {
            ++run_count;
            continue;
        }

        flush_run();
        run_begin = &record;
        run_count = 1;
        run_key = key;
    }

    flush_run();
}

}

SkillDbcHelpers& SkillDbcHelpers::Get() {
    static SkillDbcHelpers instance;
    return instance;
}

void SkillDbcHelpers::RebuildSkillTiersIndex() {
    skill_tiers_index_.clear();
    skill_tiers_min_id_ = 0;
    skill_tiers_max_id_ = 0;

    if (!skill_tiers_data_ || skill_tiers_count_ == 0) {
        return;
    }

    skill_tiers_min_id_ = skill_tiers_data_[0].id;
    skill_tiers_max_id_ = skill_tiers_data_[0].id;
    for (uint32_t i = 1; i < skill_tiers_count_; ++i) {
        const uint32_t id = skill_tiers_data_[i].id;
        if (id < skill_tiers_min_id_) {
            skill_tiers_min_id_ = id;
        }
        if (id > skill_tiers_max_id_) {
            skill_tiers_max_id_ = id;
        }
    }

    skill_tiers_index_.assign(
        static_cast<std::size_t>(skill_tiers_max_id_ - skill_tiers_min_id_) + 1u,
        nullptr);
    for (uint32_t i = 0; i < skill_tiers_count_; ++i) {
        const SkillTiersRec& record = skill_tiers_data_[i];
        skill_tiers_index_[record.id - skill_tiers_min_id_] = &record;
    }
}

void SkillDbcHelpers::SetSkillRaceClassInfoData(
    const SkillRaceClassInfoRec* data, uint32_t count) {
    race_class_info_data_ = data;
    race_class_info_count_ = count;
}

void SkillDbcHelpers::SetSkillLineData(const SkillLineRec* data,
                                       uint32_t count) {
    skill_line_data_ = data;
    skill_line_count_ = count;
}

void SkillDbcHelpers::SetSkillTiersData(const SkillTiersRec* data,
                                        uint32_t count) {
    skill_tiers_data_ = data;
    skill_tiers_count_ = count;
    RebuildSkillTiersIndex();
}

void SkillDbcHelpers::SetSkillCostsData(const SkillCostsDataRec* data,
                                        uint32_t count) {
    skill_costs_data_ = data;
    skill_costs_count_ = count;
}

void SkillDbcHelpers::BuildFromDBC() {

    race_class_runs_.clear();
    skill_line_map_.clear();
    cost_runs_.clear();

    BuildLatestContiguousRuns(
        race_class_info_data_, race_class_info_count_, race_class_runs_,
        [](const SkillRaceClassInfoRec& rec) { return rec.skill_line_id; });

    if (skill_line_data_) {
        for (uint32_t i = 0; i < skill_line_count_; ++i) {
            const auto& rec = skill_line_data_[i];
            skill_line_map_[rec.id] = &rec;
        }
    }

    BuildLatestContiguousRuns(
        skill_costs_data_, skill_costs_count_, cost_runs_,
        [](const SkillCostsDataRec& rec) { return rec.skill_costs_id; });

    dirty_ = true;
}

void SkillDbcHelpers::Reset() {
    race_class_runs_.clear();
    skill_line_map_.clear();
    cost_runs_.clear();
    skill_tiers_index_.clear();
    skill_tiers_min_id_ = 0;
    skill_tiers_max_id_ = 0;
    dirty_ = false;
}

uint32_t SkillDbcHelpers::GetMaxValueForTier(
    const SkillRaceClassInfoRec* rec, uint32_t tier_index) const {
    if (!rec) return 0;

    uint32_t tier_id = rec->skill_tier_id;
    if (tier_id == 0) return 0;

    if (skill_tiers_index_.empty() || tier_id < skill_tiers_min_id_ ||
        tier_id > skill_tiers_max_id_) {
        return 0;
    }

    const SkillTiersRec* tiers_rec = skill_tiers_index_[tier_id - skill_tiers_min_id_];
    if (!tiers_rec) return 0;

    if (tier_index >= kMaxSkillTiers) return 0;

    return tiers_rec->max_values[tier_index];
}

const SkillRaceClassInfoRec* SkillDbcHelpers::FindBySkillId(
    uint8_t race, uint8_t player_class, uint32_t skill_line_id) const {

    const auto it = race_class_runs_.find(skill_line_id);
    if (it == race_class_runs_.end() || !it->second.first_entry) {
        return nullptr;
    }

    for (uint32_t i = 0; i < it->second.count; ++i) {
        const SkillRaceClassInfoRec* rec = it->second.first_entry + i;
        const bool race_ok = SkillRaceClassInfoMaskMatches(rec->race_mask, race);
        const bool class_ok =
            SkillRaceClassInfoMaskMatches(rec->class_mask, player_class);
        if (race_ok && class_ok) {
            return rec;
        }
    }

    return nullptr;
}

uint32_t SkillDbcHelpers::GetPointCost(
    const SkillRaceClassInfoRec* race_class_rec, uint32_t rank) const {
    if (!race_class_rec || rank == 0) return 0;

    const auto skill_line_it = skill_line_map_.find(race_class_rec->skill_line_id);
    if (skill_line_it == skill_line_map_.end() ||
        skill_line_it->second->skill_cost_id == 0) {
        return 0;
    }

    const auto it = cost_runs_.find(skill_line_it->second->skill_cost_id);
    if (it == cost_runs_.end() || !it->second.first_entry) {
        return 0;
    }

    if (rank > it->second.count) {
        return 0;
    }

    const SkillCostsDataRec* rank_rec = it->second.first_entry + (rank - 1);
    if (race_class_rec->skill_cost_index >= 3) return 0;

    return rank_rec->cost_data[race_class_rec->skill_cost_index];
}

void SkillDbcHelpers::RegisterSpellOpcodeHandlers() {

}

}

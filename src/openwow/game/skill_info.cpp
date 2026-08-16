
#include "openwow/game/skill_info.h"

#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_entries_gameplay.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/skill_line_ability_lookup.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <unordered_map>

namespace openwow::game {

namespace {

constexpr std::uint32_t kMaxPlayerSkillSlots = 128;

struct SkillQueueCostContext {
  CGPlayer_C::SkillInfo skill{};
  const data::dbc::SkillLineEntry* skill_line = nullptr;
  const data::dbc::SkillRaceClassInfoEntry* race_class_info = nullptr;
};

[[nodiscard]] const data::dbc::SkillRaceClassInfoEntry* FindSkillRaceClassInfo(
    const data::dbc::DbcLoader& dbc,
    const std::uint8_t race,
    const std::uint8_t player_class,
    const std::uint32_t skill_line_id) {
  return FindSkillRaceClassInfoBySkillId(
      dbc.skill_race_class_info().entries(), race, player_class, skill_line_id);
}

[[nodiscard]] std::uint32_t SkillBaseRankForQueue(
    const CGPlayer_C::SkillInfo& skill) {
  const auto step_modifier =
      skill.value != 0 ? static_cast<std::uint16_t>(skill.step_modifier) : 0u;
  return static_cast<std::uint32_t>(skill.value) + step_modifier;
}

[[nodiscard]] std::uint32_t SkillMaxRankForQueue(
    const CGPlayer_C::SkillInfo& skill) {

  const auto step_modifier =
      skill.max_value != 0 ? static_cast<std::uint16_t>(skill.step_modifier) : 0u;
  return static_cast<std::uint32_t>(skill.max_value) + step_modifier;
}

[[nodiscard]] std::optional<SkillQueueCostContext> BuildSkillQueueCostContext(
    const CGPlayer_C& player,
    const data::dbc::DbcLoader& dbc,
    const std::uint32_t skill_id) {
  if (skill_id > std::numeric_limits<std::uint16_t>::max()) {
    return std::nullopt;
  }

  const auto skill = player.FindSkill(static_cast<std::uint16_t>(skill_id));
  const auto* skill_line = dbc.skill_line().LookupEntry(skill_id);
  const auto* race_class_info = FindSkillRaceClassInfo(
      dbc, player.State().GetRace(), player.State().GetClass(), skill_id);
  if (!skill.has_value() || skill_line == nullptr || race_class_info == nullptr) {
    return std::nullopt;
  }

  return SkillQueueCostContext{*skill, skill_line, race_class_info};
}

[[nodiscard]] std::uint32_t CategorySortIndex(
    const data::dbc::DbcLoader* dbc,
    const std::uint32_t category_id) {
  if (dbc == nullptr) {
    return category_id;
  }

  if (const auto* category = dbc->skill_line_category().LookupEntry(category_id);
      category != nullptr) {
    return category->sort_index;
  }

  return category_id;
}

[[nodiscard]] std::string_view SkillLineName(
    const data::dbc::DbcLoader* dbc,
    const std::uint32_t skill_id) {
  if (dbc == nullptr) {
    return {};
  }

  if (const auto* entry = dbc->skill_line().LookupEntry(skill_id); entry != nullptr) {
    return entry->name;
  }

  return {};
}

[[nodiscard]] int CompareSkillCategoriesByDisplayOrder(
    const SkillCategoryInfoEntry& left,
    const SkillCategoryInfoEntry& right,
    const data::dbc::DbcLoader* dbc) {
  const auto left_sort = CategorySortIndex(dbc, left.category_id);
  const auto right_sort = CategorySortIndex(dbc, right.category_id);
  if (left_sort < right_sort) {
    return -1;
  }

  if (left_sort > right_sort) {
    return 1;
  }

  return 0;
}

[[nodiscard]] int CompareSkillDisplayEntries(
    const SkillLineInfoEntry& left,
    const SkillLineInfoEntry& right,
    const std::unordered_map<std::uint32_t, std::uint32_t>& category_order_by_id,
    const data::dbc::DbcLoader* dbc) {
  if (left.is_visible != right.is_visible) {
    return left.is_visible != 0 ? -1 : 1;
  }

  if (left.is_visible == 0) {
    return 0;
  }

  const auto left_order_it = category_order_by_id.find(left.category_id);
  const auto right_order_it = category_order_by_id.find(right.category_id);
  const auto left_order =
      (left_order_it != category_order_by_id.end()) ? left_order_it->second : 0u;
  const auto right_order =
      (right_order_it != category_order_by_id.end()) ? right_order_it->second : 0u;
  if (left_order != right_order) {
    return left_order < right_order ? -1 : 1;
  }

  const bool left_is_header = left.skill_id == 0;
  const bool right_is_header = right.skill_id == 0;
  if (left_is_header != right_is_header) {
    return left_is_header ? -1 : 1;
  }

  if (left.is_untrained != right.is_untrained) {
    return left.is_untrained == 0 ? -1 : 1;
  }

  const auto left_name = SkillLineName(dbc, left.skill_id);
  const auto right_name = SkillLineName(dbc, right.skill_id);
  if (!left_name.empty() && !right_name.empty()) {
    return openwow::core::SStrCmpNoCaseCollate(
        left_name.data(), right_name.data(), 0x7FFFFFFFu);
  }

  return 0;
}

}

std::uint32_t ResolveSkillRankCost(
    const data::dbc::DbcLoader& dbc,
    const data::dbc::SkillLineEntry& skill_line,
    const data::dbc::SkillRaceClassInfoEntry& race_class_info,
    const std::uint32_t rank) {
  if (skill_line.skill_cost_id == 0 || rank == 0) {
    return 0;
  }

  const auto& costs = dbc.skill_costs_data().entries();
  std::size_t visible_run_begin = costs.size();
  std::size_t visible_run_size = 0;
  for (std::size_t index = 0; index < costs.size(); ++index) {
    if (costs[index].skill_costs_id != skill_line.skill_cost_id) {
      continue;
    }

    if (index == 0 || costs[index - 1].skill_costs_id != skill_line.skill_cost_id) {
      visible_run_begin = index;
      visible_run_size = 1;
    } else {
      ++visible_run_size;
    }
  }

  if (visible_run_begin == costs.size() || rank > visible_run_size) {
    return 0;
  }

  const auto& cost_entry = costs[visible_run_begin + rank - 1];

  switch (race_class_info.skill_cost_index) {
    case 0:
      return cost_entry.cost0;
    case 1:
      return cost_entry.cost1;
    case 2:
      return cost_entry.cost2;
    default:
      return 0;
  }
}

SkillInfoStore& SkillInfoStore::Get() {
  static SkillInfoStore instance;
  return instance;
}

void SkillInfoStore::AllocateArrays(std::uint32_t num_skill_lines) {
  skill_line_capacity_ = num_skill_lines + kMaxPlayerSkillSlots;
  skill_entries_.assign(skill_line_capacity_, {});
  for (auto& entry : skill_entries_) {
    entry.is_visible = 1;
  }

  category_capacity_ = num_skill_lines;
  category_entries_.assign(category_capacity_, {});

  num_skill_lines_ = 0;
  num_categories_ = 0;
  num_visible_ = 0;
  selected_skill_id_ = 0;
  total_points_used_ = 0;
  expand_mask_ = 0xFFFFFFFF;
  owner_guid_ = 0;
  dbc_ = nullptr;
}

void SkillInfoStore::UpdateFromPlayer(const CGPlayer_C& player,
                                      const data::dbc::DbcLoader& dbc) {
  const std::uint64_t player_guid = player.GetGuid().GetRawValue();
  const bool same_owner = owner_guid_ != 0 && owner_guid_ == player_guid;
  std::unordered_map<std::uint32_t, std::uint32_t> queued_by_skill;
  if (same_owner) {
    queued_by_skill.reserve(num_skill_lines_);
    for (std::uint32_t index = 0; index < num_skill_lines_; ++index) {
      const auto& entry = skill_entries_[index];
      if (entry.skill_id != 0 && entry.queued_points != 0) {
        queued_by_skill.emplace(entry.skill_id, entry.queued_points);
      }
    }
  }
  const std::uint32_t previous_selected_skill = selected_skill_id_;
  const std::uint32_t previous_expand_mask = expand_mask_;

  const auto required_capacity = dbc.skill_line().size();
  if (skill_line_capacity_ < required_capacity + kMaxPlayerSkillSlots ||
      category_capacity_ < required_capacity) {
    AllocateArrays(required_capacity);
  }

  owner_guid_ = player_guid;
  dbc_ = &dbc;
  selected_skill_id_ = same_owner ? previous_selected_skill : 0;
  expand_mask_ = same_owner ? previous_expand_mask : 0xFFFFFFFF;
  num_skill_lines_ = 0;
  num_categories_ = 0;
  num_visible_ = 0;
  total_points_used_ = 0;

  for (std::uint32_t slot_index = 0; slot_index < kMaxPlayerSkillSlots; ++slot_index) {
    const auto skill = player.GetSkill(static_cast<std::uint16_t>(slot_index));
    if (skill.skill_id == 0) {
      continue;
    }

    const auto* skill_line = dbc.skill_line().LookupEntry(skill.skill_id);
    if (skill_line == nullptr || skill_line->category_id < 0) {
      continue;
    }

    const auto category_id = static_cast<std::uint32_t>(skill_line->category_id);
    if (dbc.skill_line_category().LookupEntry(category_id) == nullptr) {
      continue;
    }

    const auto* race_class_info = FindSkillRaceClassInfo(
        dbc, player.State().GetRace(), player.State().GetClass(), skill.skill_id);
    if (race_class_info == nullptr) {
      continue;
    }

    const auto flags = race_class_info->flags;
    if ((flags & 0x2u) != 0) {
      continue;
    }

    const bool passes_visibility_gate =
        (flags & 0x1u) != 0 ||
        skill.value != 0 ||
        (((flags & 0x4u) != 0) && player.State().GetLevel() >= race_class_info->min_level);
    if (!passes_visibility_gate) {
      continue;
    }

    std::uint32_t category_index = 0;
    while (category_index < num_categories_ &&
           category_entries_[category_index].category_id != category_id) {
      ++category_index;
    }

    const bool category_exists = category_index < num_categories_;
    const auto needed_entries = category_exists ? 1u : 2u;
    if (num_skill_lines_ + needed_entries > skill_line_capacity_) {
      continue;
    }
    if (!category_exists && num_categories_ >= category_capacity_) {
      continue;
    }

    auto& skill_entry = skill_entries_[num_skill_lines_++];
    skill_entry = {};
    skill_entry.skill_id = skill.skill_id;
    skill_entry.category_id = category_id;
    skill_entry.is_untrained = (skill.value == 0) ? 1u : 0u;
    if (const auto queued = queued_by_skill.find(skill.skill_id);
        queued != queued_by_skill.end()) {
      skill_entry.queued_points = queued->second;
    }

    if (!category_exists) {
      auto& category_entry = category_entries_[num_categories_++];
      category_entry = {};
      category_entry.category_id = category_id;

      auto& header_entry = skill_entries_[num_skill_lines_++];
      header_entry = {};
      header_entry.category_id = category_id;
      header_entry.is_visible = 1;
    }
  }

  RebuildDisplayList();
  RecalculateQueuedPointCosts(player, dbc);
}

void SkillInfoStore::RebuildDisplayList() {
  num_visible_ = num_skill_lines_;

  for (std::uint32_t category_index = 0; category_index < num_categories_; ++category_index) {
    category_entries_[category_index].is_collapsed =
        ((expand_mask_ & (1u << category_index)) == 0) ? 1u : 0u;
  }

  for (std::uint32_t entry_index = 0; entry_index < num_skill_lines_; ++entry_index) {
    auto& entry = skill_entries_[entry_index];
    if (entry.skill_id == 0) {
      entry.is_visible = 1;
      continue;
    }

    entry.is_visible = 1;
    for (std::uint32_t category_index = 0; category_index < num_categories_; ++category_index) {
      if (category_entries_[category_index].category_id != entry.category_id) {
        continue;
      }

      entry.is_visible =
          (category_entries_[category_index].is_collapsed == 0 &&
           (!hide_untrained_skills_ || entry.is_untrained == 0))
              ? 1u
              : 0u;
      if (entry.is_visible == 0) {
        --num_visible_;
      }
      break;
    }
  }

  std::stable_sort(category_entries_.begin(),
                   category_entries_.begin() + num_categories_,
                   [this](const SkillCategoryInfoEntry& left,
                          const SkillCategoryInfoEntry& right) {
                     return CompareSkillCategoriesByDisplayOrder(left, right, dbc_) < 0;
                   });

  std::unordered_map<std::uint32_t, std::uint32_t> category_order_by_id;
  category_order_by_id.reserve(num_categories_);
  for (std::uint32_t category_index = 0; category_index < num_categories_; ++category_index) {
    category_order_by_id.emplace(category_entries_[category_index].category_id,
                                 category_index);
  }

  std::stable_sort(skill_entries_.begin(),
                   skill_entries_.begin() + num_skill_lines_,
                   [this, &category_order_by_id](const SkillLineInfoEntry& left,
                                                 const SkillLineInfoEntry& right) {
                     return CompareSkillDisplayEntries(
                                left, right, category_order_by_id, dbc_) < 0;
                   });
}

std::optional<std::uint32_t> SkillInfoStore::FindCategoryIndexForSkillEntry(
    const std::uint32_t skill_index) const {
  if (skill_index >= num_skill_lines_) {
    return std::nullopt;
  }

  const auto category_id = skill_entries_[skill_index].category_id;
  for (std::uint32_t category_index = 0; category_index < num_categories_; ++category_index) {
    if (category_entries_[category_index].category_id != category_id) {
      continue;
    }

    return category_index;
  }

  return std::nullopt;
}

bool SkillInfoStore::CollapseCategory(std::uint32_t skill_index) {
  const auto category_index = FindCategoryIndexForSkillEntry(skill_index);
  if (!category_index.has_value()) {
    return false;
  }

  expand_mask_ &= ~(1u << *category_index);
  RebuildDisplayList();
  return true;
}

bool SkillInfoStore::ExpandCategory(std::uint32_t skill_index) {
  const auto category_index = FindCategoryIndexForSkillEntry(skill_index);
  if (!category_index.has_value()) {
    return false;
  }

  expand_mask_ |= (1u << *category_index);
  RebuildDisplayList();
  return true;
}

void SkillInfoStore::CollapseAllCategories() {
  expand_mask_ = 0;
  RebuildDisplayList();
}

void SkillInfoStore::ExpandAllCategories() {
  expand_mask_ = 0xFFFFFFFF;
  RebuildDisplayList();
}

bool SkillInfoStore::AddQueuedPoint(
    const std::uint32_t skill_index,
    const CGPlayer_C& player,
    const data::dbc::DbcLoader& dbc) {
  if (skill_index >= num_visible_) {
    return false;
  }

  auto& entry = skill_entries_[skill_index];
  if (entry.skill_id == 0) {
    return false;
  }

  const auto context = BuildSkillQueueCostContext(player, dbc, entry.skill_id);
  if (!context.has_value()) {
    return false;
  }

  if (SkillBaseRankForQueue(context->skill) + entry.queued_points >=
      SkillMaxRankForQueue(context->skill)) {
    return false;
  }

  const auto next_rank =
      SkillBaseRankForQueue(context->skill) + entry.queued_points + 1u;
  const auto point_cost = ResolveSkillRankCost(
      dbc, *context->skill_line, *context->race_class_info, next_rank);
  const auto available_points = player.GetProfessionPoints();
  if (total_points_used_ > available_points ||
      point_cost > available_points - total_points_used_) {
    return false;
  }

  ++entry.queued_points;
  total_points_used_ += point_cost;
  return true;
}

bool SkillInfoStore::RemoveQueuedPoint(
    const std::uint32_t skill_index,
    const CGPlayer_C& player,
    const data::dbc::DbcLoader& dbc) {
  if (skill_index >= num_visible_) {
    return false;
  }

  auto& entry = skill_entries_[skill_index];
  if (entry.skill_id == 0 || entry.queued_points == 0) {
    return false;
  }

  const auto context = BuildSkillQueueCostContext(player, dbc, entry.skill_id);
  if (!context.has_value()) {
    return false;
  }

  const auto refunded_rank =
      SkillBaseRankForQueue(context->skill) + entry.queued_points;
  const auto point_cost = ResolveSkillRankCost(
      dbc, *context->skill_line, *context->race_class_info, refunded_rank);
  --entry.queued_points;
  total_points_used_ =
      point_cost <= total_points_used_ ? total_points_used_ - point_cost : 0;
  return true;
}

void SkillInfoStore::ClearQueuedPoints() {
  for (std::uint32_t index = 0; index < num_skill_lines_; ++index) {
    skill_entries_[index].queued_points = 0;
  }
  total_points_used_ = 0;
}

std::uint32_t SkillInfoStore::GetQueuedPointsForSkill(
    const std::uint32_t skill_id) const {
  for (std::uint32_t index = 0; index < num_skill_lines_; ++index) {
    const auto& entry = skill_entries_[index];
    if (entry.skill_id == skill_id) {
      return entry.queued_points;
    }
  }
  return 0;
}

std::vector<std::pair<std::uint32_t, std::uint32_t>>
SkillInfoStore::CollectQueuedPoints() const {
  std::vector<std::pair<std::uint32_t, std::uint32_t>> queued_points;

  for (std::uint32_t index = 0; index < num_visible_; ++index) {
    const auto& entry = skill_entries_[index];
    if (entry.skill_id != 0 && entry.queued_points != 0) {
      queued_points.emplace_back(entry.skill_id, entry.queued_points);
    }
  }
  return queued_points;
}

void SkillInfoStore::RecalculateQueuedPointCosts(
    const CGPlayer_C& player,
    const data::dbc::DbcLoader& dbc) {
  std::uint64_t total_cost = 0;
  for (std::uint32_t index = 0; index < num_skill_lines_; ++index) {
    auto& entry = skill_entries_[index];
    if (entry.skill_id == 0 || entry.queued_points == 0) {
      continue;
    }

    const auto context = BuildSkillQueueCostContext(player, dbc, entry.skill_id);
    if (!context.has_value()) {
      entry.queued_points = 0;
      continue;
    }

    const auto current_rank = SkillBaseRankForQueue(context->skill);
    const auto maximum_rank = SkillMaxRankForQueue(context->skill);
    const auto remaining_ranks =
        maximum_rank > current_rank ? maximum_rank - current_rank : 0u;
    entry.queued_points = std::min(entry.queued_points, remaining_ranks);
    const auto base_rank = SkillBaseRankForQueue(context->skill);
    for (std::uint32_t queued_rank = 0;
         queued_rank < entry.queued_points;
         ++queued_rank) {
      total_cost += ResolveSkillRankCost(
          dbc,
          *context->skill_line,
          *context->race_class_info,
          base_rank + queued_rank + 1u);
    }
  }

  total_points_used_ = static_cast<std::uint32_t>(std::min<std::uint64_t>(
      total_cost, std::numeric_limits<std::uint32_t>::max()));
}

std::int32_t SkillInfoStore::GetSelectedSkillIndex() const {
  if (selected_skill_id_ == 0) {
    return -1;
  }

  for (std::uint32_t index = 0; index < num_visible_; ++index) {
    if (skill_entries_[index].skill_id == selected_skill_id_) {
      return static_cast<std::int32_t>(index);
    }
  }

  return -1;
}

void SkillInfoStore::SetSelectedSkillEntryIndex(const std::uint32_t entry_index) {
  if (entry_index >= num_skill_lines_) {
    selected_skill_id_ = 0;
    return;
  }

  selected_skill_id_ = skill_entries_[entry_index].skill_id;
}

std::uint32_t SkillInfoStore::GetCategoryCollapseState(std::uint32_t skill_index) const {
  const auto category_index = FindCategoryIndexForSkillEntry(skill_index);
  if (!category_index.has_value()) {
    return 0;
  }

  return category_entries_[*category_index].is_collapsed;
}

std::int16_t SkillInfoStore::GetSkillModifier(std::uint32_t slot_index,
                                              const std::uint16_t* modifier_fields) const {
  if (modifier_fields == nullptr || slot_index >= kMaxPlayerSkillSlots) {
    return 0;
  }

  return static_cast<std::int16_t>(modifier_fields[3 * slot_index + 490]);
}

const SkillLineInfoEntry* SkillInfoStore::GetSkillEntry(std::uint32_t index) const {
  if (index >= num_skill_lines_) {
    return nullptr;
  }

  return &skill_entries_[index];
}

const SkillCategoryInfoEntry* SkillInfoStore::GetCategoryEntry(std::uint32_t index) const {
  if (index >= num_categories_) {
    return nullptr;
  }

  return &category_entries_[index];
}

void SkillInfoStore::FreeAllEntries() {
  for (auto& entry : skill_entries_) {
    entry = {};
  }

  for (auto& category : category_entries_) {
    category = {};
  }

  num_skill_lines_ = 0;
  num_categories_ = 0;
  num_visible_ = 0;
  selected_skill_id_ = 0;
  total_points_used_ = 0;
  owner_guid_ = 0;
}

void SkillInfoStore::Reset() {
  skill_entries_.clear();
  category_entries_.clear();
  num_skill_lines_ = 0;
  skill_line_capacity_ = 0;
  num_categories_ = 0;
  category_capacity_ = 0;
  num_visible_ = 0;
  selected_skill_id_ = 0;
  total_points_used_ = 0;
  expand_mask_ = 0xFFFFFFFF;
  owner_guid_ = 0;
  dbc_ = nullptr;
  hide_untrained_skills_ = false;
}

}

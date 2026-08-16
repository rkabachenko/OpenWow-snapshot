
#include "openwow/game/talent_info.h"

#include "openwow/data/formats/dbc/dbc_loader.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

namespace openwow::game {

namespace {

constexpr uint32_t kTalentGroupStorageCapacity = 2;
constexpr uint32_t kTalentGlyphSlotCount = 6;

TalentInfoEntry BuildTalentEntryFromDBC(const TalentDBC &talent) {
  TalentInfoEntry entry{};
  entry.talent_id = talent.talent_id;
  entry.tab_id = talent.tab_id;
  entry.max_rank = talent.max_rank;
  entry.tier = talent.tier;
  entry.column = talent.column;
  entry.spell_ids = talent.spell_ids;
  entry.prereq_talent = talent.prereq_talent;
  entry.prereq_rank = talent.prereq_rank;
  entry.flags = talent.flags;
  entry.required_spell_id = talent.required_spell_id;
  entry.pet_talent_mask_words = talent.pet_talent_mask_words;
  return entry;
}

uint32_t GetTalentSpellRankCapacity(const TalentInfoEntry &talent) {

  for (std::size_t index = talent.spell_ids.size(); index != 0; --index) {
    if (talent.spell_ids[index - 1] != 0) {
      return static_cast<uint32_t>(index);
    }
  }
  return 0;
}

uint32_t TalentRankPointCount(const int32_t raw_rank) {
  return raw_rank < 0 ? 0u : static_cast<uint32_t>(raw_rank + 1);
}

int32_t AddI32Wrapping(const int32_t lhs, const int32_t rhs) {
  const auto bits = static_cast<uint32_t>(lhs) + static_cast<uint32_t>(rhs);
  if (bits <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
    return static_cast<int32_t>(bits);
  }
  return std::numeric_limits<int32_t>::min() +
         static_cast<int32_t>(bits - 0x80000000u);
}

bool MatchesPlayerTalentTab(const TalentTabDBC &tab, uint32_t class_id, uint32_t race_id) {
  if (class_id == 0 || class_id > 32) {
    return false;
  }

  const uint32_t class_bit = 1u << (class_id - 1);
  const uint32_t race_bit = (race_id >= 1 && race_id <= 32) ? (1u << (race_id - 1)) : 0;

  if (tab.class_mask != 0) {
    if ((tab.class_mask & class_bit) == 0) {
      return false;
    }
    return tab.race_mask == 0 || ((tab.race_mask & race_bit) != 0);
  }

  if (tab.race_mask == 0) {
    return false;
  }

  return (tab.race_mask & race_bit) != 0;
}

bool MatchesPetTalentTab(const TalentTabDBC &tab, int32_t pet_talent_type) {
  if (pet_talent_type < 0 || pet_talent_type >= 32) {
    return false;
  }
  if (tab.pet_mask == 0) {
    return false;
  }

  return (tab.pet_mask & (1u << pet_talent_type)) != 0;
}

void ReleaseTalentTabArrayStorage(std::vector<TalentTabEntry> *tabs) {
  if (tabs == nullptr) {
    return;
  }

  std::vector<TalentTabEntry>().swap(*tabs);
}

void ClearTalentGroupEntries(TalentGroupData *group) {
  if (group == nullptr) {
    return;
  }

  group->talent_by_id.clear();

  for (auto &tab : group->tabs) {
    tab.ClearTierEntries();
  }
  group->tabs.clear();
  group->unspent_points = 0;
  std::memset(group->glyph_ids, 0, sizeof(group->glyph_ids));
}

const TalentTabEntry *FindGroupTabById(const TalentGroupData &group, uint32_t tab_id) {
  for (const auto &tab : group.tabs) {
    if (tab.tab_id == tab_id) {
      return &tab;
    }
  }
  return nullptr;
}

TalentTabEntry *FindGroupTabById(TalentGroupData &group, uint32_t tab_id) {
  for (auto &tab : group.tabs) {
    if (tab.tab_id == tab_id) {
      return &tab;
    }
  }
  return nullptr;
}

TalentGroupTabInfo BuildTalentGroupTabInfo(const TalentTabEntry &tab) {
  TalentGroupTabInfo info{};
  info.tab_id = tab.tab_id;

  for (const auto &talent : tab.talents) {
    info.points_spent += TalentRankPointCount(talent.current_rank);
    info.preview_points_spent +=
        static_cast<int32_t>(talent.preview_rank) - static_cast<int32_t>(talent.current_rank);
  }
  return info;
}

TalentTabEntry BuildRuntimeGroupTab(const TalentTabEntry &static_tab,
                                    const std::unordered_map<uint32_t, TalentInfoEntry *> &entries) {
  TalentTabEntry runtime_tab{};
  runtime_tab.tab_id = static_tab.tab_id;
  runtime_tab.talents.reserve(static_tab.talents.size());

  for (const auto &static_talent : static_tab.talents) {
    const auto entry_it = entries.find(static_talent.talent_id);
    if (entry_it == entries.end() || entry_it->second == nullptr) {
      continue;
    }

    runtime_tab.talents.push_back(*entry_it->second);
  }

  runtime_tab.talent_count = static_cast<uint32_t>(runtime_tab.talents.size());
  return runtime_tab;
}

void RebuildGroupTabState(TalentGroupData *group, const std::vector<TalentTabEntry> &static_tabs) {
  if (group == nullptr) {
    return;
  }

  std::vector<TalentTabEntry> rebuilt_tabs;
  rebuilt_tabs.reserve(static_tabs.size());

  for (const auto &static_tab : static_tabs) {
    rebuilt_tabs.push_back(BuildRuntimeGroupTab(static_tab, group->talent_by_id));
  }

  group->tabs = std::move(rebuilt_tabs);
  group->RebuildIndex();
}

}

void TalentTabEntry::ClearTierEntries() {
  talents.clear();
  talent_count = 0;
}

void TalentGroupData::RebuildIndex() {
  talent_by_id.clear();
  for (auto &tab : tabs) {
    for (auto &t : tab.talents) {
      talent_by_id[t.talent_id] = &t;
    }
  }
}

uint16_t TalentGroupData::GetGlyph(uint32_t slot_index) const {
  if (slot_index >= 6) {
    return 6;
  }
  return glyph_ids[slot_index];
}

std::optional<TalentGroupTabInfo> TalentGroupData::FindTabInfoById(const uint32_t tab_id) const {
  const auto *tab = FindGroupTabById(*this, tab_id);
  if (tab == nullptr) {
    return std::nullopt;
  }
  return BuildTalentGroupTabInfo(*tab);
}

TalentInfoStore &TalentInfoStore::Get() {
  static TalentInfoStore instance;
  return instance;
}

uint32_t TalentInfoStore::GetGroupIndexArg(const std::optional<uint32_t> arg) const {
  if (!arg.has_value()) {
    return active_group_index_;
  }

  return *arg - 1u;
}

uint32_t TalentInfoStore::GetDefaultGroupIndex(const bool is_pet) const {
  return is_pet ? 0u : active_group_index_;
}

uint32_t TalentInfoStore::GetActiveGroupIndexForContext(const bool inspect,
                                                        const bool is_pet) const {
  if (inspect) {
    return inspect_active_group_index_;
  }
  return GetDefaultGroupIndex(is_pet);
}

bool TalentInfoStore::ValidatePetTalent(const TalentInfoEntry &talent,
                                        uint32_t creature_family) const {
  const CreatureFamilyTalentInfo *family_info = FindCreatureFamilyTalentInfo(creature_family);
  if (!family_info || !family_info->has_pet_talent_mask_index) {
    return false;
  }

  if (talent.pet_talent_mask_words[0] == 0 && talent.pet_talent_mask_words[1] == 0) {
    return true;
  }

  const int32_t mask_index = family_info->pet_talent_mask_index;
  if (mask_index < 0) {
    return true;
  }

  const std::size_t word_index = static_cast<std::size_t>(mask_index / 32);
  if (word_index >= talent.pet_talent_mask_words.size()) {
    return false;
  }

  const uint32_t mask = 1u << (mask_index & 31);
  return (talent.pet_talent_mask_words[word_index] & mask) != 0;
}

TalentInfoEntry *TalentInfoStore::FindInSortedTab(TalentTabEntry &tab, uint32_t talent_id) {
  if (tab.talents.empty())
    return nullptr;
  int lo = 0;
  int hi = static_cast<int>(tab.talents.size()) - 1;
  while (lo <= hi) {
    int mid = static_cast<int>(static_cast<uint32_t>(lo + hi) >> 1);
    auto &entry = tab.talents[mid];
    if (entry.talent_id == talent_id)
      return &entry;
    if (entry.talent_id < talent_id) {
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return nullptr;
}

const TalentInfoEntry *TalentInfoStore::FindInSortedTab(const TalentTabEntry &tab,
                                                        uint32_t talent_id) const {
  if (tab.talents.empty())
    return nullptr;
  int lo = 0;
  int hi = static_cast<int>(tab.talents.size()) - 1;
  while (lo <= hi) {
    int mid = static_cast<int>(static_cast<uint32_t>(lo + hi) >> 1);
    const auto &entry = tab.talents[mid];
    if (entry.talent_id == talent_id)
      return &entry;
    if (entry.talent_id < talent_id) {
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return nullptr;
}

const TalentTabEntry *TalentInfoStore::FindGroupTabByTalentId(const TalentGroupData &group,
                                                              const uint32_t talent_id) const {
  const TalentDBC *dbc_entry = FindTalentDBC(talent_id);
  if (dbc_entry == nullptr) {
    return nullptr;
  }

  return FindGroupTabById(group, dbc_entry->tab_id);
}

TalentTabEntry *TalentInfoStore::FindGroupTabByTalentId(TalentGroupData &group,
                                                        const uint32_t talent_id) {
  return const_cast<TalentTabEntry *>(
      static_cast<const TalentInfoStore *>(this)->FindGroupTabByTalentId(group, talent_id));
}

const TalentDBC *TalentInfoStore::FindTalentDBC(uint32_t talent_id) const {
  auto it =
      std::find_if(talent_dbc_.begin(), talent_dbc_.end(),
                   [talent_id](const TalentDBC &talent) { return talent.talent_id == talent_id; });
  if (it == talent_dbc_.end())
    return nullptr;
  return &*it;
}

bool TalentInfoStore::ParseGroupsFromPacket(const uint8_t *data, size_t len,
                                            std::vector<TalentGroupData> *groups,
                                            uint32_t max_groups, uint32_t *out_unspent,
                                            uint32_t *out_active_group,
                                            uint32_t *out_num_groups) {
  if (out_active_group != nullptr) {
    *out_active_group = 0;
  }
  if (out_num_groups != nullptr) {
    *out_num_groups = 0;
  }
  if (out_unspent != nullptr) {
    *out_unspent = 0;
  }
  if (data == nullptr || groups == nullptr) {
    return false;
  }

  size_t pos = 0;
  const auto can_read = [&](size_t width) { return width <= len - std::min(pos, len); };
  const auto read_u8 = [&](uint8_t *out) -> bool {
    if (out == nullptr || !can_read(sizeof(uint8_t))) {
      return false;
    }
    *out = data[pos];
    ++pos;
    return true;
  };
  const auto read_u16 = [&](uint16_t *out) -> bool {
    if (out == nullptr || !can_read(sizeof(uint16_t))) {
      return false;
    }
    *out = static_cast<uint16_t>(data[pos] |
                                 (static_cast<uint16_t>(data[pos + 1]) << 8));
    pos += sizeof(uint16_t);
    return true;
  };
  const auto read_u32 = [&](uint32_t *out) -> bool {
    if (out == nullptr || !can_read(sizeof(uint32_t))) {
      return false;
    }
    *out = static_cast<uint32_t>(data[pos]) |
           (static_cast<uint32_t>(data[pos + 1]) << 8) |
           (static_cast<uint32_t>(data[pos + 2]) << 16) |
           (static_cast<uint32_t>(data[pos + 3]) << 24);
    pos += sizeof(uint32_t);
    return true;
  };

  uint32_t unspent = 0;
  if (!read_u32(&unspent)) {
    return false;
  }
  if (out_unspent != nullptr) {
    *out_unspent = (static_cast<int32_t>(unspent) < 0) ? 0u : unspent;
  }

  uint8_t num_groups = 0;
  if (!read_u8(&num_groups)) {
    return false;
  }
  if (out_num_groups != nullptr) {
    *out_num_groups = num_groups;
  }

  uint8_t active_group = 0;
  if (!read_u8(&active_group)) {
    return false;
  }
  if (out_active_group != nullptr) {
    *out_active_group = active_group;
  }

  groups->clear();
  groups->resize(std::min<uint32_t>(num_groups, max_groups));

  for (uint32_t group_index = 0; group_index < num_groups; ++group_index) {
    TalentGroupData *group =
        group_index < groups->size() ? &(*groups)[group_index] : nullptr;

    uint8_t num_talents = 0;
    if (!read_u8(&num_talents)) {
      return false;
    }
    for (uint32_t talent_index = 0; talent_index < num_talents; ++talent_index) {
      uint32_t talent_id = 0;
      uint8_t rank = 0;
      if (!read_u32(&talent_id) || !read_u8(&rank)) {
        return false;
      }

      if (group == nullptr) {
        continue;
      }

      const auto* definition = FindTalentDBC(talent_id);
      if (definition == nullptr || rank >= definition->spell_ids.size() ||
          definition->spell_ids[rank] == 0) {
        continue;
      }

      TalentInfoEntry *entry = EnsureTalentEntry(*group, talent_id);
      if (entry == nullptr) {
        continue;
      }

      entry->current_rank = rank;
      entry->preview_rank = rank;
    }

    uint8_t num_glyphs = 0;
    if (!read_u8(&num_glyphs)) {
      return false;
    }
    for (uint32_t glyph_index = 0; glyph_index < num_glyphs; ++glyph_index) {
      uint16_t glyph_id = 0;
      if (!read_u16(&glyph_id)) {
        return false;
      }
      if (group != nullptr && glyph_index < kTalentGlyphSlotCount) {
        group->glyph_ids[glyph_index] = glyph_id;
      }
    }

    if (group == nullptr) {
      continue;
    }

    for (auto &tab : group->tabs) {
      tab.talent_count = static_cast<uint32_t>(tab.talents.size());
    }
    group->RebuildIndex();
  }
  return true;
}

void TalentInfoStore::RecalcGroupUnspentPoints(std::vector<TalentGroupData> *groups,
                                               uint32_t active_group,
                                               uint32_t active_group_unspent_points,
                                               const std::vector<TalentTabEntry> *static_tabs) {
  if (!groups || groups->empty()) {
    return;
  }

  const auto count_committed_points = [](const TalentGroupData &group) {
    uint32_t spent = 0;
    for (const auto &tab : group.tabs) {
      for (const auto &talent : tab.talents) {
        spent += TalentRankPointCount(talent.current_rank);
      }
    }
    return spent;
  };

  if (static_tabs != nullptr && !static_tabs->empty()) {
    for (auto &group : *groups) {
      RebuildGroupTabState(&group, *static_tabs);
    }
  }

  for (auto &group : *groups) {
    for (auto &tab : group.tabs) {
      for (auto &talent : tab.talents) {
        talent.preview_rank = talent.current_rank;
      }
    }
  }

  uint32_t total_available_points = active_group_unspent_points;
  if (active_group < groups->size()) {
    auto &current_group = (*groups)[active_group];
    current_group.unspent_points = active_group_unspent_points;
    total_available_points += count_committed_points(current_group);
  }

  for (uint32_t group_index = 0; group_index < groups->size(); ++group_index) {
    if (group_index == active_group) {
      continue;
    }

    const uint32_t spent = count_committed_points((*groups)[group_index]);

    const int32_t unspent =
        static_cast<int32_t>(total_available_points) - static_cast<int32_t>(spent);
    (*groups)[group_index].unspent_points = (unspent < 0) ? 0u : static_cast<uint32_t>(unspent);
  }
}

TalentTabEntry *TalentInfoStore::GetOrCreateTabById(TalentGroupData &group, uint32_t tab_id) {
  if (TalentTabEntry *tab = FindGroupTabById(group, tab_id); tab != nullptr) {
    return tab;
  }

  auto it = std::find_if(group.tabs.begin(), group.tabs.end(),
                         [tab_id](const TalentTabEntry &tab) { return tab.tab_id > tab_id; });

  TalentTabEntry tab{};
  tab.tab_id = tab_id;
  it = group.tabs.insert(it, std::move(tab));
  return &*it;
}

TalentInfoEntry *TalentInfoStore::EnsureTalentEntry(TalentGroupData &group, uint32_t talent_id) {
  const TalentDBC *dbc_entry = FindTalentDBC(talent_id);
  if (!dbc_entry)
    return nullptr;

  TalentTabEntry *tab = GetOrCreateTabById(group, dbc_entry->tab_id);
  for (auto &entry : tab->talents) {
    if (entry.talent_id == talent_id)
      return &entry;
  }

  tab->talents.push_back(BuildTalentEntryFromDBC(*dbc_entry));
  tab->talent_count = static_cast<uint32_t>(tab->talents.size());
  return &tab->talents.back();
}

int32_t TalentInfoStore::CountTotalPreviewPointsSpent(bool is_pet) const {
  if (is_pet) {
    if (!HasPetTalentData() || pet_tab_array_.empty()) {
      return 0;
    }
    return GetPreviewPointsSpent(0, true);
  }

  int32_t total = 0;
  for (uint32_t group_index = 0; group_index < num_groups_; ++group_index) {
    total += GetPreviewPointsSpent(group_index, false);
  }
  return total;
}

const TalentTabEntry *TalentInfoStore::GetTalentTabArray(uint32_t index, bool inspect,
                                                         bool is_pet) const {
  if (inspect) {
    if (is_pet)
      return nullptr;

    if (index >= inspect_tab_array_.size())
      return nullptr;
    return &inspect_tab_array_[index];
  }
  if (is_pet) {
    if (index >= pet_tab_array_.size())
      return nullptr;
    return &pet_tab_array_[index];
  }
  if (index >= player_tab_array_.size())
    return nullptr;
  return &player_tab_array_[index];
}

uint32_t TalentInfoStore::GetTalentTabCount(bool inspect, bool is_pet) const {
  if (inspect) {
    return is_pet ? 0u : static_cast<uint32_t>(inspect_tab_array_.size());
  }

  if (is_pet) {
    return static_cast<uint32_t>(pet_tab_array_.size());
  }

  return static_cast<uint32_t>(player_tab_array_.size());
}

uint32_t TalentInfoStore::GetTalentGroupCount(bool inspect, bool is_pet) const {
  if (inspect) {
    return inspect_group_count_;
  }

  if (is_pet) {
    return HasPetTalentData() && !pet_tab_array_.empty() ? 1u : 0u;
  }

  return num_groups_;
}

uint32_t TalentInfoStore::GetUnspentPointsForGroup(uint32_t group_index, bool is_pet) const {
  if (is_pet) {
    if (!HasPetTalentData() || pet_tab_array_.empty())
      return 0;
    if (group_index >= 1)
      return 0;
    return pet_group_.unspent_points;
  }
  if (group_index >= num_groups_)
    return 0;
  if (group_index >= player_groups_.size())
    return 0;
  return player_groups_[group_index].unspent_points;
}

TalentGroupData *TalentInfoStore::GetGroupByIndex(uint32_t index, bool is_pet) {
  if (is_pet) {
    if (!HasPetTalentData() || pet_tab_array_.empty())
      return nullptr;
    if (index >= 1)
      return nullptr;
    return &pet_group_;
  }
  if (index >= num_groups_)
    return nullptr;
  if (index >= player_groups_.size())
    return nullptr;
  return &player_groups_[index];
}

const TalentGroupData *TalentInfoStore::GetGroupByIndex(uint32_t index, bool is_pet) const {
  if (is_pet) {
    if (!HasPetTalentData() || pet_tab_array_.empty())
      return nullptr;
    if (index >= 1)
      return nullptr;
    return &pet_group_;
  }
  if (index >= num_groups_)
    return nullptr;
  if (index >= player_groups_.size())
    return nullptr;
  return &player_groups_[index];
}

int32_t TalentInfoStore::GetPreviewPointsSpent(uint32_t group_index, bool is_pet) const {
  const auto *group = GetGroupByIndex(group_index, is_pet);
  if (!group)
    return 0;
  int32_t total = 0;
  for (const auto &tab : group->tabs) {
    for (const auto &t : tab.talents) {
      total += static_cast<int32_t>(t.preview_rank) - static_cast<int32_t>(t.current_rank);
    }
  }
  return total;
}

const TalentGroupData *TalentInfoStore::GetTalentGroupData(uint32_t index, bool inspect,
                                                           bool is_pet) const {
  if (!inspect) {
    return GetGroupByIndex(index, is_pet);
  }

  if (is_pet)
    return nullptr;
  if (index >= inspect_group_count_)
    return nullptr;
  if (index >= inspect_groups_.size())
    return nullptr;
  return &inspect_groups_[index];
}

void TalentInfoStore::BuildTabsFromDBC(uint32_t class_id, uint32_t creature_family, bool is_pet,
                                       uint32_t race_id, bool inspect) {
  auto &target = inspect ? inspect_tab_array_ : (is_pet ? pet_tab_array_ : player_tab_array_);
  int32_t pet_talent_type = -1;
  if (is_pet) {
    pet_talent_type = ResolvePetTalentType(creature_family);
    if (pet_talent_type < 0) {
      return;
    }
  }
  RebuildTalentTabArray(target, class_id, race_id, pet_talent_type);
}

void TalentInfoStore::RebuildTalentTabArray(std::vector<TalentTabEntry> &target, uint32_t class_id,
                                            uint32_t race_id, int32_t pet_talent_type) {
  std::vector<const TalentTabDBC *> matching_tabs;
  matching_tabs.reserve(talent_tab_dbc_.size());

  for (const auto &tab : talent_tab_dbc_) {
    const bool matches = (pet_talent_type >= 0) ? MatchesPetTalentTab(tab, pet_talent_type)
                                                : MatchesPlayerTalentTab(tab, class_id, race_id);
    if (matches) {
      matching_tabs.push_back(&tab);
    }
  }

  target.clear();
  target.reserve(matching_tabs.size());

  std::unordered_map<uint32_t, std::size_t> tab_index_by_id;
  tab_index_by_id.reserve(matching_tabs.size());

  for (const TalentTabDBC *tab : matching_tabs) {
    TalentTabEntry entry{};
    entry.tab_id = tab->tab_id;
    target.push_back(std::move(entry));
    tab_index_by_id.emplace(tab->tab_id, target.size() - 1);
  }

  TalentTabEntry *active_tab = nullptr;
  uint32_t active_tab_id = 0;
  bool have_active_tab = false;

  for (const auto &talent : talent_dbc_) {
    if (have_active_tab && talent.tab_id == active_tab_id) {
      if (active_tab != nullptr) {
        active_tab->talents.push_back(BuildTalentEntryFromDBC(talent));
        active_tab->talent_count = static_cast<uint32_t>(active_tab->talents.size());
      }
      continue;
    }

    active_tab_id = talent.tab_id;
    have_active_tab = true;
    active_tab = nullptr;

    const auto tab_it = tab_index_by_id.find(talent.tab_id);
    if (tab_it == tab_index_by_id.end()) {
      continue;
    }

    active_tab = &target[tab_it->second];
    active_tab->talents.clear();
    active_tab->talents.push_back(BuildTalentEntryFromDBC(talent));
    active_tab->talent_count = 1;
  }
}

int32_t TalentInfoStore::ResolvePetTalentType(uint32_t creature_family) const {
  const CreatureFamilyTalentInfo *family_info = FindCreatureFamilyTalentInfo(creature_family);
  if (!family_info || !family_info->has_pet_talent_type) {
    return -1;
  }
  return family_info->pet_talent_type;
}

const CreatureFamilyTalentInfo *
TalentInfoStore::FindCreatureFamilyTalentInfo(uint32_t creature_family) const {
  const auto it = creature_family_talent_info_.find(creature_family);
  if (it == creature_family_talent_info_.end()) {
    return nullptr;
  }
  return &it->second;
}

void TalentInfoStore::FreeTabArrays(bool is_pet) {
  if (is_pet) {
    ReleaseTalentTabArrayStorage(&pet_tab_array_);
  } else {
    ReleaseTalentTabArrayStorage(&player_tab_array_);
  }
}

void TalentInfoStore::SetPetTalentCreatureFamily(const uint32_t creature_family) {
  if (creature_family != 0) {
    if (pet_talent_creature_family_ != creature_family) {
      BuildTabsFromDBC(0, creature_family, true);
      RebuildGroupTabState(&pet_group_, pet_tab_array_);
    }
  } else {
    FreeTabArrays(true);
    ClearTalentGroupEntries(&pet_group_);
  }

  pet_talent_creature_family_ = creature_family;
}

void TalentInfoStore::ResetPreviewForTab(uint32_t group_index, uint32_t tab_index, bool is_pet) {
  auto *group = GetGroupByIndex(group_index, is_pet);
  if (!group)
    return;

  const auto *static_tab = GetTalentTabArray(tab_index, false, is_pet);
  if (static_tab == nullptr)
    return;

  auto *runtime_tab = FindGroupTabById(*group, static_tab->tab_id);
  if (runtime_tab == nullptr)
    return;

  for (auto &t : runtime_tab->talents) {
    t.preview_rank = t.current_rank;
  }
}

void TalentInfoStore::ResetPreviewForAllTabs(uint32_t group_index, bool is_pet) {
  auto *group = GetGroupByIndex(group_index, is_pet);
  if (!group)
    return;
  for (auto &tab : group->tabs) {
    for (auto &t : tab.talents) {
      t.preview_rank = t.current_rank;
    }
  }
}

void TalentInfoStore::ResetAllPreviewState(bool is_pet) {
  if (is_pet) {
    if (HasPetTalentData() && !pet_tab_array_.empty()) {
      ResetPreviewForAllTabs(0, true);
    }
    return;
  }

  for (uint32_t group_index = 0; group_index < num_groups_; ++group_index) {
    ResetPreviewForAllTabs(group_index, false);
  }
}

TalentInfoEntry *TalentInfoStore::FindTalentByID(uint32_t group_index, uint32_t talent_id,
                                                 bool is_pet) {
  auto *group = GetGroupByIndex(group_index, is_pet);
  if (!group)
    return nullptr;
  auto it = group->talent_by_id.find(talent_id);
  if (it != group->talent_by_id.end())
    return it->second;
  return nullptr;
}

std::optional<TalentInfoEntry> TalentInfoStore::FindTalentDefinitionByID(
    const uint32_t talent_id) const {
  const auto *dbc_talent = FindTalentDBC(talent_id);
  if (dbc_talent == nullptr) {
    return std::nullopt;
  }

  return BuildTalentEntryFromDBC(*dbc_talent);
}

void TalentInfoStore::LoadFromDbc(const openwow::data::dbc::DbcLoader &dbc) {
  ClearDBC();

  for (const auto &tab : dbc.talent_tab().entries()) {
    TalentTabDBC entry{};
    entry.tab_id = tab.id;
    entry.order_index = tab.order_index;
    entry.class_mask = tab.class_mask;
    entry.race_mask = tab.race_mask;
    entry.pet_mask = tab.pet_talent_mask;
    RegisterTalentTabDBC(entry);
  }

  for (const auto &talent : dbc.talent().entries()) {
    TalentDBC entry{};
    entry.talent_id = talent.id;
    entry.tab_id = talent.tab_id;
    entry.tier = talent.tier_id;
    entry.column = talent.column_index;
    entry.spell_ids = talent.spell_rank;
    entry.prereq_talent = talent.prereq_talent;
    entry.prereq_rank = talent.prereq_rank;
    entry.flags = talent.flags;
    entry.required_spell_id = talent.required_spell_id;
    entry.pet_talent_mask_words = talent.pet_talent_mask;

    entry.max_rank = 0;
    for (std::size_t rank = entry.spell_ids.size(); rank != 0; --rank) {
      if (entry.spell_ids[rank - 1] != 0) {
        entry.max_rank = static_cast<uint32_t>(rank);
        break;
      }
    }
    RegisterTalentDBC(entry);
  }

  for (const auto &family : dbc.creature_family().entries()) {
    RegisterCreatureFamilyTalentInfo(family.id, family.pet_talent_type,
                                     static_cast<int32_t>(family.category));
  }
}

void TalentInfoStore::RegisterTalentTabDBC(const TalentTabDBC &tab) {
  talent_tab_dbc_.push_back(tab);
}

void TalentInfoStore::RegisterTalentDBC(const TalentDBC &talent) {
  talent_dbc_.push_back(talent);
}

void TalentInfoStore::RegisterCreatureFamilyTalentType(uint32_t creature_family,
                                                       int32_t pet_talent_type) {
  CreatureFamilyTalentInfo &info = creature_family_talent_info_[creature_family];
  info.pet_talent_type = pet_talent_type;
  info.has_pet_talent_type = true;
}

void TalentInfoStore::RegisterCreatureFamilyTalentMaskIndex(uint32_t creature_family,
                                                            int32_t pet_talent_mask_index) {
  CreatureFamilyTalentInfo &info = creature_family_talent_info_[creature_family];
  info.pet_talent_mask_index = pet_talent_mask_index;
  info.has_pet_talent_mask_index = true;
}

void TalentInfoStore::RegisterCreatureFamilyTalentInfo(uint32_t creature_family,
                                                       int32_t pet_talent_type,
                                                       int32_t pet_talent_mask_index) {
  CreatureFamilyTalentInfo &info = creature_family_talent_info_[creature_family];
  info.pet_talent_type = pet_talent_type;
  info.pet_talent_mask_index = pet_talent_mask_index;
  info.has_pet_talent_type = true;
  info.has_pet_talent_mask_index = true;
}

void TalentInfoStore::ClearDBC() {
  talent_tab_dbc_.clear();
  talent_dbc_.clear();
  creature_family_talent_info_.clear();
}

void TalentInfoStore::ResetForWorldLogout() {
  initialized_ = false;
  pet_talent_creature_family_ = 0;
  inspect_target_guid_ = 0;
}

void TalentInfoStore::SetNumGroups(uint32_t n) {
  num_groups_ = n;
  if (player_groups_.size() < n) {
    player_groups_.resize(n);
  }
}

uint32_t TalentInfoStore::GetNumGroups() const {
  return num_groups_;
}

void TalentInfoStore::SetPetTalentDataId(uint32_t value) {
  pet_talent_creature_family_ = value;
}

uint32_t TalentInfoStore::GetPetTalentDataId() const {
  return pet_talent_creature_family_;
}

bool TalentInfoStore::HasPetTalentData() const {
  return pet_talent_creature_family_ != 0;
}

void TalentInfoStore::SetActiveGroupIndex(uint32_t index) {
  if (index < num_groups_)
    active_group_index_ = index;
}

uint32_t TalentInfoStore::GetActiveGroupIndex() const {
  return active_group_index_;
}

void TalentInfoStore::ShutdownClearGameUiData() {
  FreeTabArrays(false);
  FreeTabArrays(true);
  ClearAllGroups(false);
  ClearTalentGroupEntries(&pet_group_);
  ClearInspectTabArray();
  ClearInspectGroupCache();
}

void TalentInfoStore::Reset() {
  ShutdownClearGameUiData();
  ResetForWorldLogout();
  active_group_unspent_points_ = 0;
  num_groups_ = 1;
  active_group_index_ = 0;
  player_groups_.clear();
  player_groups_.resize(1);
}

std::optional<TalentGroupTabInfo> TalentInfoStore::GetTabInfo(const TalentGroupData &group,
                                                              uint32_t tab_index) const {
  if (tab_index >= group.tabs.size()) {
    return std::nullopt;
  }

  return BuildTalentGroupTabInfo(group.tabs[tab_index]);
}

std::optional<TalentGroupTabInfo> TalentInfoStore::GetTalentGroupTabInfo(uint32_t group_index,
                                                                         uint32_t tab_index,
                                                                         bool inspect,
                                                                         bool is_pet) const {
  const auto *group = GetTalentGroupData(group_index, inspect, is_pet);
  if (!group) {
    return std::nullopt;
  }

  const auto *static_tab = GetTalentTabArray(tab_index, inspect, is_pet);
  if (static_tab == nullptr) {
    return std::nullopt;
  }

  if (const auto tab_info = group->FindTabInfoById(static_tab->tab_id); tab_info.has_value()) {
    return tab_info;
  }

  TalentGroupTabInfo empty_tab_info{};
  empty_tab_info.tab_id = static_tab->tab_id;
  return empty_tab_info;
}

TalentInfoEntry *TalentInfoStore::LookupTalentInGroup(uint32_t group_index, uint32_t talent_id,
                                                      bool inspect, bool is_pet) {
  if (!inspect) {
    return FindTalentByID(group_index, talent_id, is_pet);
  }
  if (is_pet)
    return nullptr;
  if (group_index >= inspect_group_count_)
    return nullptr;
  if (group_index >= inspect_groups_.size())
    return nullptr;
  auto &group = inspect_groups_[group_index];
  auto it = group.talent_by_id.find(talent_id);
  if (it != group.talent_by_id.end())
    return it->second;
  return nullptr;
}

bool TalentInfoStore::ParseFromPacket(const uint8_t *data, size_t len,
                                      uint32_t *out_unspent,
                                      uint32_t *out_active_group,
                                      uint32_t *out_num_groups) {
  std::vector<TalentGroupData> parsed_groups;
  uint32_t parsed_unspent = 0;
  uint32_t parsed_active_group = 0;
  uint32_t parsed_num_groups = 0;
  if (!ParseGroupsFromPacket(data, len, &parsed_groups,
                             kTalentGroupStorageCapacity, &parsed_unspent,
                             &parsed_active_group, &parsed_num_groups)) {
    return false;
  }
  player_groups_ = std::move(parsed_groups);
  for (auto &group : player_groups_) {
    group.RebuildIndex();
  }
  if (out_unspent != nullptr) {
    *out_unspent = parsed_unspent;
  }
  if (out_active_group != nullptr) {
    *out_active_group = parsed_active_group;
  }
  if (out_num_groups != nullptr) {
    *out_num_groups = parsed_num_groups;
  }
  return true;
}

void TalentInfoStore::ClearAllGroups(bool is_pet) {
  if (is_pet) {
    ClearTalentGroupEntries(&pet_group_);
    return;
  }

  for (auto &group : player_groups_) {
    ClearTalentGroupEntries(&group);
  }
  active_group_index_ = 0;
  num_groups_ = 0;
  active_group_unspent_points_ = 0;
}

void TalentInfoStore::RecalcUnspentPoints(uint32_t active_group,
                                          uint32_t active_group_unspent_points) {
  RecalcGroupUnspentPoints(&player_groups_, active_group, active_group_unspent_points,
                           &player_tab_array_);
}

void TalentInfoStore::InitFromPlayer(uint32_t class_id, uint32_t race_id) {
  if (!initialized_) {
    BuildTabsFromDBC(class_id, 0, false , race_id);
    initialized_ = true;
    RecalcUnspentPoints(active_group_index_, active_group_unspent_points_);
  }
}

bool TalentInfoStore::ProcessServerData(const uint8_t *data, size_t len) {
  if (data == nullptr) {
    return false;
  }

  std::vector<TalentGroupData> parsed_groups;
  uint32_t new_unspent = 0;
  uint32_t new_active = 0;
  uint32_t new_num_groups = 0;
  if (!ParseGroupsFromPacket(data, len, &parsed_groups,
                             kTalentGroupStorageCapacity, &new_unspent,
                             &new_active, &new_num_groups)) {
    return false;
  }

  player_groups_ = std::move(parsed_groups);
  for (auto &group : player_groups_) {
    group.RebuildIndex();
  }
  active_group_unspent_points_ = new_unspent;
  active_group_index_ = new_active;
  num_groups_ = new_num_groups;

  if (initialized_) {
    RecalcUnspentPoints(active_group_index_, active_group_unspent_points_);
  }
  return true;
}

bool TalentInfoStore::ProcessPetServerData(const uint8_t *data, size_t len) {
  if (data == nullptr) {
    return false;
  }

  TalentGroupData parsed_group;
  size_t pos = 0;
  const auto can_read = [&](const size_t width) {
    return width <= len - std::min(pos, len);
  };
  const auto read_u8 = [&](uint8_t *out) -> bool {
    if (out == nullptr || !can_read(sizeof(uint8_t))) {
      return false;
    }
    *out = data[pos];
    ++pos;
    return true;
  };
  const auto read_u32 = [&](uint32_t *out) -> bool {
    if (out == nullptr || !can_read(sizeof(uint32_t))) {
      return false;
    }
    *out = static_cast<uint32_t>(data[pos]) |
           (static_cast<uint32_t>(data[pos + 1]) << 8) |
           (static_cast<uint32_t>(data[pos + 2]) << 16) |
           (static_cast<uint32_t>(data[pos + 3]) << 24);
    pos += sizeof(uint32_t);
    return true;
  };

  if (!read_u32(&parsed_group.unspent_points)) {
    return false;
  }

  uint8_t num_talents = 0;
  if (!read_u8(&num_talents)) {
    return false;
  }
  for (uint32_t talent_index = 0; talent_index < num_talents; ++talent_index) {
    uint32_t talent_id = 0;
    uint8_t rank = 0;
    if (!read_u32(&talent_id) || !read_u8(&rank)) {
      return false;
    }

    const auto* definition = FindTalentDBC(talent_id);
    if (definition == nullptr || rank >= definition->spell_ids.size() ||
        definition->spell_ids[rank] == 0) {
      continue;
    }

    TalentInfoEntry *entry = EnsureTalentEntry(parsed_group, talent_id);
    if (entry == nullptr) {
      continue;
    }

    entry->current_rank = rank;
    entry->preview_rank = rank;
  }

  parsed_group.RebuildIndex();
  pet_group_ = std::move(parsed_group);
  pet_group_.RebuildIndex();

  if (HasPetTalentData() && !pet_tab_array_.empty()) {
    RebuildGroupTabState(&pet_group_, pet_tab_array_);
  }
  return true;
}

std::optional<int32_t>
TalentInfoStore::ResolvePreviewPointDelta(uint32_t group_index, uint32_t tab_index,
                                          uint32_t talent_index, int32_t delta,
                                          bool is_pet) const {
  if (delta == 0) {
    return std::nullopt;
  }

  const TalentGroupData *group = nullptr;
  if (is_pet) {
    if (!HasPetTalentData() || pet_tab_array_.empty())
      return std::nullopt;
    if (group_index >= 1)
      return std::nullopt;
    group = &pet_group_;
  } else {
    if (group_index >= num_groups_)
      return std::nullopt;
    if (group_index >= player_groups_.size())
      return std::nullopt;
    group = &player_groups_[group_index];
  }

  const auto *static_tab = GetTalentTabArray(tab_index, false, is_pet);
  if (group == nullptr || static_tab == nullptr)
    return std::nullopt;
  if (talent_index >= static_tab->talents.size())
    return std::nullopt;

  const TalentInfoEntry &static_talent = static_tab->talents[talent_index];
  const uint32_t points_per_tier = is_pet ? 3u : 5u;
  const int32_t total_preview_points = GetPreviewPointsSpent(group_index, is_pet);
  if (delta > 0 && static_cast<int32_t>(group->unspent_points) - total_preview_points <= 0) {
    return std::nullopt;
  }

  TalentGroupTabInfo tab_info{};
  tab_info.tab_id = static_tab->tab_id;
  if (const TalentTabEntry *runtime_tab = FindGroupTabByTalentId(*group, static_talent.talent_id);
      runtime_tab != nullptr) {
    tab_info = BuildTalentGroupTabInfo(*runtime_tab);
  }

  const int32_t tab_points_after_preview =
      static_cast<int32_t>(tab_info.points_spent) + tab_info.preview_points_spent;
  if (tab_points_after_preview <
      static_cast<int32_t>(static_talent.tier * points_per_tier)) {
    return std::nullopt;
  }

  for (std::size_t prereq_index = 0; prereq_index < static_talent.prereq_talent.size();
       ++prereq_index) {
    const uint32_t prereq_talent_id = static_talent.prereq_talent[prereq_index];
    if (prereq_talent_id == 0 || FindTalentDBC(prereq_talent_id) == nullptr) {
      continue;
    }

    const auto prereq_it = group->talent_by_id.find(prereq_talent_id);
    if (prereq_it == group->talent_by_id.end() || prereq_it->second == nullptr) {
      return std::nullopt;
    }

    const int32_t required_rank =
        static_cast<int32_t>(static_talent.prereq_rank[prereq_index]);
    if (prereq_it->second->preview_rank < required_rank) {
      return std::nullopt;
    }
  }

  const auto existing = group->talent_by_id.find(static_talent.talent_id);
  const TalentInfoEntry *talent =
      (existing != group->talent_by_id.end()) ? existing->second : nullptr;
  const int32_t committed_rank = talent ? talent->current_rank : -1;
  const int32_t preview_rank = talent ? talent->preview_rank : -1;
  const auto rank_capacity = GetTalentSpellRankCapacity(static_talent);
  if (rank_capacity == 0) {
    return std::nullopt;
  }
  const int32_t max_rank = static_cast<int32_t>(rank_capacity) - 1;

  const int32_t requested_preview_rank = AddI32Wrapping(preview_rank, delta);
  const int32_t target_preview_rank =
      std::clamp(requested_preview_rank, committed_rank, max_rank);
  const int32_t applied_delta = target_preview_rank - preview_rank;
  if (applied_delta == 0) {
    return std::nullopt;
  }

  if (applied_delta < 0) {
    const int32_t new_preview_rank = preview_rank + applied_delta;

    for (const auto &tab : group->tabs) {
      for (const auto &dependent : tab.talents) {
        if (dependent.preview_rank < 0) {
          continue;
        }

        for (std::size_t prereq_index = 0; prereq_index < dependent.prereq_talent.size();
             ++prereq_index) {
          if (dependent.prereq_talent[prereq_index] != static_talent.talent_id) {
            continue;
          }

          const int32_t required_rank =
              static_cast<int32_t>(dependent.prereq_rank[prereq_index]);
          if (new_preview_rank < required_rank) {
            return std::nullopt;
          }
        }
      }
    }

    const TalentTabEntry *runtime_tab = FindGroupTabByTalentId(*group, static_talent.talent_id);
    if (runtime_tab != nullptr) {
      std::array<int32_t, 32> points_by_tier = {};
      uint32_t max_tier = 0;
      for (const auto &tab_talent : runtime_tab->talents) {
        int32_t effective_preview_rank = static_cast<int32_t>(tab_talent.preview_rank);
        if (tab_talent.talent_id == static_talent.talent_id) {
          effective_preview_rank = new_preview_rank;
        }

        if (effective_preview_rank < 0) {
          continue;
        }

        if (tab_talent.tier < points_by_tier.size()) {
          points_by_tier[tab_talent.tier] += effective_preview_rank + 1;
          max_tier = std::max(max_tier, tab_talent.tier);
        }
      }

      int32_t points_in_lower_tiers = 0;
      for (uint32_t tier = 0; tier <= max_tier && tier < points_by_tier.size(); ++tier) {
        const int32_t points_in_tier = points_by_tier[tier];
        if (tier > static_talent.tier && points_in_tier > 0 &&
            points_in_lower_tiers < static_cast<int32_t>(tier * points_per_tier)) {
          return std::nullopt;
        }
        points_in_lower_tiers += points_in_tier;
      }
    }
  }

  return applied_delta;
}

void TalentInfoStore::AddPreviewPoints(uint32_t group_index, uint32_t tab_index,
                                       uint32_t talent_index, int32_t delta, bool is_pet) {
  const std::optional<int32_t> applied_delta =
      ResolvePreviewPointDelta(group_index, tab_index, talent_index, delta, is_pet);
  if (!applied_delta.has_value()) {
    return;
  }

  TalentGroupData *group = nullptr;
  if (is_pet) {
    if (group_index >= 1 || !HasPetTalentData() || pet_tab_array_.empty()) {
      return;
    }
    group = &pet_group_;
  } else {
    if (group_index >= num_groups_ || group_index >= player_groups_.size()) {
      return;
    }
    group = &player_groups_[group_index];
  }

  const auto *static_tab = GetTalentTabArray(tab_index, false, is_pet);
  if (group == nullptr || static_tab == nullptr || talent_index >= static_tab->talents.size()) {
    return;
  }

  const TalentInfoEntry &static_talent = static_tab->talents[talent_index];
  const auto existing = group->talent_by_id.find(static_talent.talent_id);
  TalentInfoEntry *talent = (existing != group->talent_by_id.end()) ? existing->second : nullptr;

  if (talent == nullptr) {
    talent = EnsureTalentEntry(*group, static_talent.talent_id);
    if (talent == nullptr) {
      return;
    }
    group->RebuildIndex();
    const auto refreshed = group->talent_by_id.find(static_talent.talent_id);
    if (refreshed == group->talent_by_id.end()) {
      return;
    }
    talent = refreshed->second;
  }

  const int32_t target_preview_rank =
      static_cast<int32_t>(talent->preview_rank) + *applied_delta;
  talent->preview_rank = target_preview_rank;
  group->talent_by_id[static_talent.talent_id] = talent;
}

void TalentInfoStore::InitInspectFromGuid(uint64_t target_guid, uint32_t class_id,
                                          uint32_t race_id) {
  ClearInspectTabArray();
  if (target_guid == 0 || class_id == 0) {
    return;
  }

  BuildTabsFromDBC(class_id, 0, false , race_id, true );
  inspect_target_guid_ = target_guid;
  if (inspect_target_guid_ != 0) {
    RecalcGroupUnspentPoints(&inspect_groups_, inspect_active_group_index_,
                             inspect_active_group_unspent_points_, &inspect_tab_array_);
  }
}

void TalentInfoStore::ClearInspectTabArray() {
  ReleaseTalentTabArrayStorage(&inspect_tab_array_);
}

void TalentInfoStore::ClearInspectGroupCache() {
  inspect_groups_.clear();
  inspect_active_group_index_ = 0;
  inspect_group_count_ = 0;
  inspect_active_group_unspent_points_ = 0;
}

void TalentInfoStore::ClearInspectData() {
  ClearInspectGroupCache();
  ClearInspectTabArray();
  inspect_target_guid_ = 0;
}

uint64_t TalentInfoStore::GetInspectTargetGuid() const {
  return inspect_target_guid_;
}

void TalentInfoStore::ParseInspectTalentPacket(const uint8_t *data, size_t len) {
  if (data == nullptr) {
    return;
  }

  if (len == 0) {
    ClearInspectGroupCache();
    return;
  }

  std::vector<TalentGroupData> parsed_groups;
  uint32_t parsed_unspent = 0;
  uint32_t parsed_active_group = 0;
  uint32_t parsed_group_count = 0;
  if (!ParseGroupsFromPacket(data, len, &parsed_groups,
                             kTalentGroupStorageCapacity, &parsed_unspent,
                             &parsed_active_group, &parsed_group_count)) {
    return;
  }
  inspect_groups_ = std::move(parsed_groups);
  inspect_active_group_unspent_points_ = parsed_unspent;
  inspect_active_group_index_ = parsed_active_group;
  inspect_group_count_ = parsed_group_count;
  if (inspect_target_guid_ != 0) {
    RecalcGroupUnspentPoints(&inspect_groups_, inspect_active_group_index_,
                             inspect_active_group_unspent_points_, &inspect_tab_array_);
  }
}

}

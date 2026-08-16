
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

inline constexpr std::size_t kTalentSpellRankCount = 9;
inline constexpr std::size_t kTalentPrereqCount = 3;
inline constexpr std::size_t kPetTalentMaskWordCount = 2;

struct TalentInfoEntry {
  uint32_t talent_id = 0;
  uint32_t tab_id = 0;

  int32_t current_rank = -1;
  int32_t preview_rank = -1;
  uint32_t max_rank = 0;
  uint32_t tier = 0;
  uint32_t column = 0;
  std::array<uint32_t, kTalentSpellRankCount> spell_ids = {};
  std::array<uint32_t, kTalentPrereqCount> prereq_talent = {};
  std::array<uint32_t, kTalentPrereqCount> prereq_rank = {};
  uint32_t flags = 0;
  uint32_t required_spell_id = 0;
  std::array<uint32_t, kPetTalentMaskWordCount> pet_talent_mask_words = {};
};

struct TalentTabEntry {
  uint32_t tab_id = 0;
  uint32_t talent_count = 0;
  std::vector<TalentInfoEntry> talents;

  void ClearTierEntries();
};

struct TalentGroupTabInfo {
  uint32_t tab_id = 0;
  uint32_t points_spent = 0;
  int32_t preview_points_spent = 0;
};

struct TalentGroupData {

  std::vector<TalentTabEntry> tabs;
  uint32_t unspent_points = 0;
  uint16_t glyph_ids[6] = {};

  std::unordered_map<uint32_t, TalentInfoEntry *> talent_by_id;

  void RebuildIndex();
  [[nodiscard]] uint16_t GetGlyph(uint32_t slot_index) const;

  [[nodiscard]] std::optional<TalentGroupTabInfo> FindTabInfoById(uint32_t tab_id) const;
};

struct TalentTabDBC {
  uint32_t tab_id = 0;
  uint32_t order_index = 0;
  uint32_t class_mask = 0;
  uint32_t race_mask = 0;
  uint32_t pet_mask = 0;
};

struct TalentDBC {
  uint32_t talent_id = 0;
  uint32_t tab_id = 0;
  uint32_t tier = 0;
  uint32_t column = 0;
  uint32_t max_rank = 0;
  std::array<uint32_t, kTalentSpellRankCount> spell_ids = {};
  std::array<uint32_t, kTalentPrereqCount> prereq_talent = {};
  std::array<uint32_t, kTalentPrereqCount> prereq_rank = {};
  uint32_t flags = 0;
  uint32_t required_spell_id = 0;
  std::array<uint32_t, kPetTalentMaskWordCount> pet_talent_mask_words = {};
};

struct CreatureFamilyTalentInfo {
  int32_t pet_talent_type = -1;
  int32_t pet_talent_mask_index = -1;
  bool has_pet_talent_type = false;
  bool has_pet_talent_mask_index = false;
};

class TalentInfoStore {
public:
  static TalentInfoStore &Get();

  [[nodiscard]] uint32_t GetGroupIndexArg(std::optional<uint32_t> arg) const;

  [[nodiscard]] uint32_t GetDefaultGroupIndex(bool is_pet) const;

  [[nodiscard]] uint32_t GetActiveGroupIndexForContext(bool inspect, bool is_pet) const;

  [[nodiscard]] bool ValidatePetTalent(const TalentInfoEntry &talent,
                                       uint32_t creature_family) const;

  [[nodiscard]] TalentInfoEntry *FindInSortedTab(TalentTabEntry &tab, uint32_t talent_id);
  [[nodiscard]] const TalentInfoEntry *FindInSortedTab(const TalentTabEntry &tab,
                                                       uint32_t talent_id) const;

  [[nodiscard]] TalentTabEntry *FindGroupTabByTalentId(TalentGroupData &group,
                                                       uint32_t talent_id);
  [[nodiscard]] const TalentTabEntry *FindGroupTabByTalentId(const TalentGroupData &group,
                                                             uint32_t talent_id) const;

  [[nodiscard]] int32_t CountTotalPreviewPointsSpent(bool is_pet) const;

  [[nodiscard]] const TalentTabEntry *GetTalentTabArray(uint32_t index, bool inspect,
                                                        bool is_pet) const;

  [[nodiscard]] uint32_t GetTalentTabCount(bool inspect, bool is_pet) const;

  [[nodiscard]] uint32_t GetTalentGroupCount(bool inspect, bool is_pet) const;

  [[nodiscard]] uint32_t GetUnspentPointsForGroup(uint32_t group_index, bool is_pet) const;

  [[nodiscard]] TalentGroupData *GetGroupByIndex(uint32_t index, bool is_pet);
  [[nodiscard]] const TalentGroupData *GetGroupByIndex(uint32_t index, bool is_pet) const;

  [[nodiscard]] int32_t GetPreviewPointsSpent(uint32_t group_index, bool is_pet) const;

  [[nodiscard]] const TalentGroupData *GetTalentGroupData(uint32_t index, bool inspect,
                                                          bool is_pet) const;

  void BuildTabsFromDBC(uint32_t class_id, uint32_t creature_family, bool is_pet,
                        uint32_t race_id = 0, bool inspect = false);

  void FreeTabArrays(bool is_pet);

  void SetPetTalentCreatureFamily(uint32_t creature_family);

  void ResetPreviewForTab(uint32_t group_index, uint32_t tab_index, bool is_pet);

  void ResetPreviewForAllTabs(uint32_t group_index, bool is_pet);

  void ResetAllPreviewState(bool is_pet);

  [[nodiscard]] TalentInfoEntry *FindTalentByID(uint32_t group_index, uint32_t talent_id,
                                                bool is_pet);
  [[nodiscard]] std::optional<TalentInfoEntry> FindTalentDefinitionByID(
      uint32_t talent_id) const;

  [[nodiscard]] std::optional<TalentGroupTabInfo> GetTabInfo(const TalentGroupData &group,
                                                             uint32_t tab_index) const;
  [[nodiscard]] std::optional<TalentGroupTabInfo>
  GetTalentGroupTabInfo(uint32_t group_index, uint32_t tab_index, bool inspect, bool is_pet) const;

  [[nodiscard]] TalentInfoEntry *LookupTalentInGroup(uint32_t group_index, uint32_t talent_id,
                                                     bool inspect, bool is_pet);

  bool ParseFromPacket(const uint8_t *data, size_t len,
                       uint32_t *out_unspent,
                       uint32_t *out_active_group,
                       uint32_t *out_num_groups);

  void ClearAllGroups(bool is_pet);

  void RecalcUnspentPoints(uint32_t active_group, uint32_t active_group_unspent_points);

  void InitFromPlayer(uint32_t class_id, uint32_t race_id = 0);

  bool ProcessServerData(const uint8_t *data, size_t len);

  bool ProcessPetServerData(const uint8_t *data, size_t len);

  [[nodiscard]] std::optional<int32_t>
  ResolvePreviewPointDelta(uint32_t group_index, uint32_t tab_index,
                           uint32_t talent_index, int32_t delta,
                           bool is_pet) const;
  void AddPreviewPoints(uint32_t group_index, uint32_t tab_index, uint32_t talent_index,
                        int32_t delta, bool is_pet);

  void InitInspectFromGuid(uint64_t target_guid, uint32_t class_id, uint32_t race_id = 0);

  void ClearInspectTabArray();

  void ClearInspectData();
  [[nodiscard]] uint64_t GetInspectTargetGuid() const;

  void ParseInspectTalentPacket(const uint8_t *data, size_t len);

  void LoadFromDbc(const openwow::data::dbc::DbcLoader &dbc);
  void RegisterTalentTabDBC(const TalentTabDBC &tab);
  void RegisterTalentDBC(const TalentDBC &talent);
  void RegisterCreatureFamilyTalentType(uint32_t creature_family, int32_t pet_talent_type);
  void RegisterCreatureFamilyTalentMaskIndex(uint32_t creature_family,
                                             int32_t pet_talent_mask_index);
  void RegisterCreatureFamilyTalentInfo(uint32_t creature_family, int32_t pet_talent_type,
                                        int32_t pet_talent_mask_index);
  void ClearDBC();

  void ResetForWorldLogout();
  void SetNumGroups(uint32_t n);
  [[nodiscard]] uint32_t GetNumGroups() const;

  void SetPetTalentDataId(uint32_t value);
  [[nodiscard]] uint32_t GetPetTalentDataId() const;
  [[nodiscard]] bool HasPetTalentData() const;
  void SetActiveGroupIndex(uint32_t index);
  [[nodiscard]] uint32_t GetActiveGroupIndex() const;

  void ShutdownClearGameUiData();
  void Reset();

private:
  TalentInfoStore() = default;

  [[nodiscard]] const TalentDBC *FindTalentDBC(uint32_t talent_id) const;
  [[nodiscard]] bool ParseGroupsFromPacket(
      const uint8_t *data, size_t len, std::vector<TalentGroupData> *groups,
      uint32_t max_groups, uint32_t *out_unspent,
      uint32_t *out_active_group, uint32_t *out_num_groups);
  static void RecalcGroupUnspentPoints(std::vector<TalentGroupData> *groups, uint32_t active_group,
                                       uint32_t active_group_unspent_points,
                                       const std::vector<TalentTabEntry> *static_tabs);
  [[nodiscard]] TalentTabEntry *GetOrCreateTabById(TalentGroupData &group, uint32_t tab_id);
  [[nodiscard]] TalentInfoEntry *EnsureTalentEntry(TalentGroupData &group, uint32_t talent_id);
  void RebuildTalentTabArray(std::vector<TalentTabEntry> &target, uint32_t class_id,
                             uint32_t race_id, int32_t pet_talent_type);

  void ClearInspectGroupCache();
  [[nodiscard]] const CreatureFamilyTalentInfo *
  FindCreatureFamilyTalentInfo(uint32_t creature_family) const;
  [[nodiscard]] int32_t ResolvePetTalentType(uint32_t creature_family) const;

  bool initialized_ = false;
  uint32_t active_group_unspent_points_ = 0;
  uint32_t num_groups_ = 1;
  uint32_t pet_talent_creature_family_ = 0;
  uint32_t active_group_index_ = 0;

  std::vector<TalentGroupData> player_groups_;

  TalentGroupData pet_group_;

  std::vector<TalentGroupData> inspect_groups_;
  uint64_t inspect_target_guid_ = 0;
  uint32_t inspect_active_group_index_ = 0;
  uint32_t inspect_group_count_ = 0;
  uint32_t inspect_active_group_unspent_points_ = 0;

  std::vector<TalentTabEntry> player_tab_array_;
  std::vector<TalentTabEntry> pet_tab_array_;
  std::vector<TalentTabEntry> inspect_tab_array_;

  std::vector<TalentTabDBC> talent_tab_dbc_;
  std::vector<TalentDBC> talent_dbc_;
  std::unordered_map<uint32_t, CreatureFamilyTalentInfo> creature_family_talent_info_;
};

}


#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

class CGObject_C;
class ObjectManager;
class QueryCache;

[[nodiscard]] std::optional<std::uint32_t> ResolveSpellSkillLineId(
    const ObjectManager& objects,
    const openwow::data::dbc::DbcLoader* dbc_loader,
    std::uint8_t race,
    std::uint8_t player_class,
    std::uint32_t spell_id);

struct SpellInfo {
  uint32_t spell_id = 0;
  uint32_t rank = 0;
  bool is_known = true;
  bool is_passive = false;
  bool is_talent = false;
  uint32_t skill_line_id = 0;
};

struct SpellTab {
  std::string name;
  std::string texture;
  uint32_t offset = 0;
  uint32_t num_spells = 0;
  uint32_t skill_line_id = 0;
  std::optional<uint32_t> num_known;

  uint32_t highest_rank_offset = 0;
};

enum class CompanionSpellType : std::uint8_t {
  Critter = 0,
  Mount = 1,
};

enum class SpellbookUiEventType : std::uint8_t {
  SpellsChanged,
  LearnedSpellInTab,
  CompanionLearned,
  CompanionUnlearned,
  CompanionUpdate,
};

struct SpellbookUiEvent {
  SpellbookUiEventType type = SpellbookUiEventType::SpellsChanged;
  std::uint32_t argument = 0;
};

enum class SkinnableResourceType : std::uint8_t {
  Leather = 0,
  Herb = 1,
  Rock = 2,
  Bolts = 3,
};

struct CreatureTemplateInfo;

[[nodiscard]] SkinnableResourceType ResolveSkinnableResourceType(
    const CreatureTemplateInfo& creature_template);

class SpellbookSystem {
 public:
  static SpellbookSystem& Get();

  void SetSpells(const ObjectManager& objects,
                 const std::vector<SpellInfo>& spells);
  void AddSpell(const ObjectManager& objects, const SpellInfo& spell);
  void RemoveSpell(const ObjectManager& objects, uint32_t spell_id);
  void ReplaceSpell(const ObjectManager& objects, uint32_t old_spell_id,
                    const SpellInfo& spell);
  bool HasSpell(uint32_t spell_id) const;

  bool HasSpellOrSupersedingRank(uint32_t spell_id, std::uint8_t race,
                                 std::uint8_t player_class) const;
  size_t GetNumSpells() const;
  const SpellInfo* GetSpell(uint32_t spell_id) const;

  const std::vector<SpellInfo>& GetKnownSpellList() const;
  const std::vector<SpellInfo>& GetSpellList() const;

  [[nodiscard]] std::vector<std::uint32_t> GetCompanionSpellList(
      CompanionSpellType type) const;
  void SetCompanionSpellListOrder(CompanionSpellType type,
                                  std::vector<std::uint32_t> spell_ids);

  [[nodiscard]] bool SortCompanionSpellLists(const QueryCache& query_cache);
  [[nodiscard]] std::optional<std::uint64_t> BeginCompanionNameQuery(
      std::uint32_t spell_id);
  [[nodiscard]] bool CompleteCompanionNameQuery(std::uint32_t spell_id,
                                                std::uint64_t token);
  [[nodiscard]] bool HasPendingCompanionNameQueries() const;
  void ClearCompanionSpellLists();

  void QueueUiEvent(SpellbookUiEventType type, std::uint32_t argument = 0);
  void QueueLearnedSpellInTab(std::uint32_t spell_id);
  [[nodiscard]] std::vector<SpellbookUiEvent> ConsumeUiEvents();

  void SetDbcLoader(const ::openwow::data::dbc::DbcLoader* dbc_loader,
                    const ObjectManager& objects);
  [[nodiscard]] const ::openwow::data::dbc::DbcLoader* GetDbcLoader() const {
    return dbc_loader_;
  }
  void SetTabs(const ObjectManager& objects,
               const std::vector<SpellTab>& tabs);
  size_t GetNumTabs() const;
  const SpellTab* GetTab(size_t index) const;

  const SpellInfo* GetPlayerSpellBookSlot(uint32_t slot) const;

  size_t GetPlayerSpellBookSlotCount() const;

  [[nodiscard]] std::uint32_t GetKnownSlotFromHighestRankSlot(
      std::uint32_t highest_rank_slot) const;

  void RefreshDisplayState(const ObjectManager& objects);

  [[nodiscard]] std::uint32_t FindKnownSpellByCategory(std::uint32_t category) const;
  [[nodiscard]] std::uint32_t GetSummonFriendSpellId() const;
  [[nodiscard]] std::uint32_t GetAutoRangedCombatSpellId() const;
  [[nodiscard]] std::uint32_t GetCorpseOrPlayerGatherInteractionSpellId() const;
  [[nodiscard]] std::uint32_t GetSkinnableGatherInteractionSpellId(
      SkinnableResourceType type) const;
  [[nodiscard]] std::uint32_t ResolveGatherInteractionSpellId(
      const CGObject_C& target,
      const QueryCache* query_cache) const;
  void TrackLearnedGatherInteractionSpell(std::uint32_t spell_id);
  void ForgetGatherInteractionSpell(std::uint32_t spell_id);

  void SetCooldown(uint32_t spell_id, uint32_t remaining_ms,
                   uint32_t duration_ms);
  bool IsOnCooldown(uint32_t spell_id) const;

  std::pair<uint32_t, uint32_t> GetCooldown(uint32_t spell_id) const;
  void ClearCooldown(uint32_t spell_id);
  void ClearAllCooldowns();

  void ResetSpellbookData();
  void Reset();

 private:
  SpellbookSystem() = default;

  std::unordered_map<uint32_t, SpellInfo> spells_;
  struct SupersessionAbilityEdge {
    std::uint32_t skill_id = 0;
    std::uint32_t race_mask = 0;
    std::uint32_t class_mask = 0;
    std::uint32_t exclude_race_mask = 0;
    std::uint32_t exclude_class_mask = 0;
    std::uint32_t next_spell = 0;
  };

  struct SupersessionSkillEligibility {
    std::uint32_t race_mask = 0;
    std::uint32_t class_mask = 0;
  };

  std::unordered_map<uint32_t, std::vector<SupersessionAbilityEdge>>
      supersession_abilities_by_spell_;
  std::unordered_map<uint32_t, std::vector<SupersessionSkillEligibility>>
      supersession_skill_eligibility_;
  std::vector<SpellInfo> known_spell_list_;
  std::vector<SpellInfo> spell_list_;
  std::array<std::vector<std::uint32_t>, 2> companion_spell_lists_;
  std::unordered_map<std::uint32_t, std::uint64_t>
      pending_companion_name_queries_;
  std::uint64_t next_companion_name_query_token_ = 1;
  std::vector<SpellbookUiEvent> pending_ui_events_;

  std::vector<std::uint32_t> highest_rank_spell_ids_;
  std::vector<SpellTab> tabs_;

  bool has_explicit_tabs_ = false;
  const ::openwow::data::dbc::DbcLoader* dbc_loader_ = nullptr;
  std::uint32_t summon_friend_spell_id_ = 0;
  std::uint32_t auto_ranged_combat_spell_id_ = 0;
  std::array<std::uint32_t, 4> skinnable_gather_interaction_spell_ids_{};
  std::uint32_t corpse_or_player_gather_interaction_spell_id_ = 0;
  std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> cooldowns_;
  mutable std::mutex mutex_;

  [[nodiscard]] std::uint32_t ResolveSpellFamilyNameLocked(std::uint32_t spell_id) const;
  [[nodiscard]] bool IsAutoRangedCombatSpellLocked(std::uint32_t spell_id) const;
  void RebuildSupersessionIndexLocked();
  void RefreshTrackedSpellIdsLocked();
  void RebuildSpellTabsLocked();
  void RebuildDisplayStateLocked(const ObjectManager& objects);
};

[[nodiscard]] std::uint32_t GetAutoSelfCastAttackSpellId();

}


#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace openwow::game {

enum class TrainerSpellState : uint8_t {
  Available   = 0,
  Unavailable = 1,
  Known       = 2,
};

enum class TrainerType : uint8_t {
  ClassTrainer      = 0,
  MountTrainer      = 1,
  TradeSkillTrainer = 2,
  PetTrainer        = 3,
};

struct TrainerSpellEntry {
  uint32_t spell_id = 0;
  uint8_t state = 0;
  uint32_t cost = 0;
  uint8_t req_level = 0;
  uint32_t req_skill = 0;
  uint32_t req_skill_value = 0;
  std::vector<uint32_t> req_spells;
};

enum class TrainerFilter : uint8_t {
  All         = 0,
  Available   = 1,
  Unavailable = 2,
};

enum class TrainerSortMode : uint8_t {
  Default     = 0,
  ByLevel     = 1,
  ByCost      = 2,
  ByState     = 3,
};

enum class TrainerEvent : uint8_t {
  WindowOpened  = 0,
  WindowClosed  = 1,
  SpellLearned  = 2,
  LearnFailed   = 3,
};

using TrainerEventCallback = std::function<void(TrainerEvent, uint32_t )>;

class TrainerSystem {
 public:
  static TrainerSystem& Get();

  void SetTrainerList(uint64_t npc_guid, uint32_t trainer_id,
                      const std::vector<TrainerSpellEntry>& spells);
  bool HasTrainerWindow() const;
  uint64_t GetTrainerGuid() const;
  uint32_t GetTrainerId() const;

  void SetTrainerType(TrainerType type);
  TrainerType GetTrainerType() const;

  void SetGreeting(const std::string& greeting);
  const std::string& GetGreeting() const;

  size_t GetNumTrainerSpells() const;
  const TrainerSpellEntry* GetTrainerSpell(size_t index) const;

  const TrainerSpellEntry* FindSpellById(uint32_t spell_id) const;

  int FindSpellIndex(uint32_t spell_id) const;

  size_t GetNumAvailable() const;
  size_t GetNumUnavailable() const;
  size_t GetNumKnown() const;

  void SetFilter(TrainerFilter filter);
  TrainerFilter GetFilter() const;

  void SetSortMode(TrainerSortMode mode);
  TrainerSortMode GetSortMode() const;

  std::vector<const TrainerSpellEntry*> GetFilteredSpells() const;

  bool CanLearnSpell(uint32_t spell_id, uint8_t player_level,
                     uint32_t player_gold) const;

  uint32_t GetTotalAvailableCost() const;

  void SetSelectedSpell(uint32_t spell_id);
  uint32_t GetSelectedSpell() const;

  uint32_t RegisterCallback(TrainerEventCallback cb);
  void UnregisterCallback(uint32_t id);

  void NotifySpellLearned(uint32_t spell_id);

  void CloseTrainer();
  void Reset();

 private:
  TrainerSystem() = default;
  void FireEvent(TrainerEvent event, uint32_t spell_id = 0);

  uint64_t trainer_guid_ = 0;
  uint32_t trainer_id_ = 0;
  TrainerType trainer_type_ = TrainerType::ClassTrainer;
  std::string greeting_;
  std::vector<TrainerSpellEntry> spells_;
  bool has_window_ = false;

  TrainerFilter filter_ = TrainerFilter::All;
  TrainerSortMode sort_mode_ = TrainerSortMode::Default;
  uint32_t selected_spell_ = 0;

  struct CallbackEntry {
    uint32_t id;
    TrainerEventCallback fn;
  };
  std::vector<CallbackEntry> callbacks_;
  uint32_t next_callback_id_ = 1;

  mutable std::mutex mutex_;
};

}

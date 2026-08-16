
#include "openwow/game/trainer_system.h"

#include <algorithm>

namespace openwow::game {

TrainerSystem& TrainerSystem::Get() {
  static TrainerSystem instance;
  return instance;
}

void TrainerSystem::SetTrainerList(uint64_t npc_guid, uint32_t trainer_id,
                                   const std::vector<TrainerSpellEntry>& spells) {
  std::lock_guard lock(mutex_);
  trainer_guid_ = npc_guid;
  trainer_id_ = trainer_id;
  spells_ = spells;
  has_window_ = true;
  selected_spell_ = 0;
  filter_ = TrainerFilter::All;
  sort_mode_ = TrainerSortMode::Default;
  FireEvent(TrainerEvent::WindowOpened);
}

bool TrainerSystem::HasTrainerWindow() const {
  std::lock_guard lock(mutex_);
  return has_window_;
}

uint64_t TrainerSystem::GetTrainerGuid() const {
  std::lock_guard lock(mutex_);
  return trainer_guid_;
}

uint32_t TrainerSystem::GetTrainerId() const {
  std::lock_guard lock(mutex_);
  return trainer_id_;
}

void TrainerSystem::SetTrainerType(TrainerType type) {
  std::lock_guard lock(mutex_);
  trainer_type_ = type;
}

TrainerType TrainerSystem::GetTrainerType() const {
  std::lock_guard lock(mutex_);
  return trainer_type_;
}

void TrainerSystem::SetGreeting(const std::string& greeting) {
  std::lock_guard lock(mutex_);
  greeting_ = greeting;
}

const std::string& TrainerSystem::GetGreeting() const {
  std::lock_guard lock(mutex_);
  return greeting_;
}

size_t TrainerSystem::GetNumTrainerSpells() const {
  std::lock_guard lock(mutex_);
  return spells_.size();
}

const TrainerSpellEntry* TrainerSystem::GetTrainerSpell(size_t index) const {
  std::lock_guard lock(mutex_);
  if (index >= spells_.size()) return nullptr;
  return &spells_[index];
}

const TrainerSpellEntry* TrainerSystem::FindSpellById(uint32_t spell_id) const {
  std::lock_guard lock(mutex_);
  for (const auto& s : spells_) {
    if (s.spell_id == spell_id) return &s;
  }
  return nullptr;
}

int TrainerSystem::FindSpellIndex(uint32_t spell_id) const {
  std::lock_guard lock(mutex_);
  for (size_t i = 0; i < spells_.size(); ++i) {
    if (spells_[i].spell_id == spell_id) return static_cast<int>(i);
  }
  return -1;
}

size_t TrainerSystem::GetNumAvailable() const {
  std::lock_guard lock(mutex_);
  return static_cast<size_t>(
      std::count_if(spells_.begin(), spells_.end(),
                    [](const TrainerSpellEntry& s) { return s.state == 0; }));
}

size_t TrainerSystem::GetNumUnavailable() const {
  std::lock_guard lock(mutex_);
  return static_cast<size_t>(
      std::count_if(spells_.begin(), spells_.end(),
                    [](const TrainerSpellEntry& s) { return s.state == 1; }));
}

size_t TrainerSystem::GetNumKnown() const {
  std::lock_guard lock(mutex_);
  return static_cast<size_t>(
      std::count_if(spells_.begin(), spells_.end(),
                    [](const TrainerSpellEntry& s) { return s.state == 2; }));
}

void TrainerSystem::SetFilter(TrainerFilter filter) {
  std::lock_guard lock(mutex_);
  filter_ = filter;
}

TrainerFilter TrainerSystem::GetFilter() const {
  std::lock_guard lock(mutex_);
  return filter_;
}

void TrainerSystem::SetSortMode(TrainerSortMode mode) {
  std::lock_guard lock(mutex_);
  sort_mode_ = mode;
}

TrainerSortMode TrainerSystem::GetSortMode() const {
  std::lock_guard lock(mutex_);
  return sort_mode_;
}

std::vector<const TrainerSpellEntry*> TrainerSystem::GetFilteredSpells() const {
  std::lock_guard lock(mutex_);

  std::vector<const TrainerSpellEntry*> result;
  result.reserve(spells_.size());
  for (const auto& s : spells_) {
    switch (filter_) {
      case TrainerFilter::Available:
        if (s.state != 0) continue;
        break;
      case TrainerFilter::Unavailable:
        if (s.state != 1) continue;
        break;
      case TrainerFilter::All:
      default:
        break;
    }
    result.push_back(&s);
  }

  switch (sort_mode_) {
    case TrainerSortMode::ByLevel:
      std::stable_sort(result.begin(), result.end(),
                       [](const TrainerSpellEntry* a, const TrainerSpellEntry* b) {
                         return a->req_level < b->req_level;
                       });
      break;
    case TrainerSortMode::ByCost:
      std::stable_sort(result.begin(), result.end(),
                       [](const TrainerSpellEntry* a, const TrainerSpellEntry* b) {
                         return a->cost < b->cost;
                       });
      break;
    case TrainerSortMode::ByState:

      std::stable_sort(result.begin(), result.end(),
                       [](const TrainerSpellEntry* a, const TrainerSpellEntry* b) {
                         return a->state > b->state;
                       });
      break;
    case TrainerSortMode::Default:
    default:
      break;
  }

  return result;
}

bool TrainerSystem::CanLearnSpell(uint32_t spell_id, uint8_t player_level,
                                  uint32_t player_gold) const {
  std::lock_guard lock(mutex_);
  for (const auto& s : spells_) {
    if (s.spell_id != spell_id) continue;

    if (s.state == 2) return false;

    if (s.state == 1) return false;

    if (player_level < s.req_level) return false;

    if (player_gold < s.cost) return false;

    return true;
  }
  return false;
}

uint32_t TrainerSystem::GetTotalAvailableCost() const {
  std::lock_guard lock(mutex_);
  uint32_t total = 0;
  for (const auto& s : spells_) {
    if (s.state == 0) total += s.cost;
  }
  return total;
}

void TrainerSystem::SetSelectedSpell(uint32_t spell_id) {
  std::lock_guard lock(mutex_);
  selected_spell_ = spell_id;
}

uint32_t TrainerSystem::GetSelectedSpell() const {
  std::lock_guard lock(mutex_);
  return selected_spell_;
}

uint32_t TrainerSystem::RegisterCallback(TrainerEventCallback cb) {
  uint32_t id = next_callback_id_++;
  callbacks_.push_back({id, std::move(cb)});
  return id;
}

void TrainerSystem::UnregisterCallback(uint32_t id) {
  callbacks_.erase(
      std::remove_if(callbacks_.begin(), callbacks_.end(),
                     [id](const CallbackEntry& e) { return e.id == id; }),
      callbacks_.end());
}

void TrainerSystem::FireEvent(TrainerEvent event, uint32_t spell_id) {
  auto snapshot = callbacks_;
  for (const auto& entry : snapshot) {
    if (entry.fn) entry.fn(event, spell_id);
  }
}

void TrainerSystem::NotifySpellLearned(uint32_t spell_id) {
  std::lock_guard lock(mutex_);
  for (auto& s : spells_) {
    if (s.spell_id == spell_id) {
      s.state = 2;
      FireEvent(TrainerEvent::SpellLearned, spell_id);

      for (auto& other : spells_) {
        if (other.state != 1) continue;

        auto& reqs = other.req_spells;
        reqs.erase(std::remove(reqs.begin(), reqs.end(), spell_id), reqs.end());
      }
      return;
    }
  }

  FireEvent(TrainerEvent::LearnFailed, spell_id);
}

void TrainerSystem::CloseTrainer() {
  std::lock_guard lock(mutex_);
  if (has_window_) {
    has_window_ = false;
    FireEvent(TrainerEvent::WindowClosed);
  }
  trainer_guid_ = 0;
  trainer_id_ = 0;
  trainer_type_ = TrainerType::ClassTrainer;
  greeting_.clear();
  spells_.clear();
  selected_spell_ = 0;
  filter_ = TrainerFilter::All;
  sort_mode_ = TrainerSortMode::Default;
}

void TrainerSystem::Reset() {
  CloseTrainer();
  callbacks_.clear();
  next_callback_id_ = 1;
}

}

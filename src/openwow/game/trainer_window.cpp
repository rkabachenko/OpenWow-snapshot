
#include "openwow/game/trainer_window.h"

#include <algorithm>
#include <cctype>

namespace openwow::game {

namespace {

std::string ToLower(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return out;
}

}

void TrainerWindow::Open(ObjectGuid trainerGuid, const std::string& trainerName,
                         TrainerServiceType serviceType) {
  trainerGuid_  = trainerGuid;
  trainerName_  = trainerName;
  serviceType_  = serviceType;
  open_         = true;
  selectedIndex_ = -1;
  spells_.clear();
}

void TrainerWindow::Close() {
  open_ = false;
}

bool TrainerWindow::IsOpen() const { return open_; }

ObjectGuid TrainerWindow::GetTrainerGuid() const { return trainerGuid_; }
const std::string& TrainerWindow::GetTrainerName() const { return trainerName_; }
TrainerServiceType TrainerWindow::GetServiceType() const { return serviceType_; }

void TrainerWindow::SetSpells(const std::vector<TrainerSpellInfo>& spells) {
  spells_ = spells;
}

const std::vector<TrainerSpellInfo>& TrainerWindow::GetSpells() const {
  return spells_;
}

uint32_t TrainerWindow::GetSpellCount() const {
  return static_cast<uint32_t>(spells_.size());
}

std::vector<TrainerSpellInfo> TrainerWindow::GetAvailableSpells() const {
  std::vector<TrainerSpellInfo> result;
  for (const auto& s : spells_)
    if (s.state == TrainerSpellState::Available) result.push_back(s);
  return result;
}

std::vector<TrainerSpellInfo> TrainerWindow::GetUnavailableSpells() const {
  std::vector<TrainerSpellInfo> result;
  for (const auto& s : spells_)
    if (s.state == TrainerSpellState::Unavailable) result.push_back(s);
  return result;
}

std::vector<TrainerSpellInfo> TrainerWindow::GetKnownSpells() const {
  std::vector<TrainerSpellInfo> result;
  for (const auto& s : spells_)
    if (s.state == TrainerSpellState::AlreadyKnown) result.push_back(s);
  return result;
}

std::optional<TrainerSpellInfo> TrainerWindow::GetSpellByIndex(uint32_t index) const {
  if (index >= spells_.size()) return std::nullopt;
  return spells_[index];
}

std::optional<TrainerSpellInfo> TrainerWindow::FindSpell(uint32_t spellId) const {
  for (const auto& s : spells_)
    if (s.spellId == spellId) return s;
  return std::nullopt;
}

std::vector<TrainerSpellInfo> TrainerWindow::FilterByName(const std::string& query) const {
  std::string lq = ToLower(query);
  std::vector<TrainerSpellInfo> result;
  for (const auto& s : spells_) {
    if (ToLower(s.spellName).find(lq) != std::string::npos)
      result.push_back(s);
  }
  return result;
}

void TrainerWindow::SetShowUnavailable(bool show) { showUnavailable_ = show; }
bool TrainerWindow::GetShowUnavailable() const { return showUnavailable_; }

int32_t TrainerWindow::GetSelectedIndex() const { return selectedIndex_; }
void TrainerWindow::SetSelectedIndex(int32_t index) { selectedIndex_ = index; }

bool TrainerWindow::CanAfford(uint32_t index, uint32_t playerMoney) const {
  if (index >= spells_.size()) return false;
  return playerMoney >= spells_[index].cost;
}

uint32_t TrainerWindow::GetTotalCost() const {
  uint32_t total = 0;
  for (const auto& s : spells_)
    if (s.state == TrainerSpellState::Available) total += s.cost;
  return total;
}

void TrainerWindow::Reset() {
  trainerGuid_   = ObjectGuid{};
  trainerName_.clear();
  serviceType_   = TrainerServiceType::Class;
  spells_.clear();
  open_          = false;
  showUnavailable_ = true;
  selectedIndex_ = -1;
}

void TrainerWindowDisplay::SetTrainerType(TrainerDisplayServiceType type) {
  displayType_ = type;
}

TrainerDisplayServiceType TrainerWindowDisplay::GetTrainerType() const {
  return displayType_;
}

void TrainerWindowDisplay::SetTrainerName(const std::string& name) {
  displayName_ = name;
}

const std::string& TrainerWindowDisplay::GetTrainerName() const {
  return displayName_;
}

void TrainerWindowDisplay::SetSpells(std::vector<TrainerSpellDisplayEntry> spells) {
  displaySpells_ = std::move(spells);
  selectedSpellId_.reset();
}

const std::vector<TrainerSpellDisplayEntry>& TrainerWindowDisplay::GetAllSpells() const {
  return displaySpells_;
}

std::vector<const TrainerSpellDisplayEntry*> TrainerWindowDisplay::GetAvailableSpells() const {
  std::vector<const TrainerSpellDisplayEntry*> result;
  for (const auto& s : displaySpells_) {
    if (s.state == TrainerDisplaySpellState::Available)
      result.push_back(&s);
  }
  return result;
}

std::vector<const TrainerSpellDisplayEntry*> TrainerWindowDisplay::GetUnavailableSpells() const {
  std::vector<const TrainerSpellDisplayEntry*> result;
  for (const auto& s : displaySpells_) {
    if (s.state != TrainerDisplaySpellState::Available &&
        s.state != TrainerDisplaySpellState::AlreadyKnown)
      result.push_back(&s);
  }
  return result;
}

size_t TrainerWindowDisplay::GetSpellCount() const {
  return displaySpells_.size();
}

size_t TrainerWindowDisplay::GetAvailableCount() const {
  size_t count = 0;
  for (const auto& s : displaySpells_)
    if (s.state == TrainerDisplaySpellState::Available) ++count;
  return count;
}

void TrainerWindowDisplay::SetSelectedSpell(uint32_t spellId) {
  for (const auto& s : displaySpells_) {
    if (s.spellId == spellId) {
      selectedSpellId_ = spellId;
      return;
    }
  }
}

std::optional<uint32_t> TrainerWindowDisplay::GetSelectedSpell() const {
  return selectedSpellId_;
}

std::optional<TrainerSpellDisplayEntry> TrainerWindowDisplay::GetSelectedSpellInfo() const {
  if (!selectedSpellId_) return std::nullopt;
  for (const auto& s : displaySpells_)
    if (s.spellId == *selectedSpellId_) return s;
  return std::nullopt;
}

bool TrainerWindowDisplay::CanBuySelected() const {
  if (!selectedSpellId_) return false;
  for (const auto& s : displaySpells_) {
    if (s.spellId == *selectedSpellId_) {
      return s.state == TrainerDisplaySpellState::Available &&
             playerMoney_ >= s.cost;
    }
  }
  return false;
}

void TrainerWindowDisplay::SetPlayerMoney(uint64_t money) {
  playerMoney_ = money;
}

uint64_t TrainerWindowDisplay::GetPlayerMoney() const {
  return playerMoney_;
}

void TrainerWindowDisplay::FilterByAvailable(bool onlyAvailable) {
  filterAvailableOnly_ = onlyAvailable;
}

bool TrainerWindowDisplay::IsOpen() const { return displayOpen_; }

void TrainerWindowDisplay::SetOpen(bool open) { displayOpen_ = open; }

std::vector<const TrainerSpellDisplayEntry*> TrainerWindowDisplay::Search(
    const std::string& query) const {
  std::string lq = ToLower(query);
  std::vector<const TrainerSpellDisplayEntry*> result;
  for (const auto& s : displaySpells_) {
    if (ToLower(s.spellName).find(lq) != std::string::npos)
      result.push_back(&s);
  }
  return result;
}

void TrainerWindowDisplay::Clear() {
  displayType_ = TrainerDisplayServiceType::ClassTrainer;
  displayName_.clear();
  displaySpells_.clear();
  selectedSpellId_.reset();
  playerMoney_ = 0;
  displayOpen_ = false;
  filterAvailableOnly_ = false;
}

}

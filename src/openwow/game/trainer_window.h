
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class TrainerSpellState : uint8_t {
  Available    = 0,
  Unavailable  = 1,
  AlreadyKnown = 2,
};

enum class TrainerServiceType : uint8_t {
  Class      = 0,
  Mounts     = 1,
  Tradeskills = 2,
  Pets       = 3,
  DualSpec   = 4,
};

struct TrainerSpellInfo {
  uint32_t        spellId          = 0;
  std::string     spellName;
  std::string     spellIcon;
  std::string     rank;
  uint32_t        cost             = 0;
  uint32_t        requiredLevel    = 0;
  uint32_t        requiredSkillId  = 0;
  uint32_t        requiredSkillValue = 0;
  TrainerSpellState state          = TrainerSpellState::Unavailable;
  bool            isProficiency    = false;
};

class TrainerWindow {
 public:
  void Open(ObjectGuid trainerGuid, const std::string& trainerName,
            TrainerServiceType serviceType);
  void Close();
  [[nodiscard]] bool IsOpen() const;

  [[nodiscard]] ObjectGuid GetTrainerGuid() const;
  [[nodiscard]] const std::string& GetTrainerName() const;
  [[nodiscard]] TrainerServiceType GetServiceType() const;

  void SetSpells(const std::vector<TrainerSpellInfo>& spells);
  [[nodiscard]] const std::vector<TrainerSpellInfo>& GetSpells() const;
  [[nodiscard]] uint32_t GetSpellCount() const;

  [[nodiscard]] std::vector<TrainerSpellInfo> GetAvailableSpells() const;
  [[nodiscard]] std::vector<TrainerSpellInfo> GetUnavailableSpells() const;
  [[nodiscard]] std::vector<TrainerSpellInfo> GetKnownSpells() const;

  [[nodiscard]] std::optional<TrainerSpellInfo> GetSpellByIndex(uint32_t index) const;
  [[nodiscard]] std::optional<TrainerSpellInfo> FindSpell(uint32_t spellId) const;
  [[nodiscard]] std::vector<TrainerSpellInfo> FilterByName(const std::string& query) const;

  void SetShowUnavailable(bool show);
  [[nodiscard]] bool GetShowUnavailable() const;

  [[nodiscard]] int32_t GetSelectedIndex() const;
  void SetSelectedIndex(int32_t index);

  [[nodiscard]] bool CanAfford(uint32_t index, uint32_t playerMoney) const;
  [[nodiscard]] uint32_t GetTotalCost() const;

  void Reset();

 private:
  ObjectGuid                   trainerGuid_;
  std::string                  trainerName_;
  TrainerServiceType           serviceType_ = TrainerServiceType::Class;
  std::vector<TrainerSpellInfo> spells_;
  bool                         open_            = false;
  bool                         showUnavailable_ = true;
  int32_t                      selectedIndex_   = -1;
};

enum class TrainerDisplayServiceType : uint8_t {
  ClassTrainer      = 0,
  ProfessionTrainer = 1,
  PetTrainer        = 2,
  RidingTrainer     = 3,
  ColdWeatherFlying = 4,
};

enum class TrainerDisplaySpellState : uint8_t {
  Available      = 0,
  Unavailable    = 1,
  AlreadyKnown   = 2,
  NotEnoughMoney = 3,
  TooLowLevel    = 4,
  MissingPrereq  = 5,
};

struct TrainerSpellDisplayEntry {
  uint32_t                  spellId       = 0;
  std::string               spellName;
  std::string               spellIcon;
  uint32_t                  cost          = 0;
  uint8_t                   reqLevel      = 0;
  uint32_t                  reqSkillId    = 0;
  uint32_t                  reqSkillValue = 0;
  TrainerDisplaySpellState  state         = TrainerDisplaySpellState::Unavailable;
};

class TrainerWindowDisplay {
 public:
  void SetTrainerType(TrainerDisplayServiceType type);
  [[nodiscard]] TrainerDisplayServiceType GetTrainerType() const;

  void SetTrainerName(const std::string& name);
  [[nodiscard]] const std::string& GetTrainerName() const;

  void SetSpells(std::vector<TrainerSpellDisplayEntry> spells);
  [[nodiscard]] const std::vector<TrainerSpellDisplayEntry>& GetAllSpells() const;
  [[nodiscard]] std::vector<const TrainerSpellDisplayEntry*> GetAvailableSpells() const;
  [[nodiscard]] std::vector<const TrainerSpellDisplayEntry*> GetUnavailableSpells() const;
  [[nodiscard]] size_t GetSpellCount() const;
  [[nodiscard]] size_t GetAvailableCount() const;

  void SetSelectedSpell(uint32_t spellId);
  [[nodiscard]] std::optional<uint32_t> GetSelectedSpell() const;
  [[nodiscard]] std::optional<TrainerSpellDisplayEntry> GetSelectedSpellInfo() const;
  [[nodiscard]] bool CanBuySelected() const;

  void SetPlayerMoney(uint64_t money);
  [[nodiscard]] uint64_t GetPlayerMoney() const;

  void FilterByAvailable(bool onlyAvailable);

  [[nodiscard]] bool IsOpen() const;
  void SetOpen(bool open);

  [[nodiscard]] std::vector<const TrainerSpellDisplayEntry*> Search(
      const std::string& query) const;

  void Clear();

 private:
  TrainerDisplayServiceType          displayType_ = TrainerDisplayServiceType::ClassTrainer;
  std::string                        displayName_;
  std::vector<TrainerSpellDisplayEntry> displaySpells_;
  std::optional<uint32_t>            selectedSpellId_;
  uint64_t                           playerMoney_        = 0;
  bool                               displayOpen_        = false;
  bool                               filterAvailableOnly_ = false;
};

}

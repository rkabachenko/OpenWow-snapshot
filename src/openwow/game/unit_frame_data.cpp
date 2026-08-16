#include "openwow/game/unit_frame_data.h"

#include "openwow/game/aura_tracker.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/objects/cgplayer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace openwow::game {

UnitFrameDataProvider& UnitFrameDataProvider::Get() {
  static UnitFrameDataProvider instance;
  return instance;
}

void UnitFrameDataProvider::Update(const ObjectManager& objects) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto player_guid = objects.GetLocalPlayerGuid();
  if (!player_guid.IsEmpty()) {
    UpdateUnitData(objects, player_guid, player_);
  } else {
    player_ = UnitFrameData{};
  }

  auto target_guid = objects.GetTargetGuid();
  if (!target_guid.IsEmpty()) {
    UpdateUnitData(objects, target_guid, target_);

    auto* target_unit = objects.GetUnit(target_guid);
    if (target_unit) {
      auto tot_guid = target_unit->State().GetTarget();
      if (!tot_guid.IsEmpty()) {
        UpdateUnitData(objects, tot_guid, target_of_target_);
      } else {
        target_of_target_ = UnitFrameData{};
      }
    }
  } else {
    target_ = UnitFrameData{};
    target_of_target_ = UnitFrameData{};
  }

  auto focus_guid = objects.GetFocusTargetGuid();
  if (!focus_guid.IsEmpty()) {
    UpdateUnitData(objects, focus_guid, focus_);
  } else {
    focus_ = UnitFrameData{};
  }

  auto* local_player = objects.GetActivePlayer();
  if (local_player) {
    auto pet_guid = local_player->State().GetPetGUID();
    if (!pet_guid.IsEmpty()) {
      UpdateUnitData(objects, pet_guid, pet_);
    } else {
      pet_ = UnitFrameData{};
    }
  } else {
    pet_ = UnitFrameData{};
  }
}

void UnitFrameDataProvider::UpdateUnitData(const ObjectManager& objects,
                                           const ObjectGuid& guid,
                                           UnitFrameData& data) {
  const auto* unit = objects.GetUnit(guid);
  if (!unit) {
    data = UnitFrameData{};
    return;
  }

  data.guid = guid;
  data.name = unit->GetName();
  data.race = unit->State().GetRace();
  data.class_id = unit->State().GetClass();
  data.gender = unit->State().GetGender();
  data.level = unit->State().GetLevel();

  data.health = static_cast<std::int32_t>(unit->State().GetHealth());
  data.max_health = static_cast<std::int32_t>(unit->State().GetMaxHealth());
  data.health_percent =
      data.max_health > 0
          ? static_cast<float>(data.health) /
                static_cast<float>(data.max_health) * 100.0f
          : 0.0f;

  auto power_type_raw = unit->State().GetPowerType();
  data.power_type = static_cast<PowerType>(power_type_raw);
  data.power =
      static_cast<std::int32_t>(unit->State().GetPower(power_type_raw));
  data.max_power =
      static_cast<std::int32_t>(unit->State().GetMaxPower(power_type_raw));
  data.power_percent =
      data.max_power > 0
          ? static_cast<float>(data.power) /
                static_cast<float>(data.max_power) * 100.0f
          : 0.0f;

  data.display_id = unit->Presentation().DisplayId();

  data.is_dead = unit->State().IsDead();
  data.is_ghost = unit->State().IsDeadOrGhost() && !unit->State().IsDead();
  data.in_combat = unit->State().IsInCombat();

  auto flags = unit->State().GetUnitFlags();
  data.is_pvp = (flags & 0x00001000) != 0;
  data.is_afk = false;
  data.is_dnd = false;

  auto& tracker = AuraTracker::Get();
  data.buff_count = tracker.GetBuffCount(guid);
  data.debuff_count = tracker.GetDebuffCount(guid);

  data.has_data = true;
}

const UnitFrameData& UnitFrameDataProvider::GetPlayerData() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return player_;
}

const UnitFrameData& UnitFrameDataProvider::GetTargetData() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return target_;
}

const UnitFrameData& UnitFrameDataProvider::GetFocusData() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return focus_;
}

const UnitFrameData& UnitFrameDataProvider::GetTargetOfTargetData() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return target_of_target_;
}

const UnitFrameData& UnitFrameDataProvider::GetPetData() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pet_;
}

const UnitFrameData& UnitFrameDataProvider::GetPartyData(
    std::uint32_t index) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index < party_.size()) return party_[index];
  return empty_data_;
}

const UnitFrameData& UnitFrameDataProvider::GetRaidData(
    std::uint32_t index) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index < raid_.size()) return raid_[index];
  return empty_data_;
}

const UnitFrameData& UnitFrameDataProvider::GetUnitData(
    const std::string& token) const {
  std::lock_guard<std::mutex> lock(mutex_);

  if (token == "player") return player_;
  if (token == "target") return target_;
  if (token == "focus") return focus_;
  if (token == "targettarget") return target_of_target_;
  if (token == "pet") return pet_;

  if (token.size() >= 6 && token.compare(0, 5, "party") == 0) {
    char digit = token[5];
    if (digit >= '1' && digit <= '4') {
      std::uint32_t idx = static_cast<std::uint32_t>(digit - '1');
      if (idx < party_.size()) return party_[idx];
    }
  }

  if (token.size() >= 5 && token.compare(0, 4, "raid") == 0) {
    int num = 0;
    for (std::size_t i = 4; i < token.size(); ++i) {
      char c = token[i];
      if (c >= '0' && c <= '9')
        num = num * 10 + (c - '0');
      else
        return empty_data_;
    }
    if (num >= 1 && num <= 40) {
      return raid_[static_cast<std::size_t>(num - 1)];
    }
  }

  return empty_data_;
}

bool UnitFrameDataProvider::HasTarget() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return target_.has_data;
}

bool UnitFrameDataProvider::HasFocus() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return focus_.has_data;
}

bool UnitFrameDataProvider::HasPet() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pet_.has_data;
}

bool UnitFrameDataProvider::HasPartyMember(std::uint32_t index) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index < party_.size()) return party_[index].has_data;
  return false;
}

void UnitFrameDataProvider::SetPlayerData(const UnitFrameData& data) {
  std::lock_guard<std::mutex> lock(mutex_);
  player_ = data;
}

void UnitFrameDataProvider::SetTargetData(const UnitFrameData& data) {
  std::lock_guard<std::mutex> lock(mutex_);
  target_ = data;
}

void UnitFrameDataProvider::SetFocusData(const UnitFrameData& data) {
  std::lock_guard<std::mutex> lock(mutex_);
  focus_ = data;
}

void UnitFrameDataProvider::SetTargetOfTargetData(const UnitFrameData& data) {
  std::lock_guard<std::mutex> lock(mutex_);
  target_of_target_ = data;
}

void UnitFrameDataProvider::SetPetData(const UnitFrameData& data) {
  std::lock_guard<std::mutex> lock(mutex_);
  pet_ = data;
}

void UnitFrameDataProvider::SetPartyData(std::uint32_t index,
                                         const UnitFrameData& data) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index < party_.size()) party_[index] = data;
}

void UnitFrameDataProvider::SetRaidData(std::uint32_t index,
                                        const UnitFrameData& data) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (index < raid_.size()) raid_[index] = data;
}

void UnitFrameDataProvider::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  player_ = UnitFrameData{};
  target_ = UnitFrameData{};
  focus_ = UnitFrameData{};
  target_of_target_ = UnitFrameData{};
  pet_ = UnitFrameData{};
  for (auto& p : party_) p = UnitFrameData{};
  for (auto& r : raid_) r = UnitFrameData{};
}

}

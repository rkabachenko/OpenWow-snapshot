
#include "openwow/game/player_data_view.h"

namespace openwow::game {

void PlayerDataView::SetPlayerData(const PlayerDataSnapshot& data) {
  data_ = data;
}

void PlayerDataView::Reset() {
  data_.reset();
}

std::optional<PlayerDataSnapshot> PlayerDataView::GetPlayerData() const {
  return data_;
}

std::string PlayerDataView::GetName() const {
  return data_ ? data_->name : std::string{};
}

std::uint32_t PlayerDataView::GetLevel() const {
  return data_ ? data_->level : 0;
}

std::uint32_t PlayerDataView::GetXP() const {
  return data_ ? data_->xp : 0;
}

std::uint32_t PlayerDataView::GetNextLevelXP() const {
  return data_ ? data_->nextLevelXP : 0;
}

float PlayerDataView::GetXPPercent() const {
  if (!data_ || data_->nextLevelXP == 0) return 0.0f;
  return (static_cast<float>(data_->xp) /
          static_cast<float>(data_->nextLevelXP)) *
         100.0f;
}

std::uint64_t PlayerDataView::GetMoney() const {
  return data_ ? data_->money : 0;
}

GoldSilverCopper PlayerDataView::GetMoneyAsGoldSilverCopper() const {
  if (!data_) return {};
  const auto total = data_->money;
  GoldSilverCopper gsc;
  gsc.gold = static_cast<std::uint32_t>(total / 10000);
  gsc.silver = static_cast<std::uint32_t>((total % 10000) / 100);
  gsc.copper = static_cast<std::uint32_t>(total % 100);
  return gsc;
}

std::string PlayerDataView::GetGuildName() const {
  return data_ ? data_->guildName : std::string{};
}

std::string PlayerDataView::GetClassName() const {
  return data_ ? data_->classInfo.className : std::string{};
}

std::uint32_t PlayerDataView::GetClassId() const {
  return data_ ? data_->classInfo.classId : 0;
}

std::uint32_t PlayerDataView::GetTitleId() const {
  return data_ ? data_->titleId : 0;
}

std::uint32_t PlayerDataView::GetAchievementPoints() const {
  return data_ ? data_->achievementPoints : 0;
}

std::uint32_t PlayerDataView::GetTotalHKs() const {
  return data_ ? data_->totalHKs : 0;
}

std::uint32_t PlayerDataView::GetHonorPoints() const {
  return data_ ? data_->honorPoints : 0;
}

std::uint32_t PlayerDataView::GetArenaPoints() const {
  return data_ ? data_->arenaPoints : 0;
}

std::uint32_t PlayerDataView::GetRestedXP() const {
  return data_ ? data_->restedXP : 0;
}

std::string PlayerDataView::GetRealmName() const {
  return data_ ? data_->realmName : std::string{};
}

std::string PlayerDataView::FormatMoney() const {
  if (!data_) return "0c";
  uint64_t copper = data_->money;
  uint32_t gold   = static_cast<uint32_t>(copper / 10000);
  uint32_t silver = static_cast<uint32_t>((copper % 10000) / 100);
  uint32_t rem    = static_cast<uint32_t>(copper % 100);

  std::string result;
  if (gold > 0)   result += std::to_string(gold) + "g ";
  if (silver > 0 || gold > 0) result += std::to_string(silver) + "s ";
  result += std::to_string(rem) + "c";
  return result;
}

std::string PlayerDataView::FormatLevel() const {
  if (!data_) return "Level 0";
  std::string result = "Level " + std::to_string(data_->level);
  if (!data_->classInfo.className.empty()) {
    result += " " + data_->classInfo.className;
  }
  if (!data_->classInfo.specName.empty()) {
    result += " (" + data_->classInfo.specName + ")";
  }
  return result;
}

ObjectGuid PlayerDataView::GetGuid() const {
  return data_ ? data_->guid : ObjectGuid{};
}

bool PlayerDataView::IsMaxLevel() const {
  return data_ && data_->level >= maxLevel_;
}

void PlayerDataView::SetMaxLevel(std::uint32_t maxLevel) {
  maxLevel_ = maxLevel;
}

bool PlayerDataView::HasData() const {
  return data_.has_value();
}

}

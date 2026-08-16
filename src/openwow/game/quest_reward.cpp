#include "openwow/game/quest_reward.h"

#include <algorithm>
#include <sstream>

namespace openwow::game {

void QuestRewardChooser::SetRewardData(std::uint32_t questId,
                                       QuestRewardData data) {
  if (questId == 0) return;
  questId_ = questId;
  data_ = std::move(data);
  selectedChoice_ = -1;
  open_ = true;
}

std::optional<QuestRewardData> QuestRewardChooser::GetRewardData() const {
  return data_;
}

std::uint32_t QuestRewardChooser::GetQuestId() const { return questId_; }

std::vector<QuestRewardItem> QuestRewardChooser::GetFixedRewards() const {
  if (!data_) return {};
  return data_->fixedRewards;
}

std::vector<QuestRewardItem> QuestRewardChooser::GetChoiceRewards() const {
  if (!data_) return {};
  return data_->choiceRewards;
}

std::vector<QuestRewardReputation>
QuestRewardChooser::GetRewardReputations() const {
  if (!data_) return {};
  return data_->rewardReputations;
}

std::uint32_t QuestRewardChooser::GetRewardMoney() const {
  if (!data_) return 0;
  return data_->rewardMoney;
}

std::uint32_t QuestRewardChooser::GetRewardXP() const {
  if (!data_) return 0;
  return data_->rewardXP;
}

std::string QuestRewardChooser::FormatMoney(std::uint32_t copper) {
  if (copper == 0) return "0c";

  std::uint32_t gold   = copper / 10000;
  std::uint32_t silver = (copper % 10000) / 100;
  std::uint32_t cop    = copper % 100;

  std::ostringstream oss;
  if (gold > 0) oss << gold << "g";
  if (silver > 0) {
    if (gold > 0) oss << " ";
    oss << silver << "s";
  }
  if (cop > 0) {
    if (gold > 0 || silver > 0) oss << " ";
    oss << cop << "c";
  }
  return oss.str();
}

std::string QuestRewardChooser::GetRewardMoneyString() const {
  return FormatMoney(GetRewardMoney());
}

std::string QuestRewardChooser::GetRequiredMoneyString() const {
  if (!data_) return "";
  return FormatMoney(data_->requiredMoney);
}

bool QuestRewardChooser::HasRequiredMoney() const {
  return data_.has_value() && data_->requiredMoney > 0;
}

void QuestRewardChooser::SetSelectedChoice(std::uint32_t index) {
  if (!data_) return;
  if (index < static_cast<std::uint32_t>(data_->choiceRewards.size())) {
    selectedChoice_ = static_cast<std::int32_t>(index);
  }
}

std::int32_t QuestRewardChooser::GetSelectedChoice() const {
  return selectedChoice_;
}

bool QuestRewardChooser::IsValidChoice(std::int32_t index) const {
  if (!data_) return false;
  if (index < 0) return false;
  return static_cast<std::uint32_t>(index) < data_->choiceRewards.size();
}

std::int32_t QuestRewardChooser::GetBestChoiceByQuality() const {
  if (!data_ || data_->choiceRewards.empty()) return -1;

  std::int32_t bestIdx = 0;
  std::uint32_t bestQuality = data_->choiceRewards[0].quality;

  for (std::size_t i = 1; i < data_->choiceRewards.size(); ++i) {
    if (data_->choiceRewards[i].quality > bestQuality) {
      bestQuality = data_->choiceRewards[i].quality;
      bestIdx = static_cast<std::int32_t>(i);
    }
  }
  return bestIdx;
}

bool QuestRewardChooser::HasChoiceRewards() const {
  return data_.has_value() && !data_->choiceRewards.empty();
}

bool QuestRewardChooser::HasRewardMoney() const {
  return data_.has_value() && data_->rewardMoney > 0;
}

bool QuestRewardChooser::HasRewardXP() const {
  return data_.has_value() && data_->rewardXP > 0;
}

std::uint32_t QuestRewardChooser::GetChoiceCount() const {
  if (!data_) return 0;
  return static_cast<std::uint32_t>(data_->choiceRewards.size());
}

std::uint32_t QuestRewardChooser::GetFixedRewardCount() const {
  if (!data_) return 0;
  return static_cast<std::uint32_t>(data_->fixedRewards.size());
}

std::uint32_t QuestRewardChooser::GetRewardHonor() const {
  if (!data_) return 0;
  return data_->rewardHonor;
}

std::uint32_t QuestRewardChooser::GetRewardArenaPoints() const {
  if (!data_) return 0;
  return data_->rewardArenaPoints;
}

std::uint32_t QuestRewardChooser::GetRewardTalentPoints() const {
  if (!data_) return 0;
  return data_->rewardTalentPoints;
}

bool QuestRewardChooser::HasRewardSpell() const {
  return data_.has_value() && data_->rewardSpell.has_value();
}

std::optional<QuestRewardSpell> QuestRewardChooser::GetRewardSpell() const {
  if (!data_) return std::nullopt;
  return data_->rewardSpell;
}

bool QuestRewardChooser::IsOpen() const { return open_; }

void QuestRewardChooser::Open() { open_ = true; }

void QuestRewardChooser::Close() {
  open_ = false;
  selectedChoice_ = -1;
}

bool QuestRewardChooser::CanComplete() const {
  if (!data_) return false;
  if (!open_) return false;

  if (!data_->choiceRewards.empty() && selectedChoice_ < 0) return false;

  if (!data_->choiceRewards.empty() &&
      static_cast<std::uint32_t>(selectedChoice_) >=
          data_->choiceRewards.size()) {
    return false;
  }
  return true;
}

void QuestRewardChooser::Reset() {
  questId_ = 0;
  data_.reset();
  selectedChoice_ = -1;
  open_ = false;
}

}

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

struct QuestRewardItem {
  std::uint32_t itemId{0};
  std::uint32_t displayId{0};
  std::string name;
  std::uint32_t count{1};
  std::uint32_t quality{0};
};

struct QuestRewardReputation {
  std::uint32_t factionId{0};
  std::string factionName;
  std::int32_t amount{0};
};

struct QuestRewardSpell {
  std::uint32_t spellId{0};
  std::string name;
  std::string icon;
};

struct QuestRewardData {
  std::uint32_t requiredMoney{0};
  std::uint32_t rewardMoney{0};
  std::uint32_t rewardXP{0};
  std::uint32_t rewardHonor{0};
  std::uint32_t rewardArenaPoints{0};
  std::uint32_t rewardTalentPoints{0};

  std::vector<QuestRewardItem> fixedRewards;
  std::vector<QuestRewardItem> choiceRewards;

  std::vector<QuestRewardReputation> rewardReputations;

  std::optional<QuestRewardSpell> rewardSpell;
  std::uint32_t rewardTitle{0};
};

class QuestRewardChooser {
 public:

  void SetRewardData(std::uint32_t questId, QuestRewardData data);

  [[nodiscard]] std::optional<QuestRewardData> GetRewardData() const;

  [[nodiscard]] std::uint32_t GetQuestId() const;

  [[nodiscard]] std::vector<QuestRewardItem> GetFixedRewards() const;
  [[nodiscard]] std::vector<QuestRewardItem> GetChoiceRewards() const;
  [[nodiscard]] std::vector<QuestRewardReputation> GetRewardReputations() const;

  [[nodiscard]] std::uint32_t GetRewardMoney() const;
  [[nodiscard]] std::uint32_t GetRewardXP() const;

  void SetSelectedChoice(std::uint32_t index);
  [[nodiscard]] std::int32_t GetSelectedChoice() const;

  [[nodiscard]] bool HasChoiceRewards() const;
  [[nodiscard]] bool HasRewardMoney() const;
  [[nodiscard]] bool HasRewardXP() const;

  [[nodiscard]] bool IsOpen() const;
  void Open();
  void Close();

  [[nodiscard]] bool CanComplete() const;

  [[nodiscard]] std::int32_t GetBestChoiceByQuality() const;

  [[nodiscard]] bool IsValidChoice(std::int32_t index) const;

  [[nodiscard]] std::uint32_t GetChoiceCount() const;

  [[nodiscard]] std::uint32_t GetFixedRewardCount() const;

  [[nodiscard]] static std::string FormatMoney(std::uint32_t copper);

  [[nodiscard]] std::string GetRewardMoneyString() const;

  [[nodiscard]] std::string GetRequiredMoneyString() const;

  [[nodiscard]] bool HasRequiredMoney() const;

  [[nodiscard]] std::uint32_t GetRewardHonor() const;

  [[nodiscard]] std::uint32_t GetRewardArenaPoints() const;

  [[nodiscard]] std::uint32_t GetRewardTalentPoints() const;

  [[nodiscard]] bool HasRewardSpell() const;

  [[nodiscard]] std::optional<QuestRewardSpell> GetRewardSpell() const;

  void Reset();

 private:
  std::uint32_t questId_{0};
  std::optional<QuestRewardData> data_;
  std::int32_t selectedChoice_{-1};
  bool open_{false};
};

}

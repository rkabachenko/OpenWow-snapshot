#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openwow::game {

struct InGameHelpTopic {
  std::uint32_t topicId{0};
  std::string title;
  std::string body;
  std::string category;
  std::uint32_t parentId{0};
};

struct TutorialFlagEntry {
  std::uint32_t flagIndex{0};
  bool isSeen{false};
};

class HelpSystem {
 public:

  void AddTopic(const InGameHelpTopic& topic);

  [[nodiscard]] std::optional<InGameHelpTopic> GetTopic(
      std::uint32_t topicId) const;

  [[nodiscard]] std::vector<InGameHelpTopic> GetTopicsForCategory(
      const std::string& category) const;

  [[nodiscard]] std::vector<InGameHelpTopic> GetChildren(
      std::uint32_t parentId) const;

  [[nodiscard]] std::vector<InGameHelpTopic> GetRootTopics() const;

  [[nodiscard]] std::vector<InGameHelpTopic> SearchTopics(
      const std::string& query) const;

  [[nodiscard]] std::uint32_t GetTopicCount() const;

  void SetTutorialSeen(std::uint32_t flagIndex);
  [[nodiscard]] bool IsTutorialSeen(std::uint32_t flagIndex) const;

  void SetAllTutorialsSeen();
  void ResetTutorials();

  [[nodiscard]] std::uint32_t GetSeenTutorialCount() const;

  [[nodiscard]] std::uint32_t GetTotalTutorialCount() const;

  [[nodiscard]] bool IsOpen() const;
  void Open();
  void Close();

  void Reset();

 private:
  mutable std::mutex mutex_;
  std::vector<InGameHelpTopic> topics_;
  std::unordered_set<std::uint32_t> seen_tutorials_;
  std::uint32_t max_flag_index_{0};
  bool open_{false};
};

}

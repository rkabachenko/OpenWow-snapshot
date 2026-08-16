
#include "openwow/game/help_system.h"

#include <algorithm>
#include <cctype>

namespace openwow::game {

namespace {

bool ContainsCaseInsensitive(const std::string& haystack,
                             const std::string& needle) {
  if (needle.empty()) return true;
  if (haystack.size() < needle.size()) return false;
  auto it = std::search(
      haystack.begin(), haystack.end(), needle.begin(), needle.end(),
      [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) ==
               std::tolower(static_cast<unsigned char>(b));
      });
  return it != haystack.end();
}

}

void HelpSystem::AddTopic(const InGameHelpTopic& topic) {
  std::lock_guard lock(mutex_);

  for (const auto& t : topics_) {
    if (t.topicId == topic.topicId) return;
  }
  topics_.push_back(topic);
}

std::optional<InGameHelpTopic> HelpSystem::GetTopic(
    std::uint32_t topicId) const {
  std::lock_guard lock(mutex_);
  for (const auto& t : topics_) {
    if (t.topicId == topicId) return t;
  }
  return std::nullopt;
}

std::vector<InGameHelpTopic> HelpSystem::GetTopicsForCategory(
    const std::string& category) const {
  std::lock_guard lock(mutex_);
  std::vector<InGameHelpTopic> result;
  for (const auto& t : topics_) {
    if (t.category == category) result.push_back(t);
  }
  return result;
}

std::vector<InGameHelpTopic> HelpSystem::GetChildren(
    std::uint32_t parentId) const {
  std::lock_guard lock(mutex_);
  std::vector<InGameHelpTopic> result;
  for (const auto& t : topics_) {
    if (t.parentId == parentId) result.push_back(t);
  }
  return result;
}

std::vector<InGameHelpTopic> HelpSystem::GetRootTopics() const {
  return GetChildren(0);
}

std::vector<InGameHelpTopic> HelpSystem::SearchTopics(
    const std::string& query) const {
  std::lock_guard lock(mutex_);
  std::vector<InGameHelpTopic> result;
  for (const auto& t : topics_) {
    if (ContainsCaseInsensitive(t.title, query) ||
        ContainsCaseInsensitive(t.body, query)) {
      result.push_back(t);
    }
  }
  return result;
}

std::uint32_t HelpSystem::GetTopicCount() const {
  std::lock_guard lock(mutex_);
  return static_cast<std::uint32_t>(topics_.size());
}

void HelpSystem::SetTutorialSeen(std::uint32_t flagIndex) {
  std::lock_guard lock(mutex_);
  seen_tutorials_.insert(flagIndex);
  if (flagIndex >= max_flag_index_) {
    max_flag_index_ = flagIndex + 1;
  }
}

bool HelpSystem::IsTutorialSeen(std::uint32_t flagIndex) const {
  std::lock_guard lock(mutex_);
  return seen_tutorials_.count(flagIndex) > 0;
}

void HelpSystem::SetAllTutorialsSeen() {
  std::lock_guard lock(mutex_);
  for (std::uint32_t i = 0; i < max_flag_index_; ++i) {
    seen_tutorials_.insert(i);
  }
}

void HelpSystem::ResetTutorials() {
  std::lock_guard lock(mutex_);
  seen_tutorials_.clear();

}

std::uint32_t HelpSystem::GetSeenTutorialCount() const {
  std::lock_guard lock(mutex_);
  return static_cast<std::uint32_t>(seen_tutorials_.size());
}

std::uint32_t HelpSystem::GetTotalTutorialCount() const {
  std::lock_guard lock(mutex_);
  return max_flag_index_;
}

bool HelpSystem::IsOpen() const {
  std::lock_guard lock(mutex_);
  return open_;
}

void HelpSystem::Open() {
  std::lock_guard lock(mutex_);
  open_ = true;
}

void HelpSystem::Close() {
  std::lock_guard lock(mutex_);
  open_ = false;
}

void HelpSystem::Reset() {
  std::lock_guard lock(mutex_);
  topics_.clear();
  seen_tutorials_.clear();
  max_flag_index_ = 0;
  open_ = false;
}

}


#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openwow::game {

enum class FilterLevel : uint8_t {
  None = 0,
  Light = 1,
  Medium = 2,
  Heavy = 3,
};

class ChatFilter {
 public:
  static ChatFilter& Get();

  void SetFilterLevel(FilterLevel level);
  [[nodiscard]] FilterLevel GetFilterLevel() const;

  void SetMature(bool enabled);
  [[nodiscard]] bool IsMature() const;

  void SetMatureFilterEnabled(bool enabled) { SetMature(enabled); }
  [[nodiscard]] bool IsMatureFilterEnabled() const { return IsMature(); }

  void AddWord(const std::string& word);
  void RemoveWord(const std::string& word);
  [[nodiscard]] uint32_t GetFilteredWordCount() const;

  void AddFilterWord(const std::string& word) { AddWord(word); }
  void RemoveFilterWord(const std::string& word) { RemoveWord(word); }
  void ClearFilterWords();
  [[nodiscard]] std::size_t GetFilterWordCount() const {
    return GetFilteredWordCount();
  }

  [[nodiscard]] std::string FilterMessage(const std::string& input) const;

  [[nodiscard]] bool ContainsProfanity(const std::string& input) const;

  [[nodiscard]] std::string FilterText(const std::string& text) const {
    return FilterMessage(text);
  }
  [[nodiscard]] bool ContainsBadWord(const std::string& text) const {
    return ContainsProfanity(text);
  }

  void AddException(const std::string& word);
  void RemoveException(const std::string& word);
  [[nodiscard]] uint32_t GetExceptionCount() const;

  [[nodiscard]] std::string FilterURL(const std::string& input) const;

  [[nodiscard]] bool IsSpam(const std::string& input) const;

  void SetSpamThreshold(uint32_t repeats);
  [[nodiscard]] uint32_t GetSpamThreshold() const;

  struct SpamInfo {
    float messages_per_minute = 0;
    std::uint32_t repeated_count = 0;
    bool is_spam = false;
  };
  SpamInfo CheckSpam(const ObjectGuid& sender, const std::string& message);

  void LoadDefaultFilters();

  void AddIgnored(const ObjectGuid& guid, const std::string& name);
  void RemoveIgnored(const ObjectGuid& guid);
  [[nodiscard]] bool IsIgnored(const ObjectGuid& guid) const;
  [[nodiscard]] bool IsIgnored(const std::string& name) const;
  [[nodiscard]] std::size_t GetIgnoredCount() const;

  void SetChannelFiltered(std::uint32_t channelType, bool filtered);
  [[nodiscard]] bool IsChannelFiltered(std::uint32_t channelType) const;

  void Clear();

  void Reset();

 private:
  ChatFilter() = default;

  [[nodiscard]] bool ContainsWordCI(const std::string& text,
                                    const std::string& word) const;

  [[nodiscard]] bool IsException(const std::string& word) const;

  FilterLevel filter_level_ = FilterLevel::Medium;
  bool mature_filter_ = true;
  uint32_t spam_threshold_ = 3;
  std::vector<std::string> filter_words_;
  std::unordered_set<std::string> exceptions_;

  struct SenderHistory {
    std::vector<std::uint32_t> timestamps;
    std::string last_message;
    std::uint32_t repeat_count = 0;
  };
  std::unordered_map<std::uint64_t, SenderHistory> sender_history_;
  std::unordered_map<std::uint64_t, std::string> ignored_;
  std::unordered_set<std::string> ignored_names_;
  std::unordered_set<std::uint32_t> filtered_channels_;

  mutable std::mutex mutex_;
};

}


#include "openwow/game/chat_filter.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <regex>

namespace openwow::game {

ChatFilter& ChatFilter::Get() {
  static ChatFilter instance;
  return instance;
}

void ChatFilter::SetFilterLevel(FilterLevel level) {
  std::lock_guard<std::mutex> lock(mutex_);
  filter_level_ = level;
}

FilterLevel ChatFilter::GetFilterLevel() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return filter_level_;
}

void ChatFilter::SetMature(bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  mature_filter_ = enabled;
}

bool ChatFilter::IsMature() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return mature_filter_;
}

static std::string ToLowerStr(const std::string& s) {
  std::string result = s;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}

bool ChatFilter::ContainsWordCI(const std::string& text,
                                const std::string& word) const {
  if (word.empty() || text.size() < word.size()) return false;

  std::string text_lower = ToLowerStr(text);
  std::string word_lower = ToLowerStr(word);

  return text_lower.find(word_lower) != std::string::npos;
}

bool ChatFilter::IsException(const std::string& word) const {
  return exceptions_.count(ToLowerStr(word)) > 0;
}

void ChatFilter::AddWord(const std::string& word) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string lower = ToLowerStr(word);

  for (const auto& existing : filter_words_) {
    if (ToLowerStr(existing) == lower) return;
  }
  filter_words_.push_back(word);
}

void ChatFilter::RemoveWord(const std::string& word) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string lower = ToLowerStr(word);
  filter_words_.erase(
      std::remove_if(filter_words_.begin(), filter_words_.end(),
                     [&](const std::string& w) {
                       return ToLowerStr(w) == lower;
                     }),
      filter_words_.end());
}

uint32_t ChatFilter::GetFilteredWordCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<uint32_t>(filter_words_.size());
}

void ChatFilter::ClearFilterWords() {
  std::lock_guard<std::mutex> lock(mutex_);
  filter_words_.clear();
}

std::string ChatFilter::FilterMessage(const std::string& input) const {
  std::lock_guard<std::mutex> lock(mutex_);

  if (filter_level_ == FilterLevel::None) return input;
  if (!mature_filter_ || filter_words_.empty()) return input;

  std::string result = input;
  std::string result_lower = ToLowerStr(result);

  for (const auto& word : filter_words_) {
    if (word.empty()) continue;
    if (exceptions_.count(ToLowerStr(word))) continue;

    std::string word_lower = ToLowerStr(word);
    std::size_t pos = 0;

    while ((pos = result_lower.find(word_lower, pos)) != std::string::npos) {

      std::string matched = result.substr(pos, word.size());
      if (exceptions_.count(ToLowerStr(matched))) {
        pos += word.size();
        continue;
      }

      char rep_char = '*';
      if (filter_level_ == FilterLevel::Heavy) rep_char = '#';

      std::string replacement(word.size(), rep_char);
      result.replace(pos, word.size(), replacement);
      result_lower.replace(pos, word.size(), replacement);
      pos += replacement.size();
    }
  }
  return result;
}

bool ChatFilter::ContainsProfanity(const std::string& input) const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string input_lower = ToLowerStr(input);
  for (const auto& word : filter_words_) {
    if (word.empty()) continue;
    if (exceptions_.count(ToLowerStr(word))) continue;
    std::string word_lower = ToLowerStr(word);
    if (input_lower.find(word_lower) != std::string::npos) return true;
  }
  return false;
}

void ChatFilter::AddException(const std::string& word) {
  std::lock_guard<std::mutex> lock(mutex_);
  exceptions_.insert(ToLowerStr(word));
}

void ChatFilter::RemoveException(const std::string& word) {
  std::lock_guard<std::mutex> lock(mutex_);
  exceptions_.erase(ToLowerStr(word));
}

uint32_t ChatFilter::GetExceptionCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<uint32_t>(exceptions_.size());
}

std::string ChatFilter::FilterURL(const std::string& input) const {

  try {
    static const std::regex url_regex(
        R"((https?://\S+|www\.\S+))", std::regex::icase);
    return std::regex_replace(input, url_regex, "");
  } catch (...) {
    return input;
  }
}

bool ChatFilter::IsSpam(const std::string& input) const {
  if (input.empty()) return false;

  if (input.size() >= 10) {
    size_t upper_count = 0;
    size_t alpha_count = 0;
    for (unsigned char c : input) {
      if (std::isalpha(c)) {
        ++alpha_count;
        if (std::isupper(c)) ++upper_count;
      }
    }
    if (alpha_count > 0 &&
        static_cast<float>(upper_count) / static_cast<float>(alpha_count) >
            0.8f) {
      return true;
    }
  }

  if (input.size() >= 8) {
    size_t max_run = 1, run = 1;
    for (size_t i = 1; i < input.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(input[i])) ==
          std::tolower(static_cast<unsigned char>(input[i - 1]))) {
        ++run;
        if (run > max_run) max_run = run;
      } else {
        run = 1;
      }
    }
    if (max_run >= spam_threshold_ + 3) return true;
  }

  return false;
}

void ChatFilter::SetSpamThreshold(uint32_t repeats) {
  std::lock_guard<std::mutex> lock(mutex_);
  spam_threshold_ = repeats;
}

uint32_t ChatFilter::GetSpamThreshold() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return spam_threshold_;
}

static std::uint32_t GetCurrentTimeSec() {
  auto now = std::chrono::steady_clock::now();
  return static_cast<std::uint32_t>(
      std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch())
          .count());
}

ChatFilter::SpamInfo ChatFilter::CheckSpam(const ObjectGuid& sender,
                                           const std::string& message) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto raw = sender.GetRawValue();
  auto now = GetCurrentTimeSec();

  auto& history = sender_history_[raw];
  history.timestamps.push_back(now);

  history.timestamps.erase(
      std::remove_if(history.timestamps.begin(), history.timestamps.end(),
                     [now](std::uint32_t ts) { return (now - ts) > 60; }),
      history.timestamps.end());

  if (message == history.last_message) {
    ++history.repeat_count;
  } else {
    history.repeat_count = 1;
    history.last_message = message;
  }

  SpamInfo info;
  info.messages_per_minute = static_cast<float>(history.timestamps.size());
  info.repeated_count = history.repeat_count;
  info.is_spam =
      (info.messages_per_minute > 10.0f) ||
      (info.repeated_count > spam_threshold_);

  return info;
}

void ChatFilter::LoadDefaultFilters() {
  std::lock_guard<std::mutex> lock(mutex_);
  const char* defaults[] = {"badword", "offensive", "profanity", "curseword"};
  for (const auto& w : defaults) {
    std::string lower = ToLowerStr(w);
    bool exists = false;
    for (const auto& existing : filter_words_) {
      if (ToLowerStr(existing) == lower) {
        exists = true;
        break;
      }
    }
    if (!exists) filter_words_.emplace_back(w);
  }
}

void ChatFilter::AddIgnored(const ObjectGuid& guid, const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  ignored_[guid.GetRawValue()] = name;
  if (!name.empty()) {
    ignored_names_.insert(ToLowerStr(name));
  }
}

void ChatFilter::RemoveIgnored(const ObjectGuid& guid) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = ignored_.find(guid.GetRawValue());
  if (it != ignored_.end()) {
    ignored_names_.erase(ToLowerStr(it->second));
    ignored_.erase(it);
  }
}

bool ChatFilter::IsIgnored(const ObjectGuid& guid) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ignored_.find(guid.GetRawValue()) != ignored_.end();
}

bool ChatFilter::IsIgnored(const std::string& name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ignored_names_.find(ToLowerStr(name)) != ignored_names_.end();
}

std::size_t ChatFilter::GetIgnoredCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return ignored_.size();
}

void ChatFilter::SetChannelFiltered(std::uint32_t channelType, bool filtered) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (filtered) {
    filtered_channels_.insert(channelType);
  } else {
    filtered_channels_.erase(channelType);
  }
}

bool ChatFilter::IsChannelFiltered(std::uint32_t channelType) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return filtered_channels_.find(channelType) != filtered_channels_.end();
}

void ChatFilter::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  filter_words_.clear();
  exceptions_.clear();
}

void ChatFilter::Reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  filter_level_ = FilterLevel::Medium;
  mature_filter_ = true;
  spam_threshold_ = 3;
  filter_words_.clear();
  exceptions_.clear();
  sender_history_.clear();
  ignored_.clear();
  ignored_names_.clear();
  filtered_channels_.clear();
}

}

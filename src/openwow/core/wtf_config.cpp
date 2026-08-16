
#include "openwow/core/wtf_config.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <sstream>

namespace openwow::core {

static std::string TrimWhitespace(const std::string& s) {
  auto start = s.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return {};
  auto end = s.find_last_not_of(" \t\r\n");
  return s.substr(start, end - start + 1);
}

static std::string StripQuotes(const std::string& s) {
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
    return s.substr(1, s.size() - 2);
  }
  return s;
}

bool WTFConfig::ParseFile(const std::string& content) {
  std::istringstream stream(content);
  std::string line;
  bool parsed_any = false;

  while (std::getline(stream, line)) {
    line = TrimWhitespace(line);
    if (line.empty() || line[0] == '#' || line[0] == '-') continue;

    if (line.size() < 5) continue;

    std::string prefix = line.substr(0, 4);
    if (prefix != "SET " && prefix != "set " && prefix != "Set ") continue;

    std::string rest = TrimWhitespace(line.substr(4));
    if (rest.empty()) continue;

    auto space_pos = rest.find_first_of(" \t");
    if (space_pos == std::string::npos) {

      Set(rest, "");
      parsed_any = true;
      continue;
    }

    std::string key = rest.substr(0, space_pos);
    std::string value = TrimWhitespace(rest.substr(space_pos + 1));
    value = StripQuotes(value);

    Set(key, value);
    parsed_any = true;
  }

  if (parsed_any) modified_ = false;
  return parsed_any;
}

std::string WTFConfig::GenerateFile() const {
  std::string result;
  for (const auto& key : order_) {
    auto it = entries_.find(key);
    if (it == entries_.end()) continue;
    result += "SET " + key + " \"" + it->second + "\"\n";
  }
  return result;
}

void WTFConfig::Set(const std::string& key, const std::string& value) {
  auto it = entries_.find(key);
  if (it == entries_.end()) {
    order_.push_back(key);
    entries_[key] = value;
  } else {
    it->second = value;
  }
  modified_ = true;
}

std::optional<std::string> WTFConfig::Get(const std::string& key) const {
  auto it = entries_.find(key);
  if (it != entries_.end()) return it->second;
  return std::nullopt;
}

std::optional<int> WTFConfig::GetInt(const std::string& key) const {
  auto val = Get(key);
  if (!val) return std::nullopt;

  int result = 0;
  auto [ptr, ec] =
      std::from_chars(val->data(), val->data() + val->size(), result);
  if (ec != std::errc()) return std::nullopt;
  return result;
}

std::optional<float> WTFConfig::GetFloat(const std::string& key) const {
  auto val = Get(key);
  if (!val) return std::nullopt;

  try {
    return std::stof(*val);
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<bool> WTFConfig::GetBool(const std::string& key) const {
  auto val = Get(key);
  if (!val) return std::nullopt;

  std::string lower = *val;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  if (lower == "1" || lower == "true" || lower == "yes") return true;
  if (lower == "0" || lower == "false" || lower == "no") return false;
  return std::nullopt;
}

void WTFConfig::Remove(const std::string& key) {
  auto it = entries_.find(key);
  if (it != entries_.end()) {
    entries_.erase(it);
    order_.erase(std::remove(order_.begin(), order_.end(), key), order_.end());
    modified_ = true;
  }
}

bool WTFConfig::Has(const std::string& key) const {
  return entries_.find(key) != entries_.end();
}

std::vector<std::string> WTFConfig::GetKeys() const {
  return order_;
}

std::vector<std::pair<std::string, std::string>> WTFConfig::GetAll() const {
  std::vector<std::pair<std::string, std::string>> result;
  result.reserve(order_.size());
  for (const auto& key : order_) {
    auto it = entries_.find(key);
    if (it != entries_.end()) {
      result.emplace_back(key, it->second);
    }
  }
  return result;
}

uint32_t WTFConfig::GetEntryCount() const {
  return static_cast<uint32_t>(entries_.size());
}

void WTFConfig::Clear() {
  entries_.clear();
  order_.clear();
  modified_ = true;
}

void WTFConfig::SetAccountName(const std::string& name) {
  account_name_ = name;
}

const std::string& WTFConfig::GetAccountName() const {
  return account_name_;
}

void WTFConfig::SetRealmName(const std::string& name) { realm_name_ = name; }

const std::string& WTFConfig::GetRealmName() const { return realm_name_; }

void WTFConfig::SetCharacterName(const std::string& name) {
  character_name_ = name;
}

const std::string& WTFConfig::GetCharacterName() const {
  return character_name_;
}

std::string WTFConfig::GetConfigPath() const {
  if (account_name_.empty()) {
    return "WTF/Config.wtf";
  }
  if (!realm_name_.empty() && !character_name_.empty()) {
    return "WTF/Account/" + account_name_ + "/" + realm_name_ + "/" +
           character_name_ + "/config-cache.wtf";
  }

  return "WTF/Account/" + account_name_ + "/config-cache.wtf";
}

bool WTFConfig::IsModified() const { return modified_; }

void WTFConfig::MarkSaved() { modified_ = false; }

}

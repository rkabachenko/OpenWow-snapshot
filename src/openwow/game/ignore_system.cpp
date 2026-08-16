
#include "openwow/game/ignore_system.h"

#include <algorithm>
#include <cctype>

namespace openwow::game {

IgnoreSystem& IgnoreSystem::Get() {
  static IgnoreSystem instance;
  return instance;
}

std::string IgnoreSystem::NormalizeName(const std::string& name) {
  std::string result = name;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}

IgnoreEntry* IgnoreSystem::FindEntry(std::uint64_t guid) {
  for (auto& e : entries_) {
    if (e.guid == guid) return &e;
  }
  return nullptr;
}

const IgnoreEntry* IgnoreSystem::FindEntry(std::uint64_t guid) const {
  for (const auto& e : entries_) {
    if (e.guid == guid) return &e;
  }
  return nullptr;
}

bool IgnoreSystem::AddIgnore(std::uint64_t guid, const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (entries_.size() >= kMaxIgnores) return false;

  if (FindEntry(guid)) return false;

  IgnoreEntry entry;
  entry.guid = guid;
  entry.name = name;
  entries_.push_back(std::move(entry));
  return true;
}

bool IgnoreSystem::RemoveIgnore(std::uint64_t guid) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = std::find_if(entries_.begin(), entries_.end(),
                         [guid](const IgnoreEntry& e) {
                           return e.guid == guid;
                         });
  if (it == entries_.end()) return false;

  entries_.erase(it);
  channel_mutes_.erase(guid);
  return true;
}

bool IgnoreSystem::IsIgnored(std::uint64_t guid) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return FindEntry(guid) != nullptr;
}

bool IgnoreSystem::IsIgnoredByName(const std::string& name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto normalized = NormalizeName(name);
  for (const auto& e : entries_) {
    if (NormalizeName(e.name) == normalized) return true;
  }
  return false;
}

std::vector<IgnoreEntry> IgnoreSystem::GetIgnoreList() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return entries_;
}

std::uint32_t IgnoreSystem::GetIgnoreCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<std::uint32_t>(entries_.size());
}

bool IgnoreSystem::IsAtCap() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return entries_.size() >= kMaxIgnores;
}

void IgnoreSystem::SetReason(std::uint64_t guid, const std::string& reason) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto* entry = FindEntry(guid);
  if (entry) entry->reason = reason;
}

void IgnoreSystem::ForEach(const IgnoreCallback& callback) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& e : entries_) {
    callback(e);
  }
}

void IgnoreSystem::MuteInChannel(std::uint64_t guid,
                                 const std::string& channel_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  channel_mutes_[guid].insert(NormalizeName(channel_name));
}

void IgnoreSystem::UnmuteInChannel(std::uint64_t guid,
                                   const std::string& channel_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = channel_mutes_.find(guid);
  if (it != channel_mutes_.end()) {
    it->second.erase(NormalizeName(channel_name));
    if (it->second.empty()) {
      channel_mutes_.erase(it);
    }
  }
}

bool IgnoreSystem::IsMutedInChannel(std::uint64_t guid,
                                    const std::string& channel_name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = channel_mutes_.find(guid);
  if (it == channel_mutes_.end()) return false;
  return it->second.count(NormalizeName(channel_name)) > 0;
}

void IgnoreSystem::ClearAll() {
  std::lock_guard<std::mutex> lock(mutex_);
  entries_.clear();
  channel_mutes_.clear();
}

void IgnoreSystem::Reset() {
  ClearAll();
}

}

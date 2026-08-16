#include "openwow/ui/strata_manager.h"

#include <algorithm>
#include <limits>

namespace openwow::ui {

void StrataManager::SetStrata(const std::string& frameName,
                              RenderStrata strata, FrameLevel level) {
  auto [it, inserted] = entries_.try_emplace(frameName);
  auto& stored = it->second;
  if (inserted) {
    stored.creation_order = next_creation_order_++;
  }
  auto& e = stored.entry;
  e.frameName = frameName;
  e.strata = strata;
  e.level = level;

}

RenderStrata StrataManager::GetStrata(const std::string& frameName) const {
  auto it = entries_.find(frameName);
  return it != entries_.end() ? it->second.entry.strata : RenderStrata::Medium;
}

FrameLevel StrataManager::GetLevel(const std::string& frameName) const {
  auto it = entries_.find(frameName);
  return it != entries_.end() ? it->second.entry.level : 0;
}

void StrataManager::SetShown(const std::string& frameName, bool shown) {
  auto [it, inserted] = entries_.try_emplace(frameName);
  auto& stored = it->second;
  if (inserted) {
    stored.creation_order = next_creation_order_++;
  }
  auto& e = stored.entry;
  e.frameName = frameName;
  if (!e.isShown && shown) {
    CompactVisibleLevels(e.strata);
    e.level = CountVisibleInStrata(e.strata);
  }
  e.isShown = shown;
}

bool StrataManager::IsShown(const std::string& frameName) const {
  auto it = entries_.find(frameName);
  return it != entries_.end() ? it->second.entry.isShown : true;
}

std::vector<StrataEntry> StrataManager::GetVisibleFrames() const {
  std::vector<const StoredEntry*> sorted_entries;
  sorted_entries.reserve(entries_.size());
  for (const auto& [_, stored] : entries_) {
    if (stored.entry.isShown) sorted_entries.push_back(&stored);
  }
  std::sort(sorted_entries.begin(), sorted_entries.end(),
            [](const StoredEntry* lhs, const StoredEntry* rhs) {
              if (lhs->entry.strata != rhs->entry.strata) {
                return static_cast<uint8_t>(lhs->entry.strata)
                    < static_cast<uint8_t>(rhs->entry.strata);
              }
              if (lhs->entry.level != rhs->entry.level) {
                return lhs->entry.level < rhs->entry.level;
              }
              return lhs->creation_order < rhs->creation_order;
            });

  std::vector<StrataEntry> result;
  result.reserve(sorted_entries.size());
  for (const StoredEntry* stored : sorted_entries) {
    result.push_back(stored->entry);
  }
  return result;
}

std::vector<StrataEntry> StrataManager::GetFramesByStrata(
    RenderStrata strata) const {
  std::vector<const StoredEntry*> sorted_entries;
  sorted_entries.reserve(entries_.size());
  for (const auto& [_, stored] : entries_) {
    if (stored.entry.strata == strata) sorted_entries.push_back(&stored);
  }

  std::sort(sorted_entries.begin(), sorted_entries.end(),
            [](const StoredEntry* lhs, const StoredEntry* rhs) {
              if (lhs->entry.level != rhs->entry.level) {
                return lhs->entry.level < rhs->entry.level;
              }
              return lhs->creation_order < rhs->creation_order;
            });

  std::vector<StrataEntry> result;
  result.reserve(sorted_entries.size());
  for (const StoredEntry* stored : sorted_entries) {
    result.push_back(stored->entry);
  }
  return result;
}

std::size_t StrataManager::GetFrameCount() const { return entries_.size(); }

std::size_t StrataManager::GetVisibleCount() const {
  std::size_t n = 0;
  for (const auto& [_, stored] : entries_) {
    if (stored.entry.isShown) ++n;
  }
  return n;
}

std::vector<std::string> StrataManager::GetRenderOrder() const {
  auto visible = GetVisibleFrames();
  std::vector<std::string> order;
  order.reserve(visible.size());
  for (const auto& e : visible) order.push_back(e.frameName);
  return order;
}

void StrataManager::Raise(const std::string& frameName) {
  auto it = entries_.find(frameName);
  if (it == entries_.end()) return;
  auto& target = it->second.entry;
  if (!target.isShown) return;

  CompactVisibleLevels(target.strata);
  target.level = CountVisibleInStrata(target.strata);
}

void StrataManager::Lower(const std::string& frameName) {
  auto it = entries_.find(frameName);
  if (it == entries_.end()) return;

  FrameLevel minLevel = std::numeric_limits<FrameLevel>::max();
  for (const auto& [name, stored] : entries_) {
    if (stored.entry.strata == it->second.entry.strata && name != frameName) {
      minLevel = std::min(minLevel, stored.entry.level);
    }
  }
  if (minLevel == std::numeric_limits<FrameLevel>::max()) {
    return;
  }
  if (it->second.entry.level >= minLevel) {
    it->second.entry.level = minLevel - 1;
  }
}

void StrataManager::Reset() {
  entries_.clear();
  next_creation_order_ = 0;
}

FrameLevel StrataManager::CountVisibleInStrata(RenderStrata strata) const {
  FrameLevel count = 0;
  for (const auto& [_, stored] : entries_) {
    if (stored.entry.strata == strata && stored.entry.isShown) {
      ++count;
    }
  }
  return count;
}

void StrataManager::CompactVisibleLevels(RenderStrata strata) {
  std::vector<StoredEntry*> visible_entries;
  visible_entries.reserve(entries_.size());
  for (auto& [_, stored] : entries_) {
    if (stored.entry.strata == strata && stored.entry.isShown) {
      visible_entries.push_back(&stored);
    }
  }

  std::sort(visible_entries.begin(), visible_entries.end(),
            [](const StoredEntry* lhs, const StoredEntry* rhs) {
              if (lhs->entry.level != rhs->entry.level) {
                return lhs->entry.level < rhs->entry.level;
              }
              return lhs->creation_order < rhs->creation_order;
            });

  FrameLevel compacted_level = 0;
  for (StoredEntry* stored : visible_entries) {
    stored->entry.level = compacted_level++;
  }
}

}

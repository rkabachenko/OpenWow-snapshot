#include "openwow/game/activities/dance/application/known_dance_catalog.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace openwow::game {
namespace {

[[nodiscard]] char FoldAscii(const char value) {
  return static_cast<char>(
      std::tolower(static_cast<unsigned char>(value)));
}

[[nodiscard]] std::uint32_t DefaultNameHash(const std::string_view name) {
  std::uint32_t hash = 2166136261u;
  for (const char value : name) {
    hash ^= static_cast<std::uint8_t>(FoldAscii(value));
    hash *= 16777619u;
  }
  return hash;
}

[[nodiscard]] bool DefaultNameEqual(const std::string_view left,
                                    const std::string_view right) {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin(),
                    [](const char lhs, const char rhs) {
                      return FoldAscii(lhs) == FoldAscii(rhs);
                    });
}

}

KnownDanceCatalog::KnownDanceCatalog()
    : name_hasher_(DefaultNameHash), name_equal_(DefaultNameEqual) {}

void KnownDanceCatalog::SetNameHasher(NameHasher hasher) {
  name_hasher_ = hasher ? std::move(hasher) : NameHasher(DefaultNameHash);
}

void KnownDanceCatalog::SetNameEqual(NameEqual equal) {
  name_equal_ = equal ? std::move(equal) : NameEqual(DefaultNameEqual);
}

KnownDanceCatalog::Iterator KnownDanceCatalog::FindByNameMutable(
    const std::string_view name) {
  const auto bucket = entries_by_name_hash_.find(HashName(name));
  if (bucket == entries_by_name_hash_.end()) {
    return entries_.end();
  }
  for (auto entry = bucket->second.rbegin();
       entry != bucket->second.rend(); ++entry) {
    if (NamesEqual((*entry)->name, name)) {
      return *entry;
    }
  }
  return entries_.end();
}

void KnownDanceCatalog::Index(const Iterator entry) {
  entries_by_name_hash_[HashName(entry->name)].push_back(entry);
  entries_by_id_[entry->dance_id].push_back(entry);
}

KnownDanceCatalog::Iterator KnownDanceCatalog::Store(
    KnownDanceEntry entry) {
  entries_.push_back(std::move(entry));
  const auto stored = std::prev(entries_.end());
  Index(stored);
  return stored;
}

void KnownDanceCatalog::Unindex(const Iterator entry) {
  const auto erase_entry = [entry](auto& buckets, const auto key) {
    auto bucket = buckets.find(key);
    if (bucket == buckets.end()) {
      return;
    }
    auto& entries = bucket->second;
    entries.erase(std::remove(entries.begin(), entries.end(), entry),
                  entries.end());
    if (entries.empty()) {
      buckets.erase(bucket);
    }
  };
  erase_entry(entries_by_name_hash_, HashName(entry->name));
  erase_entry(entries_by_id_, entry->dance_id);
}

void KnownDanceCatalog::Add(std::string name, const DanceId dance_id,
                            const DanceSequenceId sequence_id) {
  const auto existing = FindByNameMutable(name);
  if (existing == entries_.end()) {
    Store({std::move(name), dance_id, sequence_id});
    return;
  }

  if (existing->dance_id != dance_id) {
    auto id_bucket = entries_by_id_.find(existing->dance_id);
    if (id_bucket != entries_by_id_.end()) {
      auto& entries = id_bucket->second;
      entries.erase(std::remove(entries.begin(), entries.end(), existing),
                    entries.end());
      if (entries.empty()) {
        entries_by_id_.erase(id_bucket);
      }
    }
    existing->dance_id = dance_id;
    entries_by_id_[dance_id].push_back(existing);
  }
  existing->sequence_id = sequence_id;
}

void KnownDanceCatalog::Remove(const DanceId dance_id) {
  const auto bucket = entries_by_id_.find(dance_id);
  if (bucket == entries_by_id_.end()) {
    return;
  }
  const auto entries = bucket->second;
  for (const auto entry : entries) {
    Unindex(entry);
    entries_.erase(entry);
  }
}

void KnownDanceCatalog::Update(std::string name, const DanceId dance_id,
                               const DanceSequenceId sequence_id) {
  Remove(dance_id);
  Store({std::move(name), dance_id, sequence_id});
}

const KnownDanceEntry* KnownDanceCatalog::FindByName(
    const std::string_view name) const {
  const auto bucket = entries_by_name_hash_.find(HashName(name));
  if (bucket == entries_by_name_hash_.end()) {
    return nullptr;
  }
  for (auto entry = bucket->second.rbegin();
       entry != bucket->second.rend(); ++entry) {
    if (NamesEqual((*entry)->name, name)) {
      return &(**entry);
    }
  }
  return nullptr;
}

const KnownDanceEntry* KnownDanceCatalog::FindById(
    const DanceId dance_id) const {
  const auto bucket = entries_by_id_.find(dance_id);
  if (bucket == entries_by_id_.end() || bucket->second.empty()) {
    return nullptr;
  }
  return &(*bucket->second.back());
}

void KnownDanceCatalog::Clear() {
  entries_by_name_hash_.clear();
  entries_by_id_.clear();
  entries_.clear();
}

std::uint32_t KnownDanceCatalog::HashName(
    const std::string_view name) const {
  return name_hasher_(name);
}

bool KnownDanceCatalog::NamesEqual(const std::string_view left,
                                   const std::string_view right) const {
  return name_equal_(left, right);
}

}

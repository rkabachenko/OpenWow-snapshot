
#include "openwow/game/declined_words.h"

#include "openwow/core/storm_string.h"
#include "openwow/data/formats/dbc/dbc_entries_extended.h"
#include "openwow/data/formats/dbc/dbc_loader.h"

namespace openwow::game {
namespace {

constexpr std::size_t kStormNameCompareLimit = 0x7FFFFFFFu;
constexpr std::uint32_t kMaxRetailDeclinedCaseIndex = 5;

}

DeclinedWords& DeclinedWords::Get() {
  static DeclinedWords instance;
  return instance;
}

void DeclinedWords::Initialize() {
  std::lock_guard lock(mutex_);
  manual_entries_.clear();
  dbc_entries_.clear();
  dbc_populated_ = false;
}

void DeclinedWords::Clear() {
  std::lock_guard lock(mutex_);
  manual_entries_.clear();
  dbc_entries_.clear();
  dbc_populated_ = true;
}

void DeclinedWords::BindDbcLoader(const openwow::data::dbc::DbcLoader* dbc_loader) {
  std::lock_guard lock(mutex_);
  dbc_loader_ = dbc_loader;
  dbc_entries_.clear();
  dbc_populated_ = (dbc_loader_ == nullptr);
}

void DeclinedWords::SetDeclinedName(const std::string& nominative,
                                    const DeclinedName& declined) {
  std::lock_guard lock(mutex_);
  SetOrReplaceEntry(manual_entries_, nominative, declined);
}

const DeclinedName* DeclinedWords::GetDeclinedName(
    const std::string& nominative) const {
  std::lock_guard lock(mutex_);
  if (!dbc_populated_) {
    const_cast<DeclinedWords*>(this)->PopulateFromDbcLocked();
  }

  if (const DeclinedName* manual = FindEntry(manual_entries_, nominative); manual != nullptr) {
    return manual;
  }

  return FindEntry(dbc_entries_, nominative);
}

std::size_t DeclinedWords::GetCount() const {
  std::lock_guard lock(mutex_);
  return manual_entries_.size() + dbc_entries_.size();
}

const DeclinedName* DeclinedWords::FindEntry(const EntryMap& entries,
                                             const std::string& nominative) {
  const auto hash = HashCI(nominative);
  const auto [begin, end] = entries.equal_range(hash);
  for (auto it = begin; it != end; ++it) {
    if (NamesMatch(it->second.nominative, nominative)) {
      return &it->second.declined;
    }
  }

  return nullptr;
}

void DeclinedWords::SetOrReplaceEntry(EntryMap& entries,
                                      const std::string& nominative,
                                      const DeclinedName& declined) {
  const auto hash = HashCI(nominative);
  const auto [begin, end] = entries.equal_range(hash);
  for (auto it = begin; it != end; ++it) {
    if (NamesMatch(it->second.nominative, nominative)) {
      it->second.declined = declined;
      return;
    }
  }

  entries.emplace(hash, StoredDeclinedName{nominative, declined});
}

void DeclinedWords::PopulateFromDbcLocked() {
  dbc_entries_.clear();
  dbc_populated_ = true;
  if (dbc_loader_ == nullptr) {
    return;
  }

  std::unordered_map<std::uint32_t, DeclinedName> cases_by_word_id;
  for (const auto& entry : dbc_loader_->declined_word_cases().entries()) {
    if (entry.case_index == 0 || entry.case_index > kMaxRetailDeclinedCaseIndex) {
      continue;
    }

    DeclinedName& declined = cases_by_word_id[entry.declined_word_id];
    const auto slot = static_cast<std::size_t>(entry.case_index - 1);
    if (declined.forms[slot].empty()) {
      declined.forms[slot] = std::string(entry.declined_word);
    }
  }

  for (const auto& word : dbc_loader_->declined_word().entries()) {
    const auto case_it = cases_by_word_id.find(word.id);
    if (case_it == cases_by_word_id.end()) {
      continue;
    }

    SetOrReplaceEntry(dbc_entries_, std::string(word.word), case_it->second);
  }
}

bool DeclinedWords::NamesMatch(const std::string& lhs, const std::string& rhs) {
  return openwow::core::SStrCmpI(lhs.c_str(), rhs.c_str(), kStormNameCompareLimit) == 0;
}

std::uint32_t DeclinedWords::HashCI(const std::string& str) {
  return openwow::core::SStrHashCI(str.c_str());
}

}

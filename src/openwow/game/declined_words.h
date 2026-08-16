
#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace openwow::data::dbc {
class DbcLoader;
}

namespace openwow::game {

static constexpr std::size_t kMaxDeclinedForms = 5;

struct DeclinedName {

  std::array<std::string, kMaxDeclinedForms> forms;
};

class DeclinedWords {
 public:
  static DeclinedWords& Get();

  void Initialize();

  void Clear();

  void BindDbcLoader(const openwow::data::dbc::DbcLoader* dbc_loader);

  void SetDeclinedName(const std::string& nominative,
                       const DeclinedName& declined);

  [[nodiscard]] const DeclinedName* GetDeclinedName(
      const std::string& nominative) const;

  [[nodiscard]] std::size_t GetCount() const;

 private:
  struct StoredDeclinedName {
    std::string nominative;
    DeclinedName declined;
  };

  DeclinedWords() = default;

  using EntryMap = std::unordered_multimap<std::uint32_t, StoredDeclinedName>;

  static const DeclinedName* FindEntry(const EntryMap& entries,
                                       const std::string& nominative);
  static void SetOrReplaceEntry(EntryMap& entries,
                                const std::string& nominative,
                                const DeclinedName& declined);
  void PopulateFromDbcLocked();
  static bool NamesMatch(const std::string& lhs, const std::string& rhs);
  static std::uint32_t HashCI(const std::string& str);

  EntryMap manual_entries_;
  EntryMap dbc_entries_;
  const openwow::data::dbc::DbcLoader* dbc_loader_{nullptr};
  bool dbc_populated_{true};
  mutable std::mutex mutex_;
};

}

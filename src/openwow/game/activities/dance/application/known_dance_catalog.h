#pragma once

#include "openwow/game/activities/dance/model/known_dance_entry.h"

#include <functional>
#include <list>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openwow::game {

class KnownDanceCatalog final {
 public:
  using NameHasher = std::function<std::uint32_t(std::string_view)>;
  using NameEqual = std::function<bool(std::string_view, std::string_view)>;

  KnownDanceCatalog();

  void SetNameHasher(NameHasher hasher);
  void SetNameEqual(NameEqual equal);

  void Add(std::string name, DanceId dance_id,
           DanceSequenceId sequence_id);
  void Remove(DanceId dance_id);
  void Update(std::string name, DanceId dance_id,
              DanceSequenceId sequence_id);
  void Clear();

  [[nodiscard]] const KnownDanceEntry* FindByName(
      std::string_view name) const;
  [[nodiscard]] const KnownDanceEntry* FindById(DanceId dance_id) const;

 private:
  using Entries = std::list<KnownDanceEntry>;
  using Iterator = Entries::iterator;
  using Bucket = std::vector<Iterator>;
  using NameBuckets = std::unordered_map<std::uint32_t, Bucket>;
  using IdBuckets =
      std::unordered_map<DanceId, Bucket, DanceIdHash>;

  [[nodiscard]] Iterator FindByNameMutable(std::string_view name);
  Iterator Store(KnownDanceEntry entry);
  void Index(Iterator entry);
  void Unindex(Iterator entry);
  [[nodiscard]] std::uint32_t HashName(std::string_view name) const;
  [[nodiscard]] bool NamesEqual(std::string_view left,
                                std::string_view right) const;

  Entries entries_;
  IdBuckets entries_by_id_;
  NameBuckets entries_by_name_hash_;
  NameHasher name_hasher_;
  NameEqual name_equal_;
};

}

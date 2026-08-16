
#include "openwow/data/formats/dbc/transport_dbc_index.h"

#include <algorithm>
#include <iterator>

namespace openwow::data::dbc {

namespace {

template <typename Entry>
void CopyAndSortByTransportId(const DbcStore<Entry> &store,
                              std::vector<Entry> &destination) {
  destination.assign(store.entries().begin(), store.entries().end());

  std::stable_sort(destination.begin(), destination.end(),
                   [](const Entry &left, const Entry &right) {
                     return left.transport_id < right.transport_id;
                   });
}

template <typename Entry>
int FindFirstTransportIndex(const std::vector<Entry> &entries,
                            const std::uint32_t transport_id) {
  const auto first = std::lower_bound(
      entries.begin(), entries.end(), transport_id,
      [](const Entry &entry, const std::uint32_t id) {
        return entry.transport_id < id;
      });
  return first != entries.end() && first->transport_id == transport_id
             ? static_cast<int>(std::distance(entries.begin(), first))
             : -1;
}

template <typename Entry>
DbcEntryRange<Entry> FindTransportEntries(const std::vector<Entry> &entries,
                                          const std::uint32_t transport_id) {
  const auto first = FindFirstTransportIndex(entries, transport_id);
  if (first < 0) {
    return {};
  }

  const auto first_index = static_cast<std::size_t>(first);
  auto count = std::size_t{1u};
  while (first_index + count < entries.size() &&
         entries[first_index + count].transport_id == transport_id) {
    ++count;
  }
  return {&entries[first_index], count};
}

}

void TransportDbcIndex::Build(const DbcStore<TransportAnimationEntry> &animation_store,
                              const DbcStore<TransportRotationEntry> &rotation_store) {
  CopyAndSortByTransportId(animation_store, sorted_animations_);
  CopyAndSortByTransportId(rotation_store, sorted_rotations_);
}

int TransportDbcIndex::FindFirstAnimationIndex(std::uint32_t transport_id) const {
  return FindFirstTransportIndex(sorted_animations_, transport_id);
}

int TransportDbcIndex::FindFirstRotationIndex(std::uint32_t transport_id) const {
  return FindFirstTransportIndex(sorted_rotations_, transport_id);
}

DbcEntryRange<TransportAnimationEntry>
TransportDbcIndex::FindAnimationsByTransportId(std::uint32_t transport_id) const {
  return FindTransportEntries(sorted_animations_, transport_id);
}

DbcEntryRange<TransportRotationEntry>
TransportDbcIndex::FindRotationsByTransportId(std::uint32_t transport_id) const {
  return FindTransportEntries(sorted_rotations_, transport_id);
}

}

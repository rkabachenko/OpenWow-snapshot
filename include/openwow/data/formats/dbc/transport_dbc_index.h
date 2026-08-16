
#pragma once

#include "openwow/data/formats/dbc/dbc_entries_world.h"
#include "openwow/data/formats/dbc/dbc_store.h"

#include <cstdint>
#include <vector>

namespace openwow::data::dbc {

template <typename T> struct DbcEntryRange {
  const T *data = nullptr;
  std::size_t count = 0;

  [[nodiscard]] bool empty() const { return count == 0; }
  [[nodiscard]] const T *begin() const { return data; }
  [[nodiscard]] const T *end() const { return data + count; }
  [[nodiscard]] std::size_t size() const { return count; }
  [[nodiscard]] const T &operator[](std::size_t i) const { return data[i]; }
};

class TransportDbcIndex {
public:
  TransportDbcIndex() = default;

  void Build(const DbcStore<TransportAnimationEntry> &animation_store,
             const DbcStore<TransportRotationEntry> &rotation_store);

  [[nodiscard]] DbcEntryRange<TransportAnimationEntry>
  FindAnimationsByTransportId(std::uint32_t transport_id) const;

  [[nodiscard]] DbcEntryRange<TransportRotationEntry>
  FindRotationsByTransportId(std::uint32_t transport_id) const;

  [[nodiscard]] int FindFirstRotationIndex(std::uint32_t transport_id) const;

  [[nodiscard]] int FindFirstAnimationIndex(std::uint32_t transport_id) const;

  [[nodiscard]] std::size_t animation_count() const {
    return sorted_animations_.size();
  }
  [[nodiscard]] std::size_t rotation_count() const {
    return sorted_rotations_.size();
  }

  [[nodiscard]] bool empty() const {
    return sorted_animations_.empty() && sorted_rotations_.empty();
  }

private:
  std::vector<TransportAnimationEntry> sorted_animations_;
  std::vector<TransportRotationEntry> sorted_rotations_;
};

}

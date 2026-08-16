
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::ui::widgets {

struct EditHistory {
  struct Entry {
    std::string text;
    uint32_t cursorPos{0};

    [[nodiscard]] bool IsEmpty() const noexcept { return text.empty(); }
  };

  void Resize(uint32_t newSize);

  void Clear();

  void SetCapacity(uint32_t newCap);

  [[nodiscard]] uint32_t Count() const noexcept { return m_count; }
  [[nodiscard]] uint32_t Capacity() const noexcept { return m_capacity; }
  [[nodiscard]] bool Empty() const noexcept { return m_capacity == 0; }

  [[nodiscard]] const Entry& At(uint32_t idx) const { return m_entries[idx]; }
  [[nodiscard]] Entry& At(uint32_t idx) { return m_entries[idx]; }

  [[nodiscard]] const Entry* Data() const noexcept { return m_entries.data(); }
  [[nodiscard]] Entry* Data() noexcept { return m_entries.data(); }

  uint32_t m_count{0};
  uint32_t m_capacity{0};
  std::vector<Entry> m_entries;
};

}


#include "openwow/ui/widgets/edit_history.h"

#include <algorithm>
#include <utility>

namespace openwow::ui::widgets {

void EditHistory::Resize(uint32_t newSize) {

  m_entries.resize(newSize);

  m_count = newSize;
}

void EditHistory::Clear() {
  m_entries.clear();
}

void EditHistory::SetCapacity(uint32_t newCap) {
  if (newCap == m_capacity)
    return;

  if (newCap > 0) {
    Resize(newCap);

    m_capacity = newCap;
  } else {
    Clear();
    m_count = 0;
    m_capacity = 0;
  }
}

}

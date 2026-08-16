
#include "openwow/data/db_client_object.h"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace openwow::data {

DBClientColorTable::DBClientColorTable() { Reset(); }

void DBClientColorTable::Reset() {

  for (auto& c : colors_) {
    c.r = 0.0f;
    c.g = 0.0f;
    c.b = 0.0f;
  }

  std::memset(state_a_, 0, sizeof(state_a_));
  state_b_ = 0;
  std::memset(state_c_, 0, sizeof(state_c_));
}

const ColorRGB& DBClientColorTable::GetColor(uint32_t index) const {
  assert(index < kColorEntryCount);
  return colors_[index];
}

void DBClientColorTable::SetColor(uint32_t index, const ColorRGB& color) {
  assert(index < kColorEntryCount);
  colors_[index] = color;
}

uint32_t DBClientColorTable::GetStateField(uint32_t index) const {
  if (index < 4) return state_a_[index];
  if (index == 4) return state_b_;
  if (index < 10) return state_c_[index - 5];
  return 0;
}

void DBClientColorTable::SetStateField(uint32_t index, uint32_t value) {
  if (index < 4) { state_a_[index] = value; return; }
  if (index == 4) { state_b_ = value; return; }
  if (index < 10) { state_c_[index - 5] = value; }
}

}

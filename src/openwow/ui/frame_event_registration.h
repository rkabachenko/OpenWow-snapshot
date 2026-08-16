#pragma once

#include <algorithm>
#include <vector>

namespace openwow::ui::frame_event_registration {

template <typename T>
bool Contains(const std::vector<T>& values, const T& value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

template <typename T>
bool AddUnique(std::vector<T>& values, const T& value) {
  if (Contains(values, value)) {
    return false;
  }
  values.push_back(value);
  return true;
}

template <typename T>
bool Remove(std::vector<T>& values, const T& value) {
  const auto it = std::remove(values.begin(), values.end(), value);
  if (it == values.end()) {
    return false;
  }
  values.erase(it, values.end());
  return true;
}

}

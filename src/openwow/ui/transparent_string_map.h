#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace openwow::ui {

struct TransparentStringHash {
  using is_transparent = void;

  std::size_t operator()(const std::string_view value) const noexcept {
    return std::hash<std::string_view>{}(value);
  }
};

struct TransparentStringEqual {
  using is_transparent = void;

  bool operator()(const std::string_view left,
                  const std::string_view right) const noexcept {
    return left == right;
  }
};

template <typename Value>
using TransparentStringMap =
    std::unordered_map<std::string, Value, TransparentStringHash,
                       TransparentStringEqual>;

}

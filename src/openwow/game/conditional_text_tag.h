
#pragma once

#include <cstddef>
#include <string_view>

namespace openwow::game {

struct ConditionalTextTagContext {
  int selector = 0;
  int class_selector = 0;
  int race_selector = 0;
};

struct ConditionalTextTagSelection {
  std::string_view text;
  std::size_t consumed = 0;
};

[[nodiscard]] bool TrySelectConditionalTextTag(
    std::string_view input,
    const ConditionalTextTagContext &context,
    ConditionalTextTagSelection *selection);

}

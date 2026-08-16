
#pragma once

#include <cstdint>

namespace openwow::ui::widgets {

enum class ContentNodeType : std::uint32_t {
  kText = 0,
  kTextWithHyperlinks = 1,
  kImage = 2,
};

struct ContentNode {

  std::uintptr_t prev_link{0};

  std::uintptr_t next_node{0};

  ContentNodeType type{ContentNodeType::kText};

  void* child_object{nullptr};
};

static_assert(sizeof(ContentNode) == (sizeof(void*) == 4 ? 16 : sizeof(ContentNode)),
              "ContentNode must be 16 bytes to match the binary allocation");

}

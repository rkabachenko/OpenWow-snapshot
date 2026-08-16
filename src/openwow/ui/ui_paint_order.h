#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <queue>
#include <span>
#include <string_view>
#include <vector>

namespace openwow::ui {

[[nodiscard]] constexpr int UiDrawLayerOrder(
    const std::string_view layer) noexcept {
  if (layer == "BACKGROUND") return 0;
  if (layer == "BORDER") return 1;
  if (layer == "ARTWORK") return 2;
  if (layer == "OVERLAY") return 3;
  if (layer == "HIGHLIGHT") return 4;
  return 2;
}

inline constexpr std::size_t kNoUiPaintParent =
    std::numeric_limits<std::size_t>::max();

struct UiPaintOrderNode {
  std::size_t parent{kNoUiPaintParent};
  std::size_t insertion_order{0u};
  int strata_order{2};
  int frame_level{0};
  int draw_layer{2};
  int draw_sublevel{0};
  double effective_depth{0.0};
  bool region{false};
};

struct UiPaintRect {
  float x{0.0F};
  float y{0.0F};
  float width{0.0F};
  float height{0.0F};

  [[nodiscard]] constexpr bool HasArea() const noexcept {
    return width > 0.0F && height > 0.0F;
  }
};

struct UiScrollClipNode {
  UiPaintRect viewport;
  float horizontal_scroll_pixels{0.0F};
  float vertical_scroll_pixels{0.0F};
};

struct UiPaintPresentation {
  float offset_x{0.0F};
  float offset_y{0.0F};
  std::optional<UiPaintRect> clip;
  bool clipped_out{false};
};

[[nodiscard]] inline UiPaintPresentation BuildUiScrollPresentation(
    const std::span<const UiScrollClipNode> ancestors) noexcept {
  UiPaintPresentation result;
  for (const auto& ancestor : ancestors) {
    UiPaintRect viewport = ancestor.viewport;
    viewport.x += result.offset_x;
    viewport.y += result.offset_y;
    if (!viewport.HasArea()) {
      result.clipped_out = true;
      return result;
    }

    if (!result.clip.has_value()) {
      result.clip = viewport;
    } else {
      const float left = std::max(result.clip->x, viewport.x);
      const float top = std::max(result.clip->y, viewport.y);
      const float right = std::min(result.clip->x + result.clip->width,
                                   viewport.x + viewport.width);
      const float bottom = std::min(result.clip->y + result.clip->height,
                                    viewport.y + viewport.height);
      if (right <= left || bottom <= top) {
        result.clip.reset();
        result.clipped_out = true;
        return result;
      }
      result.clip = UiPaintRect{left, top, right - left, bottom - top};
    }
    result.offset_x -= ancestor.horizontal_scroll_pixels;
    result.offset_y -= ancestor.vertical_scroll_pixels;
  }
  return result;
}

[[nodiscard]] inline std::vector<std::size_t> BuildUiPaintOrder(
    const std::vector<UiPaintOrderNode>& nodes) {
  const auto frame_less = [&nodes](const std::size_t a,
                                   const std::size_t b) {
    const auto& lhs = nodes[a];
    const auto& rhs = nodes[b];
    if (lhs.strata_order != rhs.strata_order) {
      return lhs.strata_order < rhs.strata_order;
    }
    if (lhs.frame_level != rhs.frame_level) {
      return lhs.frame_level < rhs.frame_level;
    }
    if (lhs.effective_depth != rhs.effective_depth) {
      return lhs.effective_depth < rhs.effective_depth;
    }
    return lhs.insertion_order < rhs.insertion_order;
  };
  const auto region_less = [&nodes](const std::size_t a,
                                    const std::size_t b) {
    const auto& lhs = nodes[a];
    const auto& rhs = nodes[b];
    if (lhs.draw_layer != rhs.draw_layer) {
      return lhs.draw_layer < rhs.draw_layer;
    }
    if (lhs.draw_sublevel != rhs.draw_sublevel) {
      return lhs.draw_sublevel < rhs.draw_sublevel;
    }
    if (lhs.effective_depth != rhs.effective_depth) {
      return lhs.effective_depth < rhs.effective_depth;
    }
    return lhs.insertion_order < rhs.insertion_order;
  };

  std::vector<std::vector<std::size_t>> region_children(nodes.size());
  std::vector<std::size_t> render_units;
  render_units.reserve(nodes.size());
  for (std::size_t index = 0; index < nodes.size(); ++index) {
    const auto parent = nodes[index].parent;
    if (nodes[index].region && parent < nodes.size() && parent != index &&
        !nodes[parent].region) {
      region_children[parent].push_back(index);
    } else {
      render_units.push_back(index);
    }
  }
  for (auto& children : region_children) {
    std::stable_sort(children.begin(), children.end(), region_less);
  }

  const auto same_frame_bucket = [&nodes](const std::size_t a,
                                          const std::size_t b) {
    return nodes[a].strata_order == nodes[b].strata_order &&
           nodes[a].frame_level == nodes[b].frame_level &&
           nodes[a].effective_depth == nodes[b].effective_depth;
  };
  std::vector<std::vector<std::size_t>> equal_bucket_children(nodes.size());
  std::vector<std::size_t> indegree(nodes.size(), 0u);
  for (const std::size_t index : render_units) {
    const auto parent = nodes[index].parent;
    if (nodes[index].region || parent >= nodes.size() || parent == index ||
        nodes[parent].region || !same_frame_bucket(parent, index)) {
      continue;
    }
    equal_bucket_children[parent].push_back(index);
    ++indegree[index];
  }

  const auto ready_after = [&frame_less](const std::size_t a,
                                         const std::size_t b) {
    return frame_less(b, a);
  };
  std::priority_queue<std::size_t, std::vector<std::size_t>,
                      decltype(ready_after)>
      ready(ready_after);
  for (const std::size_t index : render_units) {
    if (indegree[index] == 0u) {
      ready.push(index);
    }
  }

  std::vector<std::size_t> ordered;
  ordered.reserve(nodes.size());
  std::vector<std::uint8_t> emitted(nodes.size(), 0u);
  const auto emit_unit = [&](const std::size_t index) {
    if (index >= nodes.size() || emitted[index] != 0u) {
      return;
    }
    emitted[index] = 1u;
    ordered.push_back(index);
    for (const std::size_t child : region_children[index]) {
      if (emitted[child] == 0u) {
        emitted[child] = 1u;
        ordered.push_back(child);
      }
    }
  };

  while (!ready.empty()) {
    const std::size_t index = ready.top();
    ready.pop();
    emit_unit(index);
    for (const std::size_t child : equal_bucket_children[index]) {
      if (--indegree[child] == 0u) {
        ready.push(child);
      }
    }
  }

  std::stable_sort(render_units.begin(), render_units.end(), frame_less);
  for (const std::size_t index : render_units) {
    emit_unit(index);
  }
  return ordered;
}

}

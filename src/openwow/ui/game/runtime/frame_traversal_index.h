#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::ui::framexml {
struct FrameRect;
struct UiFrame;
}

namespace openwow::ui::game::runtime {

class FrameStore;
class RetainedLayout;

class FrameTraversalIndex final {
 public:
  enum SemanticOwnerMask : std::uint8_t {
    kWorldMapOwner = 1u << 0u,
    kCharacterPanelOwner = 1u << 1u,
    kPlayerFrameOwner = 1u << 2u,
  };

  struct ScrollClipAncestor {
    std::string key;

    std::uint64_t handle{~0ULL};
    float effective_scale{1.0F};
  };

  struct TraversalEntry {
    std::string key;

    std::uint64_t handle{~0ULL};
    std::string mouse_target_key;

    std::uint64_t mouse_target_handle{~0ULL};
    int lua_ref{-2};
    int strata_rank{3};
    std::array<ScrollClipAncestor, 8> scroll_clip_ancestors{};
    std::uint8_t scroll_clip_ancestor_count{0u};
    std::uint8_t semantic_owner_mask{0u};
    float effective_scale{1.0F};
    bool effective_visible{false};
    bool uses_mouse{false};
    bool uses_mouse_wheel{false};
    bool uses_keyboard{false};

    const openwow::ui::framexml::UiFrame* frame{nullptr};
    const openwow::ui::framexml::FrameRect* rect{nullptr};
  };

  struct FrameStackEntry {
    std::string key;
    int lua_ref{-2};
    int strata{3};
    int level{0};
    bool mouse_enabled{false};
    bool visible{false};
  };

  struct Metrics {
    std::uint64_t snapshot_rebuilds{0};

    std::uint64_t incremental_patches{0};
    std::uint64_t full_rebuilds{0};
    std::uint64_t hit_test_rebuilds{0};
    std::size_t traversal_entries{0};
    std::size_t hit_test_entries{0};
    std::size_t last_hit_test_candidates{0};
    std::uint64_t generation{0};
  };

  FrameTraversalIndex(FrameStore& frames, RetainedLayout& layout);
  ~FrameTraversalIndex();
  FrameTraversalIndex(const FrameTraversalIndex&) = delete;
  FrameTraversalIndex& operator=(const FrameTraversalIndex&) = delete;

  void Clear();
  void InvalidateHierarchy() noexcept;
  void InvalidatePaintOrder() noexcept;
  void InvalidateHitTest() noexcept;
  [[nodiscard]] bool order_dirty() const noexcept;

  enum class OrderInvalidationKind : std::uint8_t {

    kOrderKey,

    kInherited,
  };
  void InvalidateFrameOrder(std::string_view key,
                            OrderInvalidationKind kind) noexcept;

  void Rebuild(float root_scale, float viewport_height);

  void RefreshRectCache();
  [[nodiscard]] std::span<const TraversalEntry> render_snapshot() const noexcept;
  [[nodiscard]] std::span<const TraversalEntry> input_snapshot() const noexcept;
  [[nodiscard]] std::string HitTarget(float x, float y,
                                      float viewport_height);
  [[nodiscard]] bool Contains(std::string_view key, float x, float y,
                              float viewport_height,
                              bool apply_scroll_clip = false) const;
  [[nodiscard]] bool IsEffectivelyVisible(std::string_view key) const;

  [[nodiscard]] bool IsEffectivelyVisible(std::uint64_t handle) const;
  [[nodiscard]] std::vector<FrameStackEntry> FrameStackAt(
      float x, float y, bool show_hidden, float viewport_height) const;
  [[nodiscard]] const Metrics& metrics() const noexcept;
  void ResetMetrics() noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}

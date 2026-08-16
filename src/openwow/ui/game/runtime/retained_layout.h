#pragma once

#include "openwow/ui/transparent_string_map.h"

#include "openwow/ui/framexml/layout_resolver.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

struct lua_State;

namespace openwow::ui {
class ModelNaturalSizeSource;
class TextureNaturalSizeSource;
}

namespace openwow::ui::game::runtime {

class FrameStore;

class RetainedLayout final {
 public:
  struct ScrollFrameRanges {
    double horizontal{0.0};
    double vertical{0.0};
  };

  struct Ports {
    std::function<void()> order_invalidated;

    std::function<void(std::string_view)> order_key_invalidated;
    std::function<void(std::string_view)> inherited_order_invalidated;
    std::function<void()> on_update_order_invalidated;
    std::function<void()> hit_test_invalidated;
  };

  struct Metrics {
    std::uint64_t full_resolves{0};
    std::uint64_t incremental_resolves{0};
    std::uint64_t dependency_graph_rebuilds{0};
    std::uint64_t construction_nodes_published{0};
    std::size_t last_resolve_candidates{0};
    std::size_t last_lua_sync_candidates{0};
    std::size_t last_on_demand_frames{0};
  };

  struct MoveSizingSession {
    std::string frame_name;
    std::string relative_name;
    int mode{0};
    float cursor_x{0.0F};
    float cursor_y{0.0F};
    bool active{false};
  };

  explicit RetainedLayout(FrameStore& frames, Ports ports = {});
  ~RetainedLayout();
  RetainedLayout(const RetainedLayout&) = delete;
  RetainedLayout& operator=(const RetainedLayout&) = delete;

  void BindLuaState(lua_State* lua) noexcept;

  void BindTextureNaturalSizeSource(
      openwow::ui::TextureNaturalSizeSource* source) noexcept;
  [[nodiscard]] openwow::ui::TextureNaturalSizeSource*
  texture_natural_size_source() const noexcept;

  void BindModelNaturalSizeSource(
      openwow::ui::ModelNaturalSizeSource* source) noexcept;
  [[nodiscard]] openwow::ui::ModelNaturalSizeSource*
  model_natural_size_source() const noexcept;

  void RetryPendingNaturalSizes();
  void Reserve(std::size_t capacity);
  void Clear();

  [[nodiscard]] float viewport_width() const noexcept;
  [[nodiscard]] float viewport_height() const noexcept;
  [[nodiscard]] float root_scale() const noexcept;
  [[nodiscard]] std::int32_t mode() const noexcept;
  [[nodiscard]] bool dirty() const noexcept;
  [[nodiscard]] const Metrics& metrics() const noexcept;

  [[nodiscard]] std::uint64_t RectsGeneration() const noexcept;

  [[nodiscard]] std::uint64_t ScrollFrameRangesGeneration() const noexcept;
  [[nodiscard]] const openwow::ui::TransparentStringMap<openwow::ui::framexml::FrameRect>&
  rects() const noexcept;

  [[nodiscard]] bool SetViewport(float width, float height);
  void SetRootScale(float scale, bool force, bool defer_solve);
  void SetMode(std::int32_t mode) noexcept;
  void SetBootstrapActive(bool active) noexcept;
  void EnterFrameTreeInstantiation() noexcept;
  void LeaveFrameTreeInstantiation() noexcept;

  void Invalidate() noexcept;
  void InvalidateGraph() noexcept;
  void QueueLuaMutation(std::string_view frame_name);
  void PublishFrame(std::string_view frame_name);
  void RemoveFrame(std::string_view frame_name);
  void ReindexDependencies(std::string_view frame_name);

  void SyncTrackedFramesFromLua();
  void SolveIfDirty();
  void RefreshTrackedLayout();
  [[nodiscard]] const openwow::ui::framexml::FrameRect* FindRect(
      std::string_view frame_name);
  [[nodiscard]] ScrollFrameRanges ResolveScrollFrameRanges(
      std::string_view frame_name);

  [[nodiscard]] std::optional<openwow::ui::framexml::FrameRect>
  ResolveAnonymousRegionRect(lua_State* lua, int region_index,
                             std::string_view parent_name,
                             std::optional<openwow::ui::framexml::FrameRect>
                                 parent_rect = std::nullopt) const;

  [[nodiscard]] std::string BuildLayoutCache();
  void ApplyLayoutCache(std::string_view layout_text);

  [[nodiscard]] bool BeginMoveSizing(std::string_view frame_name, int mode,
                                     float cursor_x, float cursor_y,
                                     MoveSizingSession* session);
  [[nodiscard]] bool UpdateMoveSizing(MoveSizingSession* session,
                                      float cursor_x, float cursor_y);
  [[nodiscard]] bool CommitMoveSizing(MoveSizingSession* session);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}

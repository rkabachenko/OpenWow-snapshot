#pragma once

#include "openwow/ui/framexml/ui_frame.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct lua_State;

namespace openwow::ui::widgets {
class CScriptObject;
class CSimpleFrame;
class CSimpleMinimap;
class CSimpleWorldFrame;
class StatusBar;
}

namespace openwow::ui::game::runtime {

class FrameStore final {
 public:

  using FrameHandle = std::uint64_t;
  static constexpr FrameHandle kInvalidFrameHandle =
      std::numeric_limits<FrameHandle>::max();

  struct Ports {
    std::function<void(std::string_view, int)> before_binding_release;
    std::function<void(std::string_view)> after_identity_release;
    std::function<void()> hierarchy_invalidated;
    std::function<void()> layout_graph_invalidated;
    std::function<void()> paint_order_invalidated;

    std::function<void(std::string_view)> order_key_invalidated;
    std::function<void(std::string_view)> inherited_order_invalidated;
    std::function<void()> hit_test_invalidated;
  };

  struct FrameView {
    std::string_view key;
    openwow::ui::framexml::UiFrame* frame;
    int lua_ref;
  };

  struct ConstFrameView {
    std::string_view key;
    const openwow::ui::framexml::UiFrame* frame;
    int lua_ref;
  };

  explicit FrameStore(Ports ports = {});
  ~FrameStore();

  FrameStore(const FrameStore&) = delete;
  FrameStore& operator=(const FrameStore&) = delete;

  void BindLuaState(lua_State* state) noexcept;
  void Reserve(std::size_t capacity);

  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] std::size_t binding_count() const noexcept;
  [[nodiscard]] std::size_t enumerable_frame_count() const noexcept;
  [[nodiscard]] bool contains(std::string_view key) const;
  [[nodiscard]] openwow::ui::framexml::UiFrame* FindFrame(
      std::string_view key);
  [[nodiscard]] const openwow::ui::framexml::UiFrame* FindFrame(
      std::string_view key) const;

  [[nodiscard]] FrameHandle HandleOf(std::string_view key) const;

  [[nodiscard]] FrameHandle ParentHandleOf(FrameHandle handle) const;
  [[nodiscard]] std::string_view KeyOf(FrameHandle handle) const;
  [[nodiscard]] openwow::ui::framexml::UiFrame* FindFrame(FrameHandle handle);
  [[nodiscard]] const openwow::ui::framexml::UiFrame* FindFrame(
      FrameHandle handle) const;

  [[nodiscard]] std::span<const FrameHandle> registration_handles()
      const noexcept;
  [[nodiscard]] std::optional<int> FindLuaRef(std::string_view key) const;
  [[nodiscard]] std::optional<int> FindLuaRef(FrameHandle handle) const;
  [[nodiscard]] std::optional<std::string> FindKeyForLuaTable(
      lua_State* state, int table_index) const;
  [[nodiscard]] openwow::ui::widgets::CSimpleFrame* FindSimpleFrame(
      std::string_view key);
  [[nodiscard]] openwow::ui::widgets::StatusBar* FindStatusBar(
      std::string_view key);
  [[nodiscard]] const openwow::ui::widgets::StatusBar* FindStatusBar(
      std::string_view key) const;
  [[nodiscard]] openwow::ui::widgets::CSimpleMinimap* FindMinimap(
      std::string_view key);
  [[nodiscard]] openwow::ui::widgets::CSimpleWorldFrame* FindWorldFrame(
      std::string_view key);

  [[nodiscard]] std::vector<FrameView> CollectStableFrames();
  [[nodiscard]] std::vector<ConstFrameView> CollectStableFrames() const;

  void ForEachStableFrame(
      const std::function<void(const ConstFrameView&)>& visitor) const;
  [[nodiscard]] std::vector<openwow::ui::framexml::UiFrame>
  CollectFramesInRegistrationOrder() const;
  [[nodiscard]] std::vector<const openwow::ui::framexml::UiFrame*>
  CollectFramePointersInRegistrationOrder() const;
  [[nodiscard]] std::vector<std::string> CollectBindingKeys() const;
  [[nodiscard]] std::span<const std::string> registration_order() const noexcept;

  [[nodiscard]] std::span<const std::string> FramesOfKind(
      std::string_view kind) const;
  [[nodiscard]] std::span<const std::string> ScrollChildContentFrames() const;

  void InvalidateClassificationIndex() noexcept;
  [[nodiscard]] std::span<const int> tracked_lua_refs() const noexcept;
  [[nodiscard]] std::size_t registration_checkpoint() const noexcept;

  [[nodiscard]] std::string AllocateUniqueKey(std::string_view preferred_key);
  openwow::ui::framexml::UiFrame& InsertFrame(
      std::string key, openwow::ui::framexml::UiFrame frame);
  void ReplaceFrame(std::string_view key,
                    openwow::ui::framexml::UiFrame frame);
  void AttachLuaBinding(std::string_view key, int lua_ref);
  void AttachNativeObject(
      std::string_view key,
      std::unique_ptr<openwow::ui::widgets::CScriptObject> object);
  void NotifyHierarchyMutation(std::string_view key,
                               std::string_view previous_parent);
  void InvalidatePaintOrder();
  void InvalidateHitTest();

  [[nodiscard]] bool PushNewestEnumerableFrame(lua_State* state) const;
  [[nodiscard]] bool ContainsEnumerableFrameNativeIdentity(
      const void* native_identity) const noexcept;
  [[nodiscard]] bool PushNextEnumerableFrame(lua_State* state,
                                              const void* native_identity) const;

  void SetFrameUserPlaced(std::string_view key, bool user_placed);
  void SetFrameDontSavePosition(std::string_view key,
                                bool dont_save_position);
  void SetFrameAlphaByte(std::string_view key, std::uint8_t alpha_byte);
  void SetRegionDrawLayer(std::string_view key, std::string_view draw_layer);
  void SetTextureRole(
      std::string_view key, std::string_view owner,
      openwow::ui::framexml::UiFrame::TextureRole role,
      const void* texture_identity);
  void SetTextureColor(std::string_view key, float red, float green,
                       float blue, float alpha);
  [[nodiscard]] std::optional<std::array<float, 4>> FindStatusBarColor(
      std::string_view owner) const;
  void SetScrollOffset(std::string_view key, bool horizontal, float offset);
  void SetScrollChildSubtree(std::string_view root_key,
                             bool scroll_child_content);

  [[nodiscard]] std::vector<std::string> CollectSubtreePostorder(
      std::string_view root_key) const;
  void DestroySubtree(std::string_view root_key);
  void RollbackRegistrationsAfter(std::size_t checkpoint);
  void ReleaseAllLuaBindingsWhileValid();
  void DestroyNativeObjectsWhileLuaValid();
  void ClearAfterLuaDestroyed();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}


#pragma once

#include "openwow/ui/animation/animation_types.h"
#include "openwow/ui/widgets/script_object.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace openwow::ui::anim {
class AnimationGroup;
}

namespace openwow::ui::widgets {

class CSimpleRender;
class CSimpleTexture;
class CSimpleLine;

struct SimpleRenderBatchClipRect {
  float left{0.0f};
  float top{0.0f};
  float right{0.0f};
  float bottom{0.0f};

  [[nodiscard]] bool IntersectsNormalizedUnitSquare() const noexcept {
    return right >= 0.0f && left <= 1.0f && bottom >= 0.0f && top <= 1.0f;
  }

  [[nodiscard]] bool IsFullyOffScreen() const noexcept {
    return right < 0.0f || left > 1.0f || bottom < 0.0f || top > 1.0f;
  }
};

class DeferredRenderCallbackList {
public:
  using Callback = std::function<void()>;

  void Add(Callback callback) {
    if (!callback) {
      return;
    }

    callbacks_.push_back(std::move(callback));
  }

  void Clear() noexcept {
    callbacks_.clear();
  }

  [[nodiscard]] bool Empty() const noexcept {
    return callbacks_.empty();
  }

  [[nodiscard]] std::size_t Size() const noexcept {
    return callbacks_.size();
  }

  void ExecuteAll() const {
    for (auto it = callbacks_.rbegin(); it != callbacks_.rend(); ++it) {
      (*it)();
    }
  }

private:
  std::vector<Callback> callbacks_;
};

class SimpleRenderBatchSink {
public:
  using DeferredRenderCallback = std::function<void()>;

  virtual ~SimpleRenderBatchSink() = default;

  [[nodiscard]] virtual const SimpleRenderBatchClipRect &GetClipRect() const noexcept = 0;
  virtual void AddText(const CSimpleRender &render, std::string_view text) = 0;
  virtual void AddEmbeddedTexture(const CSimpleTexture &texture) = 0;
  virtual void AddLine(const CSimpleLine &line) {
    (void)line;
  }
  virtual void AddDeferredRenderCallback(DeferredRenderCallback callback) {
    (void)callback;
  }
};

enum class DrawLayer : uint8_t {
  Background = 0,
  Border = 1,
  Artwork = 2,
  Overlay = 3,
  Highlight = 4,
  COUNT_
};

[[nodiscard]] const char *DrawLayerName(DrawLayer layer) noexcept;
[[nodiscard]] DrawLayer DrawLayerFromName(const std::string &name) noexcept;

enum class FramePoint : uint8_t {
  TopLeft = 0,
  Top,
  TopRight,
  Left,
  Center,
  Right,
  BottomLeft,
  Bottom,
  BottomRight,
  COUNT_
};

[[nodiscard]] const char *FramePointName(FramePoint pt) noexcept;
[[nodiscard]] FramePoint FramePointFromName(const std::string &name) noexcept;

class CScriptRegion;

[[nodiscard]] std::string ExpandParentNameToken(const CScriptRegion &region,
                                                std::string_view name);

struct RegionAnchor {
  FramePoint point{FramePoint::TopLeft};
  CScriptRegion *relativeTo{nullptr};
  std::string relativeToName;
  FramePoint relativePoint{FramePoint::TopLeft};
  float offsetX{0.0f};
  float offsetY{0.0f};
};

struct ScreenRect {
  float left{0.0f};
  float top{0.0f};
  float right{0.0f};
  float bottom{0.0f};

  [[nodiscard]] float Width() const noexcept {
    return right - left;
  }
  [[nodiscard]] float Height() const noexcept {
    return bottom - top;
  }

  [[nodiscard]] bool IsFullyOffScreen() const noexcept {
    return right < 0.0f || left > 1.0f || bottom < 0.0f || top > 1.0f;
  }

  [[nodiscard]] static ScreenRect Intersection(const ScreenRect& a,
                                               const ScreenRect& b) noexcept {
    return {
      std::max(a.left,   b.left),
      std::max(a.top,    b.top),
      std::min(a.right,  b.right),
      std::min(a.bottom, b.bottom),
    };
  }

  ScreenRect& IntersectWith(const ScreenRect& other) noexcept {
    *this = Intersection(*this, other);
    return *this;
  }

  [[nodiscard]] bool Intersects(const ScreenRect& other) const noexcept {
    return std::max(left, other.left)  < std::min(right,  other.right)
        && std::max(top,  other.top)   < std::min(bottom, other.bottom);
  }

  [[nodiscard]] bool ContainsPoint(float x, float y) const noexcept {
    return x >= left && x <= right && y >= top && y <= bottom;
  }
};

using ScriptCallback = std::function<void()>;

class CScriptRegion : public CScriptObject {
public:
  explicit CScriptRegion(ScriptObjectType type) noexcept : CScriptObject(type) {}

  ~CScriptRegion() override;

  [[nodiscard]] bool IsKindOf(ScriptObjectType t) const noexcept override {
    return t == ScriptObjectType::Region || CScriptObject::IsKindOf(t);
  }

  [[nodiscard]] bool IsTypeOf(const char *typeName) const noexcept override;

  void SetName(const std::string &name) override;

  virtual void Show();
  virtual void Hide();
  void SetShown(bool s) {
    if (s)
      Show();
    else
      Hide();
  }
  [[nodiscard]] bool IsShown() const noexcept {
    return shown_;
  }
  [[nodiscard]] virtual bool IsVisible() const noexcept {
    return effectiveVisible_;
  }
  virtual void RefreshVisibilityFromHierarchy();

  [[nodiscard]] virtual bool IsDragMovedInHierarchy() const noexcept;

  virtual void OnLayout() {}

  void MarkLayoutDirty() noexcept {
    InvalidateLayout(false);
  }
  void ClearLayoutDirty() noexcept {
    layoutDirty_ = false;
  }
  [[nodiscard]] bool HasPendingLayout() const noexcept {
    return layoutDirty_;
  }

  virtual void OnParentAlphaChanged(bool ) {}

  virtual void OnFrameScaleChanged(float , bool ) {
    MarkLayoutDirty();
  }

  virtual void OnParentFontHeightChanged(float , bool ) {
    MarkLayoutDirty();
  }

  virtual void OnFrameDepthChanged(float , bool ) {
    MarkLayoutDirty();
  }

  virtual void StopAnimations() {}

  virtual void RegisterRenderCallbacks(SimpleRenderBatchSink & ) const {}

  virtual void ApplyAnimRotation(FramePoint ,
                                 const float* ,
                                 float ) {}

  virtual void ApplyAnimScale(std::uint32_t ,
                              const float* ,
                              const float* ) {}

  virtual void PreRender(int , bool ,
                         bool ) {}
  virtual void PostRender(int , bool ) {}

  virtual void OnUpdateCascade(float ) {}
  virtual void OnEventCascade(int , int , int ) {}

  virtual void ProcessFrameUpdatePass() {}
  virtual void ProcessFramePostUpdatePass(float elapsed) {

    UpdateOwnedAnimationGroups(elapsed);
  }

  void SetAlpha(float alpha) noexcept {
    alpha_ = alpha;
  }
  [[nodiscard]] float GetAlpha() const noexcept {
    return alpha_;
  }

  [[nodiscard]] std::uint8_t GetEffectiveAlphaByte() const noexcept {
    auto self_byte = static_cast<std::uint8_t>(
        std::clamp(static_cast<int>(alpha_ * 255.0f), 0, 255));
    if (!parent_)
      return self_byte;
    return static_cast<std::uint8_t>(
        (static_cast<unsigned>(self_byte) *
         static_cast<unsigned>(parent_->GetEffectiveAlphaByte())) /
        255u);
  }

  [[nodiscard]] float GetEffectiveAlpha() const noexcept {
    return static_cast<float>(GetEffectiveAlphaByte()) / 255.0f;
  }

  virtual void SetWidth(float w) noexcept {
    width_ = w;
  }
  virtual void SetHeight(float h) noexcept {
    height_ = h;
  }
  virtual void SetSize(float w, float h) noexcept {
    width_ = w;
    height_ = h;
  }
  [[nodiscard]] float GetWidth() const noexcept {
    return width_;
  }
  [[nodiscard]] float GetHeight() const noexcept {
    return height_;
  }

  virtual void GetClampRectInsets(float& left, float& right,
                                  float& top, float& bottom) const noexcept {
    left = 0.0f;
    right = 0.0f;
    top = 0.0f;
    bottom = 0.0f;
  }

  void SetParent(CScriptRegion *parent);
  [[nodiscard]] CScriptRegion *GetParent() const noexcept {
    return parent_;
  }

  void SetPoint(const RegionAnchor &anchor);

  void ClearAllPoints() {
    UnregisterAllRelativeDependencies();
    anchors_.clear();
  }
  void ClearPoint(FramePoint pt) {
    UnregisterRelativeDependency(pt);
    anchors_.erase(pt);
    InvalidateLayout(false);
  }

  void SetAllPoints(CScriptRegion *relativeTo);

  [[nodiscard]] virtual bool
  CanAcceptProtectedDependency(const CScriptRegion *dependent) const noexcept;
  [[nodiscard]] size_t GetNumPoints() const noexcept {
    return anchors_.size();
  }
  [[nodiscard]] bool HasPoint(FramePoint pt) const {
    return anchors_.count(pt) > 0;
  }
  [[nodiscard]] const RegionAnchor *GetPoint(FramePoint pt) const {
    auto it = anchors_.find(pt);
    return it != anchors_.end() ? &it->second : nullptr;
  }

  void SetRect(const ScreenRect &r) noexcept {
    const ScreenRect oldRect = rect_;
    rect_ = r;
    cachedLayoutRect_ = r;
    cachedLayoutRectValid_ = true;
    liveRectInitialized_ = true;
    OnRectChanged(oldRect);
  }
  [[nodiscard]] const ScreenRect &GetRect() const noexcept {
    return rect_;
  }

  [[nodiscard]] bool TryGetCachedLayoutRect(ScreenRect *outRect) const noexcept;
  void ResolvePendingLayoutRect() noexcept;

  void SetScript(const std::string &handler, ScriptCallback cb) {
    scripts_[handler] = std::move(cb);
  }
  void RemoveScript(const std::string &handler) {
    scripts_.erase(handler);
  }
  [[nodiscard]] bool HasScript(const std::string &handler) const {
    return scripts_.count(handler) > 0;
  }
  bool RunScript(const std::string &handler) {
    auto it = scripts_.find(handler);
    if (it == scripts_.end() || !it->second)
      return false;
    it->second();
    return true;
  }
  [[nodiscard]] std::vector<std::string> GetScriptNames() const {
    std::vector<std::string> result;
    result.reserve(scripts_.size());
    for (const auto &[k, _] : scripts_)
      result.push_back(k);
    return result;
  }
  [[nodiscard]] size_t GetScriptCount() const noexcept {
    return scripts_.size();
  }

  using AnimationKindQuery = std::function<bool(openwow::ui::anim::AnimKind)>;
  using AnimationStopCallback = std::function<void()>;

  void AttachAnimationGroup(std::uintptr_t handle, AnimationKindQuery containsKind);
  void AttachAnimationGroup(std::uintptr_t handle, AnimationKindQuery containsKind,
                            AnimationStopCallback stopCallback);
  void AttachAnimationKind(std::uintptr_t handle, openwow::ui::anim::AnimKind kind);
  [[nodiscard]] bool DetachAnimationGroup(std::uintptr_t handle);
  [[nodiscard]] size_t GetAttachedAnimationGroupCount() const noexcept {
    return attachedAnimationGroups_.size();
  }

  void StopAllAttachedAnimationGroups();
  [[nodiscard]] bool HasAttachedAnimationGroupsInSelfOrAncestorHierarchy() const noexcept;
  [[nodiscard]] bool HasAttachedAnimationKind(openwow::ui::anim::AnimKind kind) const;
  [[nodiscard]] bool
  HasAttachedAnimationKindInSelfOrAncestorHierarchy(openwow::ui::anim::AnimKind kind) const;

  void RegisterOwnedAnimationGroup(openwow::ui::anim::AnimationGroup* group);
  void UnregisterOwnedAnimationGroup(openwow::ui::anim::AnimationGroup* group);
  [[nodiscard]] size_t GetOwnedAnimationGroupCount() const noexcept {
    return ownedAnimationGroups_.size();
  }

  void SetDrawLayer(DrawLayer layer) noexcept;
  [[nodiscard]] DrawLayer GetDrawLayer() const noexcept {
    return drawLayer_;
  }

protected:
  void InvalidateLayout(bool attemptResolve) noexcept;
  void InvalidateRelativeDependents() noexcept;

  virtual void OnRectChanged(const ScreenRect& ) {}

  virtual void HandleLayoutInvalidation(bool attemptResolve) noexcept;

  virtual void OnRelativeAnchorTargetDestroyed() noexcept {}
  void QueueOwningFrameDrawLayerStateUpdateIfVisible() noexcept;
  void QueueOwningFrameRenderRetryIfVisible() const noexcept;
  void NotifyOwningScrollFrameOfContentChangeIfVisible() noexcept;
public:
  void ShowVisible();
  void HideVisible();
protected:
  [[nodiscard]] virtual bool AllowsChildDrawLayer(DrawLayer ) const noexcept {
    return true;
  }
  virtual void OnEffectiveVisibilityChanged(bool ) {}

  bool shown_{true};
  bool effectiveVisible_{true};
  bool layoutDirty_{false};
  bool cachedLayoutRectValid_{false};
  bool liveRectInitialized_{false};
  float alpha_{1.0f};
  float width_{0.0f};
  float height_{0.0f};
  CScriptRegion *parent_{nullptr};
  std::unordered_map<FramePoint, RegionAnchor> anchors_;
  std::unordered_map<CScriptRegion *, std::uint16_t> relativeDependents_;
  ScreenRect rect_{};
  ScreenRect cachedLayoutRect_{};
  std::unordered_map<std::string, ScriptCallback> scripts_;
  struct AttachedAnimationGroup {
    std::uintptr_t handle{0};
    AnimationKindQuery containsKind{};
    AnimationStopCallback stopCallback{};
  };
  std::vector<AttachedAnimationGroup> attachedAnimationGroups_;
  std::vector<openwow::ui::anim::AnimationGroup*> ownedAnimationGroups_;
  DrawLayer drawLayer_{DrawLayer::Artwork};

  void UpdateOwnedAnimationGroups(float elapsed);

private:
  void ApplyParentLayerAndVisibility(CScriptRegion *parent, DrawLayer layer,
                                     bool shown) noexcept;
  void RegisterRelativeDependency(FramePoint point, CScriptRegion *relativeTo) noexcept;
  void UnregisterRelativeDependency(FramePoint point) noexcept;
  void UnregisterRelativeDependency(FramePoint point, CScriptRegion *relativeTo) noexcept;
  void UnregisterAllRelativeDependencies() noexcept;
  void DetachDependentsOnDestroy() noexcept;
  void RemoveDestroyedRelative(CScriptRegion *relativeTo) noexcept;
};

}

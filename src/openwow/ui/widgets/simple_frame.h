
#pragma once

#include "openwow/ui/rect_utils.h"
#include "openwow/ui/ui_coordinate_space.h"
#include "openwow/ui/widgets/script_region.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace openwow::ui::widgets {

class CSimpleFontString;
class CSimpleTexture;
class CSimpleLine;
class CSimpleFrame;

[[nodiscard]] std::unique_ptr<CSimpleFrame> CreateFrameWidget(
    std::string_view frame_type);

enum class FrameStrata : uint8_t {
  World = 0,
  Background = 1,
  Low = 2,
  Medium = 3,
  High = 4,
  Dialog = 5,
  Fullscreen = 6,
  FullscreenDialog = 7,
  Tooltip = 8,
  COUNT_
};

[[nodiscard]] const char* FrameStrataName(FrameStrata strata) noexcept;
[[nodiscard]] FrameStrata FrameStrataFromName(const std::string& name) noexcept;

[[nodiscard]] const char* MouseButtonName(uint32_t buttonFlag) noexcept;
[[nodiscard]] uint32_t MouseButtonFlag(const char* name) noexcept;
[[nodiscard]] uint32_t MouseButtonScriptOrdinalToFlag(int buttonOrdinal) noexcept;
[[nodiscard]] int MouseButtonFlagToScriptOrdinal(uint32_t buttonFlag) noexcept;

struct BackdropInfo {
  std::string bgFile;
  std::string edgeFile;
  std::string alphaMode;
  bool tile{false};
  float tileSize{0.0f};
  float edgeSize{0.0f};
  float insetLeft{0.0f}, insetRight{0.0f}, insetTop{0.0f}, insetBottom{0.0f};
};

inline float QuantizeBackdropColorComponent(float value) noexcept {
  if (std::isnan(value)) {
    value = 1.0f;
  } else if (value < 0.0f) {
    value = 0.0f;
  } else if (value >= 1.0f) {
    value = 1.0f;
  }

  const int quantized =
      std::clamp(static_cast<int>(value * 255.0f + 0.5f), 0, 255);
  return static_cast<float>(quantized) / 255.0f;
}

class CSimpleFrame : public CScriptRegion {
 public:
  explicit CSimpleFrame(ScriptObjectType type = ScriptObjectType::Frame)
      : CScriptRegion(type) {
    RegisterForStacking();
  }
  ~CSimpleFrame() override;

  [[nodiscard]] bool IsKindOf(ScriptObjectType t) const noexcept override {
    return t == ScriptObjectType::Frame || CScriptRegion::IsKindOf(t);
  }

  [[nodiscard]] bool IsTypeOf(const char* typeName) const noexcept override;

  void SetFrameStrata(FrameStrata strata) noexcept;
  [[nodiscard]] FrameStrata GetFrameStrata() const noexcept { return strata_; }

  void SetFrameLevel(int32_t level) noexcept;
  [[nodiscard]] int32_t GetFrameLevel() const noexcept { return level_; }

  bool Raise();

  void SetFrameName(const std::string& name);
  void SetFrameId(uint32_t id) noexcept { frameId_ = id; }
  [[nodiscard]] uint32_t GetFrameId() const noexcept { return frameId_; }
  [[nodiscard]] static CSimpleFrame* FindNamedFrame(const std::string& name);

  void EnableMouse(bool e) noexcept { mouseEnabled_ = e; }
  [[nodiscard]] bool IsMouseEnabled() const noexcept { return mouseEnabled_; }

  void EnableKeyboard(bool e) noexcept { keyboardEnabled_ = e; }
  [[nodiscard]] bool IsKeyboardEnabled() const noexcept {
    return keyboardEnabled_;
  }

  void EnableMouseWheel(bool e) noexcept { mouseWheelEnabled_ = e; }
  [[nodiscard]] bool IsMouseWheelEnabled() const noexcept {
    return mouseWheelEnabled_;
  }

  void SetIgnoreDepth(bool ignore) noexcept { ignoreDepth_ = ignore; }
  [[nodiscard]] bool IsIgnoringDepth() const noexcept { return ignoreDepth_; }

  void SetMovable(bool m) noexcept { movable_ = m; }
  [[nodiscard]] bool IsMovable() const noexcept { return movable_; }

  void SetResizable(bool r) noexcept { resizable_ = r; }
  [[nodiscard]] bool IsResizable() const noexcept { return resizable_; }

  void SetDontSavePosition(bool dont_save_position) noexcept {
    dontSavePosition_ = dont_save_position;
  }
  [[nodiscard]] bool DoesDontSavePosition() const noexcept {
    return dontSavePosition_;
  }

  void SetClampedToScreen(bool clamped) noexcept;
  [[nodiscard]] bool IsClampedToScreen() const noexcept;

  void SetClampRectInsets(float left, float bottom,
                          float right, float top) noexcept {
    clampInsetL_ = left;
    clampInsetB_ = bottom;
    clampInsetR_ = right;
    clampInsetT_ = top;
  }

  void GetClampRectInsets(float& left, float& right,
                          float& top, float& bottom) const noexcept override {
    left = clampInsetL_;
    right = clampInsetR_;
    top = clampInsetT_;
    bottom = clampInsetB_;
  }

  void SetToplevel(bool t) noexcept;
  [[nodiscard]] bool IsToplevel() const noexcept { return toplevel_; }

  void SetPropagateKeyboardInput(bool p) noexcept { propagateKeyboard_ = p; }
  [[nodiscard]] bool DoesPropagateKeyboardInput() const noexcept {
    return propagateKeyboard_;
  }

  void SetBackdrop(const BackdropInfo& bd) {
    backdrop_ = bd;
    hasBackdrop_ = true;
    SetBackdropColor(1.0f, 1.0f, 1.0f, 1.0f);
    SetBackdropBorderColor(1.0f, 1.0f, 1.0f, 1.0f);
  }
  void ClearBackdrop() { backdrop_ = {}; hasBackdrop_ = false; }
  [[nodiscard]] bool HasBackdrop() const noexcept { return hasBackdrop_; }
  [[nodiscard]] const BackdropInfo& GetBackdrop() const noexcept {
    return backdrop_;
  }

  void SetBackdropColor(float r, float g, float b, float a = 1.0f) noexcept {
    bdColorR_ = QuantizeBackdropColorComponent(r);
    bdColorG_ = QuantizeBackdropColorComponent(g);
    bdColorB_ = QuantizeBackdropColorComponent(b);
    bdColorA_ = QuantizeBackdropColorComponent(a);
  }
  void GetBackdropColor(float& r, float& g, float& b, float& a) const noexcept {
    r = bdColorR_;
    g = bdColorG_;
    b = bdColorB_;
    a = bdColorA_;
  }
  void SetBackdropBorderColor(float r, float g, float b,
                              float a = 1.0f) noexcept {
    bdBorderR_ = QuantizeBackdropColorComponent(r);
    bdBorderG_ = QuantizeBackdropColorComponent(g);
    bdBorderB_ = QuantizeBackdropColorComponent(b);
    bdBorderA_ = QuantizeBackdropColorComponent(a);
  }
  void GetBackdropBorderColor(float& r, float& g, float& b, float& a) const noexcept {
    r = bdBorderR_;
    g = bdBorderG_;
    b = bdBorderB_;
    a = bdBorderA_;
  }

  void AddChild(CSimpleFrame* child) {
    if (!child || child->GetParent() == this) {
      return;
    }
    child->SetParentFrame(this);
  }
  void RemoveChild(CSimpleFrame* child) {
    if (!child || child->GetParent() != this) {
      return;
    }
    child->SetParentFrame(nullptr);
  }
  [[nodiscard]] const std::vector<CSimpleFrame*>& GetChildren() const noexcept {
    return children_;
  }
  [[nodiscard]] size_t GetNumChildren() const noexcept {
    return children_.size();
  }
  void AdoptOwnedChild(std::unique_ptr<CSimpleFrame> child);

  void SetDrawLayerEnabled(DrawLayer layer, bool enabled) noexcept;
  [[nodiscard]] bool IsDrawLayerEnabled(DrawLayer layer) const noexcept;
  void QueueDrawLayerStateUpdate(DrawLayer layer) noexcept;
  void QueueRenderRetryStateUpdate() noexcept;

  void AddRegion(CScriptRegion* region);
  [[nodiscard]] const std::vector<CScriptRegion*>& GetRegions() const noexcept {
    return regions_;
  }
  [[nodiscard]] size_t GetNumRegions() const noexcept {
    return regions_.size();
  }
  [[nodiscard]] CScriptRegion* GetTitleRegion() const noexcept {
    return titleRegion_.get();
  }

  void RegisterEvent(const std::string& event) { events_.insert(event); }
  void UnregisterEvent(const std::string& event) { events_.erase(event); }
  void UnregisterAllEvents() { events_.clear(); }
  [[nodiscard]] bool IsEventRegistered(const std::string& event) const {
    return events_.count(event) > 0;
  }
  [[nodiscard]] const std::unordered_set<std::string>& GetRegisteredEvents()
      const noexcept {
    return events_;
  }
  [[nodiscard]] size_t GetNumEvents() const noexcept { return events_.size(); }

  void SetHitRectInsets(float l, float r, float t, float b) noexcept {
    hitInsetL_ = l; hitInsetR_ = r; hitInsetT_ = t; hitInsetB_ = b;
  }

  void GetHitRectInsetsOut(float* left, float* right,
                           float* top, float* bottom) const noexcept {
    if (left)   *left   = hitInsetL_;
    if (right)  *right  = hitInsetR_;
    if (top)    *top    = hitInsetT_;
    if (bottom) *bottom = hitInsetB_;
  }

  [[nodiscard]] bool HitTest(
      float x, float y,
      openwow::ui::DevicePixelsPerUiUnit scale) const noexcept {
    if (!mouseEnabled_ || !IsVisible()) return false;
    const float l =
        rect_.left + openwow::ui::UiUnitsToDevicePixels(hitInsetL_, scale);
    const float r =
        rect_.right - openwow::ui::UiUnitsToDevicePixels(hitInsetR_, scale);
    const float t =
        rect_.top + openwow::ui::UiUnitsToDevicePixels(hitInsetT_, scale);
    const float b =
        rect_.bottom - openwow::ui::UiUnitsToDevicePixels(hitInsetB_, scale);
    return openwow::ui::RectContainsPointInclusive(l, t, r, b, x, y);
  }

  [[nodiscard]] bool IsPointInFrame(float x, float y) const noexcept;

  bool SetScale(float s, bool force = false);
  [[nodiscard]] float GetScale() const noexcept { return scale_; }
  [[nodiscard]] float GetEffectiveScale() const noexcept { return layoutScale_; }

  bool SetDepth(float depth, bool force = false);
  [[nodiscard]] float GetDepth() const noexcept { return depth_; }
  [[nodiscard]] float GetEffectiveDepth() const noexcept {
    return inheritedDepth_ + depth_;
  }

  void SetMaxResize(float w, float h) noexcept {
    maxResizeW_ = w;
    maxResizeH_ = h;
  }
  void SetMinResize(float w, float h) noexcept {
    minResizeW_ = w;
    minResizeH_ = h;
  }

  void EnableProtection() noexcept {
    QueueLayoutInvalidation(true);
    protectionEnabled_ = true;
    protectionBits_ = static_cast<uint8_t>((protectionBits_ & 0xFCu) | 1u);
  }
  void DisableProtection() noexcept {
    protectionBits_ &= 0xFCu;
    protectionEnabled_ = false;
    QueueLayoutInvalidation(true);
  }
  [[nodiscard]] bool IsExplicitlyProtected() const noexcept {
    return protectionEnabled_;
  }

  [[nodiscard]] bool IsProtected() const noexcept;

  void SetAllowAttributeChanges(bool allow) noexcept {
    allowAttributeChanges_ = allow;
  }

  [[nodiscard]] bool HasAllowedAttributeChanges() const noexcept;

  void RegisterForClicksMask(uint32_t buttonMask) noexcept {
    registeredClicksMask_ = buttonMask;
  }
  void RegisterForAllClicksMask() noexcept { registeredClicksMask_ = 0x1F; }
  [[nodiscard]] uint32_t GetRegisteredClicksMask() const noexcept {
    return registeredClicksMask_;
  }

  struct DragState {
    bool dragging{false};
    bool dragMoved{false};
    uint32_t buttonFlags{0};
    float startX{0.0f};
    float startY{0.0f};
  };
  [[nodiscard]] const DragState& GetDragState() const noexcept {
    return dragState_;
  }
  [[nodiscard]] bool IsHighlightActive() const noexcept {
    return highlightActive_;
  }

  [[nodiscard]] bool IsDragMovedInHierarchy() const noexcept override;

  void Show() override;
  void Hide() override;
  [[nodiscard]] bool IsVisible() const noexcept override { return visible_; }
  void SetRect(const ScreenRect& rect) noexcept;

  bool SetLayoutScale(float scale, bool force = false);

  bool SetLayoutDepth(float depth, bool force = false);

  void SetLayoutWidth(float w);

  void SetLayoutHeight(float h);

  void SetLayoutWidthAndHeight(float w, float h);

  [[nodiscard]] float GetLayoutWidth() const noexcept { return layoutWidth_; }

  [[nodiscard]] float GetLayoutHeight() const noexcept { return layoutHeight_; }

  [[nodiscard]] bool TryGetCachedLayoutRect(ScreenRect* outRect) const noexcept;

  void GetDimensionsWithFallback(float* outWidth, float* outHeight,
                                  bool allowFallback);

  void InvalidateRelativeDependentLayouts();

  void LoadAnchorPointsFromXML(const void* xmlNode, void* errorHandler);

  void ProcessLayerElements(const void* xmlNode, void* errorHandler);

  void ProcessInheritsAndFrames(const void* xmlNode, void* errorHandler);

  void LoadScriptElements(const void* xmlNode, void* errorHandler);

  void LinkRegion(CScriptRegion* region);

  virtual void UnlinkRegion(CScriptRegion* region);

  void DetachDestroyedRegion(CScriptRegion* region) noexcept;

  virtual bool FireOnKeyDown(const std::string& key);
  virtual bool FireOnKeyUp(const std::string& key);
  virtual bool FireOnChar(const std::string& ch);
  virtual bool FireOnMouseDown(uint32_t buttonFlag, float x, float y,
                               const char* buttonName = nullptr);
  virtual bool FireOnMouseUp(uint32_t buttonFlag, float x, float y,
                             const char* buttonName = nullptr);
  virtual bool FireOnMouseWheel(int32_t delta);
  virtual void FireOnEnter(bool motion = false);
  virtual void FireOnLeave(bool motion = false, bool clearDragState = true);
  virtual void FireOnUpdate(float elapsed);
  virtual void FireOnSizeChanged(float height, float width);
  virtual void FireOnShow();
  virtual void FireOnHide();
  virtual void FireOnDragStart(uint32_t buttonFlags);
  virtual void FireOnDragStop();
  virtual void FireOnReceiveDrag();

  virtual bool OnMouseMove(const void* inputEvent);

  virtual void OnResize(const float* newRect);

  virtual void LoadNameAndId(const void* xmlNode, void* errorHandler);

  virtual void PostLoadProcess(const void* xmlNode, void* errorHandler);

  virtual void CompileScriptHandlers(void* compiler, void* luaState,
                                      void* errorCtx, bool recursive);

  virtual void RegisterLayerRenderCallbacks(SimpleRenderBatchSink& sink,
                                            int layerIndex);

  virtual void StopAllRegionAnimations();

  virtual void SetFocusFrame(CSimpleFrame* focus);

  virtual void AddChildToList(CSimpleFrame* child);

  virtual void RemoveChildFromList(CSimpleFrame* child);

  void ApplyVisibilityCascade(bool root_visible);

  virtual void SetParentFrame(CSimpleFrame* newParent);

  virtual bool CheckProtectedChildren(bool* outHasPending);

  virtual bool GetBoundsRect(float* bounds);

  virtual bool RefreshScaleCascade(bool force);

  virtual void OnAlphaChanged();

  virtual bool RefreshDepthCascade(bool force);

  void OnLayout() override;

  void PreRender(int arg1, bool runProtectionTransition, bool primeAlphaState) override;

  void PostRender(int arg1, bool runProtectionTransition) override;

  void OnUpdateCascade(float elapsed) override;

  void ApplyAnimRotation(FramePoint anchorPoint, const float* originOffset,
                         float radians) override;

  void ApplyAnimScale(std::uint32_t originPoint, const float* originOffset,
                      const float* scaleDelta) override;

  void OnEventCascade(int eventId, int arg1, int arg2) override;

  void RunAnimationSlots();

  void StopMovingOrSizing();

  static float PixelSnap(float value);

  void LoadAttributesXML(const void* xmlNode, void* errorHandler);

  virtual void LoadXML(const void* xmlNode, void* errorHandler);

  void StartMoving(int moveSizingMode);

  enum class ScrollChildRootOverride : std::int8_t {
    Keep = -1,
    Clear = 0,
    Set = 1,
  };

  void UpdateAlpha();

  uint8_t ApplyAlphaFadeStep(int16_t step);

  void SetInheritedAlpha(uint8_t value) noexcept { inheritedAlpha_ = value; }
  [[nodiscard]] uint8_t GetInheritedAlpha() const noexcept { return inheritedAlpha_; }
  [[nodiscard]] uint8_t GetCurrentAlpha() const noexcept { return currentAlpha_; }
  [[nodiscard]] uint8_t GetCascadedAlphaByte() const noexcept {
    return static_cast<uint8_t>(
        static_cast<uint32_t>(currentAlpha_) * inheritedAlpha_ / 255u);
  }
  [[nodiscard]] bool IsInScrollChildHierarchy() const noexcept {
    return inScrollChildHierarchy_;
  }
  [[nodiscard]] bool IsScrollChildRoot() const noexcept {
    return isScrollChildRoot_;
  }
  void SetScrollChildHierarchyState(
      bool in_scroll_child_hierarchy,
      ScrollChildRootOverride root_override);

  [[nodiscard]] const std::string& GetLastKeyDown() const noexcept { return lastKeyDown_; }
  [[nodiscard]] const std::string& GetLastKeyUp() const noexcept { return lastKeyUp_; }
  [[nodiscard]] const std::string& GetLastChar() const noexcept { return lastChar_; }
  [[nodiscard]] const std::string& GetLastMouseButton() const noexcept { return lastMouseButton_; }
  [[nodiscard]] bool GetLastMotion() const noexcept { return lastMotion_; }
  [[nodiscard]] int32_t GetLastWheelDelta() const noexcept { return lastWheelDelta_; }
  [[nodiscard]] float GetLastElapsed() const noexcept { return lastElapsed_; }
  [[nodiscard]] float GetLastOnSizeChangedHeight() const noexcept {
    return lastOnSizeChangedHeight_;
  }
  [[nodiscard]] float GetLastOnSizeChangedWidth() const noexcept {
    return lastOnSizeChangedWidth_;
  }

 protected:
  void ProcessFrameUpdatePass() override;
  void RefreshVisibilityFromHierarchy() override;
  void ShowVisible();
  void HideVisible();

  bool liveRectInitialized_{false};
  std::string lastMouseButton_;

 private:
  friend class CScriptRegion;

  void HandleLayoutInvalidation(bool attemptResolve) noexcept override;
  void OnRelativeAnchorTargetDestroyed() noexcept override;
  void SetDrawLayerStateInternal(DrawLayer layer, bool enabled,
                                 bool queueUpdate) noexcept;
  void LinkRegionFromParentChange(CScriptRegion* region) noexcept;
  void UnlinkRegionFromParentChange(CScriptRegion* region) noexcept;
  void SetHoveredChildFrame(CSimpleFrame* child) noexcept;
  void ClearHoveredChildFrame(CSimpleFrame* child) noexcept;
  void RegisterForStacking();
  void UnregisterFromStacking() noexcept;
  static void MarkToplevelOverlapStateDirty() noexcept;
  static void RefreshCachedToplevelOverlapFlags();
  static void RefreshCachedToplevelOverlapFlagsIfDirty();
  void RefreshRaiseOverlapState();
  void SetFrameLevelCascade(int32_t level);

  static void NotifyResolvedRectChanged(const CSimpleFrame& frame);

  [[nodiscard]] CSimpleFrame* FindNearestToplevelAncestor() noexcept;
  [[nodiscard]] const CSimpleFrame* FindNearestToplevelAncestor() const noexcept;
  [[nodiscard]] bool IsDescendantOf(const CSimpleFrame* ancestor) const noexcept;
  [[nodiscard]] bool OverlapsVisiblePeerInStrata() const;
  [[nodiscard]] bool RaiseNearestToplevelFrameIfIntersecting(bool refresh_overlap);
  void NotifyOwningScrollFrameOfContentChange();

  static void CompactVisibleLevels(FrameStrata strata);
  [[nodiscard]] static int32_t CountOccupiedVisibleLevels(FrameStrata strata);

  FrameStrata strata_{FrameStrata::Medium};
  int32_t level_{0};
  uint32_t frameId_{0};

  bool mouseEnabled_{false};
  bool keyboardEnabled_{false};
  bool mouseWheelEnabled_{false};
  bool movable_{false};
  bool resizable_{false};
  bool toplevel_{false};
  bool dontSavePosition_{false};
  bool propagateKeyboard_{false};

  bool protectionEnabled_{false};
  uint8_t protectionBits_{0};

  bool allowAttributeChanges_{false};

  bool ignoreDepth_{false};

  uint32_t registeredClicksMask_{0};

  DragState dragState_;

  bool highlightActive_{false};

  uint8_t currentAlpha_{255};

  uint8_t inheritedAlpha_{255};

  uint8_t computedAlpha_{255};

  bool loading_{false};

  bool visible_{true};
  std::uint64_t visibilityGeneration_{0};
  bool hasIntersectingVisiblePeer_{false};
  bool inScrollChildHierarchy_{false};
  bool isScrollChildRoot_{false};

  CSimpleFrame* focusFrame_{nullptr};

  CSimpleFrame* hoveredChildFrame_{nullptr};

  uint32_t layoutFlags_{0};

  float layoutWidth_{0.0f};

  float layoutHeight_{0.0f};

  float layoutScale_{1.0f};

  float layoutDepth_{1.0f};

  float inheritedDepth_{1.0f};

  float cachedLayoutRect_[4]{};

  bool layoutQueued_{false};
  uint8_t layoutRetryCount_{0};

  uint32_t inputEventFlags_{0};

  std::string lastKeyDown_;
  std::string lastKeyUp_;
  std::string lastChar_;
  bool lastMotion_{false};
  int32_t lastWheelDelta_{0};
  float lastElapsed_{0.0f};
  float lastOnSizeChangedHeight_{0.0f};
  float lastOnSizeChangedWidth_{0.0f};

  bool hasBackdrop_{false};
  BackdropInfo backdrop_;
  float bdColorR_{0}, bdColorG_{0}, bdColorB_{0}, bdColorA_{0};
  float bdBorderR_{0}, bdBorderG_{0}, bdBorderB_{0}, bdBorderA_{0};

  std::vector<CSimpleFrame*> children_;
  std::vector<std::unique_ptr<CSimpleFrame>> ownedChildren_;
  std::vector<std::unique_ptr<CScriptRegion>> ownedRegions_;
  std::vector<CScriptRegion*> regions_;
  std::unique_ptr<CScriptRegion> titleRegion_;
  std::unordered_set<std::string> events_;

  float hitInsetL_{0}, hitInsetR_{0}, hitInsetT_{0}, hitInsetB_{0};
  float scale_{1.0f};

  float depth_{0.0f};

  float maxResizeW_{0}, maxResizeH_{0};
  float minResizeW_{0}, minResizeH_{0};

  float clampInsetL_{0}, clampInsetR_{0}, clampInsetT_{0}, clampInsetB_{0};

  struct AnimationSlotBatchState {
    DeferredRenderCallbackList deferredCallbacks;
    uint32_t textCount{0};
    uint32_t textureCount{0};
    uint32_t lineCount{0};

    void Reset() noexcept {
      deferredCallbacks.Clear();
      textCount = 0;
      textureCount = 0;
      lineCount = 0;
    }

    void Submit() const {
      deferredCallbacks.ExecuteAll();
    }
  };

  uint32_t pendingAnimSlots_{0};
  static constexpr int kNumAnimSlots = 5;
  std::array<AnimationSlotBatchState, kNumAnimSlots> animationSlotBatches_{};

  static constexpr float kDragThresholdSq = 100.0f;

 private:
  void QueueLayoutInvalidation(bool attemptResolve = false) noexcept;
  bool TryResolveCachedLayoutRectFromCurrentRect() noexcept;

  [[nodiscard]] bool AllowsChildDrawLayer(DrawLayer layer) const noexcept override;
  void RefreshLayerVisibility(DrawLayer layer);
  void UpdateCachedLayoutRect(const ScreenRect& rect) noexcept;
  std::array<bool, static_cast<size_t>(DrawLayer::COUNT_)> drawLayerEnabled_{
      true, true, true, true, false};
};

}

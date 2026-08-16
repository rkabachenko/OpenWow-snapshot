
#include "openwow/ui/widgets/script_region.h"
#include "openwow/core/storm_error.h"
#include "openwow/ui/animation/animation_group.h"
#include "openwow/ui/widgets/simple_frame.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <utility>

namespace openwow::ui::widgets {

namespace {
struct DrawLayerEntry {
  DrawLayer layer;
  const char *name;
};
constexpr std::array kDrawLayers = {
    DrawLayerEntry{DrawLayer::Background, "BACKGROUND"},
    DrawLayerEntry{DrawLayer::Border, "BORDER"},
    DrawLayerEntry{DrawLayer::Artwork, "ARTWORK"},
    DrawLayerEntry{DrawLayer::Overlay, "OVERLAY"},
    DrawLayerEntry{DrawLayer::Highlight, "HIGHLIGHT"},
};

struct FramePointEntry {
  FramePoint pt;
  const char *name;
};
constexpr std::array kFramePoints = {
    FramePointEntry{FramePoint::TopLeft, "TOPLEFT"},
    FramePointEntry{FramePoint::Top, "TOP"},
    FramePointEntry{FramePoint::TopRight, "TOPRIGHT"},
    FramePointEntry{FramePoint::Left, "LEFT"},
    FramePointEntry{FramePoint::Center, "CENTER"},
    FramePointEntry{FramePoint::Right, "RIGHT"},
    FramePointEntry{FramePoint::BottomLeft, "BOTTOMLEFT"},
    FramePointEntry{FramePoint::Bottom, "BOTTOM"},
    FramePointEntry{FramePoint::BottomRight, "BOTTOMRIGHT"},
};

constexpr std::uint16_t AnchorDependencyMask(const FramePoint point) noexcept {
  return static_cast<std::uint16_t>(1u << static_cast<unsigned>(point));
}

bool UsesSimpleLayeredRegionDestructorParity(ScriptObjectType type) noexcept {
  switch (type) {
  case ScriptObjectType::Region:
  case ScriptObjectType::FontString:
  case ScriptObjectType::Texture:
  case ScriptObjectType::Line:
  case ScriptObjectType::Font:
    return true;
  default:
    return false;
  }
}

bool IsProtectedLayoutRecursive(const CScriptRegion *region,
                                std::unordered_set<const CScriptRegion *> *visited) noexcept {
  if (region == nullptr || visited == nullptr || !visited->insert(region).second) {
    return false;
  }

  if (const auto *frame = dynamic_cast<const CSimpleFrame *>(region); frame != nullptr &&
      frame->IsProtected()) {
    return true;
  }

  if (const auto *parent = region->GetParent();
      parent != nullptr && IsProtectedLayoutRecursive(parent, visited)) {
    return true;
  }

  for (const auto &entry : kFramePoints) {
    const auto *anchor = region->GetPoint(entry.pt);
    if (anchor == nullptr || anchor->relativeTo == nullptr || anchor->relativeTo == region) {
      continue;
    }
    if (IsProtectedLayoutRecursive(anchor->relativeTo, visited)) {
      return true;
    }
  }

  return false;
}

bool IsProtectedLayout(const CScriptRegion *region) noexcept {
  std::unordered_set<const CScriptRegion *> visited;
  return IsProtectedLayoutRecursive(region, &visited);
}
}

const char *DrawLayerName(DrawLayer layer) noexcept {
  for (const auto &e : kDrawLayers)
    if (e.layer == layer)
      return e.name;
  return "ARTWORK";
}

DrawLayer DrawLayerFromName(const std::string &name) noexcept {
  for (const auto &e : kDrawLayers)
    if (StrCaseEq(name.c_str(), e.name))
      return e.layer;
  return DrawLayer::Artwork;
}

const char *FramePointName(FramePoint pt) noexcept {
  for (const auto &e : kFramePoints)
    if (e.pt == pt)
      return e.name;
  return "TOPLEFT";
}

FramePoint FramePointFromName(const std::string &name) noexcept {
  for (const auto &e : kFramePoints)
    if (name == e.name)
      return e.pt;
  return FramePoint::TopLeft;
}

std::string ExpandParentNameToken(const CScriptRegion &region,
                                  const std::string_view name) {
  constexpr std::string_view kParentToken = "$parent";

  if (name.size() < kParentToken.size()) {
    return std::string(name);
  }

  for (std::size_t index = 0; index < kParentToken.size(); ++index) {
    const auto lhs = static_cast<unsigned char>(name[index]);
    const auto rhs = static_cast<unsigned char>(kParentToken[index]);
    if (std::tolower(lhs) != std::tolower(rhs)) {
      return std::string(name);
    }
  }

  std::string expanded;
  for (const CScriptRegion *parent = region.GetParent(); parent != nullptr;
       parent = parent->GetParent()) {
    if (!parent->GetName().empty()) {
      expanded = parent->GetName();
      break;
    }
  }

  expanded.append(name.substr(kParentToken.size()));
  return expanded;
}

bool CScriptRegion::IsTypeOf(const char *typeName) const noexcept {
  if (StrCaseEq(typeName, "Region"))
    return true;
  return CScriptObject::IsTypeOf(typeName);
}

void CScriptRegion::SetName(const std::string &name) {
  if (name.empty()) {
    CScriptObject::SetName(name);
    return;
  }
  CScriptObject::SetName(ExpandParentNameToken(*this, name));
}

CScriptRegion::~CScriptRegion() {

  const auto owned_animation_groups = std::exchange(ownedAnimationGroups_, {});
  for (auto *group : owned_animation_groups) {
    if (group != nullptr && group->GetOwnerRegion() == this) {
      group->SetOwnerRegion(nullptr);
    }
  }

  UnregisterAllRelativeDependencies();
  DetachDependentsOnDestroy();

  if (!UsesSimpleLayeredRegionDestructorParity(type_)) {
    return;
  }

  if (parent_ != nullptr) {

    HideVisible();
    if (auto *ownerFrame = dynamic_cast<CSimpleFrame *>(parent_); ownerFrame != nullptr) {
      ownerFrame->DetachDestroyedRegion(this);
    }
    parent_ = nullptr;
    return;
  }

  if (shown_) {
    Hide();
    return;
  }

  HideVisible();
}

void CScriptRegion::Show() {
  shown_ = true;
  RefreshVisibilityFromHierarchy();
}

void CScriptRegion::Hide() {
  shown_ = false;
  HideVisible();
}

void CScriptRegion::ApplyParentLayerAndVisibility(CScriptRegion *parent,
                                                  DrawLayer layer,
                                                  bool shown) noexcept {
  if (parent_ == parent) {
    if (parent_ == nullptr) {
      if (effectiveVisible_) {
        HideVisible();
      }
      drawLayer_ = layer;
      shown_ = shown;
      return;
    }

    if (drawLayer_ == layer) {
      if (shown_ == shown) {
        return;
      }
      shown_ = shown;
      RefreshVisibilityFromHierarchy();
      return;
    }

    if (effectiveVisible_) {
      HideVisible();
    }
    drawLayer_ = layer;
    shown_ = shown;
    RefreshVisibilityFromHierarchy();
    return;
  }

  auto *old_parent_frame = dynamic_cast<CSimpleFrame *>(parent_);
  if (old_parent_frame != nullptr) {
    HideVisible();
    old_parent_frame->UnlinkRegionFromParentChange(this);
  }

  parent_ = parent;
  drawLayer_ = layer;

  auto *new_parent_frame = dynamic_cast<CSimpleFrame *>(parent_);
  if (new_parent_frame != nullptr) {
    new_parent_frame->LinkRegionFromParentChange(this);
    OnParentAlphaChanged(false);
    OnFrameDepthChanged(new_parent_frame->GetEffectiveDepth(), false);
    shown_ = shown;
    RefreshVisibilityFromHierarchy();
    return;
  }

  shown_ = shown;
  if (old_parent_frame == nullptr) {
    RefreshVisibilityFromHierarchy();
  }
}

void CScriptRegion::SetParent(CScriptRegion *parent) {
  ApplyParentLayerAndVisibility(parent, drawLayer_, shown_);
}

void CScriptRegion::SetPoint(const RegionAnchor &anchor) {
  if (anchor.relativeTo == nullptr || anchor.relativeTo == this) {
    openwow::core::SErrSetLastError(87);
    return;
  }

  if (!anchor.relativeTo->CanAcceptProtectedDependency(this)) {
    return;
  }

  UnregisterRelativeDependency(anchor.point);
  anchors_[anchor.point] = anchor;
  RegisterRelativeDependency(anchor.point, anchor.relativeTo);
  InvalidateLayout(false);
}

void CScriptRegion::SetAllPoints(CScriptRegion *relativeTo) {
  if (relativeTo == nullptr || relativeTo == this) {
    openwow::core::SErrSetLastError(87);
    return;
  }

  if (!relativeTo->CanAcceptProtectedDependency(this)) {
    return;
  }

  UnregisterAllRelativeDependencies();
  anchors_.clear();
  for (auto pt : {FramePoint::TopLeft, FramePoint::BottomRight}) {
    RegionAnchor anchor;
    anchor.point = pt;
    anchor.relativeTo = relativeTo;
    anchor.relativePoint = pt;
    anchors_[pt] = anchor;
    RegisterRelativeDependency(pt, relativeTo);
  }
  InvalidateLayout(false);
}

bool CScriptRegion::CanAcceptProtectedDependency(
    const CScriptRegion *dependent) const noexcept {
  if (!IsProtectedLayout(dependent)) {
    return true;
  }

  for (const auto &entry : kFramePoints) {
    const auto *anchor = GetPoint(entry.pt);
    if (anchor == nullptr || anchor->relativeTo == nullptr || anchor->relativeTo == this) {
      continue;
    }
    if (!anchor->relativeTo->CanAcceptProtectedDependency(dependent)) {
      return false;
    }
  }

  return true;
}

void CScriptRegion::AttachAnimationGroup(std::uintptr_t handle, AnimationKindQuery containsKind) {
  AttachAnimationGroup(handle, std::move(containsKind), nullptr);
}

void CScriptRegion::AttachAnimationGroup(std::uintptr_t handle, AnimationKindQuery containsKind,
                                         AnimationStopCallback stopCallback) {
  if (handle == 0 || !containsKind) {
    return;
  }

  const auto existing = std::find_if(
      attachedAnimationGroups_.begin(), attachedAnimationGroups_.end(),
      [handle](const AttachedAnimationGroup &entry) { return entry.handle == handle; });
  if (existing != attachedAnimationGroups_.end()) {
    existing->containsKind = std::move(containsKind);
    if (stopCallback) {
      existing->stopCallback = std::move(stopCallback);
    }
    return;
  }

  attachedAnimationGroups_.push_back(
      AttachedAnimationGroup{handle, std::move(containsKind), std::move(stopCallback)});
}

void CScriptRegion::AttachAnimationKind(const std::uintptr_t handle,
                                        const openwow::ui::anim::AnimKind kind) {
  AttachAnimationGroup(handle, [kind](const openwow::ui::anim::AnimKind queriedKind) {
    return queriedKind == kind;
  });
}

void CScriptRegion::StopAllAttachedAnimationGroups() {
  if (attachedAnimationGroups_.empty()) {
    return;
  }

  auto groups = std::move(attachedAnimationGroups_);
  attachedAnimationGroups_.clear();

  for (auto &entry : groups) {
    if (entry.stopCallback) {
      entry.stopCallback();
    }
  }
}

bool CScriptRegion::DetachAnimationGroup(const std::uintptr_t handle) {
  if (handle == 0) {
    return false;
  }

  const auto originalSize = attachedAnimationGroups_.size();
  attachedAnimationGroups_.erase(std::remove_if(attachedAnimationGroups_.begin(),
                                                attachedAnimationGroups_.end(),
                                                [handle](const AttachedAnimationGroup &entry) {
                                                  return entry.handle == handle;
                                                }),
                                 attachedAnimationGroups_.end());
  return attachedAnimationGroups_.size() != originalSize;
}

bool CScriptRegion::HasAttachedAnimationGroupsInSelfOrAncestorHierarchy() const noexcept {
  for (const CScriptRegion *region = this; region != nullptr; region = region->parent_) {
    if (!region->attachedAnimationGroups_.empty()) {
      return true;
    }
  }

  return false;
}

bool CScriptRegion::HasAttachedAnimationKind(const openwow::ui::anim::AnimKind kind) const {
  return std::any_of(attachedAnimationGroups_.begin(), attachedAnimationGroups_.end(),
                     [kind](const AttachedAnimationGroup &entry) {
                       return entry.containsKind && entry.containsKind(kind);
                     });
}

bool CScriptRegion::HasAttachedAnimationKindInSelfOrAncestorHierarchy(
    const openwow::ui::anim::AnimKind kind) const {
  for (const CScriptRegion *region = this; region != nullptr; region = region->parent_) {
    if (region->HasAttachedAnimationKind(kind)) {
      return true;
    }
  }

  return false;
}

void CScriptRegion::RegisterOwnedAnimationGroup(openwow::ui::anim::AnimationGroup *group) {
  if (group == nullptr) {
    return;
  }

  ownedAnimationGroups_.insert(ownedAnimationGroups_.begin(), group);
}

void CScriptRegion::UnregisterOwnedAnimationGroup(openwow::ui::anim::AnimationGroup *group) {
  if (group == nullptr) {
    return;
  }

  ownedAnimationGroups_.erase(
      std::remove(ownedAnimationGroups_.begin(), ownedAnimationGroups_.end(), group),
      ownedAnimationGroups_.end());
}

void CScriptRegion::UpdateOwnedAnimationGroups(const float elapsed) {

  for (auto *group : ownedAnimationGroups_) {
    if (group != nullptr) {
      group->Update(elapsed);
    }
  }
}

void CScriptRegion::SetDrawLayer(DrawLayer layer) noexcept {
  ApplyParentLayerAndVisibility(parent_, layer, shown_);
}

void CScriptRegion::QueueOwningFrameDrawLayerStateUpdateIfVisible() noexcept {
  if (!IsVisible()) {
    return;
  }

  auto *owner_frame = dynamic_cast<CSimpleFrame *>(parent_);
  if (!owner_frame) {
    return;
  }

  owner_frame->QueueDrawLayerStateUpdate(drawLayer_);
}

void CScriptRegion::QueueOwningFrameRenderRetryIfVisible() const noexcept {
  if (!IsVisible()) {
    return;
  }

  auto *owner_frame = dynamic_cast<CSimpleFrame *>(parent_);
  if (!owner_frame) {
    return;
  }

  owner_frame->QueueRenderRetryStateUpdate();
}

void CScriptRegion::NotifyOwningScrollFrameOfContentChangeIfVisible() noexcept {
  if (!IsVisible()) {
    return;
  }

  auto *owner_frame = dynamic_cast<CSimpleFrame *>(parent_);
  if (!owner_frame) {
    return;
  }

  owner_frame->NotifyOwningScrollFrameOfContentChange();
}

void CScriptRegion::RefreshVisibilityFromHierarchy() {
  const bool parent_visible = !parent_ || parent_->IsVisible();
  const bool layer_visible = !parent_ || parent_->AllowsChildDrawLayer(drawLayer_);
  if (shown_ && parent_visible && layer_visible) {
    ShowVisible();
    return;
  }

  HideVisible();
}

bool CScriptRegion::IsDragMovedInHierarchy() const noexcept {
  if (parent_) {
    return parent_->IsDragMovedInHierarchy();
  }
  return false;
}

void CScriptRegion::ShowVisible() {
  if (effectiveVisible_) {
    return;
  }

  if (auto *owner_frame = dynamic_cast<CSimpleFrame *>(parent_); owner_frame != nullptr) {
    OnFrameScaleChanged(owner_frame->GetEffectiveScale(), false);
    owner_frame->QueueDrawLayerStateUpdate(drawLayer_);
  }

  effectiveVisible_ = true;
  OnEffectiveVisibilityChanged(true);
}

void CScriptRegion::HideVisible() {
  if (!effectiveVisible_) {
    return;
  }

  if (auto *owner_frame = dynamic_cast<CSimpleFrame *>(parent_); owner_frame != nullptr) {
    owner_frame->QueueDrawLayerStateUpdate(drawLayer_);
  }

  effectiveVisible_ = false;
  OnEffectiveVisibilityChanged(false);
}

void CScriptRegion::HandleLayoutInvalidation(const bool attemptResolve) noexcept {
  if (attemptResolve && liveRectInitialized_ && std::isfinite(rect_.left) &&
      std::isfinite(rect_.top) && std::isfinite(rect_.right) &&
      std::isfinite(rect_.bottom)) {
    cachedLayoutRect_ = rect_;
    cachedLayoutRectValid_ = true;
    layoutDirty_ = false;
    return;
  }

  layoutDirty_ = true;
}

void CScriptRegion::InvalidateLayout(bool attemptResolve) noexcept {
  HandleLayoutInvalidation(attemptResolve);
}

bool CScriptRegion::TryGetCachedLayoutRect(ScreenRect *outRect) const noexcept {
  if (outRect == nullptr || !cachedLayoutRectValid_) {
    return false;
  }

  *outRect = cachedLayoutRect_;
  return true;
}

void CScriptRegion::ResolvePendingLayoutRect() noexcept {
  if (!layoutDirty_) {
    return;
  }

  InvalidateLayout(true);
}

void CScriptRegion::InvalidateRelativeDependents() noexcept {
  for (const auto &[dependent, mask] : relativeDependents_) {
    if (dependent == nullptr || mask == 0u) {
      continue;
    }

    dependent->InvalidateLayout(false);
  }
}

void CScriptRegion::RegisterRelativeDependency(FramePoint point,
                                               CScriptRegion *relativeTo) noexcept {
  if (relativeTo == nullptr || relativeTo == this) {
    return;
  }

  relativeTo->relativeDependents_[this] |= AnchorDependencyMask(point);
}

void CScriptRegion::UnregisterRelativeDependency(FramePoint point) noexcept {
  const auto it = anchors_.find(point);
  if (it == anchors_.end()) {
    return;
  }

  UnregisterRelativeDependency(point, it->second.relativeTo);
}

void CScriptRegion::UnregisterRelativeDependency(FramePoint point,
                                                 CScriptRegion *relativeTo) noexcept {
  if (relativeTo == nullptr || relativeTo == this) {
    return;
  }

  const auto dependent_it = relativeTo->relativeDependents_.find(this);
  if (dependent_it == relativeTo->relativeDependents_.end()) {
    return;
  }

  dependent_it->second &= static_cast<std::uint16_t>(~AnchorDependencyMask(point));
  if (dependent_it->second == 0u) {
    relativeTo->relativeDependents_.erase(dependent_it);
  }
}

void CScriptRegion::UnregisterAllRelativeDependencies() noexcept {
  for (const auto &[point, anchor] : anchors_) {
    UnregisterRelativeDependency(point, anchor.relativeTo);
  }
}

void CScriptRegion::DetachDependentsOnDestroy() noexcept {
  auto dependents = std::move(relativeDependents_);
  relativeDependents_.clear();

  for (const auto &[dependent, mask] : dependents) {
    if (dependent == nullptr || mask == 0u) {
      continue;
    }
    dependent->RemoveDestroyedRelative(this);
  }
}

void CScriptRegion::RemoveDestroyedRelative(CScriptRegion *relativeTo) noexcept {
  bool removed_anchor = false;
  for (auto it = anchors_.begin(); it != anchors_.end();) {
    if (it->second.relativeTo != relativeTo) {
      ++it;
      continue;
    }
    it = anchors_.erase(it);
    removed_anchor = true;
  }

  if (removed_anchor) {
    OnRelativeAnchorTargetDestroyed();
  }
}

}

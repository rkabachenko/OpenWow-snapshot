#include "openwow/ui/anchor_system.h"

#include <algorithm>
#include <cmath>

namespace openwow::ui {

void AnchorSystem::SetPoint(const std::string& frame,
                            const FrameAnchor& anchor) {
  auto& fd = GetOrCreate(frame);
  fd.anchors[anchor.point] = anchor;
}

void AnchorSystem::SetAllPoints(const std::string& frame,
                                const std::string& relativeTo) {
  auto& fd = GetOrCreate(frame);

  for (auto p : {AnchorPoint::TopLeft, AnchorPoint::BottomRight}) {
    FrameAnchor a;
    a.point = p;
    a.relativeTo = relativeTo;
    a.relativePoint = p;
    a.offsetX = 0.0f;
    a.offsetY = 0.0f;
    fd.anchors[p] = a;
  }
}

void AnchorSystem::ClearPoints(const std::string& frame) {
  auto it = frames_.find(frame);
  if (it != frames_.end()) it->second.anchors.clear();
}

void AnchorSystem::ClearPoint(const std::string& frame, AnchorPoint point) {
  auto it = frames_.find(frame);
  if (it != frames_.end()) it->second.anchors.erase(point);
}

std::vector<FrameAnchor> AnchorSystem::GetAnchors(
    const std::string& frame) const {
  std::vector<FrameAnchor> result;
  auto fd = FindFrame(frame);
  if (!fd) return result;
  result.reserve(fd->anchors.size());
  for (const auto& [_, a] : fd->anchors) result.push_back(a);
  return result;
}

std::size_t AnchorSystem::GetAnchorCount(const std::string& frame) const {
  auto fd = FindFrame(frame);
  return fd ? fd->anchors.size() : 0;
}

bool AnchorSystem::HasAnchor(const std::string& frame,
                             AnchorPoint point) const {
  auto fd = FindFrame(frame);
  if (!fd) return false;
  return fd->anchors.count(point) > 0;
}

void AnchorSystem::SetSize(const std::string& frame, float width,
                           float height) {
  auto& fd = GetOrCreate(frame);
  fd.size.width = width;
  fd.size.height = height;
}

FrameSize AnchorSystem::GetSize(const std::string& frame) const {
  auto fd = FindFrame(frame);
  return fd ? fd->size : FrameSize{};
}

namespace {

void AnchorToCoords(AnchorPoint p, const FrameRect& r, float& outX,
                    float& outY) {
  switch (p) {
    case AnchorPoint::TopLeft:
      outX = r.left;
      outY = r.top;
      break;
    case AnchorPoint::Top:
      outX = (r.left + r.right) * 0.5f;
      outY = r.top;
      break;
    case AnchorPoint::TopRight:
      outX = r.right;
      outY = r.top;
      break;
    case AnchorPoint::Left:
      outX = r.left;
      outY = (r.top + r.bottom) * 0.5f;
      break;
    case AnchorPoint::Center:
      outX = (r.left + r.right) * 0.5f;
      outY = (r.top + r.bottom) * 0.5f;
      break;
    case AnchorPoint::Right:
      outX = r.right;
      outY = (r.top + r.bottom) * 0.5f;
      break;
    case AnchorPoint::BottomLeft:
      outX = r.left;
      outY = r.bottom;
      break;
    case AnchorPoint::Bottom:
      outX = (r.left + r.right) * 0.5f;
      outY = r.bottom;
      break;
    case AnchorPoint::BottomRight:
      outX = r.right;
      outY = r.bottom;
      break;
    default:
      outX = 0.0f;
      outY = 0.0f;
      break;
  }
}

}

FrameRect AnchorSystem::ResolvePosition(const std::string& frame) const {
  auto fd = FindFrame(frame);
  if (!fd) return {};

  if (fd->anchors.empty()) {
    return {0.0f, 0.0f, fd->size.width, fd->size.height};
  }

  float sumX = 0.0f, sumY = 0.0f;
  std::size_t count = 0;

  for (const auto& [_, a] : fd->anchors) {
    FrameRect relRect{};
    if (!a.relativeTo.empty()) {
      auto rel = FindFrame(a.relativeTo);
      if (rel) {

        relRect = FrameRect{0.0f, 0.0f, rel->size.width, rel->size.height};
      }
    }
    float rx = 0.0f, ry = 0.0f;
    AnchorToCoords(a.relativePoint, relRect, rx, ry);
    sumX += rx + a.offsetX;
    sumY += ry + a.offsetY;
    ++count;
  }

  float cx = sumX / static_cast<float>(count);
  float cy = sumY / static_cast<float>(count);

  float hw = fd->size.width * 0.5f;
  float hh = fd->size.height * 0.5f;

  return {cx - hw, cy - hh, cx + hw, cy + hh};
}

float AnchorSystem::GetEffectiveWidth(const std::string& frame) const {
  auto r = ResolvePosition(frame);
  return r.right - r.left;
}

float AnchorSystem::GetEffectiveHeight(const std::string& frame) const {
  auto r = ResolvePosition(frame);
  return r.bottom - r.top;
}

std::size_t AnchorSystem::GetFrameCount() const { return frames_.size(); }

void AnchorSystem::Reset() { frames_.clear(); }

const AnchorSystem::FrameData* AnchorSystem::FindFrame(
    const std::string& name) const {
  auto it = frames_.find(name);
  return it != frames_.end() ? &it->second : nullptr;
}

AnchorSystem::FrameData& AnchorSystem::GetOrCreate(const std::string& name) {
  return frames_[name];
}

}

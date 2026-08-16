
#include "openwow/game/taxi_system.h"

#include <utility>

namespace openwow::game {

namespace {

std::vector<TaxiPreviewSegment>& EnsurePreviewSlot(
    std::vector<std::vector<TaxiPreviewSegment>>& slots,
    const std::size_t slot) {
  if (slots.size() <= slot) {
    slots.resize(slot + 1);
  }
  return slots[slot];
}

const TaxiPreviewSegment* FindPreviewSegment(
    const std::vector<std::vector<TaxiPreviewSegment>>& slots,
    const std::size_t slot,
    const std::size_t route_index) {
  if (slot >= slots.size() || route_index >= slots[slot].size()) {
    return nullptr;
  }
  return &slots[slot][route_index];
}

}

TaxiSystem& TaxiSystem::Get() {
  static TaxiSystem instance;
  return instance;
}

void TaxiSystem::ResetRouteDisplayStateUnlocked() {
  preview_line_pairs_.clear();
  preview_segments_by_slot_.clear();
  cached_display_slice_.reset();
}

void TaxiSystem::ResetSessionStateUnlocked() {
  ResetRouteDisplayStateUnlocked();
  taxi_map_open_ = false;
  current_node_id_ = 0;
  selected_destination_ = 0;
}

void TaxiSystem::OpenTaxiMap(const std::uint32_t current_node_id) {
  std::lock_guard lock(mutex_);
  ResetRouteDisplayStateUnlocked();
  taxi_map_open_ = true;
  current_node_id_ = current_node_id;
  selected_destination_ = 0;
}

void TaxiSystem::CloseTaxiMap() {
  std::lock_guard lock(mutex_);
  taxi_map_open_ = false;

  preview_segments_by_slot_.clear();
}

void TaxiSystem::ResetRouteDisplayState() {
  std::lock_guard lock(mutex_);
  ResetRouteDisplayStateUnlocked();
}

void TaxiSystem::CacheDisplaySlice(TaxiSliceState state) {
  std::lock_guard lock(mutex_);
  cached_display_slice_ = std::move(state);
}

std::optional<TaxiSliceState> TaxiSystem::GetCachedDisplaySlice() const {
  std::lock_guard lock(mutex_);
  return cached_display_slice_;
}

void TaxiSystem::ResetSessionState() {
  std::lock_guard lock(mutex_);
  ResetSessionStateUnlocked();
}

bool TaxiSystem::IsTaxiMapOpen() const {
  std::lock_guard lock(mutex_);
  return taxi_map_open_;
}

std::uint32_t TaxiSystem::GetCurrentNPCNode() const {
  std::lock_guard lock(mutex_);
  return current_node_id_;
}

void TaxiSystem::SetSelectedDestination(const std::uint32_t node_id) {
  std::lock_guard lock(mutex_);
  selected_destination_ = node_id;
}

std::uint32_t TaxiSystem::GetSelectedDestination() const {
  std::lock_guard lock(mutex_);
  return selected_destination_;
}

void TaxiSystem::ClearPreviewLinePairs() {
  std::lock_guard lock(mutex_);
  preview_line_pairs_.clear();
}

void TaxiSystem::AppendPreviewLinePair(const std::uint32_t src_node_id,
                                       const std::uint32_t dst_node_id) {
  std::lock_guard lock(mutex_);
  preview_line_pairs_.push_back({src_node_id, dst_node_id});
}

std::vector<TaxiPreviewLine> TaxiSystem::GetPreviewLinePairs() const {
  std::lock_guard lock(mutex_);
  return preview_line_pairs_;
}

void TaxiSystem::ClearPreviewSegments(const std::size_t slot) {
  std::lock_guard lock(mutex_);
  if (slot < preview_segments_by_slot_.size()) {
    preview_segments_by_slot_[slot].clear();
  }
}

std::size_t TaxiSystem::AppendZeroedPreviewSegment(const std::size_t slot) {
  std::lock_guard lock(mutex_);
  auto& segments = EnsurePreviewSlot(preview_segments_by_slot_, slot);
  segments.emplace_back();
  return segments.size() - 1;
}

void TaxiSystem::AppendPreviewSegment(
    const std::size_t slot, const TaxiPreviewSegment& segment) {
  std::lock_guard lock(mutex_);
  EnsurePreviewSlot(preview_segments_by_slot_, slot).push_back(segment);
}

bool TaxiSystem::SetPreviewSegment(const std::size_t slot,
                                   const std::size_t route_index,
                                   const TaxiPreviewSegment& segment) {
  std::lock_guard lock(mutex_);
  if (slot >= preview_segments_by_slot_.size() ||
      route_index >= preview_segments_by_slot_[slot].size()) {
    return false;
  }
  preview_segments_by_slot_[slot][route_index] = segment;
  return true;
}

std::size_t TaxiSystem::GetPreviewRouteCount(const std::size_t slot) const {
  std::lock_guard lock(mutex_);
  return slot < preview_segments_by_slot_.size()
             ? preview_segments_by_slot_[slot].size()
             : 0;
}

float TaxiSystem::GetPreviewSrcX(const std::size_t slot,
                                 const std::size_t route_index) const {
  std::lock_guard lock(mutex_);
  const auto* segment =
      FindPreviewSegment(preview_segments_by_slot_, slot, route_index);
  return segment != nullptr ? segment->src_x : 0.0f;
}

float TaxiSystem::GetPreviewSrcY(const std::size_t slot,
                                 const std::size_t route_index) const {
  std::lock_guard lock(mutex_);
  const auto* segment =
      FindPreviewSegment(preview_segments_by_slot_, slot, route_index);
  return segment != nullptr ? segment->src_y : 0.0f;
}

float TaxiSystem::GetPreviewDestX(const std::size_t slot,
                                  const std::size_t route_index) const {
  std::lock_guard lock(mutex_);
  const auto* segment =
      FindPreviewSegment(preview_segments_by_slot_, slot, route_index);
  return segment != nullptr ? segment->dst_x : 0.0f;
}

float TaxiSystem::GetPreviewDestY(const std::size_t slot,
                                  const std::size_t route_index) const {
  std::lock_guard lock(mutex_);
  const auto* segment =
      FindPreviewSegment(preview_segments_by_slot_, slot, route_index);
  return segment != nullptr ? segment->dst_y : 0.0f;
}

void TaxiSystem::Reset() {
  std::lock_guard lock(mutex_);
  ResetSessionStateUnlocked();
}

}

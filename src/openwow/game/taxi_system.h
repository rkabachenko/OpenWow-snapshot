
#pragma once

#include "openwow/game/taxi_slice_state.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace openwow::game {

struct TaxiPreviewLine {
  std::uint32_t src_node_id = 0;
  std::uint32_t dst_node_id = 0;
};

struct TaxiPreviewSegment {
  float src_x = 0.0f;
  float src_y = 0.0f;
  float src_z = 0.0f;
  float dst_x = 0.0f;
  float dst_y = 0.0f;
  float dst_z = 0.0f;
};

class TaxiSystem {
 public:
  static TaxiSystem& Get();

  TaxiSystem(const TaxiSystem&) = delete;
  TaxiSystem& operator=(const TaxiSystem&) = delete;

  void OpenTaxiMap(std::uint32_t current_node_id);
  void CloseTaxiMap();
  void ResetRouteDisplayState();
  void CacheDisplaySlice(TaxiSliceState state);
  [[nodiscard]] std::optional<TaxiSliceState> GetCachedDisplaySlice() const;
  void ResetSessionState();
  [[nodiscard]] bool IsTaxiMapOpen() const;
  [[nodiscard]] std::uint32_t GetCurrentNPCNode() const;

  void SetSelectedDestination(std::uint32_t node_id);
  [[nodiscard]] std::uint32_t GetSelectedDestination() const;

  void ClearPreviewLinePairs();
  void AppendPreviewLinePair(std::uint32_t src_node_id,
                             std::uint32_t dst_node_id);
  [[nodiscard]] std::vector<TaxiPreviewLine> GetPreviewLinePairs() const;

  void ClearPreviewSegments(std::size_t slot);
  [[nodiscard]] std::size_t AppendZeroedPreviewSegment(std::size_t slot);
  void AppendPreviewSegment(std::size_t slot,
                            const TaxiPreviewSegment& segment);
  [[nodiscard]] bool SetPreviewSegment(std::size_t slot,
                                       std::size_t route_index,
                                       const TaxiPreviewSegment& segment);
  [[nodiscard]] std::size_t GetPreviewRouteCount(std::size_t slot) const;
  [[nodiscard]] float GetPreviewSrcX(std::size_t slot,
                                     std::size_t route_index) const;
  [[nodiscard]] float GetPreviewSrcY(std::size_t slot,
                                     std::size_t route_index) const;
  [[nodiscard]] float GetPreviewDestX(std::size_t slot,
                                      std::size_t route_index) const;
  [[nodiscard]] float GetPreviewDestY(std::size_t slot,
                                      std::size_t route_index) const;

  void Reset();

 private:
  TaxiSystem() = default;

  void ResetRouteDisplayStateUnlocked();
  void ResetSessionStateUnlocked();

  bool taxi_map_open_ = false;
  std::uint32_t current_node_id_ = 0;
  std::uint32_t selected_destination_ = 0;
  std::vector<TaxiPreviewLine> preview_line_pairs_;
  std::vector<std::vector<TaxiPreviewSegment>> preview_segments_by_slot_;
  std::optional<TaxiSliceState> cached_display_slice_;

  mutable std::mutex mutex_;
};

}

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace openwow::debug {

enum class InspectorRangeThumb : std::uint8_t {
  kNone = 0,
  kLower = 1,
  kUpper = 2,
};

struct InspectorRangeDragEffect {
  InspectorRangeThumb thumb{InspectorRangeThumb::kNone};
  bool dispatch_action{false};
  bool request_redraw{false};

  [[nodiscard]] bool handled() const noexcept {
    return thumb != InspectorRangeThumb::kNone;
  }

  bool operator==(const InspectorRangeDragEffect&) const = default;
};

struct InspectorRect {
  float x{0.0F};
  float y{0.0F};
  float width{0.0F};
  float height{0.0F};

  bool operator==(const InspectorRect&) const = default;
};

struct InspectorRangeDrawLayout {
  InspectorRect track;
  std::optional<InspectorRect> lower_range;
  std::optional<InspectorRect> selected_range;
  std::optional<InspectorRect> upper_range;
  std::string label;
  float label_center_x{0.0F};
  float label_baseline_y{0.0F};

  [[nodiscard]] bool values_are_drawable() const noexcept {
    return lower_range.has_value();
  }
};

class InspectorRangeSliderModel {
 public:
  [[nodiscard]] float min_value() const noexcept { return min_value_; }
  [[nodiscard]] float max_value() const noexcept { return max_value_; }
  [[nodiscard]] float lower_value() const noexcept { return lower_value_; }
  [[nodiscard]] float upper_value() const noexcept { return upper_value_; }
  [[nodiscard]] InspectorRangeThumb tracking_thumb() const noexcept {
    return tracking_thumb_;
  }

  void set_min_value(float value) noexcept { min_value_ = value; }
  void set_max_value(float value) noexcept { max_value_ = value; }
  void set_lower_value(float value) noexcept { lower_value_ = value; }
  void set_upper_value(float value) noexcept { upper_value_ = value; }

  [[nodiscard]] InspectorRangeDragEffect PointerDown(
      float pointer_x, float pointer_y, float bounds_width,
      float bounds_height) noexcept;
  [[nodiscard]] InspectorRangeDragEffect PointerDragged(
      float pointer_x, float bounds_width) noexcept;

  [[nodiscard]] InspectorRangeDrawLayout DrawLayout(
      InspectorRect bounds) const;

 private:
  [[nodiscard]] static float RetailClamp(float value, float lower,
                                         float upper) noexcept;

  float min_value_{0.0F};
  float max_value_{1.0F};
  float lower_value_{0.0F};
  float upper_value_{1.0F};
  InspectorRangeThumb tracking_thumb_{InspectorRangeThumb::kNone};
};

using InspectorScrollSource = std::uint64_t;

struct InspectorScrollSyncTransition {
  std::optional<InspectorScrollSource> unregister_source;
  std::optional<InspectorScrollSource> register_source;

  bool operator==(const InspectorScrollSyncTransition&) const = default;
};

struct InspectorScrollSyncEffect {
  InspectorRect target_document_rect;
  bool reflect_target_clip_view{false};

  bool operator==(const InspectorScrollSyncEffect&) const = default;
};

class InspectorVerticalScrollSyncModel {
 public:
  [[nodiscard]] InspectorScrollSyncTransition SetSynchronizedSource(
      std::optional<InspectorScrollSource> source) noexcept;
  [[nodiscard]] InspectorScrollSyncTransition StopSynchronizing() noexcept;

  [[nodiscard]] std::optional<InspectorScrollSource> source() const noexcept {
    return source_;
  }

  [[nodiscard]] std::optional<InspectorScrollSyncEffect>
  SourceVisibleRectChanged(InspectorScrollSource source,
                           InspectorRect source_visible_rect) const noexcept;

 private:
  std::optional<InspectorScrollSource> source_;
};

enum class InspectorDomain : std::uint8_t {
  kObjects,
  kResources,
  kDbc,
  kPackets,
  kRender,
  kCapture,
  kCount,
};

struct InspectorRecordIdentity {
  InspectorDomain domain{InspectorDomain::kObjects};
  std::uint64_t value{0};
  std::uint64_t generation{0};

  bool operator==(const InspectorRecordIdentity&) const = default;
};

struct InspectorObjectSnapshot {
  std::string type;
  std::string name;
  std::uint32_t entry{0};
  std::uint32_t display_id{0};
  double x{0.0};
  double y{0.0};
  double z{0.0};
  double orientation_radians{0.0};
  std::uint64_t health{0};
  std::uint64_t maximum_health{0};
  std::uint32_t level{0};

  bool operator==(const InspectorObjectSnapshot&) const = default;
};

struct InspectorResourceSnapshot {
  std::string kind;
  std::string name;
  std::string state;
  std::string status;
  std::optional<std::uint64_t> count;
  std::optional<std::uint64_t> bytes;

  bool operator==(const InspectorResourceSnapshot&) const = default;
};

using InspectorDbcFieldValue =
    std::variant<std::uint32_t, std::int32_t, float, std::string>;

struct InspectorDbcFieldSnapshot {
  std::uint32_t index{0};
  InspectorDbcFieldValue value{std::uint32_t{0}};

  bool operator==(const InspectorDbcFieldSnapshot&) const = default;
};

struct InspectorDbcRecordSnapshot {
  std::string path;
  std::uint32_t row{0};
  std::vector<InspectorDbcFieldSnapshot> fields;

  bool operator==(const InspectorDbcRecordSnapshot&) const = default;
};

enum class InspectorPacketDirection : std::uint8_t {
  kClientToServer,
  kServerToClient,
};

struct InspectorPacketSnapshot {
  InspectorPacketDirection direction{InspectorPacketDirection::kClientToServer};
  std::uint32_t opcode{0};
  std::string name;
  std::uint64_t payload_bytes{0};
  std::uint64_t timestamp_nanoseconds{0};
  bool filtered{false};

  bool operator==(const InspectorPacketSnapshot&) const = default;
};

struct InspectorRenderSnapshot {
  std::string backend;
  std::string status;
  std::uint64_t frame_number{0};
  std::uint64_t gpu_duration_nanoseconds{0};
  std::uint64_t cpu_duration_nanoseconds{0};
  std::uint64_t draw_calls{0};
  std::uint64_t compute_calls{0};
  std::uint64_t blit_calls{0};
  std::uint64_t gpu_bytes{0};

  bool operator==(const InspectorRenderSnapshot&) const = default;
};

struct InspectorCaptureSnapshot {
  std::uint64_t capture_identity{0};
  std::uint32_t frame_index{0};
  std::string label;
  std::string primitive;
  std::uint64_t gpu_duration_nanoseconds{0};
  std::uint64_t resource_count{0};
  bool renderable{false};
  bool has_error{false};

  bool operator==(const InspectorCaptureSnapshot&) const = default;
};

using InspectorRecordValue =
    std::variant<InspectorObjectSnapshot, InspectorResourceSnapshot,
                 InspectorDbcRecordSnapshot, InspectorPacketSnapshot,
                 InspectorRenderSnapshot, InspectorCaptureSnapshot>;

struct InspectorRecord {
  InspectorRecordIdentity identity;
  InspectorRecordValue value;
  bool stale{false};

  bool operator==(const InspectorRecord&) const = default;
};

struct InspectorSnapshot {
  InspectorDomain domain{InspectorDomain::kObjects};
  std::uint64_t generation{0};
  std::vector<InspectorRecord> records;

  bool operator==(const InspectorSnapshot&) const = default;
};

enum class InspectorSortKey : std::uint8_t {
  kIdentity,
  kGeneration,
  kType,
  kName,
  kCount,
  kBytes,
  kDuration,
};

enum class InspectorSortDirection : std::uint8_t {
  kAscending,
  kDescending,
};

struct InspectorSort {
  InspectorSortKey key{InspectorSortKey::kIdentity};
  InspectorSortDirection direction{InspectorSortDirection::kAscending};

  bool operator==(const InspectorSort&) const = default;
};

struct InspectorFilter {
  std::string query;
  std::optional<InspectorDomain> domain;
  bool include_stale{false};

  bool operator==(const InspectorFilter&) const = default;
};

struct InspectorStorageLimits {
  std::size_t max_records{10'000};
  std::size_t max_stale_records{1'000};
  std::size_t max_record_bytes{1U << 20U};

  bool operator==(const InspectorStorageLimits&) const = default;
};

enum class InspectorPublishStatus : std::uint8_t {
  kAccepted,
  kUnavailable,
  kStaleGeneration,
  kInvalidSnapshot,
};

struct InspectorPublishResult {
  InspectorPublishStatus status{InspectorPublishStatus::kAccepted};
  std::size_t accepted_records{0};
  std::size_t dropped_records{0};
  std::size_t evicted_records{0};

  [[nodiscard]] bool accepted() const noexcept {
    return status == InspectorPublishStatus::kAccepted;
  }

  bool operator==(const InspectorPublishResult&) const = default;
};

using InspectorSnapshotProvider =
    std::function<std::optional<InspectorSnapshot>()>;

class InspectorUiModel {
 public:
  explicit InspectorUiModel(InspectorStorageLimits limits = {});

  void SetProvider(InspectorDomain domain, InspectorSnapshotProvider provider);
  [[nodiscard]] InspectorPublishResult Refresh(InspectorDomain domain);
  [[nodiscard]] InspectorPublishResult Publish(InspectorSnapshot snapshot);

  void SetFilter(InspectorFilter filter);
  void SetSort(InspectorSort sort) noexcept;
  [[nodiscard]] const InspectorFilter& filter() const noexcept { return filter_; }
  [[nodiscard]] InspectorSort sort() const noexcept { return sort_; }

  [[nodiscard]] std::vector<InspectorRecord> Records() const;
  [[nodiscard]] std::optional<InspectorRecord> Find(
      InspectorRecordIdentity identity) const;
  [[nodiscard]] bool Select(InspectorRecordIdentity identity);
  void ClearSelection() noexcept;
  [[nodiscard]] std::optional<InspectorRecordIdentity> selection() const noexcept {
    return selection_;
  }
  [[nodiscard]] std::optional<InspectorRecord> Selected() const;

  [[nodiscard]] std::uint64_t generation(InspectorDomain domain) const noexcept;
  [[nodiscard]] std::size_t size() const noexcept { return records_.size(); }
  [[nodiscard]] const InspectorStorageLimits& limits() const noexcept {
    return limits_;
  }

 private:
  struct StoredRecord {
    InspectorRecord record;
    std::uint64_t ordinal{0};
  };

  [[nodiscard]] std::size_t EnforceLimits();

  InspectorStorageLimits limits_;
  std::vector<InspectorSnapshotProvider> providers_;
  std::vector<std::uint64_t> generations_;
  std::vector<StoredRecord> records_;
  InspectorFilter filter_;
  InspectorSort sort_;
  std::optional<InspectorRecordIdentity> selection_;
  std::uint64_t next_ordinal_{1};
};

[[nodiscard]] std::string FormatInspectorBytes(std::uint64_t bytes);
[[nodiscard]] std::string FormatInspectorDuration(
    std::uint64_t nanoseconds);
[[nodiscard]] std::string FormatInspectorCount(std::uint64_t count);

}

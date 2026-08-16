#pragma once

#include "openwow/render/resources/render_resource_identity.h"
#include "openwow/render/resources/render_resource_snapshot.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace openwow::render {
class RenderSubmitTrace;
}

namespace openwow::debug {

enum class BatchVisualizationMode : std::uint8_t {
  kLiveWireframes,
  kLiveNoWireframes,
  kLiveWireframesOnMarker,
  kMarkerWireframes,
  kMarkerNoWireframes,
  kMarkerWireframesOnSelection,
  kSelectedBatchOnly,
  kMarkerToSelectionRange,
};

[[nodiscard]] bool BatchVisualizationModeUsesMarker(
    BatchVisualizationMode mode) noexcept;
[[nodiscard]] bool BatchVisualizationModeIsLive(
    BatchVisualizationMode mode) noexcept;

enum class CaptureMode : std::uint8_t { kSingleFrame, kContinuous };
enum class CaptureTransition : std::uint8_t { kStarted, kStopped };
enum class BatchMarker : std::uint8_t { kNone, kBegin, kEnd };

struct CaptureIdentity {
  std::uint64_t value{0};
  std::uint64_t generation{0};
  bool operator==(const CaptureIdentity&) const = default;
};

struct CapturedDrawHandle {
  CaptureIdentity capture;
  std::uint64_t draw_id{0};
  bool operator==(const CapturedDrawHandle&) const = default;
};

struct CapturedResourceHandle {
  CaptureIdentity capture;
  render::RenderResourceKey resource;
  bool operator==(const CapturedResourceHandle&) const = default;
};

struct CapturedSourceLink {
  std::string path;
  std::uint32_t line{0};
  std::uint32_t column{0};
  bool operator==(const CapturedSourceLink&) const = default;
};

enum class CapturedShaderStage : std::uint8_t { kVertex, kPixel };

struct CapturedShaderLink {
  CapturedShaderStage stage{CapturedShaderStage::kVertex};
  render::RenderResourceKey resource;
  std::optional<CapturedSourceLink> source;
  bool operator==(const CapturedShaderLink&) const = default;
};

struct CapturedPrimitiveReference {
  std::uint32_t topology_code{0x00000501U};
  std::optional<render::RenderResourceKey> vertex_format;
  std::optional<render::RenderResourceKey> vertex_buffer;
  std::optional<render::RenderResourceKey> index_buffer;
  std::uint32_t first_vertex{0};
  std::uint32_t vertex_count{0};
  std::uint32_t first_index{0};
  std::uint32_t index_count{0};
  bool operator==(const CapturedPrimitiveReference&) const = default;
};

struct CapturedPrimitiveTopology {
  std::uint32_t code{0};
  std::uint32_t leading_elements{0};
  std::uint32_t elements_per_primitive{1};
  bool operator==(const CapturedPrimitiveTopology&) const = default;
};

struct CapturedPrimitiveSelection {
  std::array<std::optional<std::uint32_t>, 3> element_indices{};
  [[nodiscard]] std::size_t size() const noexcept;
  bool operator==(const CapturedPrimitiveSelection&) const = default;
};

[[nodiscard]] std::optional<CapturedPrimitiveTopology>
FindCapturedPrimitiveTopology(std::uint32_t code) noexcept;
[[nodiscard]] std::uint32_t CapturedPrimitiveCount(
    std::uint32_t code, std::uint32_t element_count) noexcept;
[[nodiscard]] CapturedPrimitiveSelection SelectCapturedPrimitiveElements(
    std::uint32_t code, std::uint32_t primitive_row) noexcept;

enum class CapturedVertexComponentType : std::uint32_t {
  kUnsignedByte = 0x1401,
  kSignedShort = 0x1402,
  kUnsignedShort = 0x1403,
  kFloat = 0x1406,
};

enum class CapturedIndexComponentType : std::uint32_t {
  kUnsignedShort = 0x1403,
  kUnsignedInt = 0x1405,
};

struct CapturedVertexAttributeFormat {
  CapturedVertexComponentType component_type{CapturedVertexComponentType::kFloat};
  std::uint32_t component_count{0};
  bool normalized{false};
};

[[nodiscard]] std::optional<std::vector<double>> DecodeCapturedVertexAttribute(
    std::span<const std::uint8_t> bytes, std::size_t byte_offset,
    CapturedVertexAttributeFormat format);
[[nodiscard]] std::optional<CapturedPrimitiveSelection>
ResolveCapturedVertexSelection(
    const CapturedPrimitiveSelection& element_selection,
    std::uint32_t first_vertex, std::uint32_t first_index,
    std::span<const std::uint8_t> index_bytes,
    std::optional<CapturedIndexComponentType> index_type) noexcept;

using RenderStateValue =
    std::variant<bool, std::int64_t, std::uint64_t, double, std::string,
                 std::vector<std::int64_t>, std::vector<std::uint64_t>,
                 std::vector<double>>;

enum class RenderStateComparison : std::uint8_t {
  kNumeric,
  kBitwiseFloatingPoint,
};

struct RenderStateEntry {
  std::string path;
  RenderStateValue value;
  bool editable{false};
  RenderStateComparison comparison{RenderStateComparison::kNumeric};
  bool operator==(const RenderStateEntry&) const = default;
};

class RenderStateSnapshot {
 public:
  void Set(std::string path, RenderStateValue value, bool editable = false,
           RenderStateComparison comparison = RenderStateComparison::kNumeric);
  [[nodiscard]] std::optional<RenderStateEntry> Find(
      std::string_view path) const;
  [[nodiscard]] const std::vector<RenderStateEntry>& entries() const noexcept {
    return entries_;
  }
  [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
  bool operator==(const RenderStateSnapshot&) const = default;

 private:
  std::vector<RenderStateEntry> entries_;
};

[[nodiscard]] bool RenderStateValuesEqual(
    const RenderStateValue& lhs, const RenderStateValue& rhs,
    RenderStateComparison comparison = RenderStateComparison::kNumeric) noexcept;

struct RenderStateDifference {
  std::string path;
  std::optional<RenderStateValue> current;
  std::optional<RenderStateValue> default_value;
  std::optional<RenderStateValue> previous;
  std::optional<RenderStateValue> unedited;
  bool editable{false};
  RenderStateComparison comparison{RenderStateComparison::kNumeric};
  bool is_default{false};
  bool changed_from_previous{false};
  bool changed_from_unedited{false};
  bool operator==(const RenderStateDifference&) const = default;
};

[[nodiscard]] std::vector<RenderStateDifference> CompareRenderStateSnapshots(
    const RenderStateSnapshot& current, const RenderStateSnapshot& defaults,
    const std::optional<RenderStateSnapshot>& previous = std::nullopt,
    const std::optional<RenderStateSnapshot>& unedited = std::nullopt);

struct CapturedRenderBatch {
  std::uint64_t id{0};
  std::uint32_t frame_index{0};
  std::string label;
  std::string primitive;
  std::string annotation;
  bool enabled{true};
  bool renderable{true};
  bool shader_error{false};
  bool vertex_format_mismatch{false};
  BatchMarker marker{BatchMarker::kNone};
  std::uint64_t gpu_duration_nanoseconds{0};
  std::array<std::optional<render::RenderResourceKey>, 16> resource_slots{};
  std::vector<std::string> searchable_terms;
  std::vector<CapturedSourceLink> source_links;
  std::vector<CapturedShaderLink> shader_links;
  std::optional<CapturedPrimitiveReference> primitive_reference;
  RenderStateSnapshot state;
  std::optional<RenderStateSnapshot> unedited_state;
  bool operator==(const CapturedRenderBatch&) const = default;
};

enum class CapturedRenderEventKind : std::uint8_t {
  kFrameBegin,
  kDraw,
  kMarker,
  kFrameEnd,
};

struct CapturedRenderEvent {
  std::uint64_t sequence{0};
  std::uint32_t frame_index{0};
  CapturedRenderEventKind kind{CapturedRenderEventKind::kDraw};
  std::optional<std::uint64_t> draw_id;
  std::string label;
  bool operator==(const CapturedRenderEvent&) const = default;
};

struct CapturedRenderFrame {
  std::uint32_t frame_index{0};
  RenderStateSnapshot default_state;
  render::RenderResourceSnapshot resources;
  std::vector<CapturedRenderBatch> batches;
  std::vector<CapturedRenderEvent> events;
};

struct RenderCaptureFilter {
  std::string query;
  std::optional<std::uint32_t> frame_index;
  std::optional<render::RenderResourceKey> resource;
  bool renderable_only{false};
  bool errors_only{false};
};

struct RenderCaptureLimits {
  std::size_t max_batches{100'000};
  std::size_t max_resources{100'000};
  std::size_t max_events{200'000};
  std::size_t max_state_entries_per_batch{16'384};
  std::size_t max_search_terms_per_batch{256};
  std::size_t max_links_per_batch{256};
  std::size_t max_string_bytes{1U << 20U};
  std::uint64_t max_capture_bytes{512ULL << 20U};
};

enum class RenderCaptureErrorCode : std::uint8_t {
  kNone,
  kInvalidCapture,
  kStaleHandle,
  kLimitExceeded,
  kArchive,
  kMalformedData,
};

struct RenderCaptureError {
  RenderCaptureErrorCode code{RenderCaptureErrorCode::kNone};
  std::string detail;
  explicit operator bool() const noexcept {
    return code != RenderCaptureErrorCode::kNone;
  }
};

class RenderCaptureInspector {
 public:
  explicit RenderCaptureInspector(RenderCaptureLimits limits = {});

  CaptureTransition ToggleCapture(CaptureMode requested_mode,
                                   bool preserve_existing);
  void StopCapture();
  [[nodiscard]] bool is_capturing() const;
  [[nodiscard]] std::optional<CaptureMode> capture_mode() const;

  [[nodiscard]] bool AppendCapturedBatch(CapturedRenderBatch batch);
  [[nodiscard]] bool AppendCapturedEvent(CapturedRenderEvent event);
  [[nodiscard]] bool SetDefaultState(RenderStateSnapshot default_state);
  [[nodiscard]] bool ReplaceCapturedFrame(CapturedRenderFrame frame);
  void ClearCapturedFrame();

  [[nodiscard]] CapturedRenderFrame frame() const;
  [[nodiscard]] CaptureIdentity identity() const;
  [[nodiscard]] std::uint64_t revision() const;
  [[nodiscard]] RenderCaptureError error() const;
  void ClearError();

  [[nodiscard]] bool Save(const std::filesystem::path& path);
  [[nodiscard]] bool Load(const std::filesystem::path& path);

  void SetVisualizationMode(BatchVisualizationMode mode);
  [[nodiscard]] BatchVisualizationMode visualization_mode() const;

  [[nodiscard]] std::vector<CapturedDrawHandle> Filter(
      const RenderCaptureFilter& filter) const;
  [[nodiscard]] bool Select(CapturedDrawHandle handle);
  void ClearSelection();
  [[nodiscard]] bool SelectLastRenderable();
  [[nodiscard]] bool SelectAdjacent(int direction);
  [[nodiscard]] bool SelectAdjacentUsingResource(
      CapturedResourceHandle resource, int direction);
  [[nodiscard]] std::optional<CapturedDrawHandle> selected() const;
  [[nodiscard]] std::optional<CapturedRenderBatch> SelectedBatch() const;
  [[nodiscard]] std::optional<CapturedRenderBatch> PreviousBatch() const;
  [[nodiscard]] std::optional<CapturedPrimitiveReference> SelectedPrimitive() const;

  [[nodiscard]] bool SetMarker(std::optional<CapturedDrawHandle> handle);
  [[nodiscard]] std::optional<CapturedDrawHandle> marker() const;
  [[nodiscard]] bool ToggleOrRemoveBatch(CapturedDrawHandle handle, bool remove);
  [[nodiscard]] bool SetAnnotation(CapturedDrawHandle handle,
                                   std::string_view text);

  [[nodiscard]] std::vector<RenderStateDifference> SelectedStateDifferences() const;
  [[nodiscard]] bool BeginSelectedStateEdit(std::string_view path);
  [[nodiscard]] bool PreviewSelectedStateEdit(RenderStateValue value);
  [[nodiscard]] bool CommitSelectedStateEdit();
  [[nodiscard]] bool CancelSelectedStateEdit();
  [[nodiscard]] bool is_editing_state() const;
  [[nodiscard]] std::string state_edit_path() const;

  [[nodiscard]] static bool IsRenderable(
      const CapturedRenderBatch& batch) noexcept;
  [[nodiscard]] static bool BatchUsesResource(
      const CapturedRenderBatch& batch,
      render::RenderResourceKey resource) noexcept;
  [[nodiscard]] static std::size_t ActiveResourceSlotCount(
      const CapturedRenderBatch& batch) noexcept;

 private:
  struct StateEditTransaction {
    std::uint64_t draw_id{0};
    std::string path;
    RenderStateValue original_value;
    RenderStateComparison comparison{RenderStateComparison::kNumeric};
  };

  [[nodiscard]] std::optional<std::size_t> FindBatchIndex(
      std::uint64_t draw_id) const noexcept;
  [[nodiscard]] bool IsCurrent(CaptureIdentity identity) const noexcept;
  [[nodiscard]] bool ValidateFrame(const CapturedRenderFrame& frame,
                                   std::string& detail) const;
  [[nodiscard]] std::uint64_t ApproximateBytes(
      const CapturedRenderFrame& frame) const noexcept;
  void SetError(RenderCaptureErrorCode code, std::string detail);
  void ClearFrameUnlocked();
  void AbandonStateEdit() noexcept;

  mutable std::mutex mutex_;
  RenderCaptureLimits limits_;
  CapturedRenderFrame frame_;
  CaptureIdentity identity_{1, 1};
  std::optional<CaptureMode> capture_mode_;
  BatchVisualizationMode visualization_mode_{
      BatchVisualizationMode::kLiveWireframes};
  std::optional<std::uint64_t> selected_draw_id_;
  std::optional<std::uint64_t> marker_draw_id_;
  std::optional<StateEditTransaction> state_edit_;
  RenderCaptureError error_;
  std::uint64_t revision_{0};
};

[[nodiscard]] CapturedRenderFrame BuildCapturedRenderFrame(
    const render::RenderSubmitTrace& trace,
    std::optional<std::uint32_t> frame_index = std::nullopt);

}

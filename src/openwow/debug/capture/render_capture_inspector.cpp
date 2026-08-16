#include "openwow/debug/capture/render_capture_inspector.h"

#include "openwow/debug/capture/frame_replay_archive.h"
#include "openwow/render/diagnostics/render_submit_trace.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <limits>
#include <set>
#include <type_traits>
#include <utility>

namespace openwow::debug {
namespace {

constexpr std::array<CapturedPrimitiveTopology, 14> kTopologies{{
    {0x00000501U, 0, 1}, {0x00000000U, 0, 1},
    {0x00000001U, 0, 2}, {0x00000002U, 1, 1},
    {0x00000003U, 1, 1}, {0x00000004U, 0, 3},
    {0x00000005U, 2, 1}, {0x00000006U, 2, 1},
    {0x00001500U, 0, 1}, {0x00001503U, 0, 1},
    {0x00000C01U, 0, 1}, {0x434D5442U, 0, 1},
    {0x434D5445U, 0, 1}, {0x00000500U, 0, 1},
}};

constexpr std::array<std::uint8_t, 8> kArchiveMagic{
    'O', 'W', 'C', 'A', 'P', '0', '0', '1'};
constexpr std::uint32_t kArchiveVersion = 1;
std::atomic<std::uint64_t> gNextCaptureIdentity{1};

bool CheckedAdd(const std::uint32_t lhs, const std::uint32_t rhs,
                std::uint32_t& result) noexcept {
  if (lhs > std::numeric_limits<std::uint32_t>::max() - rhs) return false;
  result = lhs + rhs;
  return true;
}

std::uint16_t ReadU16(const std::uint8_t* bytes) noexcept {
  return static_cast<std::uint16_t>(bytes[0]) |
         static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8U);
}

std::uint32_t ReadU32(const std::uint8_t* bytes) noexcept {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[2]) << 16U) |
         (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

char AsciiLower(const char value) noexcept {
  return value >= 'A' && value <= 'Z'
             ? static_cast<char>(value + ('a' - 'A'))
             : value;
}

std::string Lower(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(), AsciiLower);
  return result;
}

bool ContainsFolded(const std::string_view value,
                    const std::string_view query) {
  return query.empty() || Lower(value).find(query) != std::string::npos;
}

template <typename Value>
bool NumericEqual(const Value& lhs, const Value& rhs) noexcept {
  return lhs == rhs;
}

bool NumericEqual(const std::vector<double>& lhs,
                  const std::vector<double>& rhs) noexcept {
  if (lhs.size() != rhs.size()) return false;
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (lhs[i] != rhs[i]) return false;
  }
  return true;
}

template <typename Value>
bool BitwiseEqual(const Value& lhs, const Value& rhs) noexcept {
  return NumericEqual(lhs, rhs);
}

bool BitwiseEqual(const double lhs, const double rhs) noexcept {
  return std::bit_cast<std::uint64_t>(lhs) == std::bit_cast<std::uint64_t>(rhs);
}

bool BitwiseEqual(const std::vector<double>& lhs,
                  const std::vector<double>& rhs) noexcept {
  if (lhs.size() != rhs.size()) return false;
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (!BitwiseEqual(lhs[i], rhs[i])) return false;
  }
  return true;
}

bool OptionalEqual(const std::optional<RenderStateValue>& lhs,
                   const std::optional<RenderStateValue>& rhs,
                   const RenderStateComparison comparison) noexcept {
  return lhs.has_value() == rhs.has_value() &&
         (!lhs || RenderStateValuesEqual(*lhs, *rhs, comparison));
}

std::optional<RenderStateValue> ValueOf(
    const std::optional<RenderStateEntry>& entry) {
  return entry ? std::optional<RenderStateValue>(entry->value) : std::nullopt;
}

RenderStateComparison ComparisonOf(
    const std::optional<RenderStateEntry>& current,
    const std::optional<RenderStateEntry>& defaults,
    const std::optional<RenderStateEntry>& previous,
    const std::optional<RenderStateEntry>& unedited) noexcept {
  if (current) return current->comparison;
  if (defaults) return defaults->comparison;
  if (previous) return previous->comparison;
  return unedited ? unedited->comparison : RenderStateComparison::kNumeric;
}

std::uint64_t StateValueBytes(const RenderStateValue& value) noexcept {
  return std::visit(
      [](const auto& stored) -> std::uint64_t {
        using Value = std::remove_cvref_t<decltype(stored)>;
        if constexpr (std::is_same_v<Value, std::string>) {
          return stored.size();
        } else if constexpr (requires { typename Value::value_type; }) {
          return stored.size() * sizeof(typename Value::value_type);
        } else {
          return sizeof(stored);
        }
      },
      value);
}

bool StateFits(const RenderStateSnapshot& state,
               const RenderCaptureLimits& limits) noexcept {
  if (state.size() > limits.max_state_entries_per_batch) return false;
  for (const auto& entry : state.entries()) {
    if (entry.path.size() > limits.max_string_bytes ||
        StateValueBytes(entry.value) > limits.max_capture_bytes ||
        (std::holds_alternative<std::string>(entry.value) &&
         std::get<std::string>(entry.value).size() > limits.max_string_bytes)) {
      return false;
    }
  }
  return true;
}

void AppendTraceState(RenderStateSnapshot& state,
                      const render::RenderSubmitTraceEvent& event) {
  state.Set("submit.frame", static_cast<std::uint64_t>(event.frame));
  state.Set("submit.view", static_cast<std::uint64_t>(event.view));
  state.Set("submit.skinProfile", static_cast<std::uint64_t>(event.skin_profile));
  state.Set("submit.instanceId", static_cast<std::uint64_t>(event.instance_id));
  state.Set("submit.stateMask", static_cast<std::uint64_t>(event.state_mask));
  state.Set("submit.sortKey", static_cast<std::int64_t>(event.sort_key));
  state.Set("submit.metadata.pass", event.pass);
  state.Set("submit.metadata.pipeline", event.pipeline);
  state.Set("submit.metadata.skinningMode", event.skinning_mode);
  state.Set("submit.metadata.status", event.status);
  state.Set("submit.metadata.reason", event.reason);
  state.Set("submit.metadata.detail", event.detail);
  state.Set("submit.metadata.mesh", event.mesh);
  state.Set("submit.metadata.material", event.material);
}

class Writer {
 public:
  void U8(const std::uint8_t value) { bytes_.push_back(value); }
  void U32(const std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
      U8(static_cast<std::uint8_t>(value >> shift));
    }
  }
  void U64(const std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
      U8(static_cast<std::uint8_t>(value >> shift));
    }
  }
  void Bool(const bool value) { U8(value ? 1 : 0); }
  void String(const std::string_view value) {
    U64(value.size());
    bytes_.insert(bytes_.end(), value.begin(), value.end());
  }
  void Key(const render::RenderResourceKey key) {
    U8(static_cast<std::uint8_t>(key.kind));
    U64(key.id);
  }
  void OptionalKey(const std::optional<render::RenderResourceKey>& key) {
    Bool(key.has_value());
    if (key) Key(*key);
  }
  [[nodiscard]] const std::vector<std::uint8_t>& bytes() const { return bytes_; }

 private:
  std::vector<std::uint8_t> bytes_;
};

class Reader {
 public:
  Reader(const std::span<const std::uint8_t> bytes,
         const RenderCaptureLimits& limits)
      : bytes_(bytes), limits_(limits) {}

  bool U8(std::uint8_t& value) {
    if (position_ == bytes_.size()) return false;
    value = bytes_[position_++];
    return true;
  }
  bool U32(std::uint32_t& value) {
    if (Remaining() < 4) return false;
    value = ReadU32(bytes_.data() + position_);
    position_ += 4;
    return true;
  }
  bool U64(std::uint64_t& value) {
    if (Remaining() < 8) return false;
    value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8) {
      value |= static_cast<std::uint64_t>(bytes_[position_++]) << shift;
    }
    return true;
  }
  bool Bool(bool& value) {
    std::uint8_t encoded = 0;
    if (!U8(encoded) || encoded > 1) return false;
    value = encoded != 0;
    return true;
  }
  bool String(std::string& value) {
    std::uint64_t size = 0;
    if (!U64(size) || size > limits_.max_string_bytes || size > Remaining()) {
      return false;
    }
    value.assign(reinterpret_cast<const char*>(bytes_.data() + position_),
                 static_cast<std::size_t>(size));
    position_ += static_cast<std::size_t>(size);
    return true;
  }
  bool Key(render::RenderResourceKey& key) {
    std::uint8_t kind = 0;
    if (!U8(kind) || kind > static_cast<std::uint8_t>(
                              render::RenderResourceKind::VertexFormat) ||
        !U64(key.id)) {
      return false;
    }
    key.kind = static_cast<render::RenderResourceKind>(kind);
    return true;
  }
  bool OptionalKey(std::optional<render::RenderResourceKey>& key) {
    bool present = false;
    if (!Bool(present)) return false;
    if (!present) {
      key.reset();
      return true;
    }
    render::RenderResourceKey value;
    if (!Key(value)) return false;
    key = value;
    return true;
  }
  [[nodiscard]] bool done() const noexcept { return position_ == bytes_.size(); }

 private:
  [[nodiscard]] std::size_t Remaining() const noexcept {
    return bytes_.size() - position_;
  }
  std::span<const std::uint8_t> bytes_;
  const RenderCaptureLimits& limits_;
  std::size_t position_{0};
};

void WriteStateValue(Writer& writer, const RenderStateValue& value) {
  writer.U8(static_cast<std::uint8_t>(value.index()));
  std::visit(
      [&writer](const auto& stored) {
        using Value = std::remove_cvref_t<decltype(stored)>;
        if constexpr (std::is_same_v<Value, bool>) {
          writer.Bool(stored);
        } else if constexpr (std::is_same_v<Value, std::int64_t> ||
                             std::is_same_v<Value, std::uint64_t>) {
          writer.U64(static_cast<std::uint64_t>(stored));
        } else if constexpr (std::is_same_v<Value, double>) {
          writer.U64(std::bit_cast<std::uint64_t>(stored));
        } else if constexpr (std::is_same_v<Value, std::string>) {
          writer.String(stored);
        } else {
          writer.U64(stored.size());
          for (const auto element : stored) {
            if constexpr (std::is_same_v<typename Value::value_type, double>) {
              writer.U64(std::bit_cast<std::uint64_t>(element));
            } else {
              writer.U64(static_cast<std::uint64_t>(element));
            }
          }
        }
      },
      value);
}

bool ReadStateValue(Reader& reader, RenderStateValue& value,
                    const RenderCaptureLimits& limits) {
  std::uint8_t type = 0;
  if (!reader.U8(type)) return false;
  bool boolean = false;
  std::uint64_t scalar = 0;
  std::string string;
  if (type == 0) {
    if (!reader.Bool(boolean)) return false;
    value = boolean;
    return true;
  }
  if (type >= 1 && type <= 3) {
    if (!reader.U64(scalar)) return false;
    if (type == 1) value = static_cast<std::int64_t>(scalar);
    if (type == 2) value = scalar;
    if (type == 3) value = std::bit_cast<double>(scalar);
    return true;
  }
  if (type == 4) {
    if (!reader.String(string)) return false;
    value = std::move(string);
    return true;
  }
  if (type < 5 || type > 7 || !reader.U64(scalar) ||
      scalar > limits.max_state_entries_per_batch) {
    return false;
  }
  if (type == 5) {
    std::vector<std::int64_t> values;
    values.reserve(static_cast<std::size_t>(scalar));
    for (std::uint64_t i = 0, encoded = 0; i < scalar; ++i) {
      if (!reader.U64(encoded)) return false;
      values.push_back(static_cast<std::int64_t>(encoded));
    }
    value = std::move(values);
  } else if (type == 6) {
    std::vector<std::uint64_t> values;
    values.reserve(static_cast<std::size_t>(scalar));
    for (std::uint64_t i = 0, encoded = 0; i < scalar; ++i) {
      if (!reader.U64(encoded)) return false;
      values.push_back(encoded);
    }
    value = std::move(values);
  } else {
    std::vector<double> values;
    values.reserve(static_cast<std::size_t>(scalar));
    for (std::uint64_t i = 0, encoded = 0; i < scalar; ++i) {
      if (!reader.U64(encoded)) return false;
      values.push_back(std::bit_cast<double>(encoded));
    }
    value = std::move(values);
  }
  return true;
}

void WriteState(Writer& writer, const RenderStateSnapshot& state) {
  writer.U64(state.size());
  for (const auto& entry : state.entries()) {
    writer.String(entry.path);
    WriteStateValue(writer, entry.value);
    writer.Bool(entry.editable);
    writer.U8(static_cast<std::uint8_t>(entry.comparison));
  }
}

bool ReadState(Reader& reader, RenderStateSnapshot& state,
               const RenderCaptureLimits& limits) {
  std::uint64_t count = 0;
  if (!reader.U64(count) || count > limits.max_state_entries_per_batch) {
    return false;
  }
  for (std::uint64_t i = 0; i < count; ++i) {
    std::string path;
    RenderStateValue value;
    bool editable = false;
    std::uint8_t comparison = 0;
    if (!reader.String(path) || !ReadStateValue(reader, value, limits) ||
        !reader.Bool(editable) || !reader.U8(comparison) || comparison > 1) {
      return false;
    }
    state.Set(std::move(path), std::move(value), editable,
              static_cast<RenderStateComparison>(comparison));
  }
  return true;
}

void WriteSource(Writer& writer, const CapturedSourceLink& source) {
  writer.String(source.path);
  writer.U32(source.line);
  writer.U32(source.column);
}

bool ReadSource(Reader& reader, CapturedSourceLink& source) {
  return reader.String(source.path) && reader.U32(source.line) &&
         reader.U32(source.column);
}

void WritePrimitive(Writer& writer, const CapturedPrimitiveReference& primitive) {
  writer.U32(primitive.topology_code);
  writer.OptionalKey(primitive.vertex_format);
  writer.OptionalKey(primitive.vertex_buffer);
  writer.OptionalKey(primitive.index_buffer);
  writer.U32(primitive.first_vertex);
  writer.U32(primitive.vertex_count);
  writer.U32(primitive.first_index);
  writer.U32(primitive.index_count);
}

bool ReadPrimitive(Reader& reader, CapturedPrimitiveReference& primitive) {
  return reader.U32(primitive.topology_code) &&
         reader.OptionalKey(primitive.vertex_format) &&
         reader.OptionalKey(primitive.vertex_buffer) &&
         reader.OptionalKey(primitive.index_buffer) &&
         reader.U32(primitive.first_vertex) && reader.U32(primitive.vertex_count) &&
         reader.U32(primitive.first_index) && reader.U32(primitive.index_count);
}

void WriteBatch(Writer& writer, const CapturedRenderBatch& batch) {
  writer.U64(batch.id);
  writer.U32(batch.frame_index);
  writer.String(batch.label);
  writer.String(batch.primitive);
  writer.String(batch.annotation);
  writer.Bool(batch.enabled);
  writer.Bool(batch.renderable);
  writer.Bool(batch.shader_error);
  writer.Bool(batch.vertex_format_mismatch);
  writer.U8(static_cast<std::uint8_t>(batch.marker));
  writer.U64(batch.gpu_duration_nanoseconds);
  for (const auto& slot : batch.resource_slots) writer.OptionalKey(slot);
  writer.U64(batch.searchable_terms.size());
  for (const auto& term : batch.searchable_terms) writer.String(term);
  writer.U64(batch.source_links.size());
  for (const auto& source : batch.source_links) WriteSource(writer, source);
  writer.U64(batch.shader_links.size());
  for (const auto& shader : batch.shader_links) {
    writer.U8(static_cast<std::uint8_t>(shader.stage));
    writer.Key(shader.resource);
    writer.Bool(shader.source.has_value());
    if (shader.source) WriteSource(writer, *shader.source);
  }
  writer.Bool(batch.primitive_reference.has_value());
  if (batch.primitive_reference) WritePrimitive(writer, *batch.primitive_reference);
  WriteState(writer, batch.state);
  writer.Bool(batch.unedited_state.has_value());
  if (batch.unedited_state) WriteState(writer, *batch.unedited_state);
}

bool ReadBatch(Reader& reader, CapturedRenderBatch& batch,
               const RenderCaptureLimits& limits) {
  std::uint8_t marker = 0;
  if (!reader.U64(batch.id) || !reader.U32(batch.frame_index) ||
      !reader.String(batch.label) || !reader.String(batch.primitive) ||
      !reader.String(batch.annotation) || !reader.Bool(batch.enabled) ||
      !reader.Bool(batch.renderable) || !reader.Bool(batch.shader_error) ||
      !reader.Bool(batch.vertex_format_mismatch) || !reader.U8(marker) ||
      marker > static_cast<std::uint8_t>(BatchMarker::kEnd) ||
      !reader.U64(batch.gpu_duration_nanoseconds)) {
    return false;
  }
  batch.marker = static_cast<BatchMarker>(marker);
  for (auto& slot : batch.resource_slots) {
    if (!reader.OptionalKey(slot)) return false;
  }
  std::uint64_t count = 0;
  if (!reader.U64(count) || count > limits.max_search_terms_per_batch) return false;
  batch.searchable_terms.resize(static_cast<std::size_t>(count));
  for (auto& term : batch.searchable_terms) {
    if (!reader.String(term)) return false;
  }
  if (!reader.U64(count) || count > limits.max_links_per_batch) return false;
  batch.source_links.resize(static_cast<std::size_t>(count));
  for (auto& source : batch.source_links) {
    if (!ReadSource(reader, source)) return false;
  }
  if (!reader.U64(count) || count > limits.max_links_per_batch) return false;
  batch.shader_links.resize(static_cast<std::size_t>(count));
  for (auto& shader : batch.shader_links) {
    std::uint8_t stage = 0;
    bool has_source = false;
    if (!reader.U8(stage) || stage > 1 || !reader.Key(shader.resource) ||
        !reader.Bool(has_source)) {
      return false;
    }
    shader.stage = static_cast<CapturedShaderStage>(stage);
    if (has_source) {
      CapturedSourceLink source;
      if (!ReadSource(reader, source)) return false;
      shader.source = std::move(source);
    }
  }
  bool present = false;
  if (!reader.Bool(present)) return false;
  if (present) {
    CapturedPrimitiveReference primitive;
    if (!ReadPrimitive(reader, primitive)) return false;
    batch.primitive_reference = std::move(primitive);
  }
  if (!ReadState(reader, batch.state, limits) || !reader.Bool(present)) return false;
  if (present) {
    RenderStateSnapshot unedited;
    if (!ReadState(reader, unedited, limits)) return false;
    batch.unedited_state = std::move(unedited);
  }
  return reader.done();
}

FrameReplayResourceKind ArchiveKind(const render::RenderResourceKind kind) {
  switch (kind) {
    case render::RenderResourceKind::VertexFormat:
      return FrameReplayResourceKind::kVertexFormat;
    case render::RenderResourceKind::VertexShader:
    case render::RenderResourceKind::PixelShader:
      return FrameReplayResourceKind::kShader;
    case render::RenderResourceKind::VertexBuffer:
    case render::RenderResourceKind::IndexBuffer:
      return FrameReplayResourceKind::kBuffer;
    case render::RenderResourceKind::Texture:
      return FrameReplayResourceKind::kTexture;
  }
  return FrameReplayResourceKind::kBuffer;
}

void WriteResource(Writer& writer, const render::RenderResourceRecord& resource) {
  const auto& descriptor = resource.descriptor;
  writer.Key(descriptor.key);
  writer.U64(descriptor.byte_size);
  writer.U32(descriptor.width);
  writer.U32(descriptor.height);
  writer.U32(descriptor.depth);
  writer.U32(descriptor.mip_count);
  writer.String(descriptor.format);
  writer.String(descriptor.name);
  writer.Bool(descriptor.render_target);
  writer.Bool(descriptor.dynamic);
  writer.Bool(descriptor.utility);
  writer.U64(resource.content.bytes.size());
  for (const auto byte : resource.content.bytes) writer.U8(byte);
  writer.U32(resource.content.element_stride);
  writer.Bool(resource.content.index_component_type.has_value());
  if (resource.content.index_component_type) {
    writer.U32(static_cast<std::uint32_t>(*resource.content.index_component_type));
  }
  writer.U64(resource.content.vertex_attributes.size());
  for (const auto& attribute : resource.content.vertex_attributes) {
    writer.U32(attribute.stream);
    writer.U32(attribute.semantic);
    writer.U32(attribute.byte_offset);
    writer.U32(attribute.byte_stride);
    writer.U32(static_cast<std::uint32_t>(attribute.component_type));
    writer.U32(attribute.component_count);
    writer.Bool(attribute.normalized);
  }
  writer.U64(resource.batch_indices.size());
  for (const auto batch : resource.batch_indices) writer.U32(batch);
}

bool ReadResource(Reader& reader, render::RenderResourceDescriptor& descriptor,
                  render::RenderResourceContent& content,
                  std::vector<std::uint32_t>& uses,
                  const RenderCaptureLimits& limits) {
  if (!reader.Key(descriptor.key) || !reader.U64(descriptor.byte_size) ||
      !reader.U32(descriptor.width) || !reader.U32(descriptor.height) ||
      !reader.U32(descriptor.depth) || !reader.U32(descriptor.mip_count) ||
      !reader.String(descriptor.format) || !reader.String(descriptor.name) ||
      !reader.Bool(descriptor.render_target) || !reader.Bool(descriptor.dynamic) ||
      !reader.Bool(descriptor.utility)) {
    return false;
  }
  std::uint64_t count = 0;
  if (!reader.U64(count) || count > limits.max_capture_bytes) return false;
  content.bytes.resize(static_cast<std::size_t>(count));
  for (auto& byte : content.bytes) {
    if (!reader.U8(byte)) return false;
  }
  bool present = false;
  std::uint32_t type = 0;
  if (!reader.U32(content.element_stride) || !reader.Bool(present)) return false;
  if (present) {
    if (!reader.U32(type) ||
        (type != static_cast<std::uint32_t>(render::RenderCapturedComponentType::UnsignedShort) &&
         type != static_cast<std::uint32_t>(render::RenderCapturedComponentType::UnsignedInt))) {
      return false;
    }
    content.index_component_type =
        static_cast<render::RenderCapturedComponentType>(type);
  }
  if (!reader.U64(count) || count > limits.max_links_per_batch) return false;
  content.vertex_attributes.resize(static_cast<std::size_t>(count));
  for (auto& attribute : content.vertex_attributes) {
    if (!reader.U32(attribute.stream) || !reader.U32(attribute.semantic) ||
        !reader.U32(attribute.byte_offset) || !reader.U32(attribute.byte_stride) ||
        !reader.U32(type) || !reader.U32(attribute.component_count) ||
        !reader.Bool(attribute.normalized)) {
      return false;
    }
    attribute.component_type = static_cast<render::RenderCapturedComponentType>(type);
  }
  if (!reader.U64(count) || count > limits.max_batches) return false;
  uses.resize(static_cast<std::size_t>(count));
  for (auto& use : uses) {
    if (!reader.U32(use)) return false;
  }
  return reader.done();
}

void WriteManifest(Writer& writer, const CaptureIdentity identity,
                   const CapturedRenderFrame& frame,
                   const std::vector<std::string>& resource_names) {
  for (const auto byte : kArchiveMagic) writer.U8(byte);
  writer.U32(kArchiveVersion);
  writer.U64(identity.value);
  writer.U64(identity.generation);
  writer.U32(frame.frame_index);
  WriteState(writer, frame.default_state);
  writer.U64(frame.events.size());
  for (const auto& event : frame.events) {
    writer.U64(event.sequence);
    writer.U32(event.frame_index);
    writer.U8(static_cast<std::uint8_t>(event.kind));
    writer.Bool(event.draw_id.has_value());
    if (event.draw_id) writer.U64(*event.draw_id);
    writer.String(event.label);
  }
  writer.U64(frame.batches.size());
  writer.U64(resource_names.size());
  for (const auto& name : resource_names) writer.String(name);
}

bool ReadManifest(Reader& reader, CaptureIdentity& identity,
                  CapturedRenderFrame& frame, std::size_t& batch_count,
                  std::vector<std::string>& resource_names,
                  const RenderCaptureLimits& limits) {
  for (const auto expected : kArchiveMagic) {
    std::uint8_t actual = 0;
    if (!reader.U8(actual) || actual != expected) return false;
  }
  std::uint32_t version = 0;
  if (!reader.U32(version) || version != kArchiveVersion ||
      !reader.U64(identity.value) || identity.value == 0 ||
      !reader.U64(identity.generation) ||
      !reader.U32(frame.frame_index) ||
      !ReadState(reader, frame.default_state, limits)) {
    return false;
  }
  std::uint64_t count = 0;
  if (!reader.U64(count) || count > limits.max_events) return false;
  frame.events.resize(static_cast<std::size_t>(count));
  for (auto& event : frame.events) {
    std::uint8_t kind = 0;
    bool has_draw = false;
    if (!reader.U64(event.sequence) || !reader.U32(event.frame_index) ||
        !reader.U8(kind) || kind > static_cast<std::uint8_t>(CapturedRenderEventKind::kFrameEnd) ||
        !reader.Bool(has_draw)) {
      return false;
    }
    event.kind = static_cast<CapturedRenderEventKind>(kind);
    if (has_draw) {
      std::uint64_t draw = 0;
      if (!reader.U64(draw)) return false;
      event.draw_id = draw;
    }
    if (!reader.String(event.label)) return false;
  }
  if (!reader.U64(count) || count > limits.max_batches) return false;
  batch_count = static_cast<std::size_t>(count);
  if (!reader.U64(count) || count > limits.max_resources) return false;
  resource_names.resize(static_cast<std::size_t>(count));
  for (auto& name : resource_names) {
    if (!reader.String(name)) return false;
  }
  return reader.done();
}

}

std::size_t CapturedPrimitiveSelection::size() const noexcept {
  for (std::size_t i = element_indices.size(); i > 0; --i) {
    if (element_indices[i - 1]) return i;
  }
  return 0;
}

std::optional<CapturedPrimitiveTopology> FindCapturedPrimitiveTopology(
    const std::uint32_t code) noexcept {
  const auto found = std::find_if(kTopologies.begin(), kTopologies.end(),
                                  [code](const auto& value) {
                                    return value.code == code;
                                  });
  return found == kTopologies.end()
             ? std::nullopt
             : std::optional<CapturedPrimitiveTopology>(*found);
}

std::uint32_t CapturedPrimitiveCount(const std::uint32_t code,
                                     const std::uint32_t element_count) noexcept {
  const auto topology = FindCapturedPrimitiveTopology(code);
  if (!topology || topology->elements_per_primitive == 0 ||
      element_count < topology->leading_elements) {
    return 0;
  }
  return (element_count - topology->leading_elements) /
         topology->elements_per_primitive;
}

CapturedPrimitiveSelection SelectCapturedPrimitiveElements(
    const std::uint32_t code, const std::uint32_t row) noexcept {
  CapturedPrimitiveSelection selection;
  std::uint32_t first = 0, second = 0, third = 0;
  switch (code) {
    case 0x00000000U:
    case 0x00000500U:
    case 0x00000501U:
    case 0x00000C01U:
    case 0x00001500U:
    case 0x00001503U:
    case 0x434D5442U:
    case 0x434D5445U:
      selection.element_indices[0] = row;
      return selection;
    case 0x00000001U:
      if (row > std::numeric_limits<std::uint32_t>::max() / 2U) return {};
      first = row * 2U;
      if (!CheckedAdd(first, 1, second)) return {};
      selection.element_indices = {first, second, std::nullopt};
      return selection;
    case 0x00000002U:
    case 0x00000003U:
      if (!CheckedAdd(row, 1, second)) return {};
      selection.element_indices = {row, second, std::nullopt};
      return selection;
    case 0x00000004U:
      if (row > std::numeric_limits<std::uint32_t>::max() / 3U) return {};
      first = row * 3U;
      if (!CheckedAdd(first, 1, second) || !CheckedAdd(first, 2, third)) return {};
      selection.element_indices = {first, second, third};
      return selection;
    case 0x00000005U:
      if (!CheckedAdd(row, 1, second) || !CheckedAdd(row, 2, third)) return {};
      selection.element_indices =
          (row & 1U) == 0U
              ? std::array<std::optional<std::uint32_t>, 3>{row, second, third}
              : std::array<std::optional<std::uint32_t>, 3>{row, third, second};
      return selection;
    case 0x00000006U:
      if (!CheckedAdd(row, 1, second) || !CheckedAdd(row, 2, third)) return {};
      selection.element_indices = {0U, second, third};
      return selection;
    default:
      return {};
  }
}

std::optional<std::vector<double>> DecodeCapturedVertexAttribute(
    const std::span<const std::uint8_t> bytes, const std::size_t byte_offset,
    const CapturedVertexAttributeFormat format) {
  if (format.component_count > 4) return std::nullopt;
  std::size_t width = 0;
  switch (format.component_type) {
    case CapturedVertexComponentType::kUnsignedByte: width = 1; break;
    case CapturedVertexComponentType::kSignedShort:
    case CapturedVertexComponentType::kUnsignedShort: width = 2; break;
    case CapturedVertexComponentType::kFloat: width = 4; break;
    default: return std::nullopt;
  }
  if (byte_offset > bytes.size() ||
      format.component_count > (bytes.size() - byte_offset) / width) {
    return std::nullopt;
  }
  std::vector<double> result;
  result.reserve(format.component_count);
  const auto* cursor = bytes.data() + byte_offset;
  for (std::uint32_t i = 0; i < format.component_count; ++i, cursor += width) {
    switch (format.component_type) {
      case CapturedVertexComponentType::kUnsignedByte:
        result.push_back(static_cast<double>(*cursor) /
                         (format.normalized ? 255.0 : 1.0));
        break;
      case CapturedVertexComponentType::kSignedShort:
        result.push_back(static_cast<double>(static_cast<std::int16_t>(ReadU16(cursor))) /
                         (format.normalized ? 32768.0 : 1.0));
        break;
      case CapturedVertexComponentType::kUnsignedShort:
        result.push_back(static_cast<double>(ReadU16(cursor)) /
                         (format.normalized ? 65535.0 : 1.0));
        break;
      case CapturedVertexComponentType::kFloat:
        result.push_back(static_cast<double>(std::bit_cast<float>(ReadU32(cursor))));
        break;
      default: return std::nullopt;
    }
  }
  return result;
}

std::optional<CapturedPrimitiveSelection> ResolveCapturedVertexSelection(
    const CapturedPrimitiveSelection& elements, const std::uint32_t first_vertex,
    const std::uint32_t first_index,
    const std::span<const std::uint8_t> index_bytes,
    const std::optional<CapturedIndexComponentType> index_type) noexcept {
  std::size_t width = 0;
  if (index_type) {
    if (*index_type == CapturedIndexComponentType::kUnsignedShort) width = 2;
    else if (*index_type == CapturedIndexComponentType::kUnsignedInt) width = 4;
    else return std::nullopt;
  }
  CapturedPrimitiveSelection result;
  for (std::size_t i = 0; i < elements.element_indices.size(); ++i) {
    if (!elements.element_indices[i]) continue;
    std::uint32_t vertex = *elements.element_indices[i];
    if (index_type) {
      std::uint32_t logical = 0;
      if (!CheckedAdd(first_index, vertex, logical) ||
          logical > std::numeric_limits<std::size_t>::max() / width) {
        return std::nullopt;
      }
      const auto offset = static_cast<std::size_t>(logical) * width;
      if (offset > index_bytes.size() || width > index_bytes.size() - offset) {
        return std::nullopt;
      }
      vertex = width == 2 ? ReadU16(index_bytes.data() + offset)
                          : ReadU32(index_bytes.data() + offset);
    }
    if (!CheckedAdd(vertex, first_vertex, vertex)) return std::nullopt;
    result.element_indices[i] = vertex;
  }
  return result;
}

bool BatchVisualizationModeUsesMarker(const BatchVisualizationMode mode) noexcept {
  return mode == BatchVisualizationMode::kLiveWireframesOnMarker ||
         mode == BatchVisualizationMode::kMarkerWireframes ||
         mode == BatchVisualizationMode::kMarkerNoWireframes ||
         mode == BatchVisualizationMode::kMarkerWireframesOnSelection ||
         mode == BatchVisualizationMode::kMarkerToSelectionRange;
}

bool BatchVisualizationModeIsLive(const BatchVisualizationMode mode) noexcept {
  return mode == BatchVisualizationMode::kLiveWireframes ||
         mode == BatchVisualizationMode::kLiveNoWireframes ||
         mode == BatchVisualizationMode::kLiveWireframesOnMarker;
}

void RenderStateSnapshot::Set(std::string path, RenderStateValue value,
                              const bool editable,
                              const RenderStateComparison comparison) {
  const auto found = std::lower_bound(
      entries_.begin(), entries_.end(), std::string_view(path),
      [](const RenderStateEntry& entry, const std::string_view candidate) {
        return entry.path < candidate;
      });
  RenderStateEntry replacement{std::move(path), std::move(value), editable,
                               comparison};
  if (found != entries_.end() && found->path == replacement.path) {
    *found = std::move(replacement);
  } else {
    entries_.insert(found, std::move(replacement));
  }
}

std::optional<RenderStateEntry> RenderStateSnapshot::Find(
    const std::string_view path) const {
  const auto found = std::lower_bound(
      entries_.begin(), entries_.end(), path,
      [](const RenderStateEntry& entry, const std::string_view candidate) {
        return entry.path < candidate;
      });
  return found != entries_.end() && found->path == path
             ? std::optional<RenderStateEntry>(*found)
             : std::nullopt;
}

bool RenderStateValuesEqual(const RenderStateValue& lhs,
                            const RenderStateValue& rhs,
                            const RenderStateComparison comparison) noexcept {
  if (lhs.index() != rhs.index()) return false;
  return std::visit(
      [comparison](const auto& left, const auto& right) noexcept {
        using Left = std::remove_cvref_t<decltype(left)>;
        using Right = std::remove_cvref_t<decltype(right)>;
        if constexpr (!std::is_same_v<Left, Right>) return false;
        else if (comparison == RenderStateComparison::kBitwiseFloatingPoint)
          return BitwiseEqual(left, right);
        else
          return NumericEqual(left, right);
      },
      lhs, rhs);
}

std::vector<RenderStateDifference> CompareRenderStateSnapshots(
    const RenderStateSnapshot& current, const RenderStateSnapshot& defaults,
    const std::optional<RenderStateSnapshot>& previous,
    const std::optional<RenderStateSnapshot>& unedited) {
  std::set<std::string> paths;
  const auto append = [&paths](const RenderStateSnapshot& state) {
    for (const auto& entry : state.entries()) paths.insert(entry.path);
  };
  append(current);
  append(defaults);
  if (previous) append(*previous);
  if (unedited) append(*unedited);
  std::vector<RenderStateDifference> result;
  result.reserve(paths.size());
  for (const auto& path : paths) {
    const auto now = current.Find(path);
    const auto baseline = defaults.Find(path);
    const auto before = previous ? previous->Find(path) : std::nullopt;
    const auto original = unedited ? unedited->Find(path) : std::nullopt;
    const auto comparison = ComparisonOf(now, baseline, before, original);
    RenderStateDifference difference;
    difference.path = path;
    difference.current = ValueOf(now);
    difference.default_value = ValueOf(baseline);
    difference.previous = ValueOf(before);
    difference.unedited = ValueOf(original);
    difference.editable = now && now->editable;
    difference.comparison = comparison;
    difference.is_default = OptionalEqual(difference.current,
                                          difference.default_value, comparison);
    difference.changed_from_previous =
        previous && !OptionalEqual(difference.current, difference.previous,
                                   comparison);
    difference.changed_from_unedited =
        unedited && !OptionalEqual(difference.current, difference.unedited,
                                   comparison);
    result.push_back(std::move(difference));
  }
  return result;
}

RenderCaptureInspector::RenderCaptureInspector(RenderCaptureLimits limits)
    : limits_(limits) {
  identity_.value = gNextCaptureIdentity.fetch_add(1, std::memory_order_relaxed);
}

CaptureTransition RenderCaptureInspector::ToggleCapture(
    const CaptureMode requested_mode, const bool preserve_existing) {
  std::scoped_lock lock(mutex_);
  AbandonStateEdit();
  if (capture_mode_) {
    capture_mode_.reset();
    return CaptureTransition::kStopped;
  }
  if (!preserve_existing) ClearFrameUnlocked();
  capture_mode_ = requested_mode;
  error_ = {};
  return CaptureTransition::kStarted;
}

void RenderCaptureInspector::StopCapture() {
  std::scoped_lock lock(mutex_);
  capture_mode_.reset();
}

bool RenderCaptureInspector::is_capturing() const {
  std::scoped_lock lock(mutex_);
  return capture_mode_.has_value();
}

std::optional<CaptureMode> RenderCaptureInspector::capture_mode() const {
  std::scoped_lock lock(mutex_);
  return capture_mode_;
}

bool RenderCaptureInspector::AppendCapturedBatch(CapturedRenderBatch batch) {
  std::scoped_lock lock(mutex_);
  if (!capture_mode_) return false;
  if (frame_.batches.size() == limits_.max_batches || FindBatchIndex(batch.id)) {
    SetError(RenderCaptureErrorCode::kLimitExceeded,
             "captured draw exceeds configured limits or duplicates identity");
    return false;
  }
  frame_.batches.push_back(std::move(batch));
  std::string detail;
  if (!ValidateFrame(frame_, detail)) {
    frame_.batches.pop_back();
    SetError(RenderCaptureErrorCode::kLimitExceeded, std::move(detail));
    return false;
  }
  ++revision_;
  error_ = {};
  return true;
}

bool RenderCaptureInspector::AppendCapturedEvent(CapturedRenderEvent event) {
  std::scoped_lock lock(mutex_);
  if (!capture_mode_) return false;
  if (frame_.events.size() == limits_.max_events ||
      event.label.size() > limits_.max_string_bytes ||
      (!frame_.events.empty() &&
       event.sequence <= frame_.events.back().sequence)) {
    SetError(RenderCaptureErrorCode::kLimitExceeded,
             "captured event exceeds limits or is not ordered");
    return false;
  }
  if (event.draw_id && !FindBatchIndex(*event.draw_id)) {
    SetError(RenderCaptureErrorCode::kInvalidCapture,
             "captured event references unknown draw");
    return false;
  }
  frame_.events.push_back(std::move(event));
  std::string detail;
  if (!ValidateFrame(frame_, detail)) {
    frame_.events.pop_back();
    SetError(RenderCaptureErrorCode::kLimitExceeded, std::move(detail));
    return false;
  }
  ++revision_;
  error_ = {};
  return true;
}

bool RenderCaptureInspector::SetDefaultState(RenderStateSnapshot default_state) {
  std::scoped_lock lock(mutex_);
  if (!StateFits(default_state, limits_)) {
    SetError(RenderCaptureErrorCode::kLimitExceeded,
             "default state exceeds configured entry limit");
    return false;
  }
  const auto previous = frame_.default_state;
  frame_.default_state = std::move(default_state);
  if (ApproximateBytes(frame_) > limits_.max_capture_bytes) {
    frame_.default_state = previous;
    SetError(RenderCaptureErrorCode::kLimitExceeded,
             "capture exceeds configured byte limit");
    return false;
  }
  ++revision_;
  error_ = {};
  return true;
}

bool RenderCaptureInspector::ReplaceCapturedFrame(CapturedRenderFrame frame) {
  std::string detail;
  if (!ValidateFrame(frame, detail)) {
    std::scoped_lock lock(mutex_);
    SetError(RenderCaptureErrorCode::kInvalidCapture, std::move(detail));
    return false;
  }
  std::stable_sort(frame.events.begin(), frame.events.end(),
                   [](const auto& lhs, const auto& rhs) {
                     return lhs.sequence < rhs.sequence;
                   });
  std::scoped_lock lock(mutex_);
  frame_ = std::move(frame);
  selected_draw_id_.reset();
  marker_draw_id_.reset();
  AbandonStateEdit();
  ++identity_.generation;
  ++revision_;
  error_ = {};
  return true;
}

void RenderCaptureInspector::ClearCapturedFrame() {
  std::scoped_lock lock(mutex_);
  ClearFrameUnlocked();
}

CapturedRenderFrame RenderCaptureInspector::frame() const {
  std::scoped_lock lock(mutex_);
  return frame_;
}

CaptureIdentity RenderCaptureInspector::identity() const {
  std::scoped_lock lock(mutex_);
  return identity_;
}

std::uint64_t RenderCaptureInspector::revision() const {
  std::scoped_lock lock(mutex_);
  return revision_;
}

RenderCaptureError RenderCaptureInspector::error() const {
  std::scoped_lock lock(mutex_);
  return error_;
}

void RenderCaptureInspector::ClearError() {
  std::scoped_lock lock(mutex_);
  error_ = {};
}

bool RenderCaptureInspector::Save(const std::filesystem::path& path) {
  std::scoped_lock lock(mutex_);
  std::string detail;
  if (!ValidateFrame(frame_, detail)) {
    SetError(RenderCaptureErrorCode::kInvalidCapture, std::move(detail));
    return false;
  }
  FrameReplayArchiveError archive_error;
  FrameReplayArchiveLimits archive_limits;
  archive_limits.max_entries = std::min<std::size_t>(
      65'535, frame_.batches.size() + frame_.resources.size() + 1);
  archive_limits.max_entry_uncompressed_bytes = limits_.max_capture_bytes;
  archive_limits.max_archive_bytes = std::min<std::uint64_t>(
      0xFFFFFFFFULL, limits_.max_capture_bytes);
  auto archive = FrameReplayArchive::Open(path, FrameReplayArchiveMode::kCreate,
                                          &archive_error, archive_limits);
  if (!archive) {
    SetError(RenderCaptureErrorCode::kArchive, archive_error.detail);
    return false;
  }
  for (std::size_t i = 0; i < frame_.batches.size(); ++i) {
    Writer writer;
    WriteBatch(writer, frame_.batches[i]);
    if (!archive->WriteEntry(FrameReplayBatchEntryName(static_cast<std::uint32_t>(i)),
                             writer.bytes(), &archive_error)) {
      SetError(RenderCaptureErrorCode::kArchive, archive_error.detail);
      return false;
    }
  }
  std::vector<std::string> resource_names;
  const auto resources = frame_.resources.Records();
  resource_names.reserve(resources.size());
  for (std::size_t i = 0; i < resources.size(); ++i) {
    const auto name = FrameReplayResourceEntryName(
        ArchiveKind(resources[i].descriptor.key.kind),
        static_cast<std::uint32_t>(i));
    Writer writer;
    WriteResource(writer, resources[i]);
    if (!archive->WriteEntry(name, writer.bytes(), &archive_error)) {
      SetError(RenderCaptureErrorCode::kArchive, archive_error.detail);
      return false;
    }
    resource_names.push_back(name);
  }
  Writer manifest;
  WriteManifest(manifest, identity_, frame_, resource_names);
  if (!archive->WriteEntry(kFrameReplayIdentifierEntry, manifest.bytes(),
                           &archive_error) ||
      !archive->Close(&archive_error)) {
    SetError(RenderCaptureErrorCode::kArchive, archive_error.detail);
    return false;
  }
  error_ = {};
  return true;
}

bool RenderCaptureInspector::Load(const std::filesystem::path& path) {
  RenderCaptureLimits limits;
  {
    std::scoped_lock lock(mutex_);
    limits = limits_;
  }
  const auto fail = [this](const RenderCaptureErrorCode code,
                           std::string detail) {
    std::scoped_lock lock(mutex_);
    SetError(code, std::move(detail));
    return false;
  };
  FrameReplayArchiveError archive_error;
  FrameReplayArchiveLimits archive_limits;
  archive_limits.max_entries = std::min<std::size_t>(
      65'535, limits.max_batches + limits.max_resources + 1);
  archive_limits.max_entry_uncompressed_bytes = limits.max_capture_bytes;
  archive_limits.max_archive_bytes = std::min<std::uint64_t>(
      0xFFFFFFFFULL, limits.max_capture_bytes);
  auto archive = FrameReplayArchive::Open(path, FrameReplayArchiveMode::kRead,
                                          &archive_error, archive_limits);
  if (!archive) return fail(RenderCaptureErrorCode::kArchive, archive_error.detail);
  auto manifest_bytes = archive->ReadEntry(kFrameReplayIdentifierEntry,
                                           &archive_error);
  if (!manifest_bytes) return fail(RenderCaptureErrorCode::kArchive,
                                   archive_error.detail);
  CapturedRenderFrame loaded;
  CaptureIdentity archived_identity;
  std::size_t batch_count = 0;
  std::vector<std::string> resource_names;
  Reader manifest(*manifest_bytes, limits);
  if (!ReadManifest(manifest, archived_identity, loaded, batch_count,
                    resource_names, limits)) {
    return fail(RenderCaptureErrorCode::kMalformedData,
                "invalid render capture manifest");
  }
  loaded.batches.reserve(batch_count);
  for (std::size_t i = 0; i < batch_count; ++i) {
    auto bytes = archive->ReadEntry(
        FrameReplayBatchEntryName(static_cast<std::uint32_t>(i)), &archive_error);
    if (!bytes) return fail(RenderCaptureErrorCode::kArchive, archive_error.detail);
    Reader reader(*bytes, limits);
    CapturedRenderBatch batch;
    if (!ReadBatch(reader, batch, limits)) {
      return fail(RenderCaptureErrorCode::kMalformedData,
                  "invalid captured draw entry");
    }
    loaded.batches.push_back(std::move(batch));
  }
  for (const auto& name : resource_names) {
    auto bytes = archive->ReadEntry(name, &archive_error);
    if (!bytes) return fail(RenderCaptureErrorCode::kArchive, archive_error.detail);
    Reader reader(*bytes, limits);
    render::RenderResourceDescriptor descriptor;
    render::RenderResourceContent content;
    std::vector<std::uint32_t> uses;
    if (!ReadResource(reader, descriptor, content, uses, limits) ||
        loaded.resources.AddResource(descriptor, std::move(content)) !=
            render::RenderResourceSnapshot::AddResult::Added) {
      return fail(RenderCaptureErrorCode::kMalformedData,
                  "invalid captured resource entry");
    }
    for (const auto use : uses) {
      if (!loaded.resources.RecordBatchUse(use, descriptor.key)) {
        return fail(RenderCaptureErrorCode::kMalformedData,
                    "invalid captured resource use");
      }
    }
  }
  std::string detail;
  if (!ValidateFrame(loaded, detail)) {
    return fail(RenderCaptureErrorCode::kInvalidCapture, std::move(detail));
  }
  std::scoped_lock lock(mutex_);
  frame_ = std::move(loaded);
  archived_identity.generation =
      std::max(archived_identity.generation, identity_.generation) + 1;
  identity_ = archived_identity;
  capture_mode_.reset();
  selected_draw_id_.reset();
  marker_draw_id_.reset();
  AbandonStateEdit();
  ++revision_;
  error_ = {};
  return true;
}

void RenderCaptureInspector::SetVisualizationMode(
    const BatchVisualizationMode mode) {
  std::scoped_lock lock(mutex_);
  visualization_mode_ = mode;
}

BatchVisualizationMode RenderCaptureInspector::visualization_mode() const {
  std::scoped_lock lock(mutex_);
  return visualization_mode_;
}

std::vector<CapturedDrawHandle> RenderCaptureInspector::Filter(
    const RenderCaptureFilter& filter) const {
  std::scoped_lock lock(mutex_);
  const auto query = Lower(filter.query);
  std::vector<CapturedDrawHandle> result;
  for (const auto& batch : frame_.batches) {
    if ((filter.frame_index && batch.frame_index != *filter.frame_index) ||
        (filter.resource && !BatchUsesResource(batch, *filter.resource)) ||
        (filter.renderable_only && !IsRenderable(batch)) ||
        (filter.errors_only && !batch.shader_error &&
         !batch.vertex_format_mismatch)) {
      continue;
    }
    bool matches = ContainsFolded(batch.label, query) ||
                   ContainsFolded(batch.primitive, query) ||
                   ContainsFolded(batch.annotation, query);
    for (const auto& term : batch.searchable_terms) {
      matches = matches || ContainsFolded(term, query);
    }
    if (matches) result.push_back({identity_, batch.id});
  }
  return result;
}

bool RenderCaptureInspector::Select(const CapturedDrawHandle handle) {
  std::scoped_lock lock(mutex_);
  if (!IsCurrent(handle.capture) || !FindBatchIndex(handle.draw_id)) {
    SetError(RenderCaptureErrorCode::kStaleHandle, "draw handle is stale");
    return false;
  }
  selected_draw_id_ = handle.draw_id;
  error_ = {};
  return true;
}

void RenderCaptureInspector::ClearSelection() {
  std::scoped_lock lock(mutex_);
  selected_draw_id_.reset();
}

bool RenderCaptureInspector::SelectLastRenderable() {
  std::scoped_lock lock(mutex_);
  for (auto found = frame_.batches.rbegin(); found != frame_.batches.rend(); ++found) {
    if (IsRenderable(*found)) {
      selected_draw_id_ = found->id;
      return true;
    }
  }
  return false;
}

bool RenderCaptureInspector::SelectAdjacent(const int direction) {
  std::scoped_lock lock(mutex_);
  if (direction == 0 || frame_.batches.empty()) return false;
  const bool forward = direction > 0;
  auto index = selected_draw_id_ ? FindBatchIndex(*selected_draw_id_) : std::nullopt;
  std::size_t cursor = index ? *index : (forward ? 0 : frame_.batches.size() - 1);
  if (index) {
    if ((forward && cursor + 1 == frame_.batches.size()) ||
        (!forward && cursor == 0)) return false;
    cursor = forward ? cursor + 1 : cursor - 1;
  }
  while (true) {
    if (IsRenderable(frame_.batches[cursor])) {
      selected_draw_id_ = frame_.batches[cursor].id;
      return true;
    }
    if ((forward && cursor + 1 == frame_.batches.size()) ||
        (!forward && cursor == 0)) return false;
    cursor = forward ? cursor + 1 : cursor - 1;
  }
}

bool RenderCaptureInspector::SelectAdjacentUsingResource(
    const CapturedResourceHandle resource, const int direction) {
  std::scoped_lock lock(mutex_);
  if (!IsCurrent(resource.capture)) {
    SetError(RenderCaptureErrorCode::kStaleHandle, "resource handle is stale");
    return false;
  }
  if (!frame_.resources.Contains(resource.resource) || direction == 0 ||
      frame_.batches.empty()) return false;
  const bool forward = direction > 0;
  auto index = selected_draw_id_ ? FindBatchIndex(*selected_draw_id_) : std::nullopt;
  std::size_t cursor = index ? *index : (forward ? 0 : frame_.batches.size() - 1);
  if (index) {
    if ((forward && cursor + 1 == frame_.batches.size()) ||
        (!forward && cursor == 0)) return false;
    cursor = forward ? cursor + 1 : cursor - 1;
  }
  while (true) {
    if (BatchUsesResource(frame_.batches[cursor], resource.resource)) {
      selected_draw_id_ = frame_.batches[cursor].id;
      return true;
    }
    if ((forward && cursor + 1 == frame_.batches.size()) ||
        (!forward && cursor == 0)) return false;
    cursor = forward ? cursor + 1 : cursor - 1;
  }
}

std::optional<CapturedDrawHandle> RenderCaptureInspector::selected() const {
  std::scoped_lock lock(mutex_);
  return selected_draw_id_ && FindBatchIndex(*selected_draw_id_)
             ? std::optional<CapturedDrawHandle>(
                   CapturedDrawHandle{identity_, *selected_draw_id_})
             : std::nullopt;
}

std::optional<CapturedRenderBatch> RenderCaptureInspector::SelectedBatch() const {
  std::scoped_lock lock(mutex_);
  if (!selected_draw_id_) return std::nullopt;
  const auto index = FindBatchIndex(*selected_draw_id_);
  return index ? std::optional<CapturedRenderBatch>(frame_.batches[*index])
               : std::nullopt;
}

std::optional<CapturedRenderBatch> RenderCaptureInspector::PreviousBatch() const {
  std::scoped_lock lock(mutex_);
  if (!selected_draw_id_) return std::nullopt;
  const auto index = FindBatchIndex(*selected_draw_id_);
  return index && *index > 0
             ? std::optional<CapturedRenderBatch>(frame_.batches[*index - 1])
             : std::nullopt;
}

std::optional<CapturedPrimitiveReference>
RenderCaptureInspector::SelectedPrimitive() const {
  std::scoped_lock lock(mutex_);
  if (!selected_draw_id_) return std::nullopt;
  const auto index = FindBatchIndex(*selected_draw_id_);
  return index ? frame_.batches[*index].primitive_reference : std::nullopt;
}

bool RenderCaptureInspector::SetMarker(
    const std::optional<CapturedDrawHandle> handle) {
  std::scoped_lock lock(mutex_);
  if (!handle) {
    marker_draw_id_.reset();
    return true;
  }
  if (!IsCurrent(handle->capture) || !FindBatchIndex(handle->draw_id)) {
    SetError(RenderCaptureErrorCode::kStaleHandle, "marker handle is stale");
    return false;
  }
  marker_draw_id_ = handle->draw_id;
  return true;
}

std::optional<CapturedDrawHandle> RenderCaptureInspector::marker() const {
  std::scoped_lock lock(mutex_);
  return marker_draw_id_ && FindBatchIndex(*marker_draw_id_)
             ? std::optional<CapturedDrawHandle>(
                   CapturedDrawHandle{identity_, *marker_draw_id_})
             : std::nullopt;
}

bool RenderCaptureInspector::ToggleOrRemoveBatch(
    const CapturedDrawHandle handle, const bool remove) {
  std::scoped_lock lock(mutex_);
  if (!IsCurrent(handle.capture)) {
    SetError(RenderCaptureErrorCode::kStaleHandle, "draw handle is stale");
    return false;
  }
  const auto index = FindBatchIndex(handle.draw_id);
  if (!index) return false;
  if (!remove) {
    frame_.batches[*index].enabled = !frame_.batches[*index].enabled;
  } else {
    if (state_edit_ && state_edit_->draw_id == handle.draw_id) AbandonStateEdit();
    frame_.batches.erase(frame_.batches.begin() +
                         static_cast<std::ptrdiff_t>(*index));
    frame_.events.erase(
        std::remove_if(frame_.events.begin(), frame_.events.end(),
                       [handle](const CapturedRenderEvent& event) {
                         return event.draw_id == handle.draw_id;
                       }),
        frame_.events.end());
    if (selected_draw_id_ == handle.draw_id) selected_draw_id_.reset();
    if (marker_draw_id_ == handle.draw_id) marker_draw_id_.reset();
  }
  ++revision_;
  error_ = {};
  return true;
}

bool RenderCaptureInspector::SetAnnotation(const CapturedDrawHandle handle,
                                           const std::string_view text) {
  std::scoped_lock lock(mutex_);
  if (!IsCurrent(handle.capture)) {
    SetError(RenderCaptureErrorCode::kStaleHandle, "draw handle is stale");
    return false;
  }
  const auto index = FindBatchIndex(handle.draw_id);
  if (!index || text.size() > limits_.max_string_bytes) {
    SetError(RenderCaptureErrorCode::kLimitExceeded,
             "annotation exceeds configured string limit");
    return false;
  }
  const auto previous = frame_.batches[*index].annotation;
  frame_.batches[*index].annotation.assign(text);
  if (ApproximateBytes(frame_) > limits_.max_capture_bytes) {
    frame_.batches[*index].annotation = previous;
    SetError(RenderCaptureErrorCode::kLimitExceeded,
             "capture exceeds configured byte limit");
    return false;
  }
  ++revision_;
  error_ = {};
  return true;
}

std::vector<RenderStateDifference>
RenderCaptureInspector::SelectedStateDifferences() const {
  std::scoped_lock lock(mutex_);
  if (!selected_draw_id_) return {};
  const auto index = FindBatchIndex(*selected_draw_id_);
  if (!index) return {};
  const auto previous = *index > 0
                            ? std::optional<RenderStateSnapshot>(
                                  frame_.batches[*index - 1].state)
                            : std::nullopt;
  const auto& selected = frame_.batches[*index];
  return CompareRenderStateSnapshots(selected.state, frame_.default_state,
                                     previous, selected.unedited_state);
}

bool RenderCaptureInspector::BeginSelectedStateEdit(
    const std::string_view path) {
  std::scoped_lock lock(mutex_);
  if (state_edit_ || !selected_draw_id_) return false;
  const auto index = FindBatchIndex(*selected_draw_id_);
  if (!index) return false;
  auto& batch = frame_.batches[*index];
  const auto entry = batch.state.Find(path);
  if (!entry || !entry->editable) return false;
  if (!batch.unedited_state) batch.unedited_state = batch.state;
  state_edit_ = StateEditTransaction{batch.id, std::string(path), entry->value,
                                     entry->comparison};
  return true;
}

bool RenderCaptureInspector::PreviewSelectedStateEdit(RenderStateValue value) {
  std::scoped_lock lock(mutex_);
  if (!state_edit_) return false;
  const auto index = FindBatchIndex(state_edit_->draw_id);
  if (!index) return false;
  auto& state = frame_.batches[*index].state;
  const auto current = state.Find(state_edit_->path);
  if (!current || !current->editable || current->value.index() != value.index()) {
    return false;
  }
  if (StateValueBytes(value) > limits_.max_capture_bytes ||
      (std::holds_alternative<std::string>(value) &&
       std::get<std::string>(value).size() > limits_.max_string_bytes)) {
    SetError(RenderCaptureErrorCode::kLimitExceeded,
             "state value exceeds configured limit");
    return false;
  }
  if (!RenderStateValuesEqual(current->value, value, state_edit_->comparison)) {
    const auto previous = current->value;
    state.Set(state_edit_->path, std::move(value), true, state_edit_->comparison);
    if (ApproximateBytes(frame_) > limits_.max_capture_bytes) {
      state.Set(state_edit_->path, previous, true, state_edit_->comparison);
      SetError(RenderCaptureErrorCode::kLimitExceeded,
               "capture exceeds configured byte limit");
      return false;
    }
    ++revision_;
  }
  return true;
}

bool RenderCaptureInspector::CommitSelectedStateEdit() {
  std::scoped_lock lock(mutex_);
  if (!state_edit_) return false;
  AbandonStateEdit();
  return true;
}

bool RenderCaptureInspector::CancelSelectedStateEdit() {
  std::scoped_lock lock(mutex_);
  if (!state_edit_) return false;
  const auto transaction = *state_edit_;
  const auto index = FindBatchIndex(transaction.draw_id);
  if (!index) {
    AbandonStateEdit();
    return false;
  }
  auto& state = frame_.batches[*index].state;
  const auto current = state.Find(transaction.path);
  if (!current || !current->editable ||
      current->value.index() != transaction.original_value.index()) {
    AbandonStateEdit();
    return false;
  }
  if (!RenderStateValuesEqual(current->value, transaction.original_value,
                              transaction.comparison)) {
    state.Set(transaction.path, transaction.original_value, true,
              transaction.comparison);
    ++revision_;
  }
  AbandonStateEdit();
  return true;
}

bool RenderCaptureInspector::is_editing_state() const {
  std::scoped_lock lock(mutex_);
  return state_edit_.has_value();
}

std::string RenderCaptureInspector::state_edit_path() const {
  std::scoped_lock lock(mutex_);
  return state_edit_ ? state_edit_->path : std::string{};
}

bool RenderCaptureInspector::IsRenderable(
    const CapturedRenderBatch& batch) noexcept {
  return batch.enabled && batch.renderable && batch.marker == BatchMarker::kNone;
}

bool RenderCaptureInspector::BatchUsesResource(
    const CapturedRenderBatch& batch,
    const render::RenderResourceKey resource) noexcept {
  const auto in_slots = std::any_of(
      batch.resource_slots.begin(), batch.resource_slots.end(),
      [resource](const auto& slot) { return slot && *slot == resource; });
  const auto in_shaders = std::any_of(
      batch.shader_links.begin(), batch.shader_links.end(),
      [resource](const auto& link) { return link.resource == resource; });
  if (in_slots || in_shaders) return true;
  if (!batch.primitive_reference) return false;
  const auto& primitive = *batch.primitive_reference;
  return primitive.vertex_format == resource || primitive.vertex_buffer == resource ||
         primitive.index_buffer == resource;
}

std::size_t RenderCaptureInspector::ActiveResourceSlotCount(
    const CapturedRenderBatch& batch) noexcept {
  for (std::size_t i = batch.resource_slots.size(); i > 0; --i) {
    if (batch.resource_slots[i - 1]) return i;
  }
  return 0;
}

std::optional<std::size_t> RenderCaptureInspector::FindBatchIndex(
    const std::uint64_t draw_id) const noexcept {
  const auto found = std::find_if(frame_.batches.begin(), frame_.batches.end(),
                                  [draw_id](const auto& batch) {
                                    return batch.id == draw_id;
                                  });
  return found == frame_.batches.end()
             ? std::nullopt
             : std::optional<std::size_t>(
                   static_cast<std::size_t>(found - frame_.batches.begin()));
}

bool RenderCaptureInspector::IsCurrent(const CaptureIdentity identity) const noexcept {
  return identity == identity_;
}

bool RenderCaptureInspector::ValidateFrame(const CapturedRenderFrame& frame,
                                           std::string& detail) const {
  if (frame.batches.size() > limits_.max_batches ||
      frame.resources.size() > limits_.max_resources ||
      frame.events.size() > limits_.max_events ||
      frame.batches.size() + frame.resources.size() + 1 > 65'535 ||
      !StateFits(frame.default_state, limits_)) {
    detail = "capture exceeds configured object limits";
    return false;
  }
  for (const auto& resource : frame.resources.Records()) {
    if (resource.descriptor.format.size() > limits_.max_string_bytes ||
        resource.descriptor.name.size() > limits_.max_string_bytes ||
        resource.content.bytes.size() > limits_.max_capture_bytes ||
        resource.content.vertex_attributes.size() >
            limits_.max_state_entries_per_batch) {
      detail = "captured resource exceeds configured limits";
      return false;
    }
  }
  std::set<std::uint64_t> draw_ids;
  for (const auto& batch : frame.batches) {
    if (!draw_ids.insert(batch.id).second ||
        batch.label.size() > limits_.max_string_bytes ||
        batch.primitive.size() > limits_.max_string_bytes ||
        batch.annotation.size() > limits_.max_string_bytes ||
        !StateFits(batch.state, limits_) ||
        (batch.unedited_state && !StateFits(*batch.unedited_state, limits_)) ||
        batch.searchable_terms.size() > limits_.max_search_terms_per_batch ||
        batch.source_links.size() > limits_.max_links_per_batch ||
        batch.shader_links.size() > limits_.max_links_per_batch) {
      detail = "captured draw is duplicate or exceeds configured limits";
      return false;
    }
    for (const auto& term : batch.searchable_terms) {
      if (term.size() > limits_.max_string_bytes) {
        detail = "captured search term exceeds configured string limit";
        return false;
      }
    }
    for (const auto& source : batch.source_links) {
      if (source.path.size() > limits_.max_string_bytes) {
        detail = "captured source link exceeds configured string limit";
        return false;
      }
    }
    for (const auto& slot : batch.resource_slots) {
      if (slot && !frame.resources.Contains(*slot)) {
        detail = "captured draw references missing resource";
        return false;
      }
    }
    for (const auto& shader : batch.shader_links) {
      if (!frame.resources.Contains(shader.resource) ||
          (shader.resource.kind != render::RenderResourceKind::VertexShader &&
           shader.resource.kind != render::RenderResourceKind::PixelShader)) {
        detail = "captured shader link references invalid resource";
        return false;
      }
      if (shader.source && shader.source->path.size() > limits_.max_string_bytes) {
        detail = "captured shader source exceeds configured string limit";
        return false;
      }
    }
    if (batch.primitive_reference) {
      for (const auto& resource : {batch.primitive_reference->vertex_format,
                                   batch.primitive_reference->vertex_buffer,
                                   batch.primitive_reference->index_buffer}) {
        if (resource && !frame.resources.Contains(*resource)) {
          detail = "captured primitive references missing resource";
          return false;
        }
      }
    }
  }
  std::uint64_t previous_sequence = 0;
  bool first = true;
  for (const auto& event : frame.events) {
    if ((!first && event.sequence <= previous_sequence) ||
        event.label.size() > limits_.max_string_bytes ||
        (event.draw_id && !draw_ids.contains(*event.draw_id))) {
      detail = "captured event order or draw reference is invalid";
      return false;
    }
    previous_sequence = event.sequence;
    first = false;
  }
  if (ApproximateBytes(frame) > limits_.max_capture_bytes) {
    detail = "capture exceeds configured byte limit";
    return false;
  }
  return true;
}

std::uint64_t RenderCaptureInspector::ApproximateBytes(
    const CapturedRenderFrame& frame) const noexcept {
  std::uint64_t total = sizeof(frame);
  const auto add = [&total](const std::uint64_t value) {
    total = value > std::numeric_limits<std::uint64_t>::max() - total
                ? std::numeric_limits<std::uint64_t>::max()
                : total + value;
  };
  for (const auto& entry : frame.default_state.entries()) {
    add(entry.path.size());
    add(StateValueBytes(entry.value));
  }
  for (const auto& batch : frame.batches) {
    add(sizeof(batch));
    add(batch.label.size());
    add(batch.primitive.size());
    add(batch.annotation.size());
    for (const auto& term : batch.searchable_terms) add(term.size());
    for (const auto& source : batch.source_links) add(source.path.size());
    for (const auto& shader : batch.shader_links) {
      if (shader.source) add(shader.source->path.size());
    }
    for (const auto& entry : batch.state.entries()) {
      add(entry.path.size());
      add(StateValueBytes(entry.value));
    }
    if (batch.unedited_state) {
      for (const auto& entry : batch.unedited_state->entries()) {
        add(entry.path.size());
        add(StateValueBytes(entry.value));
      }
    }
  }
  for (const auto& event : frame.events) add(sizeof(event) + event.label.size());
  for (const auto& resource : frame.resources.Records()) {
    add(sizeof(resource));
    add(resource.descriptor.format.size());
    add(resource.descriptor.name.size());
    add(resource.content.bytes.size());
    add(resource.content.vertex_attributes.size() *
        sizeof(render::RenderCapturedVertexAttribute));
    add(resource.batch_indices.size() * sizeof(std::uint32_t));
  }
  return total;
}

void RenderCaptureInspector::SetError(const RenderCaptureErrorCode code,
                                      std::string detail) {
  error_ = {code, std::move(detail)};
}

void RenderCaptureInspector::ClearFrameUnlocked() {
  frame_ = {};
  selected_draw_id_.reset();
  marker_draw_id_.reset();
  AbandonStateEdit();
  identity_.value = gNextCaptureIdentity.fetch_add(1, std::memory_order_relaxed);
  ++identity_.generation;
  ++revision_;
  error_ = {};
}

void RenderCaptureInspector::AbandonStateEdit() noexcept {
  state_edit_.reset();
}

CapturedRenderFrame BuildCapturedRenderFrame(
    const render::RenderSubmitTrace& trace,
    std::optional<std::uint32_t> frame_index) {
  const auto& events = trace.events();
  if (!frame_index && !events.empty()) frame_index = events.back().frame;
  CapturedRenderFrame frame;
  frame.frame_index = frame_index.value_or(0);
  frame.resources = trace.resources();
  frame.default_state.Set("submit.view", std::uint64_t{0});
  frame.default_state.Set("submit.skinProfile", std::uint64_t{0});
  frame.default_state.Set("submit.instanceId", std::uint64_t{0});
  frame.default_state.Set("submit.stateMask", std::uint64_t{0});
  frame.default_state.Set("submit.sortKey", std::int64_t{-1});
  frame.default_state.Set("submit.metadata.pass", std::string{});
  frame.default_state.Set("submit.metadata.pipeline", std::string{});
  frame.default_state.Set("submit.metadata.skinningMode", std::string{});
  frame.default_state.Set("submit.metadata.status", std::string{"Ready"});
  frame.default_state.Set("submit.metadata.reason", std::string{"None"});
  frame.default_state.Set("submit.metadata.detail", std::string{});
  frame.default_state.Set("submit.metadata.mesh", std::string{});
  frame.default_state.Set("submit.metadata.material", std::string{});
  if (frame_index) frame.default_state.Set("submit.frame",
                                          static_cast<std::uint64_t>(*frame_index));
  std::uint64_t sequence = 0;
  for (const auto& event : events) {
    if (!frame_index || event.frame != *frame_index) continue;
    CapturedRenderBatch batch;
    batch.id = (static_cast<std::uint64_t>(event.frame) << 32U) | event.idx;
    batch.frame_index = event.frame;
    batch.label = event.model_path.empty() ? event.pass : event.model_path;
    batch.primitive = event.mesh;
    batch.renderable = event.status == "Ready" &&
                       event.pass.find(".reject") == std::string::npos;
    batch.gpu_duration_nanoseconds = event.gpu_duration_nanoseconds;
    batch.shader_error = event.shader_error;
    batch.vertex_format_mismatch = event.vertex_format_mismatch;
    batch.resource_slots = event.resource_slots;
    if (event.primitive_reference) {
      const auto& primitive = *event.primitive_reference;
      batch.primitive_reference = CapturedPrimitiveReference{
          primitive.topology_code, primitive.vertex_format,
          primitive.vertex_buffer, primitive.index_buffer,
          primitive.first_vertex, primitive.vertex_count,
          primitive.first_index, primitive.index_count};
    }
    batch.searchable_terms = {
        event.model_path, event.pass, event.pipeline, event.skinning_mode,
        event.status, event.reason, event.detail, event.mesh, event.material};
    AppendTraceState(batch.state, event);
    frame.events.push_back(CapturedRenderEvent{
        sequence++, event.frame, CapturedRenderEventKind::kDraw, batch.id,
        batch.label});
    frame.batches.push_back(std::move(batch));
  }
  return frame;
}

}

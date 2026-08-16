#pragma once

#include "openwow/render/resources/render_resource_identity.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace openwow::render {

struct RenderResourceDescriptor {
  RenderResourceKey key;
  std::uint64_t byte_size = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t depth = 0;
  std::uint32_t mip_count = 0;
  std::string format;
  std::string name;
  bool render_target = false;
  bool dynamic = false;
  bool utility = false;

  friend bool operator==(const RenderResourceDescriptor&,
                         const RenderResourceDescriptor&) = default;
};

enum class RenderCapturedComponentType : std::uint32_t {
  UnsignedByte = 0x1401,
  SignedShort = 0x1402,
  UnsignedShort = 0x1403,
  UnsignedInt = 0x1405,
  Float = 0x1406,
};

struct RenderCapturedVertexAttribute {
  std::uint32_t stream = 0;
  std::uint32_t semantic = 0;
  std::uint32_t byte_offset = 0;
  std::uint32_t byte_stride = 0;
  RenderCapturedComponentType component_type{
      RenderCapturedComponentType::Float};
  std::uint32_t component_count = 0;
  bool normalized = false;

  friend bool operator==(const RenderCapturedVertexAttribute&,
                         const RenderCapturedVertexAttribute&) = default;
};

struct RenderResourceContent {

  std::vector<std::uint8_t> bytes;
  std::uint32_t element_stride = 0;
  std::optional<RenderCapturedComponentType> index_component_type;

  std::vector<RenderCapturedVertexAttribute> vertex_attributes;

  friend bool operator==(const RenderResourceContent&,
                         const RenderResourceContent&) = default;
};

struct RenderResourceRecord {
  RenderResourceDescriptor descriptor;
  RenderResourceContent content;
  std::vector<std::uint32_t> batch_indices;
};

struct RenderResourceSnapshotSummary {
  std::size_t texture_count = 0;
  std::size_t buffer_count = 0;
  std::size_t shader_count = 0;
  std::size_t vertex_format_count = 0;
  std::size_t batch_count = 0;
  std::uint64_t total_bytes = 0;
  bool total_bytes_saturated = false;
};

class RenderResourceSnapshot final {
 public:
  enum class AddResult : std::uint8_t {
    Added,
    Duplicate,
    ConflictingDescriptor,
    InvalidIdentity,
  };

  [[nodiscard]] AddResult AddResource(
      RenderResourceDescriptor descriptor, RenderResourceContent content = {});

  [[nodiscard]] bool RecordBatchUse(std::uint32_t batch_index,
                                    RenderResourceKey resource);

  [[nodiscard]] bool Contains(RenderResourceKey resource) const noexcept;
  [[nodiscard]] const RenderResourceDescriptor* FindDescriptor(
      RenderResourceKey resource) const noexcept;
  [[nodiscard]] const RenderResourceContent* FindContent(
      RenderResourceKey resource) const noexcept;
  [[nodiscard]] std::size_t size() const noexcept { return resources_.size(); }
  [[nodiscard]] bool empty() const noexcept { return resources_.empty(); }

  [[nodiscard]] std::vector<RenderResourceRecord> Records() const;
  [[nodiscard]] RenderResourceSnapshotSummary Summary() const;
  [[nodiscard]] std::string ToCsv() const;

  void Clear() noexcept;

 private:
  struct KeyLess {
    [[nodiscard]] bool operator()(const RenderResourceKey& lhs,
                                  const RenderResourceKey& rhs) const noexcept;
  };

  struct StoredResource {
    RenderResourceDescriptor descriptor;
    RenderResourceContent content;
    std::set<std::uint32_t> batch_indices;
  };

  std::map<RenderResourceKey, StoredResource, KeyLess> resources_;
};

[[nodiscard]] const char* RenderResourceKindName(RenderResourceKind kind) noexcept;

}

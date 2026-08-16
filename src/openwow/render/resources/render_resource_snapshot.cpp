#include "openwow/render/resources/render_resource_snapshot.h"

#include <limits>
#include <string_view>
#include <utility>

namespace openwow::render {
namespace {

void AppendCsvField(std::string& out, const std::string_view value) {
  const bool quote = value.find_first_of(",\"\r\n") != std::string_view::npos;
  if (!quote) {
    out.append(value);
    return;
  }

  out.push_back('"');
  for (const char ch : value) {
    if (ch == '"') {
      out.push_back('"');
    }
    out.push_back(ch);
  }
  out.push_back('"');
}

void AppendFlags(std::string& out, const RenderResourceDescriptor& descriptor) {
  bool needs_separator = false;
  const auto append = [&](const std::string_view flag) {
    if (needs_separator) {
      out.push_back(';');
    }
    out.append(flag);
    needs_separator = true;
  };

  if (descriptor.render_target) {
    append("render_target");
  }
  if (descriptor.dynamic) {
    append("dynamic");
  }
  if (descriptor.utility) {
    append("utility");
  }
}

void SaturatingAdd(const std::uint64_t value, std::uint64_t& total,
                   bool& saturated) noexcept {
  if (value > std::numeric_limits<std::uint64_t>::max() - total) {
    total = std::numeric_limits<std::uint64_t>::max();
    saturated = true;
    return;
  }
  total += value;
}

}

const char* RenderResourceKindName(const RenderResourceKind kind) noexcept {
  switch (kind) {
    case RenderResourceKind::Texture:
      return "Texture";
    case RenderResourceKind::VertexBuffer:
      return "Vertex Buffer";
    case RenderResourceKind::IndexBuffer:
      return "Index Buffer";
    case RenderResourceKind::VertexShader:
      return "Vertex Shader";
    case RenderResourceKind::PixelShader:
      return "Pixel Shader";
    case RenderResourceKind::VertexFormat:
      return "Vertex Format";
  }
  return "Unknown";
}

bool RenderResourceSnapshot::KeyLess::operator()(
    const RenderResourceKey& lhs, const RenderResourceKey& rhs) const noexcept {
  if (lhs.kind != rhs.kind) {
    return static_cast<std::uint8_t>(lhs.kind) < static_cast<std::uint8_t>(rhs.kind);
  }
  return lhs.id < rhs.id;
}

RenderResourceSnapshot::AddResult RenderResourceSnapshot::AddResource(
    RenderResourceDescriptor descriptor, RenderResourceContent content) {
  if (descriptor.key.id == 0) {
    return AddResult::InvalidIdentity;
  }

  if (!content.bytes.empty() &&
      descriptor.byte_size != content.bytes.size()) {
    return AddResult::ConflictingDescriptor;
  }
  if (descriptor.key.kind != RenderResourceKind::VertexFormat &&
      !content.vertex_attributes.empty()) {
    return AddResult::ConflictingDescriptor;
  }
  if (descriptor.key.kind != RenderResourceKind::IndexBuffer &&
      content.index_component_type.has_value()) {
    return AddResult::ConflictingDescriptor;
  }

  const auto existing = resources_.find(descriptor.key);
  if (existing != resources_.end()) {
    return existing->second.descriptor == descriptor &&
                   existing->second.content == content
               ? AddResult::Duplicate
               : AddResult::ConflictingDescriptor;
  }

  const RenderResourceKey key = descriptor.key;
  resources_.emplace(key, StoredResource{.descriptor = std::move(descriptor),
                                         .content = std::move(content)});
  return AddResult::Added;
}

bool RenderResourceSnapshot::RecordBatchUse(const std::uint32_t batch_index,
                                            const RenderResourceKey resource) {
  const auto found = resources_.find(resource);
  if (found == resources_.end()) {
    return false;
  }
  found->second.batch_indices.insert(batch_index);
  return true;
}

bool RenderResourceSnapshot::Contains(const RenderResourceKey resource) const noexcept {
  return resources_.contains(resource);
}

const RenderResourceDescriptor* RenderResourceSnapshot::FindDescriptor(
    const RenderResourceKey resource) const noexcept {
  const auto found = resources_.find(resource);
  return found != resources_.end() ? &found->second.descriptor : nullptr;
}

const RenderResourceContent* RenderResourceSnapshot::FindContent(
    const RenderResourceKey resource) const noexcept {
  const auto found = resources_.find(resource);
  return found != resources_.end() ? &found->second.content : nullptr;
}

std::vector<RenderResourceRecord> RenderResourceSnapshot::Records() const {
  std::vector<RenderResourceRecord> records;
  records.reserve(resources_.size());
  for (const auto& [key, stored] : resources_) {
    (void)key;
    records.push_back({
        .descriptor = stored.descriptor,
        .content = stored.content,
        .batch_indices = std::vector<std::uint32_t>(stored.batch_indices.begin(),
                                                    stored.batch_indices.end()),
    });
  }
  return records;
}

RenderResourceSnapshotSummary RenderResourceSnapshot::Summary() const {
  RenderResourceSnapshotSummary summary;
  std::set<std::uint32_t> batches;

  for (const auto& [key, stored] : resources_) {
    switch (key.kind) {
      case RenderResourceKind::Texture:
        ++summary.texture_count;
        break;
      case RenderResourceKind::VertexBuffer:
      case RenderResourceKind::IndexBuffer:
        ++summary.buffer_count;
        break;
      case RenderResourceKind::VertexShader:
      case RenderResourceKind::PixelShader:
        ++summary.shader_count;
        break;
      case RenderResourceKind::VertexFormat:
        ++summary.vertex_format_count;
        break;
    }

    SaturatingAdd(stored.descriptor.byte_size, summary.total_bytes,
                  summary.total_bytes_saturated);
    batches.insert(stored.batch_indices.begin(), stored.batch_indices.end());
  }

  summary.batch_count = batches.size();
  return summary;
}

std::string RenderResourceSnapshot::ToCsv() const {
  std::string out;
  out.reserve(128 + resources_.size() * 96);
  out += "kind,id,bytes,width,height,depth,mips,format,flags,name,batches\n";

  for (const auto& [key, stored] : resources_) {
    const auto& descriptor = stored.descriptor;
    AppendCsvField(out, RenderResourceKindName(key.kind));
    out.push_back(',');
    out += std::to_string(key.id);
    out.push_back(',');
    out += std::to_string(descriptor.byte_size);
    out.push_back(',');
    out += std::to_string(descriptor.width);
    out.push_back(',');
    out += std::to_string(descriptor.height);
    out.push_back(',');
    out += std::to_string(descriptor.depth);
    out.push_back(',');
    out += std::to_string(descriptor.mip_count);
    out.push_back(',');
    AppendCsvField(out, descriptor.format);
    out.push_back(',');
    std::string flags;
    AppendFlags(flags, descriptor);
    AppendCsvField(out, flags);
    out.push_back(',');
    AppendCsvField(out, descriptor.name);
    out.push_back(',');

    bool needs_separator = false;
    for (const std::uint32_t batch : stored.batch_indices) {
      if (needs_separator) {
        out.push_back(';');
      }
      out += std::to_string(batch);
      needs_separator = true;
    }
    out.push_back('\n');
  }

  return out;
}

void RenderResourceSnapshot::Clear() noexcept {
  resources_.clear();
}

}

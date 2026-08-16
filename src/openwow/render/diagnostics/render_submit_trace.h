#pragma once

#include "openwow/render/resources/render_resource_identity.h"
#include "openwow/render/resources/render_resource_snapshot.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace openwow::render {

struct RenderSubmitPrimitiveTrace {
  std::uint32_t topology_code{0x00000501u};
  std::optional<RenderResourceKey> vertex_format;
  std::optional<RenderResourceKey> vertex_buffer;
  std::optional<RenderResourceKey> index_buffer;
  std::uint32_t first_vertex{0};
  std::uint32_t vertex_count{0};
  std::uint32_t first_index{0};
  std::uint32_t index_count{0};
};

struct RenderSubmitTraceEvent {
  std::uint32_t idx{0};
  std::uint32_t frame{0};
  std::uint16_t view{0};
  std::string model_path;
  std::uint32_t skin_profile{0};
  std::uint32_t instance_id{0};
  std::string pass;
  std::string pipeline;
  std::string skinning_mode;
  std::string status{"Ready"};
  std::string reason{"None"};
  std::string detail;
  std::uint32_t state_mask{0};

  std::int32_t sort_key{-1};
  std::string mesh;
  std::string material;

  std::optional<RenderSubmitPrimitiveTrace> primitive_reference;
  std::array<std::optional<RenderResourceKey>, 16> resource_slots{};
  std::uint64_t gpu_duration_nanoseconds{0};
  bool shader_error{false};
  bool vertex_format_mismatch{false};
};

class RenderSubmitTrace {
 public:
  void Add(RenderSubmitTraceEvent e) {
    const auto record = [this, batch = e.idx](
                            const std::optional<RenderResourceKey>& resource) {
      if (resource.has_value()) {
        (void)resources_.RecordBatchUse(batch, *resource);
      }
    };
    if (e.primitive_reference.has_value()) {
      record(e.primitive_reference->vertex_format);
      record(e.primitive_reference->vertex_buffer);
      record(e.primitive_reference->index_buffer);
    }
    for (const auto& resource : e.resource_slots) {
      record(resource);
    }
    events_.push_back(std::move(e));
  }

  [[nodiscard]] RenderResourceSnapshot::AddResult RegisterResource(
      RenderResourceDescriptor descriptor, RenderResourceContent content = {}) {
    return resources_.AddResource(std::move(descriptor), std::move(content));
  }

  [[nodiscard]] bool HasResource(const RenderResourceKey resource) const noexcept {
    return resources_.Contains(resource);
  }

  [[nodiscard]] const RenderResourceSnapshot& resources() const noexcept {
    return resources_;
  }

  [[nodiscard]] const std::vector<RenderSubmitTraceEvent>& events() const { return events_; }

  [[nodiscard]] std::string ToTsv() const {
    std::string out;
    out.reserve(256 + events_.size() * 96);
    out += "idx\tframe\tview\tmodel_path\tskin_profile\tinstance_id\tpass\tpipeline\tskinning_mode\tstatus\treason\tdetail\tstate_mask\tsort_key\tmesh\tmaterial\n";
    for (const auto& e : events_) {
      out += std::to_string(e.idx);
      out += '\t';
      out += std::to_string(e.frame);
      out += '\t';
      out += std::to_string(static_cast<std::uint32_t>(e.view));
      out += '\t';
      out += EscapeField(e.model_path);
      out += '\t';
      out += std::to_string(e.skin_profile);
      out += '\t';
      out += std::to_string(e.instance_id);
      out += '\t';
      out += EscapeField(e.pass);
      out += '\t';
      out += EscapeField(e.pipeline);
      out += '\t';
      out += EscapeField(e.skinning_mode);
      out += '\t';
      out += EscapeField(e.status);
      out += '\t';
      out += EscapeField(e.reason);
      out += '\t';
      out += EscapeField(e.detail);
      out += '\t';
      out += Hex32(e.state_mask);
      out += '\t';
      if (e.sort_key < 0) {
        out += '-';
      } else {
        out += std::to_string(e.sort_key);
      }
      out += '\t';
      out += EscapeField(e.mesh);
      out += '\t';
      out += EscapeField(e.material);
      out += '\n';
    }
    return out;
  }

  [[nodiscard]] bool WriteTsvFile(const std::filesystem::path& path) const {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    const std::string tsv = ToTsv();
    out.write(tsv.data(), static_cast<std::streamsize>(tsv.size()));
    return static_cast<bool>(out);
  }

 private:
  static std::string Hex32(std::uint32_t v) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.resize(10);
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < 8; ++i) {
      const std::uint32_t shift = static_cast<std::uint32_t>((7 - i) * 4);
      out[2 + i] = kHex[(v >> shift) & 0xFu];
    }
    return out;
  }

  static std::string EscapeField(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char ch : s) {
      switch (ch) {
        case '\\': out += "\\\\"; break;
        case '\t': out += "\\t"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        default: out.push_back(ch); break;
      }
    }
    return out;
  }

  std::vector<RenderSubmitTraceEvent> events_;
  RenderResourceSnapshot resources_;
};

struct RenderSubmitTraceBinding {
  std::reference_wrapper<RenderSubmitTrace> trace;
  std::reference_wrapper<const std::uint32_t> frame;
  std::reference_wrapper<std::uint32_t> index;
};

}

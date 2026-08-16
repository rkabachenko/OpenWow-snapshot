#pragma once

#include "openwow/render/scene/scene_graph.h"

#include <cstdint>
#include <optional>
#include <unordered_map>

namespace openwow::render {

struct AttachmentTransform {
  Mat4 local_matrix{};

  [[nodiscard]] static AttachmentTransform Translation(float x, float y, float z) {
    return AttachmentTransform{.local_matrix = Mat4::Translation(x, y, z)};
  }
};

class AttachmentGraph {
 public:
  explicit AttachmentGraph(SceneGraph* graph);

  uint32_t Attach(uint32_t child_node_id,
                  uint32_t parent_node_id,
                  uint32_t attachment_index);

  void Detach(uint32_t child_node_id);

  [[nodiscard]] uint32_t LinkNode(uint32_t child_node_id) const;

  void SetAttachmentTransform(uint32_t parent_node_id,
                              uint32_t attachment_index,
                              const AttachmentTransform& t);

  [[nodiscard]] std::optional<AttachmentTransform> GetAttachmentTransform(
      uint32_t parent_node_id,
      uint32_t attachment_index) const;

  void RefreshAll();

 private:
  struct Binding {
    uint32_t parent{0};
    uint32_t attachment_index{0};
    uint32_t link{0};
  };

  struct ParentKey {
    uint32_t parent{0};
    uint32_t index{0};
    bool operator==(const ParentKey& o) const {
      return parent == o.parent && index == o.index;
    }
  };

  struct ParentKeyHash {
    std::size_t operator()(const ParentKey& k) const noexcept {

      return (static_cast<std::size_t>(k.parent) << 1) ^ static_cast<std::size_t>(k.index);
    }
  };

  void ApplyToLink(uint32_t link_node_id, const AttachmentTransform& t);

  SceneGraph* graph_{nullptr};
  std::unordered_map<uint32_t, Binding> bindings_by_child_;
  std::unordered_map<ParentKey, AttachmentTransform, ParentKeyHash> transforms_;
};

}


#include "openwow/render/models/animation/attachment_graph.h"

#include <utility>

namespace openwow::render {

AttachmentGraph::AttachmentGraph(SceneGraph* graph) : graph_(graph) {}

void AttachmentGraph::ApplyToLink(uint32_t link_node_id, const AttachmentTransform& t) {
  if (graph_ == nullptr || link_node_id == 0) return;
  graph_->SetLocalAffineTransform(link_node_id, t.local_matrix);
}

uint32_t AttachmentGraph::Attach(uint32_t child_node_id,
                                 uint32_t parent_node_id,
                                 uint32_t attachment_index) {
  if (graph_ == nullptr) return 0;
  if (child_node_id == 0 || parent_node_id == 0) return 0;
  if (graph_->GetNode(child_node_id) == nullptr) return 0;
  if (graph_->GetNode(parent_node_id) == nullptr) return 0;

  Detach(child_node_id);

  const uint32_t link = graph_->CreateNode(RenderableType::Unknown, 0.0f, 0.0f, 0.0f);
  if (link == 0) return 0;
  graph_->SetParent(link, parent_node_id);

  const ParentKey key{parent_node_id, attachment_index};
  if (auto it = transforms_.find(key); it != transforms_.end()) {
    ApplyToLink(link, it->second);
  }

  graph_->SetParent(child_node_id, link);

  bindings_by_child_.insert_or_assign(child_node_id,
                                     Binding{parent_node_id, attachment_index, link});
  return link;
}

void AttachmentGraph::Detach(uint32_t child_node_id) {
  if (graph_ == nullptr) return;
  auto it = bindings_by_child_.find(child_node_id);
  if (it == bindings_by_child_.end()) return;

  const uint32_t link = it->second.link;

  graph_->SetParent(child_node_id, 0);
  if (link != 0) {
    graph_->DestroyNode(link);
  }
  bindings_by_child_.erase(it);
}

uint32_t AttachmentGraph::LinkNode(uint32_t child_node_id) const {
  auto it = bindings_by_child_.find(child_node_id);
  if (it == bindings_by_child_.end()) return 0;
  return it->second.link;
}

void AttachmentGraph::SetAttachmentTransform(uint32_t parent_node_id,
                                             uint32_t attachment_index,
                                             const AttachmentTransform& t) {
  if (graph_ == nullptr || parent_node_id == 0) return;
  transforms_.insert_or_assign(ParentKey{parent_node_id, attachment_index}, t);

  for (auto& [child, b] : bindings_by_child_) {
    (void)child;
    if (b.parent == parent_node_id && b.attachment_index == attachment_index) {
      ApplyToLink(b.link, t);
    }
  }
}

std::optional<AttachmentTransform> AttachmentGraph::GetAttachmentTransform(
    uint32_t parent_node_id,
    uint32_t attachment_index) const {
  auto it = transforms_.find(ParentKey{parent_node_id, attachment_index});
  if (it == transforms_.end()) return std::nullopt;
  return it->second;
}

void AttachmentGraph::RefreshAll() {
  if (graph_ == nullptr) return;
  for (const auto& [child, b] : bindings_by_child_) {
    (void)child;
    const ParentKey key{b.parent, b.attachment_index};
    auto it = transforms_.find(key);
    if (it != transforms_.end()) {
      ApplyToLink(b.link, it->second);
    }
  }
}

}

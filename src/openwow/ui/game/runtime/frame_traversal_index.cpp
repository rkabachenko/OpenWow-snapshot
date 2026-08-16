#include "openwow/ui/game/runtime/frame_traversal_index.h"

#include "openwow/ui/framexml/ui_frame.h"
#include "openwow/ui/game/game_ui_scale.h"
#include "openwow/ui/game/runtime/frame_store.h"
#include "openwow/ui/game/runtime/retained_layout.h"
#include "openwow/ui/rect_utils.h"
#include "openwow/ui/ui_enum_helpers.h"
#include "openwow/ui/ui_paint_order.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace openwow::ui::game::runtime {
namespace {

using UiFrame = openwow::ui::framexml::UiFrame;

constexpr float kHitTestCellSize = 64.0F;
constexpr std::size_t kMaxCellsPerRecord = 4096u;

int StrataRank(const std::string_view strata) noexcept {
  if (strata == "WORLD") return 0;
  if (strata == "BACKGROUND") return 1;
  if (strata == "LOW") return 2;
  if (strata == "MEDIUM") return 3;
  if (strata == "HIGH") return 4;
  if (strata == "DIALOG") return 5;
  if (strata == "FULLSCREEN") return 6;
  if (strata == "FULLSCREEN_DIALOG") return 7;
  if (strata == "TOOLTIP") return 8;
  return 3;
}

int FrameStackStrata(const std::string& strata) noexcept {
  int value = 0;
  static_cast<void>(
      openwow::ui::StringToScriptFrameStrata(strata.c_str(), &value));
  return value;
}

std::int32_t CellCoordinate(const float value) {
  const double cell = std::floor(static_cast<double>(value) /
                                 static_cast<double>(kHitTestCellSize));
  return static_cast<std::int32_t>(std::clamp(
      cell, static_cast<double>(std::numeric_limits<std::int32_t>::min()),
      static_cast<double>(std::numeric_limits<std::int32_t>::max())));
}

std::int64_t CellKey(const std::int32_t x, const std::int32_t y) {
  return static_cast<std::int64_t>(
      (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32u) |
      static_cast<std::uint32_t>(y));
}

}

struct FrameTraversalIndex::Impl {

  using FrameHandle = FrameStore::FrameHandle;

  struct HierarchyMetadata {
    std::string mouse_target_key;
    FrameHandle mouse_target_handle{FrameStore::kInvalidFrameHandle};
    double effective_depth{0.0};
    float effective_scale{1.0F};
    std::array<ScrollClipAncestor, 8> scroll_clip_ancestors{};
    std::uint8_t scroll_clip_ancestor_count{0u};
    std::uint8_t semantic_owner_mask{0u};
    bool effective_visible{true};
  };

  struct HitTestRecord {
    std::string key;
    std::string mouse_target_key;
    float left{0.0F};
    float top{0.0F};
    float right{0.0F};
    float bottom{0.0F};
    int strata_rank{3};
  };

  struct FrameOrder {

    TraversalEntry traversal;
    int strata_order{3};
    int frame_level{0};
    double effective_depth{0.0};
    std::size_t insertion_order{0};

    std::size_t parent_index{openwow::ui::kNoUiPaintParent};
    int draw_layer_order{2};
    int draw_sublevel{0};
    bool region{false};
  };

  static void RefreshPaintFields(FrameOrder& row) {
    const auto* frame = row.traversal.frame;
    row.region =
        frame != nullptr &&
        (frame->runtime_kind == UiFrame::RuntimeKind::Texture ||
         frame->runtime_kind == UiFrame::RuntimeKind::FontString);
    row.draw_layer_order = openwow::ui::UiDrawLayerOrder(
        frame != nullptr ? std::string_view(frame->draw_layer)
                         : std::string_view{});
    row.draw_sublevel = frame != nullptr ? frame->draw_sublevel : 0;
  }

  FrameStore& frames;
  RetainedLayout& layout;
  std::vector<TraversalEntry> render_snapshot;
  std::vector<TraversalEntry> input_snapshot;

  std::unordered_map<FrameHandle, bool> visibility_index;
  std::vector<HitTestRecord> hit_test_records;
  std::unordered_map<std::int64_t, std::vector<std::size_t>> hit_test_buckets;
  std::vector<std::size_t> hit_test_global_records;
  Metrics metrics;
  float root_scale{1.0F};
  float viewport_height{0.0F};
  bool order_dirty{true};
  bool hit_test_dirty{true};

  std::uint64_t cached_rects_generation{0};

  bool full_rebuild_required{true};
  std::unordered_set<std::string> order_key_dirty;
  std::unordered_set<std::string> inherited_dirty;
  std::vector<FrameOrder> entries;

  std::unordered_map<FrameHandle, std::size_t> index_by_key;

  std::uint64_t entries_rects_generation{0};

  void EnsureEntriesRectsCurrent() {
    const std::uint64_t generation = layout.RectsGeneration();
    if (entries_rects_generation == generation) return;
    for (auto& row : entries) RefreshEntryRect(row.traversal);
    entries_rects_generation = generation;
  }

  void RefreshEntryRect(TraversalEntry& entry) const {
    const auto it = layout.rects().find(entry.key);
    entry.rect = it != layout.rects().end() ? &it->second : nullptr;
  }

  HierarchyMetadata ComputeHierarchy(const FrameHandle handle,
                                     const UiFrame& frame) const {
    HierarchyMetadata metadata;
    metadata.effective_scale = root_scale;

    const bool detached_region =
        (frame.runtime_kind == UiFrame::RuntimeKind::Texture ||
         frame.runtime_kind == UiFrame::RuntimeKind::FontString) &&
        frame.parent.empty();
    metadata.effective_visible = !detached_region;
    const UiFrame* current = &frame;
    FrameHandle current_handle = handle;
    constexpr int kMaxDepth = 64;
    for (int depth = 0; depth < kMaxDepth && current != nullptr; ++depth) {
      metadata.effective_depth += static_cast<double>(current->depth);
      metadata.effective_scale *= current->scale;
      metadata.effective_visible = metadata.effective_visible &&
                                   current->visible &&
                                   current->runtime_draw_layer_enabled;
      if (metadata.mouse_target_key.empty() &&
          (current->runtime_uses_mouse || current->enable_mouse)) {
        metadata.mouse_target_key = current->name;
        metadata.mouse_target_handle = current_handle;
      }
      if (current->name == "WorldMapFrame") {
        metadata.semantic_owner_mask |= kWorldMapOwner;
      } else if (current->name == "CharacterFrame") {
        metadata.semantic_owner_mask |= kCharacterPanelOwner;
      } else if (current->name == "PlayerFrame") {
        metadata.semantic_owner_mask |= kPlayerFrameOwner;
      }
      if (current->parent.empty()) break;
      current_handle = frames.ParentHandleOf(current_handle);
      current = frames.FindFrame(current_handle);
    }

    if (!frame.scroll_child_content) return metadata;
    std::array<const UiFrame*, 8> inner_to_outer{};
    std::array<FrameHandle, 8> inner_to_outer_handles{};
    std::uint8_t count = 0u;
    FrameHandle ancestor_handle = frames.ParentHandleOf(handle);
    const UiFrame* ancestor = frames.FindFrame(ancestor_handle);
    for (int depth = 0; depth < kMaxDepth && ancestor != nullptr; ++depth) {
      if (openwow::text::EqualsIgnoreCaseAscii(ancestor->kind, "ScrollFrame") &&
          count < inner_to_outer.size()) {
        inner_to_outer[count] = ancestor;
        inner_to_outer_handles[count] = ancestor_handle;
        ++count;
      }
      ancestor_handle = frames.ParentHandleOf(ancestor_handle);
      ancestor = frames.FindFrame(ancestor_handle);
    }
    const auto effective_scale_of = [&](const FrameHandle owner_handle,
                                        const UiFrame& owner) {
      float scale = root_scale;
      const UiFrame* node = &owner;
      FrameHandle node_handle = owner_handle;
      for (int depth = 0; depth < kMaxDepth && node != nullptr; ++depth) {
        scale *= node->scale;
        if (node->parent.empty()) break;
        node_handle = frames.ParentHandleOf(node_handle);
        node = frames.FindFrame(node_handle);
      }
      return scale;
    };
    for (std::uint8_t index = 0u; index < count; ++index) {
      const std::uint8_t source = count - 1u - index;
      const UiFrame* owner = inner_to_outer[source];
      metadata.scroll_clip_ancestors[index] = {
          .key = owner->name,
          .handle = inner_to_outer_handles[source],
          .effective_scale = effective_scale_of(inner_to_outer_handles[source], *owner),
      };
    }
    metadata.scroll_clip_ancestor_count = count;
    return metadata;
  }

  TraversalEntry BuildEntry(const FrameHandle handle, const UiFrame& frame,
                            const HierarchyMetadata& hierarchy) const {
    const auto rect_it = layout.rects().find(frame.name);
    return {
        .key = frame.name,
        .handle = handle,
        .mouse_target_key = hierarchy.mouse_target_key,
        .mouse_target_handle = hierarchy.mouse_target_handle,
        .lua_ref = frames.FindLuaRef(handle).value_or(-2),
        .strata_rank = StrataRank(frame.frame_strata),
        .scroll_clip_ancestors = hierarchy.scroll_clip_ancestors,
        .scroll_clip_ancestor_count = hierarchy.scroll_clip_ancestor_count,
        .semantic_owner_mask = hierarchy.semantic_owner_mask,
        .effective_scale = hierarchy.effective_scale,
        .effective_visible = hierarchy.effective_visible,
        .uses_mouse = frame.runtime_uses_mouse || frame.enable_mouse,
        .uses_mouse_wheel = frame.runtime_uses_mouse_wheel,
        .uses_keyboard = frame.runtime_uses_keyboard,

        .frame = &frame,
        .rect = rect_it != layout.rects().end() ? &rect_it->second : nullptr,
    };
  }

  bool BuildHitRect(const TraversalEntry& entry, const bool apply_scroll_clip,
                    HitTestRecord* const out) const {
    const auto* frame = frames.FindFrame(entry.key);
    const auto rect = layout.rects().find(entry.key);
    if (frame == nullptr || rect == layout.rects().end() ||
        rect->second.width <= 0 || rect->second.height <= 0) {
      return false;
    }
    const float pixel_scale = openwow::ui::game::ComputeGameUiRenderPixelScale(
        viewport_height, entry.effective_scale);
    *out = {
        .key = entry.key,
        .mouse_target_key = entry.mouse_target_key,
        .left = static_cast<float>(rect->second.x) +
                frame->hit_rect_inset_left * pixel_scale,
        .top = static_cast<float>(rect->second.y) +
               frame->hit_rect_inset_top * pixel_scale,
        .right = static_cast<float>(rect->second.x + rect->second.width) -
                 frame->hit_rect_inset_right * pixel_scale,
        .bottom = static_cast<float>(rect->second.y + rect->second.height) -
                  frame->hit_rect_inset_bottom * pixel_scale,
        .strata_rank = entry.strata_rank,
    };

    if (!entry.mouse_target_key.empty() &&
        entry.mouse_target_key != entry.key) {

      const auto* target = frames.FindFrame(entry.mouse_target_handle);
      const auto target_rect = layout.rects().find(entry.mouse_target_key);
      if (target == nullptr || target_rect == layout.rects().end() ||
          target_rect->second.width <= 0 || target_rect->second.height <= 0) {
        return false;
      }
      const float target_scale =
          openwow::ui::game::ComputeGameUiRenderPixelScale(
              viewport_height,
              ComputeHierarchy(entry.mouse_target_handle, *target)
                  .effective_scale);
      out->left = std::max(
          out->left, static_cast<float>(target_rect->second.x) +
                         target->hit_rect_inset_left * target_scale);
      out->top = std::max(
          out->top, static_cast<float>(target_rect->second.y) +
                        target->hit_rect_inset_top * target_scale);
      out->right = std::min(
          out->right,
          static_cast<float>(target_rect->second.x +
                             target_rect->second.width) -
              target->hit_rect_inset_right * target_scale);
      out->bottom = std::min(
          out->bottom,
          static_cast<float>(target_rect->second.y +
                             target_rect->second.height) -
              target->hit_rect_inset_bottom * target_scale);
    }
    if (apply_scroll_clip) {
      std::array<openwow::ui::UiScrollClipNode, 8> clip_nodes{};
      std::size_t clip_count = 0u;
      for (std::uint8_t index = 0u;
           index < entry.scroll_clip_ancestor_count; ++index) {
        const auto& ancestor = entry.scroll_clip_ancestors[index];

        const auto* owner = frames.FindFrame(ancestor.handle);
        const auto owner_rect = layout.rects().find(ancestor.key);
        if (owner == nullptr || owner_rect == layout.rects().end()) continue;
        const float owner_scale =
            openwow::ui::game::ComputeGameUiRenderPixelScale(
                viewport_height, ancestor.effective_scale);
        clip_nodes[clip_count++] = {
            .viewport = {static_cast<float>(owner_rect->second.x),
                         static_cast<float>(owner_rect->second.y),
                         static_cast<float>(owner_rect->second.width),
                         static_cast<float>(owner_rect->second.height)},
            .horizontal_scroll_pixels =
                owner->runtime_horizontal_scroll * owner_scale,
            .vertical_scroll_pixels = owner->runtime_vertical_scroll * owner_scale,
        };
      }
      const auto presentation = openwow::ui::BuildUiScrollPresentation(
          std::span<const openwow::ui::UiScrollClipNode>(clip_nodes.data(),
                                                         clip_count));
      if (presentation.clipped_out) return false;
      out->left += presentation.offset_x;
      out->right += presentation.offset_x;
      out->top += presentation.offset_y;
      out->bottom += presentation.offset_y;
      if (presentation.clip.has_value()) {
        out->left = std::max(out->left, presentation.clip->x);
        out->top = std::max(out->top, presentation.clip->y);
        out->right = std::min(out->right,
                              presentation.clip->x + presentation.clip->width);
        out->bottom = std::min(
            out->bottom, presentation.clip->y + presentation.clip->height);
      }
    }
    return out->right > out->left && out->bottom > out->top &&
           std::isfinite(out->left) && std::isfinite(out->top) &&
           std::isfinite(out->right) && std::isfinite(out->bottom);
  }

  void BuildEntriesFull() {
    entries.clear();
    entries.reserve(frames.size());
    std::size_t insertion_order = 0u;

    for (const FrameHandle handle : frames.registration_handles()) {
      const auto* frame = frames.FindFrame(handle);
      if (frame == nullptr) continue;
      const auto hierarchy = ComputeHierarchy(handle, *frame);
      auto traversal = BuildEntry(handle, *frame, hierarchy);
      entries.push_back({
          .traversal = std::move(traversal),
          .strata_order = StrataRank(frame->frame_strata),
          .frame_level = frame->frame_level,
          .effective_depth = hierarchy.effective_depth,
          .insertion_order = insertion_order++,
      });
    }
    index_by_key.clear();
    index_by_key.reserve(entries.size() * 2u);
    for (std::size_t index = 0; index < entries.size(); ++index) {
      index_by_key.emplace(entries[index].traversal.handle, index);
    }

    visibility_index.clear();
    visibility_index.reserve(entries.size());
    for (auto& row : entries) {
      RefreshPaintFields(row);
      const auto parent_it =
          index_by_key.find(frames.ParentHandleOf(row.traversal.handle));
      row.parent_index = parent_it != index_by_key.end()
                             ? parent_it->second
                             : openwow::ui::kNoUiPaintParent;
      visibility_index.insert_or_assign(row.traversal.handle,
                                        row.traversal.effective_visible);
    }
  }

  void PatchEntriesIncremental() {

    for (const auto& key : order_key_dirty) {
      const auto found = index_by_key.find(frames.HandleOf(key));
      if (found == index_by_key.end()) continue;
      auto& row = entries[found->second];
      if (row.traversal.frame == nullptr) continue;
      row.strata_order = StrataRank(row.traversal.frame->frame_strata);
      row.frame_level = row.traversal.frame->frame_level;
      row.traversal.strata_rank = row.strata_order;

      RefreshPaintFields(row);
    }

    for (const auto& root_key : inherited_dirty) {
      for (const auto& key : frames.CollectSubtreePostorder(root_key)) {
        const FrameHandle handle = frames.HandleOf(key);
        const auto found = index_by_key.find(handle);
        if (found == index_by_key.end()) continue;
        const auto* frame = frames.FindFrame(handle);
        if (frame == nullptr) continue;
        const auto hierarchy = ComputeHierarchy(handle, *frame);
        auto& row = entries[found->second];
        row.traversal = BuildEntry(handle, *frame, hierarchy);
        row.effective_depth = hierarchy.effective_depth;
        RefreshPaintFields(row);

        visibility_index.insert_or_assign(row.traversal.handle,
                                          row.traversal.effective_visible);
      }
    }
  }

  [[nodiscard]] bool CanPatchIncrementally() const noexcept {
    if (full_rebuild_required || entries.empty()) return false;
    const std::size_t dirty_roots = order_key_dirty.size() + inherited_dirty.size();
    return dirty_roots * 3U < std::max<std::size_t>(entries.size() * 2U, 1U);
  }

  void RebuildHitTest() {
    hit_test_records.clear();
    hit_test_buckets.clear();
    hit_test_global_records.clear();
    hit_test_records.reserve(input_snapshot.size());
    for (const auto& entry : input_snapshot) {

      const auto* frame = entry.frame;
      if (frame == nullptr ||
          frame->runtime_kind == UiFrame::RuntimeKind::Texture ||
          frame->runtime_kind == UiFrame::RuntimeKind::FontString) {
        continue;
      }

      if (entry.mouse_target_key.empty()) continue;

      if (!entry.uses_mouse) continue;
      HitTestRecord record;
      if (!entry.effective_visible || !BuildHitRect(entry, true, &record)) continue;
      const std::size_t record_index = hit_test_records.size();
      hit_test_records.push_back(std::move(record));
      const auto& stored = hit_test_records.back();
      const std::int32_t min_x = CellCoordinate(stored.left);
      const std::int32_t max_x = CellCoordinate(stored.right);
      const std::int32_t min_y = CellCoordinate(stored.top);
      const std::int32_t max_y = CellCoordinate(stored.bottom);
      const std::uint64_t cells_x =
          static_cast<std::uint64_t>(static_cast<std::int64_t>(max_x) - min_x) + 1u;
      const std::uint64_t cells_y =
          static_cast<std::uint64_t>(static_cast<std::int64_t>(max_y) - min_y) + 1u;
      if (cells_x > kMaxCellsPerRecord || cells_y > kMaxCellsPerRecord ||
          cells_x * cells_y > kMaxCellsPerRecord) {
        hit_test_global_records.push_back(record_index);
        continue;
      }
      for (std::int64_t y = min_y; y <= max_y; ++y) {
        for (std::int64_t x = min_x; x <= max_x; ++x) {
          hit_test_buckets[CellKey(static_cast<std::int32_t>(x),
                                   static_cast<std::int32_t>(y))]
              .push_back(record_index);
        }
      }
    }
    ++metrics.hit_test_rebuilds;
    metrics.hit_test_entries = hit_test_records.size();
    hit_test_dirty = false;
  }
};

FrameTraversalIndex::FrameTraversalIndex(FrameStore& frames,
                                         RetainedLayout& layout)
    : impl_(std::make_unique<Impl>(Impl{.frames = frames, .layout = layout})) {}

FrameTraversalIndex::~FrameTraversalIndex() = default;

void FrameTraversalIndex::Clear() {
  impl_->render_snapshot.clear();
  impl_->input_snapshot.clear();
  impl_->visibility_index.clear();
  impl_->hit_test_records.clear();
  impl_->hit_test_buckets.clear();
  impl_->hit_test_global_records.clear();
  impl_->entries.clear();
  impl_->index_by_key.clear();
  impl_->entries_rects_generation = 0;
  impl_->order_key_dirty.clear();
  impl_->inherited_dirty.clear();
  impl_->full_rebuild_required = true;
  impl_->order_dirty = true;
  impl_->hit_test_dirty = true;
}

void FrameTraversalIndex::InvalidateHierarchy() noexcept {
  impl_->order_dirty = true;
  impl_->hit_test_dirty = true;

  impl_->full_rebuild_required = true;
}

void FrameTraversalIndex::InvalidatePaintOrder() noexcept {
  impl_->order_dirty = true;
  impl_->hit_test_dirty = true;

  impl_->full_rebuild_required = true;
}

void FrameTraversalIndex::InvalidateHitTest() noexcept {
  impl_->hit_test_dirty = true;
}

void FrameTraversalIndex::InvalidateFrameOrder(
    const std::string_view key, const OrderInvalidationKind kind) noexcept {
  switch (kind) {
    case OrderInvalidationKind::kOrderKey:
      impl_->order_key_dirty.emplace(key);
      break;
    case OrderInvalidationKind::kInherited:
      impl_->inherited_dirty.emplace(key);
      break;
  }
  impl_->order_dirty = true;
  impl_->hit_test_dirty = true;
}

bool FrameTraversalIndex::order_dirty() const noexcept {
  return impl_->order_dirty;
}

void FrameTraversalIndex::Rebuild(const float root_scale,
                                  const float viewport_height) {
  impl_->root_scale = root_scale;
  impl_->viewport_height = viewport_height;
  if (!impl_->order_dirty) return;

  if (impl_->CanPatchIncrementally()) {

    impl_->EnsureEntriesRectsCurrent();
    impl_->PatchEntriesIncremental();
    ++impl_->metrics.incremental_patches;
  } else {
    impl_->BuildEntriesFull();
    impl_->entries_rects_generation = impl_->layout.RectsGeneration();
    ++impl_->metrics.full_rebuilds;
  }
  impl_->order_key_dirty.clear();
  impl_->inherited_dirty.clear();
  impl_->full_rebuild_required = false;

  const auto& entries = impl_->entries;
  std::vector<openwow::ui::UiPaintOrderNode> paint_nodes;
  paint_nodes.reserve(entries.size());
  for (const auto& entry : entries) {

    paint_nodes.push_back({
        .parent = entry.parent_index,
        .insertion_order = entry.insertion_order,
        .strata_order = entry.strata_order,
        .frame_level = entry.frame_level,
        .draw_layer = entry.draw_layer_order,
        .draw_sublevel = entry.draw_sublevel,
        .effective_depth = entry.effective_depth,
        .region = entry.region,
    });
  }

  const auto paint_order = openwow::ui::BuildUiPaintOrder(paint_nodes);

  impl_->render_snapshot.clear();
  impl_->render_snapshot.reserve(entries.size());
  for (std::size_t rank = 0; rank < paint_order.size(); ++rank) {
    const auto& entry = entries[paint_order[rank]];
    if (entry.traversal.effective_visible) {
      impl_->render_snapshot.push_back(entry.traversal);
    }
  }

  std::vector<std::size_t> input_order(paint_order.rbegin(),
                                       paint_order.rend());
  impl_->input_snapshot.clear();
  impl_->input_snapshot.reserve(entries.size());
  for (std::size_t rank = 0; rank < input_order.size(); ++rank) {
    const auto& entry = entries[input_order[rank]];
    if (entry.traversal.effective_visible) {
      impl_->input_snapshot.push_back(entry.traversal);
    }
  }
  ++impl_->metrics.snapshot_rebuilds;
  ++impl_->metrics.generation;
  impl_->metrics.traversal_entries = entries.size();
  impl_->order_dirty = false;
  impl_->hit_test_dirty = true;

  impl_->cached_rects_generation = impl_->layout.RectsGeneration();
}

void FrameTraversalIndex::RefreshRectCache() {

  const std::uint64_t generation = impl_->layout.RectsGeneration();
  if (impl_->cached_rects_generation == generation) return;
  for (auto& entry : impl_->render_snapshot) impl_->RefreshEntryRect(entry);
  for (auto& entry : impl_->input_snapshot) impl_->RefreshEntryRect(entry);
  impl_->cached_rects_generation = generation;
}

std::span<const FrameTraversalIndex::TraversalEntry>
FrameTraversalIndex::render_snapshot() const noexcept {
  return impl_->render_snapshot;
}

std::span<const FrameTraversalIndex::TraversalEntry>
FrameTraversalIndex::input_snapshot() const noexcept {
  return impl_->input_snapshot;
}

std::string FrameTraversalIndex::HitTarget(const float x, const float y,
                                           const float viewport_height) {
  impl_->viewport_height = viewport_height;
  impl_->metrics.last_hit_test_candidates = 0u;
  if (!std::isfinite(x) || !std::isfinite(y)) return {};
  if (impl_->hit_test_dirty) impl_->RebuildHitTest();

  static const std::vector<std::size_t> kEmptyBucket;
  const auto bucket = impl_->hit_test_buckets.find(
      CellKey(CellCoordinate(x), CellCoordinate(y)));
  const auto& local = bucket != impl_->hit_test_buckets.end()
                          ? bucket->second
                          : kEmptyBucket;
  std::size_t local_cursor = 0u;
  std::size_t global_cursor = 0u;
  while (local_cursor < local.size() ||
         global_cursor < impl_->hit_test_global_records.size()) {
    std::size_t record_index;
    if (global_cursor >= impl_->hit_test_global_records.size() ||
        (local_cursor < local.size() &&
         local[local_cursor] < impl_->hit_test_global_records[global_cursor])) {
      record_index = local[local_cursor++];
    } else {
      record_index = impl_->hit_test_global_records[global_cursor++];
    }
    if (record_index >= impl_->hit_test_records.size()) continue;
    const auto& record = impl_->hit_test_records[record_index];
    ++impl_->metrics.last_hit_test_candidates;
    if (!openwow::ui::RectContainsPointInclusive(
            record.left, record.top, record.right, record.bottom, x, y)) {
      continue;
    }
    if (!record.mouse_target_key.empty()) return record.mouse_target_key;
  }
  return {};
}

bool FrameTraversalIndex::Contains(const std::string_view key, const float x,
                                   const float y, const float viewport_height,
                                   const bool apply_scroll_clip) const {
  impl_->viewport_height = viewport_height;
  const auto entry = std::find_if(
      impl_->input_snapshot.begin(), impl_->input_snapshot.end(),
      [key](const TraversalEntry& candidate) { return candidate.key == key; });
  if (entry == impl_->input_snapshot.end()) return false;
  Impl::HitTestRecord rect;
  return impl_->BuildHitRect(*entry, apply_scroll_clip, &rect) &&
         openwow::ui::RectContainsPointInclusive(rect.left, rect.top, rect.right,
                                                 rect.bottom, x, y);
}

bool FrameTraversalIndex::IsEffectivelyVisible(const std::string_view key) const {

  return IsEffectivelyVisible(impl_->frames.HandleOf(key));
}

bool FrameTraversalIndex::IsEffectivelyVisible(const std::uint64_t handle) const {
  if (!impl_->order_dirty) {
    const auto visible = impl_->visibility_index.find(handle);
    return visible != impl_->visibility_index.end() && visible->second;
  }
  const auto* frame = impl_->frames.FindFrame(handle);
  return frame != nullptr &&
         impl_->ComputeHierarchy(handle, *frame).effective_visible;
}

std::vector<FrameTraversalIndex::FrameStackEntry>
FrameTraversalIndex::FrameStackAt(const float x, const float y,
                                  const bool show_hidden,
                                  const float viewport_height) const {
  impl_->viewport_height = viewport_height;
  std::vector<FrameStackEntry> result;
  result.reserve(impl_->frames.size());
  for (const Impl::FrameHandle handle : impl_->frames.registration_handles()) {
    const auto* frame = impl_->frames.FindFrame(handle);
    if (frame == nullptr) continue;
    const auto hierarchy = impl_->ComputeHierarchy(handle, *frame);
    if (!show_hidden && !frame->visible) continue;
    const auto entry = impl_->BuildEntry(handle, *frame, hierarchy);
    Impl::HitTestRecord rect;
    if (!impl_->BuildHitRect(entry, false, &rect) ||
        !openwow::ui::RectContainsPointInclusive(
            rect.left, rect.top, rect.right, rect.bottom, x, y)) {
      continue;
    }
    result.push_back({
        .key = frame->name,
        .lua_ref = entry.lua_ref,
        .strata = FrameStackStrata(frame->frame_strata),
        .level = frame->frame_level,
        .mouse_enabled = entry.uses_mouse,
        .visible = frame->visible,
    });
  }
  return result;
}

const FrameTraversalIndex::Metrics& FrameTraversalIndex::metrics() const noexcept {
  return impl_->metrics;
}

void FrameTraversalIndex::ResetMetrics() noexcept {
  const std::uint64_t generation = impl_->metrics.generation;
  impl_->metrics = {};
  impl_->metrics.generation = generation;
}

}

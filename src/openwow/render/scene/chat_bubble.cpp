#include "openwow/render/scene/chat_bubble.h"
#include "openwow/render/backend/bgfx/bgfx_texture_lease.h"

#include "openwow/render/scene/object_renderer.h"
#include "openwow/render/api/math/render_matrix_math.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>

namespace openwow::render {
namespace {

constexpr char kBackgroundTexturePath[] =
    "Interface\\Tooltips\\ChatBubble-Background.blp";
constexpr char kBackdropTexturePath[] =
    "Interface\\Tooltips\\ChatBubble-Backdrop.blp";
constexpr char kTailTexturePath[] =
    "Interface\\Tooltips\\ChatBubble-Tail.blp";
constexpr std::uint32_t kUnmountedHeadAttachmentLookup = 0x12u;
constexpr std::uint32_t kMountedHeadAttachmentLookup = 0x1Du;
constexpr std::uint32_t kFallbackFillArgb = 0xE6FFFFFFu;
constexpr std::uint32_t kFallbackBorderArgb = 0xCC404040u;

enum EdgeStripCell : int {
  kCellLeft = 0,
  kCellRight = 1,
  kCellTop = 2,
  kCellBottom = 3,
  kCellTopLeft = 4,
  kCellTopRight = 5,
  kCellBottomLeft = 6,
  kCellBottomRight = 7,
};
constexpr float kEdgeStripCellUvWidth = 32.0f / 256.0f;
constexpr float kEdgeStripSeamU = 2.0f / 256.0f;
constexpr float kEdgeStripCellFarU = 30.0f / 256.0f;
constexpr float kEdgeStripSeamV = 2.0f / 32.0f;
constexpr float kEdgeStripFarV = 30.0f / 32.0f;

constexpr float CellU0(const EdgeStripCell cell) {
  return static_cast<float>(cell) * kEdgeStripCellUvWidth + kEdgeStripSeamU;
}
constexpr float CellU1(const EdgeStripCell cell) {
  return static_cast<float>(cell) * kEdgeStripCellUvWidth + kEdgeStripCellFarU;
}

constexpr std::uint64_t kWrapSampler = 0u;

std::uint32_t ModulateArgbAlpha(const std::uint32_t argb,
                                const float alpha) {
  const auto source_alpha = static_cast<float>((argb >> 24u) & 0xFFu);
  const auto output_alpha = static_cast<std::uint32_t>(std::clamp(
      std::lround(source_alpha * std::clamp(alpha, 0.0f, 1.0f)), 0l, 255l));
  return (output_alpha << 24u) | (argb & 0x00FFFFFFu);
}

std::size_t NextUtf8Boundary(const std::string_view text,
                             const std::size_t offset) {
  if (offset >= text.size()) {
    return text.size();
  }
  const auto lead = static_cast<unsigned char>(text[offset]);
  std::size_t length = 1u;
  if ((lead & 0xE0u) == 0xC0u) {
    length = 2u;
  } else if ((lead & 0xF0u) == 0xE0u) {
    length = 3u;
  } else if ((lead & 0xF8u) == 0xF0u) {
    length = 4u;
  }
  return std::min(text.size(), offset + length);
}

bool DrawOrderLess(const ChatBubbleDrawInfo& lhs,
                   const ChatBubbleDrawInfo& rhs) {
  if (lhs.projected_depth != rhs.projected_depth) {
    return lhs.projected_depth < rhs.projected_depth;
  }
  if (lhs.bubble.insertionOrder != rhs.bubble.insertionOrder) {
    return lhs.bubble.insertionOrder < rhs.bubble.insertionOrder;
  }
  return lhs.bubble.id < rhs.bubble.id;
}

}

bool ChatBubblePresenter::Initialize() {
  if (initialized_) {
    return true;
  }

  initialized_ = true;

  text_renderer_.SetQuadBatchingEnabled(true);
  EnsureFont();
  EnsureTextures();
  diagnostics::Log(diagnostics::LogLevel::kInfo, "ChatBubblePresenter: initialized");
  return true;
}

void ChatBubblePresenter::Shutdown() {
  if (!initialized_) {
    return;
  }

  prepared_layouts_.clear();
  background_lease_ = {};
  backdrop_lease_ = {};
  tail_lease_ = {};
  text_renderer_.Shutdown();
  last_rendered_bubble_count_ = 0u;
  last_render_generation_ = 0u;
  initialized_ = false;
  diagnostics::Log(diagnostics::LogLevel::kInfo, "ChatBubblePresenter: shutdown");
}

void ChatBubblePresenter::SetFileLoader(FileLoader loader) {
  file_loader_ = std::move(loader);
  if (!initialized_) {
    return;
  }

  prepared_layouts_.clear();
  background_lease_ = {};
  backdrop_lease_ = {};
  tail_lease_ = {};
  text_renderer_.Shutdown();
  EnsureFont();
  EnsureTextures();
}

bool ChatBubblePresenter::stock_visuals_ready() const noexcept {
  return initialized_ && text_renderer_.is_ready() &&
         background_lease_.valid() && backdrop_lease_.valid() &&
         tail_lease_.valid();
}

std::vector<ChatBubbleDrawInfo> ChatBubblePresenter::BuildDrawList(
    const std::span<const game::ChatBubble> bubbles,
    const game::ObjectPresentationSnapshot& objects,
    const ObjectRenderer& object_renderer,
    const float screen_w, const float screen_h, const float* view_mtx,
    const float* proj_mtx) const {
  std::vector<ChatBubbleDrawInfo> draws;
  if (objects.local_player.guid.IsEmpty() || screen_w <= 0.0f ||
      screen_h <= 0.0f || view_mtx == nullptr || proj_mtx == nullptr) {
    return draws;
  }

  draws.reserve(bubbles.size());
  for (const auto& bubble : bubbles) {
    if (!std::isfinite(bubble.alpha) || bubble.alpha <= 0.0f) {
      continue;
    }

    const auto owner = std::lower_bound(
        objects.active.begin(), objects.active.end(),
        bubble.unitGuid.GetRawValue(),
        [](const game::ObjectPresentationRecord& record,
           const std::uint64_t raw_guid) {
          return record.handle.guid.GetRawValue() < raw_guid;
        });
    if (owner == objects.active.end() ||
        owner->handle.guid != bubble.unitGuid) {
      continue;
    }

    const auto head_position = object_renderer.QueryModelAttachmentPosition(
        owner->handle,
        owner->mounted ? kMountedHeadAttachmentLookup
                       : kUnmountedHeadAttachmentLookup);
    if (!head_position.has_value()) {
      continue;
    }

    ChatBubbleDrawInfo draw;

    draw.world_x = owner->x;
    draw.world_y = owner->y;
    draw.world_z = (*head_position)[2] + kHeadClearance;
    if (!std::isfinite(draw.world_x) || !std::isfinite(draw.world_y) ||
        !std::isfinite(draw.world_z) ||
        !ProjectRetailAnchor(draw.world_x, draw.world_y, draw.world_z,
                             screen_w, screen_h, view_mtx, proj_mtx,
                             &draw.screen_x, &draw.screen_y,
                             &draw.projected_depth)) {
      continue;
    }
    draw.bubble = bubble;
    draws.push_back(std::move(draw));
  }

  std::stable_sort(draws.begin(), draws.end(), DrawOrderLess);
  for (std::size_t index = 0u; index < draws.size(); ++index) {
    draws[index].frame_level = static_cast<std::uint32_t>(index) + 2u;
  }
  return draws;
}

void ChatBubblePresenter::Render(
    const std::uint8_t view_id, const float screen_w, const float screen_h,
    const float* view_mtx, const float* proj_mtx,
    const std::span<const game::ChatBubble> bubbles,
    const game::ObjectPresentationSnapshot& objects,
    const ObjectRenderer& object_renderer,
    const std::uint64_t compositor_generation) {
  last_render_generation_ = compositor_generation;
  last_rendered_bubble_count_ = 0u;
  if (!initialized_ || !text_renderer_.is_ready() ||
      screen_w <= 0.0f || screen_h <= 0.0f) {
    return;
  }

  const auto draws = BuildDrawList(bubbles, objects, object_renderer, screen_w,
                                   screen_h, view_mtx, proj_mtx);
  DiscardInactiveLayouts(draws);
  if (draws.empty()) {
    return;
  }

  bgfx::setViewMode(view_id, bgfx::ViewMode::Sequential);
  text_renderer_.BeginFrame(view_id, screen_w, screen_h);
  bgfx::setViewClear(view_id, BGFX_CLEAR_NONE);
  bgfx::touch(view_id);

  const float ui_scale = screen_h / kStockUiCoordinateHeight;
  const float edge_size = kEdgeSize * ui_scale;
  const float background_tile_size = kBackgroundTileSize * ui_scale;
  const float background_inset = kBackgroundInset * ui_scale;
  const float tail_size = kTailSize * ui_scale;
  const float tail_overlap = kTailOverlap * ui_scale;
  const float minimum_text_width = kMinimumTextWidth * ui_scale;

  const float diagonal = std::sqrt(screen_w * screen_w + screen_h * screen_h);
  const float diag_scale = diagonal / kUiCoordinateDiagonalReference;
  const float text_padding = kTextPadding * diag_scale;

  const float maximum_text_width = std::max(
      minimum_text_width, kMaxTextWidthReferencePixels * ui_scale);
  for (const auto& draw : draws) {
    const auto& layout =
        PrepareLayout(draw.bubble, maximum_text_width, diag_scale);
    if (layout.lines.empty()) {
      continue;
    }

    const float box_width =
        std::max(minimum_text_width, layout.text_width) + 2.0f * text_padding;
    const float box_height = layout.text_height + 2.0f * text_padding;
    const float box_x = draw.screen_x - box_width * 0.5f;

    const float tail_descent = tail_size - tail_overlap;
    const float box_y = draw.screen_y - box_height - tail_descent;
    if (box_x + box_width < 0.0f || box_x > screen_w ||
        box_y + box_height + tail_descent < 0.0f || box_y > screen_h) {
      continue;
    }

    const float alpha = std::clamp(draw.bubble.alpha, 0.0f, 1.0f);
    const std::uint32_t white_tint = ModulateArgbAlpha(0xFFFFFFFFu, alpha);

    if (background_lease_.valid()) {

      text_renderer_.DrawTexturedQuad(
          view_id, box_x + background_inset, box_y + background_inset,
          box_width - 2.0f * background_inset,
          box_height - 2.0f * background_inset,
          BgfxTextureLeaseAccess::Get(background_lease_), white_tint,
          kWrapSampler, 0.0f, 0.0f, box_width / background_tile_size,
          box_height / background_tile_size);
    } else {
      text_renderer_.DrawQuad(view_id, box_x, box_y, box_width, box_height,
                              ModulateArgbAlpha(kFallbackFillArgb, alpha));
    }

    if (backdrop_lease_.valid()) {
      const auto backdrop = BgfxTextureLeaseAccess::Get(backdrop_lease_);
      const float side_height = box_height - 2.0f * edge_size;
      const float side_width = box_width - 2.0f * edge_size;

      const float vertical_repeat =
          std::max(0.0f, box_height / edge_size - 2.0f);
      const float horizontal_repeat =
          std::max(0.0f, box_width / edge_size - 2.0f);

      text_renderer_.DrawTexturedQuad(
          view_id, box_x, box_y + edge_size, edge_size, side_height, backdrop,
          white_tint, kWrapSampler, CellU0(kCellLeft), kEdgeStripSeamV,
          CellU1(kCellLeft), vertical_repeat - kEdgeStripSeamV);
      text_renderer_.DrawTexturedQuad(
          view_id, box_x + box_width - edge_size, box_y + edge_size,
          edge_size, side_height, backdrop, white_tint, kWrapSampler,
          CellU0(kCellRight), kEdgeStripSeamV, CellU1(kCellRight),
          vertical_repeat - kEdgeStripSeamV);

      text_renderer_.DrawTexturedQuadCorners(
          view_id, box_x + edge_size, box_y, side_width, edge_size, backdrop,
          {CellU0(kCellTop), horizontal_repeat - kEdgeStripSeamV,
           CellU1(kCellTop), horizontal_repeat - kEdgeStripSeamV,
           CellU0(kCellTop), kEdgeStripSeamV, CellU1(kCellTop),
           kEdgeStripSeamV},
          white_tint, kWrapSampler);
      text_renderer_.DrawTexturedQuadCorners(
          view_id, box_x + edge_size, box_y + box_height - edge_size,
          side_width, edge_size, backdrop,
          {CellU0(kCellBottom), horizontal_repeat - kEdgeStripSeamV,
           CellU1(kCellBottom), horizontal_repeat - kEdgeStripSeamV,
           CellU0(kCellBottom), kEdgeStripSeamV, CellU1(kCellBottom),
           kEdgeStripSeamV},
          white_tint, kWrapSampler);

      text_renderer_.DrawTexturedQuad(
          view_id, box_x, box_y, edge_size, edge_size, backdrop, white_tint,
          BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, CellU0(kCellTopLeft),
          kEdgeStripSeamV, CellU1(kCellTopLeft), kEdgeStripFarV);
      text_renderer_.DrawTexturedQuad(
          view_id, box_x + box_width - edge_size, box_y, edge_size, edge_size,
          backdrop, white_tint, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
          CellU0(kCellTopRight), kEdgeStripSeamV, CellU1(kCellTopRight),
          kEdgeStripFarV);
      text_renderer_.DrawTexturedQuad(
          view_id, box_x, box_y + box_height - edge_size, edge_size,
          edge_size, backdrop, white_tint,
          BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
          CellU0(kCellBottomLeft), kEdgeStripSeamV, CellU1(kCellBottomLeft),
          kEdgeStripFarV);
      text_renderer_.DrawTexturedQuad(
          view_id, box_x + box_width - edge_size,
          box_y + box_height - edge_size, edge_size, edge_size, backdrop,
          white_tint, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
          CellU0(kCellBottomRight), kEdgeStripSeamV,
          CellU1(kCellBottomRight), kEdgeStripFarV);
    } else {
      const auto border = ModulateArgbAlpha(kFallbackBorderArgb, alpha);
      text_renderer_.DrawQuad(view_id, box_x, box_y, box_width, 1.0f, border);
      text_renderer_.DrawQuad(view_id, box_x, box_y + box_height - 1.0f,
                              box_width, 1.0f, border);
      text_renderer_.DrawQuad(view_id, box_x, box_y, 1.0f, box_height, border);
      text_renderer_.DrawQuad(view_id, box_x + box_width - 1.0f, box_y, 1.0f,
                              box_height, border);
    }

    if (tail_lease_.valid()) {

      text_renderer_.DrawTexturedQuad(
          view_id, draw.screen_x - tail_size,
          box_y + box_height - tail_overlap, tail_size, tail_size,
          BgfxTextureLeaseAccess::Get(tail_lease_), white_tint);
    }

    float text_y = box_y + text_padding;
    for (std::size_t line = 0u; line < layout.lines.size(); ++line) {
      const float text_x =
          draw.screen_x - layout.line_widths[line] * 0.5f;
      (void)text_renderer_.DrawTextAlpha(
          view_id, text_x, text_y, layout.lines[line], draw.bubble.color,
          alpha, diag_scale);
      text_y += text_renderer_.line_height() * diag_scale;
    }
    ++last_rendered_bubble_count_;
  }

  text_renderer_.FlushQuadBatch();
}

bool ChatBubblePresenter::ProjectRetailAnchor(
    const float world_x, const float world_y, const float world_z,
    const float screen_w, const float screen_h, const float* view_mtx,
    const float* proj_mtx, float* screen_x, float* screen_y,
    float* projected_depth) {
  if (screen_x == nullptr || screen_y == nullptr ||
      projected_depth == nullptr || view_mtx == nullptr ||
      proj_mtx == nullptr) {
    return false;
  }

  const RenderVec4 world{world_x, world_y, world_z, 1.0f};
  const RenderVec4 view = TransformRowVector4x4(
      world, RenderMatrix4x4View{view_mtx, 16u});
  const RenderVec4 clip = TransformRowVector4x4(
      view, RenderMatrix4x4View{proj_mtx, 16u});
  if (!std::isfinite(clip[0]) || !std::isfinite(clip[1]) ||
      !std::isfinite(clip[2]) || !std::isfinite(clip[3]) ||
      clip[3] <= std::numeric_limits<float>::epsilon()) {
    return false;
  }

  const float inverse_w = 1.0f / clip[3];

  *screen_x = std::ceil((clip[0] * inverse_w * 0.5f + 0.5f) * screen_w);
  *screen_y = std::ceil((0.5f - clip[1] * inverse_w * 0.5f) * screen_h);
  *projected_depth = clip[2];
  return std::isfinite(*screen_x) && std::isfinite(*screen_y);
}

std::vector<std::string> ChatBubblePresenter::WrapText(
    const std::string& text, const float maximum_width,
    const float text_scale) const {
  std::vector<std::string> lines;
  if (text.empty()) {
    return lines;
  }

  const auto push_wrapped_segment = [&](const std::string_view segment) {
    if (segment.empty()) {
      lines.emplace_back();
      return;
    }

    std::size_t line_start = 0u;
    std::size_t cursor = 0u;
    std::size_t last_break_start = std::string_view::npos;
    std::size_t last_break_end = std::string_view::npos;
    while (cursor < segment.size()) {
      const std::size_t next = NextUtf8Boundary(segment, cursor);
      const bool is_break =
          segment[cursor] == ' ' || segment[cursor] == '\t';
      const std::string candidate(
          segment.substr(line_start, next - line_start));
      if (cursor == line_start ||
          text_renderer_.Measure(candidate, text_scale).width <= maximum_width) {
        if (is_break) {
          last_break_start = cursor;
          last_break_end = next;
        }
        cursor = next;
        continue;
      }

      std::size_t line_end = cursor;
      std::size_t next_line_start = cursor;
      if (is_break) {
        next_line_start = next;
      } else if (last_break_end != std::string_view::npos) {
        line_end = last_break_start;
        next_line_start = last_break_end;
      }

      while (line_end > line_start &&
             (segment[line_end - 1u] == ' ' ||
              segment[line_end - 1u] == '\t')) {
        --line_end;
      }
      while (next_line_start < segment.size() &&
             (segment[next_line_start] == ' ' ||
              segment[next_line_start] == '\t')) {
        ++next_line_start;
      }
      if (line_end > line_start) {
        lines.emplace_back(segment.substr(line_start, line_end - line_start));
      }

      line_start = next_line_start;
      cursor = line_start;
      last_break_start = std::string_view::npos;
      last_break_end = std::string_view::npos;
    }

    if (line_start < segment.size()) {
      lines.emplace_back(segment.substr(line_start));
    }
  };

  std::size_t segment_start = 0u;
  while (segment_start <= text.size()) {
    const std::size_t newline = text.find('\n', segment_start);
    const std::size_t segment_end =
        newline == std::string::npos ? text.size() : newline;
    push_wrapped_segment(std::string_view(text).substr(
        segment_start, segment_end - segment_start));
    if (newline == std::string::npos) {
      break;
    }
    segment_start = newline + 1u;
  }
  return lines;
}

const ChatBubblePresenter::PreparedLayout&
ChatBubblePresenter::PrepareLayout(const game::ChatBubble& bubble,
                                   const float maximum_text_width,
                                   const float text_scale) {
  auto& prepared = prepared_layouts_[bubble.id];
  if (prepared.insertion_order == bubble.insertionOrder &&
      prepared.text == bubble.text &&
      prepared.maximum_text_width == maximum_text_width &&
      prepared.text_scale == text_scale) {
    return prepared;
  }

  prepared = {};
  prepared.insertion_order = bubble.insertionOrder;
  prepared.text = bubble.text;
  prepared.maximum_text_width = maximum_text_width;
  prepared.text_scale = text_scale;
  prepared.lines = WrapText(bubble.text, maximum_text_width, text_scale);
  prepared.line_widths.reserve(prepared.lines.size());
  for (const auto& line : prepared.lines) {
    const float line_width = text_renderer_.Measure(line, text_scale).width;
    prepared.line_widths.push_back(line_width);
    prepared.text_width = std::max(prepared.text_width, line_width);
  }
  prepared.text_height = text_renderer_.line_height() * text_scale *
                         static_cast<float>(prepared.lines.size());
  return prepared;
}

void ChatBubblePresenter::DiscardInactiveLayouts(
    const std::vector<ChatBubbleDrawInfo>& draws) {
  std::erase_if(prepared_layouts_, [&](const auto& entry) {
    return std::none_of(draws.begin(), draws.end(), [&](const auto& draw) {
      return draw.bubble.id == entry.first;
    });
  });
}

void ChatBubblePresenter::EnsureFont() {
  if (text_renderer_.is_ready()) {
    return;
  }

  if (font_path_.empty()) {
    return;
  }

  bool loaded = false;
  if (file_loader_) {
    auto font_bytes = file_loader_(font_path_);
    if (!font_bytes.empty()) {
      loaded = text_renderer_.InitFromMemory(font_path_, std::move(font_bytes),
                                             kNameplateFontHeight);
    }
  }
  if (!loaded) {
    loaded = text_renderer_.InitFromVirtualPath(font_path_, kNameplateFontHeight);
  }
  if (!loaded) {
    diagnostics::Log(diagnostics::LogLevel::kWarn,
              "ChatBubblePresenter: NAMEPLATE_FONT initialization failed");
  }
}

void ChatBubblePresenter::EnsureTextures() {
  auto& textures = texture_manager_;
  if (!background_lease_.valid()) {
    background_lease_ = textures.AcquireTextureStrict(kBackgroundTexturePath);
  }
  if (!backdrop_lease_.valid()) {
    backdrop_lease_ = textures.AcquireTextureStrict(kBackdropTexturePath);
  }
  if (!tail_lease_.valid()) {
    tail_lease_ = textures.AcquireTextureStrict(kTailTexturePath);
  }
}

}

#include "openwow/ui/widgets/simple_render.h"

#include <array>
#include <cmath>

namespace openwow::ui::widgets {
namespace {

FramePoint ResolveTextureAnchor(std::uint32_t justify, float left,
                                float right, float top, float bottom,
                                float rendered_height, float offset_x,
                                float offset_y, float& x, float& y) {
  int column{};
  switch (justify & 0x7u) {
    case 1u:
      x = left;
      break;
    case 2u:
      column = 1;
      x = (left + right) * 0.5f;
      break;
    case 4u:
      column = 2;
      x = right;
      break;
    default:
      x = left;
      break;
  }

  int row{};
  switch (justify & 0x38u) {
    case 8u:
      y = top;
      break;
    case 16u:
      row = 1;
      y = (rendered_height + top + bottom) * 0.5f;
      break;
    case 32u:
      row = 2;
      y = rendered_height + bottom;
      break;
    default:
      y = top;
      break;
  }
  x += offset_x;
  y += offset_y;
  return static_cast<FramePoint>(row * 3 + column);
}

}

void CSimpleRender::AddToRenderBatch(BatchSink& sink) const {
  if (!fontFace_ || text_.empty() || !textLayout_ ||
      !sink.GetClipRect().IntersectsNormalizedUnitSquare()) {
    return;
  }
  sink.AddText(*this, text_);
  embeddedTextures_.ForEach([&sink](const CSimpleEmbeddedTexture& node) {
    const auto* texture = node.GetTexture();
    if (texture != nullptr && texture->HasRenderableContent()) {
      sink.AddEmbeddedTexture(*texture);
    }
  });
}

void CSimpleRender::CreateTextureNodes(const float rendered_text_height) {
  if (!textLayout_) return;
  const float scale = scaleFactor_ > 0.0f ? scaleFactor_ : 1.0f;
  const float inverse_scale = 1.0f / scale;
  const float alpha = ColorComponent(RenderableTextAlpha());

  for (const auto& element : textLayout_->elements) {
    const auto& token = textLayout_->tokens[element.token_index];
    if (token.kind !=
        openwow::render::text::FormattedTokenKind::InlineImage) {
      continue;
    }

    auto texture = std::make_unique<CSimpleTexture>();
    texture->SetDrawLayer(DrawLayer::Artwork);
    const float left = element.x * inverse_scale;
    const float top = element.y * inverse_scale;
    const float width = element.width * inverse_scale;
    const float height = element.height * inverse_scale;
    float anchor_x{};
    float anchor_y{};
    const FramePoint point = ResolveTextureAnchor(
        justifyFlags_, left, left + width, top, top + height,
        rendered_text_height, token.image.x_offset.value_or(0.0f),
        token.image.y_offset.value_or(0.0f), anchor_x, anchor_y);

    texture->SetPoint({.point = point,
                       .relativeTo = layoutOwner_,
                       .relativePoint = point,
                       .offsetX = anchor_x,
                       .offsetY = anchor_y});
    texture->SetWidth(width);
    texture->SetHeight(height);
    texture->SetVertexColor(1.0f, 1.0f, 1.0f, alpha);

    if (token.image.texture_width.value_or(0.0f) > 0.0f &&
        token.image.texture_height.value_or(0.0f) > 0.0f &&
        token.image.left && token.image.top && token.image.right &&
        token.image.bottom) {
      const float texture_width = *token.image.texture_width;
      const float texture_height = *token.image.texture_height;
      texture->SetTexCoord(*token.image.top / texture_height,
                           *token.image.bottom / texture_height,
                           *token.image.left / texture_width,
                           *token.image.right / texture_width);
    }
    texture->SetTexture(token.image.path);
    texture->SetShown(!token.image.path.empty());
    embeddedTextures_.Add(std::move(texture));
  }
}

void CSimpleRender::SetTextGeometryInputs(
    const float base_x, const float base_y, const float justify_offset_x,
    const float justify_offset_y, const float depth) noexcept {
  textGeometryInputs_ = {
      base_x, base_y, justify_offset_x, justify_offset_y, depth};
  textPosition_ = BuildCurrentTextPosition();
}

void CSimpleRender::SetTextDepth(const float depth) noexcept {
  textGeometryInputs_.depth = depth;
  textPosition_ = BuildCurrentTextPosition();
}

CSimpleRender::TextPosition
CSimpleRender::BuildCurrentTextPosition() const noexcept {
  return {
      .x = textGeometryInputs_.baseX +
           textGeometryInputs_.justifyOffsetX * scaleFactor_,
      .y = textGeometryInputs_.baseY +
           textGeometryInputs_.justifyOffsetY * scaleFactor_,
      .z = textGeometryInputs_.depth,
  };
}

bool CSimpleRender::OnOwnerRectChanged(const ScreenRect& old_rect,
                                       const ScreenRect& new_rect) {
  constexpr float epsilon = 0.00001f;
  const bool dimensions_changed =
      std::fabs(old_rect.Width() - new_rect.Width()) >= epsilon ||
      std::fabs(old_rect.Height() - new_rect.Height()) >= epsilon;
  textGeometryInputs_.baseX = new_rect.top;
  textGeometryInputs_.baseY = new_rect.left;
  textPosition_ = BuildCurrentTextPosition();
  if (dimensions_changed) Invalidate();
  return old_rect.IsFullyOffScreen() != new_rect.IsFullyOffScreen();
}

}


#include "openwow/ui/widgets/simple_color_select.h"

#include "openwow/ui/widgets/simple_texture.h"
#include "openwow/ui/widgets/widget_xml_helpers.h"
#include "openwow/ui/xml/frame_xml_parser.h"
#include "openwow/foundation/text/ascii.h"

namespace openwow::ui::widgets {

static CSimpleTexture* CreateTextureFromXMLChild(
    const openwow::ui::xml::XMLNode &node,
    CSimpleFrame *parent,
    openwow::ui::xml::ErrorContext *error_handler) {
  auto* texture = new CSimpleTexture();
  texture->SetParent(parent);
  texture->SetDrawLayer(DrawLayer::Artwork);

  openwow::ui::xml::XMLFrameDef texture_def;
  texture_def.type = node.tag;
  texture_def.attributes = node.attributes;
  texture_def.raw_node = node;

  texture->LoadXML(texture_def, error_handler);
  return texture;
}

void CSimpleColorSelect::LoadXML(const void *xmlNode, void *errorHandler) {
  const auto *frame_def =
      static_cast<const openwow::ui::xml::XMLFrameDef *>(xmlNode);
  auto *error_handler =
      static_cast<openwow::ui::xml::ErrorContext *>(errorHandler);
  if (frame_def == nullptr) {
    return;
  }

  CSimpleFrame::LoadXML(xmlNode, errorHandler);

  for (const auto &child : frame_def->raw_node.children) {

    if (openwow::text::EqualsIgnoreCaseAscii(child.tag,
                                              "ColorWheelTexture")) {
      CSimpleTexture *texture =
          CreateTextureFromXMLChild(child, this, error_handler);
      if (texture != wheelTexture_) {
        delete wheelTexture_;

        if (texture != nullptr) {
          texture->SetParent(this);
          texture->SetDrawLayer(DrawLayer::Artwork);
          texture->Show();
        }

        wheelTexture_ = texture;
      }
      continue;
    }

    if (openwow::text::EqualsIgnoreCaseAscii(child.tag,
                                              "ColorWheelThumbTexture")) {
      CSimpleTexture *texture =
          CreateTextureFromXMLChild(child, this, error_handler);
      if (texture != wheelThumbTexture_) {
        delete wheelThumbTexture_;

        if (texture != nullptr) {
          texture->SetParent(this);
          texture->SetDrawLayer(DrawLayer::Overlay);
          texture->Show();
          texture->ClearAllPoints();
        }

        wheelThumbTexture_ = texture;
        UpdateWheelThumbPosition();
      }
      continue;
    }

    if (openwow::text::EqualsIgnoreCaseAscii(child.tag,
                                              "ColorValueTexture")) {
      CSimpleTexture *texture =
          CreateTextureFromXMLChild(child, this, error_handler);
      SetValueTexture(texture);
      continue;
    }

    if (openwow::text::EqualsIgnoreCaseAscii(child.tag,
                                              "ColorValueThumbTexture")) {
      CSimpleTexture *texture =
          CreateTextureFromXMLChild(child, this, error_handler);
      if (texture != valueThumbTexture_) {
        delete valueThumbTexture_;

        if (texture != nullptr) {
          texture->SetParent(this);
          texture->SetDrawLayer(DrawLayer::Overlay);
          texture->Show();
          texture->ClearAllPoints();
        }

        valueThumbTexture_ = texture;
        UpdateValueThumbPosition();
      }
      continue;
    }
  }
}

void CSimpleColorSelect::SetValueTexture(CSimpleTexture* tex) noexcept {
  if (tex == valueTexture_) {
    return;
  }

  delete valueTexture_;

  if (tex != nullptr) {

    const TextureGradientColor kBlack{0.0f, 0.0f, 0.0f, 1.0f};
    const TextureGradientColor kWhite{1.0f, 1.0f, 1.0f, 1.0f};
    tex->SetGradient(TextureGradientOrientation::Vertical, kBlack, kWhite);

    tex->SetParent(this);
    tex->SetDrawLayer(DrawLayer::Artwork);
    tex->Show();
  }

  valueTexture_ = tex;

  UpdatePackedColorAndRefreshValueBar();
}

}

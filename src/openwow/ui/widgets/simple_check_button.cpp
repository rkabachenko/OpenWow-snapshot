
#include "openwow/ui/widgets/simple_button.h"
#include "openwow/ui/widgets/simple_check_button.h"

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

CSimpleCheckButton::~CSimpleCheckButton() {
  delete checkedTexture_;
  checkedTexture_ = nullptr;
  delete disabledCheckedTexture_;
  disabledCheckedTexture_ = nullptr;
}

void CSimpleCheckButton::LoadXML(const void *xmlNode, void *errorHandler) {
  const auto *frame_def =
      static_cast<const openwow::ui::xml::XMLFrameDef *>(xmlNode);
  auto *error_handler =
      static_cast<openwow::ui::xml::ErrorContext *>(errorHandler);
  if (frame_def == nullptr) {
    return;
  }

  CSimpleButton::LoadXML(xmlNode, errorHandler);

  if (const char *checked_attr = FindAttributeValue(*frame_def, "checked");
      checked_attr != nullptr && *checked_attr != '\0') {
    const bool newChecked =
        ScriptParseBoolStringOrDefault(checked_attr, false);
    const bool prev = checked_;
    checked_ = newChecked;
    UpdateCheckedTextureVisibility(prev, false);
  }

  for (const auto &child : frame_def->raw_node.children) {
    if (openwow::text::EqualsIgnoreCaseAscii(child.tag, "CheckedTexture")) {
      CSimpleTexture *texture =
          CreateTextureFromXMLChild(child, this, error_handler);
      if (texture != checkedTexture_) {
        delete checkedTexture_;

        if (texture != nullptr) {

          texture->SetParent(this);
          texture->SetDrawLayer(DrawLayer::Overlay);
        }

        checkedTexture_ = texture;

        UpdateCheckedTextureVisibility(checked_, true);
      }
      continue;
    }

    if (openwow::text::EqualsIgnoreCaseAscii(child.tag,
                                              "DisabledCheckedTexture")) {
      CSimpleTexture *texture =
          CreateTextureFromXMLChild(child, this, error_handler);
      if (texture != disabledCheckedTexture_) {
        delete disabledCheckedTexture_;

        if (texture != nullptr) {
          texture->SetParent(this);
          texture->SetDrawLayer(DrawLayer::Overlay);
        }

        disabledCheckedTexture_ = texture;

        UpdateCheckedTextureVisibility(checked_, true);
      }
      continue;
    }
  }
}

void CSimpleCheckButton::SetChecked(bool c) noexcept {
  const bool prev = checked_;
  checked_ = c;
  UpdateCheckedTextureVisibility(prev, true);
}

void CSimpleCheckButton::SetCheckedTexture(CSimpleTexture* texture) {
  if (texture == checkedTexture_) {
    return;
  }

  delete checkedTexture_;

  if (texture != nullptr) {
    texture->SetParent(this);
    texture->SetDrawLayer(DrawLayer::Overlay);
  }

  const bool prevChecked = checked_;
  checkedTexture_ = texture;
  UpdateCheckedTextureVisibility(prevChecked, true);
}

bool CSimpleCheckButton::SetCheckedTextureFromFile(const char* filePath) {

  if (checkedTexture_ != nullptr) {
    checkedTexture_->SetTexture(filePath ? filePath : "");
    return true;
  }

  auto* tex = new CSimpleTexture();
  tex->SetDrawLayer(DrawLayer::Artwork);

  if (!tex->SetTexture(filePath ? filePath : "")) {
    delete tex;
    return false;
  }

  tex->SetAllPoints(this);

  tex->SetBlendMode(BlendMode::Add);

  SetCheckedTexture(tex);
  return true;
}

void CSimpleCheckButton::SetDisabledCheckedTexture(CSimpleTexture* texture) {
  if (texture == disabledCheckedTexture_) {
    return;
  }

  delete disabledCheckedTexture_;

  if (texture != nullptr) {
    texture->SetParent(this);
    texture->SetDrawLayer(DrawLayer::Overlay);
  }

  const bool prevChecked = checked_;
  disabledCheckedTexture_ = texture;
  UpdateCheckedTextureVisibility(prevChecked, true);
}

bool CSimpleCheckButton::SetDisabledCheckedTextureFromFile(const char* filePath) {
  if (disabledCheckedTexture_ != nullptr) {
    disabledCheckedTexture_->SetTexture(filePath ? filePath : "");
    return true;
  }

  auto* tex = new CSimpleTexture();
  tex->SetDrawLayer(DrawLayer::Artwork);

  if (!tex->SetTexture(filePath ? filePath : "")) {
    delete tex;
    return false;
  }

  tex->SetAllPoints(this);
  tex->SetBlendMode(BlendMode::Add);

  SetDisabledCheckedTexture(tex);
  return true;
}

void CSimpleCheckButton::UpdateCheckedTextureVisibility(bool prevChecked,
                                                        bool force) {
  if (!force && prevChecked == checked_) {
    return;
  }

  if (checkedTexture_ != nullptr) {
    checkedTexture_->HideVisible();
  }
  if (disabledCheckedTexture_ != nullptr) {
    disabledCheckedTexture_->HideVisible();
  }

  if (checked_) {
    CSimpleTexture* toShow = nullptr;

    if (disabledCheckedTexture_ != nullptr &&
        GetCurrentVisualSlot() == ButtonTextureSlot::Disabled) {
      toShow = disabledCheckedTexture_;
    } else if (checkedTexture_ != nullptr) {
      toShow = checkedTexture_;
    }

    if (toShow != nullptr) {
      toShow->ShowVisible();
    }
  }
}

}


#include "openwow/ui/widgets/simple_button.h"
#include "openwow/ui/widgets/simple_font_string.h"
#include "openwow/ui/widgets/simple_html_frame.h"

#include "openwow/ui/widgets/csimple_html_content_node.h"
#include "openwow/ui/widgets/script_region.h"
#include "openwow/ui/widgets/simple_html.h"

namespace openwow::ui::widgets {

namespace {

JustifyH SimpleHtmlJustifyH(const std::string& align) noexcept {
  if (align == "CENTER") {
    return JustifyH::Center;
  }
  if (align == "RIGHT") {
    return JustifyH::Right;
  }
  return JustifyH::Left;
}

ContentNodeType SimpleHtmlContentNodeType(const SimpleHTML::TextBlock& block) noexcept {
  return block.hyperlink.empty() ? ContentNodeType::kText : ContentNodeType::kTextWithHyperlinks;
}

}

void CSimpleHTMLFrame::SetText(const std::string& html) {
  htmlText_ = html;
  DestroyContent();

  SimpleHTML parser;
  parser.SetHyperlinksEnabled(hyperlinksEnabled_);
  parser.SetHyperlinkFormat(hyperlinkFormat_);
  parser.SetText(htmlText_);

  contentW_ = parser.GetContentWidth();
  contentH_ = parser.GetContentHeight();
  hyperlinkRegionCount_ = parser.GetHyperlinks().size();

  for (const auto& item : parser.GetParsedContent()) {
    if (item.kind == SimpleHTML::ParsedContent::Kind::kImage) {
      auto* texture = new CSimpleTexture();
      texture->SetParent(this);
      texture->SetDrawLayer(DrawLayer::Overlay);
      if (item.image.width > 0.0f || item.image.height > 0.0f) {
        texture->SetSize(item.image.width, item.image.height);
      }
      if (!item.image.src.empty()) {
        (void)texture->SetTexture(item.image.src);
      }

      ContentNode node;
      node.type = ContentNodeType::kImage;
      node.child_object = texture;
      contentNodes_.push_back(node);
      continue;
    }

    const auto& block = item.text;
    if (block.text.empty()) continue;

    auto* font_string = new CSimpleFontString();
    font_string->SetParent(this);
    font_string->SetDrawLayer(DrawLayer::Overlay);
    font_string->SetText(block.text);
    font_string->SetTextColor(block.r, block.g, block.b, block.a);
    font_string->SetJustifyH(SimpleHtmlJustifyH(block.align));
    if (!block.font.empty()) {
      (void)font_string->SetFont(block.font, block.size, 0u);
    } else {
      font_string->SetFontSize(block.size);
      font_string->SetTextHeight(block.size);
    }

    ContentNode node;
    node.type = SimpleHtmlContentNodeType(block);
    node.child_object = font_string;
    contentNodes_.push_back(node);
  }

  needsHyperlinkRebuild_ = hyperlinkRegionCount_ != 0;
}

void CSimpleHTMLFrame::DestroyContent() {

  for (auto& node : contentNodes_) {
    if (node.child_object != nullptr) {

      auto* region = static_cast<CScriptRegion*>(node.child_object);
      delete region;
      node.child_object = nullptr;
    }
  }
  contentNodes_.clear();

  lastContentLayout_ = nullptr;
  contentYOffset_ = 0.0f;

  for (void* elem : childElements_) {
    if (elem != nullptr) {
      auto* region = static_cast<CScriptRegion*>(elem);
      delete region;
    }
  }
  childElements_.clear();

  for (auto* btn : hyperlinkButtons_) {
    if (btn != nullptr) {
      btn->SetParentFrame(nullptr);
    }
    delete btn;
  }
  hyperlinkButtons_.clear();
  hyperlinkRegionCount_ = 0;
  needsHyperlinkRebuild_ = false;
}

void CSimpleHTMLFrame::RebuildHyperlinks() {

  if (!liveRectInitialized_) {
    return;
  }

  for (auto* btn : hyperlinkButtons_) {
    if (btn != nullptr) {
      btn->SetParentFrame(nullptr);
    }
    delete btn;
  }
  hyperlinkButtons_.clear();

  if (!hyperlinksEnabled_) {
    return;
  }

  for (std::size_t i = 0; i < hyperlinkRegionCount_; ++i) {
    auto* btn = new CSimpleButton();
    btn->SetParentFrame(this);
    btn->EnableMouse(true);
    hyperlinkButtons_.push_back(btn);
  }
}

}

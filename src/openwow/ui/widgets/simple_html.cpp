
#include "openwow/ui/widgets/simple_html.h"

#include "openwow/ui/xml/frame_xml_parser.h"
#include "openwow/foundation/text/ascii.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <optional>

namespace openwow::ui::widgets {

using openwow::text::ToLowerAscii;

SimpleHTML::SimpleHTML() = default;
SimpleHTML::~SimpleHTML() = default;

void SimpleHTML::SetText(const std::string& html_text) {
  std::lock_guard lock(mutex_);
  html_text_ = html_text;
  ParseHTML();
}

std::string SimpleHTML::GetText() const {
  std::lock_guard lock(mutex_);
  return html_text_;
}

void SimpleHTML::SetFont(const std::string& element, const std::string& font_file,
                         float size, const std::string& flags) {
  std::lock_guard lock(mutex_);
  fonts_[element] = FontInfo{font_file, size, flags};
}

bool SimpleHTML::HasFont(const std::string& element) const {
  std::lock_guard lock(mutex_);
  return fonts_.contains(element);
}

SimpleHTML::FontInfo SimpleHTML::GetFont(const std::string& element) const {
  std::lock_guard lock(mutex_);
  auto it = fonts_.find(element);
  if (it != fonts_.end()) return it->second;
  return FontInfo{};
}

const std::vector<SimpleHTML::Hyperlink>& SimpleHTML::GetHyperlinks() const {

  return hyperlinks_;
}

void SimpleHTML::SetHyperlinksEnabled(bool enabled) {
  std::lock_guard lock(mutex_);
  hyperlinks_enabled_ = enabled;
}

bool SimpleHTML::GetHyperlinksEnabled() const {
  std::lock_guard lock(mutex_);
  return hyperlinks_enabled_;
}

void SimpleHTML::SetHyperlinkFormat(const std::string& format) {
  std::lock_guard lock(mutex_);
  hyperlink_format_ = format;
}

std::string SimpleHTML::GetHyperlinkFormat() const {
  std::lock_guard lock(mutex_);
  return hyperlink_format_;
}

void SimpleHTML::SetHorizontalScroll(float offset) {
  std::lock_guard lock(mutex_);
  h_scroll_ = offset;
}

float SimpleHTML::GetHorizontalScroll() const {
  std::lock_guard lock(mutex_);
  return h_scroll_;
}

void SimpleHTML::SetVerticalScroll(float offset) {
  std::lock_guard lock(mutex_);
  v_scroll_ = offset;
}

float SimpleHTML::GetVerticalScroll() const {
  std::lock_guard lock(mutex_);
  return v_scroll_;
}

float SimpleHTML::GetContentHeight() const {
  std::lock_guard lock(mutex_);
  return content_height_;
}

float SimpleHTML::GetContentWidth() const {
  std::lock_guard lock(mutex_);
  return content_width_;
}

void SimpleHTML::SetOnHyperlinkClick(HyperlinkClickFn fn) {
  std::lock_guard lock(mutex_);
  on_click_ = std::move(fn);
}

void SimpleHTML::SetOnHyperlinkEnter(HyperlinkHoverFn fn) {
  std::lock_guard lock(mutex_);
  on_enter_ = std::move(fn);
}

void SimpleHTML::SetOnHyperlinkLeave(HyperlinkHoverFn fn) {
  std::lock_guard lock(mutex_);
  on_leave_ = std::move(fn);
}

void SimpleHTML::FireHyperlinkClick(const std::string& link,
                                    const std::string& text) {
  HyperlinkClickFn fn;
  {
    std::lock_guard lock(mutex_);
    fn = on_click_;
  }
  if (fn) fn(link, text);
}

void SimpleHTML::FireHyperlinkEnter(const std::string& link,
                                    const std::string& text) {
  HyperlinkHoverFn fn;
  {
    std::lock_guard lock(mutex_);
    fn = on_enter_;
  }
  if (fn) fn(link, text);
}

void SimpleHTML::FireHyperlinkLeave(const std::string& link,
                                    const std::string& text) {
  HyperlinkHoverFn fn;
  {
    std::lock_guard lock(mutex_);
    fn = on_leave_;
  }
  if (fn) fn(link, text);
}

const std::vector<SimpleHTML::TextBlock>& SimpleHTML::GetParsedBlocks() const {
  return blocks_;
}

const std::vector<SimpleHTML::ParsedContent>& SimpleHTML::GetParsedContent() const {
  return content_;
}

std::string SimpleHTML::DecodeEntities(const std::string& raw) {
  std::string result;
  result.reserve(raw.size());
  for (std::size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] == '&') {
      auto end = raw.find(';', i);
      if (end != std::string::npos) {
        std::string entity = raw.substr(i + 1, end - i - 1);
        if (entity == "lt") {
          result += '<';
        } else if (entity == "gt") {
          result += '>';
        } else if (entity == "amp") {
          result += '&';
        } else if (entity == "quot") {
          result += '"';
        } else if (entity == "apos") {
          result += '\'';
        } else if (entity == "nbsp") {
          result += ' ';
        } else if (!entity.empty() && entity[0] == '#') {

          int code = 0;
          if (entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X')) {
            code = static_cast<int>(std::strtol(entity.c_str() + 2, nullptr, 16));
          } else {
            code = static_cast<int>(std::strtol(entity.c_str() + 1, nullptr, 10));
          }
          if (code > 0 && code < 128) {
            result += static_cast<char>(code);
          }
        } else {

          result += raw.substr(i, end - i + 1);
        }
        i = end;
        continue;
      }
    }
    result += raw[i];
  }
  return result;
}

std::string SimpleHTML::StripTags(const std::string& raw) {
  std::string result;
  result.reserve(raw.size());
  bool in_tag = false;
  for (char c : raw) {
    if (c == '<') {
      in_tag = true;
    } else if (c == '>') {
      in_tag = false;
    } else if (!in_tag) {
      result += c;
    }
  }
  return result;
}

void SimpleHTML::EmitBlock(const std::string& text, const ParseState& state,
                           bool line_break) {
  if (text.empty() && !line_break) return;
  TextBlock block;
  block.text = text;
  block.r = state.r;
  block.g = state.g;
  block.b = state.b;
  block.a = state.a;
  block.font = state.font;
  block.size = state.size;
  block.bold = state.bold;
  block.italic = state.italic;
  block.align = state.align;
  block.hyperlink = state.hyperlink;
  block.indent = state.indent;
  block.line_break = line_break;
  blocks_.push_back(block);
  ParsedContent item;
  item.kind = ParsedContent::Kind::kText;
  item.text = std::move(block);
  content_.push_back(std::move(item));
}

namespace {

std::string ExtractTagName(const std::string& tag) {
  std::size_t start = (tag.size() > 1 && tag[1] == '/') ? 2 : 1;
  std::size_t end = start;
  while (end < tag.size() && tag[end] != '>' && tag[end] != ' ' &&
         tag[end] != '/') {
    ++end;
  }
  std::string name = tag.substr(start, end - start);
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return name;
}

bool IsClosingTag(const std::string& tag) {
  return tag.size() > 1 && tag[1] == '/';
}

std::string ExtractAttr(const std::string& tag, const std::string& attr) {
  std::string lower_tag = tag;
  std::transform(lower_tag.begin(), lower_tag.end(), lower_tag.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  auto pos = lower_tag.find(attr + "=");
  if (pos == std::string::npos) pos = lower_tag.find(attr + " =");
  if (pos == std::string::npos) return {};

  pos = tag.find('=', pos);
  if (pos == std::string::npos) return {};
  ++pos;
  while (pos < tag.size() && tag[pos] == ' ') ++pos;
  if (pos >= tag.size()) return {};

  char quote = tag[pos];
  if (quote == '"' || quote == '\'') {
    ++pos;
    auto end = tag.find(quote, pos);
    if (end == std::string::npos) return {};
    return tag.substr(pos, end - pos);
  }

  auto end = pos;
  while (end < tag.size() && tag[end] != ' ' && tag[end] != '>') ++end;
  return tag.substr(pos, end - pos);
}

bool ParseColorEscape(const std::string& text, std::size_t pos,
                       float& r, float& g, float& b, float& a) {
  if (pos + 10 > text.size()) return false;
  if (text[pos] != '|' || text[pos + 1] != 'c') return false;
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  };
  a = static_cast<float>(hex(text[pos + 2]) * 16 + hex(text[pos + 3])) / 255.0f;
  r = static_cast<float>(hex(text[pos + 4]) * 16 + hex(text[pos + 5])) / 255.0f;
  g = static_cast<float>(hex(text[pos + 6]) * 16 + hex(text[pos + 7])) / 255.0f;
  b = static_cast<float>(hex(text[pos + 8]) * 16 + hex(text[pos + 9])) / 255.0f;
  return true;
}

std::size_t FindTagEndRespectingQuotes(const std::string& src, std::size_t open) {
  char quote = '\0';
  for (std::size_t i = open + 1; i < src.size(); ++i) {
    const char ch = src[i];
    if (quote != '\0') {
      if (ch == quote) {
        quote = '\0';
      }
      continue;
    }
    if (ch == '"' || ch == '\'') {
      quote = ch;
      continue;
    }
    if (ch == '>') {
      return i;
    }
  }
  return std::string::npos;
}

bool IsIgnorableMarkupTag(const std::string& tag) {
  return tag.size() > 1 && (tag[1] == '!' || tag[1] == '?');
}

bool IsSelfClosingTag(const std::string& tag) {
  if (tag.size() < 2 || IsIgnorableMarkupTag(tag)) {
    return true;
  }
  std::size_t i = tag.size() - 1;
  if (tag[i] == '>') {
    if (i == 0) {
      return false;
    }
    --i;
  }
  while (i > 0 && std::isspace(static_cast<unsigned char>(tag[i]))) {
    --i;
  }
  return tag[i] == '/';
}

float ExtractFloatAttr(const std::string& tag, const std::string& attr) {
  const std::string value = ExtractAttr(tag, attr);
  if (value.empty()) {
    return 0.0f;
  }
  char* end = nullptr;
  const float parsed = std::strtof(value.c_str(), &end);
  return end != value.c_str() ? parsed : 0.0f;
}

std::string ExtractAlignAttr(const std::string& tag) {
  std::string align = ExtractAttr(tag, "align");
  std::transform(align.begin(), align.end(), align.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  if (align == "CENTER" || align == "RIGHT") {
    return align;
  }
  return "LEFT";
}

std::optional<std::string> ExtractMatchingTagInnerMarkup(const std::string& src,
                                                         std::size_t content_begin,
                                                         std::string_view tag_name) {
  int depth = 1;
  std::size_t pos = content_begin;
  while (pos < src.size()) {
    const auto open = src.find('<', pos);
    if (open == std::string::npos) {
      return std::nullopt;
    }
    if (src.compare(open, 4, "<!--") == 0) {
      const auto comment_end = src.find("-->", open + 4);
      if (comment_end == std::string::npos) {
        return std::nullopt;
      }
      pos = comment_end + 3;
      continue;
    }
    const auto close = FindTagEndRespectingQuotes(src, open);
    if (close == std::string::npos) {
      return std::nullopt;
    }
    const std::string tag = src.substr(open, close - open + 1);
    if (IsIgnorableMarkupTag(tag)) {
      pos = close + 1;
      continue;
    }
    if (ExtractTagName(tag) == tag_name) {
      if (IsClosingTag(tag)) {
        --depth;
        if (depth == 0) {
          return src.substr(content_begin, open - content_begin);
        }
      } else if (!IsSelfClosingTag(tag)) {
        ++depth;
      }
    }
    pos = close + 1;
  }
  return std::nullopt;
}

std::optional<std::string> ExtractRetailHtmlBodyMarkup(const std::string& html_text) {
  openwow::ui::xml::XMLNode root;
  std::string error;
  if (!openwow::ui::xml::FrameXMLParser::ParseDocument(html_text, &root, &error)) {
    return std::nullopt;
  }
  if (ToLowerAscii(root.tag) != "html" || root.FindChild("BODY") == nullptr) {
    return std::nullopt;
  }

  std::size_t pos = 0;
  int root_depth = 0;
  bool in_root_html = false;
  while (pos < html_text.size()) {
    const auto open = html_text.find('<', pos);
    if (open == std::string::npos) {
      return std::nullopt;
    }
    if (html_text.compare(open, 4, "<!--") == 0) {
      const auto end = html_text.find("-->", open + 4);
      pos = end == std::string::npos ? html_text.size() : end + 3;
      continue;
    }
    const auto close = FindTagEndRespectingQuotes(html_text, open);
    if (close == std::string::npos) {
      return std::nullopt;
    }
    const std::string tag = html_text.substr(open, close - open + 1);
    if (IsIgnorableMarkupTag(tag)) {
      pos = close + 1;
      continue;
    }

    const std::string tag_name = ExtractTagName(tag);
    if (!in_root_html) {
      if (!IsClosingTag(tag) && tag_name == "html") {
        in_root_html = true;
        root_depth = IsSelfClosingTag(tag) ? 0 : 1;
      }
      pos = close + 1;
      continue;
    }

    if (IsClosingTag(tag)) {
      if (root_depth == 1 && tag_name == "html") {
        return std::nullopt;
      }
      root_depth = std::max(0, root_depth - 1);
      pos = close + 1;
      continue;
    }

    if (root_depth == 1 && tag_name == "body") {
      return ExtractMatchingTagInnerMarkup(html_text, close + 1, "body");
    }
    if (!IsSelfClosingTag(tag)) {
      ++root_depth;
    }
    pos = close + 1;
  }
  return std::nullopt;
}

}

void SimpleHTML::ParseHTML() {
  blocks_.clear();
  content_.clear();
  hyperlinks_.clear();
  content_width_ = 0;
  content_height_ = 0;

  if (html_text_.empty()) return;

  ParseState state;

  auto body_it = fonts_.find("body");
  if (body_it != fonts_.end()) {
    state.font = body_it->second.path;
    state.size = body_it->second.size;
  } else {
    auto p_it = fonts_.find("p");
    if (p_it != fonts_.end()) {
      state.font = p_it->second.path;
      state.size = p_it->second.size;
    }
  }

  auto finish_metrics = [&]() {
    if (blocks_.empty()) {
      return;
    }
    float max_width = 0;
    float total_height = 0;
    for (const auto& block : blocks_) {
      const float w = static_cast<float>(block.text.size()) * block.size * 0.6f;
      if (w > max_width) max_width = w;
      if (block.line_break) total_height += block.size * 1.2f;
    }
    if (total_height == 0.0f) {
      total_height = static_cast<float>(blocks_.size()) * 14.0f;
    }
    content_width_ = std::max(content_width_, max_width);
    content_height_ = std::max(content_height_, total_height);
  };

  const auto body_markup = ExtractRetailHtmlBodyMarkup(html_text_);
  if (!body_markup.has_value()) {
    EmitBlock(html_text_, state, false);
    finish_metrics();
    return;
  }

  const std::string& src = *body_markup;
  std::size_t pos = 0;
  std::string current_text;
  bool need_line_break = false;

  std::vector<ParseState> state_stack;

  while (pos < src.size()) {

    if (src[pos] == '|') {
      if (pos + 1 < src.size() && src[pos + 1] == 'c') {

        if (!current_text.empty()) {
          EmitBlock(DecodeEntities(current_text), state, need_line_break);
          current_text.clear();
          need_line_break = false;
        }
        float nr, ng, nb, na;
        if (ParseColorEscape(src, pos, nr, ng, nb, na)) {
          state.r = nr;
          state.g = ng;
          state.b = nb;
          state.a = na;
          pos += 10;
          continue;
        }
      }
      if (pos + 1 < src.size() && src[pos + 1] == 'r') {

        if (!current_text.empty()) {
          EmitBlock(DecodeEntities(current_text), state, need_line_break);
          current_text.clear();
          need_line_break = false;
        }
        state.r = 1.0f;
        state.g = 1.0f;
        state.b = 1.0f;
        state.a = 1.0f;
        pos += 2;
        continue;
      }

      if (pos + 1 < src.size() && src[pos + 1] == 'H') {
        if (!current_text.empty()) {
          EmitBlock(DecodeEntities(current_text), state, need_line_break);
          current_text.clear();
          need_line_break = false;
        }
        auto pipe_end = src.find('|', pos + 2);
        if (pipe_end != std::string::npos) {
          state.hyperlink = src.substr(pos + 2, pipe_end - pos - 2);

          pos = pipe_end + 2;
          continue;
        }
      }
      if (pos + 1 < src.size() && src[pos + 1] == 'h') {

        if (!current_text.empty()) {
          EmitBlock(DecodeEntities(current_text), state, need_line_break);
          current_text.clear();
          need_line_break = false;
        }
        if (!state.hyperlink.empty()) {
          Hyperlink hl;
          hl.link = state.hyperlink;
          if (!blocks_.empty()) {
            hl.text = blocks_.back().text;
          }
          hyperlinks_.push_back(std::move(hl));
          state.hyperlink.clear();
        }
        pos += 2;
        continue;
      }
    }

    if (src[pos] == '<') {
      auto close = FindTagEndRespectingQuotes(src, pos);
      if (close == std::string::npos) {
        current_text += src[pos];
        ++pos;
        continue;
      }

      if (!current_text.empty()) {
        EmitBlock(DecodeEntities(current_text), state, need_line_break);
        current_text.clear();
        need_line_break = false;
      }

      std::string tag = src.substr(pos, close - pos + 1);
      std::string tag_name = ExtractTagName(tag);
      bool closing = IsClosingTag(tag);

      if (tag_name == "br") {
        need_line_break = true;
        content_height_ += state.size;
      } else if (tag_name == "p") {
        if (closing) {
          need_line_break = true;
          if (!state_stack.empty()) {
            state = state_stack.back();
            state_stack.pop_back();
          }
        } else {
          state_stack.push_back(state);
          need_line_break = true;
          std::string align_attr = ExtractAttr(tag, "align");
          if (!align_attr.empty()) {
            std::transform(align_attr.begin(), align_attr.end(),
                           align_attr.begin(),
                           [](unsigned char c) { return std::toupper(c); });
            state.align = align_attr;
          }
          auto p_it = fonts_.find("p");
          if (p_it != fonts_.end()) {
            state.font = p_it->second.path;
            state.size = p_it->second.size;
          }
          content_height_ += state.size;
        }
      } else if (tag_name == "h1" || tag_name == "h2" || tag_name == "h3") {
        if (closing) {
          need_line_break = true;
          if (!state_stack.empty()) {
            state = state_stack.back();
            state_stack.pop_back();
          }
        } else {
          state_stack.push_back(state);
          need_line_break = true;
          state.bold = true;
          auto h_it = fonts_.find(tag_name);
          if (h_it != fonts_.end()) {
            state.font = h_it->second.path;
            state.size = h_it->second.size;
          } else {
            if (tag_name == "h1")
              state.size = 24.0f;
            else if (tag_name == "h2")
              state.size = 20.0f;
            else
              state.size = 16.0f;
          }
          std::string align_attr = ExtractAttr(tag, "align");
          if (!align_attr.empty()) {
            std::transform(align_attr.begin(), align_attr.end(),
                           align_attr.begin(),
                           [](unsigned char c) { return std::toupper(c); });
            state.align = align_attr;
          }
          content_height_ += state.size;
        }
      } else if (tag_name == "b" || tag_name == "strong") {
        if (closing) {
          state.bold = false;
        } else {
          state.bold = true;
        }
      } else if (tag_name == "i" || tag_name == "em") {
        if (closing) {
          state.italic = false;
        } else {
          state.italic = true;
        }
      } else if (tag_name == "a") {
        if (closing) {

          if (!state.hyperlink.empty()) {
            Hyperlink hl;
            hl.link = state.hyperlink;

            if (!blocks_.empty()) {
              hl.text = blocks_.back().text;
            }
            hyperlinks_.push_back(std::move(hl));
          }
          state.hyperlink.clear();
        } else {
          state.hyperlink = ExtractAttr(tag, "href");
        }
      } else if (tag_name == "img") {
        if (!closing) {
          ParsedContent item;
          item.kind = ParsedContent::Kind::kImage;
          item.image.src = ExtractAttr(tag, "src");
          item.image.width = ExtractFloatAttr(tag, "width");
          item.image.height = ExtractFloatAttr(tag, "height");
          item.image.align = ExtractAlignAttr(tag);
          content_width_ = std::max(content_width_, item.image.width);
          content_height_ += item.image.height > 0.0f ? item.image.height : state.size;
          content_.push_back(std::move(item));
        }
      }

      pos = close + 1;
      continue;
    }

    current_text += src[pos];
    ++pos;
  }

  if (!current_text.empty()) {
    EmitBlock(DecodeEntities(current_text), state, need_line_break);
  }

  finish_metrics();
}

}

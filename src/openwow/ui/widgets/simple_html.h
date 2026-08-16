
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace openwow::ui::widgets {

class SimpleHTML {
 public:
  SimpleHTML();
  ~SimpleHTML();

  void SetText(const std::string& html_text);
  [[nodiscard]] std::string GetText() const;

  void SetFont(const std::string& element, const std::string& font_file,
               float size, const std::string& flags = "");
  [[nodiscard]] bool HasFont(const std::string& element) const;

  struct FontInfo {
    std::string path;
    float size = 12.0f;
    std::string flags;
  };
  [[nodiscard]] FontInfo GetFont(const std::string& element) const;

  struct Hyperlink {
    std::string link;
    std::string text;
    float x = 0, y = 0, width = 0, height = 0;
  };
  [[nodiscard]] const std::vector<Hyperlink>& GetHyperlinks() const;

  void SetHyperlinksEnabled(bool enabled);
  [[nodiscard]] bool GetHyperlinksEnabled() const;

  void SetHorizontalScroll(float offset);
  [[nodiscard]] float GetHorizontalScroll() const;
  void SetVerticalScroll(float offset);
  [[nodiscard]] float GetVerticalScroll() const;

  [[nodiscard]] float GetContentHeight() const;
  [[nodiscard]] float GetContentWidth() const;

  void SetHyperlinkFormat(const std::string& format);
  [[nodiscard]] std::string GetHyperlinkFormat() const;

  struct TextBlock {
    std::string text;
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    std::string font;
    float size = 12.0f;
    bool bold = false;
    bool italic = false;
    std::string align = "LEFT";
    std::string hyperlink;
    float indent = 0.0f;
    bool line_break = false;
  };
  [[nodiscard]] const std::vector<TextBlock>& GetParsedBlocks() const;

  struct ImageBlock {
    std::string src;
    float width = 0.0f;
    float height = 0.0f;
    std::string align = "LEFT";
  };

  struct ParsedContent {
    enum class Kind { kText, kImage };

    Kind kind = Kind::kText;
    TextBlock text;
    ImageBlock image;
  };
  [[nodiscard]] const std::vector<ParsedContent>& GetParsedContent() const;

  using HyperlinkClickFn =
      std::function<void(const std::string& link, const std::string& text)>;
  using HyperlinkHoverFn =
      std::function<void(const std::string& link, const std::string& text)>;

  void SetOnHyperlinkClick(HyperlinkClickFn fn);
  void SetOnHyperlinkEnter(HyperlinkHoverFn fn);
  void SetOnHyperlinkLeave(HyperlinkHoverFn fn);

  void FireHyperlinkClick(const std::string& link, const std::string& text);
  void FireHyperlinkEnter(const std::string& link, const std::string& text);
  void FireHyperlinkLeave(const std::string& link, const std::string& text);

 private:
  mutable std::mutex mutex_;

  std::string html_text_;
  std::vector<TextBlock> blocks_;
  std::vector<ParsedContent> content_;
  std::vector<Hyperlink> hyperlinks_;
  std::unordered_map<std::string, FontInfo> fonts_;
  bool hyperlinks_enabled_ = true;
  std::string hyperlink_format_{"|H%s|h%s|h"};

  float h_scroll_ = 0.0f;
  float v_scroll_ = 0.0f;
  float content_width_ = 0.0f;
  float content_height_ = 0.0f;

  HyperlinkClickFn on_click_;
  HyperlinkHoverFn on_enter_;
  HyperlinkHoverFn on_leave_;

  void ParseHTML();

  struct ParseState {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    std::string font;
    float size = 12.0f;
    bool bold = false;
    bool italic = false;
    std::string align = "LEFT";
    std::string hyperlink;
    float indent = 0.0f;
  };

  void EmitBlock(const std::string& text, const ParseState& state,
                 bool line_break);
  static std::string StripTags(const std::string& raw);
  static std::string DecodeEntities(const std::string& raw);
};

}

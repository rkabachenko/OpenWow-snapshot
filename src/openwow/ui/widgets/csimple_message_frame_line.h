
#pragma once

#include "openwow/ui/widgets/simple_font_string.h"

#include <cstdint>

namespace openwow::ui::widgets {

class CSimpleMessageFrameLine {
 public:

  CSimpleMessageFrameLine() {
    fontStr_ = new CSimpleFontString();

    auto& render = fontStr_->GetRender();
    render.SetJustifyFlags(render.GetJustifyFlags() | 0x4000u);
  }

  virtual ~CSimpleMessageFrameLine() {
    delete fontStr_;
    fontStr_ = nullptr;
  }

  CSimpleMessageFrameLine(const CSimpleMessageFrameLine&) = delete;
  CSimpleMessageFrameLine& operator=(const CSimpleMessageFrameLine&) = delete;

  void AddRef() noexcept { ++refCount_; }

  void Release() noexcept {
    if (--refCount_ == 0) {
      delete this;
    }
  }

  [[nodiscard]] uint32_t GetRefCount() const noexcept { return refCount_; }

  [[nodiscard]] CSimpleFontString* GetFontString() const noexcept {
    return fontStr_;
  }

  void SetColor(uint32_t bgra) noexcept { color_ = bgra; }
  [[nodiscard]] uint32_t GetColor() const noexcept { return color_; }

  void SetAlpha(uint8_t alpha) noexcept {
    color_ = (color_ & 0x00FFFFFF) | (static_cast<uint32_t>(alpha) << 24);
  }
  [[nodiscard]] uint8_t GetAlpha() const noexcept {
    return static_cast<uint8_t>(color_ >> 24);
  }

  void SetDisplayTime(float t) noexcept { displayTime_ = t; }
  [[nodiscard]] float GetDisplayTime() const noexcept { return displayTime_; }

  void SetFadeTime(float t) noexcept { fadeTime_ = t; }
  [[nodiscard]] float GetFadeTime() const noexcept { return fadeTime_; }

  void SetVisible(bool v) noexcept { visible_ = v; }
  [[nodiscard]] bool IsVisible() const noexcept { return visible_; }

 private:
  uint32_t refCount_{0};

  uint32_t color_{0xFFFFFFFF};

  CSimpleFontString* fontStr_{nullptr};

  float    displayTime_{0.0f};

  float    fadeTime_{0.0f};

  bool     visible_{false};

};

struct MessageFrameLineEntry {
  float xOffset{0.0f};

  float yOffset{0.0f};

  CSimpleMessageFrameLine* line{nullptr};

  MessageFrameLineEntry() = default;

  MessageFrameLineEntry(float x, float y, CSimpleMessageFrameLine* l)
      : xOffset(x), yOffset(y), line(l) {
    if (line) line->AddRef();
  }

  ~MessageFrameLineEntry() {
    if (line) {
      line->Release();
      line = nullptr;
    }
  }

  MessageFrameLineEntry(const MessageFrameLineEntry& o)
      : xOffset(o.xOffset), yOffset(o.yOffset), line(o.line) {
    if (line) line->AddRef();
  }
  MessageFrameLineEntry& operator=(const MessageFrameLineEntry& o) {
    if (this != &o) {
      if (line) line->Release();
      xOffset = o.xOffset;
      yOffset = o.yOffset;
      line = o.line;
      if (line) line->AddRef();
    }
    return *this;
  }

  MessageFrameLineEntry(MessageFrameLineEntry&& o) noexcept
      : xOffset(o.xOffset), yOffset(o.yOffset), line(o.line) {
    o.line = nullptr;
  }
  MessageFrameLineEntry& operator=(MessageFrameLineEntry&& o) noexcept {
    if (this != &o) {
      if (line) line->Release();
      xOffset = o.xOffset;
      yOffset = o.yOffset;
      line = o.line;
      o.line = nullptr;
    }
    return *this;
  }
};

}

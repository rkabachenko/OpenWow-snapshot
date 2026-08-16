#pragma once

#include "openwow/ui/widgets/csimple_message_scroll_frame_display_node.h"
#include "openwow/ui/widgets/simple_frame.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace openwow::ui::widgets {

struct ScrollingMessage {
  std::string text;
  float r{1.0f}, g{1.0f}, b{1.0f};
  float timeAdded{0.0f};
  uint32_t packedColor{0xFFFFFFFF};

  bool     enabled{true};

  float    displayTime{0.0f};

  float    fadeTime{0.0f};

  int32_t  typeId{0};

  int32_t  accessId{0};

  int32_t  extraData{0};

};

class CSimpleScrollingMessageFrame : public CSimpleFrame {
 public:
  CSimpleScrollingMessageFrame()
      : CSimpleFrame(ScriptObjectType::ScrollingMessageFrame) {}
  [[nodiscard]] bool IsKindOf(ScriptObjectType t) const noexcept override {
    return t == ScriptObjectType::ScrollingMessageFrame ||
           CSimpleFrame::IsKindOf(t);
  }
  [[nodiscard]] bool IsTypeOf(const char* typeName) const noexcept override {
    return StrCaseEq(typeName, "ScrollingMessageFrame") || CSimpleFrame::IsTypeOf(typeName);
  }

  void AddMessage(const std::string& text, float r = 1.0f, float g = 1.0f,
                  float b = 1.0f, int32_t typeId = 0, int32_t accessId = 0,
                  int32_t extraData = 0) {

    auto toByte = [](float v) -> uint8_t {
      return static_cast<uint8_t>(std::min(std::max(v, 0.0f), 1.0f) * 255.0f);
    };
    const uint32_t packed = static_cast<uint32_t>(toByte(b))
                          | (static_cast<uint32_t>(toByte(g)) << 8)
                          | (static_cast<uint32_t>(toByte(r)) << 16)
                          | 0xFF000000u;
    messages_.push_back({text, r, g, b, currentTime_, packed, true,
                         0.0f, 0.0f, typeId, accessId, extraData});
    if (maxLines_ > 0 && messages_.size() > maxLines_)
      messages_.erase(messages_.begin());
    needsLayout_ = true;
    needsRedraw_ = true;
  }

  void UpdateColorByID(int32_t colorId, uint32_t packedBGRA) {
    if (colorId == 0)
      return;
    for (auto& msg : messages_) {
      if (msg.typeId == colorId) {
        msg.packedColor = packedBGRA;

        msg.b = static_cast<float>((packedBGRA      ) & 0xFF) / 255.0f;
        msg.g = static_cast<float>((packedBGRA >>  8) & 0xFF) / 255.0f;
        msg.r = static_cast<float>((packedBGRA >> 16) & 0xFF) / 255.0f;
      }
    }
    needsRedraw_ = true;
  }

  void Clear() {

    displayNodes_.clear();

    messages_.clear();

    numDisplayedLines_ = 0;

    needsRedraw_       = true;

    needsLayout_       = true;

    scrollAnimState_   = 0;

    headIndex_         = -1;

    displayOffset_     = 0;

  }

  [[nodiscard]] const std::vector<ScrollingMessage>& GetMessages() const noexcept {
    return messages_;
  }
  [[nodiscard]] size_t GetNumMessages() const noexcept {
    return messages_.size();
  }

  [[nodiscard]] int32_t GetCurrentScroll() const noexcept {
    const auto n = static_cast<int32_t>(messages_.size());
    if (n == 0 || headIndex_ == -1) return 0;
    return (headIndex_ + n - static_cast<int32_t>(displayOffset_)) % n;
  }

  void SetMaxLines(uint32_t n) noexcept { maxLines_ = n; }
  [[nodiscard]] uint32_t GetMaxLines() const noexcept { return maxLines_; }

  void SetFading(bool f) noexcept { fading_ = f; }
  [[nodiscard]] bool GetFading() const noexcept { return fading_; }

  void SetFadeDuration(float d) noexcept {
    const auto count = std::min(
        numDisplayedLines_,
        static_cast<uint32_t>(displayNodes_.size()));
    for (uint32_t i = 0; i < count; ++i) {
      auto* msg = static_cast<ScrollingMessage*>(
          const_cast<void*>(displayNodes_[i].GetMessageLine()));
      if (msg && msg->enabled) {
        msg->fadeTime = d;
      }
    }
    fadeDuration_ = d;
  }
  [[nodiscard]] float GetFadeDuration() const noexcept { return fadeDuration_; }

  void SetTimeVisible(float t) noexcept {
    const auto count = std::min(
        numDisplayedLines_,
        static_cast<uint32_t>(displayNodes_.size()));
    for (uint32_t i = 0; i < count; ++i) {
      auto* msg = static_cast<ScrollingMessage*>(
          const_cast<void*>(displayNodes_[i].GetMessageLine()));
      if (msg && msg->enabled) {
        msg->displayTime = t;
      }
    }
    timeVisible_ = t;
  }
  [[nodiscard]] float GetTimeVisible() const noexcept { return timeVisible_; }

  void SetScrollOffset(int32_t offset) noexcept {
    displayOffset_ = static_cast<uint32_t>(std::max(0, offset));
  }
  [[nodiscard]] int32_t GetScrollOffset() const noexcept {
    return static_cast<int32_t>(displayOffset_);
  }
  void ScrollUp() { displayOffset_ = (displayOffset_ > 0) ? displayOffset_ - 1 : 0; }
  void ScrollDown() { displayOffset_++; }
  void ScrollToTop() { displayOffset_ = 0; }
  void ScrollToBottom() {
    displayOffset_ = static_cast<uint32_t>(messages_.size());
  }

  void SetInsertMode(const std::string& mode) {
    insertMode_ = mode;
    needsLayout_ = true;
    needsRedraw_ = true;
  }
  [[nodiscard]] const std::string& GetInsertMode() const noexcept {
    return insertMode_;
  }

  void SetCurrentTime(float t) noexcept { currentTime_ = t; }

  [[nodiscard]] bool NeedsLayout() const noexcept { return needsLayout_; }
  void ClearLayoutFlag() noexcept { needsLayout_ = false; }
  [[nodiscard]] bool NeedsRedraw() const noexcept { return needsRedraw_; }
  void ClearRedrawFlag() noexcept { needsRedraw_ = false; }

  [[nodiscard]] uint32_t GetNumDisplayedLines() const noexcept {
    return numDisplayedLines_;
  }

 private:

  std::vector<ScrollingMessage> messages_;
  uint32_t maxLines_{128};

  int32_t  headIndex_{-1};

  uint32_t displayOffset_{0};

  uint32_t numDisplayedLines_{0};

  uint32_t scrollAnimState_{0};

  bool     needsLayout_{true};

  std::vector<CSimpleMessageScrollFrameDisplayNode> displayNodes_;
  bool     needsRedraw_{true};

  bool fading_{true};
  float fadeDuration_{3.0f};
  float timeVisible_{10.0f};
  std::string insertMode_{"BOTTOM"};
  float currentTime_{0.0f};
};

}

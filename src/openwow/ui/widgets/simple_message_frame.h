#pragma once

#include "openwow/core/cimvector.h"
#include "openwow/ui/frame_script_events.h"
#include "openwow/ui/frame_script_type_info.h"
#include "openwow/ui/widgets/csimple_message_frame_line.h"
#include "openwow/ui/widgets/simple_frame.h"

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::ui::widgets {

struct MessageFramePendingEntry {
  std::string text;
  std::uint32_t packedColor{0xFFFFFFFF};
};

class CSimpleMessageFrame : public CSimpleFrame {
 public:
  CSimpleMessageFrame() : CSimpleFrame(ScriptObjectType::MessageFrame) {}

  ~CSimpleMessageFrame() override {
    DestroyLineEntries();
  }

  [[nodiscard]] bool IsKindOf(ScriptObjectType t) const noexcept override {
    return t == ScriptObjectType::MessageFrame || CSimpleFrame::IsKindOf(t);
  }
  [[nodiscard]] bool IsTypeOf(const char* typeName) const noexcept override {
    return StrCaseEq(typeName, "MessageFrame") || CSimpleFrame::IsTypeOf(typeName);
  }

  bool AddMessage(const char* text, const std::uint32_t* packedColor) {
    if (!text) {
      return false;
    }
    pendingMessages_.push_back({text, packedColor ? *packedColor : 0xFFFFFFFF});
    return true;
  }

  void AddMessage(const std::string& text, float r = 1.0f, float g = 1.0f,
                  float b = 1.0f) {
    std::uint32_t color =
        openwow::core::PackArgbFloatsToBgra(1.0f, r, g, b);
    AddMessage(text.c_str(), &color);
  }

  void Clear() { pendingMessages_.clear(); }
  [[nodiscard]] size_t GetNumMessages() const noexcept {
    return pendingMessages_.size();
  }

  [[nodiscard]] const std::vector<MessageFramePendingEntry>&
  GetPendingMessages() const noexcept {
    return pendingMessages_;
  }
  void ClearPendingMessages() { pendingMessages_.clear(); }

  void SetFadeDuration(float d) noexcept { fadeDuration_ = d; }
  [[nodiscard]] float GetFadeDuration() const noexcept { return fadeDuration_; }

  void SetLineSpacing(float s) noexcept { lineSpacing_ = s; }
  [[nodiscard]] float GetLineSpacing() const noexcept { return lineSpacing_; }

  void SetInsertMode(const std::string& mode) {
    insertMode_ = mode;
    LayoutMessageLines();
  }
  [[nodiscard]] const std::string& GetInsertMode() const noexcept {
    return insertMode_;
  }

  void SetTextInsets(float left, float top, float right, float bottom) noexcept {
    textInsetLeft_ = left;
    textInsetTop_ = top;
    textInsetRight_ = right;
    textInsetBottom_ = bottom;
    LayoutMessageLines();
  }

  void SetDisplayDuration(float d) noexcept { displayDuration_ = d; }
  [[nodiscard]] float GetDisplayDuration() const noexcept {
    return displayDuration_;
  }

  void SetFading(bool f) noexcept { fadingEnabled_ = f; }
  [[nodiscard]] bool GetFading() const noexcept { return fadingEnabled_; }

  void OnResize(const float* newRect) override;

  void OnFrameLayoutChanged(uint32_t flags);

  void LayoutMessageLines();

  void ResizeLineEntries(uint32_t newCount) {
    const uint32_t oldCount = static_cast<uint32_t>(lineEntries_.size());
    if (newCount <= oldCount) {

      lineEntries_.resize(newCount);
      return;
    }

    lineEntries_.reserve(newCount);
    for (uint32_t i = oldCount; i < newCount; ++i) {
      auto* line = new CSimpleMessageFrameLine();

      lineEntries_.emplace_back(0.0f, 0.0f, line);

    }
  }

  void DestroyLineEntries() {

    lineEntries_.clear();
    numActiveLines_ = 0;
    visibleLineCount_ = 0;
  }

  [[nodiscard]] uint32_t GetNumActiveLines() const noexcept {
    return numActiveLines_;
  }
  [[nodiscard]] uint32_t GetVisibleLineCount() const noexcept {
    return visibleLineCount_;
  }

  [[nodiscard]] const std::vector<MessageFrameLineEntry>&
  GetLineEntries() const noexcept {
    return lineEntries_;
  }
  [[nodiscard]] std::vector<MessageFrameLineEntry>&
  GetLineEntries() noexcept {
    return lineEntries_;
  }

  [[nodiscard]] static const char* GetScriptHandlerFormat(
      const std::string& eventName) noexcept {
    return openwow::ui::GetAnyFrameScriptWrapperFormat(eventName);
  }

 private:

  uint32_t numActiveLines_{0};

  uint32_t visibleLineCount_{0};

  float computedLeft_{0.0f};

  float computedTop_{0.0f};

  float computedRight_{0.0f};

  float computedBottom_{0.0f};

  float textInsetLeft_{0.0f};

  float textInsetTop_{0.0f};

  float textInsetBottom_{0.0f};

  float textInsetRight_{0.0f};

  float lineSpacing_{10.0f};

  std::string insertMode_{"TOP"};

  float displayDuration_{10.0f};

  float fadeDuration_{3.0f};

  bool fadingEnabled_{true};

  std::vector<MessageFramePendingEntry> pendingMessages_;

  std::vector<MessageFrameLineEntry> lineEntries_;

  bool messageLinesDirty_{true};
};

}

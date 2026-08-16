#pragma once

#include "openwow/core/cimvector.h"
#include "openwow/input/input_manager.h"
#include "openwow/ui/frame_script_events.h"
#include "openwow/ui/glue/editbox_text_layout.h"
#include "openwow/ui/widgets/edit_history.h"
#include "openwow/ui/widgets/simple_frame.h"
#include "openwow/ui/widgets/simple_font_string.h"
#include "openwow/ui/widgets/simple_texture.h"
#include "openwow/foundation/text/utf8.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::ui::widgets {

enum class EditBoxInputLanguage : uint32_t {
  Roman    = 0,
  Korean   = 1,
  Chinese  = 2,
  Japanese = 3,
};

[[nodiscard]] const char* EditBoxInputLanguageToken(EditBoxInputLanguage language) noexcept;
[[nodiscard]] EditBoxInputLanguage DetectActiveEditBoxInputLanguage() noexcept;

class CSimpleEditBox : public CSimpleFrame {
 public:

  static constexpr uint32_t kDirtyText         = 0x01;
  static constexpr uint32_t kDirtySelection    = 0x02;
  static constexpr uint32_t kDirtyCursor       = 0x04;
  static constexpr uint32_t kDirtyUserModified = 0x08;

  CSimpleEditBox() : CSimpleFrame(ScriptObjectType::EditBox) {
    autoFocus_ = true;
    drawsOnUpdate_ = true;
    cursorPos_ = 0;
    blinkSpeed_ = 0.5f;
  }

  [[nodiscard]] bool IsKindOf(ScriptObjectType t) const noexcept override {
    return t == ScriptObjectType::EditBox || CSimpleFrame::IsKindOf(t);
  }
  [[nodiscard]] bool IsTypeOf(const char* typeName) const noexcept override {
    return StrCaseEq(typeName, "EditBox") || CSimpleFrame::IsTypeOf(typeName);
  }

  void FireOnHide() override;

  void FireOnUpdate(float elapsed) override;

  void ProcessDirtyFlags();

  [[nodiscard]] static CSimpleEditBox* GetFocusedEditBox() noexcept {
    return s_focusedEditBox_;
  }

  static void SetFocusedEditBox(CSimpleEditBox* eb) noexcept {
    s_focusedEditBox_ = eb;
  }

  void SetText(const std::string& text) {
    SetTextInternal(text.c_str(), text.size());
  }
  [[nodiscard]] const std::string& GetText() const noexcept { return text_; }

  void Insert(std::string_view text, int inputSource = 0, bool fireEvents = true,
              bool keepSelection = false, bool userInput = true);
  void ClearText() {
    text_.clear();
    cursorPos_ = 0;
    selectionStart_ = 0;
    selectionEnd_ = 0;
    charInfoBuffer_.clear();
  }

  void RebuildCharInfoBuffer();

  [[nodiscard]] const std::vector<uint32_t>& GetCharInfoBuffer() const noexcept {
    return charInfoBuffer_;
  }

  void SetCursorPosition(size_t pos) noexcept {
    cursorPos_ = std::min(pos, text_.size());
    dirtyFlags_ |= kDirtyCursor;
  }
  [[nodiscard]] size_t GetCursorPosition() const noexcept { return cursorPos_; }

  size_t SetCursorPositionInternal(int pos) noexcept {
    dirtyFlags_ |= kDirtyCursor;
    if (pos < 0) {
      cursorPos_ = 0;
      return 0;
    }
    const auto textLen = static_cast<int>(text_.size());
    if (pos > textLen)
      pos = textLen;
    cursorPos_ = static_cast<size_t>(pos);
    return cursorPos_;
  }

  void SetSelection(size_t start, size_t end) noexcept {
    selectionStart_ = start;
    selectionEnd_ = end;
  }
  [[nodiscard]] size_t GetSelectionStart() const noexcept {
    return selectionStart_;
  }
  [[nodiscard]] size_t GetSelectionEnd() const noexcept {
    return selectionEnd_;
  }
  [[nodiscard]] bool HasSelection() const noexcept {
    return selectionStart_ != selectionEnd_;
  }
  void ClearSelection() noexcept {
    selectionStart_ = 0;
    selectionEnd_ = 0;
  }

  void HighlightText(int start, int end) noexcept {
    const int textLen = static_cast<int>(text_.size());

    int hl_start = (start <= 0) ? 0 : start;
    if (hl_start >= textLen)
      hl_start = textLen;

    int hl_end;
    const int temp = (end <= -1) ? -1 : end;
    if (temp >= textLen) {
      hl_end = textLen;
    } else if (end <= -1) {
      hl_end = -1;
    } else {
      hl_end = end;
    }

    if (hl_end < hl_start)
      hl_end = textLen;

    dirtyFlags_ |= kDirtySelection;
    selectionStart_ = static_cast<size_t>(hl_start);
    selectionEnd_ = static_cast<size_t>(hl_end);
  }

  void SetMaxLetters(uint32_t max) noexcept { maxLetters_ = max; }
  [[nodiscard]] uint32_t GetMaxLetters() const noexcept { return maxLetters_; }

  void SetMaxBytesLimit(int32_t max) noexcept { maxBytesLimit_ = max; }
  [[nodiscard]] int32_t GetMaxBytesLimit() const noexcept { return maxBytesLimit_; }

  void SetMaxBytes(uint32_t max) noexcept { maxBytes_ = max; }
  [[nodiscard]] uint32_t GetMaxBytes() const noexcept { return maxBytes_; }

  void SetNumeric(bool n) noexcept { numeric_ = n; }
  [[nodiscard]] bool IsNumeric() const noexcept { return numeric_; }

  void SetPassword(bool p) noexcept { password_ = p; }
  [[nodiscard]] bool IsPassword() const noexcept { return password_; }

  void SetMultiLine(bool m) noexcept { multiLine_ = m; }
  [[nodiscard]] bool IsMultiLine() const noexcept { return multiLine_; }

  void SetAutoFocus(bool a) noexcept { autoFocus_ = a; }
  [[nodiscard]] bool IsAutoFocus() const noexcept { return autoFocus_; }

  void SetFocus() noexcept { hasFocus_ = true; }
  void ClearFocus() noexcept { hasFocus_ = false; }
  [[nodiscard]] bool HasFocus() const noexcept { return hasFocus_; }

  void SetBlinkSpeed(float speed) noexcept { blinkSpeed_ = speed; }
  [[nodiscard]] float GetBlinkSpeed() const noexcept { return blinkSpeed_; }

  void SetTextInsets(float left, float right, float top, float bottom) noexcept {
    textInsetLeft_   = left;
    textInsetRight_  = right;
    textInsetTop_    = top;
    textInsetBottom_ = bottom;
  }
  void GetTextInsets(float& left, float& right, float& top, float& bottom) const noexcept {
    left   = textInsetLeft_;
    right  = textInsetRight_;
    top    = textInsetTop_;
    bottom = textInsetBottom_;
  }
  [[nodiscard]] float GetTextInsetLeft()   const noexcept { return textInsetLeft_; }
  [[nodiscard]] float GetTextInsetRight()  const noexcept { return textInsetRight_; }
  [[nodiscard]] float GetTextInsetTop()    const noexcept { return textInsetTop_; }
  [[nodiscard]] float GetTextInsetBottom() const noexcept { return textInsetBottom_; }

  void SetCountInvisibleLetters(bool c) noexcept { countInvisible_ = c; }
  [[nodiscard]] bool GetCountInvisibleLetters() const noexcept {
    return countInvisible_;
  }

  void SetAltArrowKeyMode(bool m) noexcept { altArrowKeyMode_ = m; }
  [[nodiscard]] bool GetAltArrowKeyMode() const noexcept { return altArrowKeyMode_; }

  [[nodiscard]] int CountLettersInRange(int start_byte, int byte_count) const noexcept {
    if (byte_count == 0 || text_.empty()) {
      return 0;
    }
    if (countInvisible_) {

      std::size_t from;
      std::size_t len;
      if (byte_count > 0) {
        from = static_cast<std::size_t>(std::max(0, start_byte));
        len = static_cast<std::size_t>(byte_count);
      } else {
        const auto end = static_cast<std::size_t>(std::max(0, start_byte));
        const auto back = static_cast<std::size_t>(-byte_count);
        from = end > back ? end - back : 0;
        len = end - from;
      }
      if (from >= text_.size()) {
        return 0;
      }
      len = std::min(len, text_.size() - from);
      return openwow::text::Utf8CodepointCount(
          std::string_view(text_).substr(from, len));
    }
    return openwow::ui::glue::CountEditBoxVisibleLettersInRange(text_, start_byte, byte_count);
  }

  [[nodiscard]] int CountLetters() const noexcept {
    if (countInvisible_) {
      return openwow::text::Utf8CodepointCount(text_);
    }
    return openwow::ui::glue::CountEditBoxVisibleLetters(text_);
  }

  void SetHistoryLines(uint32_t n) noexcept {
    editHistory_.SetCapacity(n);
    historyLines_ = n;
    if (historyIndex_ >= static_cast<int32_t>(n)) {
      historyIndex_ = n ? static_cast<int32_t>(n) - 1 : 0;
    }
  }
  [[nodiscard]] uint32_t GetHistoryLines() const noexcept { return historyLines_; }

  void AddHistoryLine(const std::string& line, uint32_t cursorPos = 0) {
    if (historyLines_ == 0 || editHistory_.Capacity() == 0)
      return;
    auto& entry = editHistory_.At(static_cast<uint32_t>(historyIndex_));
    entry.text = line;
    entry.cursorPos = cursorPos;
    historyIndex_ = (historyIndex_ + 1) % static_cast<int32_t>(historyLines_);
  }

  [[nodiscard]] const EditHistory& GetEditHistory() const noexcept {
    return editHistory_;
  }
  [[nodiscard]] EditHistory& GetEditHistory() noexcept {
    return editHistory_;
  }

  [[nodiscard]] int32_t GetHistoryIndex() const noexcept { return historyIndex_; }
  void SetHistoryIndex(int32_t idx) noexcept { historyIndex_ = idx; }

  void SetTextInternal(const char* text, size_t textLength);

  void NavigateHistoryNext();

  void NavigateHistoryPrev();

  [[nodiscard]] uint32_t GetDirtyFlags() const noexcept { return dirtyFlags_; }
  void SetDirtyFlags(uint32_t f) noexcept { dirtyFlags_ = f; }
  void AddDirtyFlags(uint32_t f) noexcept { dirtyFlags_ |= f; }
  void ClearDirtyFlags() noexcept { dirtyFlags_ = 0; }

  [[nodiscard]] EditBoxInputLanguage GetInputLanguage() const noexcept {
    return inputLanguage_;
  }
  void SetInputLanguage(EditBoxInputLanguage lang) noexcept {
    if (lang != inputLanguage_) {
      inputLanguage_ = lang;
    }
  }

  void ToggleInputLanguage() noexcept;

  [[nodiscard]] size_t GetIMECompStart() const noexcept { return imeCompStart_; }
  [[nodiscard]] size_t GetIMECompEnd() const noexcept { return imeCompEnd_; }
  void SetIMEComposition(size_t start, size_t end) noexcept {
    imeCompStart_ = start;
    imeCompEnd_ = end;
  }
  [[nodiscard]] bool IsIMEComposing() const noexcept { return imeComposing_; }
  void SetIMEComposing(bool v) noexcept { imeComposing_ = v; }

  void EnsureIMEHighlightTexture();

  void PositionHighlightLine(CSimpleTexture* texture,
                             uint32_t rangeStart,
                             uint32_t rangeEnd);

  void UpdateSelectionHighlights();

  void SetHighlightFirst(CSimpleTexture* tex) noexcept { highlightFirst_ = tex; }
  [[nodiscard]] CSimpleTexture* GetHighlightFirst() const noexcept { return highlightFirst_; }
  void SetHighlightMiddle(CSimpleTexture* tex) noexcept { highlightMiddle_ = tex; }
  [[nodiscard]] CSimpleTexture* GetHighlightMiddle() const noexcept { return highlightMiddle_; }
  void SetHighlightLast(CSimpleTexture* tex) noexcept { highlightLast_ = tex; }
  [[nodiscard]] CSimpleTexture* GetHighlightLast() const noexcept { return highlightLast_; }

  void UpdateIMECompositionRange();

  [[nodiscard]] static bool IsShiftKeyDown() noexcept;
  [[nodiscard]] static bool IsControlKeyDown() noexcept;

  void FireOnEscapePressed();

  void FireOnEnterPressed();

  void FireOnCursorChanged();

  void FireOnCursorChangedEvent(float x, float y, float w, float h);

  [[nodiscard]] float GetCursorChangedX() const noexcept { return cursorChangedX_; }
  [[nodiscard]] float GetCursorChangedY() const noexcept { return cursorChangedY_; }
  [[nodiscard]] float GetCursorChangedW() const noexcept { return cursorChangedW_; }
  [[nodiscard]] float GetCursorChangedH() const noexcept { return cursorChangedH_; }

  void FireOnSpacePressed();

  void FireOnTextSet();

  void FireOnTextChanged();

  [[nodiscard]] bool GetTextChangedUserInput() const noexcept {
    return textChangedUserInput_;
  }

  void FireOnCharFromInsert(std::string_view text);

  [[nodiscard]] const std::string& GetOnCharText() const noexcept {
    return onCharText_;
  }

  int MoveCursorRight(bool extendSel);
  int MoveCursorRightWord(bool extendSel);

  int DeleteCharLeft(bool extendSel);
  int DeleteCharRight(bool extendSel);
  int DeleteWordRight(bool extendSel);
  int DeleteWordLeft(bool extendSel);

  void DeleteByWordCount(int wordCount, bool extendSel);

  void MoveCursorByWord(int wordCount, bool extendSel);
  void MoveCursorHome(bool extendSel);
  void MoveCursorEnd(bool extendSel);
  void MoveCursorToStart(bool extendSel);
  int MoveCursorLeftWord(bool extendSel);
  int MoveCursorToLineStart(bool extendSel);
  int MoveCursorToLineEnd(bool extendSel);
  int SelectAllToStart(bool extendSel);
  int SelectAllToEnd(bool extendSel);

  [[nodiscard]] uint32_t GetVisibleStart() const noexcept { return visibleStart_; }
  void SetVisibleStart(uint32_t v) noexcept { visibleStart_ = v; }

  [[nodiscard]] uint32_t GetVisibleCharCount() const noexcept { return visibleCharCount_; }
  void SetVisibleCharCount(uint32_t v) noexcept { visibleCharCount_ = v; }

  [[nodiscard]] const std::vector<uint32_t>& GetLineBreakPositions() const noexcept {
    return lineBreakPositions_;
  }

  void SetFontString(CSimpleFontString* fs) noexcept { fontString_ = fs; }
  [[nodiscard]] CSimpleFontString* GetFontString() const noexcept { return fontString_; }

  void SetCaretTexture(CSimpleTexture* tex) noexcept { caretTexture_ = tex; }
  [[nodiscard]] CSimpleTexture* GetCaretTexture() const noexcept { return caretTexture_; }

  void ScrollToEnsurePositionVisible(uint32_t targetPos,
                                     float leftWidth,
                                     float rightWidth);

  [[nodiscard]] uint32_t MeasureDisplayUnitSpan(
      uint32_t startPos, int count, bool atomicHyperlinks = false) const;

  [[nodiscard]] bool ScreenPointToCharIndex(float screenX, float screenY,
                                            uint32_t& outCharIndex) const;

  [[nodiscard]] bool DrawsOnUpdate() const noexcept { return drawsOnUpdate_; }
  void SetDrawsOnUpdate(bool d) noexcept { drawsOnUpdate_ = d; }

  [[nodiscard]] uint32_t GetScrollOffset() const noexcept {
    return scrollOffset_;
  }
  void SetScrollOffset(uint32_t off) noexcept { scrollOffset_ = off; }

  void EnsureBufferSize(size_t needed) noexcept {
    if (needed + 1 > maxBytes_) {
      maxBytes_ = static_cast<uint32_t>((needed + 32) & ~31u);
    }
  }

  void NotifyFontUpdate() {

  }

  [[nodiscard]] const char* GetEmbeddedFontLuaName() const noexcept {
    return embeddedFontLuaName_;
  }
  void SetEmbeddedFontLuaName(const char* name) noexcept {
    embeddedFontLuaName_ = name;
  }

  void OnFontSourceStyleChanged(const CSimpleFont& source) {
    if (fontString_ != nullptr) {
      fontString_->CopyMaskedStyleFrom(source, embeddedFontObserverMask_);
    }
    PropagateEmbeddedFontUpdate();
  }

  void PropagateEmbeddedFontUpdate() {

  }

  static bool IsEmbeddedFontTypeToken(int token) noexcept {
    return token == kEmbeddedFontTypeToken;
  }

  static bool IsEmbeddedFontTypeOf(const char* typeName) noexcept {
    return StrCaseEq(typeName, "Font");
  }

  static const char* GetEmbeddedFontTypeName() noexcept {
    return "Font";
  }

  static int GetEmbeddedFontTypeToken() noexcept {
    return kEmbeddedFontTypeToken;
  }

  static constexpr int kEmbeddedFontTypeToken = 5;

 private:

  void BeginSelectionExtend(size_t anchorPos) noexcept {
    if (selectionStart_ == selectionEnd_) {
      selectionEnd_ = anchorPos;
      selectionStart_ = anchorPos;
    }
  }
  void ClearSelectionIfNotExtending(bool extendSel) noexcept {
    if (!extendSel && selectionStart_ != selectionEnd_) {
      dirtyFlags_ |= kDirtySelection;
      selectionEnd_ = 0;
      selectionStart_ = 0;
    }
  }

  std::string text_;
  size_t cursorPos_{0};

  size_t selectionStart_{0};

  size_t selectionEnd_{0};

  int32_t maxBytesLimit_{-1};

  uint32_t scrollOffset_{0};
  uint32_t maxBytes_{32};

  uint32_t maxLetters_{0};

  uint32_t dirtyFlags_{0};

  int inputSource_{0};

  EditBoxInputLanguage inputLanguage_{EditBoxInputLanguage::Roman};
  size_t imeCompStart_{0};
  size_t imeCompEnd_{0};
  bool imeComposing_{false};
  bool numeric_{false};

  bool password_{false};

  bool multiLine_{false};

  bool autoFocus_{true};

  bool drawsOnUpdate_{true};
  bool hasFocus_{false};
  float blinkSpeed_{0.5f};

  float blinkTimer_{0.0f};

  float textInsetLeft_{0.0f};

  float textInsetRight_{0.0f};

  float textInsetTop_{0.0f};

  float textInsetBottom_{0.0f};

  bool altArrowKeyMode_{false};

  bool countInvisible_{false};

  uint32_t historyLines_{0};

  int32_t historyIndex_{0};

  EditHistory editHistory_;

  const char* embeddedFontLuaName_{nullptr};
  uint32_t embeddedFontObserverMask_{0x1Fu};

  uint32_t visibleStart_{0};

  uint32_t visibleCharCount_{0};

  std::vector<uint32_t> lineBreakPositions_;

  CSimpleFontString* fontString_{nullptr};

  std::vector<uint32_t> charInfoBuffer_;

  CSimpleTexture* highlightFirst_{nullptr};

  CSimpleTexture* highlightMiddle_{nullptr};

  CSimpleTexture* highlightLast_{nullptr};

  CSimpleTexture* caretTexture_{nullptr};

  CSimpleTexture* imeHighlightTexture_{nullptr};

  static CSimpleEditBox* s_focusedEditBox_;

  void HandleFocusLostCleanup();

  CSimpleFrame* imeCandidateFrame_{nullptr};

  float cursorChangedX_{0.0f};
  float cursorChangedY_{0.0f};
  float cursorChangedW_{0.0f};
  float cursorChangedH_{0.0f};

  bool textChangedUserInput_{false};

  std::string onCharText_;
};

}


#include "openwow/ui/widgets/simple_edit_box.h"

#include "openwow/core/storm_string.h"
#include "openwow/foundation/compiler/wide_ctype.h"
#include "openwow/platform/adapters/ime/os_ime.h"
#include "openwow/platform/process/os_platform.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/ui/ui_pixel_snap.h"
#include "openwow/ui/widgets/simple_font_string.h"
#include "openwow/ui/widgets/simple_texture.h"

#include <SDL2/SDL.h>

#include <cctype>
#include <cmath>
#include <cstdint>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <imm.h>
#endif

namespace openwow::ui::widgets {

CSimpleEditBox* CSimpleEditBox::s_focusedEditBox_ = nullptr;

bool CSimpleEditBox::IsShiftKeyDown() noexcept {
  return (SDL_GetModState() & KMOD_SHIFT) != 0;
}

bool CSimpleEditBox::IsControlKeyDown() noexcept {
  return (SDL_GetModState() & KMOD_CTRL) != 0;
}

void CSimpleEditBox::FireOnHide() {

  CSimpleFrame::FireOnHide();

  if (s_focusedEditBox_ != this) {
    return;
  }

  s_focusedEditBox_ = nullptr;

  const bool suppressIme = password_ || numeric_;
  if (!suppressIme) {
    HandleFocusLostCleanup();
  }

  if (HasScript("OnEditFocusLost")) {
    RunScript("OnEditFocusLost");
  }

  dirtyFlags_ |= kDirtyCursor;
}

void CSimpleEditBox::FireOnEscapePressed() {
  if (HasScript("OnEscapePressed")) {
    RunScript("OnEscapePressed");
  }

}

void CSimpleEditBox::FireOnEnterPressed() {
  if (HasScript("OnEnterPressed")) {
    RunScript("OnEnterPressed");
  }

}

void CSimpleEditBox::FireOnCursorChanged() {
  if (HasScript("OnCursorChanged")) {
    RunScript("OnCursorChanged");
  }
}

void CSimpleEditBox::FireOnCursorChangedEvent(float x, float y,
                                               float w, float h) {
  if (!HasScript("OnCursorChanged")) {
    return;
  }

  const float px = openwow::ui::StoredUiHorizontalCoordinateToPixels(x);
  const float py = openwow::ui::StoredUiHorizontalCoordinateToPixels(y);
  const float pw = openwow::ui::StoredUiHorizontalCoordinateToPixels(w);
  const float ph = openwow::ui::StoredUiHorizontalCoordinateToPixels(h);

  cursorChangedX_ = px;
  cursorChangedY_ = py;
  cursorChangedW_ = pw;
  cursorChangedH_ = ph;

  RunScript("OnCursorChanged");
}

void CSimpleEditBox::FireOnSpacePressed() {
  if (HasScript("OnSpacePressed")) {
    RunScript("OnSpacePressed");
  }
}

void CSimpleEditBox::FireOnTextSet() {
  if (HasScript("OnTextSet")) {
    RunScript("OnTextSet");
  }
}

void CSimpleEditBox::FireOnTextChanged() {
  if (HasScript("OnTextChanged")) {
    textChangedUserInput_ = (dirtyFlags_ & kDirtyUserModified) != 0;
    RunScript("OnTextChanged");
  }
}

void CSimpleEditBox::FireOnCharFromInsert(std::string_view text) {
  onCharText_.assign(text.data(), text.size());
  if (HasScript("OnChar")) {
    RunScript("OnChar");
  }
}

void CSimpleEditBox::RebuildCharInfoBuffer() {
  using openwow::ui::glue::CharInfoRecord;
  using openwow::ui::glue::ClassifyWowTextElement;
  using openwow::ui::glue::WowTextEscapeType;

  charInfoBuffer_.assign(text_.size(), 0);

  if (text_.empty()) {
    return;
  }

  const std::string_view tv{text_};
  std::size_t offset = 0;
  bool inHyperlink = false;

  while (offset < tv.size()) {
    auto info = ClassifyWowTextElement(tv, offset);

    if (info.type == WowTextEscapeType::HyperlinkStart) {
      inHyperlink = true;
    }

    charInfoBuffer_[offset] =
        CharInfoRecord::Pack(info.byte_count, info.type, inHyperlink);

    offset = info.next_offset;

    if (info.type == WowTextEscapeType::HyperlinkEnd) {
      inHyperlink = false;
    }
  }
}

void CSimpleEditBox::Insert(std::string_view text, int inputSource,
                            bool fireEvents, bool keepSelection,
                            bool userInput) {

  if (cursorPos_ > 0 && cursorPos_ < charInfoBuffer_.size() &&
      (charInfoBuffer_[cursorPos_] & 0x80000000u) != 0) {
    size_t pos = cursorPos_;
    while (pos > 0) {
      --pos;
      if (charInfoBuffer_[pos] != 0) {
        break;
      }
    }

    if (pos < charInfoBuffer_.size() &&
        (charInfoBuffer_[pos] & 0x80000000u) != 0) {
      return;
    }
  }

  if (selectionStart_ != selectionEnd_) {

    const size_t textLen = text_.size();
    size_t lo = std::min(selectionStart_, textLen);
    size_t hi = std::min(selectionEnd_, textLen);
    if (lo > hi) std::swap(lo, hi);
    selectionStart_ = 0;
    selectionEnd_ = 0;

    if (lo < hi) {
      text_.erase(lo, hi - lo);
      cursorPos_ = lo;
      dirtyFlags_ |= (kDirtyText | kDirtyCursor);
      if (userInput) {
        dirtyFlags_ |= kDirtyUserModified;
      } else {
        dirtyFlags_ &= ~kDirtyUserModified;
      }
      if (text_.empty()) {
        inputSource_ = 0;
      }
    }
  }

  if (text.empty()) {
    text = std::string_view{};
  }

  const size_t insertLen = text.size();

  if (numeric_ && !keepSelection && insertLen > 0) {
    for (size_t i = 0; i < text.size();) {
      unsigned char ch = static_cast<unsigned char>(text[i]);
      uint32_t codepoint;

      if (ch < 0x80) {
        codepoint = ch;
        ++i;
      } else if ((ch & 0xE0) == 0xC0 && i + 1 < text.size()) {
        codepoint = (ch & 0x1F) << 6 |
                    (static_cast<unsigned char>(text[i + 1]) & 0x3F);
        i += 2;
      } else if ((ch & 0xF0) == 0xE0 && i + 2 < text.size()) {
        codepoint = (ch & 0x0F) << 12 |
                    (static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6 |
                    (static_cast<unsigned char>(text[i + 2]) & 0x3F);
        i += 3;
      } else if ((ch & 0xF8) == 0xF0 && i + 3 < text.size()) {
        codepoint = (ch & 0x07) << 18 |
                    (static_cast<unsigned char>(text[i + 1]) & 0x3F) << 12 |
                    (static_cast<unsigned char>(text[i + 2]) & 0x3F) << 6 |
                    (static_cast<unsigned char>(text[i + 3]) & 0x3F);
        i += 4;
      } else {
        return;
      }
      if (codepoint < '0' || codepoint > '9') {
        return;
      }
    }
  }

  if (cursorPos_ > text_.size()) {
    cursorPos_ = text_.size();
  }

  const size_t savedCursorPos = cursorPos_;

  text_.insert(cursorPos_, text);
  cursorPos_ += insertLen;

  RebuildCharInfoBuffer();

  dirtyFlags_ |= (kDirtyText | kDirtyCursor);

  if (userInput) {
    dirtyFlags_ |= kDirtyUserModified;
  } else {
    dirtyFlags_ &= ~kDirtyUserModified;
  }

  if (maxBytesLimit_ >= 0 &&
      static_cast<int>(text_.size()) > maxBytesLimit_) {

    size_t truncPos = static_cast<size_t>(maxBytesLimit_);

    while (truncPos > 0 &&
           (static_cast<unsigned char>(text_[truncPos]) & 0xC0) == 0x80) {
      --truncPos;
    }
    text_.resize(truncPos);
    if (cursorPos_ > text_.size()) {
      cursorPos_ = text_.size();
    }
  }

  if (maxLetters_ > 0) {
    const int letterCount = CountLetters();
    if (letterCount > static_cast<int>(maxLetters_)) {
      if (countInvisible_) {

        text_ = openwow::text::Utf8TakeCodepoints(text_, static_cast<int>(maxLetters_));
      } else {

        const std::string_view tv{text_};
        size_t offset = 0;
        int counted = 0;
        while (offset < tv.size() && counted < static_cast<int>(maxLetters_)) {
          bool visible = false;
          offset = openwow::ui::glue::AdvanceWowTextElement(tv, offset, &visible);
          if (visible) {
            ++counted;
          }
        }

        while (offset < tv.size()) {
          bool visible = false;
          size_t next = openwow::ui::glue::AdvanceWowTextElement(tv, offset, &visible);
          if (visible) break;
          offset = next;
        }
        text_.resize(offset);
      }
      if (cursorPos_ > text_.size()) {
        cursorPos_ = text_.size();
      }
    }
  }

  if (keepSelection) {
    selectionStart_ = savedCursorPos;
    size_t selEnd = cursorPos_;
    if (selEnd > text_.size()) {
      selEnd = text_.size();
    }
    dirtyFlags_ |= kDirtySelection;
    selectionEnd_ = selEnd;
  }

  if (inputSource_ == 0 && !text_.empty()) {
    inputSource_ = inputSource;
  }

  if (fireEvents) {
    FireOnCharFromInsert(text);

    for (size_t i = 0; i < text.size();) {
      unsigned char ch = static_cast<unsigned char>(text[i]);
      if (ch < 0x80) {
        if (ch == ' ') {
          FireOnSpacePressed();
          break;
        }
        ++i;
      } else if ((ch & 0xE0) == 0xC0) {
        i += 2;
      } else if ((ch & 0xF0) == 0xE0) {
        i += 3;
      } else {
        i += 4;
      }
    }
  }
}

void CSimpleEditBox::SetTextInternal(const char* text, size_t textLength) {
  if (selectionStart_ != selectionEnd_) {
    dirtyFlags_ |= kDirtySelection;
    selectionEnd_ = 0;
    selectionStart_ = 0;
  }

  if (openwow::core::SStrCmpI(text, text_.c_str(), 0x7FFFFFFFu) != 0) {
    text_.clear();
    cursorPos_ = 0;
    charInfoBuffer_.clear();

    visibleStart_ = 0;

    Insert(std::string_view(text, textLength),
           static_cast<int>(textLength), false, false, false);

    FireOnTextSet();
  }
}

void CSimpleEditBox::NavigateHistoryNext() {
  const auto count = static_cast<int>(historyLines_);
  if (count < 1)
    return;

  for (int attempts = 1; attempts <= count; ++attempts) {
    int idx = (historyIndex_ + attempts) % count;
    const auto& entry = editHistory_.At(static_cast<uint32_t>(idx));
    if (!entry.text.empty()) {
      historyIndex_ = idx;
      SetTextInternal(entry.text.c_str(), entry.cursorPos);
      return;
    }
  }
}

void CSimpleEditBox::NavigateHistoryPrev() {
  const auto count = static_cast<int>(historyLines_);
  if (count < 1)
    return;

  for (int attempts = 1; attempts <= count; ++attempts) {
    int idx = ((historyIndex_ - attempts) % count + count) % count;
    const auto& entry = editHistory_.At(static_cast<uint32_t>(idx));
    if (!entry.text.empty()) {
      historyIndex_ = idx;
      SetTextInternal(entry.text.c_str(), entry.cursorPos);
      return;
    }
  }
}

void CSimpleEditBox::HandleFocusLostCleanup() {

  if (imeCandidateFrame_ != nullptr) {
    imeCandidateFrame_->Hide();
    imeCandidateFrame_ = nullptr;
  }

  imeComposing_ = false;

  openwow::platform::IME_SetEnabled(0);
}

void CSimpleEditBox::EnsureIMEHighlightTexture() {
  if (imeHighlightTexture_ != nullptr)
    return;

  auto* tex = new CSimpleTexture();
  tex->SetParent(this);
  tex->SetDrawLayer(DrawLayer::Overlay);

  constexpr float kR = 0x21 / 255.0f;
  constexpr float kG = 0xD1 / 255.0f;
  constexpr float kB = 0xF0 / 255.0f;
  tex->SetColorTexture(kR, kG, kB, 1.0f);
  tex->SetBlendMode(BlendMode::Add);

  if (fontString_ != nullptr) {
    const float lineHeight = fontString_->GetStringHeight();
    tex->SetHeight(lineHeight);
  }

  imeHighlightTexture_ = tex;
}

void CSimpleEditBox::PositionHighlightLine(CSimpleTexture* texture,
                                           uint32_t rangeStart,
                                           uint32_t rangeEnd) {
  if (!texture || !fontString_ || lineBreakPositions_.size() < 2)
    return;

  std::string passwordBuf;
  const char* displayText;
  if (password_) {
    passwordBuf.assign(text_.size(), '*');
    displayText = passwordBuf.c_str();
  } else {
    displayText = text_.c_str();
  }

  const float scale = GetEffectiveScale();
  const float safeScale = (scale > 0.0f) ? scale : 1.0f;
  const float lineHeightSnapped =
      openwow::ui::LegacyPixelSnapUiVerticalCoordinate(
          fontString_->GetRender().GetFontHeight() * safeScale);
  const float spacingSnapped =
      openwow::ui::LegacyPixelSnapUiVerticalCoordinate(
          fontString_->GetSpacing() * safeScale);
  const float lineAdvance = (lineHeightSnapped + spacingSnapped) / safeScale;

  const uint32_t maxLineIdx =
      static_cast<uint32_t>(lineBreakPositions_.size()) - 2;
  uint32_t lineIdx = 0;
  float yOffset = 0.0f;

  if (rangeStart >= lineBreakPositions_[1]) {
    for (uint32_t i = 0; i < maxLineIdx; ++i) {
      if (rangeStart < lineBreakPositions_[i + 1])
        break;
      yOffset -= lineAdvance;
      lineIdx = i + 1;
    }
  }

  const uint32_t lineStart = lineBreakPositions_[lineIdx];
  const uint32_t lineEnd   = lineBreakPositions_[lineIdx + 1];

  if (rangeStart < lineStart)
    rangeStart = lineStart;
  if (rangeEnd > lineEnd)
    rangeEnd = lineEnd;

  float highlightWidth;
  if (lineIdx < maxLineIdx && rangeEnd == lineEnd) {
    highlightWidth = fontString_->GetStringWidth();
  } else {
    highlightWidth = fontString_->MeasureSubstringWidth(
        displayText + rangeStart,
        static_cast<int>(rangeEnd - rangeStart));
  }

  texture->ClearAllPoints();

  const JustifyH justify = fontString_->GetJustifyH();

  RegionAnchor anchor;
  anchor.relativeTo = fontString_;
  anchor.offsetY = yOffset;

  switch (justify) {
    case JustifyH::Left: {

      float xOff = 0.0f;
      if (rangeStart != lineStart) {
        xOff = fontString_->MeasureSubstringWidth(
            displayText + lineStart,
            static_cast<int>(rangeStart - lineStart));
      }
      anchor.offsetX = xOff;
      if (multiLine_) {
        anchor.point         = FramePoint::TopLeft;
        anchor.relativePoint = FramePoint::TopLeft;
      } else {
        anchor.point         = FramePoint::Left;
        anchor.relativePoint = FramePoint::Left;
      }
      break;
    }
    case JustifyH::Center: {

      float leftOff = 0.0f;
      if (rangeStart != lineStart) {
        leftOff = fontString_->MeasureSubstringWidth(
            displayText + lineStart,
            static_cast<int>(rangeStart - lineStart));
      }
      const float fullLineWidth = fontString_->MeasureSubstringWidth(
          displayText + lineStart,
          static_cast<int>(lineEnd - lineStart));
      anchor.offsetX = leftOff - fullLineWidth * 0.5f;
      if (multiLine_) {
        anchor.point         = FramePoint::TopLeft;
        anchor.relativePoint = FramePoint::Top;
      } else {
        anchor.point         = FramePoint::Left;
        anchor.relativePoint = FramePoint::Center;
      }
      break;
    }
    case JustifyH::Right: {

      float xOff = 0.0f;
      if (rangeStart != lineEnd) {
        xOff = -fontString_->MeasureSubstringWidth(
            displayText + rangeStart,
            static_cast<int>(lineEnd - rangeStart));
      }
      anchor.offsetX = xOff;
      if (multiLine_) {
        anchor.point         = FramePoint::TopLeft;
        anchor.relativePoint = FramePoint::TopRight;
      } else {
        anchor.point         = FramePoint::Left;
        anchor.relativePoint = FramePoint::Right;
      }
      break;
    }
  }

  texture->SetPoint(anchor);

  texture->SetWidth(highlightWidth);
  texture->MarkLayoutDirty();

  constexpr float kWidthEpsilon = 0.00000023841858f;

  if (rangeStart >= rangeEnd || std::fabs(highlightWidth) < kWidthEpsilon) {
    texture->HideVisible();
  } else {
    texture->ShowVisible();
  }
}

void CSimpleEditBox::UpdateSelectionHighlights() {

  if (imeHighlightTexture_ != nullptr) {
    if (imeComposing_) {
      PositionHighlightLine(imeHighlightTexture_,
                            static_cast<uint32_t>(imeCompStart_),
                            static_cast<uint32_t>(imeCompEnd_));
    } else {
      imeHighlightTexture_->HideVisible();
    }
  }

  if (lineBreakPositions_.size() < 2)
    return;

  const auto selStart = static_cast<uint32_t>(selectionStart_);
  const auto selEnd   = static_cast<uint32_t>(selectionEnd_);
  const uint32_t maxLineIdx =
      static_cast<uint32_t>(lineBreakPositions_.size()) - 2;

  uint32_t startLine = 0;
  if (selStart >= lineBreakPositions_[1]) {
    for (uint32_t i = 0; i < maxLineIdx; ++i) {
      if (selStart < lineBreakPositions_[i + 1])
        break;
      startLine = i + 1;
    }
  }

  uint32_t endLine = 0;
  if (selEnd >= lineBreakPositions_[1]) {
    for (uint32_t i = 0; i < maxLineIdx; ++i) {
      if (selEnd < lineBreakPositions_[i + 1])
        break;
      endLine = i + 1;
    }
  }

  const uint32_t lineSpan = endLine - startLine + 1;

  if (startLine == endLine) {

    if (highlightFirst_)
      PositionHighlightLine(highlightFirst_, selStart, selEnd);

    if (highlightMiddle_)
      highlightMiddle_->HideVisible();
    if (highlightLast_)
      highlightLast_->HideVisible();
  } else {

    if (highlightFirst_) {
      PositionHighlightLine(highlightFirst_, selStart,
                            lineBreakPositions_[startLine + 1]);
    }

    if (highlightMiddle_) {
      if (lineSpan > 2)
        highlightMiddle_->ShowVisible();
      else
        highlightMiddle_->HideVisible();
    }

    if (highlightLast_) {
      PositionHighlightLine(highlightLast_,
                            lineBreakPositions_[endLine], selEnd);
    }
  }
}

void CSimpleEditBox::UpdateIMECompositionRange() {

  const auto& comp = openwow::platform::OsImeStub::Instance().GetComposition();

  const bool hasClauseInfo =
      !comp.compositionText.empty() &&
      (comp.clauseStart != 0 || comp.clauseEnd != 0);

  if (hasClauseInfo) {

    EnsureIMEHighlightTexture();

    const auto base = static_cast<uint32_t>(selectionStart_);

    const uint32_t startBytes =
        MeasureDisplayUnitSpan(base, comp.clauseStart);
    const uint32_t endBytes =
        MeasureDisplayUnitSpan(base, comp.clauseEnd);
    const uint32_t cursorBytes =
        MeasureDisplayUnitSpan(base, comp.cursorPos);

    imeCompStart_ = base + startBytes;

    imeCompEnd_   = base + endBytes;

    cursorPos_    = base + cursorBytes;

    dirtyFlags_ |= kDirtySelection | kDirtyCursor;
  } else {

    imeCompStart_ = cursorPos_;

    imeCompEnd_   = cursorPos_;

  }
}

const char* EditBoxInputLanguageToken(const EditBoxInputLanguage language) noexcept {
  switch (language) {
    case EditBoxInputLanguage::Roman:
      return "ROMAN";
    case EditBoxInputLanguage::Korean:
      return "KOREAN";
    case EditBoxInputLanguage::Chinese:
      return "CHINESE";
    case EditBoxInputLanguage::Japanese:
      return "JAPANESE";
  }

  return "ROMAN";
}

EditBoxInputLanguage DetectActiveEditBoxInputLanguage() noexcept {
#if defined(_WIN32)
  constexpr DWORD kImeNativeMode = 0x1u;
  constexpr std::uintptr_t kRomanChineseHkl = 0x08040804u;

  auto* const active_window = static_cast<HWND>(openwow::platform::OS_GetActiveWindow(0));
  if (active_window == nullptr) {
    return EditBoxInputLanguage::Roman;
  }

  HIMC const context = ::ImmGetContext(active_window);
  if (context == nullptr) {
    return EditBoxInputLanguage::Roman;
  }

  DWORD conversion = 0;
  DWORD sentence = 0;
  (void)::ImmGetConversionStatus(context, &conversion, &sentence);

  EditBoxInputLanguage result = EditBoxInputLanguage::Roman;
  if ((conversion & kImeNativeMode) != 0u) {
    const std::uintptr_t keyboard_layout =
        reinterpret_cast<std::uintptr_t>(::GetKeyboardLayout(0));
    switch (static_cast<unsigned char>(keyboard_layout & 0xFFu)) {
      case 0x11u:
        result = EditBoxInputLanguage::Japanese;
        break;
      case 0x12u:
        result = EditBoxInputLanguage::Korean;
        break;
      case 0x04u:
        if (keyboard_layout != kRomanChineseHkl) {
          result = EditBoxInputLanguage::Chinese;
        }
        break;
      default:
        break;
    }
  }

  ::ImmReleaseContext(active_window, context);
  return result;
#else
  return EditBoxInputLanguage::Roman;
#endif
}

static size_t Utf8CharLen(const std::string& text, size_t pos) {
  if (pos >= text.size()) return 0;
  auto c = static_cast<unsigned char>(text[pos]);
  if (c < 0x80) return 1;
  if ((c & 0xE0) == 0xC0) return (pos + 1 < text.size()) ? 2 : 1;
  if ((c & 0xF0) == 0xE0) return std::min<size_t>(3, text.size() - pos);
  if ((c & 0xF8) == 0xF0) return std::min<size_t>(4, text.size() - pos);
  return 1;
}

static size_t Utf8CharLenBack(const std::string& text, size_t pos) {
  if (pos == 0 || pos > text.size()) return 0;
  size_t back = 1;
  while (back < pos && back < 4 &&
         (static_cast<unsigned char>(text[pos - back]) & 0xC0) == 0x80)
    ++back;
  return back;
}

static int Utf8Decode(const std::string& text, size_t pos) {
  return openwow::core::DecodeNextLegacyUtf8Codepoint(text.c_str() + pos, nullptr);
}

static bool IsWideSpace(int cp) {
  if (cp < 0) {
    return false;
  }
  return openwow::compiler::IsUnicodeWhitespace(static_cast<std::uint32_t>(cp));
}

static void ExtendSelectionBy(CSimpleEditBox& eb, size_t ,
                              int delta) {

  size_t end = eb.GetSelectionEnd();
  if (delta > 0) {
    eb.SetSelection(eb.GetSelectionStart(),
                    end + static_cast<size_t>(delta));
  } else {
    size_t sub = static_cast<size_t>(-delta);
    eb.SetSelection(eb.GetSelectionStart(),
                    end >= sub ? end - sub : 0);
  }
}

int CSimpleEditBox::MoveCursorRight(bool extendSel) {
  if (cursorPos_ >= text_.size()) {

    ClearSelectionIfNotExtending(extendSel);
    return 0;
  }

  size_t charBytes = Utf8CharLen(text_, cursorPos_);

  if (extendSel) {
    BeginSelectionExtend(cursorPos_);
    ExtendSelectionBy(*this, cursorPos_, static_cast<int>(charBytes));
    cursorPos_ += charBytes;
    dirtyFlags_ |= kDirtyCursor;
  } else {
    if (selectionStart_ != selectionEnd_) {
      dirtyFlags_ |= kDirtySelection;
      selectionEnd_ = 0;
      selectionStart_ = 0;
    }
    cursorPos_ += charBytes;
    dirtyFlags_ |= kDirtyCursor;
  }

  ClearSelectionIfNotExtending(extendSel);
  return 0;
}

int CSimpleEditBox::MoveCursorRightWord(bool extendSel) {

  while (cursorPos_ < text_.size()) {
    const int cp = Utf8Decode(text_, cursorPos_);
    if (IsWideSpace(cp)) break;

    size_t charBytes = Utf8CharLen(text_, cursorPos_);
    if (extendSel) {
      BeginSelectionExtend(cursorPos_);
      ExtendSelectionBy(*this, cursorPos_, static_cast<int>(charBytes));
    } else if (selectionStart_ != selectionEnd_) {
      dirtyFlags_ |= kDirtySelection;
      selectionEnd_ = 0;
      selectionStart_ = 0;
    }
    cursorPos_ += charBytes;
    dirtyFlags_ |= kDirtyCursor;
  }

  while (cursorPos_ < text_.size()) {
    const int cp = Utf8Decode(text_, cursorPos_);
    if (!IsWideSpace(cp)) break;

    size_t charBytes = Utf8CharLen(text_, cursorPos_);
    if (extendSel) {
      BeginSelectionExtend(cursorPos_);
      ExtendSelectionBy(*this, cursorPos_, static_cast<int>(charBytes));
    } else if (selectionStart_ != selectionEnd_) {
      dirtyFlags_ |= kDirtySelection;
      selectionEnd_ = 0;
      selectionStart_ = 0;
    }
    cursorPos_ += charBytes;
    dirtyFlags_ |= kDirtyCursor;
  }

  ClearSelectionIfNotExtending(extendSel);
  return 0;
}

int CSimpleEditBox::DeleteCharLeft(bool ) {
  if (selectionStart_ != selectionEnd_) {

    size_t lo = std::min(selectionStart_, selectionEnd_);
    size_t hi = std::max(selectionStart_, selectionEnd_);
    text_.erase(lo, hi - lo);
    cursorPos_ = lo;
    selectionStart_ = 0;
    selectionEnd_ = 0;
    dirtyFlags_ |= kDirtySelection | kDirtyCursor;
    return 0;
  }

  if (cursorPos_ > 0) {
    size_t charBytes = Utf8CharLenBack(text_, cursorPos_);
    text_.erase(cursorPos_ - charBytes, charBytes);
    cursorPos_ -= charBytes;
    dirtyFlags_ |= kDirtyCursor;
  }
  return 0;
}

int CSimpleEditBox::DeleteCharRight(bool ) {
  if (selectionStart_ != selectionEnd_) {
    size_t lo = std::min(selectionStart_, selectionEnd_);
    size_t hi = std::max(selectionStart_, selectionEnd_);
    text_.erase(lo, hi - lo);
    cursorPos_ = lo;
    selectionStart_ = 0;
    selectionEnd_ = 0;
    dirtyFlags_ |= kDirtySelection | kDirtyCursor;
    return 0;
  }

  if (cursorPos_ < text_.size()) {
    size_t charBytes = Utf8CharLen(text_, cursorPos_);
    text_.erase(cursorPos_, charBytes);
    dirtyFlags_ |= kDirtyCursor;
  }
  return 0;
}

int CSimpleEditBox::DeleteWordRight(bool ) {
  if (selectionStart_ != selectionEnd_) {
    size_t lo = std::min(selectionStart_, selectionEnd_);
    size_t hi = std::max(selectionStart_, selectionEnd_);
    text_.erase(lo, hi - lo);
    cursorPos_ = lo;
    selectionStart_ = 0;
    selectionEnd_ = 0;
    dirtyFlags_ |= kDirtySelection | kDirtyCursor;
    return 0;
  }

  while (cursorPos_ < text_.size()) {
    const int cp = Utf8Decode(text_, cursorPos_);
    if (!IsWideSpace(cp)) break;
    size_t charBytes = Utf8CharLen(text_, cursorPos_);
    text_.erase(cursorPos_, charBytes);
    dirtyFlags_ |= kDirtyCursor;
  }

  while (cursorPos_ < text_.size()) {
    const int cp = Utf8Decode(text_, cursorPos_);
    if (IsWideSpace(cp)) break;
    size_t charBytes = Utf8CharLen(text_, cursorPos_);
    text_.erase(cursorPos_, charBytes);
    dirtyFlags_ |= kDirtyCursor;
  }

  return 0;
}

int CSimpleEditBox::DeleteWordLeft(bool ) {
  if (selectionStart_ != selectionEnd_) {
    size_t lo = std::min(selectionStart_, selectionEnd_);
    size_t hi = std::max(selectionStart_, selectionEnd_);
    text_.erase(lo, hi - lo);
    cursorPos_ = lo;
    selectionStart_ = 0;
    selectionEnd_ = 0;
    dirtyFlags_ |= kDirtySelection | kDirtyCursor;
    return 0;
  }

  while (cursorPos_ > 0) {

    size_t backLen = Utf8CharLenBack(text_, cursorPos_);
    const int cp = Utf8Decode(text_, cursorPos_ - backLen);
    if (!IsWideSpace(cp)) break;
    text_.erase(cursorPos_ - backLen, backLen);
    cursorPos_ -= backLen;
    dirtyFlags_ |= kDirtyCursor;
  }

  while (cursorPos_ > 0) {
    size_t backLen = Utf8CharLenBack(text_, cursorPos_);
    const int cp = Utf8Decode(text_, cursorPos_ - backLen);
    if (IsWideSpace(cp)) break;
    text_.erase(cursorPos_ - backLen, backLen);
    cursorPos_ -= backLen;
    dirtyFlags_ |= kDirtyCursor;
  }

  return 0;
}

void CSimpleEditBox::DeleteByWordCount(int wordCount, bool extendSel) {
  if (!wordCount) {
    return;
  }

  uint32_t bytes = MeasureDisplayUnitSpan(
      static_cast<uint32_t>(cursorPos_), wordCount, true);
  if (!bytes)
    return;

  size_t from, to;
  if (wordCount >= 0) {
    from = cursorPos_;
    to   = std::min(cursorPos_ + bytes, text_.size());
  } else {
    from = (bytes <= cursorPos_) ? cursorPos_ - bytes : 0;
    to   = cursorPos_;
  }

  if (from >= to)
    return;

  text_.erase(from, to - from);
  cursorPos_ = from;

  if (selectionStart_ != selectionEnd_) {
    dirtyFlags_ |= kDirtySelection;
    selectionStart_ = 0;
    selectionEnd_ = 0;
  }

  dirtyFlags_ |= kDirtyText | kDirtyCursor;
  if (!extendSel)
    dirtyFlags_ |= kDirtyUserModified;

  if (text_.empty())
    inputSource_ = 0;
}

void CSimpleEditBox::MoveCursorByWord(int wordCount, bool extendSel) {
  if (!wordCount) {
    return;
  }

  uint32_t dist = MeasureDisplayUnitSpan(
      static_cast<uint32_t>(cursorPos_), wordCount, true);
  int delta = static_cast<int>(dist);
  if (wordCount < 0)
    delta = -delta;

  if (extendSel) {
    BeginSelectionExtend(cursorPos_);
    ExtendSelectionBy(*this, cursorPos_, delta);
    cursorPos_ = static_cast<size_t>(
        std::max(0, static_cast<int>(cursorPos_) + delta));
    dirtyFlags_ |= kDirtyCursor;
  } else {
    if (selectionStart_ != selectionEnd_) {
      dirtyFlags_ |= kDirtySelection;
      selectionEnd_ = 0;
      selectionStart_ = 0;
    }
    cursorPos_ = static_cast<size_t>(
        std::max(0, static_cast<int>(cursorPos_) + delta));
    dirtyFlags_ |= kDirtyCursor;
  }
}

void CSimpleEditBox::MoveCursorHome(bool extendSel) {
  if (cursorPos_ > 0) {

    if (extendSel) {
      BeginSelectionExtend(cursorPos_);

      ExtendSelectionBy(*this, cursorPos_,
                        -static_cast<int>(cursorPos_));
    }
    cursorPos_ = 0;
    dirtyFlags_ |= kDirtyCursor;
  }
  ClearSelectionIfNotExtending(extendSel);
}

void CSimpleEditBox::MoveCursorEnd(bool extendSel) {
  if (cursorPos_ < text_.size()) {
    size_t dist = text_.size() - cursorPos_;
    if (extendSel) {
      BeginSelectionExtend(cursorPos_);
      ExtendSelectionBy(*this, cursorPos_, static_cast<int>(dist));
    }
    cursorPos_ = text_.size();
    dirtyFlags_ |= kDirtyCursor;
  }
  ClearSelectionIfNotExtending(extendSel);
}

void CSimpleEditBox::MoveCursorToStart(bool extendSel) {

  if (cursorPos_ > 0)
    MoveCursorByWord(-1, extendSel);
  ClearSelectionIfNotExtending(extendSel);
}

int CSimpleEditBox::MoveCursorLeftWord(bool extendSel) {
  if (cursorPos_ > 0) {

    while (cursorPos_ > 0) {
      size_t backLen = Utf8CharLenBack(text_, cursorPos_);
      const int cp = Utf8Decode(text_, cursorPos_ - backLen);
      if (!IsWideSpace(cp)) break;
      if (extendSel) {
        BeginSelectionExtend(cursorPos_);
        ExtendSelectionBy(*this, cursorPos_, -static_cast<int>(backLen));
      } else if (selectionStart_ != selectionEnd_) {
        dirtyFlags_ |= kDirtySelection;
        selectionEnd_ = 0;
        selectionStart_ = 0;
      }
      cursorPos_ -= backLen;
      dirtyFlags_ |= kDirtyCursor;
    }

    while (cursorPos_ > 0) {
      size_t backLen = Utf8CharLenBack(text_, cursorPos_);
      const int cp = Utf8Decode(text_, cursorPos_ - backLen);
      if (IsWideSpace(cp)) break;
      if (extendSel) {
        BeginSelectionExtend(cursorPos_);
        ExtendSelectionBy(*this, cursorPos_, -static_cast<int>(backLen));
      } else if (selectionStart_ != selectionEnd_) {
        dirtyFlags_ |= kDirtySelection;
        selectionEnd_ = 0;
        selectionStart_ = 0;
      }
      cursorPos_ -= backLen;
      dirtyFlags_ |= kDirtyCursor;
    }
  }

  ClearSelectionIfNotExtending(extendSel);
  return 0;
}

int CSimpleEditBox::MoveCursorToLineStart(bool extendSel) {
  while (cursorPos_ > 0) {

    if (text_[cursorPos_ - 1] == '\n') break;

    size_t backLen = Utf8CharLenBack(text_, cursorPos_);
    if (extendSel) {
      BeginSelectionExtend(cursorPos_);
      ExtendSelectionBy(*this, cursorPos_, -static_cast<int>(backLen));
    } else if (selectionStart_ != selectionEnd_) {
      dirtyFlags_ |= kDirtySelection;
      selectionEnd_ = 0;
      selectionStart_ = 0;
    }
    cursorPos_ -= backLen;
    dirtyFlags_ |= kDirtyCursor;
  }

  ClearSelectionIfNotExtending(extendSel);
  return 0;
}

int CSimpleEditBox::MoveCursorToLineEnd(bool extendSel) {
  while (cursorPos_ < text_.size()) {
    if (text_[cursorPos_] == '\n') break;

    size_t charBytes = Utf8CharLen(text_, cursorPos_);
    if (extendSel) {
      BeginSelectionExtend(cursorPos_);
      ExtendSelectionBy(*this, cursorPos_, static_cast<int>(charBytes));
    } else if (selectionStart_ != selectionEnd_) {
      dirtyFlags_ |= kDirtySelection;
      selectionEnd_ = 0;
      selectionStart_ = 0;
    }
    cursorPos_ += charBytes;
    dirtyFlags_ |= kDirtyCursor;
  }

  ClearSelectionIfNotExtending(extendSel);
  return 0;
}

int CSimpleEditBox::SelectAllToStart(bool extendSel) {
  while (cursorPos_ > 0) {
    size_t backLen = Utf8CharLenBack(text_, cursorPos_);
    if (extendSel) {
      BeginSelectionExtend(cursorPos_);
      ExtendSelectionBy(*this, cursorPos_, -static_cast<int>(backLen));
    } else if (selectionStart_ != selectionEnd_) {
      dirtyFlags_ |= kDirtySelection;
      selectionEnd_ = 0;
      selectionStart_ = 0;
    }
    cursorPos_ -= backLen;
    dirtyFlags_ |= kDirtyCursor;
  }

  ClearSelectionIfNotExtending(extendSel);
  return 0;
}

int CSimpleEditBox::SelectAllToEnd(bool extendSel) {
  if (cursorPos_ >= text_.size()) {
    ClearSelectionIfNotExtending(extendSel);
    return 0;
  }
  while (cursorPos_ < text_.size()) {
    size_t charBytes = Utf8CharLen(text_, cursorPos_);
    if (extendSel) {
      BeginSelectionExtend(cursorPos_);
      ExtendSelectionBy(*this, cursorPos_, static_cast<int>(charBytes));
    } else if (selectionStart_ != selectionEnd_) {
      dirtyFlags_ |= kDirtySelection;
      selectionEnd_ = 0;
      selectionStart_ = 0;
    }
    cursorPos_ += charBytes;
    dirtyFlags_ |= kDirtyCursor;
  }

  ClearSelectionIfNotExtending(extendSel);
  return 0;
}

uint32_t CSimpleEditBox::MeasureDisplayUnitSpan(uint32_t startPos,
                                                int count,
                                                bool atomicHyperlinks) const {
  return openwow::ui::glue::MeasureDisplayUnitSpan(charInfoBuffer_, startPos,
                                                    count, atomicHyperlinks);
}

void CSimpleEditBox::ScrollToEnsurePositionVisible(uint32_t targetPos,
                                                   float leftWidth,
                                                   float rightWidth) {
  if (multiLine_)
    return;

  const char* textBuf = text_.c_str();
  const auto textLen  = static_cast<int>(text_.size());

  int trailingCount = 0;
  if (fontString_ && targetPos > 0) {
    trailingCount = fontString_->CountTrailingCharsWithinWidth(
        textBuf, static_cast<int>(targetPos), leftWidth);
  }

  uint32_t bytesBack = MeasureDisplayUnitSpan(targetPos, -trailingCount);

  int32_t startSigned = static_cast<int32_t>(targetPos) -
                        static_cast<int32_t>(bytesBack);
  visibleStart_ = (startSigned >= 0) ? static_cast<uint32_t>(startSigned) : 0u;

  int leadingCount = 0;
  if (fontString_) {
    const char* startPtr = textBuf + visibleStart_;
    leadingCount = fontString_->CountLeadingCharsWithinWidth(
        startPtr, 0 , rightWidth);
  } else {

    leadingCount = textLen - static_cast<int>(visibleStart_);
    if (leadingCount < 0) leadingCount = 0;
  }

  visibleCharCount_ = MeasureDisplayUnitSpan(visibleStart_, leadingCount);

  lineBreakPositions_.resize(2);
  lineBreakPositions_[0] = visibleStart_;
  lineBreakPositions_[1] = visibleStart_ + visibleCharCount_;
}

void CSimpleEditBox::ProcessDirtyFlags() {
  const uint32_t initial_flags = dirtyFlags_;
  bool needLayout = (initial_flags & kDirtyText) != 0;

  if ((initial_flags & kDirtyCursor) != 0) {
    const auto cp = static_cast<uint32_t>(cursorPos_);
    if (cp < visibleStart_ ||
        cp > visibleStart_ + visibleCharCount_ ||
        (imeComposing_ && imeCompEnd_ > visibleStart_ + visibleCharCount_)) {
      needLayout = true;
    }
  }

  if (needLayout) {
    dirtyFlags_ &= ~kDirtyText;
    if (fontString_ != nullptr) {
      std::string display_text = password_ ? std::string(text_.size(), '*') : text_;
      if (!multiLine_) {
        const float available_width = std::max(0.0F, fontString_->GetWidth());
        ScrollToEnsurePositionVisible(static_cast<uint32_t>(cursorPos_),
                                      available_width, available_width);
        const std::size_t start = std::min<std::size_t>(visibleStart_, display_text.size());
        const std::size_t count = std::min<std::size_t>(
            visibleCharCount_, display_text.size() - start);
        fontString_->SetText(display_text.substr(start, count));
      } else {
        fontString_->SetText(display_text);
      }
      fontString_->RefreshTextLayout();
      lineBreakPositions_.clear();
      if (const auto& layout = fontString_->GetRender().GetTextLayout();
          layout.has_value() && !layout->lines.empty()) {
        const std::uint32_t base = multiLine_ ? 0u : visibleStart_;
        lineBreakPositions_.reserve(layout->lines.size() + 1u);
        for (const auto& line : layout->lines) {
          lineBreakPositions_.push_back(
              base + static_cast<std::uint32_t>(line.begin));
        }
        lineBreakPositions_.push_back(
            base + static_cast<std::uint32_t>(layout->lines.back().end));
      }
      if (lineBreakPositions_.size() < 2u) {
        const std::uint32_t start = multiLine_ ? 0u : visibleStart_;
        lineBreakPositions_ = {
            start,
            start + (multiLine_ ? static_cast<std::uint32_t>(text_.size())
                                : visibleCharCount_)};
      }
    }
  }
  if ((dirtyFlags_ & kDirtySelection) != 0) {
    dirtyFlags_ &= ~kDirtySelection;
    UpdateSelectionHighlights();
  }
  if ((dirtyFlags_ & kDirtyCursor) != 0) {
    dirtyFlags_ &= ~kDirtyCursor;
    if (caretTexture_ != nullptr && fontString_ != nullptr &&
        lineBreakPositions_.size() >= 2u &&
        cursorPos_ >= lineBreakPositions_.front() &&
        cursorPos_ <= lineBreakPositions_.back()) {
      std::size_t line = 0;
      while (line + 1u < lineBreakPositions_.size() - 1u &&
             cursorPos_ >= lineBreakPositions_[line + 1u]) {
        ++line;
      }
      const std::uint32_t line_start = lineBreakPositions_[line];
      const std::uint32_t line_end = lineBreakPositions_[line + 1u];
      const std::string display_text =
          password_ ? std::string(text_.size(), '*') : text_;
      const float prefix_width = fontString_->MeasureSubstringWidth(
          display_text.c_str() + line_start,
          static_cast<int>(cursorPos_ - line_start));
      const float line_width = fontString_->MeasureSubstringWidth(
          display_text.c_str() + line_start,
          static_cast<int>(line_end - line_start));
      const float line_height = fontString_->GetRender().GetFontHeight();
      const float line_advance = line_height + fontString_->GetSpacing();
      RegionAnchor anchor;
      anchor.relativeTo = fontString_;
      anchor.point = multiLine_ ? FramePoint::TopLeft : FramePoint::Left;
      anchor.offsetY = -static_cast<float>(line) * line_advance;
      switch (fontString_->GetJustifyH()) {
        case JustifyH::Left:
          anchor.relativePoint = multiLine_ ? FramePoint::TopLeft : FramePoint::Left;
          anchor.offsetX = prefix_width;
          break;
        case JustifyH::Center:
          anchor.relativePoint = multiLine_ ? FramePoint::Top : FramePoint::Center;
          anchor.offsetX = prefix_width - line_width * 0.5F;
          break;
        case JustifyH::Right:
          anchor.relativePoint = multiLine_ ? FramePoint::TopRight : FramePoint::Right;
          anchor.offsetX = prefix_width - line_width;
          break;
      }
      caretTexture_->ClearAllPoints();
      caretTexture_->SetPoint(anchor);
      caretTexture_->SetHeight(line_height);
      caretTexture_->MarkLayoutDirty();
      FireOnCursorChangedEvent(anchor.offsetX, anchor.offsetY,
                               caretTexture_->GetWidth(), line_height);
    }
  }
  if ((initial_flags & kDirtyText) != 0) {
    FireOnTextChanged();
    dirtyFlags_ &= ~kDirtyUserModified;
  }
}

bool CSimpleEditBox::ScreenPointToCharIndex(float screenX, float screenY,
                                            uint32_t& outCharIndex) const {
  if (!fontString_) return false;

  const auto& rect = fontString_->GetRect();
  if (rect.Width() <= 0.0f && rect.Height() <= 0.0f) return false;

  std::string passwordBuf;
  const char* displayText;
  if (password_) {
    passwordBuf.assign(text_.size(), '*');
    displayText = passwordBuf.c_str();
  } else {
    displayText = text_.c_str();
  }

  const float scaleFactor = GetEffectiveScale();
  const float lineHeight =
      openwow::ui::LegacyPixelSnapUiVerticalCoordinate(
          fontString_->GetRender().GetFontHeight() * scaleFactor);
  const float spacing =
      openwow::ui::LegacyPixelSnapUiVerticalCoordinate(
          fontString_->GetSpacing() * scaleFactor);
  const float totalLineHeight = lineHeight + spacing;

  uint32_t lineIdx = 0;
  if (multiLine_ && totalLineHeight > 0.0f) {
    float distFromTop = screenY - rect.top;
    const uint32_t maxLineIdx =
        lineBreakPositions_.size() >= 2
            ? static_cast<uint32_t>(lineBreakPositions_.size()) - 2
            : 0u;
    while (distFromTop > totalLineHeight && lineIdx < maxLineIdx) {
      distFromTop -= totalLineHeight;
      ++lineIdx;
    }
  }

  const JustifyH justify = fontString_->GetJustifyH();

  float effectiveLeft  = rect.left;
  float effectiveRight = rect.right;
  if (justify == JustifyH::Center &&
      lineIdx + 1 < lineBreakPositions_.size()) {
    const uint32_t lineStart = lineBreakPositions_[lineIdx];
    const uint32_t lineEnd   = lineBreakPositions_[lineIdx + 1];
    const float textWidth = fontString_->MeasureSubstringWidth(
        displayText + lineStart,
        static_cast<int>(lineEnd - lineStart));
    const float rectWidth = rect.right - rect.left;
    const float padding   = (rectWidth - textWidth) * 0.5f;
    effectiveLeft  += padding;
    effectiveRight -= padding;
  }

  float xOffset = screenX - effectiveLeft;
  if (xOffset < 0.0f) {
    outCharIndex = 0;
    return true;
  }
  if (xOffset > effectiveRight) {
    xOffset = effectiveRight;
  }

  if (lineIdx + 1 >= lineBreakPositions_.size()) return true;

  const uint32_t lineStart = lineBreakPositions_[lineIdx];
  const uint32_t lineEnd   = lineBreakPositions_[lineIdx + 1];
  const int lineLen = static_cast<int>(lineEnd - lineStart);
  const float renderScale = fontString_->GetRender().GetScaleFactor();
  const float divisor = (renderScale > 0.0f) ? renderScale : 1.0f;

  if (justify == JustifyH::Left || justify == JustifyH::Center) {
    const float preScaleWidth = xOffset / divisor;
    int charCount = fontString_->CountLeadingCharsWithinWidth(
        displayText + lineStart, lineLen, preScaleWidth);
    if (!password_) {
      charCount = static_cast<int>(
          MeasureDisplayUnitSpan(lineStart, charCount));
    }
    outCharIndex = lineStart + static_cast<uint32_t>(charCount);
    return true;
  }

  if (justify == JustifyH::Right) {
    const float distToRight = effectiveRight - effectiveLeft - xOffset;
    const float preScaleWidth = distToRight / divisor;
    int charCount = fontString_->CountTrailingCharsWithinWidth(
        displayText + lineStart, lineLen, preScaleWidth);
    if (!password_) {
      charCount = static_cast<int>(
          MeasureDisplayUnitSpan(lineStart, charCount));
    }
    outCharIndex = lineEnd - static_cast<uint32_t>(charCount);
    return true;
  }

  return true;
}

void CSimpleEditBox::FireOnUpdate(float elapsed) {

  CSimpleFrame::FireOnUpdate(elapsed);

  ProcessDirtyFlags();

  if (blinkSpeed_ == 0.0f) return;

  const auto cp = static_cast<uint32_t>(cursorPos_);
  if (cp < visibleStart_ || cp > visibleStart_ + visibleCharCount_)
    return;

  blinkTimer_ += elapsed;

  if (this != s_focusedEditBox_) return;

  if (blinkTimer_ <= blinkSpeed_) return;

  if (caretTexture_) {
    if (caretTexture_->IsShown()) {
      caretTexture_->SetShown(false);
      caretTexture_->HideVisible();
    } else {
      caretTexture_->SetShown(true);
      caretTexture_->ShowVisible();
    }
  }
  blinkTimer_ = 0.0f;
}

void CSimpleEditBox::ToggleInputLanguage() noexcept {

  const bool enableNative =
      (inputLanguage_ == EditBoxInputLanguage::Roman);
  openwow::platform::IME_ToggleNativeMode(enableNative);
}

}

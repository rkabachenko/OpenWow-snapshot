
#include "openwow/ui/widgets/editbox_impl.h"

#include <algorithm>
#include <cctype>

#include "openwow/platform/adapters/clipboard/os_clipboard.h"
#include "openwow/ui/glue/editbox_text_layout.h"

namespace openwow::ui {

void EditBoxImpl::SetText(const std::string& text) {
    text_ = text;
    ApplyMaxLetters();
    cursor_pos_ = text_.size();
    ClearSelection();
    ResetBlink();
}

const std::string& EditBoxImpl::GetText() const { return text_; }

void EditBoxImpl::InsertText(const std::string& text) {
    if (text.empty()) return;

    if (has_selection_) {
        DeleteSelection();
    }

    std::string filtered;
    filtered.reserve(text.size());
    for (char c : text) {
        if (IsValidChar(c)) filtered += c;
    }
    if (filtered.empty()) return;

    text_.insert(cursor_pos_, filtered);
    cursor_pos_ += filtered.size();
    ApplyMaxLetters();
    ResetBlink();
}

void EditBoxImpl::DeleteSelection() {
    if (!has_selection_) return;
    size_t lo = std::min(sel_start_, sel_end_);
    size_t hi = std::max(sel_start_, sel_end_);
    DeleteRange(lo, hi);
    cursor_pos_ = lo;
    ClearSelection();
    ResetBlink();
}

void EditBoxImpl::ClearText() {
    text_.clear();
    cursor_pos_ = 0;
    ClearSelection();
    ResetBlink();
}

void EditBoxImpl::SetCursorPosition(size_t pos) {
    cursor_pos_ = std::min(pos, text_.size());
    ResetBlink();
}

size_t EditBoxImpl::GetCursorPosition() const { return cursor_pos_; }

void EditBoxImpl::MoveCursorLeft(bool word) {
    if (cursor_pos_ == 0) return;
    if (word) {
        cursor_pos_ = FindWordBoundaryLeft(cursor_pos_);
    } else {
        --cursor_pos_;
    }
    ResetBlink();
}

void EditBoxImpl::MoveCursorRight(bool word) {
    if (cursor_pos_ >= text_.size()) return;
    if (word) {
        cursor_pos_ = FindWordBoundaryRight(cursor_pos_);
    } else {
        ++cursor_pos_;
    }
    ResetBlink();
}

void EditBoxImpl::MoveCursorHome() {
    cursor_pos_ = 0;
    ResetBlink();
}

void EditBoxImpl::MoveCursorEnd() {
    cursor_pos_ = text_.size();
    ResetBlink();
}

void EditBoxImpl::SetSelection(size_t start, size_t end) {
    sel_start_     = std::min(start, text_.size());
    sel_end_       = std::min(end, text_.size());
    has_selection_ = (sel_start_ != sel_end_);
}

void EditBoxImpl::SelectAll() {
    if (text_.empty()) {
        ClearSelection();
        return;
    }
    sel_start_     = 0;
    sel_end_       = text_.size();
    has_selection_ = true;
    cursor_pos_    = text_.size();
}

void EditBoxImpl::ClearSelection() {
    sel_start_ = sel_end_ = cursor_pos_;
    has_selection_ = false;
}

bool EditBoxImpl::HasSelection() const { return has_selection_; }

std::string EditBoxImpl::GetSelectedText() const {
    if (!has_selection_) return {};
    size_t lo = std::min(sel_start_, sel_end_);
    size_t hi = std::max(sel_start_, sel_end_);
    return text_.substr(lo, hi - lo);
}

size_t EditBoxImpl::GetSelectionStart() const { return sel_start_; }
size_t EditBoxImpl::GetSelectionEnd()   const { return sel_end_;   }

void EditBoxImpl::Cut() {
    if (!has_selection_) return;
    clipboard_ = glue::StripWowTextEscapes(GetSelectedText(),
                                           glue::kClipboardStripFlags);
    (void)openwow::platform::SetSystemClipboardText(clipboard_);
    DeleteSelection();
}

void EditBoxImpl::Copy() {
    if (!has_selection_) return;
    clipboard_ = glue::StripWowTextEscapes(GetSelectedText(),
                                           glue::kClipboardStripFlags);
    (void)openwow::platform::SetSystemClipboardText(clipboard_);
}

void EditBoxImpl::Paste(const std::string& text) {
    InsertText(text);
}

void EditBoxImpl::PushHistory(const std::string& text) {
    if (text.empty()) return;

    if (!history_.empty() && history_.back() == text) return;
    history_.push_back(text);
    if (history_.size() > kMaxHistory) {
        history_.erase(history_.begin());
    }
    history_index_ = -1;
}

std::string EditBoxImpl::GetHistory(int32_t offset) const {
    if (history_.empty()) return {};

    int32_t idx = static_cast<int32_t>(history_.size()) - 1 - offset;
    if (idx < 0) idx = 0;
    if (idx >= static_cast<int32_t>(history_.size()))
        idx = static_cast<int32_t>(history_.size()) - 1;
    return history_[static_cast<size_t>(idx)];
}

void EditBoxImpl::SetHistoryEnabled(bool enabled) { history_enabled_ = enabled; }

size_t EditBoxImpl::GetHistorySize() const { return history_.size(); }

void     EditBoxImpl::SetMaxLetters(uint32_t max) { max_letters_ = max; ApplyMaxLetters(); }
uint32_t EditBoxImpl::GetMaxLetters() const       { return max_letters_; }
void     EditBoxImpl::SetNumeric(bool n)           { numeric_ = n; }
bool     EditBoxImpl::IsNumeric() const            { return numeric_; }
void     EditBoxImpl::SetPassword(bool p)          { password_ = p; }
bool     EditBoxImpl::IsPassword() const           { return password_; }
void     EditBoxImpl::SetMultiLine(bool m)         { multi_line_ = m; }
bool     EditBoxImpl::IsMultiLine() const          { return multi_line_; }
void     EditBoxImpl::SetAutoFocus(bool a)         { auto_focus_ = a; }
bool     EditBoxImpl::IsAutoFocus() const          { return auto_focus_; }

bool EditBoxImpl::HandleKeyDown(uint32_t keyCode, bool shift, bool ctrl) {

    constexpr uint32_t kBackspace = 42;
    constexpr uint32_t kDelete    = 76;
    constexpr uint32_t kLeft      = 80;
    constexpr uint32_t kRight     = 79;
    constexpr uint32_t kHome      = 74;
    constexpr uint32_t kEnd       = 77;
    constexpr uint32_t kEnter     = 40;
    constexpr uint32_t kUp        = 82;
    constexpr uint32_t kDown      = 81;
    constexpr uint32_t kA         = 4;
    constexpr uint32_t kC         = 6;
    constexpr uint32_t kV         = 25;
    constexpr uint32_t kX         = 27;

    switch (keyCode) {
        case kBackspace:
            if (has_selection_) {
                DeleteSelection();
            } else if (cursor_pos_ > 0) {
                if (ctrl) {
                    size_t target = FindWordBoundaryLeft(cursor_pos_);
                    DeleteRange(target, cursor_pos_);
                    cursor_pos_ = target;
                } else {
                    --cursor_pos_;
                    text_.erase(cursor_pos_, 1);
                }
            }
            ResetBlink();
            return true;

        case kDelete:
            if (has_selection_) {
                DeleteSelection();
            } else if (cursor_pos_ < text_.size()) {
                if (ctrl) {
                    size_t target = FindWordBoundaryRight(cursor_pos_);
                    DeleteRange(cursor_pos_, target);
                } else {
                    text_.erase(cursor_pos_, 1);
                }
            }
            ResetBlink();
            return true;

        case kLeft:
            if (shift) {
                if (!has_selection_) {
                    sel_start_ = cursor_pos_;
                    has_selection_ = true;
                }
                MoveCursorLeft(ctrl);
                sel_end_ = cursor_pos_;
                has_selection_ = (sel_start_ != sel_end_);
            } else {
                if (has_selection_) {
                    cursor_pos_ = std::min(sel_start_, sel_end_);
                    ClearSelection();
                } else {
                    MoveCursorLeft(ctrl);
                }
            }
            return true;

        case kRight:
            if (shift) {
                if (!has_selection_) {
                    sel_start_ = cursor_pos_;
                    has_selection_ = true;
                }
                MoveCursorRight(ctrl);
                sel_end_ = cursor_pos_;
                has_selection_ = (sel_start_ != sel_end_);
            } else {
                if (has_selection_) {
                    cursor_pos_ = std::max(sel_start_, sel_end_);
                    ClearSelection();
                } else {
                    MoveCursorRight(ctrl);
                }
            }
            return true;

        case kHome:
            if (shift) {
                if (!has_selection_) {
                    sel_start_ = cursor_pos_;
                    has_selection_ = true;
                }
                MoveCursorHome();
                sel_end_ = cursor_pos_;
                has_selection_ = (sel_start_ != sel_end_);
            } else {
                ClearSelection();
                MoveCursorHome();
            }
            return true;

        case kEnd:
            if (shift) {
                if (!has_selection_) {
                    sel_start_ = cursor_pos_;
                    has_selection_ = true;
                }
                MoveCursorEnd();
                sel_end_ = cursor_pos_;
                has_selection_ = (sel_start_ != sel_end_);
            } else {
                ClearSelection();
                MoveCursorEnd();
            }
            return true;

        case kEnter:

            if (multi_line_) {
                InsertText("\n");
                return true;
            }
            return false;

        case kUp:
            if (history_enabled_ && !history_.empty()) {
                if (history_index_ < 0)
                    history_index_ = 0;
                else if (history_index_ < static_cast<int32_t>(history_.size()) - 1)
                    ++history_index_;
                SetText(GetHistory(history_index_));
                return true;
            }
            return false;

        case kDown:
            if (history_enabled_ && history_index_ >= 0) {
                --history_index_;
                if (history_index_ < 0) {
                    ClearText();
                } else {
                    SetText(GetHistory(history_index_));
                }
                return true;
            }
            return false;

        default:
            break;
    }

    if (ctrl) {
        switch (keyCode) {
            case kA: SelectAll(); return true;
            case kC: Copy();     return true;
            case kX: Cut();      return true;
            case kV:
                if (auto clipboard_text = openwow::platform::TryGetSystemClipboardText();
                    clipboard_text.has_value()) {
                    clipboard_ = *clipboard_text;
                    Paste(*clipboard_text);
                }
                return true;
            default: break;
        }
    }

    return false;
}

bool EditBoxImpl::HandleTextInput(const std::string& text) {
    if (text.empty()) return false;
    InsertText(text);
    return true;
}

void EditBoxImpl::UpdateBlink(float deltaTime) {
    blink_timer_ += deltaTime;
    if (blink_timer_ >= kBlinkRate) {
        blink_timer_ -= kBlinkRate;
        cursor_visible_ = !cursor_visible_;
    }
}

bool EditBoxImpl::IsCursorVisible() const { return cursor_visible_; }

void EditBoxImpl::ResetBlink() {
    blink_timer_    = 0.0f;
    cursor_visible_ = true;
}

size_t EditBoxImpl::FindWordBoundaryLeft(size_t pos) const {
    if (pos == 0) return 0;
    --pos;

    while (pos > 0 && std::isspace(static_cast<unsigned char>(text_[pos])))
        --pos;

    while (pos > 0 && !std::isspace(static_cast<unsigned char>(text_[pos - 1])))
        --pos;
    return pos;
}

size_t EditBoxImpl::FindWordBoundaryRight(size_t pos) const {
    const size_t len = text_.size();

    while (pos < len && !std::isspace(static_cast<unsigned char>(text_[pos])))
        ++pos;

    while (pos < len && std::isspace(static_cast<unsigned char>(text_[pos])))
        ++pos;
    return pos;
}

void EditBoxImpl::DeleteRange(size_t from, size_t to) {
    if (from >= to || from >= text_.size()) return;
    to = std::min(to, text_.size());
    text_.erase(from, to - from);
}

void EditBoxImpl::ApplyMaxLetters() {
    if (max_letters_ > 0 && text_.size() > max_letters_) {
        text_.resize(max_letters_);
        if (cursor_pos_ > text_.size()) cursor_pos_ = text_.size();
    }
}

bool EditBoxImpl::IsValidChar(char c) const {
    if (numeric_) {
        return std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '.';
    }

    if (c == '\n') return multi_line_;
    return (static_cast<unsigned char>(c) >= 32);
}

void EditBoxImpl::ExpandSelectionTo(size_t pos) {
    if (!has_selection_) {
        sel_start_     = cursor_pos_;
        has_selection_ = true;
    }
    sel_end_ = pos;
    if (sel_start_ == sel_end_) has_selection_ = false;
}

}


#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openwow::ui {

class EditBoxImpl {
public:
    void SetText(const std::string& text);
    [[nodiscard]] const std::string& GetText() const;
    void InsertText(const std::string& text);
    void DeleteSelection();
    void ClearText();

    void SetCursorPosition(size_t pos);
    [[nodiscard]] size_t GetCursorPosition() const;
    void MoveCursorLeft(bool word = false);
    void MoveCursorRight(bool word = false);
    void MoveCursorHome();
    void MoveCursorEnd();

    void SetSelection(size_t start, size_t end);
    void SelectAll();
    void ClearSelection();
    [[nodiscard]] bool HasSelection() const;
    [[nodiscard]] std::string GetSelectedText() const;
    [[nodiscard]] size_t GetSelectionStart() const;
    [[nodiscard]] size_t GetSelectionEnd() const;

    void Cut();
    void Copy();
    void Paste(const std::string& text);

    void PushHistory(const std::string& text);
    [[nodiscard]] std::string GetHistory(int32_t offset) const;
    void SetHistoryEnabled(bool enabled);
    [[nodiscard]] bool IsHistoryEnabled() const { return history_enabled_; }
    [[nodiscard]] size_t GetHistorySize() const;

    void     SetMaxLetters(uint32_t max);
    [[nodiscard]] uint32_t GetMaxLetters() const;
    void     SetNumeric(bool numeric);
    [[nodiscard]] bool IsNumeric() const;
    void     SetPassword(bool password);
    [[nodiscard]] bool IsPassword() const;
    void     SetMultiLine(bool multi);
    [[nodiscard]] bool IsMultiLine() const;
    void     SetAutoFocus(bool autoFocus);
    [[nodiscard]] bool IsAutoFocus() const;

    bool HandleKeyDown(uint32_t keyCode, bool shift, bool ctrl);

    bool HandleTextInput(const std::string& text);

    void UpdateBlink(float deltaTime);
    [[nodiscard]] bool IsCursorVisible() const;
    void ResetBlink();

    [[nodiscard]] const std::string& GetClipboard() const { return clipboard_; }

private:
    size_t FindWordBoundaryLeft(size_t pos) const;
    size_t FindWordBoundaryRight(size_t pos) const;
    void   DeleteRange(size_t from, size_t to);
    void   ApplyMaxLetters();
    bool   IsValidChar(char c) const;
    void   ExpandSelectionTo(size_t pos);

    std::string              text_;
    size_t                   cursor_pos_      = 0;
    size_t                   sel_start_       = 0;
    size_t                   sel_end_         = 0;
    bool                     has_selection_   = false;
    std::vector<std::string> history_;
    int32_t                  history_index_   = -1;
    bool                     history_enabled_ = false;
    uint32_t                 max_letters_     = 0;
    bool                     numeric_         = false;
    bool                     password_        = false;
    bool                     multi_line_      = false;
    bool                     auto_focus_      = false;
    float                    blink_timer_     = 0.0f;
    bool                     cursor_visible_  = true;
    std::string              clipboard_;

    static constexpr float  kBlinkRate  = 0.53f;
    static constexpr size_t kMaxHistory = 32;
};

}

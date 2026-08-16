#include "openwow/ui/game/runtime/edit_box_input.h"

#include "openwow/foundation/text/utf8.h"
#include "openwow/ui/glue/editbox_text_layout.h"

#include <algorithm>

namespace openwow::ui::game::runtime {
namespace {

void CollapseSelection(LuaEditBoxInputState &state) {
  state.selection_start = state.cursor;
  state.selection_end = state.cursor;
}

bool DeleteSelection(LuaEditBoxInputState &state) {
  if (state.selection_start == state.selection_end) {
    return false;
  }
  state.text.erase(static_cast<std::size_t>(state.selection_start),
                   static_cast<std::size_t>(state.selection_end - state.selection_start));
  state.cursor = state.selection_start;
  CollapseSelection(state);
  return true;
}

void ApplyLimits(LuaEditBoxInputState &state) {
  if (state.max_bytes > 0 && static_cast<int>(state.text.size()) > state.max_bytes) {
    std::size_t end = static_cast<std::size_t>(state.max_bytes);
    while (end > 0 && (static_cast<unsigned char>(state.text[end]) & 0xc0u) == 0x80u) {
      --end;
    }
    state.text.resize(end);
  }

  if (state.max_letters > 0) {
    const int current_count =
        state.count_invisible ? openwow::text::Utf8CodepointCount(state.text)
                              : openwow::ui::glue::CountEditBoxVisibleLetters(state.text);
    if (current_count > state.max_letters) {
      if (state.count_invisible) {
        state.text = openwow::text::Utf8TakeCodepoints(state.text, state.max_letters);
      } else {
        const std::string_view text = state.text;
        std::size_t offset = 0;
        int visible_count = 0;
        while (offset < text.size() && visible_count < state.max_letters) {
          bool visible = false;
          offset = openwow::ui::glue::AdvanceWowTextElement(text, offset, &visible);
          visible_count += visible ? 1 : 0;
        }
        while (offset < text.size()) {
          bool visible = false;
          const std::size_t next =
              openwow::ui::glue::AdvanceWowTextElement(text, offset, &visible);
          if (visible) {
            break;
          }
          offset = next;
        }
        state.text.resize(offset);
      }
    }
  }

  state.cursor = openwow::text::ClampUtf8ByteIndex(
      state.text, std::min(state.cursor, static_cast<int>(state.text.size())));
  state.selection_start = openwow::text::ClampUtf8ByteIndex(
      state.text, std::min(state.selection_start, static_cast<int>(state.text.size())));
  state.selection_end = openwow::text::ClampUtf8ByteIndex(
      state.text, std::min(state.selection_end, static_cast<int>(state.text.size())));
}

bool IsAsciiDigitText(const std::string_view text) {
  return !text.empty() &&
         std::all_of(text.begin(), text.end(), [](const unsigned char value) {
           return value >= static_cast<unsigned char>('0') &&
                  value <= static_cast<unsigned char>('9');
         });
}

}

EditBoxTextInputResult ApplyEditBoxTextInput(LuaEditBoxInputState &state,
                                             const std::string_view codepoint) {
  EditBoxTextInputResult result;
  result.state_changed = DeleteSelection(state);

  std::string insertion(codepoint);
  if (codepoint == "\t") {
    insertion = "    ";
  } else if (codepoint == "\r" || codepoint == "\n") {
    insertion = state.multiline ? "\n" : "";
  } else if (!codepoint.empty() &&
             (static_cast<unsigned char>(codepoint.front()) < 0x20u ||
              static_cast<unsigned char>(codepoint.front()) == 0x7fu)) {
    insertion.clear();
  } else if (codepoint == "|") {

    insertion = "||";
  }

  result.accepted = !insertion.empty() && (!state.numeric || IsAsciiDigitText(insertion));
  if (result.accepted) {
    state.text.insert(static_cast<std::size_t>(state.cursor), insertion);
    state.cursor += static_cast<int>(insertion.size());
    CollapseSelection(state);
    ApplyLimits(state);
    result.state_changed = true;
  }
  return result;
}

bool IsEditBoxEditingKey(const std::string_view key_name, const bool ctrl_down) {
  return key_name == "BACKSPACE" || key_name == "DELETE" || key_name == "LEFT" ||
         key_name == "RIGHT" || key_name == "HOME" || key_name == "END" ||
         (ctrl_down && key_name == "A");
}

bool ApplyEditBoxEditingKey(LuaEditBoxInputState &state, const std::string_view key_name,
                            const bool shift_down, const bool ctrl_down) {
  bool text_changed = false;

  if (ctrl_down && key_name == "A") {
    state.selection_start = 0;
    state.selection_end = static_cast<int>(state.text.size());
    state.cursor = state.selection_end;
  } else if (key_name == "BACKSPACE" || key_name == "DELETE") {
    text_changed = DeleteSelection(state);
    if (!text_changed && key_name == "BACKSPACE" && state.cursor > 0) {
      const int previous = openwow::text::Utf8PrevByteIndex(state.text, state.cursor);
      state.text.erase(static_cast<std::size_t>(previous),
                       static_cast<std::size_t>(state.cursor - previous));
      state.cursor = previous;
      CollapseSelection(state);
      text_changed = true;
    } else if (!text_changed && key_name == "DELETE" &&
               state.cursor < static_cast<int>(state.text.size())) {
      const int next = openwow::text::Utf8NextByteIndex(state.text, state.cursor);
      state.text.erase(static_cast<std::size_t>(state.cursor),
                       static_cast<std::size_t>(next - state.cursor));
      CollapseSelection(state);
      text_changed = true;
    }
  } else {
    const bool has_selection = state.selection_start != state.selection_end;
    const int old_cursor = state.cursor;
    int next_cursor = old_cursor;
    if (!shift_down && has_selection && key_name == "LEFT") {
      next_cursor = state.selection_start;
    } else if (!shift_down && has_selection && key_name == "RIGHT") {
      next_cursor = state.selection_end;
    } else if (key_name == "LEFT") {
      next_cursor = openwow::text::Utf8PrevByteIndex(state.text, old_cursor);
    } else if (key_name == "RIGHT") {
      next_cursor = openwow::text::Utf8NextByteIndex(state.text, old_cursor);
    } else if (key_name == "HOME") {
      next_cursor = 0;
    } else if (key_name == "END") {
      next_cursor = static_cast<int>(state.text.size());
    }

    if (shift_down) {
      int anchor = old_cursor;
      if (has_selection) {
        anchor = old_cursor == state.selection_start ? state.selection_end
                                                     : state.selection_start;
      }
      state.selection_start = std::min(anchor, next_cursor);
      state.selection_end = std::max(anchor, next_cursor);
      state.cursor = next_cursor;
    } else {
      state.cursor = next_cursor;
      CollapseSelection(state);
    }
  }
  return text_changed;
}

}

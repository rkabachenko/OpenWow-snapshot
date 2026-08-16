#pragma once

#include <string>
#include <string_view>

namespace openwow::ui::game::runtime {

struct LuaEditBoxInputState {
  std::string text;
  int cursor = 0;
  int selection_start = 0;
  int selection_end = 0;
  int max_letters = 0;
  int max_bytes = 0;
  bool count_invisible = false;
  bool numeric = false;
  bool multiline = false;
};

struct EditBoxTextInputResult {
  bool state_changed{false};
  bool accepted{false};
};

EditBoxTextInputResult ApplyEditBoxTextInput(LuaEditBoxInputState &state,
                                             std::string_view codepoint);
bool IsEditBoxEditingKey(std::string_view key_name, bool ctrl_down);
bool ApplyEditBoxEditingKey(LuaEditBoxInputState &state, std::string_view key_name,
                            bool shift_down, bool ctrl_down);

}

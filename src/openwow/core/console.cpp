
#include "openwow/core/console.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "openwow/core/cvar.h"
#include "openwow/core/decimal_parse.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/runtime/scheduling/evt_sched.h"
#include "openwow/core/gxcvar.h"
#include "openwow/core/storm_string.h"
#include "openwow/debug/diagnostics/debug_console.h"
#include "openwow/game/simple_script.h"
#include "openwow/platform/adapters/clipboard/os_clipboard.h"

namespace openwow::core::ida {

namespace {

constexpr float kDefaultConsoleMinimumFontHeight = 0.02f;
constexpr float kDefaultConsoleVisibleLineCount = 10.0f;
constexpr char kDefaultConsoleFontPath[] = "Fonts\\ARIALN.ttf";
constexpr std::uint32_t kDefaultConsoleSelectionHighlightColor = 0x80FFFFFFu;
constexpr std::uint32_t kDefaultConsoleBackgroundColor = 0xC0000000u;

ConsoleVisualStateSnapshot MakeDefaultConsoleVisualState() {
  return {
      .minimum_font_height = kDefaultConsoleMinimumFontHeight,
      .visible_line_count = kDefaultConsoleVisibleLineCount,
      .console_height =
          kDefaultConsoleMinimumFontHeight * kDefaultConsoleVisibleLineCount,
      .font_path = kDefaultConsoleFontPath,
      .text_colors = {
          0xFFFFFFFFu,
          0xFFFFFFFFu,
          0xFF808080u,
          0xFFFF0000u,
          0xFFFFFF00u,
          0xFFFFFFFFu,
          0xFFFFFFFFu,
      },
      .selection_highlight_color = kDefaultConsoleSelectionHighlightColor,
      .background_color = kDefaultConsoleBackgroundColor,
  };
}

ConsoleVisualStateSnapshot &GetConsoleVisualStateStorage() {
  static ConsoleVisualStateSnapshot state = MakeDefaultConsoleVisualState();
  return state;
}

void RestoreDefaultConsoleVisualState() {
  GetConsoleVisualStateStorage() = MakeDefaultConsoleVisualState();
}

}

static int s_line_count = 0;
static int s_console_toggle_key_code = kDefaultConsoleToggleKeyCode;
static bool s_console_input_routing_initialized = false;
static float s_console_character_spacing_pixels = 0.0f;
static std::uint32_t s_console_repeat_remaining_count = 0;
static std::array<char, 64> s_console_repeat_command_text{};

static float s_console_background_top = 1.0f;

static bool s_pointer_interaction_active = false;

struct ConsoleInputLineLayout {
  void *previous = nullptr;
  void *next = nullptr;
  char *text_buffer = nullptr;
  std::uint32_t text_length = 0;
  std::uint32_t buffer_capacity = 0;
  std::uint32_t cursor_offset = 0;
  std::uint32_t prompt_offset = 0;
  std::uint32_t line_flags = 0;
  void *rendered_string = nullptr;
};

#if INTPTR_MAX == INT32_MAX
static_assert(sizeof(ConsoleInputLineLayout) == 36);
#endif

static ConsoleInputLineLayout s_console_input_line{};
static bool s_console_input_line_created = false;

static bool s_tab_complete_active = false;
static std::string s_tab_complete_prefix;
static std::string s_tab_complete_cursor;

static int s_last_history_index = -1;

static int s_copy_selection_mode = 0;
static ConsoleSelectionRect s_selection_rect{};

static std::array<char, 128> s_copy_buffer{};
static std::uint32_t s_copy_selection_start = 0;
static std::uint32_t s_copy_selection_end = 0;
static float s_copy_selection_anchor_x = 0.0f;
static float s_copy_selection_current_x = 0.0f;
static float s_copy_selection_line_top = 0.0f;
static float s_copy_selection_line_bottom = 0.0f;

static int s_scroll_anchor_index = -1;

static void *GetOrCreateConsoleInputLine() {
  if (s_console_input_line_created && s_console_input_line.line_flags != 0) {
    return &s_console_input_line;
  }

  constexpr std::uint32_t kInitialCapacity = 16;
  if (s_console_input_line.text_buffer != nullptr) {
    delete[] s_console_input_line.text_buffer;
  }
  s_console_input_line.text_buffer = new char[kInitialCapacity];
  s_console_input_line.buffer_capacity = kInitialCapacity;

  std::memcpy(s_console_input_line.text_buffer, "> ", 3);

  const auto prompt_len = static_cast<std::uint32_t>(std::strlen("> "));
  s_console_input_line.prompt_offset = prompt_len;
  s_console_input_line.cursor_offset = prompt_len;
  s_console_input_line.text_length = prompt_len;
  s_console_input_line.line_flags = 1;
  s_console_input_line.rendered_string = nullptr;
  s_console_input_line.previous = nullptr;
  s_console_input_line.next = nullptr;

  s_console_input_line_created = true;
  return &s_console_input_line;
}

static void ResetConsoleInputLineToPrompt(void *console_line) {
  auto *line = static_cast<ConsoleInputLineLayout *>(console_line);
  line->cursor_offset = line->prompt_offset;
  line->text_length = line->prompt_offset;
  if (line->text_buffer != nullptr) {
    line->text_buffer[line->prompt_offset] = '\0';
  }
}

static const char *GetEditableText(const void *console_line) {
  const auto *line = static_cast<const ConsoleInputLineLayout *>(console_line);
  if (line->text_buffer == nullptr) {
    return "";
  }
  return line->text_buffer + line->prompt_offset;
}

static void ClearConsoleSelectionState() {
  s_copy_selection_mode = static_cast<int>(ConsoleCopySelectionMode::Inactive);
  s_copy_buffer.fill('\0');
  s_copy_selection_start = 0;
  s_copy_selection_end = 0;
  s_copy_selection_anchor_x = 0.0f;
  s_copy_selection_current_x = 0.0f;
  s_copy_selection_line_top = 0.0f;
  s_copy_selection_line_bottom = 0.0f;
  s_selection_rect = {};
}

static std::string EncodeUtf8Scalar(std::uint32_t codepoint) {
  if (codepoint > 0x10FFFFu ||
      (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) {
    codepoint = 0xFFFDu;
  }

  std::string encoded;
  if (codepoint <= 0x7Fu) {
    encoded.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FFu) {
    encoded.push_back(static_cast<char>(0xC0u | (codepoint >> 6u)));
    encoded.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
  } else if (codepoint <= 0xFFFFu) {
    encoded.push_back(static_cast<char>(0xE0u | (codepoint >> 12u)));
    encoded.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
    encoded.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
  } else {
    encoded.push_back(static_cast<char>(0xF0u | (codepoint >> 18u)));
    encoded.push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3Fu)));
    encoded.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
    encoded.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
  }
  return encoded;
}

constexpr float kConsoleFadeSpeed = 5.0f;

constexpr std::size_t kConsoleFileCreationCommandBufferSize = 0x2000;
constexpr std::size_t kConsoleFileCreationLineBufferSize = 0x104;
constexpr int kConsoleFileCreationAwaitOverwriteConfirmation = 0;
constexpr int kConsoleFileCreationCaptureCommands = 1;
constexpr int kConsoleFileCreationCaptureCommandsNoStateFlipOnEnd = 2;
constexpr int kConsoleFileCreationDispatchBufferedFlow = 3;
constexpr int kConsoleFileCreationInactive = 4;

struct ConsoleFileCreationCaptureState {
  int phase = kConsoleFileCreationInactive;
  std::array<char, kConsoleFileCreationCommandBufferSize> command_buffer{};
};

static constexpr int kMaxConsoleLines = 256;
static constexpr float kConsoleSelectionLeftSnapThreshold = 0.015f;

ConsoleFileCreationCaptureState &GetConsoleFileCreationCaptureState() {
  static ConsoleFileCreationCaptureState state;
  return state;
}

ConsoleSetMapCommandHandler &GetConsoleSetMapCommandHandler() {
  static ConsoleSetMapCommandHandler handler;
  return handler;
}

void ConsoleClient_RequestShutdown() {
  EvtContext_RequestShutdown(EvtContextTls_GetCurrentHandle());
}

static int ConsoleWindowTerminationCallback(int ) {
  EvtContext_RequestShutdown(0);
  return 0;
}

std::uint32_t ParseUnsignedDecimalWrapping(std::string_view digits) {
  if (digits.empty() || !std::isdigit(static_cast<unsigned char>(digits.front()))) {
    return 0;
  }

  std::uint32_t value = static_cast<std::uint32_t>(digits.front() - '0');
  for (std::size_t index = 1; index < digits.size(); ++index) {
    const unsigned char ch = static_cast<unsigned char>(digits[index]);
    if (!std::isdigit(ch)) {
      break;
    }
    value = value * 10u + static_cast<std::uint32_t>(ch - '0');
  }
  return value;
}

void ClearConsoleRepeatCommandState() {
  s_console_repeat_remaining_count = 0;
  s_console_repeat_command_text.fill('\0');
}

int Console_PeriodicUpdateEvent(void *event_data, int ) {
  return Console_PeriodicUpdate(reinterpret_cast<const float *>(event_data));
}

int LegacyConsolePeriodicUpdateCallback() {
  static const int callback = EvtSched_RegisterLegacyCallback(
      reinterpret_cast<std::intptr_t>(&Console_PeriodicUpdateEvent));
  return callback;
}

void RegisterConsolePeriodicUpdateHandler(const float priority) {
  const int callback = LegacyConsolePeriodicUpdateCallback();
  (void)EvtContext_UnregisterCurrentHandler(6u, callback);
  EvtContext_RegisterCurrentHandler(6u, callback, 0, priority);
}

static int ConsoleKeyEventCallback(void *event_data, int ) {
  if (event_data == nullptr) {
    return 1;
  }
  return ConsoleClient_HandleKeyEvent(
      reinterpret_cast<const unsigned int *>(event_data));
}

static int LegacyConsoleKeyEventCallback() {
  static const int callback = EvtSched_RegisterLegacyCallback(
      reinterpret_cast<std::intptr_t>(&ConsoleKeyEventCallback));
  return callback;
}

struct ConsoleImmediateCharPayload {
  std::uint32_t character = 0;
  std::uint32_t button_state_bits = 0;
  std::uint32_t event_word_2 = 0;
};

struct ConsoleImmediatePointerPayload {
  std::uint32_t input_mode = 0;
  std::uint32_t button_mask = 0;
  std::uint32_t held_button_mask = 0;
  std::uint32_t button_state_bits = 0;
  std::uint32_t dispatch_flags = 0;
  float normalized_x = 0.0f;
  float normalized_y = 0.0f;
  float axis_value = 0.0f;
  float timestamp = 0.0f;
};

static_assert(sizeof(ConsoleImmediateCharPayload) == 12u);
static_assert(sizeof(ConsoleImmediatePointerPayload) == 36u);

static ConsolePointerPayload ToConsolePointerPayload(
    const ConsoleImmediatePointerPayload &payload) {
  return {
      .normalized_x = payload.normalized_x,
      .normalized_y = payload.normalized_y,
  };
}

static int ConsoleCharacterEventCallback(void *event_data, int ) {
  if (event_data == nullptr) {
    return 1;
  }
  return Console_HandleCharacterInput(
      static_cast<const ConsoleImmediateCharPayload *>(event_data)->character);
}

static int ConsoleKeyReleaseEventCallback(void *event_data, int ) {
  if (event_data == nullptr) {
    return 1;
  }
  return Console_ShouldPassKeyRelease(
      static_cast<const int *>(event_data)[0]);
}

static int ConsolePointerBeginEventCallback(void *event_data, int ) {
  if (event_data == nullptr) {
    return 1;
  }
  return Console_BeginPointerInteraction(ToConsolePointerPayload(
      *static_cast<const ConsoleImmediatePointerPayload *>(event_data)));
}

static int ConsolePointerMoveEventCallback(void *event_data, int ) {
  if (event_data == nullptr) {
    return 1;
  }
  return Console_UpdatePointerInteraction(ToConsolePointerPayload(
      *static_cast<const ConsoleImmediatePointerPayload *>(event_data)));
}

static int ConsolePointerEndEventCallback(void * , int ) {
  return Console_EndPointerInteraction();
}

static int LegacyConsoleCharacterEventCallback() {
  static const int callback = EvtSched_RegisterLegacyCallback(
      reinterpret_cast<std::intptr_t>(&ConsoleCharacterEventCallback));
  return callback;
}

static int LegacyConsoleKeyReleaseEventCallback() {
  static const int callback = EvtSched_RegisterLegacyCallback(
      reinterpret_cast<std::intptr_t>(&ConsoleKeyReleaseEventCallback));
  return callback;
}

static int LegacyConsolePointerBeginEventCallback() {
  static const int callback = EvtSched_RegisterLegacyCallback(
      reinterpret_cast<std::intptr_t>(&ConsolePointerBeginEventCallback));
  return callback;
}

static int LegacyConsolePointerMoveEventCallback() {
  static const int callback = EvtSched_RegisterLegacyCallback(
      reinterpret_cast<std::intptr_t>(&ConsolePointerMoveEventCallback));
  return callback;
}

static int LegacyConsolePointerEndEventCallback() {
  static const int callback = EvtSched_RegisterLegacyCallback(
      reinterpret_cast<std::intptr_t>(&ConsolePointerEndEventCallback));
  return callback;
}

static void RegisterConsoleEventHandler(const std::uint32_t event_type,
                                        const int callback,
                                        const float priority) {
  (void)EvtContext_UnregisterCurrentHandler(event_type, callback);
  EvtContext_RegisterCurrentHandler(event_type, callback, 0, priority);
}

static void RegisterStartupConsoleCommands() {
  auto &console = openwow::debug::DebugConsole::Get();

  const auto register_raw = [&console](const std::string_view name,
                                       const std::string_view help,
                                       auto handler) {
    console.RegisterRawCommand(std::string(name), std::string(help),
                               std::move(handler), std::string(help), 2);
  };
  const auto register_command = [&console](const std::string_view name,
                                           const std::string_view help,
                                           auto handler) {
    console.RegisterCommand(std::string(name), std::string(help),
                            std::move(handler), 0, std::string(help), 2);
  };

  register_raw(
      "fontcolor", "[ColorClassName] [Red 0-255] [Green 0-255] [Blue 0-255]",
      [](const std::string_view args) {
        (void)Console_InputColorCommand(std::string(args));
        return std::string{};
      });
  register_raw(
      "bgcolor", "[alpha 0-255] [Red 0-255] [Green 0-255] [Blue 0-255]",
      [](const std::string_view args) {
        (void)Console_BgColorCommand("bgcolor", std::string(args));
        return std::string{};
      });
  register_raw(
      "highlightcolor", "[alpha 0-255] [Red 0-255] [Green 0-255] [Blue 0-255]",
      [](const std::string_view args) {
        (void)Console_HighlightColorCommand("highlightcolor", std::string(args));
        return std::string{};
      });
  register_raw("fontsize", "[15-50] arbitrary font size",
               [](const std::string_view args) {
                 (void)Console_FontSizeCommand(args);
                 return std::string{};
               });
  register_raw("font", "[fontname] make sure to use the .ttf file name",
               [](const std::string_view args) {
                 (void)Console_FontCommand(args);
                 return std::string{};
               });
  register_raw("consolelines", "[number] number of lines to show in the console",
               [](const std::string_view args) {
                 (void)Console_ConsoleLinesCommand("consolelines", std::string(args));
                 return std::string{};
               });
  register_command("clear", "Clears the console buffer",
                   [](const std::vector<std::string> &) {
                     (void)Console_ClearCommand();
                     return std::string{};
                   });
  register_command("proportionaltext", "Toggles fixed-width text characters",
                   [](const std::vector<std::string> &) {
                     (void)Console_ProportionalTextCommand();
                     return std::string{};
                   });
  register_raw("spacing", "[float] specifies inter-character spacing, in pixels",
               [](const std::string_view args) {
                 (void)Console_SpacingCommand(args);
                 return std::string{};
               });
  register_command("settings", "Shows current font and console settings",
                   [](const std::vector<std::string> &) {
                     (void)Console_SettingsCommand();
                     return std::string{};
                   });
  register_command("default", "Resets all the font and console settings",
                   [](const std::vector<std::string> &) {
                     (void)Console_DefaultCommand();
                     return std::string{};
                   });
  register_command("closeconsole", "Closes the Console window",
                   [](const std::vector<std::string> &) {
                     (void)Console_CloseWindowCommand();
                     return std::string{};
                   });
  register_raw("repeat", "Repeats a command", [](const std::string_view args) {
    (void)Console_RepeatCommand(args);
    return std::string{};
  });
  register_command(
      "AppendLogToFile",
      "[filename = ConsoleLogs/Log<Timestamp>.txt] [numLines = all]",
      [](const std::vector<std::string> &) {
        (void)Console_AppendLogToFileCommand();
        return std::string{};
      });
}

char ToUpperAscii(const char value) {
  return static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
}

bool AsciiEqualNoCase(std::string_view lhs, std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }

  for (std::size_t index = 0; index < lhs.size(); ++index) {
    if (ToUpperAscii(lhs[index]) != ToUpperAscii(rhs[index])) {
      return false;
    }
  }

  return true;
}

bool AsciiStartsWithNoCase(std::string_view value, std::string_view prefix) {
  if (value.size() < prefix.size()) {
    return false;
  }

  return AsciiEqualNoCase(value.substr(0, prefix.size()), prefix);
}

int ParseUnsignedDecimalPrefix(std::string_view digits) {
  int value = 0;
  for (const char digit : digits) {
    if (!std::isdigit(static_cast<unsigned char>(digit))) {
      break;
    }

    if (value > (std::numeric_limits<int>::max() - 9) / 10) {
      return std::numeric_limits<int>::max();
    }

    value = value * 10 + (digit - '0');
  }

  return value;
}

static int Console_Execute_HandleFileCreationState(const char *const command_line) {
  auto &state = GetConsoleFileCreationCaptureState();

  if (state.phase != kConsoleFileCreationAwaitOverwriteConfirmation) {
    if (SStrCmpNoCase(command_line, "end", 0x7FFFFFFFu) != 0) {
      char formatted_line[kConsoleFileCreationLineBufferSize];
      SStrPrintf(formatted_line, sizeof(formatted_line), "%s\n", command_line);

      const std::size_t remaining_without_terminator =
          (kConsoleFileCreationCommandBufferSize - 1u) -
          SStrLen(state.command_buffer.data());
      if (remaining_without_terminator != SStrLen(formatted_line)) {
        SStrCat(state.command_buffer.data(), formatted_line,
                kConsoleFileCreationCommandBufferSize);
      }
      return 0;
    }

    if (state.phase != kConsoleFileCreationCaptureCommandsNoStateFlipOnEnd) {
      state.phase = kConsoleFileCreationDispatchBufferedFlow;
    }
    return 1;
  }

  if (*command_line == 'n') {
    ConsoleAddLine("Canceled File Creation", COLOR_ECHO);
    state.phase = kConsoleFileCreationInactive;
    return 0;
  }

  if (*command_line == 'y') {
    ConsoleAddLine("Begin Typing the commands", COLOR_ECHO);
    state.phase = kConsoleFileCreationCaptureCommands;
    return 0;
  }

  ConsoleAddLine("You must type 'y' to confirm overwrite. Process aborted!",
                 COLOR_ERROR);
  state.phase = kConsoleFileCreationInactive;
  return 0;
}

static openwow::debug::ConsoleColor ColorFromClass(int color_class) {
  if (color_class < 0 || color_class >= COLOR_COUNT)
    return openwow::debug::ConsoleColor::White();
  const uint32_t argb = GetConsoleVisualStateStorage().text_colors[color_class];
  float r = static_cast<float>((argb >> 16) & 0xFF) / 255.0f;
  float g = static_cast<float>((argb >> 8) & 0xFF) / 255.0f;
  float b = static_cast<float>(argb & 0xFF) / 255.0f;
  return {r, g, b, 1.0f};
}

void ConsoleAddLine(const std::string &text, int color_class) {
  if (text.empty())
    return;

  auto &console = openwow::debug::DebugConsole::Get();
  console.Write(text, ColorFromClass(color_class));
  ++s_line_count;

  while (s_line_count > kMaxConsoleLines) {

    --s_line_count;
  }
}

void ConsoleLogColored(const char *fmt, int color_class, ...) {
  if (!fmt || !*fmt)
    return;

  char buf[1024];
  va_list args;
  va_start(args, color_class);
  SStrPrintfV(buf, sizeof(buf), fmt, args);
  va_end(args);

  ConsoleAddLine(buf, color_class);
}

void ConsoleLog(const char *fmt, ...) {
  if (!fmt || !*fmt)
    return;

  char buf[4096];
  va_list args;
  va_start(args, fmt);
  SStrPrintfV(buf, sizeof(buf), fmt, args);
  va_end(args);

  ConsoleAddLine(buf, COLOR_DEFAULT);
}

std::array<ConsoleQuadVertex, 4>
Console_BuildSelectionHighlightQuad(const ConsoleSelectionRect &rect) {
  return {{
      {rect.left, rect.top, 0.0f},
      {rect.right, rect.top, 0.0f},
      {rect.left, rect.bottom, 0.0f},
      {rect.right, rect.bottom, 0.0f},
  }};
}

std::array<ConsoleQuadVertex, 4>
Console_DrawSelectionHighlight(const ConsoleSelectionRect &rect) {
  return Console_BuildSelectionHighlightQuad(rect);
}

std::array<ConsoleQuadVertex, 4>
Console_BuildInputCaretQuad(float x, float y,
                            float inverse_viewport_width,
                            float line_height) {
  const float right = inverse_viewport_width + inverse_viewport_width + x;
  const float bottom = y + line_height;
  return {{
      {x, y, 0.0f},
      {right, y, 0.0f},
      {x, bottom, 0.0f},
      {right, bottom, 0.0f},
  }};
}

std::array<ConsoleQuadVertex, 4>
Console_DrawInputCaret(float x, float y,
                       float inverse_viewport_width,
                       float line_height) {
  return Console_BuildInputCaretQuad(x, y, inverse_viewport_width, line_height);
}

bool Console_ShouldRenderOverlays(const ConsoleOverlayRenderState &state) {
  return state.console_background_top < 1.0f;
}

ConsoleCopySelectionMetrics Console_UpdateCopySelectionMetrics(
    std::string_view text, float first_x, float second_x,
    const ConsoleSelectionBoundaryMeasureFn &measure_boundary) {
  ConsoleCopySelectionMetrics metrics;
  if (!measure_boundary || text.empty()) {
    return metrics;
  }

  const float left_x = std::clamp(std::min(first_x, second_x), 0.0f, 1.0f);
  const float right_x = std::clamp(std::max(first_x, second_x), 0.0f, 1.0f);

  const ConsoleSelectionBoundaryMeasurement left_boundary =
      measure_boundary(text, left_x);
  const ConsoleSelectionBoundaryMeasurement right_boundary =
      measure_boundary(text, right_x);

  metrics.start_byte_offset =
      left_boundary.byte_count != 0 ? left_boundary.byte_count - 1 : 0;
  metrics.end_byte_offset = right_boundary.byte_count;
  metrics.highlight_left_x =
      left_boundary.snapped_x < kConsoleSelectionLeftSnapThreshold
          ? 0.0f
          : left_boundary.snapped_x;
  metrics.highlight_right_x = right_boundary.snapped_x;
  return metrics;
}

int Console_HandlePointerMove(
    ConsolePointerInteractionState &state,
    const ConsolePointerPayload &pointer,
    const ConsoleCopySelectionRefreshFn &refresh_copy_metrics) {
  if (state.modifier_key_down || !state.console_visible) {
    return 1;
  }

  if (state.resize_drag_active) {
    const float resized_height = 1.0f - pointer.normalized_y;
    state.console_height =
        std::max(resized_height, state.minimum_console_height);
  } else if (1.0f - state.console_height > pointer.normalized_y) {
    return 1;
  }

  state.current_selection_x = pointer.normalized_x;
  if (state.copy_selection_mode == ConsoleCopySelectionMode::Dragging &&
      refresh_copy_metrics) {
    refresh_copy_metrics();
  }

  return 1;
}

static ConsoleSelectionBoundaryMeasurement MeasureConsoleSelectionBoundary(
    const std::string_view text, const float normalized_x) {
  ConsoleSelectionBoundaryMeasurement measurement;
  if (text.empty()) {
    return measurement;
  }

  const float advance =
      std::max(GetConsoleVisualStateStorage().minimum_font_height * 0.5f,
               1.0f / 4096.0f);
  const float clamped_x = std::clamp(normalized_x, 0.0f, 1.0f);
  const std::size_t target_character =
      static_cast<std::size_t>(clamped_x / advance);

  std::size_t byte_offset = 0;
  std::size_t character_index = 0;
  while (byte_offset < text.size() && character_index < target_character) {
    const auto lead = static_cast<unsigned char>(text[byte_offset]);
    std::size_t width = 1;
    if ((lead & 0xE0u) == 0xC0u) {
      width = 2;
    } else if ((lead & 0xF0u) == 0xE0u) {
      width = 3;
    } else if ((lead & 0xF8u) == 0xF0u) {
      width = 4;
    }
    byte_offset += std::min(width, text.size() - byte_offset);
    ++character_index;
  }

  measurement.byte_count = std::min(byte_offset + 1u, text.size());
  measurement.snapped_x = static_cast<float>(character_index) * advance;
  return measurement;
}

static void RefreshConsoleCopySelectionMetrics() {
  const std::string_view text(s_copy_buffer.data());
  const auto metrics = Console_UpdateCopySelectionMetrics(
      text, s_copy_selection_anchor_x, s_copy_selection_current_x,
      MeasureConsoleSelectionBoundary);
  s_copy_selection_start = static_cast<std::uint32_t>(
      std::min(metrics.start_byte_offset, text.size()));
  s_copy_selection_end = static_cast<std::uint32_t>(
      std::min(metrics.end_byte_offset, text.size()));
  s_selection_rect.left = metrics.highlight_left_x;
  s_selection_rect.right = metrics.highlight_right_x;
  s_selection_rect.top = s_copy_selection_line_top;
  s_selection_rect.bottom = s_copy_selection_line_bottom;
}

int Console_HandleCharacterInput(const std::uint32_t codepoint) {
  if (!IsConsoleVisible()) {
    return 1;
  }

  const std::string encoded = EncodeUtf8Scalar(codepoint);
  if (!encoded.empty() && codepoint != 0u) {
    openwow::game::simple_script::ConsoleClient_InsertText(
        GetOrCreateConsoleInputLine(), encoded.c_str());
  }
  ClearConsoleSelectionState();
  return 0;
}

int Console_BeginPointerInteraction(const ConsolePointerPayload &pointer) {
  if (!IsConsoleVisible()) {
    return 1;
  }

  auto &visual_state = GetConsoleVisualStateStorage();
  const float line_height = visual_state.minimum_font_height;
  const float console_height = visual_state.console_height;
  const float depth_from_screen_top = 1.0f - pointer.normalized_y;
  if (line_height <= 0.0f ||
      pointer.normalized_y < 1.0f - console_height) {
    return 1;
  }

  const float resize_edge = std::min(console_height, 1.0f);
  if (resize_edge - line_height * 0.75f <= depth_from_screen_top &&
      depth_from_screen_top <= console_height) {
    ClearConsoleSelectionState();
    s_pointer_interaction_active = true;
    return 0;
  }

  ClearConsoleSelectionState();
  s_pointer_interaction_active = false;

  const auto output = openwow::debug::DebugConsole::Get().GetOutput();
  std::vector<std::string> output_texts;
  output_texts.reserve(output.size());
  for (const auto &line : output) {
    output_texts.push_back(line.text);
  }

  const auto *input_layout = static_cast<const ConsoleInputLineLayout *>(
      GetOrCreateConsoleInputLine());
  const std::string input_text = input_layout->text_buffer != nullptr
                                     ? input_layout->text_buffer
                                     : "";
  const int output_anchor = s_scroll_anchor_index >= 0
                                ? s_scroll_anchor_index
                                : static_cast<int>(output_texts.size()) - 1;
  const ConsoleLineBuffer line_buffer{
      .output_texts = &output_texts,
      .scroll_anchor_index = output_anchor,
      .input_line_text = &input_text,
  };
  const std::string *const selected_line = Console_FindOutputLineAtNormalizedY(
      pointer.normalized_y, console_height, line_height, line_buffer);
  if (selected_line == nullptr) {
    return 0;
  }

  std::snprintf(s_copy_buffer.data(), s_copy_buffer.size(), "%s",
                selected_line->c_str());
  s_copy_selection_mode = static_cast<int>(ConsoleCopySelectionMode::Dragging);
  s_copy_selection_anchor_x = pointer.normalized_x;
  s_copy_selection_current_x = pointer.normalized_x;

  const int line_from_bottom = static_cast<int>(
      (console_height - depth_from_screen_top) / line_height);
  s_copy_selection_line_top =
      1.0f - ((console_height - line_height * 1.75f) -
              (static_cast<float>(line_from_bottom) - 1.0f) * line_height);
  s_copy_selection_line_bottom = s_copy_selection_line_top - line_height;
  RefreshConsoleCopySelectionMetrics();
  return 0;
}

int Console_UpdatePointerInteraction(const ConsolePointerPayload &pointer) {
  ConsolePointerInteractionState state{
      .modifier_key_down = false,
      .console_visible = IsConsoleVisible(),
      .resize_drag_active = s_pointer_interaction_active,
      .copy_selection_mode =
          static_cast<ConsoleCopySelectionMode>(s_copy_selection_mode),
      .console_height = GetConsoleVisualStateStorage().console_height,
      .minimum_console_height =
          GetConsoleVisualStateStorage().minimum_font_height,
      .current_selection_x = s_copy_selection_current_x,
  };
  const int result = Console_HandlePointerMove(state, pointer, {});
  GetConsoleVisualStateStorage().console_height = state.console_height;
  s_copy_selection_current_x = state.current_selection_x;
  if (state.copy_selection_mode == ConsoleCopySelectionMode::Dragging) {
    RefreshConsoleCopySelectionMetrics();
  }
  return result;
}

int Console_EndPointerInteraction() {
  if (!IsConsoleVisible()) {
    return 1;
  }
  s_copy_selection_mode = static_cast<int>(ConsoleCopySelectionMode::Latched);
  s_pointer_interaction_active = false;
  return 1;
}

const std::string *Console_FindOutputLineAtNormalizedY(
    float normalized_y,
    float console_height,
    float line_height,
    const ConsoleLineBuffer &buffer) {
  if (line_height <= 0.0f)
    return nullptr;

  int line_from_bottom = static_cast<int>(
      (console_height - (1.0f - normalized_y)) / line_height);

  if (line_from_bottom == 1)
    return buffer.input_line_text;

  const bool scroll_valid =
      buffer.output_texts != nullptr && buffer.scroll_anchor_index >= 0 &&
      buffer.scroll_anchor_index <
          static_cast<int>(buffer.output_texts->size());

  if (scroll_valid)
    --line_from_bottom;

  if (!scroll_valid)
    return nullptr;

  int idx = buffer.scroll_anchor_index;
  while (idx >= 0) {
    if (line_from_bottom <= 1)
      return &(*buffer.output_texts)[static_cast<std::size_t>(idx)];
    --idx;
    --line_from_bottom;
  }

  return nullptr;
}

void Console_Execute(const std::string &command_line, bool add_to_history) {
  auto &file_creation_state = GetConsoleFileCreationCaptureState();
  if (file_creation_state.phase <=
          kConsoleFileCreationCaptureCommandsNoStateFlipOnEnd &&
      !Console_Execute_HandleFileCreationState(command_line.c_str())) {
    return;
  }

  if (command_line.empty())
    return;

  auto start = command_line.find_first_not_of(' ');
  if (start == std::string::npos)
    return;
  std::string trimmed = command_line.substr(start);

  if (add_to_history) {
    if (!AsciiEqualNoCase(trimmed, CommandHistoryGetRelativeEntry(0))) {
      CommandHistoryPush(trimmed);
    }
    ResetCommandHistoryNavigation();
  }

  auto &console = openwow::debug::DebugConsole::Get();
  std::string result = console.Execute(trimmed, add_to_history);
  if (!result.empty()) {
    ConsoleAddLine(result, COLOR_DEFAULT);
  }
}

void Console_ExecuteGraphicsRestart() {
  Console_Execute("gxRestart", true);
}

void ResetConsoleFileCreationStateForTests() {
  auto &state = GetConsoleFileCreationCaptureState();
  state.phase = kConsoleFileCreationInactive;
  state.command_buffer.fill('\0');
}

void SetConsoleFileCreationStateForTests(const int state_value) {
  GetConsoleFileCreationCaptureState().phase = state_value;
}

int GetConsoleFileCreationStateForTests() {
  return GetConsoleFileCreationCaptureState().phase;
}

void SetConsoleFileCreationBufferForTests(const std::string_view buffer) {
  auto &state = GetConsoleFileCreationCaptureState();
  state.command_buffer.fill('\0');
  if (!buffer.empty()) {
    SStrCopy(state.command_buffer.data(), std::string(buffer).c_str(),
             kConsoleFileCreationCommandBufferSize);
  }
}

std::string GetConsoleFileCreationBufferForTests() {
  return GetConsoleFileCreationCaptureState().command_buffer.data();
}

void ResetConsoleVisualStateForTests() { RestoreDefaultConsoleVisualState(); }

void SetConsoleVisualStateForTests(const ConsoleVisualStateSnapshot &state) {
  GetConsoleVisualStateStorage() = state;
}

ConsoleVisualStateSnapshot GetConsoleVisualStateForTests() {
  return GetConsoleVisualStateStorage();
}

void ResetConsoleInputStateForTests() {
  delete[] s_console_input_line.text_buffer;
  s_console_input_line = {};
  s_console_input_line_created = false;
  s_tab_complete_active = false;
  s_tab_complete_prefix.clear();
  s_tab_complete_cursor.clear();
  s_last_history_index = -1;
  s_scroll_anchor_index = -1;
  s_pointer_interaction_active = false;
  ClearConsoleSelectionState();
}

std::string GetConsoleEditableTextForTests() {
  return GetEditableText(GetOrCreateConsoleInputLine());
}

float GetConsoleBackgroundTop() { return s_console_background_top; }

void SetConsoleBackgroundTopForTests(const float top) {
  s_console_background_top = top;
}

bool IsPointerInteractionActive() { return s_pointer_interaction_active; }

void SetPointerInteractionActive(const bool active) {
  s_pointer_interaction_active = active;
}

void ResetConsoleFadeStateForTests() {
  s_console_background_top = 1.0f;
  s_pointer_interaction_active = false;
}

bool IsConsoleVisible() {
  return openwow::debug::DebugConsole::Get().IsVisible();
}

void SetConsoleVisible(bool visible) {
  openwow::debug::DebugConsole::Get().SetVisible(visible);
}

int Console_CloseWindowCommand() {
  SetConsoleVisible(false);
  return 0;
}

int Console_RepeatCommand(const std::string_view args) {
  if (args.empty()) {
    s_console_repeat_remaining_count = 0;
    return 1;
  }

  std::string command_line(args);
  std::size_t separator = 0;
  while (separator < command_line.size() && command_line[separator] != '\t' &&
         command_line[separator] != ' ') {
    ++separator;
  }

  if (separator < command_line.size()) {
    command_line[separator] = '\0';
    const char *const repeated_command = command_line.c_str() + separator + 1;
    const std::uint32_t repeat_count = ParseUnsignedDecimalWrapping(command_line.c_str());
    if (repeat_count != 0 && *repeated_command != '\0') {
      std::snprintf(s_console_repeat_command_text.data(), s_console_repeat_command_text.size(),
                    "%s", repeated_command);
      s_console_repeat_remaining_count = repeat_count;
    }
  }

  return 1;
}

int Console_PeriodicUpdate(const float *frame_delta_seconds) {
  const bool console_active = IsConsoleVisible();
  float target;
  if (!console_active) {
    target = 1.0f;
  } else {
    const float raw = 1.0f - GetConsoleVisualStateStorage().console_height;
    target = (raw > 1.0f) ? 1.0f : (raw > 0.0f ? raw : 0.0f);
  }

  if (s_console_repeat_remaining_count != 0 &&
      s_console_repeat_command_text.front() != '\0') {
    Console_Execute(s_console_repeat_command_text.data(), false);
    --s_console_repeat_remaining_count;
  }

  if (s_console_background_top == target) {
    return 1;
  }

  float new_top;
  if (s_pointer_interaction_active) {
    new_top = target;
  } else {
    const float direction =
        (s_console_background_top <= target) ? 1.0f : -1.0f;
    const float delta =
        direction * (*frame_delta_seconds) * kConsoleFadeSpeed;

    new_top = s_console_background_top + delta;

    if (console_active) {

      if (target > new_top) {
        new_top = target;
      }
    } else {

      if (target < new_top) {
        new_top = target;
      }
    }
  }

  s_console_background_top = new_top;

  return 1;
}

int Console_SpacingCommand(const std::string_view raw_args) {
  if (!raw_args.empty()) {
    s_console_character_spacing_pixels =
        static_cast<float>(ParseFloatLikeSub76FB80(raw_args));
  } else {
    s_console_character_spacing_pixels = 0.0f;
  }

  return 1;
}

float GetConsoleCharacterSpacingPixels() {
  return s_console_character_spacing_pixels;
}

int GetConsoleToggleKeyCode() {
  return s_console_toggle_key_code;
}

void SetConsoleToggleKeyCode(const int key_code) {
  s_console_toggle_key_code = key_code;
}

bool IsConsoleInputRoutingInitialized() {
  return s_console_input_routing_initialized;
}

void SetConsoleInputRoutingInitialized(const bool initialized) {
  s_console_input_routing_initialized = initialized;
}

int Console_ShouldPassKeyRelease(const int key_code) {
  return ((key_code != s_console_toggle_key_code) || !s_console_input_routing_initialized) &&
         !IsConsoleVisible();
}

int ResolveConsoleToggleKeyCode(const std::string_view key_name, const int fallback_key_code) {
  if (key_name.size() == 1) {
    const char key = key_name.front();
    if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z') || key < 'a' || key > 'z') {
      return static_cast<unsigned char>(key);
    }
    return static_cast<unsigned char>(ToUpperAscii(key));
  }

  if (!key_name.empty() && ToUpperAscii(key_name.front()) == 'F' &&
      key_name.size() >= 2 &&
      std::isdigit(static_cast<unsigned char>(key_name[1]))) {
    return 767 + ParseUnsignedDecimalPrefix(key_name.substr(1));
  }

  constexpr std::string_view kNumpadPrefix = "NUMPAD";
  if (AsciiStartsWithNoCase(key_name, kNumpadPrefix)) {
    std::string_view remainder = key_name.substr(kNumpadPrefix.size());
    while (!remainder.empty() && remainder.front() == ' ') {
      remainder.remove_prefix(1);
    }

    if (!remainder.empty() && std::isdigit(static_cast<unsigned char>(remainder.front()))) {
      return static_cast<unsigned char>(remainder.front()) + 208;
    }
    if (!remainder.empty() &&
        (AsciiEqualNoCase(remainder, "PLUS") || remainder.front() == '+')) {
      return 266;
    }
    if (!remainder.empty() &&
        (AsciiEqualNoCase(remainder, "MINUS") || remainder.front() == '-')) {
      return 267;
    }
    if (!remainder.empty() &&
        (AsciiEqualNoCase(remainder, "MULTIPLY") || remainder.front() == '*')) {
      return 268;
    }
    if (!remainder.empty() &&
        (AsciiEqualNoCase(remainder, "DIVIDE") || remainder.front() == '/')) {
      return 269;
    }
    if (!remainder.empty() &&
        (AsciiEqualNoCase(remainder, "DECIMAL") || remainder.front() == '.')) {
      return 270;
    }
    if (!remainder.empty() &&
        (AsciiEqualNoCase(remainder, "EQUALS") || remainder.front() == '=')) {
      return 780;
    }
    return fallback_key_code;
  }

  if (AsciiEqualNoCase(key_name, "ESCAPE")) {
    return 512;
  }
  if (AsciiEqualNoCase(key_name, "ENTER")) {
    return 513;
  }
  if (AsciiEqualNoCase(key_name, "BACKSPACE")) {
    return 514;
  }
  if (AsciiEqualNoCase(key_name, "TAB")) {
    return 515;
  }
  if (AsciiEqualNoCase(key_name, "LEFT")) {
    return 516;
  }
  if (AsciiEqualNoCase(key_name, "UP")) {
    return 517;
  }
  if (AsciiEqualNoCase(key_name, "RIGHT")) {
    return 518;
  }
  if (AsciiEqualNoCase(key_name, "DOWN")) {
    return 519;
  }
  if (AsciiEqualNoCase(key_name, "INSERT")) {
    return 520;
  }
  if (AsciiEqualNoCase(key_name, "DELETE")) {
    return 521;
  }
  if (AsciiEqualNoCase(key_name, "HOME")) {
    return 522;
  }
  if (AsciiEqualNoCase(key_name, "END")) {
    return 523;
  }
  if (AsciiEqualNoCase(key_name, "PAGEUP")) {
    return 524;
  }
  if (AsciiEqualNoCase(key_name, "PAGEDOWN")) {
    return 525;
  }
  if (AsciiEqualNoCase(key_name, "NUMLOCK")) {
    return 527;
  }
  if (AsciiEqualNoCase(key_name, "PRINTSCREEN")) {
    return 530;
  }

  return fallback_key_code;
}

void ConsoleAndFont_StartupInitialize() {

  Console_RegisterBasicCommands();
  ClearConsoleRepeatCommandState();
  s_console_character_spacing_pixels = 0.0f;

  RegisterConsoleEventHandler(1u, LegacyConsoleCharacterEventCallback(), 7.0f);
  RegisterConsolePeriodicUpdateHandler(7.0f);
  RegisterConsoleEventHandler(9u, LegacyConsoleKeyEventCallback(), 7.0f);
  RegisterConsoleEventHandler(11u, LegacyConsoleKeyEventCallback(), 7.0f);
  RegisterConsoleEventHandler(10u, LegacyConsoleKeyReleaseEventCallback(), 7.0f);
  RegisterConsoleEventHandler(12u, LegacyConsolePointerBeginEventCallback(), 7.0f);
  RegisterConsoleEventHandler(15u, LegacyConsolePointerEndEventCallback(), 7.0f);
  RegisterConsoleEventHandler(13u, LegacyConsolePointerMoveEventCallback(), 7.0f);

  EvtWindow_SetTerminationCallback(&ConsoleWindowTerminationCallback, 0);

  RegisterStartupConsoleCommands();

  Console_Execute("ver", true);
}

void Console_UnregisterEventHandlers() {
  (void)EvtContext_UnregisterCurrentHandler(
      1u, LegacyConsoleCharacterEventCallback());
  (void)EvtContext_UnregisterCurrentHandler(6u, LegacyConsolePeriodicUpdateCallback());
  (void)EvtContext_UnregisterCurrentHandler(9u, LegacyConsoleKeyEventCallback());
  (void)EvtContext_UnregisterCurrentHandler(11u, LegacyConsoleKeyEventCallback());
  (void)EvtContext_UnregisterCurrentHandler(
      10u, LegacyConsoleKeyReleaseEventCallback());
  (void)EvtContext_UnregisterCurrentHandler(
      12u, LegacyConsolePointerBeginEventCallback());
  (void)EvtContext_UnregisterCurrentHandler(
      15u, LegacyConsolePointerEndEventCallback());
  (void)EvtContext_UnregisterCurrentHandler(
      13u, LegacyConsolePointerMoveEventCallback());
}

void ConsoleAndFont_Shutdown() {
  EvtWindow_SetTerminationCallback(nullptr, 0);
  Console_UnregisterEventHandlers();
  ClearConsoleRepeatCommandState();
  s_console_character_spacing_pixels = 0.0f;
  s_pointer_interaction_active = false;
  ClearConsoleSelectionState();
}

void SetConsoleSetMapCommandHandler(ConsoleSetMapCommandHandler handler) {
  GetConsoleSetMapCommandHandler() = std::move(handler);
}

void ResetConsoleSetMapCommandHandlerForTests() {
  GetConsoleSetMapCommandHandler() = {};
}

void Console_RegisterBasicCommands() {
  auto &console = openwow::debug::DebugConsole::Get();

  console.RegisterCommand("help", "Provides help information about a command.",
                          [](const std::vector<std::string> &args) -> std::string {
                            if (args.size() >= 2) {
                              Console_Help(args[1]);
                              return "";
                            }
                            Console_Help("");
                            return "";
                          }, 0, "Provides help information about a command.", 2);

  console.RegisterCommand("quit", "Exit the application",
                          [](const std::vector<std::string> & ) -> std::string {
                            ConsoleClient_RequestShutdown();
                            return "";
                          }, 0, "", 5);

  console.RegisterCommand("ver", "Show build version",
                          [](const std::vector<std::string> & ) -> std::string {
                            return std::string(kRetailI386BuildString);
                          }, 0, "", 5);

  console.RegisterRawCommand("setmap", "Set map (debug)",
                             [](const std::string_view raw_args) -> std::string {
                               auto &handler = GetConsoleSetMapCommandHandler();
                               if (handler) {
                                 handler(raw_args);
                               }
                               return {};
                             }, "", 5);
}

int ConsoleClient_HandleKeyEvent(const unsigned int *event_data) {
  const unsigned int key_code = event_data[0];
  const unsigned int modifiers = event_data[1];
  const bool shift_held = (modifiers & 0x3u) != 0;
  const bool ctrl_held = (modifiers & 0xCu) != 0;

  if (key_code == static_cast<unsigned int>(s_console_toggle_key_code) &&
      s_console_input_routing_initialized) {
    const bool was_visible = IsConsoleVisible();
    SetConsoleVisible(!was_visible);

    if (was_visible) {
      s_copy_selection_mode = 0;
      s_selection_rect = {};
    }
    return 0;
  }

  if (!IsConsoleVisible()) {
    return 1;
  }

  void *const input_line = GetOrCreateConsoleInputLine();
  auto *line = static_cast<ConsoleInputLineLayout *>(input_line);

  if (key_code == 67u) {
    if (ctrl_held) {
      if (s_copy_buffer[0] != '\0') {
        const std::size_t copy_len =
            (s_copy_selection_end > s_copy_selection_start)
                ? static_cast<std::size_t>(s_copy_selection_end - s_copy_selection_start)
                : 0u;
        const std::size_t clamped = (copy_len >= 127u) ? 127u : copy_len;
        if (clamped > 0 && s_copy_selection_start < s_copy_buffer.size()) {
          char temp[128]{};
          std::memcpy(temp, &s_copy_buffer[s_copy_selection_start], clamped);
          temp[clamped] = '\0';
          openwow::platform::OsClipboard_SetTextForActiveWindow(temp);
        }
      }
      s_copy_selection_mode = 0;
      s_selection_rect = {};
    }
    goto finish;
  }

  if (key_code == 86u) {
    if (ctrl_held) {
      char *clipboard_text = openwow::platform::OsClipboard_GetTextForActiveWindow();
      if (clipboard_text != nullptr) {
        openwow::game::simple_script::ConsoleClient_InsertText(input_line, clipboard_text);
        openwow::platform::OsClipboard_FreeText(clipboard_text);
        s_copy_selection_mode = 0;
        s_selection_rect = {};
      }
    }
    goto finish;
  }

  if (key_code == 512u) {
    if (line->cursor_offset > line->prompt_offset) {
      line->cursor_offset = line->prompt_offset;
      line->text_length = line->prompt_offset;
      if (line->text_buffer != nullptr) {
        line->text_buffer[line->prompt_offset] = '\0';
      }
    } else {
      SetConsoleVisible(false);
    }
    goto finish;
  }

  switch (key_code) {
    case 513u: {

      if (line->cursor_offset > line->prompt_offset) {

        const char *editable = line->text_buffer + line->prompt_offset;
        ConsoleAddLine(std::string("> ") + editable, COLOR_INPUT);
        Console_Execute(editable, true);
        s_last_history_index = -1;

        ResetConsoleInputLineToPrompt(input_line);
      }
      break;
    }

    case 514u: {

      openwow::game::simple_script::ConsoleClient_DeleteCharBeforeCursor(input_line);
      break;
    }

    case 515u: {

      if (!s_tab_complete_active) {
        s_tab_complete_cursor.clear();
        s_tab_complete_active = true;
        s_tab_complete_prefix = GetEditableText(input_line);
      }
      if (Console_TabCompleteNext(s_tab_complete_prefix,
                                  s_tab_complete_cursor,
                                  shift_held)) {
        openwow::game::simple_script::ConsoleClient_ReplaceEditableText(
            input_line, s_tab_complete_cursor.c_str());
      }
      break;
    }

    case 516u: {

      if (line->cursor_offset > line->prompt_offset) {
        --line->cursor_offset;
      }
      break;
    }

    case 517u: {

      openwow::game::simple_script::ConsoleClient_LoadOlderHistoryEntry(input_line);
      break;
    }

    case 518u: {

      if (line->cursor_offset < line->text_length) {
        ++line->cursor_offset;
      }
      break;
    }

    case 519u: {

      openwow::game::simple_script::ConsoleClient_LoadNewerHistoryEntry(input_line);
      break;
    }

    case 521u: {

      openwow::game::simple_script::ConsoleClient_DeleteCharAtCursor(input_line);
      break;
    }

    case 522u: {

      if (ctrl_held) {

        const auto &console = openwow::debug::DebugConsole::Get();
        const auto output_count = static_cast<int>(console.GetOutputSize());
        if (output_count > 0) {
          s_scroll_anchor_index = 0;
        }
      } else {

        line->cursor_offset = line->prompt_offset;
      }
      break;
    }

    case 523u: {

      if (ctrl_held) {

        s_scroll_anchor_index = -1;
      } else {

        line->cursor_offset = line->text_length;
      }
      break;
    }

    case 524u: {

      const int scroll_amount = shift_held ? 5 : 10;
      const auto &console = openwow::debug::DebugConsole::Get();
      const auto output_count = static_cast<int>(console.GetOutputSize());
      if (output_count > 0) {
        if (s_scroll_anchor_index < 0) {
          s_scroll_anchor_index = output_count - 1;
        }
        s_scroll_anchor_index -= scroll_amount;
        if (s_scroll_anchor_index < 0) {
          s_scroll_anchor_index = 0;
        }
      }
      break;
    }

    case 525u: {

      const int scroll_amount = shift_held ? 5 : 10;
      const auto &console = openwow::debug::DebugConsole::Get();
      const auto output_count = static_cast<int>(console.GetOutputSize());
      if (output_count > 0 && s_scroll_anchor_index >= 0) {
        s_scroll_anchor_index += scroll_amount;
        if (s_scroll_anchor_index >= output_count) {

          s_scroll_anchor_index = -1;
        }
      }
      break;
    }

    default:
      break;
  }

finish:

  if (key_code != 515u && key_code >= 2u && key_code != 4u &&
      key_code != 5u && !ctrl_held) {
    s_tab_complete_active = false;
    s_copy_selection_mode = 0;
    s_selection_rect = {};
  }

  return 0;
}

bool Console_TabCompleteNext(const std::string_view prefix,
                             std::string &cursor,
                             const bool reverse) {

  auto &console = openwow::debug::DebugConsole::Get();
  const std::vector<std::string> all_commands = console.GetCommandNames();

  if (all_commands.empty()) {
    return false;

  }

  std::size_t start_index = 0;
  bool found_cursor = false;

  if (!cursor.empty()) {
    for (std::size_t i = 0; i < all_commands.size(); ++i) {
      if (AsciiEqualNoCase(all_commands[i], cursor)) {
        if (reverse) {
          if (i == 0) {
            return false;

          }
          start_index = i - 1;
        } else {
          if (i + 1 >= all_commands.size()) {
            return false;

          }
          start_index = i + 1;
        }
        found_cursor = true;
        break;
      }
    }

    if (!found_cursor) {
      return false;
    }
  } else {
    if (reverse) {
      start_index = all_commands.size() - 1;
    } else {
      start_index = 0;
    }
  }

  if (reverse) {

    for (std::size_t i = start_index; ; --i) {
      if (AsciiStartsWithNoCase(all_commands[i], prefix)) {
        cursor = all_commands[i];
        return true;
      }
      if (i == 0) {
        break;

      }
    }
  } else {

    for (std::size_t i = start_index; i < all_commands.size(); ++i) {
      if (AsciiStartsWithNoCase(all_commands[i], prefix)) {
        cursor = all_commands[i];
        return true;
      }
    }
  }

  return false;
}

int Console_Help(const std::string &topic) {
  auto &console = openwow::debug::DebugConsole::Get();

  struct RetailHelpCategory {
    std::string_view name;
    int id;
  };
  static constexpr std::array<RetailHelpCategory, 9> kRetailHelpCategories{{
      {"debug", 0},   {"graphics", 1}, {"console", 2},
      {"combat", 3},  {"game", 4},     {"default", 5},
      {"net", 6},     {"sound", 7},    {"gm", 8},
  }};

  if (topic.empty()) {
    console.Write("Console help categories: ");
    std::string categories;
    for (std::size_t index = 0; index < kRetailHelpCategories.size(); ++index) {
      if (index != 0) {
        categories += ", ";
      }
      categories += kRetailHelpCategories[index].name;
    }
    console.Write(categories, openwow::debug::ConsoleColor::Cyan());
    console.Write("For more information type 'help [command] or [category]'",
                  openwow::debug::ConsoleColor::Cyan());
    return 1;
  }

  for (const auto &category : kRetailHelpCategories) {
    if (!AsciiEqualNoCase(topic, category.name)) {
      continue;
    }

    console.Write("Commands registered for the category " + topic + ":",
                  openwow::debug::ConsoleColor::Cyan());
    const auto command_names = console.GetCommandNamesForRetailCategory(category.id);
    if (command_names.empty()) {
      console.Write("NONE", openwow::debug::ConsoleColor::Cyan());
      break;
    }

    for (std::size_t offset = 0; offset < command_names.size(); offset += 8) {
      std::string line;
      const std::size_t end = std::min(offset + 8, command_names.size());
      for (std::size_t index = offset; index < end; ++index) {
        if (!line.empty()) {
          line += ", ";
        }
        line += command_names[index];
      }
      console.Write(line, openwow::debug::ConsoleColor::Cyan());
    }
    break;
  }

  if (console.HasCommand(topic)) {
    std::string help = console.GetCommandUsage(topic);
    if (help.empty()) {
      help = "No help yet";
    }
    console.Write("Help for command " + topic + ":", openwow::debug::ConsoleColor::Cyan());
    console.Write("     " + topic + " " + help, openwow::debug::ConsoleColor::Cyan());
  }

  return 1;
}

int Console_InputColorCommand(const std::string &args) {
  char class_name[64] = {};
  unsigned int r = 0, g = 0, b = 0;

  if (std::sscanf(args.c_str(), "%63s %u %u %u", class_name, &r, &g, &b) != 4) {
    ConsoleAddLine("Invalid number of parameters", COLOR_ERROR);
    return 0;
  }

  int idx = -1;
  struct {
    const char *name;
    int idx;
  } map[] = {
      {"input", 1},   {"default", 0}, {"echo", 2},   {"error", 3},
      {"warning", 4}, {"admin", 6},   {"global", 5},
  };
  for (const auto &m : map) {
    if (text::EqualsIgnoreCaseAscii(class_name, m.name)) {
      idx = m.idx;
      break;
    }
  }
  if (idx < 0) {
    ConsoleAddLine("Unknown color class. Choose 'input','echo','warning','admin',or 'global' "
                   "as the first parameter",
                   COLOR_ERROR);
    return 0;
  }

  if (r > 255 || g > 255 || b > 255) {
    ConsoleAddLine("One or more colors are not in the 0 to 255 range or missing.", COLOR_ERROR);
    ConsoleAddLine("Make sure to specify the red, green and blue colors.", COLOR_ERROR);
    return 0;
  }

  GetConsoleVisualStateStorage().text_colors[idx] =
      (0xFFu << 24) | (r << 16) | (g << 8) | b;
  return 1;
}

int Console_DefaultCommand() {
  RestoreDefaultConsoleVisualState();
  return 1;
}

int Console_SettingsCommand() {
  const auto &state = GetConsoleVisualStateStorage();
  char buffer[260];

  SStrPrintf(buffer, sizeof(buffer), "Font Height is %f",
             state.minimum_font_height);
  ConsoleAddLine(buffer, COLOR_DEFAULT);

  SStrPrintf(buffer, sizeof(buffer), "Font Name is %s", state.font_path.c_str());
  ConsoleAddLine(buffer, COLOR_DEFAULT);

  SStrPrintf(buffer, sizeof(buffer), "Number of Console Lines %f",
             state.visible_line_count);
  ConsoleAddLine(buffer, COLOR_DEFAULT);
  return 1;
}

int Console_BgColorCommand(const std::string &cmd_name,
                           const std::string &args) {
  unsigned int a = 0, r = 0, g = 0, b = 0;

  if (std::sscanf(args.c_str(), "%u %u %u %u", &a, &r, &g, &b) != 4) {
    ConsoleAddLine("Invalid number of parameters", COLOR_ERROR);
    Console_Help(cmd_name);
    return 0;
  }

  if (a > 255 || r > 255 || g > 255 || b > 255) {
    ConsoleAddLine(
        "One or more colors are not in the 0 to 255 range or missing.",
        COLOR_ERROR);
    ConsoleAddLine("Make sure to specify the red, green and blue colors.",
                   COLOR_ERROR);
    return 0;
  }

  GetConsoleVisualStateStorage().background_color =
      (a << 24) | (r << 16) | (g << 8) | b;
  return 1;
}

int Console_HighlightColorCommand(const std::string &cmd_name,
                                  const std::string &args) {
  unsigned int a = 0, r = 0, g = 0, b = 0;

  if (std::sscanf(args.c_str(), "%u %u %u %u", &a, &r, &g, &b) != 4) {
    ConsoleAddLine("Invalid number of parameters", COLOR_ERROR);
    Console_Help(cmd_name);
    return 0;
  }

  if (a > 255 || r > 255 || g > 255 || b > 255) {
    ConsoleAddLine(
        "One or more colors are not in the 0 to 255 range or missing.",
        COLOR_ERROR);
    ConsoleAddLine("Make sure to specify the red, green and blue colors.",
                   COLOR_ERROR);
    return 0;
  }

  GetConsoleVisualStateStorage().selection_highlight_color =
      (a << 24) | (r << 16) | (g << 8) | b;
  return 1;
}

int Console_FontSizeCommand(const std::string_view raw_args) {
  auto &state = GetConsoleVisualStateStorage();

  float value = 0.0f;
  char buf[64] = {};
  const auto len = std::min(raw_args.size(), sizeof(buf) - 1);
  std::memcpy(buf, raw_args.data(), len);
  if (std::sscanf(buf, "%f", &value) != 1) {
    return 0;
  }
  double font_height = static_cast<double>(value) * 0.001;

  if (font_height < 0.01) {
    font_height = 0.01;
  } else if (font_height > 0.05) {
    font_height = 0.05;
  }

  state.minimum_font_height = static_cast<float>(font_height);

  if (state.minimum_font_height > 0.0f) {
    state.visible_line_count = state.console_height / state.minimum_font_height;
    state.console_height =
        state.minimum_font_height * state.visible_line_count;
  }

  return 1;
}

int Console_FontCommand(const std::string_view font_name) {
  auto &state = GetConsoleVisualStateStorage();

  std::string path = "Fonts\\";
  path.append(font_name);
  path.append(".ttf");

  state.font_path = path;
  return 1;
}

int Console_ConsoleLinesCommand(const std::string &cmd_name,
                                const std::string &args) {
  if (args.empty()) {
    ConsoleAddLine("Please specify how many lines to display", COLOR_ERROR);
    return 1;
  }

  float line_count = 0.0f;
  if (std::sscanf(args.c_str(), "%f", &line_count) != 1) {
    ConsoleAddLine("Invalid number of parameters", COLOR_ERROR);
    Console_Help(cmd_name);
    return 0;
  }

  if (line_count == 0.0f) {
    return 0;
  }

  auto &state = GetConsoleVisualStateStorage();
  state.visible_line_count = line_count;
  state.console_height = line_count * state.minimum_font_height;
  return 1;
}

int Console_ClearCommand() {
  auto &console = openwow::debug::DebugConsole::Get();
  console.ClearOutput();
  return 1;
}

int Console_ProportionalTextCommand() {

  auto &state = GetConsoleVisualStateStorage();
  state.text_flags ^= kConsoleProportionalTextFlag;
  return 1;
}

int Console_AppendLogToFileCommand() {
  ConsoleAddLine("Access Denied", COLOR_ERROR);
  return 0;
}

}

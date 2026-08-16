
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include "openwow/foundation/compiler/printf_format.h"

namespace openwow::core::ida {

enum ConsoleColorClass : int {
  COLOR_DEFAULT = 0,
  COLOR_INPUT = 1,
  COLOR_ECHO = 2,
  COLOR_ERROR = 3,
  COLOR_WARNING = 4,
  COLOR_GLOBAL = 5,
  COLOR_ADMIN = 6,
  COLOR_COUNT = 7,
};

inline constexpr int kDefaultConsoleToggleKeyCode = 787;
inline constexpr std::string_view kRetailI386BuildString =
    "WoW [Release] Build 12340 (Jun 25 2010)";

inline constexpr std::uint32_t kConsoleProportionalTextFlag = 0x10u;

struct ConsoleVisualStateSnapshot {
  float minimum_font_height = 0.0f;

  float visible_line_count = 0.0f;

  float console_height = 0.0f;

  std::string font_path;

  std::array<std::uint32_t, COLOR_COUNT> text_colors{};
  std::uint32_t selection_highlight_color = 0;

  std::uint32_t background_color = 0;

  std::uint32_t text_flags = 0;

};

struct ConsoleSelectionBoundaryMeasurement {
  std::size_t byte_count = 0;
  float snapped_x = 0.0f;
};

using ConsoleSelectionBoundaryMeasureFn =
    std::function<ConsoleSelectionBoundaryMeasurement(std::string_view text,
                                                      float normalized_x)>;

struct ConsoleCopySelectionMetrics {
  std::size_t start_byte_offset = 0;
  std::size_t end_byte_offset = 0;
  float highlight_left_x = 0.0f;
  float highlight_right_x = 0.0f;
};

enum class ConsoleCopySelectionMode : int {
  Inactive = 0,
  Dragging = 1,

  Latched = 2,

};

struct ConsolePointerPayload {
  float normalized_x = 0.0f;
  float normalized_y = 0.0f;
};

struct ConsolePointerInteractionState {
  bool modifier_key_down = false;
  bool console_visible = false;
  bool resize_drag_active = false;

  ConsoleCopySelectionMode copy_selection_mode =
      ConsoleCopySelectionMode::Inactive;
  float console_height = 0.0f;

  float minimum_console_height = 0.0f;

  float current_selection_x = 0.0f;

};

using ConsoleCopySelectionRefreshFn = std::function<void()>;

struct ConsoleLineBuffer {

  const std::vector<std::string> *output_texts = nullptr;

  int scroll_anchor_index = -1;

  const std::string *input_line_text = nullptr;
};

struct ConsoleSelectionRect {
  float left = 0.0f;

  float top = 0.0f;

  float right = 0.0f;

  float bottom = 0.0f;

};

struct ConsoleOverlayRenderState {
  float console_background_top = 1.0f;

  bool selection_active = false;

  ConsoleSelectionRect selection_rect;

  std::uint32_t highlight_color =
      0x80FFFFFFu;

  std::uint32_t background_color =
      0xC0000000u;

};

struct ConsoleQuadVertex {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

[[nodiscard]] std::array<ConsoleQuadVertex, 4>
Console_BuildSelectionHighlightQuad(const ConsoleSelectionRect &rect);

[[nodiscard]] std::array<ConsoleQuadVertex, 4>
Console_DrawSelectionHighlight(const ConsoleSelectionRect &rect);

[[nodiscard]] std::array<ConsoleQuadVertex, 4>
Console_BuildInputCaretQuad(float x, float y,
                            float inverse_viewport_width,
                            float line_height);

[[nodiscard]] std::array<ConsoleQuadVertex, 4>
Console_DrawInputCaret(float x, float y,
                       float inverse_viewport_width,
                       float line_height);

[[nodiscard]] bool Console_ShouldRenderOverlays(
    const ConsoleOverlayRenderState &state);

void ConsoleAddLine(const std::string &text, int color_class);

OPENWOW_PRINTF_FORMAT(1, 3) void ConsoleLogColored(const char *fmt, int color_class, ...);

OPENWOW_PRINTF_FORMAT(1, 2) void ConsoleLog(const char *fmt, ...);

[[nodiscard]] ConsoleCopySelectionMetrics Console_UpdateCopySelectionMetrics(
    std::string_view text, float first_x, float second_x,
    const ConsoleSelectionBoundaryMeasureFn &measure_boundary);

int Console_HandlePointerMove(
    ConsolePointerInteractionState &state,
    const ConsolePointerPayload &pointer,
    const ConsoleCopySelectionRefreshFn &refresh_copy_metrics = {});

int Console_HandleCharacterInput(std::uint32_t codepoint);
int Console_BeginPointerInteraction(const ConsolePointerPayload &pointer);
int Console_UpdatePointerInteraction(const ConsolePointerPayload &pointer);
int Console_EndPointerInteraction();

[[nodiscard]] const std::string *Console_FindOutputLineAtNormalizedY(
    float normalized_y,
    float console_height,
    float line_height,
    const ConsoleLineBuffer &buffer);

void Console_Execute(const std::string &command_line, bool add_to_history);

void Console_ExecuteGraphicsRestart();

void ResetConsoleFileCreationStateForTests();
void SetConsoleFileCreationStateForTests(int state);
[[nodiscard]] int GetConsoleFileCreationStateForTests();
void SetConsoleFileCreationBufferForTests(std::string_view buffer);
[[nodiscard]] std::string GetConsoleFileCreationBufferForTests();
void ResetConsoleVisualStateForTests();
void SetConsoleVisualStateForTests(const ConsoleVisualStateSnapshot &state);
[[nodiscard]] ConsoleVisualStateSnapshot GetConsoleVisualStateForTests();
void ResetConsoleInputStateForTests();
[[nodiscard]] std::string GetConsoleEditableTextForTests();

[[nodiscard]] float GetConsoleBackgroundTop();
void SetConsoleBackgroundTopForTests(float top);
[[nodiscard]] bool IsPointerInteractionActive();
void SetPointerInteractionActive(bool active);
void ResetConsoleFadeStateForTests();

[[nodiscard]] bool IsConsoleVisible();
void SetConsoleVisible(bool visible);

int Console_CloseWindowCommand();

int Console_RepeatCommand(std::string_view args);

int Console_PeriodicUpdate(const float *frame_delta_seconds);

int Console_SpacingCommand(std::string_view raw_args);

[[nodiscard]] float GetConsoleCharacterSpacingPixels();

[[nodiscard]] int GetConsoleToggleKeyCode();
void SetConsoleToggleKeyCode(int key_code);

[[nodiscard]] bool IsConsoleInputRoutingInitialized();
void SetConsoleInputRoutingInitialized(bool initialized);

int Console_ShouldPassKeyRelease(int key_code);

[[nodiscard]] int ResolveConsoleToggleKeyCode(std::string_view key_name,
                                              int fallback_key_code =
                                                  kDefaultConsoleToggleKeyCode);

void ConsoleAndFont_StartupInitialize();

void Console_UnregisterEventHandlers();

void ConsoleAndFont_Shutdown();

void ConsoleClient_RequestShutdown();

void Console_RegisterBasicCommands();

using ConsoleSetMapCommandHandler = std::function<void(std::string_view)>;
void SetConsoleSetMapCommandHandler(ConsoleSetMapCommandHandler handler);
void ResetConsoleSetMapCommandHandlerForTests();

int ConsoleClient_HandleKeyEvent(const unsigned int *event_data);

[[nodiscard]] bool Console_TabCompleteNext(std::string_view prefix,
                                           std::string &cursor,
                                           bool reverse);

int Console_Help(const std::string &topic);

int Console_InputColorCommand(const std::string &args);

int Console_DefaultCommand();

int Console_SettingsCommand();

int Console_BgColorCommand(const std::string &cmd_name, const std::string &args);

int Console_HighlightColorCommand(const std::string &cmd_name,
                                  const std::string &args);

int Console_FontSizeCommand(std::string_view raw_args);

int Console_FontCommand(std::string_view font_name);

int Console_ConsoleLinesCommand(const std::string &cmd_name,
                                const std::string &args);

int Console_ClearCommand();

int Console_ProportionalTextCommand();

int Console_AppendLogToFileCommand();

}

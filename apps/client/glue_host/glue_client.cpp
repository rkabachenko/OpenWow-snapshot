#include "glue_client.h"
#include "glue_host/presentation_settings.h"
#include "glue_host/settings_capability_policy.h"
#include "scenarios/offline_world_fixture.h"

#include "debug_ui_control_adapter.h"
#include "openwow/audio/playback/audio_engine.h"
#include "openwow/audio/playback/sound_engine.h"
#include "openwow/audio/playback/sound_interface.h"
#include "openwow/audio/resources/sound_entry_resolver.h"
#include "openwow/core/client_init.h"
#include "openwow/core/client_misc.h"
#include "openwow/core/cobject_heap.h"
#include "openwow/core/console.h"
#include "openwow/core/cvar.h"
#include "openwow/core/decimal_parse.h"
#include "openwow/core/gxcvar.h"
#include "openwow/core/init_subsystems.h"
#include "openwow/core/login_state_handler.h"
#include "openwow/core/screenshot_system.h"
#include "openwow/data/archive_system.h"
#include "openwow/data/async_file_read.h"
#include "openwow/data/db_cache_instances.h"
#include "openwow/data/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/data/login_resource_validator.h"
#include "openwow/data/startup_filesystem_state.h"
#include "openwow/debug/client_error_display_cvars.h"
#include "openwow/debug/control/debug_control_json_codec.h"
#include "openwow/debug/control/debug_control_server.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/game/account_msg.h"
#include "openwow/game/activities/dance/adapters/data/dbc_dance_move_catalog.h"
#include "openwow/game/activities/dance/application/dance_studio.h"
#include "openwow/game/battlefield_info.h"
#include "openwow/game/battlenet_api.h"
#include "openwow/game/c_input_control.h"
#include "openwow/game/input_control.h"
#include "openwow/game/knowledge_base.h"
#include "openwow/game/lcd_system.h"
#include "openwow/game/localization.h"
#include "openwow/game/object_effect_system.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/player_control_runtime.h"
#include "openwow/game/pvp_info.h"
#include "openwow/game/reputation_info.h"
#include "openwow/game/script_event_helpers.h"
#include "openwow/game/simple_script.h"
#include "openwow/game/spell_visual_system.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/game/update_fields.h"
#include "openwow/game/warden_module.h"
#include "openwow/net/client_services.h"
#include "openwow/net/client_services_packet_sender.h"
#include "openwow/net/realm_config_tables.h"
#include "openwow/platform/process/os_platform.h"
#include "openwow/platform/window/system_mouse_speed.h"
#include "openwow/platform/window/window_manager.h"
#include "openwow/render/backend/bgfx/renderer_context_services.h"
#include "openwow/render/m2/m2_cvar_callbacks.h"
#include "openwow/render/m2/m2_resource_streamer.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/render/platform/gamma_controller.h"
#include "openwow/render/platform/renderer_backend_selection.h"
#include "openwow/render/resources/textures/texture_cache_budget.h"
#include "openwow/render/resources/textures/texture_filtering_mode.h"
#include "openwow/render/scene/nameplate_renderer.h"
#include "openwow/render/scene/object_renderer.h"
#include "openwow/render/ui/ui_acceleration.h"
#include "openwow/runtime/scheduling/frame_scheduler.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/screens/loading_screen_manager.h"
#include "openwow/ui/framexml/framexml_parser.h"
#include "openwow/ui/game/camera_lua_bindings.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/event_dispatcher.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/runtime/frame_event_runtime.h"
#include "openwow/ui/game/runtime/frame_store.h"
#include "openwow/ui/game/runtime/retained_layout.h"
#include "openwow/ui/game/world_ui_snapshot.h"
#include "openwow/ui/glue/cgluemgr.h"
#include "openwow/ui/glue/glue_font_metrics.h"
#include "openwow/ui/glue/glue_frame_tree_dump.h"
#include "openwow/ui/glue/glue_script_events.h"
#include "openwow/ui/glue/glue_toc_loader.h"
#include "openwow/ui/glue/glue_xml_bootstrap.h"
#include "openwow/ui/glue/interleaved_toc_processor.h"
#include "openwow/ui/glue/legal_notice_sync.h"
#include "openwow/ui/widgets/simple_edit_box.h"
#include "openwow/ui/widgets/simple_frame.h"
#include "openwow/ui/widgets/simple_model.h"
#include "openwow/vfs/sfile_core.h"
#include "openwow/world/environment/weather.h"
#include "openwow/world/world_render_pipeline.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <lua.hpp>

namespace openwow::client {

namespace {

constexpr float kGlueFrameDepthBaseline = 1.0F;
constexpr auto kDebugScreenshotTimeout = std::chrono::seconds(10);
constexpr auto kDebugScreenshotPollInterval = std::chrono::milliseconds(10);

using openwow::debug::control::CapabilityResult;
using openwow::debug::control::DebugControlError;
using openwow::debug::control::InputSubmissionResult;

std::uint32_t DebugInputWindowId(SDL_Window *const window, const std::uint32_t requested_id) {
  return requested_id != 0 || window == nullptr ? requested_id : SDL_GetWindowID(window);
}

std::uint8_t ToSdlWindowEvent(const openwow::debug::control::WindowEventKind event) {
  using Kind = openwow::debug::control::WindowEventKind;
  switch (event) {
  case Kind::kShown:
    return SDL_WINDOWEVENT_SHOWN;
  case Kind::kHidden:
    return SDL_WINDOWEVENT_HIDDEN;
  case Kind::kExposed:
    return SDL_WINDOWEVENT_EXPOSED;
  case Kind::kMoved:
    return SDL_WINDOWEVENT_MOVED;
  case Kind::kResized:
    return SDL_WINDOWEVENT_RESIZED;
  case Kind::kSizeChanged:
    return SDL_WINDOWEVENT_SIZE_CHANGED;
  case Kind::kMinimized:
    return SDL_WINDOWEVENT_MINIMIZED;
  case Kind::kMaximized:
    return SDL_WINDOWEVENT_MAXIMIZED;
  case Kind::kRestored:
    return SDL_WINDOWEVENT_RESTORED;
  case Kind::kMouseEnter:
    return SDL_WINDOWEVENT_ENTER;
  case Kind::kMouseLeave:
    return SDL_WINDOWEVENT_LEAVE;
  case Kind::kFocusGained:
    return SDL_WINDOWEVENT_FOCUS_GAINED;
  case Kind::kFocusLost:
    return SDL_WINDOWEVENT_FOCUS_LOST;
  case Kind::kClose:
    return SDL_WINDOWEVENT_CLOSE;
  case Kind::kTakeFocus:
    return SDL_WINDOWEVENT_TAKE_FOCUS;
  case Kind::kHitTest:
    return SDL_WINDOWEVENT_HIT_TEST;
  }
  return SDL_WINDOWEVENT_NONE;
}

CapabilityResult<InputSubmissionResult>
SubmitDebugInput(SDL_Window *const window, const openwow::debug::control::InputEvent &input) {
  SDL_Event event{};
  const auto error = std::visit(
      [&event, window](const auto &value) -> std::optional<DebugControlError> {
        using Input = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Input, openwow::debug::control::MouseMotionInput>) {
          event.type = SDL_MOUSEMOTION;
          event.motion.timestamp = value.timestamp_ms;
          event.motion.windowID = DebugInputWindowId(window, value.window_id);
          event.motion.which = value.device_id;
          event.motion.state = value.button_mask;
          event.motion.x = value.x_pixels;
          event.motion.y = value.y_pixels;
          event.motion.xrel = value.relative_x_pixels;
          event.motion.yrel = value.relative_y_pixels;
        } else if constexpr (std::is_same_v<Input, openwow::debug::control::MouseButtonInput>) {
          event.type = value.pressed ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
          event.button.timestamp = value.timestamp_ms;
          event.button.windowID = DebugInputWindowId(window, value.window_id);
          event.button.which = value.device_id;
          event.button.button = value.button;
          event.button.state = value.pressed ? SDL_PRESSED : SDL_RELEASED;
          event.button.clicks = value.click_count;
          event.button.x = value.x_pixels;
          event.button.y = value.y_pixels;
        } else if constexpr (std::is_same_v<Input, openwow::debug::control::MouseWheelInput>) {
          event.type = SDL_MOUSEWHEEL;
          event.wheel.timestamp = value.timestamp_ms;
          event.wheel.windowID = DebugInputWindowId(window, value.window_id);
          event.wheel.which = value.device_id;
          event.wheel.x = value.scroll_x_lines;
          event.wheel.y = value.scroll_y_lines;
          event.wheel.preciseX = value.precise_scroll_x_lines;
          event.wheel.preciseY = value.precise_scroll_y_lines;
          event.wheel.direction =
              value.direction_flipped ? SDL_MOUSEWHEEL_FLIPPED : SDL_MOUSEWHEEL_NORMAL;
        } else if constexpr (std::is_same_v<Input, openwow::debug::control::KeyboardInput>) {
          event.type = value.pressed ? SDL_KEYDOWN : SDL_KEYUP;
          event.key.timestamp = value.timestamp_ms;
          event.key.windowID = DebugInputWindowId(window, value.window_id);
          event.key.state = value.pressed ? SDL_PRESSED : SDL_RELEASED;
          event.key.repeat = value.repeat ? 1 : 0;
          event.key.keysym.scancode = static_cast<SDL_Scancode>(value.scancode);
          event.key.keysym.sym = static_cast<SDL_Keycode>(value.keycode);
          event.key.keysym.mod = static_cast<SDL_Keymod>(value.modifiers);
        } else if constexpr (std::is_same_v<Input, openwow::debug::control::TextInput>) {
          if (value.utf8_text.size() >= sizeof(event.text.text) ||
              value.utf8_text.find('\0') != std::string::npos) {
            return DebugControlError{"invalid_input",
                                     "text input does not fit SDL_TextInputEvent::text", false};
          }
          event.type = SDL_TEXTINPUT;
          event.text.timestamp = value.timestamp_ms;
          event.text.windowID = DebugInputWindowId(window, value.window_id);
          std::memcpy(event.text.text, value.utf8_text.data(), value.utf8_text.size());
          event.text.text[value.utf8_text.size()] = '\0';
        } else if constexpr (std::is_same_v<Input, openwow::debug::control::WindowInput>) {
          event.type = SDL_WINDOWEVENT;
          event.window.timestamp = value.timestamp_ms;
          event.window.windowID = DebugInputWindowId(window, value.window_id);
          event.window.event = ToSdlWindowEvent(value.event);
          event.window.data1 = value.data1;
          event.window.data2 = value.data2;
        }
        return std::nullopt;
      },
      input);
  if (error.has_value()) {
    return *error;
  }
  return InputSubmissionResult{.accepted = SDL_PushEvent(&event) == 1};
}

std::string ScreenshotMediaType(const std::filesystem::path &path) {
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  if (extension == ".jpg" || extension == ".jpeg")
    return "image/jpeg";
  if (extension == ".png")
    return "image/png";
  if (extension == ".tga")
    return "image/x-tga";
  return "application/octet-stream";
}

openwow::render::TextureCacheBudgetContext
TextureCacheBudgetContext(const openwow::ui::game::CVarSystem &cvars) {
  return {
      .physical_memory_bytes = openwow::platform::OS_GetPhysicalMemory(),
      .backend =
          openwow::render::TextureCacheBackendForRenderer(openwow::render::ResolveRendererBackend(
              openwow::render::ParseRendererBackend(cvars.GetCVar("gxApi")))),
  };
}

void RegisterTextureCacheBudget(openwow::ui::game::CVarSystem &cvars) {
  cvars.SetValidationCallback("textureCacheSize", [&cvars](const std::string &, const std::string &,
                                                           const std::string &value) {
    const auto result = openwow::render::ValidateTextureCacheSizeChange(
        openwow::core::ParseSignedDecimalLikeSub76F0D0(value), TextureCacheBudgetContext(cvars));
    openwow::core::ida::ConsoleAddLine(result.console_message, openwow::core::ida::COLOR_DEFAULT);
    return result.accepted;
  });
}

struct LogoutCountdownSnapshot {
  bool visible{false};
  float seconds{0.0f};
};

LogoutCountdownSnapshot ReadStockLogoutCountdown(openwow::ui::game::GameUIManager &manager) {
  lua_State *const state = manager.lua_state();
  if (state == nullptr) {
    return {};
  }

  for (int index = 1; index <= 4; ++index) {
    const std::string frame_name = "StaticPopup" + std::to_string(index);
    const auto *const frame = manager.frame_store().FindFrame(frame_name);
    if (frame == nullptr || !frame->visible) {
      continue;
    }

    lua_getglobal(state, frame_name.c_str());
    if (!lua_istable(state, -1)) {
      lua_pop(state, 1);
      continue;
    }

    lua_getfield(state, -1, "which");
    const char *const which = lua_tostring(state, -1);
    const bool is_logout_popup =
        which != nullptr && (std::strcmp(which, "CAMP") == 0 || std::strcmp(which, "QUIT") == 0);
    lua_pop(state, 1);

    lua_getfield(state, -1, "timeleft");
    const float seconds =
        lua_isnumber(state, -1) ? static_cast<float>(lua_tonumber(state, -1)) : 0.0f;
    lua_pop(state, 2);

    if (is_logout_popup && seconds > 0.0f && seconds <= 20.0f) {
      return {.visible = true, .seconds = seconds};
    }
  }
  return {};
}

void AppendScenarioU8(std::vector<std::uint8_t> &bytes, const std::uint8_t value) {
  bytes.push_back(value);
}

void AppendScenarioU16(std::vector<std::uint8_t> &bytes, const std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8u));
}

void AppendScenarioU32(std::vector<std::uint8_t> &bytes, const std::uint32_t value) {
  for (std::uint32_t shift = 0; shift < 32u; shift += 8u) {
    bytes.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

void AppendScenarioFloat(std::vector<std::uint8_t> &bytes, const float value) {
  std::uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(bits));
  AppendScenarioU32(bytes, bits);
}

std::uint32_t ScenarioFloatBits(const float value) {
  std::uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

void AppendScenarioPackedGuid(std::vector<std::uint8_t> &bytes, const std::uint64_t guid) {
  std::uint8_t mask = 0;
  std::array<std::uint8_t, 8> packed{};
  std::size_t packed_count = 0;
  for (std::size_t index = 0; index < packed.size(); ++index) {
    const auto byte = static_cast<std::uint8_t>(guid >> (index * 8u));
    if (byte == 0u) {
      continue;
    }
    mask |= static_cast<std::uint8_t>(1u << index);
    packed[packed_count++] = byte;
  }
  AppendScenarioU8(bytes, mask);
  bytes.insert(bytes.end(), packed.begin(), packed.begin() + packed_count);
}

void AppendScenarioUpdateFields(std::vector<std::uint8_t> &bytes,
                                const std::vector<std::pair<std::uint16_t, std::uint32_t>> &fields,
                                const std::uint16_t field_count) {
  const auto block_count = openwow::game::BitmaskBlockCount(field_count);
  std::vector<std::uint32_t> mask(block_count, 0u);
  std::vector<std::pair<std::uint16_t, std::uint32_t>> ordered = fields;
  std::sort(ordered.begin(), ordered.end(),
            [](const auto &lhs, const auto &rhs) { return lhs.first < rhs.first; });
  ordered.erase(
      std::unique(ordered.begin(), ordered.end(),
                  [](const auto &lhs, const auto &rhs) { return lhs.first == rhs.first; }),
      ordered.end());
  for (const auto &[index, value] : ordered) {
    (void)value;
    if (index < field_count) {
      mask[index / 32u] |= 1u << (index % 32u);
    }
  }

  AppendScenarioU8(bytes, block_count);
  for (const auto block : mask) {
    AppendScenarioU32(bytes, block);
  }
  for (const auto &[index, value] : ordered) {
    if (index < field_count) {
      AppendScenarioU32(bytes, value);
    }
  }
}

std::vector<std::uint8_t> BuildOfflineScenarioPlayerCreate(const openwow::game::ObjectGuid guid,
                                                           const float x, const float y,
                                                           const float z, const float orientation) {
  using namespace openwow::game;

  std::vector<std::uint8_t> bytes;
  bytes.reserve(320u);
  AppendScenarioU32(bytes, 1u);
  AppendScenarioU8(bytes, 3u);
  AppendScenarioPackedGuid(bytes, guid.GetRawValue());
  AppendScenarioU8(bytes, static_cast<std::uint8_t>(TypeID::kPlayer));

  AppendScenarioU16(bytes, kUpdateFlagLiving | kUpdateFlagSelf);
  AppendScenarioU32(bytes, 0u);
  AppendScenarioU16(bytes, 0u);
  AppendScenarioU32(bytes, 0u);
  AppendScenarioFloat(bytes, x);
  AppendScenarioFloat(bytes, y);
  AppendScenarioFloat(bytes, z);
  AppendScenarioFloat(bytes, orientation);
  AppendScenarioU32(bytes, 0u);
  for (const float speed : {2.5F, 7.0F, 4.5F, 4.722222F, 2.5F, 7.0F, 4.5F, 3.141594F, 3.14F}) {
    AppendScenarioFloat(bytes, speed);
  }

  constexpr std::uint32_t kHumanWarriorMale = 1u | (1u << 8u);
  const std::vector<std::pair<std::uint16_t, std::uint32_t>> fields = {
      {OBJECT_FIELD_GUID, static_cast<std::uint32_t>(guid.GetRawValue())},
      {OBJECT_FIELD_GUID + 1u, static_cast<std::uint32_t>(guid.GetRawValue() >> 32u)},
      {OBJECT_FIELD_TYPE, TypeMaskFor(TypeID::kPlayer)},
      {OBJECT_FIELD_SCALE_X, ScenarioFloatBits(1.0F)},
      {UNIT_FIELD_BYTES_0, kHumanWarriorMale},
      {UNIT_FIELD_HEALTH, 100u},
      {UNIT_FIELD_POWER1, 100u},
      {UNIT_FIELD_MAXHEALTH, 100u},
      {UNIT_FIELD_MAXPOWER1, 100u},
      {UNIT_FIELD_LEVEL, 1u},
      {UNIT_FIELD_FACTIONTEMPLATE, 1u},
      {UNIT_FIELD_BOUNDINGRADIUS, ScenarioFloatBits(0.306F)},
      {UNIT_FIELD_COMBATREACH, ScenarioFloatBits(1.5F)},
      {UNIT_FIELD_DISPLAYID, 49u},
      {UNIT_FIELD_NATIVEDISPLAYID, 49u},
      {PLAYER_FLAGS, 0u},
      {PLAYER_BYTES, 0u},

      {PLAYER_BYTES_2, 2u << 24u},
      {PLAYER_BYTES_3, 0u},
  };
  AppendScenarioUpdateFields(bytes, fields, PLAYER_END);
  return bytes;
}

void SyncConvertedTrialCVarToDataPreload() {
  auto &cvar_sys = openwow::ui::game::CVarSystem::Instance();
  openwow::core::SetConvertedTrialFlag(cvar_sys.GetCVarBool("converted"));
}

void EnsureConvertedTrialCVarCallbackRegistered(openwow::ui::game::CVarSystem &cvar_sys) {

  if (!cvar_sys.Exists("converted")) {
    return;
  }
  static std::once_flag once;
  std::call_once(once, [&cvar_sys]() {
    cvar_sys.AddCallback("converted", [](const std::string &, const std::string &) {
      SyncConvertedTrialCVarToDataPreload();
    });
  });
}

std::optional<std::uint8_t>
ResolveFrameGraphView(const openwow::render::api::RendererContext *renderer_context,
                      const openwow::render::api::FrameGraphPassId pass_id) {
  if (renderer_context == nullptr) {
    return std::nullopt;
  }

  const auto *pass = renderer_context->Graph().FindPass(pass_id);
  if (pass == nullptr || pass->view_id > std::numeric_limits<std::uint8_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::uint8_t>(pass->view_id);
}

std::optional<std::uint8_t>
BuildClientBackbufferFrame(openwow::render::api::RendererContext *renderer_context, const int width,
                           const int height) {
  if (renderer_context == nullptr) {
    return std::nullopt;
  }

  auto &graph = renderer_context->Graph();
  const openwow::render::api::RenderExtent extent{
      static_cast<std::uint32_t>(std::max(1, width)),
      static_cast<std::uint32_t>(std::max(1, height)),
  };
  graph.Reset();
  const auto clear = graph.AddPass(openwow::render::api::FrameGraphPassId::SceneOpaque, extent);
  graph.AddPass(openwow::render::api::FrameGraphPassId::DebugOverlay, extent);
  graph.AddPass(openwow::render::api::FrameGraphPassId::Present, extent);

  if (clear.view_id > std::numeric_limits<std::uint8_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::uint8_t>(clear.view_id);
}

std::optional<std::uint8_t>
ResolveBootstrapFpsOverlayView(const openwow::render::api::RendererContext *renderer_context) {
  if (auto view = ResolveFrameGraphView(renderer_context,
                                        openwow::render::api::FrameGraphPassId::DebugOverlay)) {
    return view;
  }
  return ResolveFrameGraphView(renderer_context, openwow::render::api::FrameGraphPassId::Present);
}

void RenderBootstrapFpsOverlay(openwow::render::ui::TextRenderer &text_renderer,
                               std::uint8_t view_id, float screen_w, float screen_h) {
  auto &overlay_state = openwow::core::GetRenderBootstrapFpsOverlayState();
  if (!overlay_state.initialized) {
    return;
  }

  const int pixel_height = std::max(1, static_cast<int>(std::lround(overlay_state.font_height)));
  if (!text_renderer.is_ready() && !text_renderer.Init(pixel_height)) {
    return;
  }

  text_renderer.BeginFrame(view_id, screen_w, screen_h);
  const auto paint = openwow::core::RenderBootstrap_FpsOverlayPaint(0.0f, 1.0f);
  for (std::size_t i = 0; i < paint.count; ++i) {
    const auto &line = paint.lines[i];
    const float draw_x = line.x * screen_w;
    const float draw_y = (1.0f - line.y) * screen_h;
    const std::uint32_t color_argb = (static_cast<std::uint32_t>(line.alpha) << 24) |
                                     (static_cast<std::uint32_t>(line.red) << 16) |
                                     (static_cast<std::uint32_t>(line.green) << 8) |
                                     static_cast<std::uint32_t>(line.blue);
    text_renderer.DrawText(view_id, draw_x, draw_y, std::string(line.text), color_argb);
  }
}

void PrepareGlueRuntimeForEnterWorld(openwow::ui::glue::GlueLuaRuntime &glue_runtime) {
  glue_runtime.ShutdownForWorld();
}

std::pair<std::string, std::uint16_t> ParseRealmListEndpoint(const std::string &realm_list) {
  std::string host = realm_list.empty() ? "127.0.0.1" : realm_list;
  std::uint16_t port = 3724;

  const auto colon = host.rfind(':');
  if (colon != std::string::npos && colon + 1 < host.size()) {
    const std::string port_text = host.substr(colon + 1);
    char *end = nullptr;
    const long parsed = std::strtol(port_text.c_str(), &end, 10);
    if (end != port_text.c_str() && *end == '\0' && parsed > 0 &&
        parsed <= std::numeric_limits<std::uint16_t>::max()) {
      port = static_cast<std::uint16_t>(parsed);
      host.resize(colon);
    }
  }

  if (host.empty()) {
    host = "127.0.0.1";
  }
  return {host, port};
}

}

GlueClient::GlueClient(Options opts)
    : opts_(std::move(opts)), window_(opts_.window),
      launch_context_(std::move(opts_.launch_context)), auth_host_("127.0.0.1"), auth_port_(3724),
      startup_trace_(opts_.startup_trace), lua_trace_(opts_.lua_trace),
      ui_frame_tree_dump_path_(std::move(opts_.ui_frame_tree_dump_path)),
      render_submit_trace_(opts_.render_submit_trace),
      render_submit_trace_path_(std::move(opts_.render_submit_trace_path)),
      glue_renderer_(&login_vfs_, texture_manager_, m2_system_,
                     [this](const std::uint32_t sound_kit_id) {
                       (void)sound_runtime_.PlaySoundKit(sound_kit_id, nullptr, nullptr);
                     }),
      glue_runtime_(display_settings_, sound_runtime_),
      glue_host_(&login_vfs_, sound_runtime_),
      glue_random_(openwow::core::GetClientStartupAdlerSeedState()),
      realm_runtime_(),
      game_loop_(display_settings_, texture_manager_, m2_system_, sound_runtime_),
      character_world_runtime_(db_cache_runtime_, realm_runtime_, game_loop_.item_definitions(),
                               m2_system_, dbc_loader_, openwow::game::SpellbookSystem::Get(),
                               openwow::game::PvPInfo::Get(), openwow::game::ReputationInfo::Get(),
                               sound_runtime_) {
  window_focused_ =
      window_ != nullptr && (SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) != 0;
  realm_runtime_.session.SetClientCacheVersionCallback(
      [this](const std::uint32_t version) { ApplyClientCacheVersion(version); });
  game_state_.customization_random = &glue_random_;
  glue_widgets_.SetFocusOwnerChangedCallback([this]() { UpdateTextInputState(); });

  glue_widgets_.BindTextureNaturalSizeSource(&glue_renderer_.texture_natural_size_source());
}

GlueClient::~GlueClient() {

  m2_system_.BindFrameJobSystem(nullptr);
  frame_job_system_.Shutdown();
  if (auto *session = character_world_runtime_.session(); session != nullptr) {
    session->dance_studio().UnbindDanceMoveCatalog();
  }
  openwow::game::SetSaveCursorPosCallback(nullptr);
  openwow::game::BattleNetApi::Instance().SetEventSink({});
  gamma_controller_.Shutdown(openwow::ui::game::CVarSystem::Instance());
  openwow::data::BindErrorTableVfs(nullptr);
}

void GlueClient::ApplyClientCacheVersion(const std::uint32_t version) {
  const auto changes = db_cache_runtime_.ApplyClientVersion(version);
  if (auto *session = character_world_runtime_.session(); session != nullptr && changes.Any()) {
    session->InvalidateDecodedCaches(version, changes);
  }
  openwow::game::WardenModuleCache_ApplyClientCacheVersion(version);
}

openwow::ui::glue::GlueLuaValue GlueClient::MakeLuaString(std::string value) const {
  openwow::ui::glue::GlueLuaValue v;
  v.kind = openwow::ui::glue::GlueLuaValue::Kind::kString;
  v.string_value = std::move(value);
  return v;
}

openwow::ui::glue::GlueLuaValue GlueClient::MakeLuaNumber(double value) const {
  openwow::ui::glue::GlueLuaValue v;
  v.kind = openwow::ui::glue::GlueLuaValue::Kind::kNumber;
  v.number_value = value;
  return v;
}

openwow::ui::glue::GlueLuaValue GlueClient::MakeLuaBool(bool value) const {
  openwow::ui::glue::GlueLuaValue v;
  v.kind = openwow::ui::glue::GlueLuaValue::Kind::kBoolean;
  v.bool_value = value;
  return v;
}

openwow::ui::LuaRunResult
GlueClient::DispatchWidgetEvent(const std::string &widget_name, const std::string &event_name,
                                const std::string &event_source,
                                const std::vector<openwow::ui::glue::GlueLuaValue> &args) {

  if (!glue_load_.ok || !glue_runtime_.IsAttachedToGlue()) {
    return openwow::ui::LuaRunResult{.ok = true, .error = ""};
  }
  const auto res = glue_runtime_.RunWidgetEvent(widget_name, event_name, event_source, args);
  if (!res.ok) {
    const std::string key = event_source + ":" + event_name + "|" + res.error;
    if (logged_lua_failures_.insert(key).second) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                                "Glue Lua dispatch failed: ctx=" + event_source + ":" + event_name +
                                    " err=" + res.error);
    }
  }
  return res;
}

openwow::ui::LuaRunResult GlueClient::DispatchButtonClick(const std::string &widget_name,
                                                          const std::string &button_name,
                                                          const bool is_down) {
  if (!button_clicks_in_progress_.insert(widget_name).second) {
    return {.ok = true, .error = ""};
  }

  const auto widget = glue_widgets_.GetWidget(widget_name);
  if (!widget.has_value() || !widget->enabled ||
      openwow::text::EqualsIgnoreCaseAscii(glue_widgets_.GetButtonState(widget_name), "DISABLED")) {
    button_clicks_in_progress_.erase(widget_name);
    return {.ok = true, .error = ""};
  }

  if (!is_down && openwow::text::EqualsIgnoreCaseAscii(widget->kind, "CheckButton")) {
    glue_widgets_.SetChecked(widget_name, !glue_widgets_.Checked(widget_name));
  }

  const std::vector<openwow::ui::glue::GlueLuaValue> args{MakeLuaString(button_name),
                                                          MakeLuaBool(is_down)};
  if (glue_runtime_.HasWidgetScript(widget_name, "PreClick")) {
    (void)DispatchWidgetEvent(widget_name, "PreClick", widget_name + ".PreClick", args);
  }
  openwow::ui::LuaRunResult result{.ok = true, .error = ""};
  if (glue_runtime_.HasWidgetScript(widget_name, "OnClick")) {
    result = DispatchWidgetEvent(widget_name, "OnClick", widget_name + ".OnClick", args);
  }
  if (glue_runtime_.HasWidgetScript(widget_name, "PostClick")) {
    (void)DispatchWidgetEvent(widget_name, "PostClick", widget_name + ".PostClick", args);
  }

  button_clicks_in_progress_.erase(widget_name);
  return result;
}

void GlueClient::FireGlueEvent(const std::string &event_name,
                               const std::vector<openwow::ui::glue::GlueLuaValue> &args) {
  if (!glue_load_.ok) {
    return;
  }
  auto targets = glue_runtime_.RegisteredWidgetsForEvent(event_name);
  if (targets.empty()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kTrace,
                              "Glue event has no listeners: " + event_name);
    return;
  }
  for (const auto &widget : targets) {
    std::vector<openwow::ui::glue::GlueLuaValue> call_args;
    call_args.reserve(args.size() + 1);
    call_args.push_back(MakeLuaString(event_name));
    for (const auto &arg : args) {
      call_args.push_back(arg);
    }
    const auto res =
        glue_runtime_.RunWidgetEvent(widget, "OnEvent", widget + ".OnEvent", call_args);
    if (!res.ok) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                                "Glue OnEvent dispatch failed: widget=" + widget +
                                    " event=" + event_name + " err=" + res.error);
    }
  }
}

void GlueClient::RunWidgetOnLoadPass() {
  const auto names = glue_widgets_.WidgetNamesInSourceOrder();
  for (const auto &widget_name : names) {
    (void)DispatchWidgetEvent(widget_name, "OnLoad", widget_name + ".OnLoad", {});
  }
}

void GlueClient::DrainScreenshotNotifications() {
  auto notifications = openwow::core::ScreenshotSystem::Instance().DrainCompletedRequests();
  for (const auto &notification : notifications) {
    switch (notification.domain) {
    case openwow::core::ScreenshotRequestDomain::GlueUi: {
      const auto event = notification.succeeded
                             ? openwow::ui::glue::GlueEventName(
                                   openwow::ui::glue::GlueScriptEvent::GlueScreenshotSucceeded)
                             : openwow::ui::glue::GlueEventName(
                                   openwow::ui::glue::GlueScriptEvent::GlueScreenshotFailed);
      if (event != nullptr) {
        FireGlueEvent(event, {});
      }
      break;
    }
    case openwow::core::ScreenshotRequestDomain::GameUi: {
      if (auto *ui = openwow::ui::game::runtime::WorldUiRuntimeContext::FromActiveLua();
          ui != nullptr) {
        ui->frame_events().dispatcher().FireEvent(
            notification.succeeded ? openwow::ui::game::events::SCREENSHOT_SUCCEEDED
                                   : openwow::ui::game::events::SCREENSHOT_FAILED);
      }
      break;
    }
    case openwow::core::ScreenshotRequestDomain::None:
    default:
      break;
    }
  }
}

std::string GlueClient::FocusedEditbox() const {
  const auto &focused = glue_widgets_.focused_widget();
  if (focused.empty()) {
    return {};
  }
  const auto w = glue_widgets_.GetWidget(focused);
  if (!w.has_value()) {
    return {};
  }
  if (ToLowerAscii(w->kind) != "editbox") {
    return {};
  }
  return focused;
}

void GlueClient::UpdateTextInputState() {

  const bool in_world_editing = mode_ == UiMode::kInWorld &&
                                game_loop_.game_ui().is_initialized() &&
                                !game_loop_.game_ui().input_router().focused_frame_name().empty();
  const bool glue_accepts_text = mode_ != UiMode::kLoading && mode_ != UiMode::kInWorld;
  const bool should_capture =
      window_focused_ && (in_world_editing || (glue_accepts_text && !FocusedEditbox().empty()));
  bool platform_is_capturing = SDL_IsTextInputActive() == SDL_TRUE;
  if (should_capture && text_input_reactivation_pending_ &&
      platform_is_capturing) {

    SDL_StopTextInput();
    platform_is_capturing = false;
  }
  if (should_capture && !platform_is_capturing) {
    SDL_StartTextInput();
    text_input_reactivation_pending_ = false;
  } else if (!should_capture && platform_is_capturing) {
    SDL_StopTextInput();
  }
  text_input_active_ = SDL_IsTextInputActive() == SDL_TRUE;
}

void GlueClient::UpdateFocusedEditBoxInputLanguage() {
  const auto focused = FocusedEditbox();
  if (focused.empty()) {
    return;
  }

  const std::string next_token = openwow::ui::widgets::EditBoxInputLanguageToken(
      openwow::ui::widgets::DetectActiveEditBoxInputLanguage());
  if (glue_widgets_.GetEditInputLanguageToken(focused) == next_token) {
    return;
  }

  glue_widgets_.SetEditInputLanguageToken(focused, next_token);
  (void)DispatchWidgetEvent(focused, "OnInputLanguageChanged", focused + ".OnInputLanguageChanged",
                            {MakeLuaString(next_token)});
}

bool GlueClient::IsUsernameEditbox(const std::string &name) {
  return name == "AccountLoginAccountEdit" || name == "AccountNameEditBox" ||
         name == "AccountEditBox";
}

bool GlueClient::IsPasswordEditbox(const std::string &name) {
  return name == "AccountLoginPasswordEdit" || name == "AccountPasswordEditBox" ||
         name == "PasswordEditBox";
}

std::string GlueClient::FindUsernameWidget() const {
  if (const auto w = FindFirstVisibleWidget(
          glue_widgets_, {"AccountLoginAccountEdit", "AccountNameEditBox", "AccountEditBox"});
      w.has_value()) {
    return w->name;
  }
  return "AccountLoginAccountEdit";
}

std::string GlueClient::FindPasswordWidget() const {
  if (const auto w = FindFirstVisibleWidget(
          glue_widgets_, {"AccountLoginPasswordEdit", "AccountPasswordEditBox", "PasswordEditBox"});
      w.has_value()) {
    return w->name;
  }
  return "AccountLoginPasswordEdit";
}

void GlueClient::SetMode(UiMode next_mode) {
  const bool mode_changed = mode_ != next_mode;
  if (mode_ == UiMode::kInWorld && next_mode != UiMode::kInWorld) {
    ReleaseInWorldMouseButtons();
  }

  mode_ = next_mode;
  if (mode_changed) {
    glue_runtime_.ClearHoveredWidget();
    last_glue_cursor_.reset();
    if (!pressed_widget_name_.empty()) {
      glue_widgets_.SetButtonState(pressed_widget_name_, "NORMAL");
      pressed_widget_name_.clear();
    }
    mouse_capture_widget_name_.clear();
    dragging_slider_name_.clear();
  }
  UpdateTextInputState();

  UpdateWindowTitle();

  layout_dirty_ = true;
}

void GlueClient::SyncGlueViewportFromWindow() {
  int width = 0;
  int height = 0;
  GetDrawableSize(window_, &width, &height);
  glue_widgets_.SetViewport(width, height);
}

void GlueClient::RefreshLayout() {
  int width = 0;
  int height = 0;
  GetDrawableSize(window_, &width, &height);
  if (width == layout_width_ && height == layout_height_) {
    return;
  }
  layout_width_ = width;
  layout_height_ = height;

  glue_widgets_.SetViewport(width, height);

  const int drawable_w = std::max(1, width);
  const int drawable_h = std::max(1, height);
  if (renderer_context_ != nullptr) {
    renderer_context_->Resize({static_cast<std::uint32_t>(drawable_w),
                               static_cast<std::uint32_t>(drawable_h)});
  }

  game_loop_.SetScreenSize(drawable_w, drawable_h);

  FireGlueEvent("DISPLAY_SIZE_CHANGED", {});
}

void GlueClient::DispatchPendingScrollRangeChangedEvents() {
  for (const auto &event : glue_widgets_.ConsumeScrollRangeChangedEvents()) {
    (void)DispatchWidgetEvent(
        event.widget_name, "OnScrollRangeChanged", event.widget_name + ".OnScrollRangeChanged",
        {MakeLuaNumber(event.horizontal_range), MakeLuaNumber(event.vertical_range)});
  }
}

void GlueClient::RefreshLoginConfiguration() {
  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  const std::string detected_locale = openwow::data::DetectLocaleRing(
      cvars.GetCVar("locale"), launch_context_.game_root.string(),
      openwow::data::GetStartupFileSystemState().retail_install_path_cache);
  openwow::data::DefaultLoadLoginConfigs(1, detected_locale.c_str());

  const auto [host, port] = ParseRealmListEndpoint(cvars.GetCVar("realmList"));
  auth_host_ = host;
  auth_port_ = port;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                            "Auth endpoint: " + auth_host_ + ":" + std::to_string(auth_port_));
}

void GlueClient::DoLoginAttempt() {
  (void)glue_runtime_.SetEditBoxTextProgrammatically("AccountLoginAccountEdit",
                                                     login_screen_.username());
  (void)glue_runtime_.SetEditBoxTextProgrammatically("AccountLoginPasswordEdit",
                                                     login_screen_.password());

  GlueFlowContext cancel_ctx;
  cancel_ctx.realm_session = &realm_runtime_.session;
  CancelGlueFlowNetworkOperations(cancel_ctx, glue_flow_state_);
  glue_flow_state_.phase = GlueFlowState::Phase::kIdle;
  glue_flow_state_.auth_protocol = std::make_shared<openwow::net::wotlk::AuthProtocol>();
  auto result = glue_flow_state_.auth_protocol->Login(
      auth_host_, auth_port_, login_screen_.username(), login_screen_.password());

  login_screen_.SecureClearPassword();
  (void)glue_runtime_.SetEditBoxTextProgrammatically("AccountLoginPasswordEdit", "");
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                            "Login attempt result: " + result.message);
  scene_state_.show_error = (result.status != openwow::net::wotlk::AuthStatus::kSuccess);
  scene_state_.status_line = result.message;

  if (result.status == openwow::net::wotlk::AuthStatus::kSuccess) {
    auth_session_token_ = result.session_token;
    if (!result.realms.empty()) {
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kInfo,
          "Realm list from auth server: " + std::to_string(result.realms.size()) + " realm(s)");
      realm_screen_.SetRealms(result.realms);
      game_state_.realms = result.realms;
      game_state_.ResetRealmListCategoryState();
      show_error_ = false;
    } else {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                                "No realms from auth server");
      realm_screen_.SetRealms({});
      game_state_.realms.clear();
      game_state_.ResetRealmListCategoryState();
      show_error_ = true;
    }
    game_state_.connected = false;
    game_state_.selected_realm_index = -1;
    game_state_.selected_character_index = -1;
    game_state_.characters.clear();
    FireGlueEvent("REALM_LIST_UPDATE", {});
    FireGlueEvent("SUGGEST_REALM", {});

    const auto on_show = DispatchWidgetEvent("RealmList", "OnShow", "RealmList.OnShow", {});
    show_error_ = show_error_ || !on_show.ok;
    SetMode(UiMode::kRealmDialog);
  } else {
    glue_flow_state_.auth_protocol.reset();
    auth_session_token_.clear();
    realm_runtime_.session.Disconnect();
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                              "Login failed; staying on login screen");
  }
}

void GlueClient::BuildAndPublishLoginVfs() {
  const auto &cvars = openwow::ui::game::CVarSystem::Instance();
  login_vfs_ = openwow::data::BuildLoginVfs(launch_context_.game_root.string(),
                                            launch_context_.enhanced_assets_root.string(),
                                            cvars.GetCVar("locale"));

  RealmAddonHandshakeComposition::BindContentVfs(&login_vfs_);

  const std::uint8_t expansion_level = openwow::data::DetermineStartupExpansionLevel(login_vfs_);
  openwow::core::SetExpansionLevel(expansion_level);
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                            "Client archive expansion level: " + std::to_string(expansion_level));
}

void GlueClient::ReloadLoginResources() {

  glue_renderer_.Shutdown();
  m2_system_.ShutdownAsyncLoading();
  BuildAndPublishLoginVfs();
  RefreshLoginConfiguration();

  SyncGlueViewportFromWindow();
  glue_fonts_ = openwow::ui::glue::GlueFontRegistry::LoadFromVfs(login_vfs_);
  m2_system_.SetAsyncFileLoader(
      [vfs = &login_vfs_](const std::string &path) -> std::vector<std::uint8_t> {
        auto bytes = vfs->ReadFileBytes(path);
        return bytes.has_value() ? std::move(*bytes) : std::vector<std::uint8_t>{};
      });
  (void)glue_renderer_.Init(renderer_context_.get());
  if (glue_fonts_.has_value()) {
    glue_renderer_.BindFontRegistry(&*glue_fonts_);
  }
  glue_renderer_.SetDbcLoader(&dbc_loader_);
  glue_renderer_.SetMoviePlayer(&glue_runtime_.GetMoviePlayer());
  glue_renderer_.BindRenderSubmitTrace(render_submit_trace_, render_submit_trace_path_);
  LogVfsMounts(login_vfs_);
  validation_ = openwow::data::ValidateLoginResources(login_vfs_);
  LogVfsProbe(login_vfs_, {
                              "/Interface/GlueXML/GlueParent.xml",
                              "/Interface/GlueXML/AccountLogin.xml",
                              "/Interface/GlueXML/RealmList.xml",
                              "/Interface/GlueXML/CharacterSelect.xml",
                              "/Interface/GlueXML/GlueParent.lua",
                              "/Interface/GlueXML/GlueStrings.lua",
                          });

  auth_session_token_.clear();
  GlueFlowContext cancel_ctx;
  cancel_ctx.realm_session = &realm_runtime_.session;
  CancelGlueFlowNetworkOperations(cancel_ctx, glue_flow_state_);
  glue_flow_state_.phase = GlueFlowState::Phase::kIdle;
  realm_runtime_.session.Disconnect();

  LoadGlueTocAndScripts();
  glue_renderer_.PrewarmModels(glue_widgets_);
  glue_renderer_.PrewarmTextures(glue_widgets_);
  CompleteGlueStartupTail();

  if (glue_load_.ok && !glue_load_.error.empty()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                              "Glue Lua reload completed with warnings: " + glue_load_.error);
  }

  scene_state_.show_error = !validation_.ok;
  scene_state_.status_line = validation_.ok
                                 ? "Login resources ready"
                                 : ("Missing " + std::to_string(validation_.missing_paths.size()) +
                                    " login resource paths");

  SyncLoginEditText(&glue_runtime_, login_screen_);

  if (glue_load_.ok) {
    FireGlueEvent("FRAMES_LOADED", {});
  }
  if (glue_load_.ok) {
    (void)sound_runtime_.RefreshEnumeratedDevicesAndReconcile(
        true);
    FireGlueEvent("SOUND_DEVICE_UPDATE", {});
  }

  FireInitialScreenEventIfNeeded();
  glue_renderer_.PrewarmModels(glue_widgets_);
  glue_renderer_.PrewarmTextures(glue_widgets_);
  if (glue_load_.ok) {
    glue_runtime_.ApplyFontStringTextKeys();
  }
  if (glue_load_.ok && glue_runtime_.IsAttachedToGlue()) {
    glue_runtime_.PumpVisibilityTransitions();
  }

  RefreshLayout();
  const int width = std::max(1, layout_width_);
  const int height = std::max(1, layout_height_);

  (void)openwow::ui::glue::ResolveGlueLayoutAndFontMetrics(
      &glue_widgets_, login_vfs_, glue_fonts_.has_value() ? &*glue_fonts_ : nullptr, width, height);
  DispatchPendingScrollRangeChangedEvents();

  layout_dirty_ = true;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "Reloaded login resources");
}

void GlueClient::EnterSelectedRealm() {

  if (glue_widgets_.GetWidget("RealmListOkButton").has_value() &&
      glue_runtime_.HasWidgetScript("RealmListOkButton", "OnClick")) {
    const auto result = DispatchButtonClick("RealmListOkButton", "LeftButton", false);
    show_error_ = show_error_ || !result.ok;
    return;
  }

  const auto selected = realm_screen_.selected_realm();
  if (!selected.has_value()) {
    show_error_ = true;
    return;
  }
  selected_realm_ = selected;
  const auto hidden = glue_runtime_.RunInlineScript(
      "self:Hide()", "RealmListFallbackHide", "RealmList.__openwow_AcceptRealm", "RealmList", {});
  show_error_ = show_error_ || !hidden.ok;
  game_state_.selected_realm_index = static_cast<int>(realm_screen_.selected_index());
  openwow::ui::glue::CGlueMgr_ConnectToRealm(game_state_);
}

void GlueClient::ShowCharacterCreate(const bool dispatch_click) {
  if (dispatch_click) {
    (void)DispatchButtonClick("CharSelectCreateCharacterButton", "LeftButton", false);
  }
  create_screen_.SyncFromGameState(game_state_);
  (void)glue_runtime_.DispatchFirstAvailableWithArgs({"SetGlueScreen"}, "GlueParent",
                                                     {"charcreate"}, false);
  SetMode(UiMode::kCharacterCreate);
}

void GlueClient::EnterSelectedCharacter(const bool dispatch_click) {
  const auto selected = character_screen_.selected_character();
  if (!selected.has_value()) {
    show_error_ = true;
    return;
  }
  if (dispatch_click) {
    (void)DispatchButtonClick("CharSelectEnterWorldButton", "LeftButton", false);
  }
  if (game_state_.selected_character_index >= 0 &&
      game_state_.selected_character_index <
          static_cast<int>(character_screen_.characters().size())) {
    const int delta =
        game_state_.selected_character_index - static_cast<int>(character_screen_.selected_index());
    if (delta != 0)
      character_screen_.MoveSelection(delta);
  }
  game_state_.wants_enter_world = true;
}

bool GlueClient::EnterOfflineScenarioWorld() {
  if (!scenario_runner_.has_value() || offline_scenario_world_active_ ||
      character_world_runtime_.session() != nullptr || realm_runtime_.session.connected()) {
    return false;
  }

  std::uint32_t kMapId = 0u;
  float kX = -8949.95F;
  float kY = -132.493F;
  float kZ = 83.5312F;
  float kOrientation = 0.6283F;
  const std::string& benchmark_scene = scenario_runner_->options().benchmark_scene;
  if (benchmark_scene == "stormwind") {

    kMapId = 0u;
    kX = -8833.38F;
    kY = 628.628F;
    kZ = 94.0066F;
    kOrientation = 3.7F;
  } else if (benchmark_scene == "orgrimmar") {
    kMapId = 1u;
    kX = 1502.71F;
    kY = -4415.42F;
    kZ = 21.7237F;
    kOrientation = 0.15F;
  } else if (benchmark_scene == "ironforge") {
    kMapId = 0u;
    kX = -4918.88F;
    kY = -940.406F;
    kZ = 501.564F;
    kOrientation = 5.4F;
  }
  const auto player_guid =
      openwow::game::ObjectGuid::CreateGlobal(openwow::game::HighGuid::kPlayer, 0x0F11u);

  successful_forward_start_packets_.store(0u, std::memory_order_relaxed);
  successful_movement_heartbeat_packets_.store(0u, std::memory_order_relaxed);
  successful_movement_stop_packets_.store(0u, std::memory_order_relaxed);
  scenario_forward_binding_key_.clear();

  openwow::net::ClientServices::Instance().SetAccountName("OFFLINE");
  (void)openwow::ui::game::CVarSystem::Instance().SetCVar("realmName", "OpenWoW", true);

  auto &world_session = character_world_runtime_.CreateSession(
      [this](const std::uint32_t version) { ApplyClientCacheVersion(version); });
  if (dance_move_catalog_) {
    world_session.dance_studio().BindDanceMoveCatalog(*dance_move_catalog_);
  }
  world_session.SetPendingCharacterIdentity({
      .name = "Openwowscenario",
      .race_id = 1u,
      .class_id = 1u,
      .gender = 0u,
  });

  const auto send_world_packet = [this](const openwow::net::wotlk::WorldPacket &packet) {
    if (packet.IsOpcode(openwow::net::wotlk::Opcode::MSG_MOVE_START_FORWARD)) {
      successful_forward_start_packets_.fetch_add(1u, std::memory_order_relaxed);
    } else if (packet.IsOpcode(openwow::net::wotlk::Opcode::MSG_MOVE_HEARTBEAT)) {
      successful_movement_heartbeat_packets_.fetch_add(1u, std::memory_order_relaxed);
    } else if (packet.IsOpcode(openwow::net::wotlk::Opcode::MSG_MOVE_STOP)) {
      successful_movement_stop_packets_.fetch_add(1u, std::memory_order_relaxed);
    }
    return true;
  };
  openwow::net::SetClientServicesPacketSendFn(send_world_packet);
  world_session.SetSendFn(send_world_packet);

  std::array<std::uint8_t, 40> scenario_session_key{};
  for (std::size_t index = 0; index < scenario_session_key.size(); ++index) {
    scenario_session_key[index] =
        static_cast<std::uint8_t>(0x5Au ^ static_cast<std::uint8_t>(index));
  }
  realm_runtime_.session.SetSessionKey(scenario_session_key);
  realm_runtime_.InitWarden(scenario_session_key);
  if (!world_session.AdoptLoginVerifyWorld(
          player_guid.GetRawValue(),
          {.map_id = kMapId, .x = kX, .y = kY, .z = kZ, .orientation = kOrientation})) {
    character_world_runtime_.Destroy();
    openwow::net::SetClientServicesPacketSendFn({});
    return false;
  }

  constexpr std::uint32_t kDeterministicNoonPackedTime = 12u << 6u;
  openwow::net::wotlk::WorldPacket login_time(openwow::net::wotlk::Opcode::SMSG_LOGIN_SETTIMESPEED);
  AppendScenarioU32(login_time.payload, kDeterministicNoonPackedTime);
  AppendScenarioFloat(login_time.payload, 1.0F / 60.0F);
  AppendScenarioU32(login_time.payload, 0u);
  if (!world_session.HandlePacket(login_time)) {
    character_world_runtime_.Destroy();
    openwow::net::SetClientServicesPacketSendFn({});
    return false;
  }

  if (!world_session.HandlePacket(offline_world_fixture::BuildInitialSpells())) {
    character_world_runtime_.Destroy();
    openwow::net::SetClientServicesPacketSendFn({});
    return false;
  }

  PrepareGlueRuntimeForEnterWorld(glue_runtime_);
  openwow::ui::framexml::ClearVirtualTemplates();
  openwow::ui::glue::CGlueMgr_CleanupEnterWorldCharacterScenes(game_state_);
  openwow::ui::glue::CGlueMgr_CleanupCharCreateForEnterWorld(game_state_);

  game_loop_.SetCharacterWorldRuntime(&character_world_runtime_);
  game_loop_.SetSendPacketFn(send_world_packet);
  game_loop_.SetClientTimeFn(openwow::core::GameClock::GetTickCount32);

  (void)openwow::core::EnterWorldInit({kMapId, kX, kY, kZ}, sound_runtime_);
  openwow::game::CGUnit_C::Initialize(world_session);

  game_loop_.EnterWorld(kMapId, kX, kY, kZ, kOrientation,
                        kMapId == 1u ? "Kalimdor" : "Azeroth");

  const openwow::net::wotlk::WorldPacket player_create(
      openwow::net::wotlk::Opcode::SMSG_UPDATE_OBJECT,
      BuildOfflineScenarioPlayerCreate(player_guid, kX, kY, kZ, kOrientation));
  if (!world_session.HandlePacket(player_create) || !world_session.IsInWorld() ||
      world_session.objects().GetLocalPlayerTyped() == nullptr) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kError,
        "Offline world scenario failed to publish its active player snapshot");
    game_loop_.LeaveWorld();
    game_loop_.SetCharacterWorldRuntime(nullptr);
    character_world_runtime_.Destroy();
    openwow::net::SetClientServicesPacketSendFn({});
    return false;
  }

  if (!world_session.HandlePacket(offline_world_fixture::BuildActionAssignments())) {
    game_loop_.LeaveWorld();
    game_loop_.SetCharacterWorldRuntime(nullptr);
    character_world_runtime_.Destroy();
    openwow::net::SetClientServicesPacketSendFn({});
    return false;
  }

  offline_scenario_world_active_ = true;
  SetMode(UiMode::kLoading);
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                            "Offline world scenario entered the production world-loading path");
  return true;
}

void GlueClient::ReturnFromWorldToGlue(const char *screen_name, bool disconnect_realm_session) {
  ReleaseInWorldMouseButtons();

  if (!scenario_forward_binding_key_.empty()) {
    (void)game_loop_.binding_input().KeyUp(scenario_forward_binding_key_);
    if (auto *session = character_world_runtime_.session(); session != nullptr) {
      session->Update(0.0f, SDL_GetTicks());
    }
    scenario_forward_binding_key_.clear();
  }

  recv_thread_.Stop();
  game_loop_.LeaveWorld();
  game_loop_.SetCharacterWorldRuntime(nullptr);
  openwow::net::SetClientServicesPacketSendFn({});
  realm_runtime_.session.SetPingSentCallback({});
  character_world_runtime_.Destroy();
  offline_scenario_world_active_ = false;

  enter_world_init_active_ = false;
  enter_world_init_started_at_ms_ = 0;

  glue_flow_state_.phase = GlueFlowState::Phase::kIdle;
  openwow::ui::glue::CGlueMgr_ResetStateToIdle();
  glue_flow_state_.cancel_requested.store(false);
  glue_flow_state_.auth_future.reset();
  glue_flow_state_.realm_future.reset();
  glue_flow_state_.world_connect_future.reset();
  glue_flow_state_.charlist_future.reset();
  glue_flow_state_.char_create_future.reset();
  glue_flow_state_.char_delete_future.reset();
  glue_flow_state_.char_rename_future.reset();
  glue_flow_state_.char_customize_future.reset();
  glue_flow_state_.char_faction_change_future.reset();
  glue_flow_state_.char_race_change_future.reset();
  glue_flow_state_.world_enter_future.reset();
  glue_flow_state_.world_connect_queue_progress.reset();
  glue_flow_state_.world_connect_used_fcm_dialog = false;
  glue_flow_state_.status_dialog_open = false;
  glue_flow_state_.status_dialog_type = openwow::ui::glue::StatusDialogType::kNone;
  glue_flow_state_.realm_fetch_transitions = false;
  glue_flow_state_.last_status_text.clear();
  glue_flow_state_.disconnect_requested = false;
  glue_flow_state_.disconnect_timer = 0.0f;

  game_state_.wants_enter_world = false;
  game_state_.status_dialog_type = openwow::ui::glue::StatusDialogType::kNone;

  if (disconnect_realm_session) {
    realm_runtime_.session.Disconnect();
    game_state_.connected = false;
  } else if (!realm_runtime_.session.ReturnToCharacterSelect()) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "GlueClient: authenticated realm stream did not return to character-select state");
    realm_runtime_.session.Disconnect();
    game_state_.connected = false;
  }

  const UiMode desired_mode =
      std::strcmp(screen_name, "charselect") == 0 ? UiMode::kCharacterSelect : UiMode::kLogin;
  const std::string previous_screen =
      game_state_.current_screen.empty() ? std::string(screen_name) : game_state_.current_screen;

  LoadGlueTocAndScripts();
  openwow::ui::glue::Login_SetScreen(game_state_.fire_event, screen_name);
  glue_runtime_.PumpVisibilityTransitions();

  if (mode_ != desired_mode) {
    HandleScreenTransition(previous_screen, screen_name,
                           true);
  }

  game_state_.current_screen = screen_name;
}

void GlueClient::HandleWorldTransportDisconnect() {

  openwow::net::ClientServices::Instance().HandleDisconnectWithCleanup();
  ReturnFromWorldToGlue("login", true);
  (void)openwow::ui::glue::CGlueMgr_NetDisconnectHandler(game_state_);
}

bool GlueClient::Initialize() {
  if (startup_trace_)
    startup_trace_->Add("glue.Initialize.begin");
  if (!InitCVars()) {
    if (startup_trace_)
      startup_trace_->Add("glue.Initialize.fail");
    return false;
  }

  glue_host_.BeginAudioDevicePrepare();
  if (startup_trace_)
    startup_trace_->Add("glue.audio_device_prepare.requested");
  if (!InitVFS()) {
    glue_host_.FinishAudioDevicePrepare();
    if (startup_trace_)
      startup_trace_->Add("glue.Initialize.fail");
    return false;
  }

  openwow::core::ida::InitializeStartupHardwareDetectionState(&login_vfs_);
  openwow::core::ida::GxCVarInitializeRuntime("World of Warcraft");

  if (!InitGraphics()) {
    glue_host_.FinishAudioDevicePrepare();
    if (startup_trace_)
      startup_trace_->Add("glue.Initialize.fail");
    return false;
  }
  if (startup_trace_)
    startup_trace_->Add("glue.InitUiShaderInit.begin");
  openwow::core::InitGameSubsystems_InitializeUiShaders(dbc_loader_);
  if (startup_trace_)
    startup_trace_->Add("glue.InitUiShaderInit.ok");

  openwow::world::CWorld_Initialize();

  openwow::game::input::InputControl_StartupInitialize();
  if (!InitGlueUI()) {
    if (startup_trace_)
      startup_trace_->Add("glue.Initialize.fail");
    return false;
  }
  if (!InitGameLoop()) {
    if (startup_trace_)
      startup_trace_->Add("glue.Initialize.fail");
    return false;
  }
  if (!InitDebugControl()) {
    if (startup_trace_)
      startup_trace_->Add("glue.Initialize.fail");
    return false;
  }
  if (startup_trace_)
    startup_trace_->Add("glue.Initialize.ok");
  return true;
}

bool GlueClient::InitCVars() {
  if (startup_trace_)
    startup_trace_->Add("glue.InitCVars.begin");

  auto &cvar_sys = openwow::ui::game::CVarSystem::Instance();

  openwow::core::ida::CVar_LoadConfig("Config.wtf");
  openwow::core::ida::GxCVarRegister();
  cvar_sys.RegisterDefaults();
  openwow::core::MemoryStorm_RegisterConsoleCommands();
  gamma_controller_.Register(cvar_sys, window_);
  openwow::render::RegisterTextureFilteringModeCVarCallback(cvar_sys);
  openwow::render::RegisterUiFasterCVarCallback(cvar_sys);
  RegisterTextureCacheBudget(cvar_sys);
  openwow::core::ida::RegisterWindowResizeLockCVarCallback(cvar_sys, window_);
  EnsureConvertedTrialCVarCallbackRegistered(cvar_sys);

  cvar_sys.ApplyClientRegisterCVarsValueFixups();

  openwow::debug::InitializeClientErrorDisplayRuntimeState();

  (void)openwow::core::AsyncIO_RegisterCVars();

  (void)cvar_sys.ReconcileValueAgainstValidationCallback("textureFilteringMode");
  (void)cvar_sys.ReconcileValueAgainstValidationCallback("processAffinityMask");
  openwow::ui::game::detail::SyncCameraMotionSettings(game_loop_.world_scene().camera());
  openwow::ui::game::detail::SyncCameraViewPresets(game_loop_.world_scene().camera());

  (void)openwow::platform::SystemMouseSpeedController::Instance().ApplyCVarValue(
      cvar_sys.GetCVar("mouseSpeed"));

  startup_m2_flags_ = openwow::render::M2_RegisterCVars();

  simple_ui_fast_path_enabled_ =
      openwow::render::ApplyCurrentUiFasterCVar(cvar_sys).simple_ui_fast_path_enabled;
  (void)openwow::core::ida::ApplyCurrentWindowResizeLockCVar(cvar_sys, window_);

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                            "CVarSystem: " + std::to_string(cvar_sys.Count()) +
                                " CVars registered after init");

  openwow::core::GameClock::Instance().Init(
      openwow::core::TimingMethodFromCVarValue(cvar_sys.GetCVarInt("timingMethod")));

  if (startup_trace_)
    startup_trace_->Add("glue.InitCVars.ok");
  return true;
}

bool GlueClient::InitVFS() {
  if (startup_trace_)
    startup_trace_->Add("glue.InitVFS.begin");
  const std::string game_data = launch_context_.game_root.string();

  {
    const auto mfil_path = std::filesystem::path(game_data) / "WoW.mfil";
    std::error_code mfil_ec;
    if (std::filesystem::exists(mfil_path, mfil_ec) && !mfil_ec) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                                "WoW.mfil found at " + mfil_path.string() +
                                    " (patch manifest stub — not parsed yet)");
    }

  }

  if (openwow::data::BackupLegacyGlueFilesystemOverrides(game_data)) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kInfo,
        "Detected loose GlueXML/FrameXML or Blizzard_* addon folders; moved them to .old");
  }

  BuildAndPublishLoginVfs();

  RefreshLoginConfiguration();

  SyncConvertedTrialCVarToDataPreload();

  SyncGlueViewportFromWindow();
  glue_fonts_ = openwow::ui::glue::GlueFontRegistry::LoadFromVfs(login_vfs_);
  LogVfsMounts(login_vfs_);

  if (ShouldDumpMpqIndex()) {
    DumpVfsIndex(login_vfs_, launch_context_.diagnostic_output_root / "mpq-index.txt");
  }

  validation_ = openwow::data::ValidateLoginResources(login_vfs_);
  if (validation_.ok) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                              "Login resource validation: OK");
  } else {
    std::string missing = "Login resource validation missing:";
    for (const auto &item : validation_.missing_paths) {
      missing += " " + item;
    }
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, missing);
    std::error_code mpq_ec;
    bool has_mpq_archives = false;
    for (const auto &entry :
         std::filesystem::directory_iterator(std::filesystem::path(game_data) / "Data", mpq_ec)) {
      if (mpq_ec)
        break;
      if (!entry.is_regular_file())
        continue;
      const auto ext = entry.path().extension().string();
      if (ext == ".MPQ" || ext == ".mpq") {
        has_mpq_archives = true;
        break;
      }
    }
    if (has_mpq_archives) {
#if !OPENWOW_HAS_STORMLIB
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kError,
          "MPQ archives detected but this binary was built without StormLib MPQ support. "
          "No placeholder login renderer is implemented; rebuild with MPQ VFS enabled.");
      std::cerr << "OpenWoW requires StormLib-enabled MPQ VFS for stock WoW installs.\n";
      if (startup_trace_)
        startup_trace_->Add("glue.InitVFS.fail");
      return false;
#else
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                                "Detected MPQ archives with StormLib-enabled MPQ VFS support.");
#endif
    }
  }

  LogVfsProbe(login_vfs_, {
                              "/Interface/GlueXML/GlueParent.xml",
                              "/Interface/GlueXML/AccountLogin.xml",
                              "/Interface/GlueXML/RealmList.xml",
                              "/Interface/GlueXML/CharacterSelect.xml",
                              "/Interface/GlueXML/GlueParent.lua",
                              "/Interface/GlueXML/GlueStrings.lua",
                          });

  {
    const int dbc_count = openwow::data::DBClient_Initialize(dbc_loader_, login_vfs_);

    openwow::net::RealmConfigTables::Get().LoadFrom(dbc_loader_);
    dance_move_catalog_.emplace(openwow::game::BuildDanceMoveCatalog(dbc_loader_.dance_moves()));
    if (auto *session = character_world_runtime_.session(); session != nullptr) {
      session->dance_studio().BindDanceMoveCatalog(*dance_move_catalog_);
    }
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                              "DBC: Loaded " + std::to_string(dbc_count) + " tables from VFS");
    openwow::data::BindErrorTableVfs(&login_vfs_);
    openwow::screens::LoadingScreenManager::Get().BindGameTipsStore(&dbc_loader_.game_tips());
  }

  glue_runtime_.SetDataDirectory(std::filesystem::path(game_data) / "Data");

  if (startup_trace_)
    startup_trace_->Add("glue.InitVFS.ok");
  return true;
}

bool GlueClient::InitGraphics() {
  if (startup_trace_)
    startup_trace_->Add("glue.InitGraphics.begin");

  const std::string saved_account_name =
      openwow::ui::game::CVarSystem::Instance().GetCVar("accountName");
  login_screen_.SetUsername(saved_account_name);
  login_screen_.SetPassword("");
  login_screen_.SetRememberPassword(!saved_account_name.empty());

  glue_runtime_.BindWidgetRuntime(&glue_widgets_);
  if (glue_fonts_.has_value()) {
    glue_runtime_.BindFontRegistry(&*glue_fonts_);
  }
  glue_runtime_.BindHost(&glue_host_);
  glue_runtime_.BindGameState(&game_state_);
  glue_runtime_.BindGameTimeData(&character_world_runtime_.game_time());

  glue_host_.SetWindow(window_);

  int window_w = 0;
  int window_h = 0;
  GetDrawableSize(window_, &window_w, &window_h);
  window_w = std::max(1, window_w);
  window_h = std::max(1, window_h);

  layout_width_ = window_w;
  layout_height_ = window_h;

  renderer_context_ = openwow::render::CreateRendererContext();
  openwow::render::api::RendererCreateInfo create_info;
  create_info.platform_window = window_;
  create_info.extent = {static_cast<std::uint32_t>(window_w), static_cast<std::uint32_t>(window_h)};
  create_info.screenshot_target = &openwow::core::ScreenshotSystem::Instance();
  const auto &cvars = openwow::ui::game::CVarSystem::Instance();
  create_info.backend = openwow::render::ResolveRendererBackend(
      openwow::render::ParseRendererBackend(cvars.GetCVar("gxApi")));
  create_info.presentation = BuildPresentationConfig(cvars);

  if (IsBenchmarkRun()) {
    create_info.presentation.vsync = false;
  }
  if (!renderer_context_ ||
      renderer_context_->Initialize(create_info) != openwow::render::api::RendererStatus::Ready) {
    std::cerr << "bgfx init failed\n";
    if (startup_trace_)
      startup_trace_->Add("glue.InitGraphics.fail");
    renderer_context_.reset();
    return false;
  }

  openwow::core::ida::ConsoleAndFont_StartupInitialize();
  {
    auto &cvars = openwow::ui::game::CVarSystem::Instance();

    (void)ApplyGraphicsStartupBootstrapSequence(
        cvars, simple_ui_fast_path_enabled_,
        {.registration_immediate_pass = false,
         .display_ready_pass = openwow::core::ida::ShouldReplayStartupDisplaySettings()});
  }
  (void)startup_m2_flags_;

  if (!frame_job_system_.IsInitialized()) {
    frame_job_system_.Initialize();
    m2_system_.BindFrameJobSystem(&frame_job_system_);
  }
  m2_system_.Initialize();
  if (!glue_renderer_.Init(renderer_context_.get())) {
    std::cerr << "GlueBgfxRenderer init failed\n";
    renderer_context_->Shutdown();
    renderer_context_.reset();
    if (startup_trace_)
      startup_trace_->Add("glue.InitGraphics.fail");
    return false;
  }
  if (glue_fonts_.has_value()) {
    glue_renderer_.BindFontRegistry(&*glue_fonts_);
  }
  glue_renderer_.SetDbcLoader(&dbc_loader_);
  glue_renderer_.BindRenderSubmitTrace(render_submit_trace_, render_submit_trace_path_);

  glue_renderer_.SetMoviePlayer(&glue_runtime_.GetMoviePlayer());

  scene_state_ = GlueSceneState{
      .status_line = validation_.ok
                         ? "Login resources ready"
                         : ("Missing " + std::to_string(validation_.missing_paths.size()) +
                            " login resource paths"),
      .show_error = !validation_.ok,
  };

  logged_lua_failures_.reserve(64);

  {
    auto &cm = game_loop_.cursor_manager();
    cm.SetFileReader([this](const std::string &path) -> std::optional<std::vector<std::uint8_t>> {
      return login_vfs_.ReadFileBytes(path);
    });
    cm.Initialize(window_);

    glue_host_.SetCursorVisibilityHook(
        [this](const bool visible) { game_loop_.cursor_manager().ShowCursor(visible); });
  }

  if (startup_trace_)
    startup_trace_->Add("glue.InitGraphics.ok");
  return true;
}

void GlueClient::HandleScreenTransition(const std::string &old_screen,
                                        const std::string &new_screen,
                                        const bool apply_background_transition) {
  if (apply_background_transition) {
    background_controller_.OnScreenTransition(old_screen, new_screen);
  }

  if (openwow::ui::glue::IsGlueScreenName(new_screen, "login")) {
    SetMode(UiMode::kLogin);
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                              "Screen transition: switched to login");
  } else if (openwow::ui::glue::IsGlueScreenName(new_screen, "movie")) {
    SetMode(UiMode::kLogin);
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                              "Screen transition: switched to movie");
  } else if (openwow::ui::glue::IsGlueScreenName(new_screen, "charselect")) {
    character_screen_.SetCharacters(game_state_.characters);
    if (!game_state_.characters.empty() && game_state_.selected_character_index >= 0 &&
        game_state_.selected_character_index < static_cast<int>(game_state_.characters.size())) {
      character_screen_.SelectCharacterById(
          game_state_.characters[static_cast<std::size_t>(game_state_.selected_character_index)]
              .id);
    } else if (!game_state_.characters.empty() && game_state_.selected_character_index < 0) {
      game_state_.selected_character_index = 0;
      character_screen_.SelectCharacterById(game_state_.characters.front().id);
    }
    SetMode(UiMode::kCharacterSelect);
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                              "Screen transition: initialized charselect (" +
                                  std::to_string(game_state_.characters.size()) + " characters)");
  } else if (openwow::ui::glue::IsGlueScreenName(new_screen, "charcreate")) {

    create_screen_.SyncFromGameState(game_state_);
    SetMode(UiMode::kCharacterCreate);
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                              "Screen transition: initialized charcreate");
  }
}

void GlueClient::LoadGlueTocAndScripts() {

  const auto bootstrap = openwow::ui::glue::LoadGlueTocWithIntegrity(login_vfs_);
  if (!bootstrap.ok) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                              "GlueXML TOC entry loading failed: " + bootstrap.error);
    glue_load_.ok = false;
    glue_load_.error = bootstrap.error;
    glue_load_.loaded_scripts.clear();
    return;
  }

  glue_bindings_.Clear();
  glue_widgets_.ClearAll();
  glue_runtime_.InitializeVm(login_vfs_);
  glue_runtime_.BindBindingRegistry(&glue_bindings_);

  openwow::ui::glue::InterleavedTocProcessor processor(login_vfs_, glue_runtime_, glue_widgets_,
                                                       glue_bindings_);

  processor.SetOnWidgetCreated(
      [this](const std::string &widget_name, const std::string &event_source) {
        const auto res = glue_runtime_.RunWidgetEvent(widget_name, "OnLoad", event_source, {});
        if (!res.ok) {
          const std::string key = event_source + ":OnLoad|" + res.error;
          if (logged_lua_failures_.insert(key).second) {
            openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                                      "Glue OnLoad dispatch failed: widget=" + widget_name +
                                          " err=" + res.error);
          }
        }
      });

  const auto toc_result = processor.ProcessToc(bootstrap.toc_entries.entries);

  const auto sound_capabilities = sound_runtime_.sound_engine().Capabilities();
  ApplySettingsCapabilityPolicy(
      glue_widgets_, openwow::ui::game::CVarSystem::Instance(),
      ClientSettingsCapabilities{
          .hardware_audio_voices = sound_capabilities.hardware_voice_selection,
          .software_hrtf = sound_capabilities.software_hrtf,
      });

  glue_load_.ok = toc_result.ok;
  glue_load_.error = toc_result.error;
  glue_load_.loaded_scripts = glue_runtime_.loaded_scripts();

  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kInfo,
      "Interleaved TOC processing: " + std::to_string(toc_result.xml_files_processed) + " XML, " +
          std::to_string(toc_result.lua_files_executed) + " Lua, " +
          std::to_string(toc_result.widgets_created) + " widgets, " +
          std::to_string(toc_result.onload_fired) + " OnLoad fired");
  if (!toc_result.ok) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                              "Interleaved TOC processing warnings: " + toc_result.error);
  }
}

void GlueClient::CompleteGlueStartupTail() {
  game_state_.ResetRealmListCategoryState();
  openwow::ui::glue::InitializeGlueStartupState();
}

void GlueClient::FireInitialScreenEventIfNeeded() {
  if (!glue_load_.ok || !game_state_.current_screen.empty())
    return;

  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  const int startup_level = static_cast<int>(openwow::core::GetExpansionLevel());
  const InitialGlueScreen initial_screen =
      SelectInitialGlueScreenAndConsumeMovieCvrs(cvars, startup_level);
  const std::string screen_name = InitialGlueScreenName(initial_screen);

  if (screen_name == "login" && glue_runtime_.IsMoviePlaying()) {
    pending_initial_screen_after_movie_ = screen_name;
    return;
  }

  openwow::ui::glue::Login_SetScreen(game_state_.fire_event, screen_name);

  if (game_state_.current_screen.empty()) {
    game_state_.current_screen = screen_name;
  }
}

bool GlueClient::InitGlueUI() {
  if (startup_trace_)
    startup_trace_->Add("glue.InitGlueUI.begin");
  openwow::core::LoginConsoleDiagnostics::Instance().SetEnabled(true);
  glue_runtime_.BindLuaEventTrace(lua_trace_);
  game_state_.send_realm_packet = [this](const openwow::net::wotlk::WorldPacket &packet) {
    return realm_runtime_.session.SendPacket(packet);
  };

  game_state_.fire_event = [this](const std::string &event_name,
                                  const std::vector<openwow::ui::glue::GlueLuaValue> &args) {
    FireGlueEvent(event_name, args);
  };
  game_state_.resolve_glue_string = [this](const std::string_view key) {
    return glue_runtime_.GetGlobalStringOrEmpty(std::string(key));
  };
  openwow::game::BattleNetApi::Instance().SetEventSink(
      [this](const std::string &event_name,
             const std::vector<openwow::game::BNetUiEventArg> &args) {
        std::vector<openwow::ui::glue::GlueLuaValue> glue_args;
        glue_args.reserve(args.size());
        for (const auto &arg : args) {
          switch (arg.kind) {
          case openwow::game::BNetUiEventArg::Kind::kNil:
            glue_args.push_back(openwow::ui::glue::MakeLuaNil());
            break;
          case openwow::game::BNetUiEventArg::Kind::kString:
            glue_args.push_back(MakeLuaString(arg.string_value));
            break;
          case openwow::game::BNetUiEventArg::Kind::kNumber:
            glue_args.push_back(MakeLuaNumber(arg.number_value));
            break;
          case openwow::game::BNetUiEventArg::Kind::kBoolean:
            glue_args.push_back(MakeLuaBool(arg.bool_value));
            break;
          }
        }
        FireGlueEvent(event_name, glue_args);
      });

  game_state_.background_controller = &background_controller_;

  game_state_.char_select_scene = &char_select_scene_;

  game_state_.char_customize_scene = &char_customize_scene_;
  char_select_scene_.BindAppearanceDbcStores(
      &dbc_loader_.char_hair_geosets(), &dbc_loader_.character_facial_hair_styles(),
      &dbc_loader_.chr_races(),
      &dbc_loader_.item_display_info(), &dbc_loader_.helmet_geoset_vis_data(),
      &dbc_loader_.char_sections(), &dbc_loader_.spell_visual_kit(),
      &dbc_loader_.spell_visual_effect_name(), &dbc_loader_.char_start_outfit(), nullptr, nullptr,
      nullptr, &dbc_loader_.creature_display_info(), &dbc_loader_.creature_model_data(),
      &dbc_loader_.creature_family());
  char_customize_scene_.BindAppearanceDbcStores(
      &dbc_loader_.char_hair_geosets(), &dbc_loader_.character_facial_hair_styles(),
      &dbc_loader_.chr_races(),
      &dbc_loader_.item_display_info(), &dbc_loader_.helmet_geoset_vis_data(),
      &dbc_loader_.char_sections(), &dbc_loader_.spell_visual_kit(),
      &dbc_loader_.spell_visual_effect_name(), &dbc_loader_.char_start_outfit(), nullptr, nullptr,
      nullptr, &dbc_loader_.creature_display_info(), &dbc_loader_.creature_model_data(),
      &dbc_loader_.creature_family());

  game_state_.on_screen_transition = [this](const std::string &old_screen,
                                            const std::string &new_screen) {
    HandleScreenTransition(old_screen, new_screen,
                           false);
  };

  glue_runtime_.BindDbcLoader(&dbc_loader_);

  openwow::audio::detail::SetDbcLoaderForAudio(&dbc_loader_);
  openwow::audio::PublishSoundRuntimeDbcData(sound_runtime_, dbc_loader_);

  if (startup_trace_)
    startup_trace_->Add("glue.audio_publication.begin");
  (void)glue_host_.InitializeAudio();
  if (startup_trace_)
    startup_trace_->Add("glue.audio_publication.end");

  SyncGlueViewportFromWindow();

  openwow::game::ObjectEffectDataStore::Instance().LoadEffectData(
      dbc_loader_.spell_visual_effect_name(), dbc_loader_.object_effect(),
      dbc_loader_.object_effect_group(), dbc_loader_.object_effect_modifier(),
      dbc_loader_.object_effect_package(), dbc_loader_.object_effect_package_elem());
  openwow::game::InitWorldEventNames();
  openwow::game::HardcodedEffectIdTable::Initialize(dbc_loader_.spell_visual_effect_name());
  openwow::net::ClientServices::Initialize();
  db_cache_runtime_.ConfigureFromInstall();
  db_cache_runtime_.LoadBeforeWarden();
  (void)game_loop_.item_definitions().HydrateRetailItemNames(db_cache_runtime_.cache());
  openwow::game::WardenModuleCache_LoadStartup(db_cache_runtime_.persistence().GetCacheDirectory(),
                                               db_cache_runtime_.persistence().GetLocale());
  db_cache_runtime_.LoadAfterWarden();

  openwow::game::InputControl_ResetCursorVisibilityForUiInit();

  LoadGlueTocAndScripts();
  glue_renderer_.PrewarmModels(glue_widgets_);
  glue_renderer_.PrewarmTextures(glue_widgets_);
  CompleteGlueStartupTail();

  openwow::game::LCD_Initialize();

  SyncLoginEditText(&glue_runtime_, login_screen_);

  if (glue_load_.ok) {
    FireGlueEvent("FRAMES_LOADED", {});
  }

  if (glue_load_.ok) {
    (void)sound_runtime_.RefreshEnumeratedDevicesAndReconcile(
        true);
    FireGlueEvent("SOUND_DEVICE_UPDATE", {});
  }

  if (const auto *pending_startup_string =
          static_cast<const char *>(openwow::game::simple_script::StartupPendingString_Get());
      pending_startup_string != nullptr) {

    auto &localization = openwow::game::Localization::Get();
    if (localization.HasString(pending_startup_string)) {
      game_state_.changed_option_warnings.emplace_back(
          localization.GetString(pending_startup_string));
    }
    (void)openwow::game::simple_script::StartupPendingString_Set(nullptr);
  }

  (void)openwow::vfs::StartDataPreloadThreadIfNeeded();

  FireInitialScreenEventIfNeeded();
  glue_renderer_.PrewarmModels(glue_widgets_);
  glue_renderer_.PrewarmTextures(glue_widgets_);
  openwow::game::InitSpellVisuals();
  if (glue_load_.ok) {
    glue_runtime_.ApplyFontStringTextKeys();
  }
  if (glue_load_.ok && glue_runtime_.IsAttachedToGlue()) {
    glue_runtime_.PumpVisibilityTransitions();
  }

  RefreshLayout();
  const int window_w = std::max(1, layout_width_);
  const int window_h = std::max(1, layout_height_);

  const auto layout_started = std::chrono::steady_clock::now();
  const auto layout_stats = openwow::ui::glue::ResolveGlueLayoutAndFontMetrics(
      &glue_widgets_, login_vfs_, glue_fonts_.has_value() ? &*glue_fonts_ : nullptr, window_w,
      window_h);
  const auto layout_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - layout_started)
                             .count();
  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kInfo,
      "Glue startup layout: elapsed_ms=" + std::to_string(layout_ms) +
          " resolves=" + std::to_string(layout_stats.full_layout_resolves) +
          " intrinsic_updates=" + std::to_string(layout_stats.intrinsic_size_updates));
  DispatchPendingScrollRangeChangedEvents();

  layout_dirty_ = false;

  if (!ui_frame_tree_dump_written_ && ui_frame_tree_dump_path_.has_value()) {
    openwow::ui::glue::GlueFrameTreeDump dumper;
    std::string err;
    const bool ok = dumper.WriteTsvFile(*ui_frame_tree_dump_path_, glue_widgets_, &err);
    if (!ok) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                                "Glue UI frame-tree dump failed: path=" +
                                    ui_frame_tree_dump_path_->string() + " err=" + err);
    }
    ui_frame_tree_dump_written_ = true;
  }

  UpdateWindowTitle();

  if (startup_trace_)
    startup_trace_->Add("glue.InitGlueUI.ok");
  return true;
}

bool GlueClient::InitGameLoop() {
  if (startup_trace_)
    startup_trace_->Add("glue.InitGameLoop.begin");

  trace_input_ = []() -> bool {
    const char *v = std::getenv("OPENWOW_UI_TRACE_INPUT");
    if (v == nullptr)
      return false;
    std::string s(v);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return s == "1" || s == "true" || s == "yes" || s == "on";
  }();

  if (opts_.scenario_opts.has_value()) {
    scenario_runner_.emplace(std::move(*opts_.scenario_opts));
  }

  game_loop_.SetFileLoader([this](const std::string &path) -> std::vector<std::uint8_t> {
    auto bytes = login_vfs_.ReadFileBytes(path);
    return bytes.value_or(std::vector<std::uint8_t>{});
  });

  game_loop_.SetPrefixFileLoader(
      [this](const std::string &path, std::size_t max_bytes) -> std::vector<std::uint8_t> {
        auto bytes = login_vfs_.ReadFilePrefix(path, max_bytes);
        return bytes.value_or(std::vector<std::uint8_t>{});
      });
  game_loop_.SetRendererContext(renderer_context_.get());

  game_loop_.SetVfs(&login_vfs_);

  if (!game_loop_.Initialize(layout_width_, layout_height_)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                              "GameLoop initialization failed (in-world rendering disabled)");
  }
  game_loop_.SetLoadingScreenArchivePathProbe([](const std::string &path) {
    return openwow::vfs::SFileArchiveHasFile_SetLastErrorOnHit(path.c_str());
  });

  openwow::game::BattlefieldInfo::Get().SetDbcLoader(&dbc_loader_);
  game_loop_.SetDbcLoader(&dbc_loader_);

  game_loop_.SetWindow(window_);
  game_loop_.SetBlockingLoadingEventPump([this]() {

    PumpPendingWindowEvents();
  });
  openwow::game::SetSaveCursorPosCallback(
      []() { (void)openwow::platform::WindowManager::Get().CaptureCursorAnchor(); });

  UpdateTextInputState();
  if (startup_trace_)
    startup_trace_->Add("glue.InitGameLoop.ok");
  return true;
}

bool GlueClient::InitDebugControl() {
  const char *const enabled = std::getenv("OPENWOW_DEBUG_CONTROL");
  if (enabled == nullptr || enabled[0] == '\0' || std::strcmp(enabled, "0") == 0) {
    return true;
  }

  std::uint16_t port = 0;
  if (const char *const value = std::getenv("OPENWOW_DEBUG_CONTROL_PORT");
      value != nullptr && value[0] != '\0') {
    unsigned int parsed = 0;
    const std::string_view text(value);
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size() ||
        parsed > std::numeric_limits<std::uint16_t>::max()) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
                                "Invalid OPENWOW_DEBUG_CONTROL_PORT; expected uint16 value");
      return false;
    }
    port = static_cast<std::uint16_t>(parsed);
  }

  openwow::debug::control::DebugControlCapabilities capabilities;
  game_loop_.game_ui().SetDebugSubmissionReceiptsEnabled(true);
  debug_ui_control_adapter_ = std::make_unique<DebugUiControlAdapter>();
  const auto stop_ui_inspector = [this] {
    if (debug_ui_control_adapter_) {
      debug_ui_control_adapter_->Stop();
    }
    game_loop_.game_ui().SetDebugSubmissionReceiptsEnabled(false);
  };
  capabilities.inspect_ui = [this](const openwow::debug::control::RequestContext &context,
                                   const openwow::debug::control::InspectUiRequest &request) {
    return debug_ui_control_adapter_->Inspect(context, request);
  };
  capabilities.submit_input = [this](const openwow::debug::control::RequestContext &,
                                     const openwow::debug::control::InputEvent &event) {
    return SubmitDebugInput(window_, event);
  };
  capabilities.capture_screenshot = [](const openwow::debug::control::RequestContext &context,
                                       const openwow::debug::control::CaptureScreenshotRequest &)
      -> CapabilityResult<openwow::debug::control::ScreenshotResult> {
    auto &screenshots = openwow::core::ScreenshotSystem::Instance();
    const std::uint32_t initial_count = screenshots.GetScreenshotCount();
    if (!screenshots.CaptureScreenshot(openwow::core::ScreenshotRequestDomain::None,
                                       openwow::core::ImageFormat::TGA)) {
      return DebugControlError{"busy", "screenshot capture is busy", true};
    }

    const auto deadline = std::chrono::steady_clock::now() + kDebugScreenshotTimeout;
    while (std::chrono::steady_clock::now() < deadline) {
      if (context.is_cancellation_requested && context.is_cancellation_requested()) {
        return DebugControlError{"cancelled", "screenshot capture cancelled", false};
      }
      if (screenshots.GetScreenshotCount() != initial_count) {
        std::error_code error;
        const std::filesystem::path path =
            std::filesystem::absolute(screenshots.GetLastScreenshotPath(), error);
        if (error || path.empty()) {
          return DebugControlError{"screenshot_failed", "screenshot artifact path is invalid",
                                   false};
        }
        openwow::debug::control::ScreenshotArtifact artifact{path.string(),
                                                             ScreenshotMediaType(path)};
        return openwow::debug::control::ScreenshotResult{std::move(artifact)};
      }
      std::this_thread::sleep_for(kDebugScreenshotPollInterval);
    }
    return DebugControlError{"timeout", "screenshot capture timed out", true};
  };

  auto codec = std::make_shared<const openwow::debug::control::BoostJsonDebugControlCodec>();
  openwow::debug::control::DebugControlServerOptions options;
  options.port = port;
  debug_control_server_ = std::make_unique<openwow::debug::control::DebugControlServer>(
      std::move(codec), std::move(capabilities), options);
  auto start = debug_control_server_->Start();
  if (const auto *error = std::get_if<DebugControlError>(&start)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
                              "Debug control server failed to start: " + error->code + ": " +
                                  error->message);
    debug_control_server_.reset();
    stop_ui_inspector();
    debug_ui_control_adapter_.reset();
    return false;
  }

  const auto &endpoint = std::get<openwow::debug::control::DebugControlEndpoint>(start);
  debug_control_endpoint_path_ =
      launch_context_.diagnostic_output_root / "debug-control-endpoint.json";
  std::filesystem::path temporary_path = debug_control_endpoint_path_;
  temporary_path += ".tmp";
  std::error_code file_error;
  std::filesystem::create_directories(debug_control_endpoint_path_.parent_path(), file_error);
  if (!file_error) {
    std::filesystem::remove(temporary_path, file_error);
  }
  if (!file_error) {
    std::filesystem::remove(debug_control_endpoint_path_, file_error);
  }

  int descriptor = -1;
  if (!file_error) {
#ifdef _WIN32
    descriptor = ::_wopen(temporary_path.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
                          _S_IREAD | _S_IWRITE);
#else
    descriptor = ::open(temporary_path.c_str(), O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
#endif
  }
  const std::string endpoint_json =
      "{\"address\":\"" + endpoint.address + "\",\"port\":" + std::to_string(endpoint.port) +
      ",\"capability_token\":\"" + endpoint.capability_token + "\"}\n";
  std::size_t written = 0;
  while (descriptor >= 0 && written < endpoint_json.size()) {
#ifdef _WIN32
    const int count = ::_write(descriptor, endpoint_json.data() + written,
                               static_cast<unsigned int>(endpoint_json.size() - written));
#else
    const ssize_t count =
        ::write(descriptor, endpoint_json.data() + written, endpoint_json.size() - written);
#endif
    if (count <= 0) {
      file_error = std::make_error_code(std::errc::io_error);
      break;
    }
    written += static_cast<std::size_t>(count);
  }
  if (descriptor >= 0 && !file_error) {
    std::filesystem::permissions(
        temporary_path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace, file_error);
  }
  if (descriptor >= 0 && !file_error) {
#ifdef _WIN32
    if (::_commit(descriptor) != 0) {
#else
    if (::fsync(descriptor) != 0) {
#endif
      file_error = std::make_error_code(std::errc::io_error);
    }
  }
  if (descriptor >= 0) {
#ifdef _WIN32
    if (::_close(descriptor) != 0 && !file_error) {
#else
    if (::close(descriptor) != 0 && !file_error) {
#endif
      file_error = std::make_error_code(std::errc::io_error);
    }
  }
  if (!file_error) {
    std::filesystem::rename(temporary_path, debug_control_endpoint_path_, file_error);
  }
  if (file_error) {
    std::error_code ignored;
    std::filesystem::remove(temporary_path, ignored);
    stop_ui_inspector();
    debug_control_server_->Stop();
    debug_control_server_.reset();
    debug_ui_control_adapter_.reset();
    debug_control_endpoint_path_.clear();
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
                              "Debug control endpoint publication failed: " + file_error.message());
    return false;
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                            "Debug control endpoint: " + endpoint.address + ":" +
                                std::to_string(endpoint.port) +
                                " capability token: " + endpoint.capability_token);
  return true;
}

void GlueClient::PumpGlueRequests(float dt) {
  if (mode_ != UiMode::kInWorld && glue_load_.ok && glue_runtime_.IsAttachedToGlue()) {
    SyncGlueFrameDepthTargets();
  }

  openwow::core::LoginConsoleDiagnostics::Instance().DrainToConsole();

  GlueFlowContext glue_flow_ctx;
  glue_flow_ctx.dt = dt;
  glue_flow_ctx.game_state = &game_state_;
  glue_flow_ctx.glue_widgets = &glue_widgets_;
  glue_flow_ctx.login_screen = &login_screen_;
  glue_flow_ctx.realm_screen = &realm_screen_;
  glue_flow_ctx.character_screen = &character_screen_;
  glue_flow_ctx.realm_session = &realm_runtime_.session;
  glue_flow_ctx.addon_discovery_vfs = &login_vfs_;
  glue_flow_ctx.auth_host = auth_host_;
  glue_flow_ctx.auth_port = auth_port_;
  glue_flow_ctx.auth_session_token = &auth_session_token_;
  glue_flow_ctx.show_error = &show_error_;
  glue_flow_ctx.resolve_glue_string = [this](const std::string &key) {
    return glue_runtime_.GetGlobalStringOrEmpty(key);
  };
  glue_flow_ctx.fire_glue_event = [this](const std::string &event_name,
                                         const std::vector<openwow::ui::glue::GlueLuaValue> &args) {
    FireGlueEvent(event_name, args);
  };
  glue_flow_ctx.set_login_status = [this](bool should_show_error, const std::string &status_line) {
    scene_state_.show_error = should_show_error;
    scene_state_.status_line = status_line;

    UpdateWindowTitle();
  };
  glue_flow_ctx.after_login_success = [this]() {
    if (openwow::ui::glue::FindAndSelectSavedRealm(
            game_state_, false)) {
      openwow::ui::glue::CGlueMgr_ConnectToRealm(game_state_);
      return;
    }

    SetMode(UiMode::kRealmDialog);
    FireGlueEvent("OPEN_REALM_LIST", {});

    glue_runtime_.PumpVisibilityTransitions();
  };
  glue_flow_ctx.setup_char_login_camera = [this](const std::uint8_t class_id,
                                                 const std::uint8_t race_id, float *const out_xyz) {
    return openwow::ui::glue::CGlueMgr_SetupCharLoginCamera(m2_system_, dbc_loader_, login_vfs_,
                                                            class_id, race_id, out_xyz);
  };
  glue_flow_ctx.enter_world_init = [this](std::uint32_t map_id, float x, float y, float z,
                                          std::uint8_t race_id) {
    (void)race_id;
    show_error_ = false;
    enter_world_init_active_ = true;
    enter_world_init_started_at_ms_ = SDL_GetTicks();

    (void)openwow::core::EnterWorldInit({map_id, x, y, z}, sound_runtime_);
    game_loop_.PrepareWorldEntry(map_id, x, y, z);
    SetMode(UiMode::kLoading);
  };
  glue_flow_ctx.abort_enter_world_init = [this]() {
    enter_world_init_active_ = false;
    enter_world_init_started_at_ms_ = 0;
    game_loop_.AbortPreparedWorldEntry();
    SetMode(UiMode::kCharacterSelect);
  };

  glue_flow_ctx.after_enter_world = [this](std::uint32_t map_id, float x, float y, float z,
                                           float orientation) {
    show_error_ = false;

    const int selected_index = game_state_.selected_character_index;
    if (selected_index < 0 || selected_index >= static_cast<int>(game_state_.characters.size()) ||
        game_state_.characters[static_cast<std::size_t>(selected_index)].id == 0) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
                                "GlueClient: lost selected character during world-session handoff");
      game_loop_.AbortPreparedWorldEntry();
      enter_world_init_active_ = false;
      enter_world_init_started_at_ms_ = 0;
      show_error_ = true;
      realm_runtime_.session.Disconnect();
      game_state_.connected = false;
      glue_flow_state_.phase = GlueFlowState::Phase::kIdle;
      SetMode(UiMode::kCharacterSelect);
      return;
    }
    const auto &selected_character =
        game_state_.characters[static_cast<std::size_t>(selected_index)];
    const std::uint64_t character_guid = selected_character.id;

    successful_forward_start_packets_.store(0, std::memory_order_relaxed);
    successful_movement_heartbeat_packets_.store(0, std::memory_order_relaxed);
    successful_movement_stop_packets_.store(0, std::memory_order_relaxed);
    scenario_forward_binding_key_.clear();
    auto &world_session = character_world_runtime_.CreateSession(
        [this](const std::uint32_t version) { ApplyClientCacheVersion(version); });
    openwow::game::CGUnit_C::Initialize(world_session);
    if (dance_move_catalog_) {
      world_session.dance_studio().BindDanceMoveCatalog(*dance_move_catalog_);
    }

    world_session.SetPendingCharacterIdentity({
        .name = selected_character.name,
        .race_id = selected_character.race_id,
        .class_id = selected_character.class_id,
        .gender = selected_character.gender,
    });
    const auto send_world_packet = [this](const openwow::net::wotlk::WorldPacket &pkt) {
      const bool sent = realm_runtime_.session.SendPacket(pkt);
      if (!sent) {
        return false;
      }
      if (pkt.IsOpcode(openwow::net::wotlk::Opcode::MSG_MOVE_START_FORWARD)) {
        successful_forward_start_packets_.fetch_add(1, std::memory_order_relaxed);
      } else if (pkt.IsOpcode(openwow::net::wotlk::Opcode::MSG_MOVE_HEARTBEAT)) {
        successful_movement_heartbeat_packets_.fetch_add(1, std::memory_order_relaxed);
      } else if (pkt.IsOpcode(openwow::net::wotlk::Opcode::MSG_MOVE_STOP)) {
        successful_movement_stop_packets_.fetch_add(1, std::memory_order_relaxed);
      }
      return true;
    };
    openwow::net::SetClientServicesPacketSendFn(send_world_packet);
    world_session.SetSendFn(send_world_packet);

    world_session.latency_tracker().SetAutoInterval(0.0f);
    realm_runtime_.session.SetPingSentCallback(
        [this](const std::uint32_t sequence, const std::uint32_t send_tick_ms) {
          if (auto *session = character_world_runtime_.session(); session != nullptr) {
            session->latency_tracker().ObservePingSent(sequence, send_tick_ms);
          }
        });

    if (game_state_.session_key_valid) {
      realm_runtime_.InitWarden(game_state_.session_key_raw);
    }

    if (!world_session.AdoptLoginVerifyWorld(
            character_guid,
            {.map_id = map_id, .x = x, .y = y, .z = z, .orientation = orientation})) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
                                "GlueClient: failed to adopt LOGIN_VERIFY_WORLD state");
      realm_runtime_.session.SetPingSentCallback({});
      character_world_runtime_.Destroy();
      openwow::net::SetClientServicesPacketSendFn({});
      game_loop_.AbortPreparedWorldEntry();
      enter_world_init_active_ = false;
      enter_world_init_started_at_ms_ = 0;
      show_error_ = true;
      realm_runtime_.session.Disconnect();
      game_state_.connected = false;
      glue_flow_state_.phase = GlueFlowState::Phase::kIdle;
      SetMode(UiMode::kCharacterSelect);
      return;
    }

    PrepareGlueRuntimeForEnterWorld(glue_runtime_);

    glue_host_.StopCreditsMusic();
    glue_host_.StopGlueMusic();
    glue_host_.StopGlueAmbience();
    openwow::ui::framexml::ClearVirtualTemplates();
    openwow::ui::glue::CGlueMgr_CleanupEnterWorldCharacterScenes(game_state_);

    openwow::ui::glue::CGlueMgr_CleanupCharCreateForEnterWorld(game_state_);

    character_world_runtime_.CreatePacketQueue();

    for (auto &packet : realm_runtime_.session.DrainDeferredPackets()) {
      character_world_runtime_.packet_queue()->Push(std::move(packet));
    }

    recv_thread_.Start(&realm_runtime_.session, character_world_runtime_.packet_queue(), [this]() {

      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                                "GlueClient: network recv thread detected disconnect");
    });

    game_loop_.SetCharacterWorldRuntime(&character_world_runtime_);

    game_loop_.SetSendPacketFn(send_world_packet);

    game_loop_.SetClientTimeFn(openwow::core::GameClock::GetTickCount32);

    const bool had_prepared_world_entry = enter_world_init_active_;
    bool used_prepared_world_entry = false;
    if (had_prepared_world_entry) {
      used_prepared_world_entry =
          game_loop_.FinalizePreparedWorldEntry(map_id, x, y, z, orientation);
    }

    if (used_prepared_world_entry) {
      if (enter_world_init_started_at_ms_ != 0) {
        world_session.StartBotDetectedCountdownFromInit(enter_world_init_started_at_ms_,
                                                        SDL_GetTicks());
      }
    } else {
      if (!had_prepared_world_entry) {
        (void)openwow::core::EnterWorldInit({map_id, x, y, z}, sound_runtime_);
      }
      game_loop_.EnterWorld(map_id, x, y, z, orientation);
    }

    enter_world_init_active_ = false;
    enter_world_init_started_at_ms_ = 0;

    SetMode(game_loop_.IsLoading() ? UiMode::kLoading : UiMode::kInWorld);
  };

  PumpGlueFlow(glue_flow_ctx, glue_flow_state_);

  if (game_state_.wants_quit) {
    game_state_.wants_quit = false;
    running_ = false;
  }
  if (openwow::core::ConsumeClientShutdownRequest()) {
    running_ = false;
  }

}

void GlueClient::SyncGlueFrameDepthTargets() {

  glue_widgets_.SetEffectiveDepth("GlueParent", kGlueFrameDepthBaseline);
  glue_widgets_.SetEffectiveDepth("OptionsSelectFrame", kGlueFrameDepthBaseline);
}

void GlueClient::UpdateHoverState() {
  if (!glue_runtime_.IsAttachedToGlue() || mode_ == UiMode::kInWorld) {
    glue_runtime_.ClearHoveredWidget();
    return;
  }

  int mx = 0;
  int my = 0;
  SDL_GetMouseState(&mx, &my);
  ScaleMouseToDrawable(window_, mx, my);

  const std::pair<int, int> cursor{mx, my};
  if (last_glue_cursor_ != cursor) {
    last_glue_cursor_ = cursor;
    glue_widgets_.SetCachedCursorPosition(mx, my);
  } else {

    glue_widgets_.MarkCursorHitTestRefresh();
  }
  glue_runtime_.PumpCursorDrivenUpdates();
}

void GlueClient::UpdateOnUpdateScripts(double elapsed_sec) {
  if (!glue_runtime_.IsAttachedToGlue()) {
    return;
  }

  glue_runtime_.PumpFrameServices();

  glue_runtime_.UpdateMovie(elapsed_sec);

  if (pending_initial_screen_after_movie_.has_value() && !glue_runtime_.IsMoviePlaying() &&
      glue_load_.ok) {
    const std::string screen_name = *pending_initial_screen_after_movie_;
    pending_initial_screen_after_movie_.reset();
    if (game_state_.current_screen.empty()) {
      openwow::ui::glue::Login_SetScreen(game_state_.fire_event, screen_name);
      if (game_state_.current_screen.empty()) {
        game_state_.current_screen = screen_name;
      }
    }
  }

  if (!glue_load_.ok)
    return;

  UpdateFocusedEditBoxInputLanguage();

  const std::vector<openwow::ui::glue::GlueLuaValue> update_args = {
      MakeLuaNumber(std::max(0.0, elapsed_sec))};
  const std::vector<openwow::ui::glue::GlueLuaValue> no_args;
  const auto &widgets = glue_runtime_.PerFrameWidgetsInUpdateOrder();
  for (const auto &widget : widgets) {
    const std::string &widget_name = widget.name;
    if (!glue_widgets_.IsVisible(widget_name)) {
      continue;
    }
    if (widget.has_on_update) {
      (void)DispatchWidgetEvent(widget_name, "OnUpdate", widget_name + ".OnUpdate", update_args);
    }

    if (widget.has_on_update_model && glue_widgets_.IsVisible(widget_name)) {

      (void)DispatchWidgetEvent(widget_name, "OnUpdateModel", widget_name + ".OnUpdateModel",
                                no_args);
    }

    if (widget.has_animation_groups) {
      glue_runtime_.UpdateWidgetAnimations(widget_name, elapsed_sec);
    }
  }
}

void GlueClient::SyncScreenModels(const float frame_delta_seconds) {
  if (mode_ == UiMode::kCharacterCreate) {
    create_screen_.SyncFromGameState(game_state_);
  } else if (mode_ == UiMode::kLoading) {

    if (game_loop_.IsLoading()) {
      game_loop_.Tick(std::max(0.0F, frame_delta_seconds));
    }

    if (enter_world_init_active_) {
      return;
    }

    if (!game_loop_.IsLoading()) {
      SetMode(UiMode::kInWorld);
    }
  }
}

void GlueClient::Render(std::uint32_t now_ms, std::uint32_t frame_delta_ms,
                        double elapsed_sec) {

  int width = 0;
  int height = 0;
  GetDrawableSize(window_, &width, &height);
  width = std::max(1, width);
  height = std::max(1, height);

  if (mode_ == UiMode::kLogin || mode_ == UiMode::kRealmDialog ||
      mode_ == UiMode::kCharacterSelect || mode_ == UiMode::kCharacterCreate) {
    if (renderer_context_ != nullptr) {
      const auto &cvars = openwow::ui::game::CVarSystem::Instance();
      const auto enabled = [&cvars](const char *name) {
        return !cvars.Exists(name) || cvars.GetCVarBool(name);
      };
      glue_renderer_.RenderGlue(glue_widgets_, renderer_context_->Graph(), width, height, now_ms,
                                frame_delta_ms,
                                {
                                    .effects_enabled = enabled("ffx"),
                                    .glow_enabled = enabled("ffxGlow"),
                                    .death_effect_enabled = enabled("ffxDeath"),
                                    .rectangle_textures = enabled("ffxRectangle"),
                                    .widescreen = cvars.GetCVarBool("widescreen"),
                                    .particle_density = cvars.GetCVarFloat("particleDensity"),
                                });
    }

    for (auto &ev : glue_widgets_.ConsumeCursorChangedEvents()) {
      (void)DispatchWidgetEvent(
          ev.widget_name, "OnCursorChanged", ev.widget_name + ".OnCursorChanged",
          {MakeLuaNumber(static_cast<double>(ev.x)), MakeLuaNumber(static_cast<double>(ev.y)),
           MakeLuaNumber(static_cast<double>(ev.w)), MakeLuaNumber(static_cast<double>(ev.h))});
    }

    for (auto &ev : glue_widgets_.ConsumeAnimationFinishedEvents()) {
      (void)DispatchWidgetEvent(ev.widget_name, "OnAnimFinished",
                                ev.widget_name + ".OnAnimFinished", {});
    }
  } else if (mode_ == UiMode::kLoading) {

  } else if (mode_ == UiMode::kInWorld) {

    const float dt = static_cast<float>(elapsed_sec);
    game_loop_.SetScreenSize(width, height);
    game_loop_.Tick(dt);

    if (auto *session = character_world_runtime_.session();
        session != nullptr && session->ConsumeLogoutComplete()) {
      auto &client_services = openwow::net::ClientServices::Instance();
      client_services.HandleLogoutComplete();
      const bool shutdown_after_logout = openwow::core::ConsumeClientShutdownRequest();
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                                shutdown_after_logout
                                    ? "GlueClient: logout complete, shutting down client"
                                    : "GlueClient: logout complete, returning to character select");
      ReturnFromWorldToGlue("charselect", false);
      if (shutdown_after_logout) {
        running_ = false;
      }
      return;
    }

    if (!offline_scenario_world_active_ &&
        (recv_thread_.IsDisconnected() || !realm_runtime_.session.connected())) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                                "GlueClient: disconnect detected, returning to login");
      HandleWorldTransportDisconnect();
      return;
    }

  } else {
    if (auto clear_view = BuildClientBackbufferFrame(renderer_context_.get(), width, height)) {
      (void)openwow::render::ClearRendererContextView(renderer_context_.get(), *clear_view,
                                                      0x040c12ff, static_cast<std::uint32_t>(width),
                                                      static_cast<std::uint32_t>(height));
    }
  }

  if (auto fps_overlay_view = ResolveBootstrapFpsOverlayView(renderer_context_.get())) {
    RenderBootstrapFpsOverlay(fps_overlay_text_renderer_, *fps_overlay_view,
                              static_cast<float>(width), static_cast<float>(height));
  }
  if (renderer_context_ != nullptr) {
    renderer_context_->MarkFinalCompositorStage(
        openwow::render::api::FinalCompositorStage::kDebugOverlay);
    renderer_context_->CompleteFinalCompositor(
        openwow::render::api::FinalCompositorTarget::kLdrBackbuffer);
  }
}

void GlueClient::UpdateWindowTitle() {

  static constexpr const char *kWindowTitle = "World of Warcraft";
  if (last_window_title_ != kWindowTitle) {
    last_window_title_ = kWindowTitle;
    SDL_SetWindowTitle(window_, kWindowTitle);
  }
}

bool GlueClient::IsBenchmarkRun() const {
  if (scenario_runner_.has_value()) {
    return scenario_runner_->options().benchmark_frames > 0;
  }
  return opts_.scenario_opts.has_value() &&
         opts_.scenario_opts->benchmark_frames > 0;
}

bool GlueClient::TickScenario(ScenarioRunner::Stage stage, std::uint32_t now_ms) {
  if (!scenario_runner_.has_value())
    return true;

  ScenarioContext scenario_ctx;
  scenario_ctx.glue_runtime = &glue_runtime_;
  scenario_ctx.glue_widgets = &glue_widgets_;
  scenario_ctx.game_state = &game_state_;
  scenario_ctx.in_world = mode_ == UiMode::kInWorld;
  scenario_ctx.flow_phase = static_cast<int>(glue_flow_state_.phase);
  scenario_ctx.show_error = show_error_;
  scenario_ctx.flow_status_text = glue_flow_state_.last_status_text;
  scenario_ctx.set_login_credentials = [this](const std::string_view account,
                                              const std::string_view password) {
    login_screen_.SetUsername(std::string(account));
    login_screen_.SetPassword(std::string(password));
    SyncLoginEditText(&glue_runtime_, login_screen_);
  };
  scenario_ctx.clear_login_password = [this]() {
    login_screen_.SecureClearPassword();
    (void)glue_runtime_.SetEditBoxTextProgrammatically("AccountLoginPasswordEdit", "");
  };
  scenario_ctx.enter_offline_world = [this]() { return EnterOfflineScenarioWorld(); };
  scenario_ctx.control_forward_movement = [this](const ScenarioForwardMovementCommand command) {
    auto &bindings = game_loop_.binding_profiles();
    if (command == ScenarioForwardMovementCommand::kStart) {
      if (!scenario_forward_binding_key_.empty()) {
        return true;
      }
      const auto forward_chords = bindings.ChordsForCommand(
          openwow::game::BindingCommand(openwow::game::BindingAction::kMoveForward),
          openwow::game::BindingProfileScope::kActive);
      if (forward_chords.empty() ||
          !game_loop_.binding_input().KeyDown(forward_chords.front().value())) {
        return false;
      }
      scenario_forward_binding_key_ = forward_chords.front().value();
      return true;
    }

    if (scenario_forward_binding_key_.empty()) {
      return false;
    }
    const bool handled = game_loop_.binding_input().KeyUp(scenario_forward_binding_key_);
    if (handled) {
      scenario_forward_binding_key_.clear();
    }
    return handled;
  };
  scenario_ctx.skip_cinematic = [this]() {
    auto &cinematic = game_loop_.cinematic_player();
    auto *const session = character_world_runtime_.session();
    if (session == nullptr || !cinematic.IsPlaying() || !cinematic.CanSkip()) {
      return false;
    }
    cinematic.Skip(*session);
    return true;
  };
  scenario_ctx.query_play_state = [this]() {
    ScenarioPlayState state;
    state.connected = offline_scenario_world_active_ || realm_runtime_.session.connected();
    state.logout_request_pending =
        openwow::net::ClientServices::Instance().HasPendingLogoutRequest();
    const auto logout_countdown = ReadStockLogoutCountdown(game_loop_.game_ui());
    state.logout_countdown_visible = logout_countdown.visible;
    state.logout_countdown_seconds = logout_countdown.seconds;
    if (renderer_context_ != nullptr) {
      const auto &compositor = renderer_context_->FinalCompositor();
      state.frame_generation = compositor.active_generation;
      state.final_backbuffer_ready = compositor.IsCurrentWorldFrameComplete();
      state.loading_final_backbuffer_ready = compositor.IsCurrentLoadingFrameComplete();
    }
    state.loading_screen_visible = game_loop_.IsLoading();
    state.loading_screen_sole_owner = mode_ == UiMode::kLoading && game_loop_.IsLoading();
    state.loading_render_submissions = game_loop_.loading_screen_render_submissions();
    state.loading_self_presented_frames = game_loop_.loading_screen_self_presented_frames();
    state.loading_coalesced_callbacks = game_loop_.loading_screen_coalesced_callbacks();
    const auto &cinematic = game_loop_.cinematic_player();
    state.cinematic_playing = cinematic.IsPlaying();
    state.cinematic_presenting = cinematic.IsPresenting();
    state.cinematic_can_skip = cinematic.CanSkip();
    if (mode_ != UiMode::kInWorld || character_world_runtime_.session() == nullptr ||
        !game_loop_.IsInWorld() || !game_loop_.game_ui().is_initialized()) {
      return state;
    }

    auto& session = *character_world_runtime_.session();
    const auto *player = session.objects().GetLocalPlayerTyped();
    const auto *mover = openwow::game::ResolveEffectiveMovingUnit(session);
    state.mover_guid = session.player_control_runtime()
                           .ActiveMoverGuid()
                           .GetRawValue();

    state.forward_binding_available =
        !game_loop_.binding_profiles()
             .ChordsForCommand(
                 openwow::game::BindingCommand(openwow::game::BindingAction::kMoveForward),
                 openwow::game::BindingProfileScope::kActive)
             .empty();
    state.ready = player != nullptr && state.mover_guid != 0 && state.forward_binding_available;
    state.terrain_tiles_loaded = game_loop_.world_scene().world_map().loaded_tile_count();
    state.object_instances = game_loop_.world_scene().object_renderer().active_instance_count();
    const auto &world_camera = game_loop_.world_scene().camera();
    state.camera_desired_distance = world_camera.distance();
    state.camera_resolved_distance = world_camera.resolved_distance();
    state.camera_x = world_camera.GetX();
    state.camera_y = world_camera.GetY();
    state.camera_z = world_camera.GetZ();
    state.camera_yaw = world_camera.yaw();
    state.camera_pitch = world_camera.pitch();
    state.player_camera_alpha_visible =
        player != nullptr && world_camera.bound_object() == player->GetGuid().GetRawValue() &&
        world_camera.resolved_distance() > 0.1f;
    if (player != nullptr) {
      state.player_render_ready =
          game_loop_.world_scene().object_renderer().IsLoadingScreenPlayerRenderAssetReady(
              player->GetGuid());
      state.player_visible_draw_submitted =
          game_loop_.world_scene().object_renderer().HasSubmittedVisibleDraw(player->GetGuid());
    }
    state.game_ui_loaded = game_loop_.game_ui().is_loaded();
    state.game_ui_frames = game_loop_.game_ui().frame_store().enumerable_frame_count();
    const auto &ui_performance = game_loop_.game_ui().performance_counters();
    state.ui_traversal_entries = ui_performance.traversal_entries;
    state.ui_render_candidates = ui_performance.last_render_candidates;
    state.world_ui_render_generation = ui_performance.last_render_generation;
    state.world_ui_world_map_descendant_submissions =
        ui_performance.last_render_world_map_descendant_submissions;
    state.world_ui_world_map_background_submissions =
        ui_performance.last_render_world_map_background_submissions;
    state.world_ui_world_map_detail_tile_submissions =
        ui_performance.last_render_world_map_detail_tile_submissions;
    state.world_ui_character_panel_descendant_submissions =
        ui_performance.last_render_character_panel_descendant_submissions;
    state.world_ui_character_panel_background_submissions =
        ui_performance.last_render_character_panel_background_submissions;
    const auto &render_stats = renderer_context_->Stats();
    state.render_draw_calls = render_stats.draw_calls;
    state.render_cpu_time_ms = render_stats.cpu_time_ms;
    state.render_gpu_time_ms = render_stats.gpu_time_ms;
    if (const auto *main_menu = game_loop_.game_ui().frame_store().FindFrame("MainMenuBar");
        main_menu != nullptr) {
      state.main_menu_visible = main_menu->visible;
    }
    const auto world_ui = openwow::ui::game::BuildWorldUiSnapshot(
        game_loop_.game_ui(), game_loop_.minimap(), game_loop_.screen_width(),
        game_loop_.screen_height());
    state.world_ui_regions_ready = world_ui.required_regions_present;
    state.world_ui_anchors_valid = world_ui.stock_anchors_valid;
    state.world_ui_text_contained = world_ui.named_text_contained;
    const auto semantic_region_ready = [&world_ui](const char *name, const bool require_visible) {
      const auto found = std::find_if(world_ui.regions.begin(), world_ui.regions.end(),
                                      [name](const auto &region) { return region.name == name; });
      return found != world_ui.regions.end() && found->present && found->rect.width > 0 &&
             found->rect.height > 0 && (!require_visible || found->visible);
    };
    state.world_ui_player_frame_ready = semantic_region_ready("PlayerFrame", true);
    state.world_ui_player_portrait_ready = world_ui.player_portrait_ready;
    const std::uint32_t power_type =
        player != nullptr ? std::min<std::uint32_t>(player->State().GetPowerType(), 6u) : 0u;
    const std::uint32_t health =
        player != nullptr ? player->GetUInt32(openwow::game::UNIT_FIELD_HEALTH) : 0u;
    const std::uint32_t maximum_health =
        player != nullptr ? player->GetUInt32(openwow::game::UNIT_FIELD_MAXHEALTH) : 0u;
    const std::uint32_t power =
        player != nullptr ? player->GetUInt32(openwow::game::UNIT_FIELD_POWER1 + power_type) : 0u;
    const std::uint32_t maximum_power =
        player != nullptr ? player->GetUInt32(openwow::game::UNIT_FIELD_MAXPOWER1 + power_type)
                          : 0u;
    state.world_ui_health_power_ready =
        player != nullptr && maximum_health > 0u && health > 0u && health <= maximum_health &&
        (maximum_power == 0u || power <= maximum_power) && world_ui.player_status_bars_ready;
    state.world_ui_unit_frames_ready = state.world_ui_player_frame_ready &&
                                       state.world_ui_player_portrait_ready &&
                                       state.world_ui_health_power_ready;
    state.world_ui_action_icon_ready = world_ui.action_icon_ready;
    state.world_ui_chat_ready = world_ui.chat_content_ready;
    state.world_ui_minimap_ready = world_ui.minimap_surface_ready;
    state.world_ui_world_map_ready = semantic_region_ready("WorldMapFrame", false);
    const auto tracked_visible = [this](const char *name) {
      const auto *const frame = game_loop_.game_ui().frame_store().FindFrame(name);
      return frame != nullptr && frame->visible;
    };
    state.world_ui_world_map_visible = tracked_visible("WorldMapFrame");
    state.world_ui_character_panel_visible = tracked_visible("CharacterFrame");
    const auto *const character_panel =
        game_loop_.game_ui().frame_store().FindFrame("CharacterFrame");
    const auto *const character_panel_rect =
        game_loop_.game_ui().retained_layout().FindRect("CharacterFrame");
    state.world_ui_character_panel_ready =
        character_panel != nullptr && character_panel_rect != nullptr &&
        character_panel_rect->width > 0 && character_panel_rect->height > 0;
    state.world_ui_character_model_ready = ui_performance.last_render_character_model_submitted;
    std::string runtime_character_name;
    if (lua_State *const state = game_loop_.game_ui().lua_state(); state != nullptr) {
      const int top = lua_gettop(state);
      lua_getglobal(state, "CharacterNameText");
      if (lua_istable(state, -1) != 0) {
        lua_getfield(state, -1, "__ow_text");
        if (const char *const text = lua_tostring(state, -1); text != nullptr) {
          runtime_character_name = text;
        }
      }
      lua_settop(state, top);
    }
    state.world_ui_character_identity_ready =
        character_world_runtime_.session() != nullptr &&
        !character_world_runtime_.session()->pending_character_name().empty() &&
        runtime_character_name == character_world_runtime_.session()->pending_character_name();
    state.world_ui_character_runtime_name_length = runtime_character_name.size();
    state.world_ui_character_expected_name_length =
        character_world_runtime_.session() != nullptr
            ? character_world_runtime_.session()->pending_character_name().size()
            : 0u;
    auto &nameplates = game_loop_.world_scene().nameplate_renderer();
    state.nameplate_render_generation = nameplates.last_render_generation();
    state.visible_nameplates = nameplates.last_rendered_stock_plate_count();
    state.nameplate_pipeline_ready =
        nameplates.stock_visuals_ready() && state.frame_generation != 0u &&
        state.nameplate_render_generation == state.frame_generation &&
        nameplates.last_rendered_name_text_count() == state.visible_nameplates;
    state.forward_active =
        mover != nullptr &&
        (mover->GetMovementInfo().flags & openwow::game::kMoveFlagForward) != 0u;
    state.forward_start_packets_sent =
        successful_forward_start_packets_.load(std::memory_order_relaxed);
    state.movement_heartbeat_packets_sent =
        successful_movement_heartbeat_packets_.load(std::memory_order_relaxed);
    state.movement_stop_packets_sent =
        successful_movement_stop_packets_.load(std::memory_order_relaxed);
    if (mover != nullptr) {
      state.x = mover->GetX();
      state.y = mover->GetY();
      state.z = mover->GetZ();
    }
    return state;
  };
  scenario_ctx.exercise_world_ui = [this](const ScenarioWorldUiAction action) {
    return scenario_world_ui_driver_.Exercise(game_loop_.game_ui(), action);
  };
  scenario_ctx.dump_world_ui_json = [this](const std::uint32_t now, const int width,
                                           const int height) {
    const auto snapshot = openwow::ui::game::BuildWorldUiSnapshot(
        game_loop_.game_ui(), game_loop_.minimap(), width, height);
    return openwow::ui::game::SerializeWorldUiSnapshotJson(snapshot, now, width, height);
  };
  scenario_ctx.request_screenshot = [this](const std::filesystem::path &path) {
    return renderer_context_ != nullptr && renderer_context_->RequestScreenshot(path);
  };
  int width = 0;
  int height = 0;
  GetDrawableSize(window_, &width, &height);
  scenario_ctx.viewport_width = width;
  scenario_ctx.viewport_height = height;
  return scenario_runner_->Tick(stage, now_ms, &scenario_ctx);
}

int GlueClient::Run() {
  running_ = true;
  present_pacer_.Reset();
  stock_window_event_state_.Reset();
  auto &clock = openwow::core::GameClock::Instance();
  if (window_ != nullptr) {
    const Uint32 window_flags = SDL_GetWindowFlags(window_);
    window_focused_ = (window_flags & SDL_WINDOW_INPUT_FOCUS) != 0;
    stock_window_event_state_.SetMinimized((window_flags & SDL_WINDOW_MINIMIZED) != 0);
  }

  text_input_reactivation_pending_ = true;
  UpdateTextInputState();

  while (running_) {

    const double elapsed_sec = clock.Tick();
    const std::uint32_t now_ms =
        SDL_GetTicks();
    const std::uint32_t frame_delta_ms = static_cast<std::uint32_t>(clock.FrameDeltaMs());

    PumpPendingWindowEvents();
    if (!running_)
      break;

    if (debug_ui_control_adapter_) {
      debug_ui_control_adapter_->Pump(game_loop_.game_ui());
    }

    PumpGlueRequests(static_cast<float>(elapsed_sec));
    if (!running_)
      break;

    if (glue_load_.ok && glue_runtime_.IsAttachedToGlue()) {
      glue_runtime_.PumpVisibilityTransitions();

      UpdateTextInputState();
    }

    openwow::core::FrameScheduler::Instance().RunFrame(elapsed_sec);

    if (!TickScenario(ScenarioRunner::Stage::kPreRender, now_ms)) {
      running_ = false;
      break;
    }

    ReconcileWindowFocus();

    UpdateHoverState();

    RefreshLayout();
    game_loop_.SetScreenSize(std::max(1, layout_width_), std::max(1, layout_height_));
    if (renderer_context_ != nullptr) {
      const auto &cvars = openwow::ui::game::CVarSystem::Instance();
      auto presentation = BuildPresentationConfig(cvars);

      if (IsBenchmarkRun()) {
        presentation.vsync = false;
      }
      renderer_context_->SetPresentationConfig(presentation);
    }

    if (renderer_context_ != nullptr) {
      openwow::render::api::FrameInfo frame_info;
      frame_info.frame_number = clock.FrameCount();
      frame_info.delta_seconds = elapsed_sec;
      frame_info.absolute_seconds = clock.TotalElapsedSec();
      frame_info.backbuffer = {static_cast<std::uint32_t>(std::max(1, layout_width_)),
                               static_cast<std::uint32_t>(std::max(1, layout_height_))};
      renderer_context_->BeginFrame(frame_info);
    }

    UpdateOnUpdateScripts(elapsed_sec);

    const bool glue_character_scenes_active = mode_ != UiMode::kInWorld;
    if (glue_character_scenes_active) {
      glue_renderer_.BindAttachedCharacterScenes(
          game_state_.char_select_scene, game_state_.char_select_model_frame_name,
          game_state_.char_customize_scene, game_state_.char_customize_frame_name);
      background_controller_.SyncActiveCharacterScene(game_state_);
    }

    if (mode_ != UiMode::kInWorld) {
      glue_renderer_.TickStreaming(glue_widgets_);
    }
    const auto streaming = glue_renderer_.StreamingCounters();

    background_controller_.Update(static_cast<float>(elapsed_sec), game_state_, glue_widgets_,
                                  streaming, glue_character_scenes_active);

    SyncScreenModels(static_cast<float>(elapsed_sec));

    (void)openwow::ui::glue::ResolveGlueLayoutAndFontMetrics(
        &glue_widgets_, login_vfs_, glue_fonts_.has_value() ? &*glue_fonts_ : nullptr,
        layout_width_, layout_height_);
    DispatchPendingScrollRangeChangedEvents();

    Render(now_ms, frame_delta_ms, elapsed_sec);

    const bool scenario_should_continue = TickScenario(ScenarioRunner::Stage::kPostRender, now_ms);

    const bool benchmarking = IsBenchmarkRun();

    const auto pace = benchmarking
        ? openwow::render::PresentPacerDecision{}
        : present_pacer_.Schedule(openwow::ui::game::CVarSystem::Instance().GetCVarInt("maxFPS"),
                                openwow::ui::game::CVarSystem::Instance().GetCVarInt("maxFPSBk"),
                                window_focused_, openwow::core::GameClock::Now());
    if (pace.delay_ms != 0) {
      SDL_Delay(pace.delay_ms);
    }
    if (pace.target_fps.has_value()) {

      while (openwow::core::GameClock::Now() < pace.deadline) {
        std::this_thread::yield();
      }
    }

    if (renderer_context_ != nullptr) {
      renderer_context_->EndFrame();
    }
    DrainScreenshotNotifications();
    if (!scenario_should_continue) {
      running_ = false;
      break;
    }
  }

  return scenario_runner_.has_value() && scenario_runner_->failed() ? 1 : 0;
}

void GlueClient::Shutdown() {
  if (debug_ui_control_adapter_) {

    debug_ui_control_adapter_->Stop();
    game_loop_.game_ui().SetDebugSubmissionReceiptsEnabled(false);
  }
  if (debug_control_server_) {
    debug_control_server_->Stop();
    debug_control_server_.reset();
  }
  if (debug_ui_control_adapter_) {
    debug_ui_control_adapter_.reset();
  }
  if (!debug_control_endpoint_path_.empty()) {
    std::error_code ignored;
    std::filesystem::remove(debug_control_endpoint_path_, ignored);
    debug_control_endpoint_path_.clear();
  }
  openwow::ui::glue::LatestAgreementsService::Get().AbortAndReset();
  ReleaseInWorldMouseButtons();
  pending_window_events_.Clear();
  stock_window_event_state_.Reset();

  GlueFlowContext cancel_ctx;
  cancel_ctx.realm_session = &realm_runtime_.session;
  CancelGlueFlowNetworkOperations(cancel_ctx, glue_flow_state_);

  if (text_input_active_) {
    SDL_StopTextInput();
    text_input_active_ = false;
  }

  recv_thread_.Stop();

  auto &knowledge_base = openwow::game::KnowledgeBase::Get();
  knowledge_base.ShutdownRequestData();
  knowledge_base.ResetSystemMessages();
  openwow::game::LCD_Shutdown();

  glue_renderer_.Shutdown();
  game_loop_.Shutdown();

  openwow::net::SetClientServicesPacketSendFn({});
  character_world_runtime_.Destroy();

  realm_runtime_.session.Disconnect();

  openwow::net::ClientServices::Instance().FullLogout();
  openwow::core::LoginConsoleDiagnostics::Instance().Shutdown();
  openwow::core::ida::ConsoleAndFont_Shutdown();
  fps_overlay_text_renderer_.Shutdown();
  openwow::data::AsyncFile_Shutdown();
  db_cache_runtime_.DestroyBeforeWarden();
  openwow::game::WardenModuleCache_Destroy();
  db_cache_runtime_.DestroyAfterWarden();
  db_cache_runtime_.FinishShutdown();
  game_loop_.item_definitions().Clear();

  glue_host_.FinishAudioDevicePrepare();
  sound_runtime_.Shutdown(false);
  sound_runtime_.ShutdownOutputDevice();
  openwow::platform::SystemMouseSpeedController::Instance().RestoreSystemDefault();
  gamma_controller_.Shutdown(openwow::ui::game::CVarSystem::Instance());
  openwow::core::ida::ClearWindowResizeLockBoundWindow();

  if (renderer_context_ != nullptr) {
    game_loop_.SetRendererContext(nullptr);
    renderer_context_->Shutdown();
    renderer_context_.reset();
  }

  openwow::core::ida::CVar_Cleanup();
}

}

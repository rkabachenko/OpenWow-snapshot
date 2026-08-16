#include "openwow/game/game_loop.h"
#include "openwow/game/arena_system.h"
#include "openwow/game/movement_callbacks.h"
#include "openwow/game/currency_system.h"
#include "openwow/game/inebriation.h"
#include "openwow/game/pvp_info.h"
#include "openwow/game/reputation_info.h"
#include "openwow/game/spellbook_system.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/audio/playback/sound_settings.h"
#include "openwow/core/client_misc.h"
#include "openwow/core/init_subsystems.h"
#include "openwow/data/async_file_read.h"
#include "openwow/data/formats/dbc/dbc_enums.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/data/streaming_init.h"
#include "openwow/debug/inspection/render_debug.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/math/vec3_cross.h"
#include "openwow/foundation/math/vec3_negate.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/game/account_data.h"
#include "openwow/game/account_data_runtime_sync.h"
#include "openwow/game/account_msg.h"
#include "openwow/game/achievements/application/tracked_achievement_state.h"
#include "openwow/game/action_validation_utils.h"
#include "openwow/game/actions/bindings/adapters/lua/binding_command_executor.h"
#include "openwow/game/actions/bindings/adapters/persistence/binding_account_data_adapter.h"
#include "openwow/game/actions/bindings/adapters/retail/modified_click_adapter.h"
#include "openwow/game/actions/bindings/application/binding_profiles.h"
#include "openwow/game/actions/held_cursor/adapters/platform/cursor_surface.h"
#include "openwow/game/actions/macros/adapters/persistence/macro_account_data_adapter.h"
#include "openwow/game/actions/macros/adapters/retail/retail_macro_condition_adapter.h"
#include "openwow/game/actions/macros/adapters/retail/retail_macro_icon_path_adapter.h"
#include "openwow/game/actions/macros/adapters/retail/retail_macro_icon_resolution_adapter.h"
#include "openwow/game/actions/macros/adapters/ui/macro_action_bar_adapter.h"
#include "openwow/game/actions/macros/application/macro_catalog.h"
#include "openwow/game/barber_shop.h"
#include "openwow/game/battlefield_info.h"
#include "openwow/game/battlenet_api.h"
#include "openwow/game/c_input_control.h"
#include "openwow/game/chat_cache.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/collision_polygon.h"
#include "openwow/game/combat/death/adapters/ui/area_spirit_healer_controller.h"
#include "openwow/game/combat_log_messages.h"
#include "openwow/game/commentator_state.h"
#include "openwow/game/commerce/merchants/adapters/lua/merchant_lua_adapter.h"
#include "openwow/game/comsat_client.h"
#include "openwow/game/current_area_record.h"
#include "openwow/game/declined_words.h"
#include "openwow/game/event_scheduler.h"
#include "openwow/game/game_misc_utils.h"
#include "openwow/game/ground_walk.h"
#include "openwow/game/group_system.h"
#include "openwow/game/guild_system.h"
#include "openwow/game/inventory/adapters/ui/item_push_presenter.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/interaction_predicate.h"
#include "openwow/game/interaction_range.h"
#include "openwow/game/inventory/loot/loot_state.h"
#include "openwow/game/inventory/operations/inventory_commands.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/knowledge_base.h"
#include "openwow/game/lcd_system.h"
#include "openwow/game/loading_screen_world_entry_gate.h"
#include "openwow/game/localization.h"
#include "openwow/game/monster_move.h"
#include "openwow/game/name_validation.h"
#include "openwow/game/objects/cgcorpse.h"
#include "openwow/game/objects/cggameobject.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/player_area_tick.h"
#include "openwow/game/player_control_runtime.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/quest_log.h"
#include "openwow/game/quest_runtime_state.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_runtime_values.h"
#include "openwow/game/spell_target_resolver.h"
#include "openwow/game/spell_text_formatter.h"
#include "openwow/game/spell_validation.h"
#include "openwow/game/spell_visual_system.h"
#include "openwow/game/spells/spellbook/adapters/lua/spellbook_lua_api.h"
#include "openwow/game/talent_info.h"
#include "openwow/game/targeting/adapters/ui/unit_selection_color_query.h"
#include "openwow/render/api/packed_color.h"
#include "openwow/render/scene/selection_circle.h"
#include "openwow/render/scene/selection_decal_math.h"
#include "openwow/game/targeting/adapters/ui/world_click_controller.h"
#include "openwow/game/tutorial_system.h"
#include "openwow/game/unit_frame_data.h"
#include "openwow/game/unit_query_bridge.h"
#include "openwow/game/unit_sound_dispatch.h"
#include "openwow/game/update_fields.h"
#include "openwow/game/vehicle_system.h"
#include "openwow/game/world_environment_state.h"
#include "openwow/game/world_scene_state.h"
#include "openwow/game/world_screen_effects.h"
#include "openwow/game/world_session.h"
#include "openwow/game/world_ui_authority.h"
#include "openwow/input/input_manager.h"
#include "openwow/net/client_services.h"
#include "openwow/net/client_services_packet_sender.h"
#include "openwow/net/wotlk/addon_handshake.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/render/api/renderer_context.h"
#include "openwow/render/backend/bgfx/renderer_context_services.h"
#include "openwow/render/diagnostics/debug_draw_renderer.h"
#include "openwow/render/effects/postprocess/post_process.h"
#include "openwow/render/effects/spell_visuals/spell_visual_effects.h"
#include "openwow/render/effects/spell_visuals/spell_visual_renderer.h"
#include "openwow/render/m2/m2_resource_streamer.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/render/m2/m2_transparent_draw_order.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/render/scene/chat_bubble.h"
#include "openwow/render/scene/floating_text.h"
#include "openwow/render/scene/nameplate_renderer.h"
#include "openwow/render/world/doodads/doodad_renderer.h"
#include "openwow/render/scene/object_renderer.h"
#include "openwow/render/scene/shadow_presentation_policy.h"
#include "openwow/render/scene/world_frame.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/screens/loading_screen_update.h"
#include "openwow/ui/addons_data.h"
#include "openwow/ui/cursor_gx_system.h"
#include "openwow/ui/game/addon_runtime_loader.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/ui/game/runtime/world_lua_runtime.h"
#include "openwow/ui/game/runtime/world_ui_runtime_context.h"
#include "openwow/ui/game/api/game_lua_api_movement.h"
#include "openwow/ui/game/api/game_lua_api_tradeskill_state.h"
#include "openwow/ui/game/camera_lua_bindings.h"
#include "openwow/ui/game/capture_point_ui_manager.h"
#include "openwow/ui/game/chat_frame_manager.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/ui/game/game_ui_scale.h"
#include "openwow/ui/game/saved_variables.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/secure_execution.h"
#include "openwow/ui/game/tooltip_system.h"
#include "openwow/ui/game/ui_load_status_log.h"
#include "openwow/ui/lua_call_helpers.h"
#include "openwow/ui/runtime/security/protected_action_gate.h"
#include "openwow/ui/surfaces/game/bindings/world_ui_lifecycle_lua_adapter.h"
#include "openwow/ui/surfaces/game/runtime/corpse_position_query.h"
#include "openwow/ui/surfaces/game/runtime/npc_interaction_controller.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/surfaces/game/runtime/world_object_picker.h"
#include "openwow/world/environment/day_night.h"
#include "openwow/world/environment/sky.h"
#include "openwow/world/world_render_pipeline.h"
#include "openwow/render/api/math/render_matrix_math.h"
#include "openwow/render/world/environment/celestial_renderer.h"
#include "openwow/render/world/environment/sky_renderer.h"

#include "openwow/vfs/sfile_core.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

namespace openwow::game {

struct GameLoop::RenderResources {
  openwow::render::FloatingTextRenderer floating_text;
  openwow::render::PostProcess post_process;
  openwow::render::DebugDrawRenderer debug_draw_renderer;
};

namespace {

render::SkyRenderSettings ReadSkyRenderSettings() {
  const auto &cvars = openwow::ui::game::CVarSystem::Instance();
  return {
      .cloud_lod = render::ParseSkyCloudLodValue(cvars.GetCVar("SkyCloudLOD")),

      .sun_glare_enabled = openwow::game::DayNight_IsSunGlareEnabled(),
  };
}

namespace protected_action_kind = ui::game::protected_action_kind;

bool CanPerformMacroOperation(const actions::macros::MacroProtectedOperation operation) {
  switch (operation) {
  case actions::macros::MacroProtectedOperation::kExecuteCommands:
    return ui::game::GameUI_CanPerformProtectedAction(protected_action_kind::kMacroExecution) != 0;
  case actions::macros::MacroProtectedOperation::kModifyCatalog:
    return ui::game::GameUI_CanPerformProtectedAction(protected_action_kind::kMacroCatalog) != 0;
  }
  return false;
}

bool CanPerformBindingOperation(const BindingProtectedOperation) {
  return ui::game::GameUI_CanPerformProtectedAction(protected_action_kind::kKeyBinding) != 0;
}

bool CanPerformInventoryMutation() {
  return ui::game::GameUI_CanPerformProtectedAction(protected_action_kind::kItemEquip) != 0;
}

constexpr float kWorldCameraNearClip = openwow::world::kWorldCameraNearClipDistance;
constexpr float kWorldCameraFallbackFarClip = 350.0f;
constexpr std::uint8_t kStandStateSit = 1;
constexpr std::uint8_t kWorldSceneOpaqueViewCount = 5;

constexpr std::uint8_t kWorldSceneAlphaViewCount = 4;
constexpr std::uint8_t kWorldWaterViewCount = 1;
constexpr std::uint8_t kWorldParticleViewCount = 3;

constexpr std::uint8_t kWorldPostProcessViewCount = 5;

float ResolveWorldCameraFarClip() {
  const auto &cvars = openwow::ui::game::CVarSystem::Instance();
  const float far_clip =
      cvars.Exists("farclip") ? cvars.GetCVarFloat("farclip") : kWorldCameraFallbackFarClip;
  return std::isfinite(far_clip) && far_clip > kWorldCameraNearClip ? far_clip
                                                                    : kWorldCameraFallbackFarClip;
}

constexpr std::uint8_t kWorldUiOffscreenViewCount = 128;
constexpr std::uint8_t kWorldUiOverlayViewCount = 8;
constexpr std::uint8_t kWorldUiViewCount = kWorldUiOffscreenViewCount + kWorldUiOverlayViewCount;
constexpr std::uint8_t kFallbackPostProcessView = 1 + 1 + 1 + kWorldSceneOpaqueViewCount +
                                                  kWorldSceneAlphaViewCount + kWorldWaterViewCount +
                                                  kWorldParticleViewCount;
constexpr std::uint8_t kFallbackWorldUiBaseView =
    kFallbackPostProcessView + kWorldPostProcessViewCount;
constexpr char kBillingNagDialogEvent[] = "BILLING_NAG_DIALOG";
constexpr char kIgrBillingNagDialogEvent[] = "IGR_BILLING_NAG_DIALOG";
constexpr std::string_view kActionButtonCommandPrefix = "ACTIONBUTTON";
constexpr std::string_view kMultiActionBarCommandPrefix = "MULTIACTIONBAR";
constexpr std::string_view kMultiActionBarButtonSeparator = "BUTTON";
constexpr std::array<std::string_view, 5> kMultiActionBarFramePrefixes = {
    "",
    "MultiBarBottomLeftButton",
    "MultiBarBottomRightButton",
    "MultiBarRightButton",
    "MultiBarLeftButton",
};

struct MovementBindingLuaCall {
  const char *on_down{nullptr};
  const char *on_up{nullptr};
};

std::optional<MovementBindingLuaCall>
ResolveMovementBindingLuaCall(const std::string_view command) {
  namespace BA = BindingAction;
  if (command == BA::kMoveForward) {
    return MovementBindingLuaCall{"MoveForwardStart", "MoveForwardStop"};
  }
  if (command == BA::kMoveBackward) {
    return MovementBindingLuaCall{"MoveBackwardStart", "MoveBackwardStop"};
  }
  if (command == BA::kTurnLeft) {
    return MovementBindingLuaCall{"TurnLeftStart", "TurnLeftStop"};
  }
  if (command == BA::kTurnRight) {
    return MovementBindingLuaCall{"TurnRightStart", "TurnRightStop"};
  }
  if (command == BA::kStrafeLeft) {
    return MovementBindingLuaCall{"StrafeLeftStart", "StrafeLeftStop"};
  }
  if (command == BA::kStrafeRight) {
    return MovementBindingLuaCall{"StrafeRightStart", "StrafeRightStop"};
  }
  if (command == BA::kPitchUp) {
    return MovementBindingLuaCall{"PitchUpStart", "PitchUpStop"};
  }
  if (command == BA::kPitchDown) {
    return MovementBindingLuaCall{"PitchDownStart", "PitchDownStop"};
  }
  if (command == BA::kJump) {
    return MovementBindingLuaCall{"JumpOrAscendStart", "AscendStop"};
  }
  if (command == BA::kSitStand) {
    return MovementBindingLuaCall{"SitStandOrDescendStart", "DescendStop"};
  }
  if (command == BA::kToggleAutoRun) {
    return MovementBindingLuaCall{"ToggleAutoRun", nullptr};
  }
  if (command == BA::kToggleRun) {
    return MovementBindingLuaCall{"ToggleRun", nullptr};
  }
  return std::nullopt;
}

render::PostProcessSettings ReadPostProcessSettings() {
  const auto &cvars = openwow::ui::game::CVarSystem::Instance();
  const auto enabled = [&cvars](const char *name) {
    return !cvars.Exists(name) || cvars.GetCVarBool(name);
  };
  return {
      .enabled = enabled("ffx"),
      .glow_enabled = enabled("ffxGlow"),
      .death_enabled = enabled("ffxDeath"),
      .rectangle_textures = enabled("ffxRectangle"),
      .multisample =
          static_cast<std::uint8_t>(std::clamp(cvars.GetCVarInt("gxMultisample"), 1, 16)),
  };
}

float ReadM2ParticleDensity() {
  const auto &cvars = openwow::ui::game::CVarSystem::Instance();
  return std::clamp(cvars.GetCVarFloat("particleDensity"), 0.1f, 1.0f);
}

float ReadWeatherParticleDensity() {
  const auto &cvars = openwow::ui::game::CVarSystem::Instance();
  constexpr std::array<float, 4> kRetailWeatherDensityScale{0.1f, 0.33f, 0.66f, 1.0f};
  const auto weather_detail =
      static_cast<std::size_t>(std::clamp(cvars.GetCVarInt("weatherDensity"), 0, 3));
  return kRetailWeatherDensityScale[weather_detail];
}

bool ReadUseWeatherShaders() {
  const auto &cvars = openwow::ui::game::CVarSystem::Instance();
  return cvars.GetCVarInt("useWeatherShaders") != 0;
}

bool DispatchMovementBindingThroughLua(lua_State *state, const std::string_view command,
                                       const bool key_down) {
  const auto call = ResolveMovementBindingLuaCall(command);
  if (!call.has_value()) {
    return false;
  }

  const char *function_name = key_down ? call->on_down : call->on_up;
  if (function_name != nullptr && !openwow::ui::CallLuaGlobalIfFunction(state, function_name)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                              "BindingAssignment: movement Lua function unavailable: " +
                                  std::string(function_name));
  }
  return true;
}

bool StartsWith(const std::string_view value, const std::string_view prefix) {
  return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::optional<int> ParsePositiveInt(const std::string_view text) {
  if (text.empty()) {
    return std::nullopt;
  }

  int value = 0;
  const auto *begin = text.data();
  const auto *end = begin + text.size();
  const auto result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{} || result.ptr != end || value <= 0) {
    return std::nullopt;
  }
  return value;
}

void ClickLuaFrame(lua_State *state, const std::string_view frame_name) {
  const std::string global_name(frame_name);
  if (!openwow::ui::CallLuaGlobalMethodIfFunction(state, global_name.c_str(), "Click",
                                                  "LeftButton")) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug,
                              "BindingAssignment: Lua action frame unavailable: " + global_name);
  }
}

bool DispatchActionButtonBinding(lua_State *state, const std::string_view command) {
  if (!StartsWith(command, kActionButtonCommandPrefix)) {
    return false;
  }

  const auto slot = ParsePositiveInt(command.substr(kActionButtonCommandPrefix.size()));
  if (!slot.has_value() || *slot > 12) {
    return true;
  }

  ClickLuaFrame(state, "ActionButton" + std::to_string(*slot));
  return true;
}

bool DispatchMultiActionBarBinding(lua_State *state, const std::string_view command) {
  if (!StartsWith(command, kMultiActionBarCommandPrefix)) {
    return false;
  }

  const std::string_view tail = command.substr(kMultiActionBarCommandPrefix.size());
  const std::size_t separator = tail.find(kMultiActionBarButtonSeparator);
  if (separator == std::string_view::npos) {
    return true;
  }

  const auto bar = ParsePositiveInt(tail.substr(0, separator));
  const auto button =
      ParsePositiveInt(tail.substr(separator + kMultiActionBarButtonSeparator.size()));
  if (!bar.has_value() || !button.has_value() ||
      static_cast<std::size_t>(*bar) >= kMultiActionBarFramePrefixes.size() || *button > 12) {
    return true;
  }

  ClickLuaFrame(state, std::string(kMultiActionBarFramePrefixes[*bar]) + std::to_string(*button));
  return true;
}

void OpenChatEditBox(lua_State *state, const char *initial_text) {
  constexpr const char *kChatEditBoxGlobal = "ChatFrame1EditBox";
  (void)openwow::ui::CallLuaGlobalMethodIfFunction(state, kChatEditBoxGlobal, "Show");
  (void)openwow::ui::CallLuaGlobalMethodIfFunction(state, kChatEditBoxGlobal, "SetFocus");
  if (initial_text != nullptr) {
    (void)openwow::ui::CallLuaGlobalMethodIfFunction(state, kChatEditBoxGlobal, "SetText",
                                                     initial_text);
  }
}

std::uint8_t ClampViewId(const std::uint16_t view_id, const std::uint8_t fallback) {
  if (view_id > std::numeric_limits<std::uint8_t>::max()) {
    return fallback;
  }
  return static_cast<std::uint8_t>(view_id);
}

std::uint8_t OffsetViewId(const std::uint8_t base, const std::uint8_t offset) {
  if (base > static_cast<std::uint8_t>(std::numeric_limits<std::uint8_t>::max() - offset)) {
    return base;
  }
  return static_cast<std::uint8_t>(base + offset);
}

void BuildWorldFrameGraph(openwow::render::api::FrameGraph &graph, std::uint16_t width,
                          std::uint16_t height) {
  namespace rapi = openwow::render::api;

  const rapi::RenderExtent extent{width, height};
  graph.Reset();
  graph.AddPass(rapi::FrameGraphPassId::ShadowDepth, extent);
  graph.AddPass(rapi::FrameGraphPassId::Reflection, extent);
  graph.AddPass(rapi::FrameGraphPassId::Refraction, extent);
  graph.AddPass(rapi::FrameGraphPassId::SceneOpaque, extent, kWorldSceneOpaqueViewCount);
  graph.AddPass(rapi::FrameGraphPassId::SceneAlpha, extent, kWorldSceneAlphaViewCount);
  graph.AddPass(rapi::FrameGraphPassId::Water, extent, kWorldWaterViewCount);
  graph.AddPass(rapi::FrameGraphPassId::Particles, extent, kWorldParticleViewCount);
  graph.AddPass(rapi::FrameGraphPassId::PostProcess, extent, kWorldPostProcessViewCount);
  graph.AddPass(rapi::FrameGraphPassId::WorldUi, extent, kWorldUiViewCount);
  graph.AddPass(rapi::FrameGraphPassId::DebugOverlay, extent);
  graph.AddPass(rapi::FrameGraphPassId::Present, extent);
}

std::uint8_t ResolveFrameGraphView(const openwow::render::api::RendererContext *renderer_context,
                                   const openwow::render::api::FrameGraphPassId pass,
                                   const std::uint16_t offset, const std::uint8_t fallback) {
  if (renderer_context == nullptr) {
    return fallback;
  }

  const auto *frame_pass = renderer_context->Graph().FindPass(pass);
  if (frame_pass == nullptr) {
    return fallback;
  }
  return ClampViewId(static_cast<std::uint16_t>(frame_pass->view_id + offset), fallback);
}

std::uint8_t ResolveFrameGraphView(const openwow::render::api::RendererContext *renderer_context,
                                   const openwow::render::api::FrameGraphPassId pass,
                                   const std::uint8_t fallback) {
  return ResolveFrameGraphView(renderer_context, pass, 0, fallback);
}

std::uint8_t ResolveWorldUiBaseView(const openwow::render::api::RendererContext *renderer_context) {
  return ResolveFrameGraphView(
      renderer_context, openwow::render::api::FrameGraphPassId::WorldUi, kWorldUiOffscreenViewCount,
      static_cast<std::uint8_t>(kFallbackWorldUiBaseView + kWorldUiOffscreenViewCount));
}

std::uint8_t
ResolveWorldUiOffscreenView(const openwow::render::api::RendererContext *renderer_context,
                            const std::uint16_t offset = 0u) {
  return ResolveFrameGraphView(renderer_context, openwow::render::api::FrameGraphPassId::WorldUi,
                               offset,
                               static_cast<std::uint8_t>(kFallbackWorldUiBaseView + offset));
}

std::uint8_t
ResolveDebugOverlayView(const openwow::render::api::RendererContext *renderer_context) {
  return ResolveFrameGraphView(
      renderer_context, openwow::render::api::FrameGraphPassId::DebugOverlay,
      static_cast<std::uint8_t>(kFallbackWorldUiBaseView + kWorldUiViewCount));
}

std::optional<std::uint8_t>
BuildBlockingLoadingFrameGraph(openwow::render::api::RendererContext *renderer_context,
                               const std::uint16_t width, const std::uint16_t height) {
  if (renderer_context == nullptr) {
    return std::nullopt;
  }

  namespace rapi = openwow::render::api;
  auto &graph = renderer_context->Graph();
  const rapi::RenderExtent extent{width, height};
  graph.Reset();
  const auto clear = graph.AddPass(rapi::FrameGraphPassId::SceneOpaque, extent);
  graph.AddPass(rapi::FrameGraphPassId::WorldUi, extent, kWorldUiViewCount);
  graph.AddPass(rapi::FrameGraphPassId::DebugOverlay, extent);
  graph.AddPass(rapi::FrameGraphPassId::Present, extent);

  if (clear.view_id > std::numeric_limits<std::uint8_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::uint8_t>(clear.view_id);
}

WorldSceneRenderViews
ResolveWorldSceneRenderViews(const openwow::render::api::RendererContext *renderer_context) {
  namespace rapi = openwow::render::api;
  const std::uint8_t scene_opaque =
      ResolveFrameGraphView(renderer_context, rapi::FrameGraphPassId::SceneOpaque, 3);
  const std::uint8_t scene_alpha =
      ResolveFrameGraphView(renderer_context, rapi::FrameGraphPassId::SceneAlpha,
                            static_cast<std::uint8_t>(scene_opaque + kWorldSceneOpaqueViewCount));
  const std::uint8_t particle_base = ResolveFrameGraphView(
      renderer_context, rapi::FrameGraphPassId::Particles,
      static_cast<std::uint8_t>(scene_alpha + kWorldSceneAlphaViewCount + kWorldWaterViewCount));
  const std::uint8_t world_ui = ResolveWorldUiBaseView(renderer_context);

  return WorldSceneRenderViews{
      .world =
          openwow::render::WorldRenderViews{
              .shadow =
                  ResolveFrameGraphView(renderer_context, rapi::FrameGraphPassId::ShadowDepth, 0),
              .sky = ResolveFrameGraphView(renderer_context, rapi::FrameGraphPassId::SceneOpaque, 0,
                                           scene_opaque),
              .scene = ResolveFrameGraphView(renderer_context, rapi::FrameGraphPassId::SceneOpaque,
                                             1, OffsetViewId(scene_opaque, 1)),
              .wmo = ResolveFrameGraphView(renderer_context, rapi::FrameGraphPassId::SceneOpaque, 2,
                                           OffsetViewId(scene_opaque, 2)),
              .alpha = scene_alpha,
              .reflection =
                  ResolveFrameGraphView(renderer_context, rapi::FrameGraphPassId::Reflection, 1),
              .refraction =
                  ResolveFrameGraphView(renderer_context, rapi::FrameGraphPassId::Refraction, 2),
              .water = ResolveFrameGraphView(
                  renderer_context, rapi::FrameGraphPassId::Water, 0,
                  static_cast<std::uint8_t>(scene_alpha + kWorldSceneAlphaViewCount)),
              .weather = ResolveFrameGraphView(renderer_context, rapi::FrameGraphPassId::Particles,
                                               0, particle_base),
          },
      .blob_shadows = ResolveFrameGraphView(renderer_context, rapi::FrameGraphPassId::SceneOpaque,
                                            3, OffsetViewId(scene_opaque, 3)),
      .objects = ResolveFrameGraphView(renderer_context, rapi::FrameGraphPassId::SceneOpaque, 4,
                                       OffsetViewId(scene_opaque, 4)),
      .mounts = ResolveFrameGraphView(renderer_context, rapi::FrameGraphPassId::SceneAlpha, 1,
                                      OffsetViewId(scene_alpha, 1)),
      .particles = ResolveFrameGraphView(renderer_context, rapi::FrameGraphPassId::Particles, 1,
                                         OffsetViewId(particle_base, 1)),
      .selection_circle = ResolveFrameGraphView(
          renderer_context, rapi::FrameGraphPassId::SceneAlpha, 2, OffsetViewId(scene_alpha, 2)),
      .water_particulates = ResolveFrameGraphView(
          renderer_context, rapi::FrameGraphPassId::Particles, 2, OffsetViewId(particle_base, 2)),
      .unit_names = ResolveFrameGraphView(renderer_context, rapi::FrameGraphPassId::SceneAlpha, 3,
                                          OffsetViewId(scene_alpha, 3)),
      .nameplates = world_ui,
  };
}

bool ResolveVehicleDescriptorRenderReady(const CGUnit_C &unit, void *context) {
  auto *const loop = static_cast<GameLoop *>(context);
  return loop != nullptr &&
         loop->world_scene().object_renderer().IsRuntimeRenderAssetReady(unit.GetGuid());
}

void RefreshExactUnitBoundsFromRenderer(ObjectManager &objects) {
  objects.ForEachUnit(
      [](const ObjectGuid &, CGUnit_C &unit) { unit.Presentation().UpdateModelTransform(false); });
  objects.ForEachPlayer([](const ObjectGuid &, CGPlayer_C &player) {
    player.Presentation().UpdateModelTransform(false);
  });
}

float ResolvePlayerAnimationProgress(const CGPlayer_C &player, void *context) {
  auto *const loop = static_cast<GameLoop *>(context);
  if (loop == nullptr) {
    return 0.0f;
  }

  return loop->world_scene().object_renderer().GetAnimationProgress(player.GetGuid());
}

std::array<std::uint8_t, 13>
BuildWorldSceneFramebufferViewList(const WorldSceneRenderViews &views) {
  return {
      views.world.sky,   views.world.scene,   views.world.wmo,        views.world.alpha,
      views.world.water, views.world.weather, views.blob_shadows,     views.objects,
      views.mounts,      views.particles,     views.selection_circle, views.water_particulates,

      views.unit_names,
  };
}

std::optional<float> ResolveCorpseSupportSurfaceHeight(const CGCorpse_C &corpse, void *context) {
  auto *const loop = static_cast<GameLoop *>(context);
  if (loop == nullptr) {
    return std::nullopt;
  }

  const auto position = corpse.GetPosition();
  return loop->world_scene().collision().GetGroundHeight(position.x, position.y, position.z);
}

bool ResolveCorpseCharacterAppearanceReady(const CGCorpse_C &corpse, void *context) {
  auto *const loop = static_cast<GameLoop *>(context);
  return loop != nullptr &&
         loop->world_scene().object_renderer().IsCharacterAppearancePrepared(corpse.GetGuid());
}

std::optional<float> ResolveJumpLiquidSurfaceHeight(const CGUnit_C &unit,
                                                    void *context) {
  auto *const loop = static_cast<GameLoop *>(context);
  if (loop == nullptr) {
    return std::nullopt;
  }

  const auto position = unit.GetPosition();
  return loop->world_scene().world_map().GetLiquidSurfaceHeightAtPosition(
      position.x, position.y, position.z);
}

void ResolveWaterRippleSpawn(const UnitWaterRippleSpawn& spawn,
                             void* context) {
  auto* const loop = static_cast<GameLoop*>(context);
  if (loop == nullptr) {
    return;
  }
  loop->world_scene().world_map().QueueWaterRipplePresentation(
      world::SpawnWaterRippleCommand{
          .position = spawn.position,
          .rotation_radians = spawn.rotation_radians,
          .initial_extent = spawn.initial_extent,
          .duration_seconds = spawn.duration_seconds,
          .opacity_base = spawn.opacity_base,
          .extent_rate = spawn.extent_rate,
          .use_splash_texture = spawn.use_splash_texture,
          .use_local_player_pool = spawn.use_local_player_pool,
      });
}

std::optional<TrajectoryPoint> IntersectMissileWorldSegment(const GameLoop &loop,
                                                            const TrajectoryPoint &start,
                                                            const TrajectoryPoint &end) {
  const float delta_x = end.x - start.x;
  const float delta_y = end.y - start.y;
  const float delta_z = end.z - start.z;
  const float segment_length = std::sqrt(delta_x * delta_x + delta_y * delta_y + delta_z * delta_z);
  if (!std::isfinite(segment_length) || segment_length <= 0.0f) {
    return std::nullopt;
  }

  const auto hit = loop.world_scene().collision().Raycast(
      start.x, start.y, start.z, delta_x / segment_length, delta_y / segment_length,
      delta_z / segment_length, segment_length);
  if (!hit.has_value()) {
    return std::nullopt;
  }
  return TrajectoryPoint{hit->x, hit->y, hit->z};
}

CalcGroundPosCollisionResult ResolveCalcGroundPos(
    const CGUnit_C &unit, const std::array<float, 3> &origin,
    const float max_distance, void *context) {
  auto *const loop = static_cast<GameLoop *>(context);
  if (loop == nullptr) {
    return {};
  }
  return ResolveCalcGroundPosForLoop(*loop, unit, origin, max_distance);
}

bool ResolveUnitCollisionAabb(const float *const aabb, void *context) {
  auto *const loop = static_cast<GameLoop *>(context);
  if (loop == nullptr || aabb == nullptr) {
    return false;
  }

  bool intersects = false;
  loop->world_scene().collision().VisitFacets(
      aabb[0], aabb[3], aabb[1], aabb[4], aabb[2], aabb[5],
      [&](const openwow::world::CollisionFacetView &facet) {
        if (intersects) {
          return;
        }
        ClippedPolygon polygon{};
        polygon.count = 3;
        polygon.tags[0] = -1;
        polygon.tags[1] = -1;
        polygon.tags[2] = -1;
        for (std::size_t vertex = 0; vertex < 3u; ++vertex) {
          for (std::size_t axis = 0; axis < 3u; ++axis) {
            polygon.vertices[vertex * 3u + axis] = facet.vertices[vertex][axis];
          }
        }
        intersects = ClipPolygonToAABB(polygon, aabb) != 0;
      });
  return intersects;
}

UnitSoundGroundState ResolveUnitSoundGroundState(const CGUnit_C &unit,
                                                 const float *const event_position, void *context) {
  UnitSoundGroundState result;
  auto *const loop = static_cast<GameLoop *>(context);
  if (loop == nullptr) {
    return result;
  }
  return ResolveUnitSoundGroundStateForLoop(*loop, unit, event_position);
}

}

UnitSoundGroundState ResolveUnitSoundGroundStateForLoop(
    GameLoop &loop, const CGUnit_C &unit,
    const float *const event_position) {
  UnitSoundGroundState result;

  const Position unit_position = unit.GetPosition();
  const float x = event_position != nullptr ? event_position[0] : unit_position.x;
  const float y = event_position != nullptr ? event_position[1] : unit_position.y;
  const float z = event_position != nullptr ? event_position[2] : unit_position.z;
  auto &world = loop.world_scene().world_map();

  const std::uint64_t terrain_revision =
      loop.world_scene().collision().FacetRevision();
  const std::uint64_t area_generation =
      loop.world_scene().world_map().MovementCollisionFacetRevision();
  const std::uint64_t world_token =
      terrain_revision * 0x9e3779b97f4a7c15ull ^
      (area_generation + 0x517cc1b727220a95ull + (terrain_revision << 6u) +
       (terrain_revision >> 2u));
  const std::uint64_t guid = unit.GetGuid().GetRawValue();
  auto &memo = loop.unit_ground_state_memo_;
  if (const auto it = memo.find(guid);
      it != memo.end() && it->second.world_token == world_token &&
      it->second.x == x && it->second.y == y && it->second.z == z) {
    return it->second.state;
  }

  result.terrain_type_id = world.ResolveTerrainGroundTypeAtPosition(x, y);

  auto &collision = loop.world_scene().collision();
  if (const auto ground = collision.GetGroundHeight(x, y, z);
      ground.has_value()) {
    result.has_ground_surface = true;
    result.ground_surface_z = *ground;
  }
  if (const auto hit = collision.Raycast(x, y, z + 1.0f,
                                         0.0f, 0.0f, -1.0f, 2.0f);
      hit.has_value()) {
    result.has_vertical_clearance = true;
    result.vertical_clearance = z + 1.0f - hit->z;
  }

  const auto liquid_surface = world.QueryLiquidSurfaceAtPosition(x, y, z);
  if (liquid_surface.has_value()) {
    result.has_liquid_surface = true;
    result.liquid_surface_z = liquid_surface->surface_height;

    const float type_probe_z =
        std::min(z, liquid_surface->surface_height - 0.01f);
    result.liquid_type_id =
        type_probe_z == z ? liquid_surface->liquid_type_id
                          : world.GetUnderwaterLiquidTypeId(x, y, type_probe_z);
  }

  constexpr std::size_t kUnitGroundStateMemoLimit = 4096u;
  if (memo.size() >= kUnitGroundStateMemoLimit) {
    memo.clear();
  }
  memo.insert_or_assign(
      guid, GameLoop::UnitGroundStateMemo{
                .x = x, .y = y, .z = z, .world_token = world_token,
                .state = result});
  return result;
}

CalcGroundPosCollisionResult ResolveCalcGroundPosForLoop(
    GameLoop &loop, const CGUnit_C &unit, const std::array<float, 3> &origin,
    const float max_distance) {
  const auto &collision = loop.world_scene().collision();

  const std::uint64_t facet_revision = collision.FacetRevision();
  const std::uint64_t guid = unit.GetGuid().GetRawValue();
  auto &memo = loop.ground_contact_probe_memo_;
  if (const auto it = memo.find(guid);
      it != memo.end() && it->second.facet_revision == facet_revision &&
      std::memcmp(it->second.origin.data(), origin.data(),
                  sizeof(origin)) == 0 &&
      std::memcmp(&it->second.max_distance, &max_distance,
                  sizeof(max_distance)) == 0) {
    return it->second.result;
  }

  CalcGroundPosCollisionResult result;

  const auto ground = collision.Raycast(origin[0], origin[1], origin[2],
                                        0.0f, 0.0f, -1.0f, max_distance);
  if (ground.has_value()) {
    result.hit = true;
    result.ground_z = ground->z;
    result.normal_x = ground->normal[0];
    result.normal_y = ground->normal[1];
    result.normal_z = ground->normal[2];
  }

  constexpr std::size_t kGroundContactProbeMemoLimit = 4096u;
  if (memo.size() >= kGroundContactProbeMemoLimit) {
    memo.clear();
  }
  memo.insert_or_assign(guid, GameLoop::GroundContactProbeMemo{
                                  .origin = origin,
                                  .max_distance = max_distance,
                                  .facet_revision = facet_revision,
                                  .result = result});
  return result;
}

namespace {

bool ActivePlayerCanReceiveResurrectRequestEvent(const WorldSession &session) {
  const auto *const local_player = session.objects().GetLocalPlayerTyped();
  return local_player != nullptr && local_player->State().IsDeadOrGhost();
}

void RefreshCombatBindingState(openwow::game::BindingProfiles &key_bindings,
                               const WorldSession &session, const TargetingSystem &targeting) {

  const auto *const local_player = session.objects().GetLocalPlayerTyped();
  const bool use_combat_binding_slot =
      (local_player != nullptr && local_player->State().IsInCombat()) ||
      targeting.IsAttackActive() || targeting.IsAttackSwingActive();
  key_bindings.SetCurrentBindingStateBit(1, use_combat_binding_slot);
}

constexpr std::uint32_t kUnavailableCursorTypeOffset = 26u;
constexpr std::uint32_t kCursorTypeBuy = 3u;
constexpr std::uint32_t kCursorTypeAttack = 4u;
constexpr std::uint32_t kCursorTypeInteract = 5u;
constexpr std::uint32_t kCursorTypeSpeak = 6u;
constexpr std::uint32_t kCursorTypePickup = 8u;
constexpr std::uint32_t kCursorTypeTaxi = 9u;
constexpr std::uint32_t kCursorTypeTrainer = 10u;
constexpr std::uint32_t kCursorTypeMine = 11u;
constexpr std::uint32_t kCursorTypeSkin = 12u;
constexpr std::uint32_t kCursorTypeGatherHerbs = 13u;
constexpr std::uint32_t kCursorTypeLootAll = 16u;
constexpr std::uint32_t kCursorTypeRepairNpc = 18u;
constexpr std::uint32_t kCursorTypeSkinHorde = 20u;
constexpr std::uint32_t kCursorTypeSkinAlliance = 21u;
constexpr std::uint32_t kCursorTypeInnkeeper = 22u;
constexpr std::uint32_t kCursorTypeVehicle = 26u;

constexpr std::uint32_t kCursorNpcFlagGossip = 0x00000001u;
constexpr std::uint32_t kCursorNpcFlagTrainer = 0x00000010u;
constexpr std::uint32_t kCursorNpcFlagVendor = 0x00000080u;
constexpr std::uint32_t kCursorNpcFlagRepair = 0x00001000u;
constexpr std::uint32_t kCursorNpcFlagFlightMaster = 0x00002000u;
constexpr std::uint32_t kCursorNpcFlagSpiritHealer = 0x00004000u;
constexpr std::uint32_t kCursorNpcFlagSpiritGuide = 0x00008000u;
constexpr std::uint32_t kCursorNpcFlagInnkeeper = 0x00010000u;
constexpr std::uint32_t kCursorNpcFlagBanker = 0x00020000u;
constexpr std::uint32_t kCursorNpcFlagPetitioner = 0x00040000u;
constexpr std::uint32_t kCursorNpcFlagTabardDesigner = 0x00080000u;
constexpr std::uint32_t kCursorNpcFlagBattlemaster = 0x00100000u;
constexpr std::uint32_t kCursorNpcFlagAuctioneer = 0x00200000u;
constexpr std::uint32_t kCursorNpcFlagStableMaster = 0x00400000u;
constexpr std::uint32_t kCursorNpcFlagGuildBanker = 0x00800000u;
constexpr std::uint32_t kCursorNpcFlagSpellClick = 0x01000000u;

constexpr float kNpcServiceCursorReachPadding = 4.0f;

[[nodiscard]] std::uint32_t ApplyUnavailableCursorOffset(const std::uint32_t base_type,
                                                         const bool unavailable) {
  return unavailable ? base_type + kUnavailableCursorTypeOffset : base_type;
}

[[nodiscard]] bool IsMouseoverNpcServiceOutOfRange(const CGPlayer_C &active_player,
                                                   const CGUnit_C &unit) {
  const float reach = unit.State().GetCombatReach() + kNpcServiceCursorReachPadding;
  const float distance = active_player.GetDistance(unit);
  return distance * distance > reach * reach;
}

constexpr std::uint32_t kFactionGroupBitAlliance = 0x2u;
constexpr std::uint32_t kFactionGroupBitHorde = 0x4u;

enum class RetailTeamIndex : int { kUnknown = -1, kHorde = 0, kAlliance = 1 };

[[nodiscard]] RetailTeamIndex ResolveRetailTeamIndex(const WorldSession &session,
                                                     const CGUnit_C &unit) {
  const auto *const dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return RetailTeamIndex::kUnknown;
  }
  const auto *const race = dbc->chr_races().LookupEntry(unit.State().GetRace());
  if (race == nullptr || race->faction_id == 0u) {
    return RetailTeamIndex::kUnknown;
  }
  const auto *const faction_template = dbc->faction_template().LookupEntry(race->faction_id);
  if (faction_template == nullptr) {
    return RetailTeamIndex::kUnknown;
  }
  if ((faction_template->faction_group & kFactionGroupBitHorde) != 0u) {
    return RetailTeamIndex::kHorde;
  }
  if ((faction_template->faction_group & kFactionGroupBitAlliance) != 0u) {
    return RetailTeamIndex::kAlliance;
  }
  return RetailTeamIndex::kUnknown;
}

[[nodiscard]] std::uint32_t ResolvePlayerCorpseSkinCursorType(const RetailTeamIndex team,
                                                              const bool out_of_range) {

  const std::uint32_t base_type =
      team == RetailTeamIndex::kHorde ? kCursorTypeSkinAlliance : kCursorTypeSkinHorde;
  if (team == RetailTeamIndex::kUnknown) {
    return 0u;
  }
  return ApplyUnavailableCursorOffset(base_type, out_of_range);
}

[[nodiscard]] std::uint32_t ResolveMouseoverQuestGiverCursorType(const WorldSession &session,
                                                                 const CGUnit_C &unit,
                                                                 const bool out_of_range) {
  const auto status = session.quests().FindQuestGiverStatus(unit.GetGuid());
  if (!status.has_value()) {
    return 0u;
  }
  return ResolveQuestGiverRetailCursorType(*status, out_of_range);
}

[[nodiscard]] std::uint32_t ResolveMouseoverNpcServiceCursorType(const std::uint32_t npc_flags,
                                                                 const bool out_of_range) {
  const auto with_offset = [out_of_range](const std::uint32_t base_type) {
    return ApplyUnavailableCursorOffset(base_type, out_of_range);
  };

  if ((npc_flags & kCursorNpcFlagRepair) != 0u) {
    return with_offset(kCursorTypeRepairNpc);
  }
  if ((npc_flags & kCursorNpcFlagInnkeeper) != 0u) {
    return with_offset(kCursorTypeInnkeeper);
  }
  if ((npc_flags & kCursorNpcFlagFlightMaster) != 0u) {
    return with_offset(kCursorTypeTaxi);
  }
  if ((npc_flags & kCursorNpcFlagTrainer) != 0u) {
    return with_offset(kCursorTypeTrainer);
  }
  if ((npc_flags & (kCursorNpcFlagSpiritHealer | kCursorNpcFlagSpiritGuide)) != 0u) {
    return with_offset(kCursorTypeSpeak);
  }
  if ((npc_flags & (kCursorNpcFlagBanker | kCursorNpcFlagGuildBanker)) != 0u) {
    return with_offset(kCursorTypeBuy);
  }
  if ((npc_flags & (kCursorNpcFlagPetitioner | kCursorNpcFlagTabardDesigner |
                    kCursorNpcFlagBattlemaster)) != 0u) {
    return with_offset(kCursorTypeSpeak);
  }
  if ((npc_flags & kCursorNpcFlagAuctioneer) != 0u) {
    return with_offset(kCursorTypeBuy);
  }
  if ((npc_flags & kCursorNpcFlagStableMaster) != 0u) {
    return with_offset(kCursorTypeSpeak);
  }

  if ((npc_flags & kCursorNpcFlagVendor) != 0u) {
    return with_offset(kCursorTypePickup);
  }
  if ((npc_flags & kCursorNpcFlagGossip) != 0u) {
    return with_offset(kCursorTypeSpeak);
  }
  if ((npc_flags & kCursorNpcFlagSpellClick) != 0u) {
    return with_offset(kCursorTypeInteract);
  }
  return 0u;
}

[[nodiscard]] bool CanInteractWithMouseoverLootTarget(const CGPlayer_C &active_player,
                                                      const CGUnit_C &unit) {
  if (active_player.Animation().StandSelectionInteractionTargetGuid() ==
      unit.GetGuid().GetRawValue()) {
    return true;
  }

  return CanInteractWithTarget(active_player, unit);
}

[[nodiscard]] std::uint32_t ResolveMouseoverLootCursorType(const WorldSession &session,
                                                           const CGPlayer_C &active_player,
                                                           const CGUnit_C &unit) {
  const bool auto_loot = IsAutoLootEnabled(session.binding_profiles());
  const std::uint32_t base_type = auto_loot ? kCursorTypeLootAll : kCursorTypePickup;
  return ApplyUnavailableCursorOffset(
      base_type, !CanInteractWithMouseoverLootTarget(active_player, unit));
}

[[nodiscard]] std::uint32_t ResolveMouseoverGatherCursorType(const WorldSession &session,
                                                             const CGPlayer_C &active_player,
                                                             const CGUnit_C &unit) {
  const std::uint32_t spell_id = SpellbookSystem::Get().ResolveGatherInteractionSpellId(
      unit, &session.query_cache());

  if (unit.IsPlayer() && active_player.Interaction().CanAssistSpellTarget(unit, false)) {
    return 0u;
  }

  float max_range = 0.0f;
  if (!openwow::ui::game::TryResolveGatherInteractionMaxRange(session, active_player, spell_id,
                                                              max_range)) {
    return 0u;
  }

  const float distance = active_player.GetDistance(unit);
  const bool out_of_range = distance * distance > max_range * max_range;

  if (unit.IsPlayer()) {
    return ResolvePlayerCorpseSkinCursorType(ResolveRetailTeamIndex(session, active_player),
                                             out_of_range);
  }

  const auto *const creature_template = session.query_cache().GetCreatureTemplate(unit.GetEntry());
  std::uint32_t base_type = kCursorTypeSkin;
  if (creature_template != nullptr) {
    switch (ResolveSkinnableResourceType(*creature_template)) {
      case SkinnableResourceType::Herb:
        base_type = kCursorTypeGatherHerbs;
        break;
      case SkinnableResourceType::Rock:
      case SkinnableResourceType::Bolts:
        base_type = kCursorTypeMine;
        break;
      case SkinnableResourceType::Leather:
        base_type = kCursorTypeSkin;
        break;
    }
  }

  return ApplyUnavailableCursorOffset(base_type, out_of_range);
}

[[nodiscard]] std::uint32_t ResolveMouseoverAttackCursorType(const WorldSession &session,
                                                             const CGPlayer_C &active_player,
                                                             const CGUnit_C &unit) {

  if (unit.State().IsDead() &&
      (unit.State().GetDynamicFlags() & kUnitDynFlagDead) == 0u) {
    return 0u;
  }
  if (!active_player.Interaction().CanAttackSpellTarget(unit) ||
      active_player.State().IsPacified() || !session.has_current_map()) {
    return 0u;
  }

  const float distance = active_player.GetDistance(unit);
  const double range_squared =
      static_cast<double>(interaction_range::ComputeUnitInteractionRangeSquared(
          active_player.State().GetCombatReach(), unit.State().GetCombatReach()));
  return ApplyUnavailableCursorOffset(
      kCursorTypeAttack,
      static_cast<double>(distance) * static_cast<double>(distance) > range_squared);
}

[[nodiscard]] openwow::game::RetailCursorRequest
ResolveMouseoverCreatureCursorRequest(const WorldSession &session, const CGUnit_C &unit,
                                      const bool out_of_range) {
  const auto *const creature_template = session.query_cache().GetCreatureTemplate(unit.GetEntry());
  if (creature_template == nullptr || creature_template->icon_name.empty()) {
    return {};
  }
  return openwow::game::ResolveRetailCursorRequestFromStem(creature_template->icon_name,
                                                           out_of_range);
}

[[nodiscard]] std::uint32_t ResolveMouseoverVehicleCursorType(const CGPlayer_C &active_player,
                                                              const CGUnit_C &unit,
                                                              const bool out_of_range) {
  if (!unit.Vehicle().IsPartyRaidPlayerVehicle(unit, active_player)) {
    return 0u;
  }
  return ApplyUnavailableCursorOffset(kCursorTypeVehicle, out_of_range);
}

[[nodiscard]] const CGUnit_C *ResolveActivePossessedUnit(const WorldSession &session,
                                                         const CGPlayer_C &active_player) {
  const auto *const mover =
      session.objects().GetUnit(session.player_control_runtime().ActiveMoverGuid());
  if (mover == nullptr || !mover->State().IsPossessed()) {
    return nullptr;
  }
  return mover->State().GetCharmedByOrCreatedByGUID() == active_player.GetGuid() ? mover
                                                                                 : nullptr;
}

[[nodiscard]] bool IsMouseoverCursorRedirectedToPossessedUnit(
    const WorldSession &session, const CGPlayer_C &active_player) {
  const auto mover_guid = session.player_control_runtime().ActiveMoverGuid();
  return !mover_guid.IsEmpty() && mover_guid != active_player.GetGuid() &&
         mover_guid != active_player.GetTransportGUID();
}

[[nodiscard]] openwow::game::RetailCursorRequest
ResolveMouseoverUnitCursorRequest(const WorldSession &session, const CGPlayer_C &active_player,
                                  const CGUnit_C &unit) {

  if (IsMouseoverCursorRedirectedToPossessedUnit(session, active_player)) {
    const auto *const possessed = ResolveActivePossessedUnit(session, active_player);
    if (unit.State().GetHealth() == 0u || possessed == nullptr ||
        !possessed->Interaction().CanAttackSpellTarget(unit)) {
      return {};
    }
    return {.retail_type = kCursorTypeAttack};
  }

  const std::uint32_t npc_flags = unit.State().GetNpcFlags();

  if (npc_flags != 0u && !unit.State().IsDead() && !active_player.State().IsDead()) {
    const bool out_of_range = IsMouseoverNpcServiceOutOfRange(active_player, unit);

    if (const std::uint32_t quest_type =
            ResolveMouseoverQuestGiverCursorType(session, unit, out_of_range);
        quest_type != 0u) {
      return {.retail_type = quest_type};
    }
    if (auto creature_request =
            ResolveMouseoverCreatureCursorRequest(session, unit, out_of_range);
        creature_request.retail_type != 0u) {
      return creature_request;
    }
    if (const std::uint32_t service_type =
            ResolveMouseoverNpcServiceCursorType(npc_flags, out_of_range);
        service_type != 0u) {
      return {.retail_type = service_type};
    }
    return {.retail_type =
                ResolveMouseoverVehicleCursorType(active_player, unit, out_of_range)};
  }

  if (unit.State().IsLootableCorpseNow()) {
    return {.retail_type = ResolveMouseoverLootCursorType(session, active_player, unit)};
  }

  if (unit.State().IsSkinnable()) {
    if (const std::uint32_t gather_type =
            ResolveMouseoverGatherCursorType(session, active_player, unit);
        gather_type != 0u) {
      return {.retail_type = gather_type};
    }
  }

  return {.retail_type = ResolveMouseoverAttackCursorType(session, active_player, unit)};
}

void SyncWeaponImpactSounds(openwow::audio::SoundRuntime &sound,
                            const openwow::data::dbc::DbcLoader *dbc) {
  openwow::data::BindDbcTableRegistryLoader(dbc);
  openwow::data::DBClient_BuildItemSubClassIndex();

  if (dbc == nullptr) {
    sound.LoadWeaponImpactSounds({}, 0);
    return;
  }

  std::vector<openwow::audio::WeaponImpactSoundRowData> rows;
  rows.reserve(dbc->weapon_impact_sounds().entries().size());

  std::uint32_t weapon_item_subclass_count = 0;
  openwow::data::DBClient_GetWeaponItemSubClassCount(weapon_item_subclass_count);
  for (const auto &entry : dbc->weapon_impact_sounds().entries()) {
    openwow::audio::WeaponImpactSoundRowData row;
    row.weapon_subclass_id = entry.weapon_subclass_id;
    row.parry_type = entry.parry_type;
    row.impact_sound = entry.impact_sound;
    row.crit_impact_sound = entry.crit_impact_sound;
    rows.push_back(row);
  }

  sound.LoadWeaponImpactSounds(rows, weapon_item_subclass_count);
}

void SyncWoundDeathSounds(openwow::audio::SoundRuntime &sound,
                          const openwow::data::dbc::DbcLoader *dbc) {
  if (dbc == nullptr) {
    sound.LoadWoundDeathSoundTable({});
    return;
  }

  std::vector<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>> rows;
  rows.reserve(dbc->weapon_swing_sounds2().entries().size());

  for (const auto &entry : dbc->weapon_swing_sounds2().entries()) {
    rows.emplace_back(entry.swing_type, entry.crit, entry.sound_id);
  }

  sound.LoadWoundDeathSoundTable(rows);
}

void PrepareLogoutAccountData(WorldSession *session, const BindingProfiles *binding_profiles,
                              const openwow::world::WorldCamera *world_camera) {
  auto &account_data = AccountData::Get();
  SyncRuntimeConfigAccountData(world_camera);
  if (binding_profiles != nullptr) {
    actions::bindings::adapters::persistence::BindingAccountDataAdapter::Save(account_data,
                                                                              *binding_profiles);
  }
  const auto *dbc = session != nullptr ? session->GetDbcLoader() : nullptr;
  const auto current_zone_id = session != nullptr ? session->objects().GetZoneId() : 0u;
  account_data.SaveChat(SerializeChatCache(dbc, current_zone_id));

  if (session != nullptr) {
    actions::macros::persistence::MacroAccountDataAdapter::SaveIfDirty(session->macros(),
                                                                       account_data);
  }
}

AccountDataUploadContext BuildAccountDataUploadContext(const WorldSession *session,
                                                       BindingProfiles *binding_profiles,
                                                       openwow::ui::game::GameUIManager *ui_manager,
                                                       const SendPacketFn &send_packet) {
  return {
      .send_packet = send_packet,
      .dbc = session != nullptr ? session->GetDbcLoader() : nullptr,
      .zone_id = session != nullptr ? session->objects().GetZoneId() : 0u,
      .binding_profiles = binding_profiles,
      .macro_catalog = session != nullptr ? &session->macros() : nullptr,
      .world_camera = ui_manager != nullptr ? &ui_manager->world_camera() : nullptr,
      .retained_layout = ui_manager != nullptr ? &ui_manager->retained_layout() : nullptr,
      .include_config = true,
  };
}

void FlushLogoutAccountDataToDisk() {
  (void)AccountData::Get().FlushBoundPersistence();
}

constexpr float kCorpseProximityRangeSq = 1600.0f;
constexpr std::uint32_t kUnitFlagInCombat = 0x00080000u;

void SyncWorldSceneTimeFromState(WorldScene *world_scene, const WorldSession *world_session) {
  if (!world_scene)
    return;

  if (world_session != nullptr) {
    world_scene->SetTimeOfDay(world_session->session().GetGameTimeHourOfDay());
    return;
  }

  world_scene->SetTimeOfDay(0.0f);
}

std::optional<world::CameraPoseOverride>
ResolveCinematicCameraPose(const CinematicPlayer &player, const world::CameraFrameContext &frame) {
  float view[16]{};
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float vertical_fov = 0.0f;
  if (!player.GetCameraOverride(view, x, y, z, frame.aspect_ratio, vertical_fov)) {
    return std::nullopt;
  }

  return world::CameraPoseOverride{
      .source = world::CameraPoseSource::kCinematic,
      .position = {x, y, z},
      .forward = {view[2], view[6], view[10]},
      .up = {view[1], view[5], view[9]},
      .vertical_fov_radians = vertical_fov,
  };
}

std::optional<world::CameraPoseOverride>
ResolveCommentatorCameraPose(const CommentatorState &state) {
  const auto camera_override = state.GetManualCameraOverride();
  if (!camera_override.has_value()) {
    return std::nullopt;
  }

  return world::CameraPoseOverride{
      .source = world::CameraPoseSource::kCommentator,
      .position = {camera_override->position.x, camera_override->position.y,
                   camera_override->position.z},
      .forward = {camera_override->forward.x, camera_override->forward.y,
                  camera_override->forward.z},
      .up = {0.0f, 0.0f, 1.0f},
      .vertical_fov_radians = camera_override->fov,
  };
}

openwow::core::RetailDebugCommandBindings
BuildRetailDebugCommandBindings(const WorldSession *session,
                                const openwow::data::dbc::DbcLoader *dbc) {
  openwow::core::RetailDebugCommandBindings bindings{};
  if (session) {
    bindings.get_active_player_state =
        [session]() -> std::optional<openwow::core::RetailDebugActivePlayerState> {
      const auto *active_player = session->objects().GetActivePlayer();
      if (!active_player) {
        return std::nullopt;
      }

      const Position position = active_player->GetPosition();
      return openwow::core::RetailDebugActivePlayerState{
          .x = position.x,
          .y = position.y,
          .z = position.z,
          .orientation = position.facing,
      };
    };
    bindings.get_object_manager_status = [session] {
      const auto &objects = session->objects();
      return openwow::core::RetailDebugObjectManagerStatus{
          .tracked_count = static_cast<std::uint32_t>(objects.Count()),
          .pending_free_count = static_cast<std::uint32_t>(objects.GetPendingObjectCount()),
      };
    };
  }

  if (dbc) {
    bindings.get_dbc_loader = [dbc] { return dbc; };
    bindings.is_valid_map_id = [dbc](std::uint32_t map_id) {
      return dbc->map().LookupEntry(map_id) != nullptr;
    };
  }

  return bindings;
}

void BindCharacterAudioAndMediaAdapters(openwow::audio::SoundRuntime &sound,
                                        WorldSession *session,
                                        openwow::world::WorldMap *world_map) {
  if (session == nullptr) {
    sound.SetActivePlayerPositionCallback({});
    sound.SetObjectPositionCallback({});
    sound.SetUnitLookupCallback({});
    sound.SetPlayerLookupCallback({});
    sound.SetActivePlayerCallback({});
    sound.SetNormalizedTimeOfDayCallback({});
    sound.SetLiquidQueryCallback({});
    return;
  }

  sound.SetActivePlayerPositionCallback([session](float *position_out) {
    const auto *player = session->objects().GetActivePlayer();
    if (player == nullptr || position_out == nullptr) {
      return false;
    }

    const Position position = player->GetPosition();
    position_out[0] = position.x;
    position_out[1] = position.y;
    position_out[2] = position.z;
    return true;
  });
  sound.SetObjectPositionCallback([session](const std::uint64_t guid, float *position_out) {
    const auto *object = session->objects().Get(ObjectGuid(guid));
    if (object == nullptr || position_out == nullptr || !object->IsType(kTypeMaskObject)) {
      return false;
    }

    const Position position = object->GetPosition();
    position_out[0] = position.x;
    position_out[1] = position.y;
    position_out[2] = position.z;
    return true;
  });
  sound.SetUnitLookupCallback(
      [session](const std::uint64_t guid) { return session->objects().GetUnit(ObjectGuid(guid)); });
  sound.SetPlayerLookupCallback([session](const std::uint64_t guid) {
    return session->objects().GetPlayer(ObjectGuid(guid));
  });
  sound.SetActivePlayerCallback([session] { return session->objects().GetActivePlayer(); });
  sound.SetNormalizedTimeOfDayCallback([session] {
    return static_cast<double>(session->session().GetGameTimeHourOfDay()) / 24.0;
  });
  sound.SetLiquidQueryCallback(
      [session, world_map](const float query_radius,
                           openwow::audio::LiquidQueryWorldSnapshot& snapshot) {
        snapshot = {};
        const auto* const player = session->objects().GetActivePlayer();
        if (player == nullptr || world_map == nullptr) {
          return false;
        }
        const Position position = player->GetPosition();
        for (const auto& source : world_map->QueryLiquidSoundSources(
                 position.x, position.y, position.z, query_radius)) {
          snapshot.entries.push_back({
              .liquid_type_id = source.liquid_type_id,
              .relative_offset = {source.position[0] - position.x,
                                  source.position[1] - position.y,
                                  source.position[2] - position.z},
          });
        }
        return true;
      });
}

}

openwow::ui::game::WorldUiLifecycleOperations GameLoop::CreateWorldUiLifecycleOperations() {
  using namespace openwow::ui::game;
  return {
      .start_runtime =
          [this](const WorldUiGeneration generation, std::function<void(float)> progress) {
            return StartWorldUiRuntime(generation, std::move(progress));
          },
      .detach_runtime_callbacks =
          [this](const WorldUiGeneration generation) { DetachWorldUiRuntimeCallbacks(generation); },
      .destroy_runtime =
          [this](const WorldUiGeneration generation, const WorldUiStopReason reason) {
            DestroyWorldUiRuntime(generation, reason);
          },
      .fire_event = [this](const WorldUiLifecycleEvent event) { FireWorldUiLifecycleEvent(event); },
      .has_local_player = [this]() { return HasLocalPlayerForWorldUi(); },
      .prepare_local_player = [this]() { return PrepareLocalPlayerForWorldUi(); },
      .run_pre_enter_player_setup = [this]() { RunPreEnterLocalPlayerWorldUiSetup(); },
      .run_post_enter_player_fanout = [this]() { RunLocalPlayerWorldUiFanout(); },
      .prepare_player_leave =
          [this](const WorldUiStopReason reason) { PrepareWorldUiForPlayerLeave(reason); },
      .prepare_player_logout =
          [this](const WorldUiStopReason reason) { PrepareWorldUiForPlayerLogout(reason); },
      .persist_state = [this](const WorldUiStopReason reason) { PersistWorldUiState(reason); },
      .restore_account_data =
          [this](const WorldUiGeneration generation, const WorldUiAccountDataSlot slot,
                 WorldUiAccountDataCompletion completion) {
            RestoreWorldUiAccountData(generation, slot, std::move(completion));
          },
      .cancel_account_data =
          [this](const WorldUiGeneration generation) { CancelWorldUiAccountData(generation); },
      .reload_blocked = [this]() { return IsWorldUiReloadBlockedByCinematic(); },
      .prepare_reload = [this]() { PrepareWorldUiForReload(); },
  };
}

GameLoop::GameLoop(IsolatedRuntime, render::TextureManager &texture_manager,
                   render::m2::M2System &m2_system, openwow::audio::SoundRuntime &sound_runtime)
    : GameLoop(nullptr, texture_manager, m2_system, sound_runtime) {}

GameLoop::GameLoop(openwow::ui::display::ProductionDisplaySettingsRuntime &runtime,
                   render::TextureManager &texture_manager, render::m2::M2System &m2_system,
                   openwow::audio::SoundRuntime &sound_runtime)
    : GameLoop(&runtime, texture_manager, m2_system, sound_runtime) {}

GameLoop::GameLoop(openwow::ui::display::ProductionDisplaySettingsRuntime *runtime,
                   render::TextureManager &texture_manager, render::m2::M2System &m2_system,
                   openwow::audio::SoundRuntime &sound_runtime)
    : texture_manager_(texture_manager), m2_system_(m2_system), sound_runtime_(sound_runtime),
      render_resources_(std::make_unique<RenderResources>()),
      world_scene_(texture_manager_, m2_system_, world_frame_, world_environment_,
                   ReadSkyRenderSettings, sound_runtime_),
      offline_movement_(),
      binding_input_(
          binding_profiles_,
          [this](const BindingCommand &command, const bool pressed,
                 const std::string_view mouse_button,
                 const std::optional<std::uint16_t> modifier_state,
                 const std::uint32_t current_mouse_button_flag) {
            actions::bindings::adapters::lua::BindingCommandExecutor executor;
            if (world_session() != nullptr && game_ui_.lua_state() != nullptr) {
              return executor.ExecuteWorld(
                  binding_profiles_, *world_session(), *game_ui_.lua_state(),
                  world_session()->macros(),
                  [this](const std::uint32_t item_entry)
                      -> std::optional<actions::bindings::adapters::lua::BindingCommandExecutor::
                                           ItemBindingTarget> {
                    const auto *item =
                        FindInventoryItemByEntry(world_session()->inventory_replica(), item_entry);
                    if (item == nullptr) {
                      return std::nullopt;
                    }
                    return actions::bindings::adapters::lua::BindingCommandExecutor::
                        ItemBindingTarget{
                            .guid = item->guid,
                            .entry = item->entry,
                            .flags = item->flags,
                        };
                  },
                  command, pressed, mouse_button, modifier_state, current_mouse_button_flag);
            }
            return executor.ExecuteCore(binding_profiles_, command, pressed, modifier_state);
          },
          [](const std::string_view key, const bool pressed) {
            openwow::ui::game::ScriptEventDispatch::Get().FireEventArgs(
                openwow::ui::game::events::MODIFIER_STATE_CHANGED,
                {openwow::text::ToUpperAscii(std::string(key)), pressed ? 1 : 0});
          }),
      targeting_(world_frame_, CommentatorState::Get(), TutorialSystem::Instance(),
                 SpellbookSystem::Get(), openwow::ui::game::CVarSystem::Instance()),
      held_cursor_source_(GuildSystem::Get(), openwow::ui::game::ScriptEventDispatch::Get()),
      held_cursor_presentation_(openwow::ui::game::ScriptEventDispatch::Get(), sound_runtime_),
      held_cursor_(held_cursor_source_, held_cursor_presentation_),
      game_ui_(runtime, texture_manager_, m2_system_, world_frame_, world_scene_.camera(),
               sound_runtime_),
      floating_text_(render_resources_->floating_text), cursor_manager_(texture_manager_),
      cinematic_player_(m2_system_, sound_runtime_), post_process_(render_resources_->post_process),
      debug_draw_renderer_(render_resources_->debug_draw_renderer),
      minimap_(texture_manager_, game_ui_.minimap_state(), game_ui_.minimap_content(),
               world_environment_),
      zone_ui_state_(game_ui_.minimap_state(), game_ui_.world_map()),
      world_ui_lifecycle_(CreateWorldUiLifecycleOperations(), world_ui_session_commands_,
                          world_ui_entry_settings_) {
  m2_system_.BindTextureManager(&texture_manager_);
  CommentatorState::Get().BindCollision(&world_scene_.collision());
  CurrencySystem::Get().BindItemDefinitions(item_definitions_);
  BattlefieldInfo::Get().BindWorldMapSystem(&game_ui_.world_map());
  QuestLog::Get().BindWorldMapSystem(&game_ui_.world_map());
  BarberShop::Get().BindWorldCamera(&world_scene_.camera());
  game_ui_.BindHeldCursor(held_cursor_);
  SetVehicleDescriptorRenderReadyCallback(&ResolveVehicleDescriptorRenderReady, this);
  SetPlayerAnimationProgressCallback(&ResolvePlayerAnimationProgress, this);
  SetCorpseSupportSurfaceHeightCallback(&ResolveCorpseSupportSurfaceHeight, this);
  SetCorpseCharacterAppearanceReadyCallback(&ResolveCorpseCharacterAppearanceReady, this);
  SetJumpLiquidSurfaceHeightCallback(&ResolveJumpLiquidSurfaceHeight, this);
  SetWaterRippleSpawnCallback(&ResolveWaterRippleSpawn, this);
  SetCalcGroundPosCallback(&ResolveCalcGroundPos, this);
  SetUnitCollisionAabbCallback(&ResolveUnitCollisionAabb, this);
  SetUnitSoundGroundStateCallback(&ResolveUnitSoundGroundState, this);
}
GameLoop::~GameLoop() {
  CommentatorState::Get().BindCollision(nullptr);
  BarberShop::Get().BindWorldCamera(nullptr);
  BattlefieldInfo::Get().BindWorldMapSystem(nullptr);
  QuestLog::Get().BindWorldMapSystem(nullptr);
  ClearVehicleDescriptorRenderReadyCallback();
  ClearPlayerAnimationProgressCallback();
  ClearCorpseSupportSurfaceHeightCallback();
  ClearCorpseCharacterAppearanceReadyCallback();
  ClearJumpLiquidSurfaceHeightCallback();
  ClearWaterRippleSpawnCallback();
  ClearCalcGroundPosCallback();
  ClearUnitCollisionAabbCallback();
  ClearUnitSoundGroundStateCallback();
  Shutdown();
  SetRendererContext(nullptr);
}

void GameLoop::SetRendererContext(openwow::render::api::RendererContext *renderer_context) {
  if (renderer_context_ == renderer_context) {
    return;
  }
  if (renderer_context_ != nullptr && renderer_observer_registered_) {
    renderer_context_->RemoveDeviceLifecycleObserver(*this);
  }
  texture_manager_.BindRendererContext(renderer_context);

  m2_system_.BindRendererContext(renderer_context);
  world_scene_.BindRendererContext(renderer_context);
  renderer_context_ = renderer_context;
  renderer_observer_registered_ = false;
  if (renderer_context_ != nullptr) {
    renderer_context_->AddDeviceLifecycleObserver(*this);
    renderer_observer_registered_ = true;
    renderer_device_generation_ = renderer_context_->Generation();
  } else {
    renderer_device_generation_ = {};
  }
  game_ui_.BindRendererContext(renderer_context);
}

void GameLoop::OnRendererDeviceWillReset() {
  if (!initialized_) {
    return;
  }
  cursor_manager_.ReleaseRendererDeviceResources();
  minimap_.ReleaseRendererDeviceResources();
  debug_draw_renderer_.Shutdown();
  floating_text_.ReleaseRendererDeviceResources();
  post_process_.ReleaseRendererDeviceResources();
}

void GameLoop::OnRendererDeviceReady(const openwow::render::api::DeviceGeneration generation) {
  renderer_device_generation_ = generation;
  if (!initialized_) {
    return;
  }

  const bool post_process_ready = post_process_.RestoreRendererDeviceResources();
  const bool floating_text_ready = floating_text_.RestoreRendererDeviceResources();
  const bool minimap_ready = minimap_.RestoreRendererDeviceResources();
  const bool debug_draw_ready = debug_draw_renderer_.Initialize();
  if (!post_process_ready || !floating_text_ready || !minimap_ready || !debug_draw_ready) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
                              "GameLoop: renderer-device resource restore failed");
  }
}

bool GameLoop::CorpseProximityEventsSuppressed() const {
  return BattlefieldInfo::Get().GetActiveBGType() ==
         static_cast<std::uint32_t>(openwow::data::dbc::MapType::kArena);
}

void GameLoop::ClearCorpseProximityState() {
  if (!corpse_proximity_active_) {
    return;
  }

  corpse_proximity_active_ = false;
  if (game_ui_.is_initialized() && !CorpseProximityEventsSuppressed()) {
    game_ui_.frame_events().dispatcher().FireEvent(openwow::ui::game::events::CORPSE_OUT_OF_RANGE);
  }
}

bool GameLoop::IsCorpseProximityActive() const {
  if (!world_session() || !death_manager_.IsDeadOrGhost()) {
    return false;
  }

  const auto corpse = openwow::ui::game::GameUI_GetCorpsePositionData(*world_session());
  if (!corpse.found || corpse.map_id < 0) {
    return false;
  }

  const auto *active_player = world_session()->objects().GetActivePlayer();
  if (!active_player) {
    return false;
  }

  const float dx = active_player->GetX() - corpse.position.x;
  const float dy = active_player->GetY() - corpse.position.y;
  const float dz = active_player->GetZ() - corpse.position.z;
  const float dist_sq = dx * dx + dy * dy + dz * dz;
  return dist_sq <= kCorpseProximityRangeSq;
}

const char *GameLoop::GetCorpseProximityEventName() const {
  if (!world_session()) {
    return openwow::ui::game::events::CORPSE_OUT_OF_RANGE;
  }

  const auto corpse = openwow::ui::game::GameUI_GetCorpsePositionData(*world_session());
  return corpse.map_id == corpse.corpse_map_id ? openwow::ui::game::events::CORPSE_IN_RANGE
                                               : openwow::ui::game::events::CORPSE_IN_INSTANCE;
}

void GameLoop::UpdateCorpseProximityState(bool force_refire_current_event) {
  if (force_refire_current_event && corpse_proximity_active_ &&
      game_ui_.is_initialized()) {
    game_ui_.frame_events().dispatcher().FireEvent(GetCorpseProximityEventName());
  }

  const bool active = IsCorpseProximityActive();
  if (active == corpse_proximity_active_) {
    return;
  }

  corpse_proximity_active_ = active;
  if (!game_ui_.is_initialized() || CorpseProximityEventsSuppressed()) {
    return;
  }

  if (active) {
    game_ui_.frame_events().dispatcher().FireEvent(GetCorpseProximityEventName());
  } else {
    game_ui_.frame_events().dispatcher().FireEvent(openwow::ui::game::events::CORPSE_OUT_OF_RANGE);
  }
}

WorldSession *GameLoop::world_session() const noexcept {
  return character_world_runtime_ != nullptr ? character_world_runtime_->session() : nullptr;
}

void GameLoop::SetCharacterWorldRuntime(CharacterWorldRuntime *runtime) {
  WorldSession *const next_session = runtime != nullptr ? runtime->session() : nullptr;
  ReleaseClickToMove(world_session());
  held_cursor_source_.BindSpellTargeting(nullptr);
  DetachWorldUiMacroPresentation();
  BindCharacterAudioAndMediaAdapters(sound_runtime_, nullptr, nullptr);
  if (world_session()) {
    world_session()->BindTargetingSystem(nullptr);
    world_session()->SetWeatherPresentationCallback({});
    world_session()->SetLootSourceTargetSelectionCallback({});
    world_session()->SetTrackedGuidInvalidationCallback({});
    world_session()->missile_trajectory().BindWorldIntersection({});
    world_session()->BindWorldMapSystem(nullptr);
    world_session()->BindMinimapSystem(nullptr);
    world_session()->BindMinimapPingSystem(nullptr);
    world_session()->BindWorldFrame(nullptr);
    world_session()->BindWorldEnvironmentState(nullptr);
    world_session()->BindWorldCamera(nullptr);
    world_session()->inventory_commands().SetProtectionGate({});
    world_session()->BindHeldCursor(nullptr);
    world_session()->chat_sender().SetActivityCallback({});
    world_session()->macros().SetModifiedClickConditionQuery({});
    world_session()->macros().SetMacrosChangedCallback({});
    world_session()->macros().SetUnknownConditionHandler({});
    world_session()->macros().SetConditionSnapshotProvider({});
    world_session()->macros().SetIconResolutionQueries({});
    world_session()->macros().SetIconPathResolver({});
    world_session()->macros().SetClearActionBarMacro({});
    world_session()->macros().SetActiveShapeshiftFormProvider({});
    world_session()->macros().SetProtectionGate({});
    world_session()->macros().SetChatCommandHandler({});
  }
  targeting_.Initialize(nullptr);

  if (next_session == nullptr) {
    send_packet_fn_ = {};
    UnitFrameDataProvider::Get().Reset();
    offline_movement_.ResetTransientState();
    offline_movement_.SetPosition(player_x_, player_y_, player_z_, player_orientation_);
  }

  character_world_runtime_ = runtime;
  WorldSession *const session = next_session;
  if (session != nullptr) {
    held_cursor_source_.BindSpellTargeting(&session->spells().GetTargeting());
    session->missile_trajectory().BindWorldIntersection(
        [this](const TrajectoryPoint &start, const TrajectoryPoint &end, std::uint32_t) {
          return IntersectMissileWorldSegment(*this, start, end);
        });
  }
  if (auto *const input = GetInputControlSingleton(); input != nullptr) {
    input->BindMissileTrajectoryInputRefresh(
        session != nullptr ? MissileTrajectoryInputRefreshCallback([this]() {
          if (auto *const active_session = world_session(); active_session != nullptr) {
            active_session->missile_trajectory().LatchInputRefresh();
          }
        })
                           : MissileTrajectoryInputRefreshCallback{});
  }
  BindCharacterAudioAndMediaAdapters(sound_runtime_, session,
                                     &world_scene_.world_map());
  packet_queue_ = runtime != nullptr ? runtime->packet_queue() : nullptr;
  minimap_.BindWorldSceneState(world_session() != nullptr ? &world_session()->scene_state()
                                                          : nullptr);
  if (world_session() != nullptr) {
    world_session()->SetWeatherPresentationCallback(
        [this](const std::uint32_t weather_id, const float intensity,
               const bool smooth) {
          world_scene_.SetWeather(weather_id, intensity, smooth);
        });
    world_session()->inventory_replica().SetCurrencyAmountResolver(
        [](const std::uint32_t item_entry) { return CurrencySystem::Get().GetAmount(item_entry); });
    world_session()->BindWorldMapSystem(&game_ui_.world_map());
    world_session()->BindMinimapSystem(&game_ui_.minimap_state());
    world_session()->BindMinimapPingSystem(&game_ui_.minimap_ping());
    world_session()->BindWorldFrame(&world_frame_);
    world_session()->BindWorldEnvironmentState(&world_environment_);
    world_session()->BindWorldCamera(&world_scene_.camera());
    world_session()->SetMerchantArenaTeamQuery(
        [](const std::uint8_t slot) -> std::optional<MerchantArenaTeamRating> {
          const auto *team = ArenaSystem::Get().GetTeam(slot);
          if (team == nullptr) {
            return std::nullopt;
          }
          return MerchantArenaTeamRating{
              .team_type = team->team_type,
              .rating = team->rating,
          };
        });
  }
  held_cursor_source_.BindItemLocks(session != nullptr ? &session->item_locks() : nullptr);
  openwow::ui::game::TooltipSystem::Get().BindEquipmentSets(
      session != nullptr ? &session->equipment() : nullptr);
  openwow::ui::game::TooltipSystem::Get().BindInventory(
      session != nullptr ? &session->inventory_replica() : nullptr);
  openwow::ui::game::TooltipSystem::Get().BindItemDefinitions(
      session != nullptr ? &session->item_definitions() : nullptr);
  world_ui_session_commands_.BindSession(session);
  if (world_session() != nullptr) {
    world_session()->inventory_commands().SetProtectionGate(CanPerformInventoryMutation);
    world_session()->BindHeldCursor(&held_cursor_);
    world_session()->SetBindingProfiles(&binding_profiles_);
    world_session()->macros().SetProtectionGate(CanPerformMacroOperation);

    world_session()->macros().SetChatCommandHandler([](const std::string& line) {
      openwow::ui::game::ScriptEventDispatch::Get().FireEventArgs(
          openwow::ui::game::events::EXECUTE_CHAT_LINE, {line});
    });
    world_session()->macros().SetModifiedClickConditionQuery(
        [this](const std::optional<std::string_view> action, const std::uint16_t modifier_state,
               const std::string_view mouse_button) {
          const std::string action_text = action ? std::string(*action) : std::string{};
          return actions::bindings::adapters::retail::IsModifiedClickActive(
              binding_profiles_, action ? action_text.c_str() : nullptr, modifier_state,
              mouse_button);
        });
    world_session()->macros().SetMacrosChangedCallback([] {
      openwow::ui::game::ScriptEventDispatch::Get().FireEvent(
          openwow::ui::game::events::UPDATE_MACROS);
    });
    world_session()->macros().SetUnknownConditionHandler([](const std::string_view condition) {
      const std::string owned(condition);
      openwow::ui::game::DisplaySystemMessage(574, owned.c_str());
    });
    world_session()->macros().SetConditionSnapshotProvider(
        actions::macros::adapters::retail::MakeRetailMacroConditionSnapshotProvider(
            world_session()->macros(), *world_session(), GroupSystem::Get(), VehicleSystem::Get(),
            held_cursor_, TalentInfoStore::Get(), UnitQueryBridge::Get(), world_environment_,
            [this](const float x, const float y, const float z) {
              return world_environment_.QueryOutdoorStateAtWorldPosition(x, y, z);
            },
            [] { return static_cast<std::uint16_t>(SDL_GetModState()); },
            [session = world_session()] { return session->action_page_state().current(); }));
    world_session()->macros().SetIconResolutionQueries(
        actions::macros::adapters::retail::MakeRetailMacroIconResolutionQueries(
            world_session()->macros(), *world_session(), UnitQueryBridge::Get(),
            world_session()->inventory_replica(),
            actions::macros::adapters::retail::ResolveRetailMacroSpell));
    world_session()->macros().SetIconPathResolver(
        actions::macros::adapters::retail::MakeRetailMacroIconPathResolver(
            *world_session(), world_session()->item_definitions()));
    world_session()->macros().SetClearActionBarMacro(
        actions::macros::adapters::ui::MakeClearActionBarMacroAdapter(
            world_session()->action_assignments(),
            [session = world_session()] {
              return session->objects().GetActivePlayer() != nullptr &&
                     !session->action_assignments().IsServerSyncPending();
            },
            [session = world_session()](const actions::ActionSlot slot) {
              session->interaction().SendClearActionButton(slot.wire_value());
              openwow::ui::game::ScriptEventDispatch::Get().FireActionbarSlotChanged(
                  slot.lua_index());
            }));
    world_session()->macros().SetActiveShapeshiftFormProvider(
        actions::macros::adapters::ui::MakeActiveShapeshiftFormAdapter(*world_session()));
    if (game_ui_.is_initialized()) {
      AttachWorldUiMacroPresentation();
    }
  }

  if (world_session() != nullptr && client_time_fn_) {
    world_session()->SetClientTimeFn(client_time_fn_);
  }
  BindMovementCollisionSource();
  BindSpellTextFormatterWorldSession(world_session());
  if (world_session()) {
    world_session()->SetEnterWorldTransitionCallback([this](std::uint32_t map_id, float x, float y,
                                                            float z, float orientation,
                                                            const std::string &map_internal_name) {
      EnterWorld(map_id, x, y, z, orientation, map_internal_name);
    });
    world_session()->SetTransferPendingCallback(
        [this](const TransferPendingInfo &pending) { PrimeTransferPendingLoadingScreen(pending); });
    world_session()->chat_sender().SetActivityCallback(
        [this](const std::uint32_t now_ms) { idle_billing_.NoteUserActivity(now_ms); });
  }
  openwow::core::SetRetailDebugCommandBindings(
      BuildRetailDebugCommandBindings(world_session(), dbc_));
  targeting_.Initialize(world_session());
  if (world_session()) {
    world_session()->BindTargetingSystem(&targeting_);
    world_session()->SetLootSourceTargetSelectionCallback(
        [this](std::uint64_t guid) { targeting_.SetTarget(guid); });
    world_session()->SetTrackedGuidInvalidationCallback(
        [this](std::uint64_t guid) { targeting_.InvalidateTrackedGuidReferences(guid); });
  }
}

void GameLoop::AttachWorldUiMacroPresentation() {
  if (world_session() == nullptr || game_ui_.lua_state() == nullptr) {
    return;
  }

  auto *macros = &world_session()->macros();
  game_ui_.input_router().SetRunningMacroInputButtonProvider(
      [macros] { return macros->RunningMacroInputButton(); });
  macros->SetCastSequenceTokenResolver([state = game_ui_.lua_state()](const std::string_view body) {
    return spells::spellbook::adapters::lua::ResolveCastSequenceToken(state, body);
  });
}

void GameLoop::DetachWorldUiMacroPresentation() {
  game_ui_.input_router().SetRunningMacroInputButtonProvider({});
  if (world_session() != nullptr) {
    world_session()->macros().SetCastSequenceTokenResolver({});
  }
}

MovementController &GameLoop::movement_controller() {
  return world_session() != nullptr ? world_session()->movement() : offline_movement_;
}

const MovementController &GameLoop::movement_controller() const {
  return world_session() != nullptr ? world_session()->movement() : offline_movement_;
}

void GameLoop::BindMovementCollisionSource() {
  if (world_session() == nullptr) {
    return;
  }
  MovementCollisionCallbacks collision_callbacks;
  collision_callbacks.facet_revision = [this]() {
    const std::uint64_t terrain_revision = world_scene_.collision().FacetRevision();
    const std::uint64_t wmo_revision = world_scene_.world_map().MovementCollisionFacetRevision();
    const std::uint64_t doodad_revision = world_scene_.DoodadCollisionRevision();
    const std::uint64_t game_object_revision =
        world_scene_.object_renderer().GameObjectCollisionRevision();
    const std::uint64_t terrain_wmo =
        terrain_revision * 0x9e3779b97f4a7c15ull ^
        (wmo_revision + 0x517cc1b727220a95ull + (terrain_revision << 6u) +
         (terrain_revision >> 2u));
    const std::uint64_t with_doodads =
        terrain_wmo ^ (doodad_revision + 0x94d049bb133111ebull + (terrain_wmo << 6u) +
                       (terrain_wmo >> 2u));
    return with_doodads ^ (game_object_revision + 0xbf58476d1ce4e5b9ull +
                           (with_doodads << 6u) + (with_doodads >> 2u));
  };
  collision_callbacks.query_facets =
      [this](const CollisionAabb &bounds, const std::uint32_t collision_mask,
             const MovementCollisionLayer layer) -> std::optional<MovementCollisionFacetBatch> {
    MovementCollisionFacetBatch batch;
    const std::uint64_t terrain_revision = world_scene_.collision().FacetRevision();
    const std::uint64_t wmo_revision = world_scene_.world_map().MovementCollisionFacetRevision();
    const std::uint64_t doodad_revision = world_scene_.DoodadCollisionRevision();
    const std::uint64_t game_object_revision =
        world_scene_.object_renderer().GameObjectCollisionRevision();
    const std::uint64_t terrain_wmo =
        terrain_revision * 0x9e3779b97f4a7c15ull ^
        (wmo_revision + 0x517cc1b727220a95ull + (terrain_revision << 6u) +
         (terrain_revision >> 2u));
    const std::uint64_t with_doodads =
        terrain_wmo ^ (doodad_revision + 0x94d049bb133111ebull + (terrain_wmo << 6u) +
                       (terrain_wmo >> 2u));
    batch.revision = with_doodads ^ (game_object_revision + 0xbf58476d1ce4e5b9ull +
                                     (with_doodads << 6u) + (with_doodads >> 2u));
    const auto append_source = [&batch](const world::CollisionFacetView &source) {
      MovementCollisionFacet facet;
      facet.vertices[0] = {source.vertices[0][0], source.vertices[0][1], source.vertices[0][2]};
      facet.vertices[1] = {source.vertices[1][0], source.vertices[1][1], source.vertices[1][2]};
      facet.vertices[2] = {source.vertices[2][0], source.vertices[2][1], source.vertices[2][2]};
      const C3Vector normal{source.normal[0], source.normal[1], source.normal[2]};
      facet.normal = normal;
      facet.owner_id = source.owner_id;
      facet.facet_id = source.facet_id;
      facet.owner_guid = source.owner_guid;
      facet.plane_offset = -(normal.x * facet.vertices[0].x + normal.y * facet.vertices[0].y +
                             normal.z * facet.vertices[0].z);
      batch.facets.push_back(facet);
    };
    const std::array<float, 6> world_bounds{bounds.min.x, bounds.min.y, bounds.min.z,
                                            bounds.max.x, bounds.max.y, bounds.max.z};
    if (layer == MovementCollisionLayer::kSecondary) {

      world_scene_.world_map().VisitMovementLiquidFacets(world_bounds, 0x20000u, append_source);
      return batch;
    }

    const bool terrain_complete =
        world_scene_.world_map().AreExistingTerrainTilesLoaded(
            bounds.min.x, bounds.max.x, bounds.min.y, bounds.max.y);
    world_scene_.collision().VisitFacets(bounds.min.x, bounds.max.x, bounds.min.y, bounds.max.y,
                                         bounds.min.z, bounds.max.z, append_source);

    const bool include_game_object_geometry =
        (collision_mask & kMovementIncludeGameObjectGeometry) != 0u;
    const world::MovementWmoCollisionCompleteness wmo_completeness =
        world_scene_.world_map().VisitMovementCollisionFacets(
            world_bounds, append_source, include_game_object_geometry);

    world_scene_.VisitDoodadCollisionTriangles(
        world_bounds,
        [&batch](const render::DoodadCollisionTriangle& source) {
          MovementCollisionFacet facet;
          for (std::size_t index = 0u; index < facet.vertices.size(); ++index) {
            facet.vertices[index] = {source.vertices[index][0], source.vertices[index][1],
                                     source.vertices[index][2]};
          }
          const C3Vector edge_a{
              facet.vertices[1].x - facet.vertices[0].x,
              facet.vertices[1].y - facet.vertices[0].y,
              facet.vertices[1].z - facet.vertices[0].z};
          const C3Vector edge_b{
              facet.vertices[2].x - facet.vertices[0].x,
              facet.vertices[2].y - facet.vertices[0].y,
              facet.vertices[2].z - facet.vertices[0].z};
          C3Vector normal{
              edge_a.y * edge_b.z - edge_a.z * edge_b.y,
              edge_a.z * edge_b.x - edge_a.x * edge_b.z,
              edge_a.x * edge_b.y - edge_a.y * edge_b.x};
          const float normal_length =
              std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
          if (normal_length <= 1.0e-6f) {
            return;
          }
          normal.x /= normal_length;
          normal.y /= normal_length;
          normal.z /= normal_length;
          facet.normal = normal;
          facet.owner_id = source.owner_id;
          facet.facet_id = source.facet_id;

          facet.owner_guid = source.owner_guid;
          facet.plane_offset =
              -(normal.x * facet.vertices[0].x + normal.y * facet.vertices[0].y +
                normal.z * facet.vertices[0].z);
          batch.facets.push_back(facet);
        },
        include_game_object_geometry);

    const auto *const collision_session = world_session();
    if (include_game_object_geometry && collision_session != nullptr) {
      const auto &objects = collision_session->objects();
      world_scene_.object_renderer().VisitGameObjectCollisionTriangles(
          world_bounds,
          [&batch, &objects,
           collision_mask](const render::ObjectRenderer::GameObjectCollisionTriangle &source) {
            const auto *const game_object = objects.GetGameObject(source.guid);
            if (game_object == nullptr ||
                (!IsGuidStampedTransportModelGameObject(game_object->GetGoType()) &&
                 game_object->GetInteractionValue(
                     static_cast<std::uint16_t>(collision_mask)) == 0u)) {
              return;
            }
            MovementCollisionFacet facet;
            for (std::size_t index = 0u; index < facet.vertices.size(); ++index) {
              facet.vertices[index] = {source.vertices[index][0], source.vertices[index][1],
                                       source.vertices[index][2]};
            }
            const C3Vector edge_a{facet.vertices[1].x - facet.vertices[0].x,
                                  facet.vertices[1].y - facet.vertices[0].y,
                                  facet.vertices[1].z - facet.vertices[0].z};
            const C3Vector edge_b{facet.vertices[2].x - facet.vertices[0].x,
                                  facet.vertices[2].y - facet.vertices[0].y,
                                  facet.vertices[2].z - facet.vertices[0].z};
            C3Vector normal{edge_a.y * edge_b.z - edge_a.z * edge_b.y,
                            edge_a.z * edge_b.x - edge_a.x * edge_b.z,
                            edge_a.x * edge_b.y - edge_a.y * edge_b.x};
            const float normal_length =
                std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
            if (normal_length <= 1.0e-6f) {
              return;
            }
            normal.x /= normal_length;
            normal.y /= normal_length;
            normal.z /= normal_length;
            facet.normal = normal;
            facet.owner_id = source.owner_id;
            facet.facet_id = source.facet_id;
            facet.owner_guid = source.guid.GetRawValue();
            facet.plane_offset =
                -(normal.x * facet.vertices[0].x + normal.y * facet.vertices[0].y +
                  normal.z * facet.vertices[0].z);
            batch.facets.push_back(facet);
          });
    }

    batch.completeness =
        terrain_complete && wmo_completeness == world::MovementWmoCollisionCompleteness::kComplete
            ? MovementCollisionFacetCompleteness::kComplete
            : MovementCollisionFacetCompleteness::kPending;
    if ((collision_mask & 0x30000u) != 0u) {
      world_scene_.world_map().VisitMovementLiquidFacets(world_bounds, collision_mask,
                                                         append_source);
    }
    return batch;
  };
  world_session()->SetMovementCollisionSolver(
      std::make_shared<MovementCollisionSolver>(std::move(collision_callbacks)));

  world_session()->SetTransportCollisionReadinessQuery(
      [this](const std::uint64_t transport_guid) {
        return world_scene_.object_renderer()
            .IsLoadingScreenTransportRenderAssetReady(
                game::ObjectGuid(transport_guid));
      });
}

void GameLoop::SetDbcLoader(const openwow::data::dbc::DbcLoader *dbc) {
  dbc_ = dbc;

  Minimap_BindAreaPOIDbcLoader(dbc_);
  world_scene_.BindDbc(dbc_);
  SyncWeaponImpactSounds(sound_runtime_, dbc_);
  SyncWoundDeathSounds(sound_runtime_, dbc_);
  openwow::net::wotlk::RealmAddonHandshakeState::Instance().BindBuiltinCatalogFromDbc(dbc_);
  BindChatDisplayDbcLoader(dbc_);
  DeclinedWords::Get().BindDbcLoader(dbc_);
  BindNameValidationDbcLoader(dbc_);
  BindSpellTextFormatterDbcLoader(dbc_);
  openwow::ui::game::TooltipSystem::Get().BindDbcLoader(dbc_);
  openwow::core::SetRetailDebugCommandBindings(
      BuildRetailDebugCommandBindings(world_session(), dbc_));
  if (initialized_) {
    loading_screen_.SetDbcLoader(dbc_);
  }
}

bool GameLoop::StartWorldUiRuntime(const openwow::ui::game::WorldUiGeneration generation,
                                   std::function<void(float)> progress_callback) {
  (void)generation;
  if (!vfs_ || !world_session()) {
    return false;
  }

  ResetWorldStateUiRuntimeForEnterWorld();

  TrackedAchievementState::Get().Initialize();

  binding_profiles_.BeginWorldUiSession();

  auto sound_block = sound_runtime_.BlockNonPositionalPlayback();

  if (!game_ui_.Initialize(vfs_, world_session(), cursor_manager_)) {
    return false;
  }
  game_ui_.BindWorldUiLifecycleCommands(&world_ui_lifecycle_);
  openwow::ui::game::BindWorldUiLifecycleCommands(game_ui_.lua_state(), &world_ui_lifecycle_);
  AttachWorldUiMacroPresentation();

  game_ui_.SetViewportSize(static_cast<float>(screen_width_), static_cast<float>(screen_height_));

  if (lua_State *L = game_ui_.lua_state(); L != nullptr) {
    lua_pushlightuserdata(L, &binding_profiles_);
    lua_setfield(L, LUA_REGISTRYINDEX, "openwow.key_binding_manager");
    lua_pushlightuserdata(L, &SpellQueryBridge::Get());
    lua_setfield(L, LUA_REGISTRYINDEX, "openwow.spell_query_bridge");
  }

  (void)PumpWorldEntryProtocolControlPackets();

  const auto &identity = game_ui_.persistence_identity();
  {
    openwow::ui::game::WoWClientLogFile framexml_log(
        "Logs\\FrameXML.log", openwow::ui::game::WoWClientLogOpenMode::kTruncate);

    if (!game_ui_.LoadDefaultUI(std::move(progress_callback), &framexml_log)) {
      return false;
    }

    (void)PumpWorldEntryProtocolControlPackets();

    if (lua_State *L = game_ui_.lua_state(); L != nullptr && !identity.account_name.empty() &&
                                             !identity.realm_name.empty() &&
                                             !identity.character_name.empty()) {
      (void)openwow::ui::game::LoadAllSavedVariables(L, identity.account_name, identity.realm_name,
                                                     identity.character_name, &framexml_log);
    }
  }

  if (lua_State *L = game_ui_.lua_state()) {
    lua_pushlightuserdata(L, &binding_profiles_);
    lua_setfield(L, LUA_REGISTRYINDEX, "openwow.key_binding_manager");

    lua_pushlightuserdata(L, &death_manager_);
    lua_setfield(L, LUA_REGISTRYINDEX, "openwow.death_manager");

    if (dbc_) {
      lua_pushlightuserdata(L, const_cast<openwow::data::dbc::DbcLoader *>(dbc_));
      lua_setfield(L, LUA_REGISTRYINDEX, "openwow.dbc_loader");
    }

    lua_pushlightuserdata(L, &cursor_manager_);
    lua_setfield(L, LUA_REGISTRYINDEX, "openwow.cursor_manager");
  }

  world_session()->SetAccountDataPayloadConsumer(
      [this](const AccountDataType type, const std::uint32_t timestamp, const std::string &data) {
        auto *const session = world_session();
        if (session == nullptr || !game_ui_.is_initialized()) {
          return;
        }
        ApplyAccountDataPayload(*session, type, timestamp, data, &binding_profiles_,
                                &session->macros(), &game_ui_.retained_layout());
      });

  event_bridge_.Initialize(world_session(), &game_ui_);
  event_bridge_.Reset();

  world_session()->SetLocalPlayerCombatFlagChangedCallback(
      [this]() { RefreshCombatBindingState(binding_profiles_, *world_session(), targeting_); });
  world_session()->SetAutoAttackCombatEventCallback([this](const AutoAttackCombatEvent event,
                                                           const std::uint64_t attacker_guid,
                                                           const std::uint64_t victim_guid) {
    switch (event) {
    case AutoAttackCombatEvent::AttackStop:
      targeting_.HandleServerAttackStop(attacker_guid, victim_guid);
      break;
    case AutoAttackCombatEvent::AttackerStateUpdate:
      targeting_.HandleServerAttackerStateUpdate(attacker_guid, victim_guid);
      break;
    case AutoAttackCombatEvent::AttackStart:
      break;
    }
  });

  world_session()->SetCancelCombatCallback(
      [this]() { targeting_.StopAttack(false); });

  targeting_.SetTargetChangedCallback([this]() {
    game_ui_.frame_events().OnPlayerTargetChanged();
    game_ui_.frame_events().dispatcher().FireEvent(openwow::ui::game::events::SPELL_UPDATE_USABLE);
    if (openwow::ui::game::detail::RefreshAllActionSlotValidation(*world_session())) {
      game_ui_.frame_events().dispatcher().FireEvent(
          openwow::ui::game::events::ACTIONBAR_UPDATE_USABLE);
    }
    game_ui_.frame_events().dispatcher().FireEvent(
        openwow::ui::game::events::PET_BAR_UPDATE_USABLE);
  });
  targeting_.SetAttackStateChangedCallback([this]() {
    RefreshCombatBindingState(binding_profiles_, *world_session(), targeting_);
    game_ui_.frame_events().dispatcher().FireEvent(openwow::ui::game::events::SPELL_UPDATE_USABLE);
    if (openwow::ui::game::detail::RefreshAllActionSlotValidation(*world_session())) {
      game_ui_.frame_events().dispatcher().FireEvent(
          openwow::ui::game::events::ACTIONBAR_UPDATE_USABLE);
    }
    game_ui_.frame_events().dispatcher().FireEvent(
        openwow::ui::game::events::PET_BAR_UPDATE_USABLE);
  });
  targeting_.SetAutoFollowChangedCallback([this](bool following) {
    game_ui_.frame_events().dispatcher().FireEvent(following
                                                       ? openwow::ui::game::events::AUTOFOLLOW_BEGIN
                                                       : openwow::ui::game::events::AUTOFOLLOW_END);
  });
  targeting_.SetFocusChangedCallback([this]() { game_ui_.frame_events().OnPlayerFocusChanged(); });

  auto &battle_net = BattleNetApi::Instance();
  battle_net.SetChatDisplayHandler([this](const BNetChatDisplayRequest &request) {
    const auto *session = world_session();
    if (session == nullptr) {
      return;
    }

    ChatFrame_DisplayMessage(session->objects(), request.message.c_str(), request.chat_type,
                             request.sender_name ? request.sender_name->c_str() : nullptr, 0,
                             nullptr, nullptr,
                             request.flag_tag ? request.flag_tag->c_str() : nullptr, 0, 0, 0, 0, 0,
                             request.extra_data ? request.extra_data->data() : nullptr);
  });
  battle_net.SetUiEventDispatchEnabled(true);
  LoadArchivedRuntimeStateFromCVars(world_session()->objects());
  return true;
}

void GameLoop::PrepareWorldUiForPlayerLeave(const openwow::ui::game::WorldUiStopReason reason) {
  if (reason != openwow::ui::game::WorldUiStopReason::WorldLeave) {
    return;
  }

  if (targeting_.target_guid() != 0) {
    targeting_.ClearTarget(targeting_.target_guid(), true);
  }
  if (targeting_.focus_guid() != 0) {
    targeting_.ClearFocus();
  }
  targeting_.ResetForWorldLeave();

  openwow::ui::game::SecureExecution::Get().SetInCombatLockdown(false);
  if (world_session() && game_ui_.is_initialized()) {
    if (const auto *local_player = world_session()->objects().GetLocalPlayerTyped();
        local_player != nullptr &&
        (local_player->GetUInt32(UNIT_FIELD_FLAGS) & kUnitFlagInCombat) != 0u) {
      game_ui_.frame_events().dispatcher().FireEvent(
          openwow::ui::game::events::PLAYER_REGEN_ENABLED);
    }
  }

  openwow::ui::game::ResetCapturePointUIManagerState();
  if (world_session() == nullptr) {
    return;
  }

  world_session()->trade().HandleWorldLogout(world_session()->interaction());
  const auto trade_changes = world_session()->trade().TakeChanges();
  for (const auto guid : trade_changes.released_item_guids) {
    openwow::ui::game::GameUI_OnMouseoverUnitLeave(guid);
  }
  if (trade_changes.closed) {
    openwow::ui::game::ScriptEventDispatch::Get().FireTradeClosed();
  }
  openwow::ui::game::detail::CloseTradeSkillView(world_session());
  if (!openwow::data::IsOnlineModeActive() && !openwow::data::IsStreamingInitialized()) {
    openwow::data::AsyncFileRead_WaitAll();
  }
  world_session()->interaction().SendLfgSearchLeave();
  world_session()->PrepareForWorldLeave();
}

void GameLoop::PrepareWorldUiForPlayerLogout(const openwow::ui::game::WorldUiStopReason reason) {
  if (reason == openwow::ui::game::WorldUiStopReason::WorldLeave) {
    PrepareQuestRuntimeStateForLogout();
  }
}

void GameLoop::PersistWorldUiState(const openwow::ui::game::WorldUiStopReason reason) {
  QuestLog::Get().SaveTrackedQuestsToCVar();
  if (game_ui_.is_initialized()) {
    TrackedAchievementState::Get().SaveTrackedAchievementsToCVar();
    TutorialSystem::Instance().SaveFlaggedTutorials();
    game_ui_.SaveSavedVariables();
    AccountData::Get().SaveLayout(game_ui_.retained_layout().BuildLayoutCache());
  }

  openwow::ui::AddOnsData::Get().SaveSavedStates();

  PrepareLogoutAccountData(world_session(), &binding_profiles_, &world_scene_.camera());
  (void)UploadRuntimeAccountData(BuildAccountDataUploadContext(
      world_session(), &binding_profiles_, game_ui_.is_initialized() ? &game_ui_ : nullptr,
      send_packet_fn_));
  FlushLogoutAccountDataToDisk();

  if (reason == openwow::ui::game::WorldUiStopReason::WorldLeave) {
    if (world_session() != nullptr) {
      world_session()->macros().ClearAll();
    }
    FinalizeQuestRuntimeStateAfterLogout();
  }
}

void GameLoop::DetachWorldUiRuntimeCallbacks(
    const openwow::ui::game::WorldUiGeneration generation) {
  (void)generation;
  event_bridge_.Shutdown();
  if (world_session() != nullptr) {
    world_session()->SetLocalPlayerCombatFlagChangedCallback({});
    world_session()->SetAutoAttackCombatEventCallback({});
    world_session()->SetCancelCombatCallback({});
  }
  targeting_.SetTargetChangedCallback({});
  targeting_.SetAttackStateChangedCallback({});
  targeting_.SetAutoFollowChangedCallback({});
  targeting_.SetFocusChangedCallback({});
}

void GameLoop::DestroyWorldUiRuntime(const openwow::ui::game::WorldUiGeneration generation,
                                     const openwow::ui::game::WorldUiStopReason reason) {
  (void)generation;
  (void)reason;
  binding_profiles_.EndWorldUiSession();
  auto &battle_net = BattleNetApi::Instance();
  battle_net.SetUiEventDispatchEnabled(false);
  battle_net.SetChatDisplayHandler({});
  DetachWorldUiMacroPresentation();
  if (world_session() != nullptr) {
    world_session()->SetAccountDataPayloadConsumer({});
  }
  game_ui_.Shutdown();
}

void GameLoop::RestoreWorldUiAccountData(
    const openwow::ui::game::WorldUiGeneration generation,
    const openwow::ui::game::WorldUiAccountDataSlot slot,
    openwow::ui::game::WorldUiAccountDataCompletion completion) {
  if (!world_session() || !game_ui_.is_initialized()) {
    return;
  }

  auto &account_data = AccountData::Get();
  const auto &identity = game_ui_.persistence_identity();
  if (slot == openwow::ui::game::WorldUiAccountDataSlot::Account) {
    if (!identity.account_name.empty() && !identity.realm_name.empty() &&
        !identity.character_name.empty()) {
      account_data.LoadFromDisk("WTF", identity.account_name, identity.realm_name,
                                identity.character_name);
      ApplyCachedAccountDataPayload(*world_session(), AccountDataType::GlobalConfig,
                                    account_data.GetData(AccountDataType::GlobalConfig),
                                    &binding_profiles_, &world_session()->macros());
    }

    if (binding_profiles_.HasLoadedBindingDefinitions()) {
      binding_profiles_.LoadDefaults();
      actions::bindings::adapters::persistence::BindingAccountDataAdapter::LoadCached(
          account_data, binding_profiles_);
    }
    world_session()->macros().InitializeUiSession();
    actions::macros::persistence::MacroAccountDataAdapter::LoadAll(world_session()->macros(),
                                                                   account_data);
  } else if (!identity.account_name.empty() && !identity.realm_name.empty() &&
             !identity.character_name.empty()) {
    ApplyCachedAccountDataPayload(*world_session(), AccountDataType::PerCharacterLayout,
                                  account_data.GetData(AccountDataType::PerCharacterLayout),
                                  &binding_profiles_, &world_session()->macros(),
                                  &game_ui_.retained_layout());
    ApplyCachedAccountDataPayload(*world_session(), AccountDataType::PerCharacterConfig,
                                  account_data.GetData(AccountDataType::PerCharacterConfig),
                                  &binding_profiles_, &world_session()->macros());

    ApplyCachedAccountDataPayload(*world_session(), AccountDataType::PerCharacterChat,
                                  account_data.GetData(AccountDataType::PerCharacterChat),
                                  &binding_profiles_, &world_session()->macros());
  }

  completion(generation, slot);
}

void GameLoop::CancelWorldUiAccountData(const openwow::ui::game::WorldUiGeneration generation) {
  (void)generation;
  AccountData::Get().DeactivateSlotsByScope(true);
}

bool GameLoop::Initialize(int screen_width, int screen_height) {
  if (initialized_)
    return true;

  using StartupClock = std::chrono::steady_clock;
  const auto startup_started = StartupClock::now();

  screen_width_ = std::max(1, screen_width);
  screen_height_ = std::max(1, screen_height);
  InputControl_RefreshViewportAspect();
  ResetWorldStateUiRuntimeForEnterWorld();

  ApplyFileLoaderBindings();

  (void)world_scene_.SetScreenSize(static_cast<std::uint32_t>(screen_width_),
                                   static_cast<std::uint32_t>(screen_height_));

  const bool texture_manager_ready = texture_manager_.Initialize();
  const auto texture_ready_at = StartupClock::now();
  if (!texture_manager_ready) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                              "GameLoop: TextureManager initialization failed (non-fatal)");
  }

  if (!world_scene_.Initialize()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
                              "GameLoop: WorldScene initialization failed");
    return false;
  }
  const auto world_scene_ready_at = StartupClock::now();

  if (loading_screen_archive_path_probe_) {
    loading_screen_.SetArchivePathProbe(loading_screen_archive_path_probe_);
  }

  if (dbc_) {
    loading_screen_.SetDbcLoader(dbc_);
  }

  if (texture_manager_ready && file_loader_) {
    cursor_manager_.PreloadCursorTextures();
  }

  BindMovementCollisionSource();

  world_frame_.Initialize(static_cast<std::uint32_t>(screen_width_),
                          static_cast<std::uint32_t>(screen_height_));
  world_frame_.SetTargetSelectionCallback([this](const ObjectGuid guid) {
    if (guid) {
      targeting_.SetTarget(guid.GetRawValue());
    } else {
      targeting_.ClearTarget();
    }
  });
  world_frame_.SetTerrainClickCallback([this](const game::targeting::WorldTerrainClick &click) {
    if (world_session() != nullptr) {
      game::targeting::ui::HandleWorldTerrainClick(*world_session(), click);
    }
  });

  post_process_.Init(static_cast<uint32_t>(screen_width_), static_cast<uint32_t>(screen_height_),
                     ReadPostProcessSettings());
  post_process_.InitGPU();

  binding_profiles_.Initialize();

  if (auto *const input = GetInputControlSingleton(); input != nullptr) {
    input->BindBindingProfiles(binding_profiles_);
  }
  binding_profiles_.SetProtectionGate(CanPerformBindingOperation);
  binding_profiles_.SetExecuteCallback([this](const BindingCommand &binding_command) {
    const std::string &command = binding_command.value();

    if (ResolveMovementBindingLuaCall(command).has_value()) {
      if (world_session() != nullptr) {
        if (command != BindingAction::kToggleRun) {
          ReleaseClickToMove(world_session());
        }
        (void)DispatchMovementBindingThroughLua(game_ui_.lua_state(), command, true);
      }
      return;
    }

    namespace BA = BindingAction;
    if (command == BA::kTargetNearestEnemy) {
      input_.PressAction(kInputTabTarget);
    } else {
      if (lua_State *L = game_ui_.lua_state()) {
        if (DispatchActionButtonBinding(L, command) || DispatchMultiActionBarBinding(L, command)) {
          return;
        } else if (command == BA::kToggleCharacter) {
          (void)openwow::ui::CallLuaGlobalIfFunction(L, "ToggleCharacter", "PaperDollFrame");
        } else if (command == BA::kToggleSpellBook) {
          (void)openwow::ui::CallLuaGlobalIfFunction(L, "ToggleSpellBook", "spell");
        } else if (command == BA::kToggleTalents) {
          (void)openwow::ui::CallLuaGlobalIfFunction(L, "ToggleTalentFrame");
        } else if (command == BA::kToggleQuestLog) {
          (void)openwow::ui::CallLuaGlobalIfFunction(L, "ToggleQuestLog");
        } else if (command == BA::kToggleSocial) {
          (void)openwow::ui::CallLuaGlobalIfFunction(L, "ToggleFriendsFrame");
        } else if (command == BA::kToggleWorldMap) {
          (void)openwow::ui::CallLuaGlobalIfFunction(L, "ToggleWorldMap");
        } else if (command == BA::kToggleBackpack) {
          (void)openwow::ui::CallLuaGlobalIfFunction(L, "OpenBackpack");
        } else if (command == BA::kToggleGameMenu) {
          (void)openwow::ui::CallLuaGlobalIfFunction(L, "ToggleGameMenu");
        } else if (command == BA::kOpenChat) {
          OpenChatEditBox(L, nullptr);
        } else if (command == BA::kOpenChatSlash) {
          OpenChatEditBox(L, "/");
        } else if (command == BA::kScreenshot) {
          (void)openwow::ui::CallLuaGlobalIfFunction(L, "Screenshot");
        } else {
          openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug,
                                    "BindingAssignment: unhandled execute command: " + command);
        }
      }
    }
  });
  binding_profiles_.SetReleaseCallback([this](const BindingCommand &binding_command) {
    const std::string &command = binding_command.value();
    if (ResolveMovementBindingLuaCall(command).has_value()) {
      if (world_session() != nullptr) {
        (void)DispatchMovementBindingThroughLua(game_ui_.lua_state(), command, false);
      }
      return;
    }

    namespace BA = BindingAction;
    if (command == BA::kTargetNearestEnemy) {
      input_.ReleaseAction(kInputTabTarget);
    }
  });

  floating_text_.Initialize();
  loading_screen_.Initialize();

  minimap_.Initialize(static_cast<float>(screen_width_));
  if (!debug_draw_renderer_.Initialize()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                              "GameLoop: debug draw shader initialization failed (non-fatal)");
  }
  const auto overlays_ready_at = StartupClock::now();

  initialized_ = true;
  state_ = SceneState::kGlue;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                            "GameLoop: initialized (" + std::to_string(screen_width_) + "x" +
                                std::to_string(screen_height_) + ")");
  const auto milliseconds = [](const auto duration) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
  };
  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kInfo,
      "GameLoop startup timing: total_ms=" +
          std::to_string(milliseconds(overlays_ready_at - startup_started)) + " texture_ms=" +
          std::to_string(milliseconds(texture_ready_at - startup_started)) + " world_scene_ms=" +
          std::to_string(milliseconds(world_scene_ready_at - texture_ready_at)) + " remaining_ms=" +
          std::to_string(milliseconds(overlays_ready_at - world_scene_ready_at)));
  return true;
}

void GameLoop::Tick(float dt) {
  if (!initialized_)
    return;

  static_cast<void>(texture_manager_.PumpPreparedUploads(8u));

  if (const auto *session = world_session(); session != nullptr) {
    VoiceChat_ScheduledUpdate(*session, sound_runtime_.sound_engine(),
                              sound_runtime_.voice_loopback(),
                              openwow::core::GameClock::GetTickCount32());
  }

  switch (state_) {
  case SceneState::kGlue:
    TickGlue(dt);
    break;
  case SceneState::kLoading:
    TickLoading(dt);
    break;
  case SceneState::kInWorld:
    TickInWorld(dt);
    break;
  }

  if (world_session() != nullptr && game_ui_.is_initialized()) {
    auto inventory_changes = world_session()->TakeInventoryPresentationChanges();
    for (const auto &result : inventory_changes.item_pushes) {
      inventory::ui::PresentItemPushResult(
          world_session()->objects(), world_session()->query_cache(),
          world_session()->GetDbcLoader(), result,
          {
              .localization = &Localization::Get(),
              .tutorials = &TutorialSystem::Instance(),
              .display_loot_message =
                  [this](std::string message) {
                    ChatFrame_DisplayMessage(world_session()->objects(), message.c_str(),
                                             ChatDisplayType::kLoot, nullptr, 0, nullptr, nullptr,
                                             nullptr, 0, 0, 0, 0, 0, nullptr);
                  },
              .fire_item_push =
                  [](const int slot, std::string icon) {
                    openwow::ui::game::ScriptEventDispatch::Get().FireEventArgs(
                        openwow::ui::game::events::ITEM_PUSH, {slot, std::move(icon)});
                  },
          });
    }
    if (inventory_changes.equipment.has_value()) {
      world_scene_.ApplyEquipmentPresentation(*inventory_changes.equipment);
    }
    auto &item_interactions = world_session()->item_interactions();
    auto &events = openwow::ui::game::ScriptEventDispatch::Get();
    for (const auto &change : inventory_changes.item_lifecycle) {
      if (item_interactions.readable().has_value() &&
          item_interactions.readable()->item.GetRawValue() == change.item_guid) {
        item_interactions.close_readable();
        events.FireEvent(openwow::ui::game::events::ITEM_TEXT_CLOSED);
      }
      if (!item_interactions.socket().has_value() ||
          change.kind != WorldSession::ItemLifecycleKind::kRemoved) {
        continue;
      }
      if (item_interactions.socket()->item.GetRawValue() == change.item_guid) {
        item_interactions.cancel_socket();
        events.FireEvent(openwow::ui::game::events::SOCKET_INFO_CLOSE);
        continue;
      }
      const auto &socket = *item_interactions.socket();
      for (std::size_t index = 0; index < socket.pending_gems.size(); ++index) {
        if (socket.pending_gems[index].has_value() &&
            socket.pending_gems[index]->item.GetRawValue() == change.item_guid) {
          (void)item_interactions.place_socket_gem(index, std::nullopt);
          events.FireEvent(openwow::ui::game::events::SOCKET_INFO_UPDATE);
          break;
        }
      }
    }

    const auto changes = world_session()->inventory_commands().TakeChanges();
    for (const auto guid : changes.mouseover_items) {
      openwow::ui::game::GameUI_OnMouseoverUnitEnter(guid);
    }
    for (const auto index : changes.bind_confirmations) {
      openwow::ui::game::ScriptEventDispatch::Get().FireEventArgs(
          openwow::ui::game::events::AUTOEQUIP_BIND_CONFIRM, {static_cast<int>(index)});
    }

    for (const auto index : changes.swap_bind_confirmations) {
      openwow::ui::game::ScriptEventDispatch::Get().FireEventArgs(
          openwow::ui::game::events::EQUIP_BIND_CONFIRM, {static_cast<int>(index)});
    }
  }
}

void GameLoop::Shutdown() {
  ReleaseClickToMove(world_session());
  held_cursor_source_.BindSpellTargeting(nullptr);
  ClearVehicleDescriptorRenderReadyCallback();
  ClearCorpseSupportSurfaceHeightCallback();
  ClearCorpseCharacterAppearanceReadyCallback();
  ClearJumpLiquidSurfaceHeightCallback();
  ClearWaterRippleSpawnCallback();
  ClearCalcGroundPosCallback();
  ClearUnitSoundGroundStateCallback();

  if (initialized_ && state_ != SceneState::kGlue) {
    LeaveWorld();
  } else if (game_ui_.is_initialized()) {
    DetachWorldUiMacroPresentation();
    game_ui_.Shutdown();
    ShutdownCursorSurface();
  }

  BindCharacterAudioAndMediaAdapters(sound_runtime_, nullptr, nullptr);
  openwow::net::SetClientServicesPacketSendFn({});
  openwow::core::SetRetailDebugCommandBindings({});
  openwow::core::Console_UnregisterDebugCommands();
  idle_billing_.Reset();
  if (!initialized_) {

    DetachWorldUiMacroPresentation();
    game_ui_.Shutdown();
    ShutdownCursorSurface();
    if (world_session() != nullptr) {
      world_session()->BindTargetingSystem(nullptr);
      world_session()->SetLootSourceTargetSelectionCallback({});
      world_session()->SetTrackedGuidInvalidationCallback({});
    }
    targeting_.Initialize(nullptr);
    character_world_runtime_ = nullptr;
    world_ui_session_commands_.BindSession(nullptr);
    return;
  }
  world_scene_.camera().ResetScriptedMoveInputs();
  binding_profiles_.Shutdown();
  chat_frame_.Shutdown();
  floating_text_.Shutdown();
  HideLoadingScreen();
  loading_screen_.Shutdown();
  loot_frame_.Shutdown();
  minimap_.Shutdown();
  post_process_.Shutdown();
  debug_draw_renderer_.Shutdown();
  world_scene_.Shutdown();
  m2_system_.Shutdown();
  ShutdownCursorSurface();
  texture_manager_.Shutdown();
  BindSpellTextFormatterWorldSession(nullptr);
  if (world_session() != nullptr) {
    world_session()->BindTargetingSystem(nullptr);
    world_session()->SetLootSourceTargetSelectionCallback({});
    world_session()->SetTrackedGuidInvalidationCallback({});
    world_session()->SetLocalPlayerCombatFlagChangedCallback({});
    world_session()->SetAutoAttackCombatEventCallback({});
    world_session()->SetCancelCombatCallback({});
  }
  targeting_.Initialize(nullptr);
  character_world_runtime_ = nullptr;
  world_ui_session_commands_.BindSession(nullptr);
  packet_queue_ = nullptr;
  state_ = SceneState::kGlue;
  prepared_world_entry_ = false;
  initialized_ = false;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "GameLoop: shutdown");
}

void GameLoop::ShutdownCursorSurface() {

  if (!cursor_manager_.IsInitialized()) {
    return;
  }
  cursor_manager_.Shutdown();
}

void GameLoop::EnterWorld(std::uint32_t map_id, float x, float y, float z, float orientation,
                          const std::string &map_name) {
  if (prepared_world_entry_) {
    AbortPreparedWorldEntry();
  }

  openwow::net::ClientServices::Instance().CompleteCharacterLoginTransition(false);

  current_map_id_ = map_id;
  player_x_ = x;
  player_y_ = y;
  player_z_ = z;
  player_orientation_ = orientation;
  current_map_name_ = ResolveMapName(map_id, map_name);
  ResetEnterWorldRuntimeState();
  prepared_world_entry_ = false;

  PrepareWorldEntryRuntime();
  FinalizeWorldEntryRuntime();

  if (world_session() && client_time_fn_) {
    world_session()->StartBotDetectedCountdown(client_time_fn_());
  }
}

void GameLoop::PrepareWorldEntry(std::uint32_t map_id, float x, float y, float z,
                                 const std::string &map_name) {
  current_map_id_ = map_id;
  player_x_ = x;
  player_y_ = y;
  player_z_ = z;
  current_map_name_ = ResolveMapName(map_id, map_name);
  ResetEnterWorldRuntimeState();
  prepared_world_entry_ = true;

  PrepareWorldEntryRuntime();
}

bool GameLoop::FinalizePreparedWorldEntry(std::uint32_t map_id, float x, float y, float z,
                                          float orientation, const std::string &map_name) {
  if (!prepared_world_entry_) {
    return false;
  }

  if (current_map_id_ != map_id) {
    AbortPreparedWorldEntry();

    current_map_id_ = map_id;
    current_map_name_ = ResolveMapName(map_id, map_name);
    ResetEnterWorldRuntimeState();
    prepared_world_entry_ = true;
    PrepareWorldEntryRuntime();
  } else if (!map_name.empty()) {
    current_map_name_ = ResolveMapName(map_id, map_name);
  }

  player_x_ = x;
  player_y_ = y;
  player_z_ = z;
  player_orientation_ = orientation;

  FinalizeWorldEntryRuntime();
  return true;
}

void GameLoop::AbortPreparedWorldEntry() {
  if (!prepared_world_entry_) {
    return;
  }

  sound_runtime_.ResetWorldAudioStateForGlue();
  world_scene_.world_map().SetBlockingLoadProgressCallback({});
  HideLoadingScreen();
  world_scene_.UnloadMap();
  world_frame_.BindObjectPresentation(nullptr);
  area_trigger_system_.Cleanup();

  state_ = SceneState::kGlue;
  prepared_world_entry_ = false;
}

std::string GameLoop::ResolveMapName(const std::uint32_t map_id,
                                     const std::string &map_name) const {
  if (!map_name.empty()) {
    return map_name;
  }

  if (dbc_) {
    if (const auto *entry = dbc_->map().LookupEntry(map_id)) {
      return std::string(entry->internal_name);
    }
  }

  return "Map_" + std::to_string(map_id);
}

void GameLoop::ResetWorldStateUiRuntimeForEnterWorld() {

  synchronized_zone_map_id_ = -1;
  synchronized_zone_id_ = -1;
  synchronized_area_id_ = -1;

  synchronized_wmo_group_area_id_ = -1;
  synchronized_wmo_root_area_id_ = -1;
  synchronized_wmo_interior_group_ = false;
  zone_ui_state_.Reset();
  if (world_session() != nullptr) {
    world_session()->scene_state().Reset();
  }
}

void GameLoop::ResetEnterWorldRuntimeState() {

  death_manager_.ResetForPlayerEnterWorld();
  area_trigger_system_.Cleanup();
  world_ui_lifecycle_.BeginPlayerWorldTransition();

  world_scene_.RetireDestroyedObjectGeneration();
}

bool GameLoop::HasLocalPlayerForWorldUi() const {
  return world_session() != nullptr && world_session()->IsInWorld() &&
         world_session()->objects().GetLocalPlayer() != nullptr;
}

bool GameLoop::PrepareLocalPlayerForWorldUi() {
  return game_ui_.is_initialized() && event_bridge_.SynchronizePlayerUnitToken();
}

void GameLoop::RunPreEnterLocalPlayerWorldUiSetup() {
  if (const auto *session = world_session(); session != nullptr) {
    LCD_SetupPlayerClassDisplay(session->objects());
  }
}

void GameLoop::RunLocalPlayerWorldUiFanout() {
  if (world_session() != nullptr) {
    RefreshCombatBindingState(binding_profiles_, *world_session(), targeting_);
  }
}

void GameLoop::FireWorldUiLifecycleEvent(const openwow::ui::game::WorldUiLifecycleEvent event) {
  switch (event) {
  case openwow::ui::game::WorldUiLifecycleEvent::VariablesLoaded:
    game_ui_.frame_events().OnVariablesLoaded();
    break;
  case openwow::ui::game::WorldUiLifecycleEvent::PlayerLogin:
    game_ui_.frame_events().OnPlayerLogin();
    break;
  case openwow::ui::game::WorldUiLifecycleEvent::PlayerEnteringWorld:
    if (world_session()) {
      const auto active_player_guid =
          world_session()->objects().GetActivePlayerGuid();
      const bool combat_text_enabled =
          ui::game::CVarSystem::Instance().GetCVarBool("enableCombatText");

      CombatText_SetActiveUnitGuid(
          combat_text_enabled ? active_player_guid.GetRawValue() : 0u);
      if (combat_text_enabled) {
        if (auto* runtime =
                openwow::ui::game::runtime::WorldUiRuntimeContext::FromLua(
                    game_ui_.lua_state());
            runtime != nullptr) {
          if (auto* loader = runtime->addon_runtime_loader();
              loader != nullptr) {
            openwow::ui::game::AddonLoadLogSession load_log;
            if (!loader->Load(
                    "Blizzard_CombatText",
                    openwow::ui::game::AddonRuntimeLoadContext{
                        .identity = game_ui_.persistence_identity(),
                        .status_sink = load_log.sink(),
                        .allow_load_on_demand = true})) {
              openwow::diagnostics::Log(
                  openwow::diagnostics::LogLevel::kWarn,
                  "GameLoop: failed to load Blizzard_CombatText at world entry");
            }
          }
        }
      }
    }
    game_ui_.frame_events().OnPlayerEnteringWorld();
    if (world_session() != nullptr) {

      const auto* const dbc = world_session()->GetDbcLoader();
      const auto* const map_entry =
          (dbc != nullptr && world_session()->has_current_map())
              ? dbc->map().LookupEntry(world_session()->current_map_id())
              : nullptr;
      if (map_entry != nullptr && map_entry->map_type - 3u < 2u) {
        openwow::ui::game::ScriptEventDispatch::Get().FireEvent(
            openwow::ui::game::events::PLAYER_ENTERING_BATTLEGROUND);
      }

      ChatFrame_SetWorldUiReadyAndFlush(world_session()->objects());
    }
    break;
  case openwow::ui::game::WorldUiLifecycleEvent::PlayerLeavingWorld:
    game_ui_.frame_events().OnPlayerLeavingWorld();
    ChatFrame_ResetWorldUiReady();
    break;
  case openwow::ui::game::WorldUiLifecycleEvent::PlayerLogout:
    game_ui_.frame_events().OnPlayerLogout();
    break;
  }
}

bool GameLoop::IsWorldUiReloadBlockedByCinematic() const {
  return cinematic_player_.IsPlaying();
}

void GameLoop::PrepareWorldUiForReload() {
  if (!world_session() || !game_ui_.is_initialized()) {
    return;
  }
  game_ui_.retained_layout().RefreshTrackedLayout();
  game_ui_.SetViewportSize(static_cast<float>(screen_width_), static_cast<float>(screen_height_));
  openwow::net::ClientServices::Instance().CompleteCharacterLoginTransition(true);
}

void GameLoop::PrepareWorldEntryRuntime() {
  openwow::vfs::SetDataPreloadRequestedState(2);
  const std::uint32_t now_ms =
      client_time_fn_ ? client_time_fn_() : openwow::core::GameClock::GetTickCount32();
  loading_screen_progress_pump_.Reset(true, now_ms);
  state_ = SceneState::kLoading;

  const bool preserve_transport_hold =
      openwow::screens::LoadingScreenManager::Get().HasTransportWorldEntryHold();
  ShowLoadingScreen(current_map_id_);
  if (preserve_transport_hold) {
    openwow::screens::LoadingScreenManager::Get().SetTransportWorldEntryHold(true);
  }

  world_scene_.world_map().SetWorldEntryStreamingMode(true);

  world_scene_.world_map().SetBlockingLoadProgressCallback([this](float progress) {
    HandleLoadingScreenProgress(openwow::screens::LoadingProgressInput{}, progress);
  });

  openwow::data::AsyncFile_SetWaitCallbacks(nullptr, nullptr);
  world_scene_.LoadMap(current_map_id_, current_map_name_);
  world_scene_.UpdatePlayerPosition(player_x_, player_y_, player_z_);
  world_scene_.world_map().SetBlockingLoadProgressCallback({});
}

void GameLoop::FinalizeWorldEntryRuntime() {

  Player_C_ResetAreaStateCache();
  Player_C_ResetAreaTickCounter();
  if (world_session() != nullptr) {
    auto &world_states = world_session()->world_states();
    world_states.SetMapId(-1);
    world_states.SetZoneId(0);
    world_states.SetAreaId(0);
  }

  if (world_session()) {
    world_session()->chat().SetOnMessage([this](const ChatMessage &msg) {
      chat_frame_.AddMessage(static_cast<std::uint8_t>(msg.type), msg.sender_name, msg.message, 0,
                             msg.channel_name);
    });

    world_session()->combat_log().SetOnEvent([this](const CombatEvent &evt) {
      const auto& objects = world_session()->objects();
      const ObjectGuid active_player_guid = objects.GetActivePlayerGuid();
      const ObjectGuid source_guid(evt.source);
      const ObjectGuid target_guid(evt.target);
      const auto *target_obj = objects.Get(target_guid);
      if (!target_obj)
        return;
      const float wx = target_obj->GetX();
      const float wy = target_obj->GetY();
      const float wz = target_obj->GetZ() + 2.5f;

      const auto* source_unit = objects.GetUnit(source_guid);
      const bool source_is_direct_player = source_guid == active_player_guid;
      const bool source_is_active_player_controlled =
          source_is_direct_player ||
          (source_unit != nullptr &&
           source_unit->Interaction().GetControllingPlayerGuid() ==
               active_player_guid);
      const auto& cvars = ui::game::CVarSystem::Instance();

      switch (evt.type) {
      case CombatEventType::kMeleeAttack:
      case CombatEventType::kSpellDamage:
      case CombatEventType::kPeriodicDamage: {
        if (target_guid == active_player_guid ||
            !source_is_active_player_controlled) {
          break;
        }

        const bool is_melee = evt.type == CombatEventType::kMeleeAttack;
        const auto* dbc = world_session()->GetDbcLoader();
        const auto* spell =
            dbc != nullptr && evt.spell_id != 0
                ? dbc->spell().LookupEntry(evt.spell_id)
                : nullptr;
        const bool is_physical_type =
            is_melee || spell == nullptr ||
            (spell->attributes_ex3 & 0x8000u) != 0u;
        const auto display = BuildDamageFctDisplay(
            static_cast<std::int32_t>(evt.amount), is_physical_type,
            evt.critical, source_is_direct_player,
            evt.type == CombatEventType::kPeriodicDamage,
            cvars.GetCVarBool("CombatDamage"),
            cvars.GetCVarBool("CombatLogPeriodicSpells"),
            cvars.GetCVarBool("PetMeleeDamage"),
            cvars.GetCVarBool("PetSpellDamage"));
        if (display.has_value()) {
          const std::uint32_t color =
              display->color.value_or(0xFFFFFFFFu);
          floating_text_.AddText(
              wx, wy, wz, display->text, color,
              static_cast<std::uint32_t>(display->type), display->color);
        }
        break;
      }
      case CombatEventType::kSpellMiss: {
        if (target_guid == active_player_guid ||
            !source_is_active_player_controlled ||
            !cvars.GetCVarBool("CombatDamage")) {
          break;
        }
        if (!source_is_direct_player) {
          const char* const pet_cvar =
              evt.spell_id == 0 ? "PetMeleeDamage" : "PetSpellDamage";
          if (!cvars.GetCVarBool(pet_cvar)) {
            break;
          }
        }
        switch (evt.miss_type) {
        case MissType::Miss:
          floating_text_.AddMiss(wx, wy, wz);
          break;
        case MissType::Dodge:
          floating_text_.AddDodge(wx, wy, wz);
          break;
        case MissType::Parry:
          floating_text_.AddParry(wx, wy, wz);
          break;
        case MissType::Block:
          floating_text_.AddText(wx, wy, wz, "BLOCK", 0xFFFFFFFF);
          break;
        case MissType::Resist:
          floating_text_.AddResist(wx, wy, wz);
          break;
        case MissType::Absorb:
          floating_text_.AddText(wx, wy, wz, "ABSORB", 0xFFFFFF00);
          break;
        case MissType::Deflect:
          floating_text_.AddText(wx, wy, wz, "DEFLECT", 0xFFFFFFFF);
          break;
        case MissType::Immune:
          floating_text_.AddImmune(wx, wy, wz);
          break;
        case MissType::Evade:
          floating_text_.AddText(wx, wy, wz, "EVADE", 0xFFFFFFFF);
          break;
        case MissType::Reflect:
          floating_text_.AddText(wx, wy, wz, "REFLECT", 0xFFFFFFFF);
          break;
        }
        break;
      }
      case CombatEventType::kSpellHeal:
      case CombatEventType::kPeriodicHeal: {
        const auto *target_unit = objects.GetUnit(target_guid);
        const bool target_is_active_player_controlled =
            target_unit != nullptr &&
            (target_unit->GetGuid() == active_player_guid ||
             target_unit->Interaction().GetControllingPlayerGuid() ==
                 active_player_guid);
        const auto display = BuildHealingFctDisplay(
            static_cast<std::int32_t>(evt.amount), target_guid,
            active_player_guid, objects.GetMouseoverGuid(), evt.critical,
            cvars.GetCVarBool("CombatHealing"),
            target_is_active_player_controlled);
        if (display.has_value()) {
          const bool critical = display->type == HealingFctDisplayType::kPeriodicHealCrit;
          floating_text_.AddHealing(wx, wy, wz, evt.amount, critical,
                                    static_cast<std::uint32_t>(display->type));
        }
        break;
      }
      case CombatEventType::kSpellEnergize:
      case CombatEventType::kPeriodicEnergize: {

        if (evt.power_drain) {
          break;
        }
        const auto *target_unit = objects.GetUnit(target_guid);
        const auto display = BuildPowerGainFctDisplay(
            static_cast<std::int32_t>(evt.amount), target_guid,
            active_player_guid, cvars.GetCVarBool("CombatDamage"),
            target_unit != nullptr && target_unit->IsPlayer());
        if (display.has_value()) {
          floating_text_.AddText(wx, wy, wz, display->text, display->color,
                                 static_cast<std::uint32_t>(display->type), display->color);
        }
        break;
      }
      case CombatEventType::kSpellMechanic:
        if (!evt.mechanic_text.empty()) {
          floating_text_.AddText(wx, wy, wz, evt.mechanic_text,
                                 0xFFFFFFFFu);
        }
        break;
      case CombatEventType::kHonorKill: {

        const auto display = BuildHonorKillFctDisplay(
            static_cast<std::int32_t>(evt.amount), evt.honor_rank,
            evt.honor_rank_title);
        floating_text_.AddText(
            wx, wy, wz, display.text, 0xFFFFFFFFu,
            static_cast<std::uint32_t>(display.type), std::nullopt,
            display.rank);
        break;
      }
      default:
        break;
      }
    });
  }

  chat_frame_.SetSendCallback(
      [this](std::uint8_t type, const std::string &message, const std::string &target) {
        if (!world_session())
          return;
        auto pkt = ChatManager::BuildChatMessage(static_cast<ChatMsg>(type), Language::kCommon,
                                                 message, target);
        if (send_packet_fn_) {
          send_packet_fn_(pkt);
        }
      });

  auto &movement = movement_controller();
  movement.SetPosition(player_x_, player_y_, player_z_, player_orientation_);
  BindMovementCollisionSource();

  prev_actions_ = 0;
  ReleaseClickToMove(world_session());

  if (window_) {
    cursor_manager_.Initialize(window_);
  }

  InputControl_ResetCursorVisibilityForUiInit();

  world_scene_.UpdatePlayerPosition(player_x_, player_y_, player_z_);

  openwow::ui::game::detail::SyncCameraMotionSettings(world_scene_.camera());
  openwow::ui::game::detail::SyncCameraViewPresets(world_scene_.camera());
  const auto &camera_cvars = openwow::ui::game::CVarSystem::Instance();
  const int camera_view =
      camera_cvars.Exists("cameraView") ? camera_cvars.GetCVarInt("cameraView") : 2;
  world_scene_.camera().ResetForWorldEntry(player_x_, player_y_, player_z_, player_orientation_,
                                           camera_view);

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                            "GameLoop: EnterWorld map=" + std::to_string(current_map_id_) +
                                " name=" + current_map_name_ + " pos=(" +
                                std::to_string(player_x_) + ", " + std::to_string(player_y_) +
                                ", " + std::to_string(player_z_) + ")");

  if (!game_ui_.is_initialized()) {

    WaitForTrialStartRacePreloadGate();
    openwow::vfs::SetDataPreloadRequestedState(3);
    if (world_ui_lifecycle_.Start([this](float progress) {
          HandleLoadingScreenProgress(openwow::screens::LoadingProgressInput{}, progress);
        }) != openwow::ui::game::WorldUiStartResult::Started) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
                                "GameLoop: required in-world UI failed to start");
      LeaveWorld();
      return;
    }
  } else {
    openwow::vfs::SetDataPreloadRequestedState(3);
  }
  (void)PumpWorldEntryProtocolControlPackets();

  minimap_.OnMapChanged(current_map_id_, current_map_name_);

  if (send_packet_fn_) {
    loot_frame_.SetSendFn([this](const net::wotlk::WorldPacket &pkt) {
      if (send_packet_fn_)
        send_packet_fn_(pkt);
    });
  }

  if (world_session()) {
    world_scene_.InitializeSpellVisuals();
    world_session()->SetSpellVisualCallbacks({});

    death_manager_.Initialize(send_packet_fn_);

    cinematic_player_.BindDbc(dbc_);
    if (send_packet_fn_) {
      cinematic_player_.SetSendPacketFn(
          [this](std::uint16_t opcode, const std::uint8_t *data, std::size_t len) {
            net::wotlk::WorldPacket pkt(static_cast<net::wotlk::Opcode>(opcode));
            if (data && len > 0) {
              pkt.payload.assign(data, data + len);
            }
            if (send_packet_fn_)
              send_packet_fn_(pkt);
          });
    }
    cinematic_player_.SetStopCallback([this]() {
      if (world_session()) {
        world_session()->spell_visual().StopCinematic();
      }
    });
    world_session()->SetCinematicCallbacks({
        .on_trigger_cinematic =
            [this](std::uint32_t cinematic_sequence_id) {
              cinematic_player_.PlaySequence(*world_session(), cinematic_sequence_id);
            },
        .on_stop_cinematic = [this]() { cinematic_player_.Stop(*world_session()); },
    });

    const auto request_resurrect_offerer_name = [this](const std::uint64_t guid) {
      if (guid == 0) {
        return;
      }

      (void)world_session()->query_cache().RequestNameQuery(guid);
    };

    world_session()->SetNameQueryResponseCallback(
        [this](const std::uint64_t guid, const bool name_unknown) {
          const auto resolution = death_manager_.HandleResurrectOffererNameQueryResult(
              guid, name_unknown, world_session()->query_cache());
          if (!game_ui_.is_initialized() || resolution.fire_count == 0 ||
              !ActivePlayerCanReceiveResurrectRequestEvent(*world_session())) {
            return;
          }

          for (std::uint32_t i = 0; i < resolution.fire_count; ++i) {
            game_ui_.frame_events().OnResurrectRequest(resolution.offerer_name);
          }
        });

    const auto release_timer_context = [this]() {
      ReleaseTimerContext timer_ctx;
      if (const auto *player = world_session()->objects().GetLocalPlayerTyped()) {

        constexpr std::uint32_t kPlayerFlagsGhost = 0x00000010u;
        constexpr std::uint32_t kPlayerFlagsIsOutOfBounds = 0x00004000u;
        constexpr std::uint32_t kPlayerFieldByteReleaseTimer = 0x00000008u;
        constexpr std::uint32_t kPlayerFieldByteNoReleaseWindow = 0x00000010u;
        const std::uint32_t pflags = player->GetUInt32(PLAYER_FLAGS);
        const std::uint32_t field_bytes =
            player->GetUInt32(PLAYER_FIELD_ACTION_BAR_TOGGLES);
        timer_ctx.is_ghost = (pflags & kPlayerFlagsGhost) != 0;
        timer_ctx.has_release_timer =
            (field_bytes & kPlayerFieldByteReleaseTimer) != 0;
        timer_ctx.no_release_window =
            (field_bytes & kPlayerFieldByteNoReleaseWindow) != 0;
        timer_ctx.is_out_of_bounds = (pflags & kPlayerFlagsIsOutOfBounds) != 0;
      }
      return timer_ctx;
    };

    const auto fire_player_dead = [this]() {
      if (game_ui_.is_initialized()) {
        game_ui_.frame_events().OnPlayerDead();
      }
    };

    world_session()->SetDeathCallbacks({
        .on_dead =
            [this, release_timer_context, fire_player_dead](
                const bool force_event_dispatch) {
              const auto timer_ctx = release_timer_context();
              const bool became_dead =
                  death_manager_.HandlePlayerDeath(timer_ctx);
              if (became_dead) {

                std::uint32_t move_flags = 0u;
                std::uint32_t player_flags = 0u;
                if (const auto *const player =
                        world_session()->objects().GetLocalPlayerTyped();
                    player != nullptr) {
                  move_flags = player->GetMovementInfo().flags;
                  player_flags = player->GetUInt32(PLAYER_FLAGS);
                }
                std::ostringstream death_state;
                death_state << "DeathManager: player died move_flags=0x"
                            << std::hex << std::uppercase << move_flags
                            << " player_flags=0x" << player_flags << std::dec
                            << std::nouppercase
                            << " ghost=" << timer_ctx.is_ghost
                            << " timer=" << timer_ctx.has_release_timer
                            << " no_window=" << timer_ctx.no_release_window
                            << " oob=" << timer_ctx.is_out_of_bounds;
                openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                                          death_state.str());

              }
              if (became_dead || force_event_dispatch) {
                fire_player_dead();
              }
            },
        .on_refresh_release_timer_mode =
            [this, release_timer_context]() {
              death_manager_.RefreshReleaseTimeCountdownMode(
                  release_timer_context());
            },
        .on_death_release_loc =
            [this, release_timer_context]() {

              death_manager_.HandleDeathReleaseLoc(world_session()->misc(),
                                                   release_timer_context());
            },
        .on_corpse_reclaim_delay =
            [this]() {
              death_manager_.HandleCorpseReclaimDelay(world_session()->misc());
              UpdateCorpseProximityState(true);
            },
        .on_corpse_position_cleared =
            [this]() { ClearCorpseProximityState(); },
        .on_resurrect_request =
            [this, request_resurrect_offerer_name]() {
              death_manager_.HandleResurrectRequest(world_session()->resurrect_request());
              if (!ActivePlayerCanReceiveResurrectRequestEvent(*world_session())) {
                return;
              }

              const auto offerer_name = death_manager_.ResolveResurrectRequestEventOfferer(
                  world_session()->query_cache(), request_resurrect_offerer_name);
              if (game_ui_.is_initialized() && offerer_name) {
                game_ui_.frame_events().OnResurrectRequest(*offerer_name);
              }
            },
        .on_alive =
            [this, release_timer_context](
                const bool ,
                const bool ) {

              const bool is_ghost = release_timer_context().is_ghost;
              const bool became_alive = death_manager_.HandleAlive(is_ghost);
              if (!is_ghost) {
                ClearCorpseProximityState();
              }
              return became_alive;
            },
        .on_spirit_healer_confirm =
            [this](const std::int32_t xp_loss) {

              if (game_ui_.is_initialized()) {
                game_ui_.frame_events().OnConfirmXpLoss(xp_loss);
              }
            },
    });
  }

  if (world_session() != nullptr &&
      world_scene_.world_map().IsWorldEntryStreamingComplete() &&
      world_scene_.IsDoodadWorldEntryLoadDrained()) {
    (void)world_session()->TrySendPendingWorldportAck();
  }

  UpdateLoadingTrackedPlayerState();
  ArmTransportWorldEntryHoldForRidingPlayer();

  state_ = SceneState::kLoading;
  (void)TryCompleteLoadingScreenWorldEntry();
  const std::uint32_t now_ms =
      client_time_fn_ ? client_time_fn_() : openwow::core::GameClock::GetTickCount32();
  const auto &client_services = openwow::net::ClientServices::Instance();
  idle_billing_.ResetForWorldEntry(
      now_ms, {.time_remaining_minutes = client_services.GetBillingTimeRemaining(),
               .flags = client_services.GetBillingFlags()});
  prepared_world_entry_ = false;
}

void GameLoop::WaitForTrialStartRacePreloadGate() {
  auto &loading_state = openwow::screens::LoadingScreenManager::Get();
  if (!loading_state.IsTrialMode() || openwow::vfs::IsCurrentDataPreloadRaceReadyForLoading()) {
    return;
  }

  const int selected_race = openwow::vfs::GetDataPreloadSelectedRace();
  while (!openwow::vfs::IsCurrentDataPreloadRaceReadyForLoading()) {
    const std::uint32_t now_ms =
        client_time_fn_ ? client_time_fn_() : openwow::core::GameClock::GetTickCount32();
    const float trial_alpha =
        static_cast<float>(openwow::vfs::GetStartRaceDataPreloadProgress(selected_race));
    const auto result =
        loading_screen_progress_pump_.AdvanceTrialLoadingAlpha(trial_alpha, now_ms, true, false);
    if (result.should_present) {
      if (result.should_send_keep_alive && send_packet_fn_) {
        send_packet_fn_(openwow::net::wotlk::PacketSender::BuildKeepAlive());
      }
      PresentBlockingLoadingFrame();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

void GameLoop::PrimeTransferPendingLoadingScreen(const TransferPendingInfo &pending) {
  auto &loading_state = openwow::screens::LoadingScreenManager::Get();

  ShowLoadingScreen(pending.map_id);

  if (pending.has_map_change_details && pending.transport_entry != 0 &&
      pending.previous_map_id != kTransferPendingNoPreviousMap) {
    std::uint32_t current_transport_entry = 0;
    std::uint32_t loading_path_id = 0;
    if (world_session()) {
      if (const auto *local_player = world_session()->objects().GetLocalPlayerTyped()) {
        const auto &movement = local_player->GetMovementInfo();
        if (movement.IsOnTransport() && !movement.transport.guid.IsEmpty()) {
          if (const auto *transport =
                  world_session()->transport_mgr().GetTransport(movement.transport.guid)) {
            current_transport_entry = transport->GetEntry();
            loading_path_id = transport->GetPath().pathId;
          }
        }
      }
    }

    const bool overlay_ready = openwow::screens::UpdateLoadingScreenForMapChange(
        pending.transport_entry, pending.previous_map_id, current_transport_entry,
        loading_path_id);
    loading_state.SetTransportWorldEntryHold(overlay_ready);

    const std::uint32_t now_ms =
        client_time_fn_ ? client_time_fn_() : openwow::core::GameClock::GetTickCount32();
    loading_screen_progress_pump_.Reset(true, now_ms);
    return;
  }

  openwow::core::LoadingScreen_InitFont(static_cast<int>(pending.map_id), true);
  const std::uint32_t now_ms =
      client_time_fn_ ? client_time_fn_() : openwow::core::GameClock::GetTickCount32();
  loading_screen_progress_pump_.Reset(true, now_ms);
}

void GameLoop::LeaveWorld() {
  if (state_ == SceneState::kGlue)
    return;

  ReleaseClickToMove(world_session());
  held_cursor_source_.BindSpellTargeting(nullptr);
  auto sound_block = sound_runtime_.BlockNonPositionalPlayback();

  cinematic_player_.AbortForWorldLeave(*world_session());
  sound_runtime_.ResetWorldAudioStateForGlue();
  openwow::vfs::SetDataPreloadRequestedState(1);

  world_ui_lifecycle_.Stop(openwow::ui::game::WorldUiStopReason::WorldLeave);
  UnitFrameDataProvider::Get().Reset();
  sound_runtime_.StopAllActiveSounds();

  if (world_session()) {
    world_session()->Logout();
  }

  AccountData::Get().DeactivateSlotsByScope(true);
  AccountData::Get().ReleaseBoundPersistenceIdentity();

  openwow::ui::ResetRuntimeCursorTextureState();

  BattleNetApi::Instance().ResetFriendListStateForGlueTransition();

  if (world_session() != nullptr) {
    world_session()->loot().state().ClearPendingRolls();
  }

  loot_frame_.Hide();
  world_scene_.camera().ResetScriptedMoveInputs();

  world_scene_.UnloadMap();

  floating_text_.Clear();

  if (packet_queue_) {
    packet_queue_->Clear();
  }
  packet_queue_ = nullptr;

  Player_C_ResetAreaStateCache();
  Player_C_ResetAreaTickCounter();

  area_trigger_system_.Cleanup();
  HideLoadingScreen();
  openwow::vfs::SetDataPreloadRequestedState(0);

  state_ = SceneState::kGlue;
  prepared_world_entry_ = false;
  corpse_proximity_active_ = false;
  idle_billing_.Reset();
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "GameLoop: LeaveWorld → glue");
}

void GameLoop::SetScreenSize(int width, int height) {
  const int clamped_width = std::max(1, width);
  const int clamped_height = std::max(1, height);

  InputControl_RefreshViewportAspect();

  if (clamped_width == screen_width_ && clamped_height == screen_height_) {
    return;
  }

  screen_width_ = clamped_width;
  screen_height_ = clamped_height;
  if (game_ui_.is_initialized()) {
    game_ui_.SetViewportSize(static_cast<float>(screen_width_), static_cast<float>(screen_height_));
  }
  minimap_.OnScreenResize(static_cast<float>(screen_width_));
  (void)world_scene_.SetScreenSize(static_cast<std::uint32_t>(screen_width_),
                                   static_cast<std::uint32_t>(screen_height_));
  world_frame_.Resize(static_cast<std::uint32_t>(screen_width_),
                      static_cast<std::uint32_t>(screen_height_));
  (void)post_process_.Resize(static_cast<uint32_t>(screen_width_),
                             static_cast<uint32_t>(screen_height_));
}

void GameLoop::HandleMouseDelta(float dx, float dy) {
  if (state_ == SceneState::kInWorld) {
    NoteUserActivity();

    (void)openwow::ui::game::detail::HandleWorldMouseDelta(
        game_ui_.lua_state(), dx, dy);
  }
}

void GameLoop::BeginCameraFreelook() {
  if (state_ != SceneState::kInWorld) {
    return;
  }

  NoteUserActivity();

  world_scene_.camera().EnterFreelook();
}

void GameLoop::EndCameraFreelook() {
  if (state_ != SceneState::kInWorld) {
    return;
  }

  NoteUserActivity();
  world_scene_.camera().ExitFreelook();
}

void GameLoop::HandleScrollDelta(float delta) {
  if (state_ == SceneState::kInWorld) {
    NoteUserActivity();
    world_scene_.camera().HandleScrollDelta(delta);
  }
}

void GameLoop::SyncWorldFrameCursorContext() {
  const auto *const session = world_session();
  const auto *const spell_targeting =
      session != nullptr ? &session->spells().GetTargeting() : nullptr;
  const auto targeting_flags = spell_targeting != nullptr ? spell_targeting->GetTargetMask() : 0u;
  const auto target_flags = static_cast<SpellTargetFlag>(targeting_flags);
  world_frame_.SetCursorContext({
      .has_active_targeting_spell =
          spell_targeting != nullptr && spell_targeting->GetSpellId() != 0u,
      .targeting_flags = targeting_flags,
      .targets_terrain_and_liquid =
          HasFlag(target_flags, SpellTargetFlag::kSourceLocation | SpellTargetFlag::kDestLocation),
      .has_spell_target_mask =
          HasFlag(target_flags, SpellTargetFlag::kUnit | SpellTargetFlag::kUnitRaid |
                                    SpellTargetFlag::kUnitParty | SpellTargetFlag::kUnitEnemy |
                                    SpellTargetFlag::kUnitAlly | SpellTargetFlag::kCorpseEnemy |
                                    SpellTargetFlag::kUnitDead | SpellTargetFlag::kCorpseAlly |
                                    SpellTargetFlag::kUnitMinipet),
      .allows_creature_type_12 = HasFlag(target_flags, SpellTargetFlag::kUnitMinipet),
      .cursor_requires_unit_target =
          session != nullptr && CursorRequiresUnitTarget(session->spells()),
      .cursor_supports_auto_target =
          session != nullptr && CursorSupportsAutoTarget(session->spells()),
      .cursor_has_area_target_flag =
          session != nullptr && CursorHasAreaTargetFlag(session->spells()),
  });
}

void GameLoop::UpdateWorldFrameMouseover(float dt) {
  auto &world_frame = world_frame_;
  const auto [cursor_x, cursor_y] = openwow::input::InputManager::Get().GetMousePosition();
  world_frame.SetCursorPosition(cursor_x, cursor_y);
  if (!world_session()) {
    world_frame.Update(dt);
    return;
  }

  if (const auto *input = GetInputControlSingleton();
      input != nullptr && (input->GetCursorVisibilityFlags() & kCursorVisibilityPresented) == 0u) {
    world_frame.SetExplicitMouseoverGuid(
        openwow::ui::game::ResolveFacingMouseoverTarget(*world_session()));
    return;
  }

  if (!game_ui_.is_initialized() || game_ui_.input_router().mouseover_frame_name().empty() ||
      game_ui_.input_router().mouseover_is_world_frame()) {
    world_frame.Update(dt);
    return;
  }

  const auto unit_token = game_ui_.input_router().ResolveModifiedMouseoverUnitToken();
  if (!unit_token.has_value()) {

    return;
  }

  const auto parsed_unit = openwow::game::ParseUnitId(*unit_token);
  if (parsed_unit.kind == openwow::game::UnitIdKind::kUnknown) {
    return;
  }

  const auto guid =
      openwow::game::UnitQueryBridge::Get().ResolveToGuid(world_session(), *unit_token);
  world_frame.SetExplicitMouseoverGuid(guid);
}

void GameLoop::SyncWorldMouseoverCursor() {
  auto &world_frame = world_frame_;
  auto *const session = world_session();
  if (session == nullptr) {
    world_frame.SetCursorMode(openwow::render::WorldFrame::CursorMode::kNone);
    cursor_manager_.RestoreBaseCursor();
    return;
  }

  const bool ui_owns_mouse =
      game_ui_.is_initialized() &&
      !game_ui_.input_router().mouseover_frame_name().empty() &&
      !game_ui_.input_router().mouseover_is_world_frame();
  if (ui_owns_mouse) {
    if (!mouseover_cursor_ui_owned_) {
      mouseover_cursor_ui_owned_ = true;
      world_frame.SetCursorMode(openwow::render::WorldFrame::CursorMode::kNone);
      cursor_manager_.RestoreBaseCursor();
    }
    return;
  }
  mouseover_cursor_ui_owned_ = false;

  auto &spell_targeting = session->spells().GetTargeting();
  const auto target_flags = static_cast<SpellTargetFlag>(spell_targeting.GetTargetMask());
  const auto mouseover_guid = session->objects().GetMouseoverGuid();
  if (mouseover_guid.IsEmpty() && spell_targeting.GetSpellId() != 0u &&
      HasFlag(target_flags, SpellTargetFlag::kSourceLocation | SpellTargetFlag::kDestLocation)) {
    targeting::WorldTerrainClick click;
    if (world_frame.TryBuildHoverTerrainClickInput(&click)) {
      spell_targeting.SetCursorPosition(world_frame.GetHoverPick().world_x,
                                        world_frame.GetHoverPick().world_y,
                                        world_frame.GetHoverPick().world_z);

      const auto result = ValidateSpellGroundClickPosition(*session,
                                                           SpellGroundClickData{
                                                               click.reference_object,
                                                               click.local_position.x,
                                                               click.local_position.y,
                                                               click.local_position.z,
                                                           },
                                                           true);

      if (result == SpellGroundClickValidation::kInRange) {
        world_frame.SetCursorMode(openwow::render::WorldFrame::CursorMode::kNone);
        cursor_manager_.SetImmediateCursorType(2);
        return;
      }

      world_frame.SetCursorMode(result == SpellGroundClickValidation::kTooFar
                                    ? openwow::render::WorldFrame::CursorMode::kCombat
                                    : openwow::render::WorldFrame::CursorMode::kInteract);
      cursor_manager_.SetImmediateCursorType(28);
      return;
    }
  }

  if (mouseover_guid.IsEmpty()) {
    world_frame.SetCursorMode(openwow::render::WorldFrame::CursorMode::kNone);
    cursor_manager_.RestoreBaseCursor();
    return;
  }

  const auto *object = session->objects().Get(mouseover_guid);
  if (object == nullptr) {
    world_frame.SetCursorMode(openwow::render::WorldFrame::CursorMode::kNone);
    cursor_manager_.RestoreBaseCursor();
    return;
  }

  world_frame.SetCursorMode(openwow::render::WorldFrame::CursorMode::kNone);
  if (object->IsGameObject()) {
    const auto *game_object = static_cast<const CGGameObject_C *>(object);
    cursor_manager_.SetImmediateCursorType(
        static_cast<std::uint32_t>(game_object->GetCursorType(*session)));
    return;
  }

  const auto *const active_player = session->objects().GetActivePlayer();
  if (object->IsUnit() && active_player != nullptr) {
    const auto *unit = static_cast<const CGUnit_C *>(object);

    const auto request = ResolveMouseoverUnitCursorRequest(*session, *active_player, *unit);
    if (request.UsesCustomTexture()) {

      (void)cursor_manager_.SetCursorFromLua(request.custom_texture_path);
      return;
    }
    if (request.retail_type != 0u) {
      cursor_manager_.SetImmediateCursorType(request.retail_type);
      return;
    }
  }

  cursor_manager_.RestoreBaseCursor();
}

namespace {

void DispatchWorldClick(openwow::game::WorldSession &session,
                        openwow::render::WorldFrame &world_frame, const float screen_x,
                        const float screen_y,
                        const openwow::game::targeting::WorldClickButton button) {
  namespace click = openwow::game::targeting;
  const int click_x = static_cast<int>(screen_x);
  const int click_y = static_cast<int>(screen_y);
  const auto pick = world_frame.Pick(click_x, click_y);

  if (pick.hit && pick.type != openwow::render::PickResult::HitType::kTerrain &&
      pick.type != openwow::render::PickResult::HitType::kNone && !pick.guid.IsEmpty()) {
    click::ui::HandleWorldObjectClick(
        session, click::WorldObjectClick{.object = pick.guid, .button = button});
    return;
  }

  if (pick.hit && pick.type == openwow::render::PickResult::HitType::kTerrain) {
    click::WorldTerrainClick terrain_click;
    if (world_frame.TryBuildTerrainClickInput(pick, &terrain_click)) {
      terrain_click.button = button;
      click::ui::HandleWorldTerrainClick(session, terrain_click);
      return;
    }
  }

  const auto ray = world_frame.ScreenToWorldRay(click_x, click_y);
  click::ui::HandleEmptyWorldClick(session,
                                   click::EmptyWorldClick{
                                       .ray_start = {ray.origin_x, ray.origin_y, ray.origin_z},
                                       .ray_end =
                                           {
                                               ray.origin_x + ray.dir_x,
                                               ray.origin_y + ray.dir_y,
                                               ray.origin_z + ray.dir_z,
                                           },
                                       .button = button,
                                   });
}

}

void GameLoop::OnLeftClickWorld(float screen_x, float screen_y) {
  if (state_ != SceneState::kInWorld)
    return;

  NoteUserActivity();
  if (world_session() == nullptr) {
    return;
  }

  DispatchWorldClick(*world_session(), world_frame_, screen_x, screen_y,
                     targeting::WorldClickButton::kPrimary);
}

void GameLoop::OnRightClickWorld(float screen_x, float screen_y) {
  if (state_ != SceneState::kInWorld)
    return;

  NoteUserActivity();
  if (world_session() == nullptr) {
    return;
  }

  DispatchWorldClick(*world_session(), world_frame_, screen_x, screen_y,
                     targeting::WorldClickButton::kSecondary);
}

void GameLoop::SetSendPacketFn(SendPacketFn fn) {
  send_packet_fn_ = std::move(fn);
  const SendPacketFn send_packet_copy = send_packet_fn_;
  openwow::net::SetClientServicesPacketSendFn(
      send_packet_copy
          ? [send_packet_copy](const net::wotlk::WorldPacket &pkt) { return send_packet_copy(pkt); }
          : openwow::net::ClientServicesPacketSendFn{});
}

void GameLoop::SetClientTimeFn(std::function<std::uint32_t()> fn) {
  client_time_fn_ = std::move(fn);
  if (world_session() != nullptr) {
    world_session()->SetClientTimeFn(client_time_fn_);
  }
}

void GameLoop::SetBlockingLoadingEventPump(std::function<void()> pump) {
  loading_screen_progress_pump_.SetPendingEventPump(std::move(pump));
}

void GameLoop::ApplyFileLoaderBindings() {
  world_scene_.world_map().SetFileLoader(file_loader_);
  world_scene_.SetObjectRendererFileLoader(file_loader_);
  world_scene_.SetPrefixFileLoader(prefix_file_loader_);
  texture_manager_.SetFileLoader(file_loader_);
  world_scene_.chat_bubble_presenter().SetFileLoader(file_loader_);
  minimap_.SetFileLoader(file_loader_);
  loading_screen_.SetFileLoader(file_loader_);
  cinematic_player_.SetModelFileLoader(file_loader_);

  if (!file_loader_) {
    cursor_manager_.SetFileReader({});
    return;
  }

  cursor_manager_.SetFileReader(
      [loader = file_loader_](const std::string &path) -> std::optional<std::vector<std::uint8_t>> {
        auto bytes = loader(path);
        if (bytes.empty()) {
          return std::nullopt;
        }
        return bytes;
      });
}

void GameLoop::SetFileLoader(world::WorldMap::LoadFileCallback callback) {
  file_loader_ = std::move(callback);
  if (initialized_) {
    ApplyFileLoaderBindings();
    if (file_loader_) {
      cursor_manager_.PreloadCursorTextures();
    }
  }
}

void GameLoop::SetPrefixFileLoader(
    std::function<std::vector<std::uint8_t>(const std::string&, std::size_t)>
        callback) {
  prefix_file_loader_ = std::move(callback);
  if (initialized_) {
    ApplyFileLoaderBindings();
  }
}

void GameLoop::SetLoadingScreenArchivePathProbe(std::function<bool(const std::string &)> probe) {
  loading_screen_archive_path_probe_ = std::move(probe);
  if (initialized_) {
    loading_screen_.SetArchivePathProbe(loading_screen_archive_path_probe_);
  }
}

void GameLoop::TickGlue(float ) {

}

void GameLoop::TickLoading(float dt) {
  RefreshLoadingWorldEntryState(dt);

  (void)RenderLoadingScreenOverlay();

  if (TryCompleteLoadingScreenWorldEntry()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                              "GameLoop: loading complete -> kInWorld");
    return;
  }

}

void GameLoop::HandleLoadingScreenProgress(openwow::screens::LoadingProgressInput input,
                                           float progress) {

  (void)PumpWorldEntryProtocolControlPackets();

  const std::uint32_t now_ms =
      client_time_fn_ ? client_time_fn_() : openwow::core::GameClock::GetTickCount32();
  const auto result = loading_screen_progress_pump_.Advance(input, progress, now_ms);
  if (!result.should_present) {
    return;
  }

  if (result.should_send_keep_alive && send_packet_fn_) {
    send_packet_fn_(openwow::net::wotlk::PacketSender::BuildKeepAlive());
  }
  PresentBlockingLoadingFrame();
}

std::size_t GameLoop::PumpWorldEntryProtocolControlPackets() {
  if (state_ != SceneState::kLoading || world_session() == nullptr || packet_queue_ == nullptr) {
    return 0u;
  }

  auto packets = packet_queue_->PopByOpcode(openwow::net::wotlk::Opcode::SMSG_WARDEN_DATA);
  for (const auto &packet : packets) {
    (void)world_session()->HandlePacket(packet);
  }
  return packets.size();
}

void GameLoop::PresentBlockingLoadingFrame() {
  if (renderer_context_ == nullptr ||
      renderer_context_->Status() != openwow::render::api::RendererStatus::Ready) {
    return;
  }

  const auto width = static_cast<std::uint16_t>(std::max(1, screen_width_));
  const auto height = static_cast<std::uint16_t>(std::max(1, screen_height_));

  const bool owns_frame = !renderer_context_->IsFrameActive();
  if (!owns_frame) {

    ++loading_screen_coalesced_callbacks_;
    return;
  }

  openwow::render::api::FrameInfo frame_info;
  frame_info.frame_number = ++blocking_loading_frame_number_;
  frame_info.absolute_seconds =
      static_cast<double>(client_time_fn_ ? client_time_fn_()
                                          : openwow::core::GameClock::GetTickCount32()) /
      1000.0;
  frame_info.backbuffer = {width, height};
  renderer_context_->BeginFrame(frame_info);
  if (!renderer_context_->IsFrameActive()) {
    return;
  }

  const auto clear_view = BuildBlockingLoadingFrameGraph(renderer_context_, width, height);
  if (!clear_view.has_value()) {
    renderer_context_->EndFrame();
    return;
  }

  float view[16]{};
  float projection[16]{};
  view[0] = 1.0f;
  view[5] = 1.0f;
  view[10] = 1.0f;
  view[15] = 1.0f;
  projection[0] = 1.0f;
  projection[5] = 1.0f;
  projection[10] = 1.0f;
  projection[15] = 1.0f;
  (void)openwow::render::ConfigureRendererContextView(
      renderer_context_, *clear_view,
      openwow::render::RendererViewClearFlags::kColor |
          openwow::render::RendererViewClearFlags::kDepth,
      openwow::ui::game::detail::kLoadingScreenClearColorRgba, 1.0f, 0, width, height, view,
      projection);

  const bool submitted = RenderLoadingScreenOverlay();
  if (submitted) {
    renderer_context_->CompleteFinalCompositor(
        openwow::render::api::FinalCompositorTarget::kLdrBackbuffer);
    ++loading_screen_self_presented_frames_;
  }
  renderer_context_->EndFrame();
}

bool GameLoop::RenderLoadingScreenOverlay() {
  auto &loading_state = openwow::screens::LoadingScreenManager::Get();
  if (!loading_state.IsVisible()) {
    return false;
  }
  const std::uint8_t ui_view = ResolveWorldUiBaseView(renderer_context_);
  const std::uint8_t cursor_view = OffsetViewId(ui_view, 7);
  if (state_ == SceneState::kInWorld && loading_state.HasTransportWorldEntryHold()) {
    loading_screen_.RenderTransportProgressOverlay(loading_state, ui_view,
                                                   static_cast<float>(screen_width_),
                                                   static_cast<float>(screen_height_));
    cursor_manager_.RenderOverlay(cursor_view, static_cast<float>(screen_width_),
                                  static_cast<float>(screen_height_));
    ++loading_screen_render_submissions_;
    if (renderer_context_ != nullptr) {
      renderer_context_->MarkFinalCompositorStage(
          openwow::render::api::FinalCompositorStage::kLoadingScreen);
    }
    return true;
  }

  loading_screen_.Render(loading_state, ui_view, static_cast<float>(screen_width_),
                         static_cast<float>(screen_height_));
  cursor_manager_.RenderOverlay(cursor_view, static_cast<float>(screen_width_),
                                static_cast<float>(screen_height_));
  ++loading_screen_render_submissions_;
  if (renderer_context_ != nullptr) {
    renderer_context_->MarkFinalCompositorStage(
        openwow::render::api::FinalCompositorStage::kLoadingScreen);
  }
  return true;
}

namespace {

constexpr std::uint32_t kTargetAcquireFlashDurationMs = 500u;

constexpr std::uint32_t kPetTargetRingDurationMs = 1000u;

constexpr std::uint32_t kPetTargetRingRgb = 0x006060FFu;

constexpr std::uint32_t kGameObjectRingArgb = 0xFF7F7F7Fu;

constexpr float kGameObjectRingMaxRadius = 10.0f;

class AttackTargetRingPulse {
 public:
  [[nodiscard]] std::uint32_t Advance(const std::uint32_t now_ms) {
    std::uint32_t elapsed = now_ms - last_toggle_ms_;
    if (static_cast<std::int32_t>(elapsed - kTargetAcquireFlashDurationMs) >= 0) {
      rising_ = !rising_;
      last_toggle_ms_ = now_ms;
      elapsed = 0u;
    }
    float fraction =
        static_cast<float>(kTargetAcquireFlashDurationMs - elapsed) /
        static_cast<float>(kTargetAcquireFlashDurationMs);
    if (rising_) {
      fraction = 1.0f - fraction;
    }
    const auto green =
        static_cast<std::uint32_t>(static_cast<int>(fraction * 128.0f)) & 0xFFu;
    return 0xFFFF0000u | (green << 8);
  }

 private:
  std::uint32_t last_toggle_ms_ = 0u;
  bool rising_ = false;
};

[[nodiscard]] std::uint32_t WithAlpha(const std::uint32_t packed_argb,
                                      const float alpha_multiplier) {
  const auto base = static_cast<float>((packed_argb >> 24) & 0xFFu);
  const auto scaled = static_cast<std::uint32_t>(
      static_cast<int>(base * alpha_multiplier));
  return (packed_argb & 0x00FFFFFFu) | ((scaled & 0xFFu) << 24);
}

}

void GameLoop::UpdateSelectionDecals() {
  world_scene_.BeginSelectionDecals();
  if (world_session() == nullptr) {
    return;
  }

  auto &objects = world_session()->objects();
  const auto active_player_guid = objects.GetActivePlayerGuid();
  const auto now_ms = openwow::core::GameClock::GetTickCount32();

  const bool selection_circle_cvar =
      openwow::ui::game::CVarSystem::Instance().GetCVarInt("ObjectSelectionCircle") != 0;
  const auto target_guid = ObjectGuid(targeting_.target_guid());
  if (selection_circle_cvar && targeting_.HasTarget() &&
      target_guid != active_player_guid) {
    if (const auto *const target_unit = objects.GetUnit(target_guid);
        target_unit != nullptr) {
      if (openwow::ui::game::GameUI_IsUIVisible()) {
        SubmitUnitSelectionDecals(*target_unit, target_guid, now_ms);
      }
    } else if (const auto *const target_object =
                   objects.GetGameObject(target_guid);
               target_object != nullptr) {

      const float model_radius = ResolveGameObjectRingRadius(*target_object);
      const float radius = std::min(model_radius * target_object->GetScale(),
                                    kGameObjectRingMaxRadius);
      const auto position = target_object->GetPosition();
      world_scene_.SubmitSelectionDecal(openwow::render::SelectionDecal{
          .center_x = position.x,
          .center_y = position.y,
          .center_z = position.z,
          .xy_half_extent = radius,
          .z_half_extent = 2.0f * radius,
          .packed_argb = kGameObjectRingArgb});
    }
  }

  const auto *const active_player = objects.GetActivePlayer();
  if (active_player == nullptr) {
    return;
  }
  const auto pet_guid = active_player->State().GetPrimaryControlledUnitGUID();
  const auto *const pet = objects.GetUnit(pet_guid);
  if (pet == nullptr) {
    return;
  }
  const auto pet_target_guid = pet->State().GetTarget();
  if (pet_target_guid.IsEmpty() || pet_target_guid == active_player_guid) {
    return;
  }
  const auto *const pet_target = objects.GetUnit(pet_target_guid);
  if (pet_target == nullptr) {
    return;
  }
  const std::uint32_t elapsed = now_ms - pet->GetTargetChangeTimeMs();
  if (static_cast<std::int32_t>(elapsed) < 0 ||
      elapsed >= kPetTargetRingDurationMs) {
    return;
  }

  const float t = static_cast<float>(elapsed) /
                  static_cast<float>(kPetTargetRingDurationMs);
  const auto shape = openwow::render::PetTargetRingShape(
      pet_target->Presentation().ModelBoundingRadius(), t);
  const float radius = shape.xy_half_extent;
  const auto alpha_byte = static_cast<std::uint32_t>(
      static_cast<int>(shape.alpha_multiplier * 255.0f)) & 0xFFu;
  world_scene_.SubmitSelectionDecal(openwow::render::SelectionDecal{
      .center_x = pet_target->GetPosition().x,
      .center_y = pet_target->GetPosition().y,
      .center_z = pet_target->GetPosition().z,
      .xy_half_extent = radius,
      .z_half_extent = 2.0f * radius,
      .packed_argb = (alpha_byte << 24) | kPetTargetRingRgb});
}

float GameLoop::ResolveGameObjectRingRadius(
    const CGGameObject_C &game_object) const {

  const auto instance_id = game_object.GetPrimaryM2InstanceId();
  auto *const m2 = game_object.m2_system();
  if (instance_id == 0u || m2 == nullptr) {
    return 0.0f;
  }
  const auto query = m2->QueryInstanceModelBoundingSphere(instance_id);
  if (query.status != openwow::render::m2::M2ResultStatus::kReady) {
    return 0.0f;
  }
  return 0.5f * query.sphere[3];
}

void GameLoop::SubmitUnitSelectionDecals(const CGUnit_C &target_unit,
                                         const ObjectGuid target_guid,
                                         const std::uint32_t now_ms) {

  const float radius = target_unit.Presentation().ModelBoundingRadius();

  const auto position = target_unit.GetPosition();

  const auto *const viewer = world_session()->objects().GetActivePlayer();
  const bool attack_highlight =
      viewer != nullptr && targeting_.IsAttackActive() &&
      viewer->Interaction().CanAttackSpellTarget(target_unit);

  static AttackTargetRingPulse s_attack_ring_pulse;
  const std::uint32_t base_color =
      attack_highlight
          ? s_attack_ring_pulse.Advance(now_ms)
          : openwow::game::targeting::ui::ResolveUnitSelectionColor(
                *world_session(), target_guid,
                openwow::game::targeting::ui::UnitSelectionColorVariant::kSelection)
                .packed_argb();

  world_scene_.SubmitSelectionDecal(openwow::render::SelectionDecal{
      .center_x = position.x,
      .center_y = position.y,
      .center_z = position.z,
      .xy_half_extent = radius,
      .z_half_extent = radius,
      .packed_argb = base_color});

  if (!attack_highlight) {
    return;
  }

  const std::uint32_t elapsed =
      now_ms - targeting_.attack_target_change_time_ms();
  if (static_cast<std::int32_t>(elapsed) < 0 ||
      elapsed >= kTargetAcquireFlashDurationMs) {
    return;
  }
  const float t = static_cast<float>(elapsed) /
                  static_cast<float>(kTargetAcquireFlashDurationMs);
  const auto shape = openwow::render::SelectionRingFlashShape(radius, t);
  world_scene_.SubmitSelectionDecal(openwow::render::SelectionDecal{
      .center_x = position.x,
      .center_y = position.y,
      .center_z = position.z,
      .xy_half_extent = shape.xy_half_extent,
      .z_half_extent = shape.xy_half_extent,
      .packed_argb = WithAlpha(base_color, shape.alpha_multiplier)});
}

void GameLoop::TickInWorld(float dt) {
  if (world_ui_lifecycle_.PumpReload() ==
      openwow::ui::game::WorldUiReloadPumpResult::RuntimeStartFailed) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
                              "GameLoop: required in-world UI failed during reload");
    LeaveWorld();
    return;
  }

  UpdateNetwork();
  if (world_session() != nullptr) {
    (void)Player_C_TickAreaCheck(*world_session(), world_scene_.world_map());
  }
  SynchronizeZoneUiState();
  (void)AccountMsg::Get().Pump();
  (void)KnowledgeBase::Get().Pump();

  const auto steady_now = std::chrono::steady_clock::now().time_since_epoch();
  const std::uint32_t current_tick_ms =
      client_time_fn_
          ? client_time_fn_()
          : static_cast<std::uint32_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(steady_now).count());

  cinematic_player_.Update(*world_session(), dt);

  EventScheduler::Get().Update(dt);

  if (world_session()) {
    world_session()->objects().AdvanceTransportPathStates();
  }

  SyncWorldFrameCursorContext();
  ProcessInput(dt);
  SyncWorldSceneTimeFromState(&world_scene_, world_session());
  HandlePerFrameWorldMaintenance(current_tick_ms);

  if (world_session()) {
    world_session()->objects().AdvanceVisualState(current_tick_ms, dt);
  }
  PublishMoverFramePose(dt, current_tick_ms);
  ResolveFrameCameraPose(dt);

  UpdateCameraUnderwaterSoundArea();
  UpdateSoundListenerForFrame();

  openwow::ui::game::runtime::LuaWatchdogNoteFrame();
  if (world_session()) {
    UnitFrameDataProvider::Get().Update(world_session()->objects());
    world_session()->missile_trajectory().UpdateTrajectoryPreview(world_session()->objects());

    world_scene_.PublishObjectPresentation(world_session()->objects(), *world_session());
  }

  if (world_session()) {
    world_scene_.ConsumeSpellVisualEvents();
  }

  const auto &frame_camera = world_scene_.camera().frame_pose();
  const auto &world_cvars = openwow::ui::game::CVarSystem::Instance();

  const float m2_particle_density = ReadM2ParticleDensity();
  const float weather_particle_density = ReadWeatherParticleDensity();
  m2_system_.SetParticleDensity(m2_particle_density);
  world_scene_.Update(dt, frame_camera.position[0], frame_camera.position[1],
                      frame_camera.position[2], world_cvars.GetCVarFloat("environmentDetail"),
                      weather_particle_density, ReadUseWeatherShaders());

  if (world_session()) {
    world_frame_.BindObjectPresentation(&world_scene_.object_presentation());
  }

  world_frame_.UpdateNameplateHover(targeting_.target_guid());

  UpdateWorldFrameMouseover(dt);

  if (world_session()) {
    const auto mouseover_guid = world_frame_.GetMouseoverGuid();
    const auto new_mouseover = mouseover_guid.GetRawValue();
    world_session()->objects().SetMouseover(mouseover_guid);
    SyncWorldMouseoverCursor();
    openwow::ui::game::ValidateNpcInteractionTargets(*world_session());

    const bool cursor_over_world_frame =
        !game_ui_.is_initialized() ||
        game_ui_.input_router().mouseover_frame_name().empty() ||
        game_ui_.input_router().mouseover_is_world_frame();
    if (cursor_over_world_frame) {
      auto &tooltip = openwow::ui::game::TooltipSystem::Get();
      if (const auto *game_object = world_session()->objects().GetGameObject(mouseover_guid);
          game_object != nullptr) {
        tooltip.SetWorldGameObject(*game_object);
      } else if (tooltip.GetWorldGameObjectGuid().has_value()) {
        tooltip.Hide();
      }
    }

    if (new_mouseover != prev_mouseover_guid_) {
      prev_mouseover_guid_ = new_mouseover;
      if (game_ui_.is_initialized()) {
        game_ui_.frame_events().dispatcher().FireEvent(
            openwow::ui::game::events::UPDATE_MOUSEOVER_UNIT);
      }
    }
  }

  if (world_session()) {
    world_scene_.nameplate_renderer().SetShowClassColorInNameplate(
        openwow::ui::game::CVarSystem::Instance().GetCVarInt("ShowClassColorInNameplate") != 0);
    world_scene_.nameplate_renderer().SetAllowOverlap(
        openwow::ui::game::CVarSystem::Instance().GetCVarInt("nameplateAllowOverlap") != 0);
    world_scene_.UpdateNameplates(world_session()->objects(), *world_session(),
                                  targeting_.target_guid(),
                                  world_session()->objects().GetMouseoverGuid().GetRawValue(),
                                  world_frame_.GetNameplateHoverGuid().GetRawValue(),
                                  openwow::ui::game::GameUI_ShouldShowWorldNameplates(),
                                  player_x_, player_y_, player_z_);
  }

  UpdateSelectionDecals();

  if (world_session()) {
    world_scene_.SynchronizeObjectModelBindings(world_session()->objects(), *world_session());
    RefreshExactUnitBoundsFromRenderer(world_session()->objects());

    if (const auto *area_record =
            openwow::game::ResolveCharacterAmbientAreaRecord(*world_session());
        area_record != nullptr) {
      openwow::world::CWorld_UpdateCharacterAmbientMultiply(
          area_record->ambient_multiplier, dt);
    }
  }

  post_process_.Update(dt);
  post_process_.SetSettings(ReadPostProcessSettings());

  render::ScreenEffectState screen_effect_state{};
  float normalized_inebriation = 0.0f;
  bool suppress_local_lighting = false;
  const auto *active_player =
      world_session() ? world_session()->objects().GetActivePlayer() : nullptr;
  if (world_session()) {
    if (active_player != nullptr) {
      normalized_inebriation = ComputeNormalizedInebriation(active_player->GetDrunkState(),
                                                            active_player->GetFakeInebriation());
      suppress_local_lighting = (active_player->GetPlayerFlags() & 0x10u) != 0;
      if (dbc_) {
        screen_effect_state = ResolveActivePlayerScreenEffect(
            *active_player, *dbc_,
            ResolveActiveBattlefieldInstanceType(BattlefieldInfo::Get(), *dbc_));
      }
    }
  }
  world_scene_.SetSuppressLocalLighting(suppress_local_lighting);
  world_scene_.SetScreenEffectLightParamSlotOverride(screen_effect_state.light_param_slot_override);
  const auto screen_effect_fog_override =
      ResolveScreenEffectFogOverride(screen_effect_state, post_process_.HasSceneFramebuffer());
  world_scene_.SetScreenEffectFogOverride(screen_effect_fog_override);

  ApplyScreenEffectToPostProcess(screen_effect_state, post_process_);
  sound_runtime_.ApplyScreenEffectAudioSelections(
      static_cast<std::int32_t>(screen_effect_state.sound_ambience_id),
      static_cast<std::int32_t>(screen_effect_state.zone_music_id));

  const auto &underwater_probe = world_scene_.camera().frame_pose().position;
  const bool underwater =
      active_player != nullptr &&
      world_scene_.world_map().GetUnderwaterLiquidTypeId(
          underwater_probe[0], underwater_probe[1], underwater_probe[2]) != 0u;
  post_process_.SetWorldViewEffectState(
      ResolveWorldViewEffectState(DayNight_GetSceneVisibility(), active_player, underwater));
  post_process_.SetDrunkEffect(
      UsesDefaultWorldViewPipeline(screen_effect_state) ? normalized_inebriation : 0.0f);

  world_overlay_metrics_ = render::WorldOverlayMetrics::FromFramebuffer(
      static_cast<float>(screen_width_), static_cast<float>(screen_height_),
      openwow::ui::game::ComputeGameUiRenderPixelScale(static_cast<float>(screen_height_),
                                                       game_ui_.root_scale()));

  RenderWorld(dt);

  event_bridge_.Poll(dt);

  game_ui_.Update(dt);

  if (AccountData::Get().IsUploadDue()) {
    (void)PumpRuntimeAccountDataUpload(BuildAccountDataUploadContext(
        world_session(), &binding_profiles_, &game_ui_, send_packet_fn_));
  }
  const bool stock_frame_xml_loaded = game_ui_.is_loaded();

  floating_text_.Update(dt);

  UpdateCorpseProximityState();

  if (world_session() &&
      ShouldRunNativeWorldUiSurface(stock_frame_xml_loaded, NativeWorldUiSurface::Minimap)) {
    minimap_.Update(player_x_, player_y_, player_z_, player_orientation_, *world_session(),
                    &world_session()->objects(), world_session()->objects().GetLocalPlayerGuid());
  }

  if (world_session()) {
    const auto &loot = world_session()->loot();
    if (loot.is_looting() && !loot_frame_.IsVisible()) {

      const auto &lw = loot.loot_window();
      std::vector<openwow::ui::game::LootFrameItem> frame_items;
      frame_items.reserve(lw.items.size());
      for (const auto &li : lw.items) {
        openwow::ui::game::LootFrameItem fi;
        fi.index = li.slot_index;
        fi.item_id = li.item_id;
        fi.count = li.count;
        fi.display_id = li.display_info_id;
        fi.quality = 1;
        frame_items.push_back(fi);
      }
      loot_frame_.ShowLoot(lw.source_guid.GetRawValue(), frame_items, lw.gold);
    } else if (!loot.is_looting() && loot_frame_.IsVisible()) {
      loot_frame_.Hide();
    }
  }
  if (ShouldRunNativeWorldUiSurface(stock_frame_xml_loaded, NativeWorldUiSurface::Loot)) {
    loot_frame_.Update(dt);
  }

  if (ShouldRunNativeWorldUiSurface(stock_frame_xml_loaded, NativeWorldUiSurface::Chat)) {
    chat_frame_.Update(dt);
  }

  const std::uint8_t ui_view = ResolveWorldUiBaseView(renderer_context_);
  const std::uint8_t ui_offscreen_view = ResolveWorldUiOffscreenView(renderer_context_);

  if (ShouldRunNativeWorldUiSurface(stock_frame_xml_loaded, NativeWorldUiSurface::Minimap)) {
    if (stock_frame_xml_loaded) {

      auto &layout = game_ui_.retained_layout();
      layout.SolveIfDirty();
      if (cached_minimap_rect_ == nullptr ||
          cached_minimap_rect_generation_ != layout.RectsGeneration()) {
        cached_minimap_rect_ = layout.FindRect("Minimap");
        cached_minimap_rect_generation_ = layout.RectsGeneration();
      }
      if (const auto *const minimap_rect = cached_minimap_rect_;
          minimap_rect != nullptr) {
        minimap_.SetFrameRect(
            static_cast<float>(minimap_rect->x), static_cast<float>(minimap_rect->y),
            static_cast<float>(minimap_rect->width), static_cast<float>(minimap_rect->height));

        minimap_.SetUiUnitScale(openwow::ui::game::ComputeGameUiRenderPixelScale(
            layout.viewport_height(), layout.root_scale()));
      }
    }
    if (stock_frame_xml_loaded) {
      minimap_.RenderToTexture(ui_offscreen_view);
      game_ui_.SetMinimapSurfaceSubmitter(minimap_.surface_submitter());
    } else {
      minimap_.Render(ui_view, static_cast<float>(screen_width_),
                      static_cast<float>(screen_height_));
    }
  }

  if (stock_frame_xml_loaded) {
    const std::uint64_t compositor_generation =
        renderer_context_ != nullptr ? renderer_context_->FinalCompositor().active_generation : 0u;
    game_ui_.Render(ui_view, static_cast<float>(screen_width_), static_cast<float>(screen_height_),
                    compositor_generation, OffsetViewId(ui_offscreen_view, 1u),
                    kWorldUiOffscreenViewCount - 1u);
  }

  {
    const auto &pose = world_scene_.camera().frame_pose();

    floating_text_.Render(OffsetViewId(ui_view, 1), world_overlay_metrics_, pose.view.data(),
                          pose.projection.data());
  }

  if (ShouldRunNativeWorldUiSurface(stock_frame_xml_loaded, NativeWorldUiSurface::Chat)) {
    chat_frame_.Render(OffsetViewId(ui_view, 2), static_cast<float>(screen_width_),
                       static_cast<float>(screen_height_));
  }

  if (ShouldRunNativeWorldUiSurface(stock_frame_xml_loaded, NativeWorldUiSurface::Loot)) {
    loot_frame_.Render(OffsetViewId(ui_view, 6), static_cast<uint16_t>(screen_width_),
                       static_cast<uint16_t>(screen_height_));
  }

  if (openwow::screens::LoadingScreenManager::Get().IsVisible()) {

    (void)RenderLoadingScreenOverlay();
  } else {
    cursor_manager_.RenderOverlay(OffsetViewId(ui_view, 7), static_cast<float>(screen_width_),
                                  static_cast<float>(screen_height_));
  }

  bgfx::setViewClear(ui_view,
                     BGFX_CLEAR_DEPTH | BGFX_CLEAR_STENCIL | BGFX_CLEAR_DISCARD_DEPTH |
                         BGFX_CLEAR_DISCARD_STENCIL,
                     0u, 1.0f, 0u);
  if (renderer_context_ != nullptr) {
    renderer_context_->MarkFinalCompositorStage(
        openwow::render::api::FinalCompositorStage::kWorldUi);
  }
}

void GameLoop::ShowLoadingScreen(std::uint32_t map_id) {
  auto &loading_state = openwow::screens::LoadingScreenManager::Get();
  loading_state.Show();
  loading_state.SetTransportWorldEntryHold(false);
  loading_screen_.PrepareMap(map_id);
}

void GameLoop::HideLoadingScreen() {
  openwow::core::LoadingScreen_CleanupResources(sound_runtime_);
  loading_screen_.ReleaseMap();

  sound_runtime_.SetZoneMusicPlaybackInhibited(false);
}

void GameLoop::RefreshLoadingWorldEntryState(float dt) {
  UpdateNetwork();

  EventScheduler::Get().Update(dt);

  if (world_session()) {
    const std::uint32_t client_time_ms =
        client_time_fn_ ? client_time_fn_() : openwow::core::GameClock::GetTickCount32();
    world_session()->Update(dt, client_time_ms);
    SyncWorldSceneTimeFromState(&world_scene_, world_session());
  }

  UpdateLoadingTrackedPlayerState();
  ArmTransportWorldEntryHoldForRidingPlayer();

  if (world_session() != nullptr) {
    (void)Player_C_TickAreaCheck(*world_session(), world_scene_.world_map());
    SynchronizeZoneUiState();
  }

  sound_runtime_.SetZoneMusicPlaybackInhibited(
      !openwow::screens::LoadingScreenManager::Get().HasTransportWorldEntryHold());

  if (world_session()) {
    world_session()->objects().AdvanceVisualState(openwow::core::GameClock::GetTickCount32(), dt);
    world_scene_.PublishObjectPresentation(world_session()->objects(), *world_session());
    world_scene_.ConsumeSpellVisualEvents();
  }

  auto &camera = world_scene_.camera();
  camera.SetViewportSize(static_cast<float>(screen_width_), static_cast<float>(screen_height_));
  camera.Update(dt, client_time_fn_ ? client_time_fn_()
                                    : openwow::core::GameClock::GetTickCount32());

  ApplyCameraTargetAlpha();
  const auto &loading_camera = camera.ResolveFramePose({
      .aspect_ratio =
          static_cast<float>(screen_width_) / static_cast<float>(std::max(1, screen_height_)),
      .near_plane = kWorldCameraNearClip,
      .far_plane = ResolveWorldCameraFarClip(),
  });
  const auto &world_cvars = openwow::ui::game::CVarSystem::Instance();
  m2_system_.SetParticleDensity(ReadM2ParticleDensity());
  world_scene_.Update(dt, loading_camera.position[0], loading_camera.position[1],
                      loading_camera.position[2], world_cvars.GetCVarFloat("environmentDetail"),
                      ReadWeatherParticleDensity(), ReadUseWeatherShaders());
  if (world_session()) {
    world_scene_.SynchronizeObjectModelBindings(world_session()->objects(), *world_session());
    RefreshExactUnitBoundsFromRenderer(world_session()->objects());

    if (const auto *area_record =
            openwow::game::ResolveCharacterAmbientAreaRecord(*world_session());
        area_record != nullptr) {
      openwow::world::CWorld_UpdateCharacterAmbientMultiply(
          area_record->ambient_multiplier, dt);
    }
  }
}

void GameLoop::UpdateLoadingTrackedPlayerState() {
  if (!world_session()) {
    return;
  }

  const auto *active_player = world_session()->objects().GetActivePlayer();
  if (!active_player) {
    return;
  }

  const auto player_position = active_player->GetPosition();
  player_x_ = player_position.x;
  player_y_ = player_position.y;
  player_z_ = player_position.z;
  player_orientation_ = active_player->GetWorldFacing();

  world_scene_.UpdatePlayerPosition(player_x_, player_y_, player_z_);
  world_scene_.camera().SetTarget(player_x_, player_y_, player_z_);

  world_scene_.camera().SetReferenceFacing(player_orientation_);
}

LoadingScreenWorldEntryGateState GameLoop::BuildLoadingScreenWorldEntryGateState() const {
  LoadingScreenWorldEntryGateState state;
  state.requires_transport_assets =
      openwow::screens::LoadingScreenManager::Get().HasTransportWorldEntryHold();
  if (!world_session()) {
    return state;
  }

  const auto *active_player = world_session()->objects().GetActivePlayer();
  if (!active_player) {
    return state;
  }

  state.has_active_player = true;
  state.active_player_render_assets_ready =
      world_scene_.object_renderer().IsLoadingScreenPlayerRenderAssetReady(
          active_player->GetGuid());

  state.critical_visible_world_surface_ready =
      world_scene_.world_map().IsWorldEntryStreamingComplete() &&
      world_scene_.IsDoodadWorldEntryLoadDrained();

  const auto transport_guid = active_player->GetTransportGUID();
  if (transport_guid.IsEmpty()) {
    return state;
  }

  state.has_transport_guid = true;

  const auto *transport = world_session()->objects().GetGameObject(transport_guid);
  if (!transport) {
    return state;
  }

  state.has_transport_object = true;
  state.transport_assets_ready =
      world_scene_.object_renderer().IsLoadingScreenTransportRenderAssetReady(transport->GetGuid());
  return state;
}

void GameLoop::ArmTransportWorldEntryHoldForRidingPlayer() {
  auto &loading_state = openwow::screens::LoadingScreenManager::Get();
  if (loading_state.HasTransportWorldEntryHold()) {
    return;
  }

  if (BuildLoadingScreenWorldEntryGateState().has_transport_guid) {
    loading_state.SetTransportWorldEntryHold(true);
  }
}

bool GameLoop::ShouldKeepLoadingScreenVisibleForWorldEntry() const {
  return openwow::game::ShouldKeepLoadingScreenVisibleForWorldEntry(
      BuildLoadingScreenWorldEntryGateState());
}

bool GameLoop::TryCompleteLoadingScreenWorldEntry() {
  if (!world_ui_lifecycle_.TryActivateLocalPlayer()) {
    return false;
  }

  if (ShouldKeepLoadingScreenVisibleForWorldEntry()) {
    return false;
  }

  world_scene_.world_map().SetWorldEntryStreamingMode(false);
  HideLoadingScreen();
  state_ = SceneState::kInWorld;
  return true;
}

void GameLoop::UpdateNetwork() {
  if (!world_session())
    return;

  if (packet_queue_) {

    constexpr int kMaxPacketsPerFrame = 200;
    constexpr auto kTimeBudget = std::chrono::milliseconds(50);
    const auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < kMaxPacketsPerFrame; ++i) {
      auto pkt = packet_queue_->Pop();
      if (!pkt.has_value())
        break;

      if (!world_session()->HandlePacket(*pkt)) {

        const auto opcode_value = net::wotlk::OpcodeValue(pkt->opcode);
        if (logged_unhandled_opcodes_.insert(opcode_value).second) {
          std::ostringstream oss;
          oss << "GameLoop: unhandled opcode 0x" << std::hex << std::uppercase
              << opcode_value;
          openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, oss.str());
        }
      }
      world_session()->FlushDeferredWorldTransfer();

      if ((i & 0xF) == 0xF) {
        const auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed >= kTimeBudget) {
          openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kTrace,
                                    "GameLoop::UpdateNetwork time budget exceeded, "
                                    "processed " +
                                        std::to_string(i + 1) + " packets");
          break;
        }
      }
    }
  }
}

void GameLoop::NoteUserActivity() {
  const std::uint32_t now_ms =
      client_time_fn_ ? client_time_fn_() : openwow::core::GameClock::GetTickCount32();
  idle_billing_.NoteUserActivity(now_ms);
  if (world_session()) {
    world_session()->chat_sender().ClearLocalAfkIfNeeded(false);
  }
}

void GameLoop::ReleaseClickToMove(WorldSession *session) {
  if (session != nullptr) {
    if (ctm_owns_auto_forward_) {
      if (auto *const mover = ResolveEffectiveMovingUnit(*session); mover != nullptr) {
        const std::uint32_t timestamp =
            client_time_fn_ ? client_time_fn_() : openwow::core::GameClock::GetTickCount32();

        mover->Movement().InputControlStopForward(timestamp);
      }
    }
    session->click_to_move().Stop();
  }

  {
    const std::uint32_t timestamp =
        client_time_fn_ ? client_time_fn_() : openwow::core::GameClock::GetTickCount32();
    InputControl_ApplyControlFlagChange(kCtrlClickToMoveForward, false,
                                        timestamp);
  }
  ctm_owns_auto_forward_ = false;
  ctm_facing_turn_rate_rad_per_sec_ = 0.0f;
}

void GameLoop::ProcessInput(float dt) {

  const std::uint32_t client_time_ms =
      client_time_fn_ ? client_time_fn_() : openwow::core::GameClock::GetTickCount32();
  const std::uint32_t cur_actions = input_.actions();

  if (cur_actions != prev_actions_) {
    NoteUserActivity();

    const auto tab_bit = static_cast<std::uint32_t>(kInputTabTarget);
    if (!(prev_actions_ & tab_bit) && (cur_actions & tab_bit)) {
      targeting_.TabTarget(false, TargetFilter::kEnemy);
    }

    const auto clear_bit = static_cast<std::uint32_t>(kInputClearTarget);
    if (!(prev_actions_ & clear_bit) && (cur_actions & clear_bit)) {
      targeting_.ClearTarget();
    }

    prev_actions_ = cur_actions;
  }

  if (world_session() != nullptr) {
    auto *const controlled_mover = ResolveEffectiveMovingUnit(*world_session());

    constexpr float kTwoPi = 6.28318530717958647692f;
    constexpr float kCtmFacingDeadbandRad = 0.001f;
    constexpr float kCtmTurnRateSlowDegPerSec = 800.0f;
    constexpr float kCtmTurnRateFastDegPerSec = 1200.0f;
    constexpr float kCtmFastTurnLowerRad = 2.0943952f;
    constexpr float kCtmFastTurnUpperRad = 4.1887903f;
    constexpr float kDegreesToRadians = 0.017453292f;
    const auto face_controlled_mover = [this, controlled_mover, client_time_ms,
                                        dt](const float x, const float y,
                                            const bool instant) {
      if (controlled_mover == nullptr) {
        return;
      }
      const float dx = x - controlled_mover->GetX();
      const float dy = y - controlled_mover->GetY();
      if (std::fabs(dx) < 1.0e-6f && std::fabs(dy) < 1.0e-6f) {
        return;
      }
      const float facing = std::atan2(dy, dx);
      const float delta =
          std::remainder(facing - controlled_mover->GetOrientation(), kTwoPi);
      if (std::fabs(delta) < kCtmFacingDeadbandRad) {

        if (!instant) {
          ctm_facing_aligned_once_ = true;
        }
        return;
      }
      float applied_facing = facing;
      if (!instant) {
        if (ctm_facing_turn_rate_rad_per_sec_ == 0.0f) {

          const float wrapped =
              delta < 0.0f ? delta + kTwoPi : delta;
          const bool fast = wrapped > kCtmFastTurnLowerRad &&
                            wrapped < kCtmFastTurnUpperRad;
          ctm_facing_turn_rate_rad_per_sec_ =
              (fast ? kCtmTurnRateFastDegPerSec : kCtmTurnRateSlowDegPerSec) *
              kDegreesToRadians;
        }
        const float max_step = dt * ctm_facing_turn_rate_rad_per_sec_;
        if (std::fabs(delta) > max_step) {
          applied_facing = controlled_mover->GetOrientation() +
                           (delta > 0.0f ? max_step : -max_step);
          applied_facing = std::remainder(applied_facing, kTwoPi);
        }
      }
      controlled_mover->Movement().SendSetFacing(*world_session(), client_time_ms,
                                                 applied_facing);
    };
    auto &click_to_move = world_session()->click_to_move();
    if (click_to_move.GetActionGeneration() != ctm_driven_action_generation_) {

      ctm_driven_action_generation_ = click_to_move.GetActionGeneration();
      ctm_facing_turn_rate_rad_per_sec_ = 0.0f;

      ctm_facing_aligned_once_ = false;
      ctm_no_progress_ticks_ = 0;
      ctm_has_last_tick_position_ = false;

      if (click_to_move.IsActive()) {
        if (const auto *const player =
                world_session()->objects().GetLocalPlayerTyped();
            player != nullptr && player->GetPlayerStandState() != 0u) {
          world_session()->interaction().SendStandStateChange(0u);
        }
      }
    }
    if (click_to_move.GetAction() == CTMAction::Interact) {
      const auto target = click_to_move.GetTarget();
      const auto *object = world_session()->objects().Get(target);
      if (object == nullptr || object->IsPendingRemoval()) {
        targeting_.InvalidateTrackedGuidReferences(target.GetRawValue());
      } else {
        const auto position = object->GetPosition();
        click_to_move.UpdateDestination(position.x, position.y, position.z);
      }
    }
    if (controlled_mover != nullptr) {

      constexpr std::uint32_t kMoveFlagSwimmingOrFlying =
          kMoveFlagSwimming | kMoveFlagFlying;
      click_to_move.SetVerticalDistanceIncluded(
          (controlled_mover->GetMovementInfo().flags &
           kMoveFlagSwimmingOrFlying) != 0u);

      if (click_to_move.GetAction() == CTMAction::Move) {
        const float mover_speed =
            controlled_mover->Movement().ComputeCurrentSpeed();
        const float widen = std::max(
            1.0f, mover_speed / kCtmArrivalRadiusSpeedDivisor);
        click_to_move.SetArrivalThreshold(kCtmMoveArrivalRadius * widen);
      }

      if (click_to_move.GetAction() == CTMAction::Interact ||
          click_to_move.GetAction() == CTMAction::Loot) {
        constexpr float kCtmInteractAbandonRangeSq = 6400.0f;
        const auto destination = click_to_move.GetDestination();
        const float abandon_dx = destination.x - controlled_mover->GetX();
        const float abandon_dy = destination.y - controlled_mover->GetY();
        const float abandon_dz = destination.z - controlled_mover->GetZ();
        if (abandon_dx * abandon_dx + abandon_dy * abandon_dy +
                abandon_dz * abandon_dz >= kCtmInteractAbandonRangeSq) {
          ReleaseClickToMove(world_session());
        }
      }

      if (click_to_move.IsActive() && ctm_owns_auto_forward_ &&
          ctm_facing_aligned_once_ && dt > 0.0f) {
        const auto destination = click_to_move.GetDestination();
        const float to_destination_x = destination.x - controlled_mover->GetX();
        const float to_destination_y = destination.y - controlled_mover->GetY();
        if (std::fabs(to_destination_x) >= 1.0e-6f ||
            std::fabs(to_destination_y) >= 1.0e-6f) {
          constexpr float kCtmAbandonLowerRad = 2.7925267f;
          constexpr float kCtmAbandonUpperRad = 3.8397243f;
          const float desired =
              std::atan2(to_destination_y, to_destination_x);
          float behind = std::fmod(
              desired - controlled_mover->GetOrientation(), kTwoPi);
          if (behind < 0.0f) {
            behind += kTwoPi;
          }
          if (behind > kCtmAbandonLowerRad && behind < kCtmAbandonUpperRad) {
            ReleaseClickToMove(world_session());
          }
        }
      }

      if (click_to_move.IsActive() && ctm_owns_auto_forward_ &&
          ctm_facing_aligned_once_) {

        bool counted_action = false;
        switch (click_to_move.GetAction()) {
        case CTMAction::Move:
        case CTMAction::Loot:
          counted_action = true;
          break;
        default:
          break;
        }
        bool grew = false;
        if (counted_action && ctm_has_last_tick_position_) {
          const auto destination = click_to_move.GetDestination();
          const float cur_dx = destination.x - controlled_mover->GetX();
          const float cur_dy = destination.y - controlled_mover->GetY();
          const float cur_dz = destination.z - controlled_mover->GetZ();
          const float last_dx = destination.x - ctm_last_tick_x_;
          const float last_dy = destination.y - ctm_last_tick_y_;
          const float last_dz = destination.z - ctm_last_tick_z_;
          grew = cur_dx * cur_dx + cur_dy * cur_dy + cur_dz * cur_dz >
                 last_dx * last_dx + last_dy * last_dy + last_dz * last_dz;
        }
        if (grew) {
          ++ctm_no_progress_ticks_;
          if (ctm_no_progress_ticks_ > 4u) {
            ReleaseClickToMove(world_session());
          }
        } else {
          ctm_no_progress_ticks_ = 0u;
        }
      }
      if (click_to_move.IsActive()) {
        ctm_last_tick_x_ = controlled_mover->GetX();
        ctm_last_tick_y_ = controlled_mover->GetY();
        ctm_last_tick_z_ = controlled_mover->GetZ();
        ctm_has_last_tick_position_ = true;
      } else {
        ctm_has_last_tick_position_ = false;

        ctm_owns_auto_forward_ = false;
      }
    }
    if (controlled_mover != nullptr) {

      const auto mover_position = controlled_mover->GetPosition();
      click_to_move.Update(dt, mover_position.x, mover_position.y,
                           mover_position.z);
    } else {
      ReleaseClickToMove(world_session());
    }

    const auto completed = click_to_move.ConsumeCompletedAction();
    if (completed.has_value()) {
      if ((completed->action == CTMAction::Interact ||
           completed->action == CTMAction::Loot) &&
          controlled_mover != nullptr) {

        controlled_mover->Movement().InputControlStopForward(client_time_ms);
        click_to_move.Stop();
        ctm_owns_auto_forward_ = false;
      } else {
        ReleaseClickToMove(world_session());
      }

      if (!completed->target.IsEmpty()) {
        if (const auto *const target_object =
                world_session()->objects().Get(completed->target);
            target_object != nullptr) {

          const auto face_target_position = target_object->GetPosition();
          face_controlled_mover(face_target_position.x, face_target_position.y,
                                true);
        }
      }
    }

    if (completed.has_value() &&
        (completed->action == CTMAction::Interact ||
         completed->action == CTMAction::Loot) &&
        !completed->target.IsEmpty()) {
      const auto *object = world_session()->objects().Get(completed->target);
      const auto *interaction_player = world_session()->objects().GetLocalPlayerTyped();
      const bool eligible = object != nullptr && interaction_player != nullptr;

      const bool arrived_unit_is_active_player =
          controlled_mover != nullptr && interaction_player != nullptr &&
          controlled_mover->GetGuid() == interaction_player->GetGuid();

      if (eligible && object->IsUnit() && arrived_unit_is_active_player &&
          static_cast<const CGUnit_C &>(*object).State().IsLootableCorpseNow()) {
        static_cast<const CGUnit_C &>(*object).Interaction().RightClickInteract(
            world_session(), &targeting_);
      } else if (eligible && object->IsUnit() && arrived_unit_is_active_player &&
          openwow::ui::game::ValidateNpcInteractionTarget(*interaction_player, *object,
                                                          &world_session()->query_cache())
              .should_keep) {
        static_cast<const CGUnit_C &>(*object).Interaction().CompleteRightClickInteraction(
            *world_session());
      } else if (eligible && object->IsGameObject()) {

        auto &game_object = static_cast<CGGameObject_C &>(
            *world_session()->objects().GetMutable(completed->target));
        game_object.OnRightClickInteract(world_session(), &targeting_);
      }
    }

    switch (click_to_move.GetAction()) {
    case CTMAction::Move:
    case CTMAction::Attack:
    case CTMAction::Loot:
    case CTMAction::Interact: {
      const auto destination = click_to_move.GetDestination();
      face_controlled_mover(destination.x, destination.y, false);
      if (!ctm_owns_auto_forward_ && controlled_mover != nullptr) {

        InputControl_ApplyControlFlagChange(kCtrlClickToMoveForward, true,
                                            client_time_ms);
        controlled_mover->Movement().SendForward(*world_session(), client_time_ms, true);
        ctm_owns_auto_forward_ = true;
      }
      break;
    }
    case CTMAction::FaceTo: {
      const auto destination = click_to_move.GetDestination();
      face_controlled_mover(destination.x, destination.y, true);
      ReleaseClickToMove(world_session());
      break;
    }
    default:
      ReleaseClickToMove(world_session());
      break;
    }
  }

  if (world_session() != nullptr) {
    world_session()->Update(dt, client_time_ms);
  }
  targeting_.Update(dt);
}

void GameLoop::PublishMoverFramePose(const float dt,
                                     const std::uint32_t client_time_ms) {
  std::uint32_t mover_movement_flags = 0u;
  bool mover_is_player_on_taxi_flight = false;
  if (world_session() != nullptr) {
    if (const auto *const mover = ResolveEffectiveMovingUnit(*world_session()); mover != nullptr) {

      const auto mover_position = mover->GetPosition();
      player_x_ = mover_position.x;
      player_y_ = mover_position.y;
      player_z_ = mover_position.z;
      player_orientation_ = mover->GetWorldFacing();
      mover_movement_flags = mover->GetMovementInfo().flags;

      {
        static constexpr std::uint32_t kAboardTelemetryIntervalMs = 500u;
        static std::uint32_t s_last_aboard_telemetry_ms = 0u;
        const auto &mi = mover->GetMovementInfo();
        if (!mi.transport.guid.IsEmpty() &&
            client_time_ms - s_last_aboard_telemetry_ms >=
                kAboardTelemetryIntervalMs) {
          s_last_aboard_telemetry_ms = client_time_ms;
          const float carrier_yaw = game::Movement_GetObjectOrientation(
              world_session()->objects(), mi.transport.guid.GetRawValue());

          const float smooth_body_yaw = mover->Movement().WorldSmoothBodyFacing();
          const float cam_yaw = world_scene_.camera().yaw();
          openwow::diagnostics::Log(
              openwow::diagnostics::LogLevel::kInfo,
              "aboard: guid=" + std::to_string(mi.transport.guid.GetRawValue()) +
                  " carrier_yaw=" + std::to_string(carrier_yaw) +
                  " local=(" + std::to_string(mi.transport.offset_x) + "," +
                  std::to_string(mi.transport.offset_y) + "," +
                  std::to_string(mi.transport.offset_z) + ") local_o=" +
                  std::to_string(mi.transport.offset_o) + " world=(" +
                  std::to_string(mover_position.x) + "," +
                  std::to_string(mover_position.y) + "," +
                  std::to_string(mover_position.z) + ") world_o=" +
                  std::to_string(player_orientation_) +
                  " smooth_body=" + std::to_string(smooth_body_yaw) +
                  " cam=" + std::to_string(cam_yaw) + " flags=0x" +
                  std::to_string(mi.flags));
        }
      }

      mover_is_player_on_taxi_flight =
          mover->IsPlayer() && mover->State().IsTaxiFlight();
    }
  } else {
    const auto &offline_movement = movement_controller();
    player_x_ = offline_movement.x();
    player_y_ = offline_movement.y();
    player_z_ = offline_movement.z();
    player_orientation_ = offline_movement.orientation();
  }

  world_scene_.camera().SetReferenceFacing(player_orientation_);

  world_scene_.camera().SetBoundUnitMovementFlags(mover_movement_flags);

  world_scene_.camera().SetTaxiFlightInputSuppressed(
      world_session() != nullptr && mover_is_player_on_taxi_flight &&
      world_session()->taxi().IsInFlight());
  world_scene_.camera().SetTarget(player_x_, player_y_, player_z_);
  world_scene_.camera().SetViewportSize(static_cast<float>(screen_width_),
                                        static_cast<float>(screen_height_));
  openwow::ui::game::detail::SyncCameraMotionSettings(world_scene_.camera());
  (void)world_scene_.camera().ApplyScriptedMoveInputs(client_time_ms);
  world_scene_.camera().Update(dt, client_time_ms);
  ApplyCameraTargetAlpha();

  world_scene_.UpdatePlayerPosition(player_x_, player_y_, player_z_);
}

void GameLoop::ApplyCameraTargetAlpha() {

  auto *const session = world_session();
  const std::uint64_t bound =
      session != nullptr ? world_scene_.camera().bound_object() : 0u;
  auto *const target =
      bound != 0u ? session->objects().GetMutableUnit(ObjectGuid(bound)) : nullptr;

  if (last_camera_alpha_target_ != 0u &&
      last_camera_alpha_target_ != bound && session != nullptr) {
    if (auto *const previous =
            session->objects().GetMutableUnit(ObjectGuid(last_camera_alpha_target_));
        previous != nullptr) {
      previous->Presentation().SetUnitAlpha(1.0f);

      if (previous->IsActivePlayer()) {
        (void)world::Camera_SetActivePlayerBoundAlphaVisible(true);
      }
    }
  }
  last_camera_alpha_target_ = target != nullptr ? bound : 0u;
  if (target == nullptr) {
    return;
  }
  const std::uint8_t alpha = world_scene_.camera().camera_target_alpha();

  if (target->IsActivePlayer()) {
    (void)world::Camera_SetActivePlayerBoundAlphaVisible(alpha != 0u);
  }
  target->Presentation().SetUnitAlpha(static_cast<float>(alpha) / 255.0f);
}

void GameLoop::ResolveFrameCameraPose(const float dt) {
  const float aspect =
      static_cast<float>(screen_width_) / static_cast<float>(std::max(1, screen_height_));
  const world::CameraFrameContext frame{
      .aspect_ratio = aspect,
      .near_plane = kWorldCameraNearClip,
      .far_plane = ResolveWorldCameraFarClip(),
  };

  const ObjectManager *objects = world_session() != nullptr ? &world_session()->objects() : nullptr;
  auto &commentator = CommentatorState::Get();
  commentator.UpdateCamera(dt, objects);

  std::array<world::CameraPoseOverride, 2> overrides{};
  std::size_t override_count = 0;
  if (auto cinematic = ResolveCinematicCameraPose(cinematic_player_, frame)) {
    overrides[override_count++] = *cinematic;
  }
  if (auto commentator_pose = ResolveCommentatorCameraPose(commentator)) {
    overrides[override_count++] = *commentator_pose;
  }

  const auto &pose = world_scene_.camera().ResolveFramePose(
      frame, std::span<const world::CameraPoseOverride>(overrides).first(override_count));

  world_frame_.SetCamera(
      render::RenderMatrix4x4View{pose.view.data(), pose.view.size()},
      render::RenderMatrix4x4View{pose.projection.data(), pose.projection.size()});
}

void GameLoop::UpdateSoundListenerForFrame() {
  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  const bool listener_at_character = cvars.GetCVarBool("Sound_ListenerAtCharacter");
  sound_runtime_.SetListenerAtCharacter(listener_at_character);

  const auto &camera = world_scene_.camera().frame_pose();
  std::array<float, 3> listener_position = camera.position;
  std::array<float, 3> listener_forward = camera.forward;
  std::array<float, 3> listener_up = camera.up;

  if (listener_at_character && world_session() != nullptr) {

    auto &objects = world_session()->objects();
    const CGObject_C *subject = nullptr;
    const ObjectGuid mover_guid = objects.player_control().ActiveMoverGuid();
    if (!mover_guid.IsEmpty()) {
      const auto *mover = objects.Get(mover_guid);
      if (mover != nullptr && mover->IsUnit()) {
        subject = mover;
      }
    }
    if (subject == nullptr) {
      subject = objects.GetActivePlayer();
    }
    if (subject != nullptr) {
      const Position position = subject->GetPosition();
      listener_forward = {
          std::cos(position.facing),
          std::sin(position.facing),
          0.0f,
      };
      const float back_distance = cvars.Exists("Sound_ListenerBackDist")
                                      ? cvars.GetCVarFloat("Sound_ListenerBackDist")
                                      : 2.0f;
      const float up_distance = cvars.Exists("Sound_ListenerUpDist")
                                    ? cvars.GetCVarFloat("Sound_ListenerUpDist")
                                    : 4.0f;
      listener_position = {
          position.x - listener_forward[0] * back_distance,
          position.y - listener_forward[1] * back_distance,
          position.z + up_distance,
      };
      listener_up = {0.0f, 0.0f, 1.0f};
    }
  }

  static constexpr std::array<float, 3> kStationaryVelocity{};
  sound_runtime_.SetListener3DAttributes(
      listener_position.data(), kStationaryVelocity.data(),
      listener_forward.data(), listener_up.data());
}

void GameLoop::HandlePerFrameWorldMaintenance(const std::uint32_t current_tick_ms) {
  if (!world_session()) {
    return;
  }

  world_session()->chat_sender().Update(current_tick_ms);
  world_session()->macros().UpdateAllPendingIcons();
  openwow::ui::game::GameUI_CapturePointProximityCheck(
      openwow::ui::game::GetCapturePointUIManagerState(), world_session()->objects());

  const auto *active_player = world_session()->objects().GetActivePlayer();

  if (dbc_ != nullptr && active_player != nullptr &&
      area_trigger_system_.ConsumeCheckDue(current_tick_ms) &&
      active_player->State().GetHealth() > 0u) {
    auto &objects = world_session()->objects();
    if (auto *const active_mover =
            objects.GetMutableUnit(objects.player_control().ActiveMoverGuid());
        active_mover != nullptr) {
      const Position player_position = active_player->GetPosition();

      const std::uint32_t current_map_id =
          world_session()->has_current_map() ? world_session()->current_map_id()
                                             : current_map_id_;
      if (const auto trigger_id = area_trigger_system_.Update(
              dbc_->area_trigger().entries(), current_map_id,
              {player_position.x, player_position.y, player_position.z});
          trigger_id.has_value()) {
        (void)active_mover->Movement().SendImmediateMovementPacket(
            *world_session(),
            static_cast<std::uint16_t>(openwow::net::wotlk::Opcode::MSG_MOVE_HEARTBEAT),
            current_tick_ms);
        world_session()->interaction().SendAreaTrigger(*trigger_id);
      }
    }
  }

  const IdleBillingUpdateResult idle_result = idle_billing_.Update({
      .now_ms = current_tick_ms,
      .has_active_player = active_player != nullptr,
      .active_player_on_taxi = active_player != nullptr && active_player->State().IsTaxiFlight(),
      .active_player_can_auto_sit =
          active_player != nullptr && !active_player->State().IsDeadOrGhost() &&
          !active_player->State().IsTaxiFlight() && !active_player->State().IsSitting() &&
          active_player->Movement().CanControlCharacter(),
      .local_afk_display_active = world_session()->chat_sender().IsLocalAfkDisplayed(),
      .logout_request_active = openwow::net::ClientServices::Instance().HasPendingLogoutRequest(),
  });

  if (idle_result.fire_billing_nag_dialog && game_ui_.is_initialized()) {
    game_ui_.frame_events().dispatcher().FireEvent(kBillingNagDialogEvent,
                                                   idle_result.billing_nag_minutes);
  }

  if (idle_result.fire_igr_billing_nag_dialog && game_ui_.is_initialized()) {
    game_ui_.frame_events().dispatcher().FireEvent(kIgrBillingNagDialogEvent);
  }

  if (idle_result.show_billing_chat_warning) {
    const std::string warning_format =
        Localization::Get().GetString("BILLING_NAG_WARNING", std::string());
    if (!warning_format.empty()) {
      const std::string warning_message = Localization::Get().FormatString(
          warning_format, {std::to_string(idle_result.billing_chat_warning_minutes)});
      ChatFrame_DisplayMessage(world_session()->objects(), warning_message.c_str(),
                               ChatDisplayType::kSystem, nullptr, 0, nullptr, nullptr, nullptr, 0,
                               0, 0, 0, 0, nullptr);
    }
  }

  if (idle_result.sit_for_afk) {
    world_session()->interaction().SendStandStateChange(kStandStateSit);
  }

  if (idle_result.mark_afk) {
    world_session()->chat_sender().SendAfk("");
  }

  if (idle_result.show_idle_logout_message) {
    const std::string idle_message = Localization::Get().GetString("IDLE_MESSAGE", std::string());
    if (!idle_message.empty()) {
      ChatFrame_DisplayMessage(world_session()->objects(), idle_message.c_str(),
                               ChatDisplayType::kSystem, nullptr, 0, nullptr, nullptr, nullptr, 0,
                               0, 0, 0, 0, nullptr);
    }
  }

  if (idle_result.request_idle_logout) {
    (void)openwow::net::ClientServices::Instance().RequestLogout();
  }
}

void GameLoop::RenderWorld(float dt) {
  const auto view_width = static_cast<std::uint16_t>(std::max(1, screen_width_));
  const auto view_height = static_cast<std::uint16_t>(std::max(1, screen_height_));

  if (renderer_context_ != nullptr) {
    BuildWorldFrameGraph(renderer_context_->Graph(), view_width, view_height);
  }
  const WorldSceneRenderViews render_views = ResolveWorldSceneRenderViews(renderer_context_);
  const auto &pose = world_scene_.camera().frame_pose();
  const WorldSceneRenderCamera render_camera{
      .position = pose.position,
      .forward = pose.forward,
      .far_clip = ResolveWorldCameraFarClip(),
  };
  const auto &cvars = openwow::ui::game::CVarSystem::Instance();
  world::ShadowPresentationSettings shadow_settings =
      render::ResolveShadowPresentationSettings(cvars.GetCVarInt("extShadowQuality"),
                                                cvars.GetCVarBool("mapShadows"),
                                                cvars.GetCVarBool("projectedTextures"));
  world_scene_.SetShadowPresentationSettings(shadow_settings);
  world_scene_.SetSpecularEnabled(cvars.GetCVarBool("specular"));

  if (auto *const session = world_session(); session != nullptr) {
    SpellVisuals_UpdateAll(*session, dt);
    session->missile_trajectory().RenderMissileArc();
  }

  world_scene_.PrepareFrame(renderer_context_, render_camera, pose.view.data(),
                            pose.projection.data());
  auto &world_frame = world_frame_;

  const auto scene_framebuffer_views = BuildWorldSceneFramebufferViewList(render_views);

  post_process_.BindSceneFramebufferToViews(
      scene_framebuffer_views, view_width, view_height,
      render::PackedArgbToBgfxRgba(world_scene_.scene_clear_argb()));

  const render::PostProcess::WorldCaptureExtent world_capture_extent =
      post_process_.ResolveWorldCaptureExtent(view_width, view_height);

  render::m2::M2TransparentDrawOrder alpha_view_draw_order;
  if (auto *const session = world_session(); session != nullptr) {
    world_scene_.RenderMissileTrajectory(
        render_views.world.alpha, pose.view.data(), pose.projection.data(),
        session->missile_trajectory().GetRenderSnapshot(), alpha_view_draw_order);
  }

  world_scene_.Render(renderer_context_, render_views, render_camera, pose.view.data(),
                      pose.projection.data(), world_overlay_metrics_,
                      static_cast<float>(world_capture_extent.width),
                      static_cast<float>(world_capture_extent.height),
                      alpha_view_draw_order,
                      false);
  world_frame.ClearSelectionHighlights();

  world_scene_.RenderWaterParticulates(
      renderer_context_, render_views.water_particulates, pose.view.data(), pose.projection.data(),
      static_cast<float>(world_capture_extent.width),
      static_cast<float>(world_capture_extent.height));

  const std::uint8_t postprocess_view =
      ResolveFrameGraphView(renderer_context_, openwow::render::api::FrameGraphPassId::PostProcess,
                            kFallbackPostProcessView);
  const auto postprocess_result = post_process_.Apply(postprocess_view);
  if (renderer_context_ != nullptr && postprocess_result.HasFinalLdrBackbuffer()) {
    renderer_context_->MarkFinalCompositorStage(
        openwow::render::api::FinalCompositorStage::kPostProcess);
  }

  {
    auto &dbg = openwow::debug::RenderDebug::Get();
    if (dbg.IsEnabled()) {
      auto cmds = dbg.GetCommands();
      if (!cmds.empty()) {
        const std::uint8_t debug_view = ResolveDebugOverlayView(renderer_context_);
        (void)openwow::render::ConfigureRendererContextView(
            renderer_context_, debug_view, openwow::render::RendererViewClearFlags::kNone,
            0x00000000u, 1.0f, 0, view_width, view_height, pose.view.data(), pose.projection.data(),
            false);
        debug_draw_renderer_.Render(debug_view, pose.view.data(), pose.projection.data(), cmds);
      }
      dbg.Clear();
    }
  }

}

}

#include "openwow/ui/glue/glue_lua_runtime.h"
#include "openwow/ui/glue/glue_lua_api_internal.h"
#include "openwow/ui/glue/glue_script_events.h"
#include "openwow/ui/glue/server_alert_sync.h"
#include "openwow/ui/glue/glue_widget_runtime.h"
#include "openwow/ui/glue/widget_lua_adapter_support.h"
#include "openwow/audio/playback/audio_engine.h"
#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/core/screenshot_system.h"
#include "openwow/game/account_msg.h"
#include "openwow/game/declined_words.h"
#include "openwow/game/knowledge_base.h"
#include "openwow/media/playback/movie_lifecycle.h"
#include "openwow/ui/game/framescript/core/frame_script_invocation.h"
#include "openwow/ui/game/api/game_lua_api_sound.h"
#include "openwow/ui/game/runtime/layout_persistence.h"
#include "openwow/ui/game/secure_execution.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/frame_event_registration.h"
#include "openwow/ui/frame_script_type_info.h"
#include "openwow/ui/framexml/framexml_name_utils.h"
#include "openwow/ui/animation/animation_lua.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/lua_base_overrides.h"
#include "openwow/ui/lua_taint_api.h"
#include "openwow/ui/production_lua_surface.h"
#include "openwow/ui/display/settings/adapters/production_display_settings_runtime.h"
#include "openwow/ui/runtime/lua/frame_script_runtime.h"
#include "openwow/ui/runtime/lua/lua_composition.h"
#include "openwow/ui/rect_utils.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/vfs/client_path_identity.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_set>
#include <utility>

namespace openwow::ui::glue {

namespace {

void PushFrameScriptSelf(lua_State* lua_state, const std::string& widget_name) {
  if (!widget_name.empty()) {
    lua_getglobal(lua_state, widget_name.c_str());
    if (lua_istable(lua_state, -1) != 0) {
      return;
    }
    lua_pop(lua_state, 1);
    if (detail::PushStoredWidgetTableByRuntimeKey(lua_state, widget_name)) {
      return;
    }
  }
  lua_pushnil(lua_state);
}

openwow::ui::game::FrameScriptInvocationKind GlueInvocationKind(
    const std::string_view event_name) {
  return openwow::text::EqualsIgnoreCaseAscii(event_name, "OnEvent")
             ? openwow::ui::game::FrameScriptInvocationKind::kEvent
             : openwow::ui::game::FrameScriptInvocationKind::kHandler;
}

LuaRunResult InvokePreparedGlueFrameScript(lua_State* const state,
                                           const int self_index,
                                           const int argument_count,
                                           const std::string_view event_name) {

  const int function_index = lua_gettop(state) - argument_count;
  const int taint_source = openwow::ui::lua_get_taint(state, function_index);
  const auto security =
      taint_source != 0
          ? openwow::ui::game::FrameScriptInvocationSecurity::kInsecure
          : openwow::ui::game::FrameScriptInvocationSecurity::kSecure;
  const int status = openwow::ui::game::InvokeFrameScriptFunction(
      state, self_index, argument_count, GlueInvocationKind(event_name),
      security, 0, taint_source);
  if (status == LUA_OK) {
    return {.ok = true, .error = ""};
  }
  const char* error = lua_tostring(state, -1);
  return {
      .ok = false,
      .error = error != nullptr ? std::string(error)
                               : std::string("unknown lua execution error"),
  };
}

bool CallFontObjectMethod(lua_State* state, const int object_index,
                          const char* method, const int argument_count,
                          const std::function<void(lua_State*)>& push_arguments,
                          std::string* error) {
  if (state == nullptr || method == nullptr) {
    return false;
  }

  const int top = lua_gettop(state);
  const int object = lua_absindex(state, object_index);
  lua_getfield(state, object, method);
  if (lua_isfunction(state, -1) == 0) {
    if (error != nullptr) {
      *error = std::string("Font object has no ") + method + " method";
    }
    lua_settop(state, top);
    return false;
  }

  lua_pushvalue(state, object);
  push_arguments(state);
  if (lua_pcall(state, argument_count + 1, 0, 0) == 0) {
    return true;
  }

  if (error != nullptr) {
    const char* message = lua_tostring(state, -1);
    *error = message != nullptr ? message : "Lua font method failed";
  }
  lua_settop(state, top);
  return false;
}

std::string FontFlagsForDefinition(const openwow::ui::FontDefinition& definition) {
  std::string flags;
  const std::string outline = openwow::text::ToLowerAscii(definition.outline);
  if (outline == "thick" || outline == "thickoutline") {
    flags = "THICKOUTLINE";
  } else if (outline == "normal" || outline == "outline") {
    flags = "OUTLINE";
  }
  if (definition.monochrome) {
    if (!flags.empty()) {
      flags += '|';
    }
    flags += "MONOCHROME";
  }
  return flags;
}

float ComputeGlueMoveSizingEffectiveScale(const GlueWidgetRuntime& runtime,
                                          const std::string& widget_name) {
  const float effective_scale = runtime.ndc_to_pixel() * runtime.GetEffectiveScale(widget_name);
  return effective_scale > 0.0f ? effective_scale : 1.0f;
}

std::string ResolveGlueMoveSizingRelativeName(const GlueWidgetRuntime& runtime,
                                              const GlueWidgetState& widget) {
  if (const auto anchor = runtime.GetPoint(widget.name, 1); anchor.has_value() &&
      !anchor->relative_to.empty()) {
    return anchor->relative_to;
  }
  if (!widget.parent.empty()) {
    return widget.parent;
  }
  return "UIParent";
}

openwow::ui::framexml::FrameRect ResolveGlueMoveSizingReferenceRect(
    const GlueWidgetRuntime& runtime, const std::string& relative_name) {
  if (!relative_name.empty() && relative_name != "UIParent") {
    if (const auto widget = runtime.GetWidget(relative_name); widget.has_value()) {
      return openwow::ui::framexml::FrameRect{
          .x = widget->x,
          .y = widget->y,
          .width = widget->width,
          .height = widget->height,
      };
    }
  }

  return openwow::ui::framexml::FrameRect{
      .x = 0,
      .y = 0,
      .width = runtime.viewport_width(),
      .height = runtime.viewport_height(),
  };
}

void ApplyGlueLiveMoveSizingRectToRuntime(GlueWidgetRuntime& runtime, const std::string& widget_name,
                                          const std::string& relative_name,
                                          const openwow::ui::framexml::FrameRect& relative_rect,
                                          const openwow::ui::framexml::FrameRect& drag_rect,
                                          const float effective_scale) {
  const std::string resolved_relative_name =
      relative_name.empty() ? std::string("UIParent") : relative_name;
  runtime.ClearAllPoints(widget_name);
  runtime.SetPoint(widget_name, "TOPLEFT", resolved_relative_name, "TOPLEFT",
                   static_cast<float>(drag_rect.x - relative_rect.x) / effective_scale,
                   static_cast<float>(relative_rect.y - drag_rect.y) / effective_scale);
  runtime.SetSize(widget_name, static_cast<float>(drag_rect.width) / effective_scale,
                  static_cast<float>(drag_rect.height) / effective_scale);
}

bool MovieSubtitlesEnabled(lua_State* L, const std::string& widget_name) {
  if (L == nullptr || widget_name.empty()) return false;

  const bool cvar_enabled = openwow::ui::game::CVarSystem::Instance().GetCVarBool("movieSubtitle");
  if (!cvar_enabled) return false;

  bool enabled = true;
  if (detail::PushStoredWidgetTableByRuntimeKey(L, widget_name)) {
    lua_getfield(L, -1, "__ow_subtitles_enabled");
    if (lua_isboolean(L, -1) != 0) {
      enabled = (lua_toboolean(L, -1) != 0);
    }
    lua_pop(L, 2);
  }
  return enabled;
}

bool IsGlueModelWidgetKind(const std::string& kind) {
  return openwow::text::EqualsIgnoreCaseAscii(kind, "Model") ||
         openwow::text::EqualsIgnoreCaseAscii(kind, "ModelFFX") ||
         openwow::text::EqualsIgnoreCaseAscii(kind, "PlayerModel") ||
         openwow::text::EqualsIgnoreCaseAscii(kind, "DressUpModel");
}

}

GlueLuaRuntime::GlueLuaRuntime(IsolatedRuntime, openwow::audio::SoundRuntime& sound_runtime)
    : GlueLuaRuntime(nullptr, sound_runtime) {}

GlueLuaRuntime::GlueLuaRuntime(
    openwow::ui::display::ProductionDisplaySettingsRuntime& runtime,
    openwow::audio::SoundRuntime& sound_runtime)
    : GlueLuaRuntime(&runtime, sound_runtime) {}

GlueLuaRuntime::GlueLuaRuntime(
    openwow::ui::display::ProductionDisplaySettingsRuntime* runtime,
    openwow::audio::SoundRuntime& sound_runtime)
    : sound_runtime_(sound_runtime),
      sound_lua_context_(
          std::make_unique<openwow::ui::game::detail::SoundLuaContext>(
              sound_runtime_, nullptr)) {
  if (runtime == nullptr) {
    isolated_display_settings_runtime_ =
        std::make_unique<
            openwow::ui::display::ProductionDisplaySettingsRuntime>(
            openwow::ui::display::ProductionDisplaySettingsRuntime::
                IsolatedRuntime{});
    runtime = isolated_display_settings_runtime_.get();
  }
  display_settings_runtime_ = runtime;
  xml_text_lua_state_ = std::make_shared<lua_State*>(nullptr);
  async_event_queue_ = std::make_shared<AsyncEventQueue>();
  frame_scheduler_handle_ = openwow::core::FrameScheduler::Instance().Register(
      openwow::core::Phase::EarlyUpdate,
      0,
      [this](double) { DrainPostedEvents(); },
      "GlueLuaRuntime::DrainPostedEvents");
  ResetVm();
}

GlueLuaRuntime::~GlueLuaRuntime() {
  openwow::core::FrameScheduler::Instance().Unregister(frame_scheduler_handle_);
  frame_scheduler_handle_ = openwow::core::CallbackHandle::Invalid;
  DestroyVmState();
  async_event_queue_.reset();
}

void GlueLuaRuntime::DiscardMoviePlayback() {
  if (movie_player_.IsPlaying() || movie_player_.HasAudioSource() ||
      movie_player_.CurrentFrameRGBA() != nullptr) {
    openwow::media::ReleaseMoviePlayback(
        [this] { sound_runtime_.StopMovieAudio(); },
        [this] { movie_player_.Stop(); });
  }
  (void)movie_player_.ConsumeEvents();
  movie_widget_name_.clear();
}

void GlueLuaRuntime::DestroyVmState() noexcept {
  DiscardMoviePlayback();
  if (widget_runtime_ != nullptr) {
    widget_runtime_->ClearLifecycleVisibilityOverrides();
  }
  if (async_event_queue_) {
    std::lock_guard<std::mutex> lock(async_event_queue_->mutex);
    async_event_queue_->accept_events = false;
    async_event_queue_->pending_events.clear();
  }
  *xml_text_lua_state_ = nullptr;
  lua_state_ = nullptr;
  frame_script_runtime_.reset();
  openwow::ui::game::SecureExecution::Get().Reset();
  inline_script_refs_by_source_.clear();
  widgets_by_event_.clear();
  events_by_widget_.clear();
  widget_script_presence_cache_.clear();
  per_frame_widget_cache_.clear();
  per_frame_widget_order_revision_ = 0;
  per_frame_widget_cache_rebuild_count_ = 0;
  per_frame_widget_cache_dirty_ = true;
  last_effective_visible_.clear();
  visibility_generations_.clear();
  last_visibility_revision_ = 0;
  logged_visibility_errors_.clear();
  hovered_widget_.clear();
  xml_font_registry_.Clear();
  openwow::game::AccountMsg::Get().SetGlueFrameEventsRegistered(false);
  attached_to_glue_ = false;
}

void GlueLuaRuntime::ResetVm() {
  DestroyVmState();
  if (async_event_queue_) {
    std::lock_guard<std::mutex> lock(async_event_queue_->mutex);
    async_event_queue_->accept_events = true;
  }
  attached_to_glue_ = true;
  frame_script_runtime_ =
      std::make_unique<openwow::ui::lua::FrameScriptRuntime>();
  if (!frame_script_runtime_->BootGlue(
          openwow::ui::CreateProductionGlueLuaBindings(
              *display_settings_runtime_),
          openwow::ui::InitializeProductionGlueLuaVm)) {
    frame_script_runtime_.reset();
  }
  lua_state_ = frame_script_runtime_ != nullptr
                   ? frame_script_runtime_->state()
                   : nullptr;
  *xml_text_lua_state_ = lua_state_;
  if (lua_state_ != nullptr) {
    openwow::ui::game::detail::BindSoundLuaContext(*lua_state_,
                                                   *sound_lua_context_);
    openwow::game::AccountMsg::Get().SetGlueFrameEventsRegistered(true);
    lua_pushlightuserdata(lua_state_, widget_runtime_);
    lua_setfield(lua_state_, LUA_REGISTRYINDEX, detail::kWidgetRuntimeRegistryKey);
    lua_pushlightuserdata(lua_state_, this);
    lua_setfield(lua_state_, LUA_REGISTRYINDEX, detail::kGlueRuntimeRegistryKey);
    lua_pushlightuserdata(lua_state_, host_);
    lua_setfield(lua_state_, LUA_REGISTRYINDEX, detail::kGlueHostRegistryKey);
    lua_pushlightuserdata(lua_state_, game_state_);
    lua_setfield(lua_state_, LUA_REGISTRYINDEX, detail::kGlueGameStateRegistryKey);
    lua_pushlightuserdata(
        lua_state_, const_cast<openwow::core::ida::GameTimeData*>(game_time_));
    lua_setfield(lua_state_, LUA_REGISTRYINDEX, detail::kGameTimeRegistryKey);

    lua_pushlightuserdata(
        lua_state_,
        const_cast<openwow::vfs::VirtualFileSystem*>(vfs_));
    lua_setfield(lua_state_, LUA_REGISTRYINDEX,
                 openwow::ui::game::detail::kTextureVfsRegistryKey);
    if (dbc_loader_) {
      lua_pushlightuserdata(lua_state_, const_cast<openwow::data::dbc::DbcLoader*>(dbc_loader_));
      lua_setfield(lua_state_, LUA_REGISTRYINDEX, detail::kDbcLoaderRegistryKey);
    }
    detail::PublishWidgetsAsGlobals(lua_state_, widget_runtime_);
    SeedVisibilityCacheFromRuntime();
  }
}

GlueLuaRuntime::SingleScriptResult GlueLuaRuntime::RegisterXmlFont(
    const openwow::ui::FontDefinition& definition) {
  if (definition.name.empty()) {
    return {.ok = true};
  }
  if (lua_state_ == nullptr) {
    return {.ok = false, .error = "Lua VM not initialized"};
  }

  xml_font_registry_.Register(definition);
  const auto resolved = xml_font_registry_.Get(definition.name);
  if (!resolved.has_value()) {
    return {.ok = false,
            .error = "could not resolve XML Font " + definition.name};
  }

  const int top = lua_gettop(lua_state_);
  lua_getglobal(lua_state_, "CreateFont");
  lua_pushlstring(lua_state_, definition.name.data(), definition.name.size());
  if (lua_pcall(lua_state_, 1, 1, 0) != 0 ||
      lua_istable(lua_state_, -1) == 0) {
    const char* message = lua_tostring(lua_state_, -1);
    const std::string error =
        "unable to create XML Font " + definition.name + ": " +
        (message != nullptr ? message : "CreateFont returned no object");
    lua_settop(lua_state_, top);
    return {.ok = false, .error = error};
  }
  const int font_object = lua_absindex(lua_state_, -1);

  std::string error;
  const auto call = [&](const char* method, const int argument_count,
                        const std::function<void(lua_State*)>& push_arguments) {
    if (!error.empty()) {
      return;
    }
    (void)CallFontObjectMethod(lua_state_, font_object, method,
                               argument_count, push_arguments, &error);
  };

  if (!definition.inherits.empty()) {
    call("SetFontObject", 1, [&](lua_State* state) {
      lua_pushlstring(state, definition.inherits.data(),
                      definition.inherits.size());
    });
  }
  if (!resolved->font_file.empty() && resolved->has_height) {
    const float height_px =
        openwow::ui::StoredUiHorizontalCoordinateToPixels(resolved->height);
    const std::string flags = FontFlagsForDefinition(*resolved);
    call("SetFont", 3, [&](lua_State* state) {
      lua_pushlstring(state, resolved->font_file.data(),
                      resolved->font_file.size());
      lua_pushnumber(state, height_px);
      lua_pushlstring(state, flags.data(), flags.size());
    });
  }
  if (resolved->has_color) {
    call("SetTextColor", 4, [&](lua_State* state) {
      lua_pushnumber(state, resolved->color.r);
      lua_pushnumber(state, resolved->color.g);
      lua_pushnumber(state, resolved->color.b);
      lua_pushnumber(state, resolved->color.a);
    });
  }
  if (resolved->shadow.has_value()) {
    const auto shadow = *resolved->shadow;
    call("SetShadowColor", 4, [&](lua_State* state) {
      lua_pushnumber(state, shadow.color.r);
      lua_pushnumber(state, shadow.color.g);
      lua_pushnumber(state, shadow.color.b);
      lua_pushnumber(state, shadow.color.a);
    });
    call("SetShadowOffset", 2, [&](lua_State* state) {
      lua_pushnumber(
          state,
          openwow::ui::StoredUiHorizontalCoordinateToPixels(shadow.offset.x));
      lua_pushnumber(
          state,
          openwow::ui::StoredUiHorizontalCoordinateToPixels(shadow.offset.y));
    });
  }
  if (resolved->has_justify_h) {
    call("SetJustifyH", 1, [&](lua_State* state) {
      lua_pushlstring(state, resolved->justify_h.data(),
                      resolved->justify_h.size());
    });
  }
  if (resolved->has_justify_v) {
    call("SetJustifyV", 1, [&](lua_State* state) {
      lua_pushlstring(state, resolved->justify_v.data(),
                      resolved->justify_v.size());
    });
  }
  if (resolved->has_spacing) {
    call("SetSpacing", 1, [&](lua_State* state) {
      lua_pushnumber(
          state,
          openwow::ui::StoredUiHorizontalCoordinateToPixels(resolved->spacing));
    });
  }
  if (resolved->has_indented_word_wrap) {
    call("SetIndentedWordWrap", 1, [&](lua_State* state) {
      lua_pushboolean(state, resolved->indented_word_wrap ? 1 : 0);
    });
  }

  lua_settop(lua_state_, top);
  if (!error.empty()) {
    return {.ok = false,
            .error = "XML Font " + definition.name + ": " + error};
  }
  return {.ok = true};
}

void GlueLuaRuntime::BindHost(GlueHost* host) {
  host_ = host;
  if (lua_state_ != nullptr) {
    lua_pushlightuserdata(lua_state_, host_);
    lua_setfield(lua_state_, LUA_REGISTRYINDEX, detail::kGlueHostRegistryKey);
  }
}

void GlueLuaRuntime::BindGameState(GlueGameState* game_state) {
  game_state_ = game_state;
  if (lua_state_ != nullptr) {
    lua_pushlightuserdata(lua_state_, game_state_);
    lua_setfield(lua_state_, LUA_REGISTRYINDEX, detail::kGlueGameStateRegistryKey);
  }
}

void GlueLuaRuntime::BindGameTimeData(
    const openwow::core::ida::GameTimeData* const game_time) {
  game_time_ = game_time;
  if (lua_state_ != nullptr) {
    lua_pushlightuserdata(
        lua_state_, const_cast<openwow::core::ida::GameTimeData*>(game_time_));
    lua_setfield(lua_state_, LUA_REGISTRYINDEX, detail::kGameTimeRegistryKey);
  }
}

void GlueLuaRuntime::BindWidgetRuntime(GlueWidgetRuntime* widget_runtime) {
  if (widget_runtime_ != nullptr && widget_runtime_ != widget_runtime) {
    widget_runtime_->SetXmlTextResolver({});
  }
  widget_runtime_ = widget_runtime;
  InvalidatePerFrameWidgetCache();
  active_move_sizing_ = {};
  last_effective_visible_.clear();
  visibility_generations_.clear();
  last_visibility_revision_ = 0;
  if (widget_runtime_ != nullptr) {
    widget_runtime_->ClearLifecycleVisibilityOverrides();
    widget_runtime_->SetXmlTextResolver(
        [weak_state = std::weak_ptr<lua_State*>(xml_text_lua_state_)](
            const std::string_view token)
            -> std::optional<std::string> {
          const auto state_slot = weak_state.lock();
          lua_State* const state =
              state_slot != nullptr ? *state_slot : nullptr;
          if (state == nullptr || token.empty()) {
            return std::nullopt;
          }
          const std::string global_name(token);
          const int top = lua_gettop(state);
          lua_getglobal(state, global_name.c_str());
          std::optional<std::string> result;
          if (lua_type(state, -1) == LUA_TSTRING) {
            std::size_t length = 0;
            if (const char* value = lua_tolstring(state, -1, &length);
                value != nullptr) {
              result.emplace(value, length);
            }
          }
          lua_settop(state, top);
          return result;
        });
  }
  if (lua_state_ != nullptr) {
    lua_pushlightuserdata(lua_state_, widget_runtime_);
    lua_setfield(lua_state_, LUA_REGISTRYINDEX, detail::kWidgetRuntimeRegistryKey);
    lua_pushlightuserdata(lua_state_, this);
    lua_setfield(lua_state_, LUA_REGISTRYINDEX, detail::kGlueRuntimeRegistryKey);
    detail::PublishWidgetsAsGlobals(lua_state_, widget_runtime_);
    SeedVisibilityCacheFromRuntime();
  }
}

void GlueLuaRuntime::UpdateWidgetAnimations(const std::string& widget_name,
                                            const double elapsed_seconds) {
  if (lua_state_ == nullptr || widget_runtime_ == nullptr || widget_name.empty()) {
    return;
  }

  const int top = lua_gettop(lua_state_);
  if (detail::PushStoredWidgetTableByRuntimeKey(lua_state_, widget_name)) {
    openwow::ui::anim::UpdateRegionAnimationGroups(
        lua_state_, -1, static_cast<float>(std::max(0.0, elapsed_seconds)));
    const auto animation =
        openwow::ui::anim::GetRegionAnimationState(lua_state_, -1);
    const float translation_scale =
        widget_runtime_->ndc_to_pixel() * widget_runtime_->GetEffectiveScale(widget_name);
    widget_runtime_->SetAnimationTransform(
        widget_name, animation.translation_x * translation_scale,
        animation.translation_y * translation_scale, animation.scale_x,
        animation.scale_y, animation.rotation_radians, animation.alpha,
        animation.alpha_change);
  }
  lua_settop(lua_state_, top);
}

void GlueLuaRuntime::BindDbcLoader(const openwow::data::dbc::DbcLoader* dbc) {
  dbc_loader_ = dbc;
  openwow::game::DeclinedWords::Get().BindDbcLoader(dbc_loader_);
  if (lua_state_ != nullptr && dbc_loader_) {
    lua_pushlightuserdata(lua_state_, const_cast<openwow::data::dbc::DbcLoader*>(dbc_loader_));
    lua_setfield(lua_state_, LUA_REGISTRYINDEX, detail::kDbcLoaderRegistryKey);
  }
}

void GlueLuaRuntime::BindLuaEventTrace(GlueLuaEventTrace* trace) {
  lua_event_trace_ = trace;
}

void GlueLuaRuntime::BindBindingRegistry(const GlueBindingRegistry* bindings) {
  bindings_ = bindings;
  widget_script_presence_cache_.clear();
  InvalidatePerFrameWidgetCache();
}

void GlueLuaRuntime::BindFontRegistry(const GlueFontRegistry* font_registry) {
  font_registry_ = font_registry;
}

GlueLuaLoadResult GlueLuaRuntime::LoadScripts(const openwow::vfs::VirtualFileSystem& vfs,
                                              const std::vector<std::string>& script_paths) {
  vfs_ = &vfs;
  ResetVm();
  scripts_.clear();
  invocation_history_.clear();

  GlueLuaLoadResult result{.ok = true};
  std::vector<std::string> errors;
  errors.reserve(8);
  if (lua_state_ == nullptr) {
    result.ok = false;
    result.error = "failed to initialize Lua VM";
    return result;
  }

  for (const auto& script_path : script_paths) {
    const auto content = vfs.ReadTextFile(script_path);
    if (!content) {
      std::string error;
      if (vfs.Exists(script_path)) {
        error = "file exists but could not be read: " + script_path;
      } else {
        error = "file not found: " + script_path;
      }
      errors.push_back(error);
      continue;
    }

    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "Glue Lua candidate loaded: " + script_path + " bytes="
                           + std::to_string(content->size()));

    scripts_.insert_or_assign(script_path, *content);
    const std::string chunk_name =
        openwow::vfs::BuildClientLuaChunkName(script_path);
    if (openwow::ui::LoadClientLuaChunk(lua_state_, *content,
                                        chunk_name.c_str()) != 0) {
      const char* err = lua_tostring(lua_state_, -1);
      errors.push_back(script_path + ": " + (err ? std::string(err) : std::string("lua compile error")));
      lua_pop(lua_state_, 1);
      continue;
    }
    if (lua_pcall(lua_state_, 0, 0, 0) != 0) {
      const char* err = lua_tostring(lua_state_, -1);
      errors.push_back(script_path + ": " + (err ? std::string(err) : std::string("lua runtime error")));
      lua_pop(lua_state_, 1);
      continue;
    }

    result.loaded_scripts.push_back(script_path);
  }

  if (result.loaded_scripts.empty() && errors.empty()) {
    result.ok = false;
    result.error = "no Glue Lua scripts found in VFS";
    return result;
  }

  if (!errors.empty()) {
    result.ok = false;
    result.error =
        "Glue Lua script load failed (" + std::to_string(errors.size()) + "): " + errors.front();
    constexpr std::size_t kLogLimit = 12;
    const std::size_t limit = std::min(kLogLimit, errors.size());
    for (std::size_t i = 0; i < limit; ++i) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, "Glue Lua load error: " + errors[i]);
    }
    if (errors.size() > limit) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "Glue Lua load error: ... (" + std::to_string(errors.size() - limit) + " more)");
    }
    return result;
  }
  return result;
}

void GlueLuaRuntime::InitializeVm(const openwow::vfs::VirtualFileSystem& vfs) {
  vfs_ = &vfs;
  ResetVm();
  scripts_.clear();
  invocation_history_.clear();
  interleaved_loaded_scripts_.clear();
}

GlueLuaRuntime::SingleScriptResult GlueLuaRuntime::ExecuteFile(const std::string& script_path) {
  if (lua_state_ == nullptr) {
    return {.ok = false, .error = "Lua VM not initialized"};
  }
  if (vfs_ == nullptr) {
    return {.ok = false, .error = "VFS not bound"};
  }
  const auto content = vfs_->ReadTextFile(script_path);
  if (!content) {
    if (vfs_->Exists(script_path)) {
      return {.ok = false, .error = "file exists but could not be read: " + script_path};
    }
    return {.ok = false, .error = "file not found: " + script_path};
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "Glue Lua executing: " + script_path + " bytes=" + std::to_string(content->size()));

  scripts_.insert_or_assign(script_path, *content);
  const std::string chunk_name =
      openwow::vfs::BuildClientLuaChunkName(script_path);
  if (openwow::ui::LoadClientLuaChunk(lua_state_, *content,
                                      chunk_name.c_str()) != 0) {
    const char* err = lua_tostring(lua_state_, -1);
    std::string error_str = script_path + ": " + (err ? std::string(err) : "lua compile error");
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, "Glue Lua compile error: " + error_str);
    lua_pop(lua_state_, 1);
    return {.ok = false, .error = error_str};
  }
  if (lua_pcall(lua_state_, 0, 0, 0) != 0) {
    const char* err = lua_tostring(lua_state_, -1);
    std::string error_str = script_path + ": " + (err ? std::string(err) : "lua runtime error");
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, "Glue Lua runtime error: " + error_str);
    lua_pop(lua_state_, 1);
    return {.ok = false, .error = error_str};
  }

  interleaved_loaded_scripts_.push_back(script_path);
  return {.ok = true};
}

GlueLuaRuntime::SingleScriptResult GlueLuaRuntime::ExecuteString(const std::string& lua_code,
                                                                 const std::string& source_name) {
  if (lua_state_ == nullptr) {
    return {.ok = false, .error = "Lua VM not initialized"};
  }
  if (lua_code.empty()) {
    return {.ok = true};
  }

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug,
                     "Glue Lua executing inline: " + source_name + " bytes=" + std::to_string(lua_code.size()));

  if (openwow::ui::LoadClientLuaChunk(lua_state_, lua_code, source_name.c_str()) != 0) {
    const char* err = lua_tostring(lua_state_, -1);
    std::string error_str = source_name + ": " + (err ? std::string(err) : "lua compile error");
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, "Glue inline Lua compile error: " + error_str);
    lua_pop(lua_state_, 1);
    return {.ok = false, .error = error_str};
  }
  if (lua_pcall(lua_state_, 0, 0, 0) != 0) {
    const char* err = lua_tostring(lua_state_, -1);
    std::string error_str = source_name + ": " + (err ? std::string(err) : "lua runtime error");
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, "Glue inline Lua runtime error: " + error_str);
    lua_pop(lua_state_, 1);
    return {.ok = false, .error = error_str};
  }

  return {.ok = true};
}

const std::vector<std::string>& GlueLuaRuntime::loaded_scripts() const {
  return interleaved_loaded_scripts_;
}

void GlueLuaRuntime::PublishNewWidgets(const std::vector<std::string>& widget_names) {
  if (lua_state_ == nullptr) return;
  for (const auto& name : widget_names) {
    detail::PublishWidgetGlobal(lua_state_, name);
  }
  for (const auto& name : widget_names) {
    detail::FinalizePublishedWidgetGlobal(lua_state_, name);
  }
  if (!widget_names.empty()) {
    InvalidatePerFrameWidgetCache();
  }
}

bool GlueLuaRuntime::HasFunction(const std::string& function_name) const {
  if (lua_state_ == nullptr) {
    return false;
  }
  lua_getglobal(lua_state_, function_name.c_str());
  const bool is_fn = lua_isfunction(lua_state_, -1) != 0;
  lua_pop(lua_state_, 1);
  return is_fn;
}

bool GlueLuaRuntime::HasAnyFunction(const std::vector<std::string>& function_names) const {
  for (const auto& name : function_names) {
    if (HasFunction(name)) {
      return true;
    }
  }
  return false;
}

bool GlueLuaRuntime::HasGlobal(const std::string& global_name) const {
  if (lua_state_ == nullptr || global_name.empty()) {
    return false;
  }
  lua_getglobal(lua_state_, global_name.c_str());
  const bool ok = lua_isnil(lua_state_, -1) == 0;
  lua_pop(lua_state_, 1);
  return ok;
}

void GlueLuaRuntime::RegisterAccountMsgGlueFunctions() {
  if (lua_state_ == nullptr) {
    return;
  }

  openwow::game::AccountMsg::Get().SetGlueFrameEventsRegistered(true);
}

void GlueLuaRuntime::ShutdownForWorld() noexcept {
  DestroyVmState();
}

void GlueLuaRuntime::UnsetGlobal(const std::string& global_name) {
  if (lua_state_ == nullptr || global_name.empty()) {
    return;
  }

  openwow::ui::UnregisterLuaGlobal(lua_state_, global_name.c_str());
}

std::string GlueLuaRuntime::GetGlobalStringOrEmpty(const std::string& global_name) const {
  if (lua_state_ == nullptr || global_name.empty()) {
    return {};
  }
  lua_getglobal(lua_state_, global_name.c_str());
  const char* value = (lua_isstring(lua_state_, -1) != 0) ? lua_tostring(lua_state_, -1) : nullptr;
  std::string out = value != nullptr ? std::string(value) : std::string();
  lua_pop(lua_state_, 1);
  return out;
}

const openwow::vfs::VirtualFileSystem* GlueLuaRuntime::vfs() const {
  return vfs_;
}

const GlueFontRegistry* GlueLuaRuntime::font_registry() const {
  return font_registry_;
}

bool GlueLuaRuntime::GlobalIsTable(const std::string& global_name) const {
  if (lua_state_ == nullptr || global_name.empty()) {
    return false;
  }
  lua_getglobal(lua_state_, global_name.c_str());
  const bool ok = lua_istable(lua_state_, -1) != 0;
  lua_pop(lua_state_, 1);
  return ok;
}

bool GlueLuaRuntime::GlobalTableHasField(const std::string& table_name,
                                        const std::string& field_name) const {
  if (lua_state_ == nullptr || table_name.empty() || field_name.empty()) {
    return false;
  }
  lua_getglobal(lua_state_, table_name.c_str());
  if (lua_istable(lua_state_, -1) == 0) {
    lua_pop(lua_state_, 1);
    return false;
  }
  lua_getfield(lua_state_, -1, field_name.c_str());
  const bool ok = lua_isnil(lua_state_, -1) == 0;
  lua_pop(lua_state_, 2);
  return ok;
}

bool GlueLuaRuntime::GlobalTableHasFunction(const std::string& table_name,
                                           const std::string& field_name) const {
  if (lua_state_ == nullptr || table_name.empty() || field_name.empty()) {
    return false;
  }
  lua_getglobal(lua_state_, table_name.c_str());
  if (lua_istable(lua_state_, -1) == 0) {
    lua_pop(lua_state_, 1);
    return false;
  }
  lua_getfield(lua_state_, -1, field_name.c_str());
  const bool ok = lua_isfunction(lua_state_, -1) != 0;
  lua_pop(lua_state_, 2);
  return ok;
}

LuaRunResult GlueLuaRuntime::RunFunction(const std::string& function_name) {
  return RunFunctionWithContext(function_name, "");
}

LuaRunResult GlueLuaRuntime::RunFunctionWithContext(const std::string& function_name,
                                                    const std::string& self_widget_name) {
  return RunFunctionWithThisAndArgs(function_name, self_widget_name, {}, true);
}

LuaRunResult GlueLuaRuntime::RunFunctionWithThisAndArgs(const std::string& function_name,
                                                        const std::string& self_widget_name,
                                                        const std::vector<std::string>& args,
                                                        bool pass_self_arg) {
  if (!HasFunction(function_name)) {
    return {.ok = false, .error = "Glue Lua function missing: " + function_name};
  }

  invocation_history_.push_back(function_name);
  const int top = lua_gettop(lua_state_);
  int self_index = 0;
  if (!self_widget_name.empty()) {
    if (detail::PushStoredWidgetTableByRuntimeKey(lua_state_,
                                                  self_widget_name)) {
      self_index = lua_absindex(lua_state_, -1);
    }
  }

  lua_getglobal(lua_state_, function_name.c_str());
  for (const auto& arg : args) {
    lua_pushstring(lua_state_, arg.c_str());
  }
  const auto result = InvokePreparedGlueFrameScript(
      lua_state_, pass_self_arg ? self_index : 0,
      static_cast<int>(args.size()), "");
  lua_settop(lua_state_, top);
  return result;
}

LuaRunResult GlueLuaRuntime::RunFunctionWithThisAndNumberArgs(const std::string& function_name,
                                                              const std::string& self_widget_name,
                                                              const std::vector<double>& args,
                                                              bool pass_self_arg) {
  if (!HasFunction(function_name)) {
    return {.ok = false, .error = "Glue Lua function missing: " + function_name};
  }

  invocation_history_.push_back(function_name);
  const int top = lua_gettop(lua_state_);
  int self_index = 0;
  if (!self_widget_name.empty()) {
    if (detail::PushStoredWidgetTableByRuntimeKey(lua_state_,
                                                  self_widget_name)) {
      self_index = lua_absindex(lua_state_, -1);
    }
  }

  lua_getglobal(lua_state_, function_name.c_str());
  for (const auto& arg : args) {
    lua_pushnumber(lua_state_, static_cast<lua_Number>(arg));
  }
  const auto result = InvokePreparedGlueFrameScript(
      lua_state_, pass_self_arg ? self_index : 0,
      static_cast<int>(args.size()), "");
  lua_settop(lua_state_, top);
  return result;
}

LuaRunResult GlueLuaRuntime::DispatchFirstAvailable(const std::vector<std::string>& function_names,
                                                    const std::string& event_source) {
  return DispatchFirstAvailableWithArgs(function_names, event_source, {}, true);
}

LuaRunResult GlueLuaRuntime::DispatchFirstAvailableWithArgs(const std::vector<std::string>& function_names,
                                                            const std::string& event_source,
                                                            const std::vector<std::string>& args,
                                                            bool pass_self_arg) {
  const auto source_widget = WidgetFromEventSource(event_source);
  for (const auto& name : function_names) {
    if (!HasFunction(name)) {
      continue;
    }
    std::string self_widget_name;
    if (!source_widget.empty() && HasWidgetGlobal(source_widget)) {
      self_widget_name = source_widget;
    } else {
      const auto from_fn = WidgetFromFunctionName(name);
      if (!from_fn.empty() && HasWidgetGlobal(from_fn)) {
        self_widget_name = from_fn;
      }
    }

    const auto result = RunFunctionWithThisAndArgs(name, self_widget_name, args, pass_self_arg);
    if (result.ok) {
      invocation_history_.push_back(name + "@" + event_source);
    }
    return result;
  }
  return {.ok = false, .error = "Glue Lua handler missing for event source: " + event_source};
}

LuaRunResult GlueLuaRuntime::DispatchFirstAvailableWithNumberArgs(
    const std::vector<std::string>& function_names,
    const std::string& event_source,
    const std::vector<double>& args,
    bool pass_self_arg) {
  const auto source_widget = WidgetFromEventSource(event_source);
  for (const auto& name : function_names) {
    if (!HasFunction(name)) {
      continue;
    }
    std::string self_widget_name;
    if (!source_widget.empty() && HasWidgetGlobal(source_widget)) {
      self_widget_name = source_widget;
    } else {
      const auto from_fn = WidgetFromFunctionName(name);
      if (!from_fn.empty() && HasWidgetGlobal(from_fn)) {
        self_widget_name = from_fn;
      }
    }

    const auto result = RunFunctionWithThisAndNumberArgs(name, self_widget_name, args, pass_self_arg);
    if (result.ok) {
      invocation_history_.push_back(name + "@" + event_source);
    }
    return result;
  }
  return {.ok = false, .error = "Glue Lua handler missing for event source: " + event_source};
}

std::optional<int> GlueLuaRuntime::CompileInlineScript(const std::string& event_source,
                                                      const std::string& event_name,
                                                      const std::string& script_body) {
  if (lua_state_ == nullptr) {
    return std::nullopt;
  }
  if (event_source.empty()) {
    return std::nullopt;
  }
  const auto it = inline_script_refs_by_source_.find(event_source);
  if (it != inline_script_refs_by_source_.end()) {
    return it->second;
  }

  const char* wrapper = openwow::ui::GetAnyFrameScriptWrapperFormat(event_name);
  if (wrapper == nullptr) {
    wrapper = openwow::ui::kDefaultFrameScriptWrapperFormat;
  }
  std::string chunk(wrapper);
  const auto body_marker = chunk.find("%s");
  if (body_marker == std::string::npos) {
    return std::nullopt;
  }
  chunk.replace(body_marker, 2, script_body);

  if (luaL_loadbuffer(lua_state_, chunk.c_str(), chunk.size(), event_source.c_str()) != 0) {
    const char* err = lua_tostring(lua_state_, -1);
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "Glue inline script compile failed: src=" + event_source + " err="
                           + (err ? std::string(err) : std::string("unknown")));
    lua_pop(lua_state_, 1);
    return std::nullopt;
  }
  if (lua_pcall(lua_state_, 0, 1, 0) != 0) {
    const char* err = lua_tostring(lua_state_, -1);
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "Glue inline script init failed: src=" + event_source + " err="
                           + (err ? std::string(err) : std::string("unknown")));
    lua_pop(lua_state_, 1);
    return std::nullopt;
  }
  if (lua_isfunction(lua_state_, -1) == 0) {
    lua_pop(lua_state_, 1);
    return std::nullopt;
  }

  const int ref = luaL_ref(lua_state_, LUA_REGISTRYINDEX);
  inline_script_refs_by_source_.insert_or_assign(event_source, ref);
  return ref;
}

LuaRunResult GlueLuaRuntime::RunInlineScript(const std::string& script_body,
                                            const std::string& event_name,
                                            const std::string& event_source,
                                            const std::string& self_widget_name,
                                            const std::vector<GlueLuaValue>& args) {
  if (lua_state_ == nullptr) {
    return {.ok = false, .error = "Glue Lua VM not initialized"};
  }
  const auto ref = CompileInlineScript(event_source, event_name, script_body);
  if (!ref.has_value()) {
    return {.ok = false, .error = "Glue inline script unavailable for: " + event_source};
  }

  const int top = lua_gettop(lua_state_);
  PushFrameScriptSelf(lua_state_, self_widget_name);
  const int self_index = lua_absindex(lua_state_, -1);
  lua_rawgeti(lua_state_, LUA_REGISTRYINDEX, *ref);
  for (const auto& v : args) {
    detail::PushGlueLuaValue(lua_state_, v);
  }
  const auto result = InvokePreparedGlueFrameScript(
      lua_state_, self_index, static_cast<int>(args.size()), event_name);
  lua_settop(lua_state_, top);
  if (!result.ok) {
    return result;
  }
  invocation_history_.push_back(event_source);
  return {.ok = true, .error = ""};
}

namespace {

LuaRunResult RunGlueFunctionBinding(lua_State* lua_state,
                                    const std::string& function_name,
                                    const std::string& event_name,
                                    const std::string& self_widget_name,
                                    const std::vector<GlueLuaValue>& args) {
  if (lua_state == nullptr) {
    return {.ok = false, .error = "Glue Lua VM not initialized"};
  }
  if (function_name.empty()) {
    return {.ok = true, .error = ""};
  }

  const int top = lua_gettop(lua_state);
  PushFrameScriptSelf(lua_state, self_widget_name);
  const int self_index = lua_absindex(lua_state, -1);
  lua_getglobal(lua_state, function_name.c_str());
  if (lua_isfunction(lua_state, -1) == 0) {
    lua_settop(lua_state, top);
    return {.ok = false, .error = "Glue Lua function missing: " + function_name};
  }
  for (const auto& value : args) {
    detail::PushGlueLuaValue(lua_state, value);
  }
  const auto result = InvokePreparedGlueFrameScript(
      lua_state, self_index, static_cast<int>(args.size()), event_name);
  lua_settop(lua_state, top);
  return result;
}

}

LuaRunResult GlueLuaRuntime::RunScriptRef(int script_ref,
                                         const std::string& event_name,
                                         const std::string& event_source,
                                         const std::string& self_widget_name,
                                         const std::vector<GlueLuaValue>& args) {
  if (lua_state_ == nullptr) {
    return {.ok = false, .error = "Glue Lua VM not initialized"};
  }
  if (script_ref == LUA_NOREF || script_ref == LUA_REFNIL) {
    return {.ok = true, .error = ""};
  }

  const int top = lua_gettop(lua_state_);
  PushFrameScriptSelf(lua_state_, self_widget_name);
  const int self_index = lua_absindex(lua_state_, -1);
  lua_rawgeti(lua_state_, LUA_REGISTRYINDEX, script_ref);
  for (const auto& v : args) {
    detail::PushGlueLuaValue(lua_state_, v);
  }
  const auto result = InvokePreparedGlueFrameScript(
      lua_state_, self_index, static_cast<int>(args.size()), event_name);
  lua_settop(lua_state_, top);
  if (!result.ok) {
    return result;
  }
  invocation_history_.push_back(event_source);
  return {.ok = true, .error = ""};
}

std::vector<GlueScriptBinding> GlueLuaRuntime::ResolveBindingsForWidgetEvent(
    const std::string& widget_name,
    const std::string& event_name) const {
  if (bindings_ == nullptr || widget_name.empty() || event_name.empty()) {
    return {};
  }
  if (auto direct = bindings_->BindingsFor(widget_name, event_name); !direct.empty()) {
    return direct;
  }
  if (widget_runtime_ == nullptr) {
    return {};
  }

  const auto target_widget = widget_runtime_->GetWidget(widget_name);
  const std::string target_lua_name =
      target_widget.has_value() ? std::string(target_widget->LuaName())
                                : widget_name;
  if (target_lua_name != widget_name) {
    if (auto direct = bindings_->BindingsFor(target_lua_name, event_name);
        !direct.empty()) {
      return direct;
    }
  }

  if (const auto templ = widget_runtime_->TemplateSourceName(widget_name); !templ.empty()) {
    if (auto from_template = bindings_->BindingsFor(templ, event_name); !from_template.empty()) {
      return from_template;
    }
  }

  const auto map_instance_to_template = [](const std::string& instance_root,
                                          const std::string& template_root,
                                          const std::string& name) -> std::string {
    if (instance_root.empty() || template_root.empty() || name.empty()) return {};
    if (name == instance_root) return template_root;
    if (name.rfind(instance_root, 0) != 0) return {};
    return template_root + name.substr(instance_root.size());
  };

  std::unordered_set<std::string> visited;
  std::string current = widget_name;
  for (int depth = 0; depth < 48 && !current.empty(); ++depth) {
    if (!visited.insert(current).second) {
      break;
    }
    const auto widget = widget_runtime_->GetWidget(current);
    if (!widget.has_value()) {
      break;
    }
    if (!widget->inherits.empty()) {
      std::string instance_lua_scope(widget->LuaName());
      if (instance_lua_scope.empty()) {
        instance_lua_scope = widget_runtime_->NearestLuaName(widget->parent);
      }
      if (instance_lua_scope.empty()) {
        instance_lua_scope = "Top";
      }
      for (const auto& templ_root : openwow::ui::framexml::SplitTemplateList(
               widget->inherits,
               openwow::ui::framexml::TemplateListSyntax::kCreateFrame)) {
        const auto mapped = map_instance_to_template(
            instance_lua_scope, templ_root, target_lua_name);
        if (!mapped.empty()) {
          if (auto inherited = bindings_->BindingsFor(mapped, event_name); !inherited.empty()) {
            return inherited;
          }
        }
      }
    }
    current = widget->parent;
  }
  return {};
}

bool GlueLuaRuntime::HasWidgetScript(const std::string& widget_name,
                                    const std::string& event_name) const {
  if (lua_state_ == nullptr || widget_name.empty() || event_name.empty()) {
    return false;
  }
  const std::string cache_key = widget_name + '\x1f' + event_name;
  if (const auto cached = widget_script_presence_cache_.find(cache_key);
      cached != widget_script_presence_cache_.end()) {
    return cached->second;
  }

  const int top = lua_gettop(lua_state_);
  if (detail::PushStoredWidgetTableByRuntimeKey(lua_state_, widget_name)) {
    lua_getfield(lua_state_, -1, "__ow_scripts");
    if (lua_istable(lua_state_, -1) != 0) {
      lua_getfield(lua_state_, -1, event_name.c_str());
      const bool ok = lua_isfunction(lua_state_, -1) != 0;
      lua_settop(lua_state_, top);
      if (ok) {
        widget_script_presence_cache_.insert_or_assign(cache_key, true);
        return true;
      }
    }
  }
  lua_settop(lua_state_, top);

  const bool has_binding = !ResolveBindingsForWidgetEvent(widget_name, event_name).empty();
  widget_script_presence_cache_.insert_or_assign(cache_key, has_binding);
  return has_binding;
}

bool GlueLuaRuntime::HasWidgetAnimationGroups(
    const std::string& widget_name) const {
  if (lua_state_ == nullptr || widget_name.empty()) {
    return false;
  }

  const int top = lua_gettop(lua_state_);
  const bool has_widget =
      detail::PushStoredWidgetTableByRuntimeKey(lua_state_, widget_name);
  bool has_groups = false;
  if (has_widget) {
    lua_getfield(lua_state_, -1, "__ow_anim_groups");
    has_groups = lua_istable(lua_state_, -1) != 0;
  }
  lua_settop(lua_state_, top);
  return has_groups;
}

const std::vector<GlueLuaRuntime::PerFrameWidgetEntry>&
GlueLuaRuntime::PerFrameWidgetsInUpdateOrder() {
  if (lua_state_ == nullptr || widget_runtime_ == nullptr) {
    per_frame_widget_cache_.clear();
    return per_frame_widget_cache_;
  }

  const std::uint64_t order_revision =
      widget_runtime_->visible_widget_order_revision();
  if (!per_frame_widget_cache_dirty_ &&
      per_frame_widget_order_revision_ == order_revision) {
    return per_frame_widget_cache_;
  }

  per_frame_widget_cache_.clear();
  const auto& visible_widgets =
      widget_runtime_->VisibleWidgetPointersInRenderOrder();
  per_frame_widget_cache_.reserve(visible_widgets.size());
  for (const auto* widget : visible_widgets) {
    if (widget == nullptr || widget->name.empty() || widget->virtual_template) {
      continue;
    }

    const bool has_on_update = HasWidgetScript(widget->name, "OnUpdate");
    const bool has_on_update_model =
        IsGlueModelWidgetKind(widget->kind) &&
        HasWidgetScript(widget->name, "OnUpdateModel");
    const bool has_animation_groups = HasWidgetAnimationGroups(widget->name);
    if (!has_on_update && !has_on_update_model && !has_animation_groups) {
      continue;
    }
    per_frame_widget_cache_.push_back(PerFrameWidgetEntry{
        .name = widget->name,
        .has_on_update = has_on_update,
        .has_on_update_model = has_on_update_model,
        .has_animation_groups = has_animation_groups,
    });
  }

  per_frame_widget_order_revision_ =
      widget_runtime_->visible_widget_order_revision();
  per_frame_widget_cache_dirty_ = false;
  ++per_frame_widget_cache_rebuild_count_;
  return per_frame_widget_cache_;
}

void GlueLuaRuntime::InvalidatePerFrameWidgetCache() noexcept {
  per_frame_widget_cache_dirty_ = true;
}

void GlueLuaRuntime::InvalidateWidgetScriptCache(const std::string& widget_name,
                                                 const std::string& event_name) {
  InvalidatePerFrameWidgetCache();
  if (widget_name.empty()) {
    widget_script_presence_cache_.clear();
    return;
  }
  if (event_name.empty()) {
    const std::string prefix = widget_name + '\x1f';
    for (auto it = widget_script_presence_cache_.begin();
         it != widget_script_presence_cache_.end();) {
      if (it->first.rfind(prefix, 0) == 0) {
        it = widget_script_presence_cache_.erase(it);
      } else {
        ++it;
      }
    }
    return;
  }
  widget_script_presence_cache_.erase(widget_name + '\x1f' + event_name);
}

LuaRunResult GlueLuaRuntime::RunWidgetEvent(const std::string& widget_name,
                                            const std::string& event_name,
                                            const std::string& event_source,
                                            const std::vector<GlueLuaValue>& args) {
  if (lua_state_ == nullptr) {
    return {.ok = false, .error = "Glue Lua VM not initialized"};
  }
  const std::string widget = !widget_name.empty() ? widget_name : WidgetFromEventSource(event_source);
  if (widget.empty() || event_name.empty()) {
    return {.ok = true, .error = ""};
  }
  const int top = lua_gettop(lua_state_);

  if (detail::PushStoredWidgetTableByRuntimeKey(lua_state_, widget)) {
    const int self_index = lua_absindex(lua_state_, -1);
    lua_getfield(lua_state_, -1, "__ow_scripts");
    if (lua_istable(lua_state_, -1) != 0) {
      lua_getfield(lua_state_, -1, event_name.c_str());
      if (lua_isfunction(lua_state_, -1) != 0) {

        lua_remove(lua_state_, -2);
        for (const auto& value : args) {
          detail::PushGlueLuaValue(lua_state_, value);
        }
        const auto res = InvokePreparedGlueFrameScript(
            lua_state_, self_index, static_cast<int>(args.size()), event_name);
        lua_settop(lua_state_, top);
        if (res.ok) {
          invocation_history_.push_back(event_source);
        }
        if (lua_event_trace_ != nullptr) {
          lua_event_trace_->RecordWidgetEvent(widget, event_name, event_source, args, res);
        }
        return res;
      }
    }
  }
  lua_settop(lua_state_, top);

  const auto bindings = ResolveBindingsForWidgetEvent(widget, event_name);
  if (bindings.empty()) {
    const LuaRunResult res{.ok = true, .error = ""};
    if (lua_event_trace_ != nullptr) {
      lua_event_trace_->RecordWidgetEvent(widget, event_name, event_source, args, res);
    }
    return res;
  }

  for (const auto& binding : bindings) {
    if (binding.value.empty()) {
      continue;
    }
    if (binding.kind == GlueScriptKind::kFunctionName) {
      const auto res = RunGlueFunctionBinding(lua_state_, binding.value,
                                              event_name, widget, args);
      if (!res.ok) {
        if (lua_event_trace_ != nullptr) {
          lua_event_trace_->RecordWidgetEvent(widget, event_name, event_source, args, res);
        }
        return res;
      }

      invocation_history_.push_back(event_source);
      continue;
    }
    const auto res = RunInlineScript(binding.value, event_name, event_source, widget, args);
    if (!res.ok) {
      if (lua_event_trace_ != nullptr) {
        lua_event_trace_->RecordWidgetEvent(widget, event_name, event_source, args, res);
      }
      return res;
    }
  }
  const LuaRunResult res{.ok = true, .error = ""};
  if (lua_event_trace_ != nullptr) {
    lua_event_trace_->RecordWidgetEvent(widget, event_name, event_source, args, res);
  }
  return res;
}

LuaRunResult GlueLuaRuntime::SetEditBoxTextProgrammatically(
    const std::string& widget_name,
    const std::string& text) {
  if (widget_runtime_ == nullptr) {
    return {.ok = false, .error = "Glue widget runtime not bound"};
  }
  const auto widget = widget_runtime_->GetWidget(widget_name);
  if (!widget.has_value() ||
      !openwow::text::EqualsIgnoreCaseAscii(widget->kind, "EditBox")) {
    return {.ok = false, .error = "Glue EditBox not found: " + widget_name};
  }
  if (widget_runtime_->GetText(widget_name) == text) {
    return {.ok = true, .error = ""};
  }

  widget_runtime_->SetText(widget_name, text);
  const auto text_set = RunWidgetEvent(
      widget_name, "OnTextSet", widget_name + ".OnTextSet", {});
  const auto text_changed = RunWidgetEvent(
      widget_name, "OnTextChanged", widget_name + ".OnTextChanged",
      {MakeLuaBool(false)});
  return !text_set.ok ? text_set : text_changed;
}

bool GlueLuaRuntime::SetEditBoxFocus(const std::string& widget_name) {
  if (widget_runtime_ == nullptr ||
      !widget_runtime_->CanFocusEditBox(widget_name) ||
      widget_runtime_->focused_widget() == widget_name) {
    return false;
  }

  const std::string previous = widget_runtime_->focused_widget();
  if (!previous.empty()) {

    (void)RunWidgetEvent(previous, "OnEditFocusLost",
                         previous + ".OnEditFocusLost", {});
  }
  widget_runtime_->SetFocusedWidget(widget_name);
  (void)RunWidgetEvent(widget_name, "OnEditFocusGained",
                       widget_name + ".OnEditFocusGained", {});
  return true;
}

bool GlueLuaRuntime::ClearEditBoxFocus(const std::string& widget_name) {
  if (widget_runtime_ == nullptr || widget_name.empty() ||
      widget_runtime_->focused_widget() != widget_name) {
    return false;
  }

  widget_runtime_->SetFocusedWidget({});
  (void)RunWidgetEvent(widget_name, "OnEditFocusLost",
                       widget_name + ".OnEditFocusLost", {});
  return true;
}

void GlueLuaRuntime::ClearEditBoxFocusAfterEffectiveHide(
    const std::string& widget_name) {

  (void)ClearEditBoxFocus(widget_name);
}

void GlueLuaRuntime::AutoFocusEditBoxAfterEffectiveShow(
    const std::string& widget_name) {

  if (widget_runtime_ != nullptr &&
      widget_runtime_->focused_widget().empty() &&
      widget_runtime_->IsEditAutoFocus(widget_name) &&
      widget_runtime_->CanFocusEditBox(widget_name)) {
    (void)SetEditBoxFocus(widget_name);
  }
}

void GlueLuaRuntime::SeedVisibilityCacheFromRuntime() {
  last_effective_visible_.clear();
  visibility_generations_.clear();
  if (widget_runtime_ == nullptr) {
    last_visibility_revision_ = 0;
    return;
  }
  widget_runtime_->ClearLifecycleVisibilityOverrides();
  for (const auto& name : widget_runtime_->WidgetNamesInRegistrationOrder()) {
    last_effective_visible_.insert_or_assign(
        name, widget_runtime_->IsVisible(name));
  }
  last_visibility_revision_ = widget_runtime_->visibility_revision();
}

void GlueLuaRuntime::UpdateVisibilityCacheFor(const std::string& widget_name) {
  if (widget_runtime_ == nullptr || widget_name.empty()) {
    return;
  }
  for (const auto& name : widget_runtime_->VisibilitySubtreeNames(widget_name)) {
    last_effective_visible_.insert_or_assign(name,
                                             widget_runtime_->IsVisible(name));
  }
}

std::string GlueLuaRuntime::EffectiveVisibilityParent(
    const std::string& widget_name) const {
  if (widget_runtime_ == nullptr) {
    return {};
  }
  const auto widget = widget_runtime_->GetWidget(widget_name);
  if (!widget.has_value()) {
    return {};
  }
  if (!widget->parent.empty()) {
    return widget->parent;
  }
  if (!widget->inherits.empty()) {
    const auto inherited = widget_runtime_->GetWidget(widget->inherits);
    if (inherited.has_value() && !inherited->virtual_template) {
      return widget->inherits;
    }
  }
  return {};
}

GlueLuaRuntime::VisibilitySnapshot GlueLuaRuntime::CaptureVisibilitySubtree(
    const std::string& widget_name) const {
  VisibilitySnapshot snapshot;
  if (widget_runtime_ == nullptr || widget_name.empty()) {
    return snapshot;
  }

  const auto names = widget_runtime_->VisibilitySubtreeNames(widget_name);
  if (names.empty()) {
    return snapshot;
  }

  snapshot.entries.reserve(names.size());
  snapshot.steps.reserve(names.size() * 2);
  std::unordered_map<std::string, std::size_t> indices;
  indices.reserve(names.size());
  for (const auto& name : names) {
    const auto cached = last_effective_visible_.find(name);
    const bool visible = cached != last_effective_visible_.end()
                             ? cached->second
                             : widget_runtime_->IsVisible(name);
    const auto generation = visibility_generations_.find(name);
    indices.emplace(name, snapshot.entries.size());
    snapshot.entries.push_back({
        .name = name,
        .parent_entry = std::numeric_limits<std::size_t>::max(),
        .was_visible = visible,
        .generation = generation != visibility_generations_.end()
                          ? generation->second
                          : 0,
    });
  }

  std::vector<std::vector<std::size_t>> children(snapshot.entries.size());
  for (std::size_t index = 1; index < snapshot.entries.size(); ++index) {
    const auto parent = indices.find(
        EffectiveVisibilityParent(snapshot.entries[index].name));
    if (parent == indices.end()) {
      continue;
    }
    snapshot.entries[index].parent_entry = parent->second;
    children[parent->second].push_back(index);
  }

  struct WorkItem {
    std::size_t entry{0};
    bool exiting{false};
  };
  std::vector<WorkItem> work;
  work.reserve(snapshot.entries.size() * 2);
  work.push_back({0, false});
  while (!work.empty()) {
    const WorkItem item = work.back();
    work.pop_back();
    snapshot.steps.push_back({item.entry, !item.exiting});
    if (item.exiting) {
      continue;
    }
    work.push_back({item.entry, true});
    for (auto child = children[item.entry].rbegin();
         child != children[item.entry].rend(); ++child) {
      work.push_back({*child, false});
    }
  }
  return snapshot;
}

void GlueLuaRuntime::ClearVisibilityOverrides(
    const VisibilitySnapshot& snapshot) {
  if (widget_runtime_ == nullptr) {
    return;
  }
  for (const auto& entry : snapshot.entries) {
    widget_runtime_->ClearLifecycleVisibilityOverride(entry.name);
  }
}

bool GlueLuaRuntime::DispatchVisibilitySnapshot(
    VisibilitySnapshot& snapshot, const bool force_hidden,
    const bool clear_overrides) {
  if (widget_runtime_ == nullptr || snapshot.entries.empty()) {
    return false;
  }

  for (const auto& entry : snapshot.entries) {
    widget_runtime_->SetLifecycleVisibilityOverride(entry.name,
                                                    entry.was_visible);
  }

  struct RuntimeEntry {
    std::uint64_t generation{0};
    bool traversed{false};
    bool target_visible{false};
  };
  std::vector<RuntimeEntry> runtime(snapshot.entries.size());
  bool progressed = false;

  for (const auto& step : snapshot.steps) {
    const auto& entry = snapshot.entries[step.entry_index];
    auto& current = runtime[step.entry_index];
    if (step.entering) {
      const bool parent_traversed =
          entry.parent_entry == std::numeric_limits<std::size_t>::max() ||
          runtime[entry.parent_entry].traversed;
      const auto generation = visibility_generations_.find(entry.name);
      const std::uint64_t current_generation =
          generation != visibility_generations_.end() ? generation->second : 0;
      const bool parent_matches =
          entry.parent_entry == std::numeric_limits<std::size_t>::max() ||
          EffectiveVisibilityParent(entry.name) ==
              snapshot.entries[entry.parent_entry].name;
      if (!parent_traversed || !parent_matches ||
          current_generation != entry.generation) {
        continue;
      }

      const bool visible = widget_runtime_->IsVisible(entry.name);
      const bool target =
          !force_hidden &&
          widget_runtime_->IsVisibleIgnoringLifecycleOverride(entry.name);
      if (visible == target) {
        continue;
      }

      widget_runtime_->SetLifecycleVisibilityOverride(entry.name, target);
      const std::uint64_t next_generation = current_generation + 1;
      visibility_generations_.insert_or_assign(entry.name, next_generation);
      last_effective_visible_.insert_or_assign(entry.name, target);
      current = {next_generation, true, target};
      progressed = true;
      continue;
    }

    const auto generation = visibility_generations_.find(entry.name);
    if (!current.traversed || generation == visibility_generations_.end() ||
        generation->second != current.generation ||
        widget_runtime_->IsVisible(entry.name) != current.target_visible) {
      continue;
    }
    if (widget_runtime_->IsVirtualTemplate(entry.name)) {
      continue;
    }

    const char* event_name = current.target_visible ? "OnShow" : "OnHide";
    const auto result = RunWidgetEvent(
        entry.name, event_name, entry.name + "." + event_name, {});
    if (!result.ok) {
      const std::string key =
          entry.name + "|" + event_name + "|" + result.error;
      if (logged_visibility_errors_.insert(key).second) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                           "Glue " + std::string(event_name) +
                               " failed: widget=" + entry.name +
                               " err=" + result.error);
      }
    }
    if (current.target_visible) {
      AutoFocusEditBoxAfterEffectiveShow(entry.name);
    } else {
      ClearEditBoxFocusAfterEffectiveHide(entry.name);
    }
  }

  if (clear_overrides) {
    ClearVisibilityOverrides(snapshot);
  }
  return progressed;
}

void GlueLuaRuntime::InitializeVisibilityForNewWidgets(
    const std::vector<std::string>& widget_names) {
  if (!attached_to_glue_ || lua_state_ == nullptr || widget_runtime_ == nullptr) {
    return;
  }

  for (const auto& name : widget_names) {
    if (name.empty() || widget_runtime_->IsVirtualTemplate(name)) {
      continue;
    }
    const bool visible = widget_runtime_->IsVisible(name);
    last_effective_visible_.insert_or_assign(name, visible);
    if (!visible) {
      continue;
    }
    const auto res = RunWidgetEvent(name, "OnShow", name + ".OnShow", {});
    if (!res.ok) {
      const std::string key = name + "|OnShow|" + res.error;
      if (logged_visibility_errors_.insert(key).second) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                           "Glue OnShow failed: widget=" + name + " err=" + res.error);
      }
    }
    AutoFocusEditBoxAfterEffectiveShow(name);
    last_effective_visible_.insert_or_assign(name, widget_runtime_->IsVisible(name));
  }
  last_visibility_revision_ = widget_runtime_->visibility_revision();
}

void GlueLuaRuntime::PumpVisibilityTransitions(int max_iterations) {
  if (!attached_to_glue_ || lua_state_ == nullptr || widget_runtime_ == nullptr) {
    return;
  }
  if (last_visibility_revision_ == widget_runtime_->visibility_revision()) {
    return;
  }

  max_iterations = std::clamp(max_iterations, 1, 64);
  for (int iter = 0; iter < max_iterations; ++iter) {
    std::unordered_set<std::string> changed;
    for (const auto& name : widget_runtime_->WidgetNamesInRegistrationOrder()) {
      if (name.empty() || widget_runtime_->IsVirtualTemplate(name)) {
        continue;
      }
      const bool target =
          widget_runtime_->IsVisibleIgnoringLifecycleOverride(name);
      const auto cached = last_effective_visible_.find(name);
      if (cached == last_effective_visible_.end() ||
          cached->second != target) {
        changed.insert(name);
      }
    }
    if (changed.empty()) {
      break;
    }

    bool progressed = false;
    for (const auto& name : widget_runtime_->WidgetNamesInRegistrationOrder()) {
      if (changed.find(name) == changed.end()) {
        continue;
      }
      const std::string parent = EffectiveVisibilityParent(name);
      if (!parent.empty() && changed.find(parent) != changed.end()) {
        continue;
      }
      auto snapshot = CaptureVisibilitySubtree(name);
      progressed |= DispatchVisibilitySnapshot(snapshot, false, true);
    }
    if (!progressed) {
      break;
    }
  }
  last_visibility_revision_ = widget_runtime_->visibility_revision();
}

void GlueLuaRuntime::PumpVisibilityTransitionsFor(const std::string& widget_name,
                                                  int max_iterations) {
  if (!attached_to_glue_ || lua_state_ == nullptr || widget_runtime_ == nullptr ||
      widget_name.empty()) {
    return;
  }

  max_iterations = std::clamp(max_iterations, 1, 64);
  for (int iter = 0; iter < max_iterations; ++iter) {
    auto snapshot = CaptureVisibilitySubtree(widget_name);
    const bool progressed =
        DispatchVisibilitySnapshot(snapshot, false, true);
    if (!progressed) {
      break;
    }
  }
}

bool GlueLuaRuntime::SetWidgetParentWithVisibilityLifecycle(
    const std::string& widget_name, const std::string& new_parent) {
  if (widget_runtime_ == nullptr || widget_name.empty()) {
    return false;
  }
  const auto widget = widget_runtime_->GetWidget(widget_name);
  if (!widget.has_value() || widget->parent == new_parent) {
    return false;
  }
  if (!attached_to_glue_ || lua_state_ == nullptr) {
    widget_runtime_->SetParent(widget_name, new_parent);
    return true;
  }

  UpdateVisibilityCacheFor(widget_name);
  auto old_visibility = CaptureVisibilitySubtree(widget_name);
  const bool was_visible = !old_visibility.entries.empty() &&
                           old_visibility.entries.front().was_visible;
  if (was_visible) {

    (void)DispatchVisibilitySnapshot(old_visibility, true, false);
  }

  widget_runtime_->SetParent(widget_name, new_parent);
  if (was_visible) {
    auto new_visibility = CaptureVisibilitySubtree(widget_name);
    (void)DispatchVisibilitySnapshot(new_visibility, false, true);
    ClearVisibilityOverrides(old_visibility);
  } else {
    (void)DispatchVisibilitySnapshot(old_visibility, false, true);
  }
  return true;
}

void GlueLuaRuntime::ApplyFontStringTextKeys() {
  if (lua_state_ == nullptr || widget_runtime_ == nullptr) {
    return;
  }

  int updated = 0;
  for (const auto& name : widget_runtime_->WidgetNames()) {
    const auto widget = widget_runtime_->GetWidget(name);
    if (!widget.has_value()) {
      continue;
    }
    if (widget->text.empty()) {
      continue;
    }

    lua_getglobal(lua_state_, widget->text.c_str());
    if (lua_isstring(lua_state_, -1) != 0) {
      const char* resolved = lua_tostring(lua_state_, -1);
      if (resolved != nullptr && std::string(resolved) != widget->text) {
        widget_runtime_->SetText(name, std::string(resolved));
        ++updated;
      }
    }
    lua_pop(lua_state_, 1);
  }

  if (updated > 0) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kDebug,
                       "Applied glue text keys: updated=" + std::to_string(updated));
  }
}

void GlueLuaRuntime::RegisterEvent(const std::string& widget_name, const std::string& event_name) {
  const char* const canonical_event =
      FindGlueFrameScriptEventName(event_name);
  if (widget_name.empty() || canonical_event == nullptr) {
    return;
  }
  auto& widgets = widgets_by_event_[canonical_event];
  openwow::ui::frame_event_registration::AddUnique(widgets, widget_name);
  auto& events = events_by_widget_[widget_name];
  openwow::ui::frame_event_registration::AddUnique(
      events, std::string(canonical_event));
}

void GlueLuaRuntime::UnregisterEvent(const std::string& widget_name, const std::string& event_name) {
  const char* const canonical_event =
      FindGlueFrameScriptEventName(event_name);
  if (widget_name.empty() || canonical_event == nullptr) {
    return;
  }
  if (auto it = widgets_by_event_.find(canonical_event); it != widgets_by_event_.end()) {
    auto& widgets = it->second;
    openwow::ui::frame_event_registration::Remove(widgets, widget_name);
    if (widgets.empty()) {
      widgets_by_event_.erase(it);
    }
  }
  if (auto it = events_by_widget_.find(widget_name); it != events_by_widget_.end()) {
    auto& events = it->second;
    openwow::ui::frame_event_registration::Remove(events, std::string(canonical_event));
    if (events.empty()) {
      events_by_widget_.erase(it);
    }
  }
}

void GlueLuaRuntime::UnregisterAllEvents(const std::string& widget_name) {
  if (widget_name.empty()) {
    return;
  }
  const auto it = events_by_widget_.find(widget_name);
  if (it == events_by_widget_.end()) {
    return;
  }
  const auto events = it->second;
  for (const auto& event : events) {
    UnregisterEvent(widget_name, event);
  }
}

void GlueLuaRuntime::RegisterAllEvents(const std::string& widget_name) {
  if (widget_name.empty()) {
    return;
  }

  for (const char* event_name : kGlueEventNames) {
    if (event_name != nullptr) {
      RegisterEvent(widget_name, event_name);
    }
  }
}

bool GlueLuaRuntime::IsEventRegistered(const std::string& widget_name,
                                       const std::string& event_name) const {
  const char* const canonical_event =
      FindGlueFrameScriptEventName(event_name);
  if (canonical_event == nullptr) {
    return false;
  }
  const auto it = events_by_widget_.find(widget_name);
  if (it == events_by_widget_.end()) return false;
  const auto& events = it->second;
  return openwow::ui::frame_event_registration::Contains(
      events, std::string(canonical_event));
}

std::vector<std::string> GlueLuaRuntime::RegisteredWidgetsForEvent(const std::string& event_name) const {
  const char* const canonical_event =
      FindGlueFrameScriptEventName(event_name);
  if (canonical_event == nullptr) {
    return {};
  }
  const auto it = widgets_by_event_.find(canonical_event);
  if (it == widgets_by_event_.end()) {
    return {};
  }
  return it->second;
}

LuaRunResult GlueLuaRuntime::DispatchRegisteredEvent(
    const std::string& event_name,
    const std::vector<GlueLuaValue>& extra_args) {
  if (event_name.empty()) {
    return {.ok = true, .error = {}};
  }

  const auto targets = RegisteredWidgetsForEvent(event_name);
  if (targets.empty()) {
    return {.ok = true, .error = {}};
  }

  std::vector<GlueLuaValue> call_args;
  call_args.reserve(extra_args.size() + 1);
  call_args.push_back(MakeLuaString(event_name));
  call_args.insert(call_args.end(), extra_args.begin(), extra_args.end());

  LuaRunResult result{.ok = true, .error = {}};
  for (const auto& widget : targets) {
    result = RunWidgetEvent(widget, "OnEvent", widget + ".OnEvent", call_args);
    if (!result.ok) {
      break;
    }
  }

  return result;
}

std::function<void()> GlueLuaRuntime::MakeAsyncRegisteredEventPoster(
    std::string event_name,
    std::vector<GlueLuaValue> extra_args) const {
  std::weak_ptr<AsyncEventQueue> weak_queue = async_event_queue_;
  return [weak_queue, event_name = std::move(event_name),
          extra_args = std::move(extra_args)]() mutable {
    const auto queue = weak_queue.lock();
    if (!queue || event_name.empty()) {
      return;
    }

    std::lock_guard<std::mutex> lock(queue->mutex);
    if (!queue->accept_events) {
      return;
    }
    PostedEvent posted_event;
    posted_event.event_name = event_name;
    posted_event.extra_args = extra_args;
    queue->pending_events.push_back(std::move(posted_event));
  };
}

void GlueLuaRuntime::DrainPostedEvents() {
  if (!attached_to_glue_ || !async_event_queue_) {
    return;
  }

  std::vector<PostedEvent> pending_events;
  {
    std::lock_guard<std::mutex> lock(async_event_queue_->mutex);
    if (async_event_queue_->pending_events.empty()) {
      return;
    }
    pending_events.swap(async_event_queue_->pending_events);
  }

  for (const auto& posted_event : pending_events) {
    (void)DispatchRegisteredEvent(posted_event.event_name,
                                  posted_event.extra_args);
  }
}

bool GlueLuaRuntime::HasWidgetGlobal(const std::string& widget_name) const {
  if (lua_state_ == nullptr || widget_name.empty()) {
    return false;
  }
  lua_getglobal(lua_state_, widget_name.c_str());
  const bool ok = lua_istable(lua_state_, -1) != 0;
  lua_pop(lua_state_, 1);
  return ok;
}

std::string GlueLuaRuntime::WidgetFromEventSource(const std::string& event_source) {
  if (event_source.empty()) {
    return {};
  }
  const auto at = event_source.find('.');
  if (at == std::string::npos) {
    return event_source;
  }
  return event_source.substr(0, at);
}

std::string GlueLuaRuntime::WidgetFromFunctionName(const std::string& function_name) {
  if (function_name.empty()) {
    return {};
  }
  const auto at = function_name.find('_');
  if (at == std::string::npos || at == 0) {
    return {};
  }
  return function_name.substr(0, at);
}

bool GlueLuaRuntime::IsMoviePlaying() const {
  return movie_player_.IsPlaying();
}

bool GlueLuaRuntime::StartMovie(const std::string& avi_path, int volume) {
  if (vfs_ == nullptr) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GlueLuaRuntime::StartMovie: VFS not available");
    return false;
  }

  if (host_ != nullptr) {
    host_->PrepareMoviePlayback();
  }

  sound_runtime_.StopMovieAudio();

  if (!movie_player_.Start(avi_path, volume, vfs_, data_dir_,
                           sound_runtime_.GetOutputSampleRate(),
                           sound_runtime_.GetOutputChannelCount())) {
    return false;
  }

  if (auto source = movie_player_.AudioSource(); source != nullptr) {
    auto& audio = sound_runtime_;
    audio.PlayMovieAudio(std::move(source), movie_player_.VolumeNormalized());
    if (audio.IsMovieAudioPlaying()) {
      const auto stats = movie_player_.AudioStats();
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kInfo,
          "Movie audio stream bound: capacity_samples=" +
              std::to_string(stats.has_value() ? stats->capacity_samples : 0) +
              " rate=" + std::to_string(movie_player_.AudioSampleRate()) +
              " channels=" + std::to_string(movie_player_.AudioChannels()));
    } else {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "Movie decoded PCM but the audio output was unavailable");
    }
  }

  return true;
}

void GlueLuaRuntime::StopMovie() {
  if (!movie_player_.IsPlaying()) return;

  openwow::media::ReleaseMoviePlayback(
      [this] { sound_runtime_.StopMovieAudio(); },
      [this] { movie_player_.Stop(); });

  UpdateMovie(0.0);
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "StopMovie");
}

void GlueLuaRuntime::DispatchActiveMovieKeyUp(const std::string& key_name) {
  if (!movie_player_.IsPlaying() || movie_widget_name_.empty() || key_name.empty()) {
    return;
  }
  const std::string active_widget = movie_widget_name_;
  (void)RunWidgetEvent(active_widget, "OnKeyUp",
                       active_widget + ".OnKeyUp",
                       {MakeLuaString(key_name)});
}

bool GlueLuaRuntime::BeginWidgetSizing(const std::string& widget_name, const int move_sizing_mode) {
  if (widget_runtime_ == nullptr || widget_name.empty() || active_move_sizing_.active) {
    return false;
  }

  widget_runtime_->ResolveLayout(widget_runtime_->viewport_width(), widget_runtime_->viewport_height());
  const auto widget = widget_runtime_->GetWidget(widget_name);
  if (!widget.has_value() || widget->virtual_template || !widget->resizable) {
    return false;
  }

  active_move_sizing_ = {};
  active_move_sizing_.widget_name = widget_name;
  active_move_sizing_.relative_name = ResolveGlueMoveSizingRelativeName(*widget_runtime_, *widget);
  active_move_sizing_.mode = move_sizing_mode;
  if (const auto cursor = widget_runtime_->cached_cursor_position(); cursor.has_value()) {
    active_move_sizing_.cursor_x = static_cast<float>(cursor->first);
    active_move_sizing_.cursor_y = static_cast<float>(cursor->second);
  } else {
    const float left = static_cast<float>(widget->x);
    const float top = static_cast<float>(widget->y);
    const float right = left + static_cast<float>(widget->width);
    const float bottom = top + static_cast<float>(widget->height);
    switch (move_sizing_mode) {
      case 0:
        active_move_sizing_.cursor_x = left;
        active_move_sizing_.cursor_y = top;
        break;
      case 1:
        active_move_sizing_.cursor_x = (left + right) * 0.5f;
        active_move_sizing_.cursor_y = top;
        break;
      case 2:
        active_move_sizing_.cursor_x = right;
        active_move_sizing_.cursor_y = top;
        break;
      case 3:
        active_move_sizing_.cursor_x = left;
        active_move_sizing_.cursor_y = (top + bottom) * 0.5f;
        break;
      case 4:
        active_move_sizing_.cursor_x = (left + right) * 0.5f;
        active_move_sizing_.cursor_y = (top + bottom) * 0.5f;
        break;
      case 5:
        active_move_sizing_.cursor_x = right;
        active_move_sizing_.cursor_y = (top + bottom) * 0.5f;
        break;
      case 6:
        active_move_sizing_.cursor_x = left;
        active_move_sizing_.cursor_y = bottom;
        break;
      case 7:
        active_move_sizing_.cursor_x = (left + right) * 0.5f;
        active_move_sizing_.cursor_y = bottom;
        break;
      case 8:
      default:
        active_move_sizing_.cursor_x = right;
        active_move_sizing_.cursor_y = bottom;
        break;
    }
  }
  active_move_sizing_.active = true;

  widget_runtime_->Raise(widget_name);
  widget_runtime_->SetUserPlaced(widget_name, true);
  return true;
}

bool GlueLuaRuntime::StopWidgetMoveSizing(const std::string& widget_name) {
  if (widget_runtime_ == nullptr || widget_name.empty() || !active_move_sizing_.active ||
      active_move_sizing_.widget_name != widget_name) {
    return false;
  }

  widget_runtime_->ResolveLayout(widget_runtime_->viewport_width(), widget_runtime_->viewport_height());
  const auto widget = widget_runtime_->GetWidget(widget_name);
  if (!widget.has_value()) {
    active_move_sizing_ = {};
    return false;
  }

  const auto drag_rect = openwow::ui::framexml::FrameRect{
      .x = widget->x,
      .y = widget->y,
      .width = widget->width,
      .height = widget->height,
  };
  const auto relative_rect =
      ResolveGlueMoveSizingReferenceRect(*widget_runtime_, active_move_sizing_.relative_name);
  const auto placement =
      openwow::ui::game::runtime::ComputeNearestMatchingFramePointPlacement(
          drag_rect, relative_rect);
  const float effective_scale =
      ComputeGlueMoveSizingEffectiveScale(*widget_runtime_, widget_name);
  const std::string point_name(
      openwow::ui::game::runtime::LayoutCacheFramePointName(
          placement.point_index));
  const std::string resolved_relative_name =
      active_move_sizing_.relative_name.empty() ? std::string("UIParent")
                                                : active_move_sizing_.relative_name;

  widget_runtime_->ClearAllPoints(widget_name);
  widget_runtime_->SetPoint(widget_name, point_name, resolved_relative_name, point_name,
                            placement.pixel_offset_x / effective_scale,
                            placement.pixel_offset_y / effective_scale);
  widget_runtime_->SetSize(widget_name, static_cast<float>(drag_rect.width) / effective_scale,
                           static_cast<float>(drag_rect.height) / effective_scale);
  widget_runtime_->ResolveLayout(widget_runtime_->viewport_width(), widget_runtime_->viewport_height());
  active_move_sizing_ = {};
  return true;
}

bool GlueLuaRuntime::IsWidgetMoveSizingActive(const std::string& widget_name) const {
  return active_move_sizing_.active && active_move_sizing_.widget_name == widget_name;
}

void GlueLuaRuntime::UpdateMovie(double elapsed_seconds) {
  if (!attached_to_glue_) {
    return;
  }

  auto& audio = sound_runtime_;

  if (movie_player_.IsPlaying()) {
    std::optional<double> clock_override;
    if (audio.IsMovieAudioPlaying()) {
      clock_override = audio.MovieAudioTimeSeconds();
    }
    movie_player_.Update(elapsed_seconds, clock_override);
  }

  const auto events = movie_player_.ConsumeEvents();
  if (events.empty()) return;

  const bool allow_subtitles = MovieSubtitlesEnabled(lua_state_, movie_widget_name_);
  for (const auto& event : events) {
    switch (event.type) {
      case openwow::media::MovieEvent::kShowSubtitle:
        if (allow_subtitles && !movie_widget_name_.empty()) {
          RunWidgetEvent(movie_widget_name_, "OnMovieShowSubtitle",
                         movie_widget_name_ + ".OnMovieShowSubtitle",
                         {MakeLuaString(event.text)});
        }
        break;
      case openwow::media::MovieEvent::kHideSubtitle:
        if (allow_subtitles && !movie_widget_name_.empty()) {
          RunWidgetEvent(movie_widget_name_, "OnMovieHideSubtitle",
                         movie_widget_name_ + ".OnMovieHideSubtitle", {});
        }
        break;
      case openwow::media::MovieEvent::kFinished: {

        const std::string finished_widget = std::exchange(movie_widget_name_, {});
        openwow::media::ReleaseMoviePlayback(
            [&audio] { audio.StopMovieAudio(); },
            [this] { movie_player_.Stop(); });
        if (!finished_widget.empty()) {
          RunWidgetEvent(finished_widget, "OnMovieFinished",
                         finished_widget + ".OnMovieFinished", {});
        }
        break;
      }
    }
  }
}

void GlueLuaRuntime::PumpFrameServices() {
  if (!attached_to_glue_) {
    return;
  }

  (void)ServerAlertService::Get().PumpPendingAlert(
      [this](const std::string& alert_text) {
        (void)DispatchRegisteredEvent(
            GlueEventName(GlueScriptEvent::ShowServerAlert),
            {MakeLuaString(alert_text)});
      });
  (void)openwow::game::AccountMsg::Get().Pump();
  (void)openwow::game::KnowledgeBase::Get().Pump();

  DrainPostedEvents();

  for (const auto& result : openwow::core::ScreenshotSystem::Instance()
                                .DrainCompletedRequestsForDomain(
                                    openwow::core::ScreenshotRequestDomain::GlueUi)) {
    const auto event = result.succeeded
                           ? GlueEventName(GlueScriptEvent::GlueScreenshotSucceeded)
                           : GlueEventName(GlueScriptEvent::GlueScreenshotFailed);
    (void)DispatchRegisteredEvent(event, {});
  }
}

void GlueLuaRuntime::AdvanceFrame(double elapsed_seconds) {
  if (!attached_to_glue_) {
    return;
  }

  PumpFrameServices();

  if (widget_runtime_ == nullptr) {
    return;
  }

  widget_runtime_->ResolveLayout(widget_runtime_->viewport_width(),
                                 widget_runtime_->viewport_height());

  GlueLuaValue elapsed_arg;
  elapsed_arg.kind = GlueLuaValue::Kind::kNumber;
  elapsed_arg.number_value = elapsed_seconds;
  const std::vector<GlueLuaValue> elapsed_args{elapsed_arg};
  const std::vector<GlueLuaValue> no_args;

  const auto& per_frame_widgets = PerFrameWidgetsInUpdateOrder();
  for (const auto& widget : per_frame_widgets) {
    if (!widget_runtime_->IsVisible(widget.name)) {
      continue;
    }

    const auto dispatch_per_frame_script = [&](const char* event_name,
                                               const std::vector<GlueLuaValue>& args) {
      if (!widget_runtime_->IsVisible(widget.name)) {
        return;
      }
      const auto res = RunWidgetEvent(widget.name, event_name, widget.name + "." + event_name,
                                      args);
      if (!res.ok) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                           "GlueLuaRuntime::AdvanceFrame " + std::string(event_name) +
                               " failed: widget=" + widget.name + " err=" + res.error);
      }
    };

    if (widget.has_on_update) {
      dispatch_per_frame_script("OnUpdate", elapsed_args);
    }
    if (widget.has_on_update_model) {

      dispatch_per_frame_script("OnUpdateModel", no_args);
    }
  }

  PumpCursorDrivenUpdates();
}

void GlueLuaRuntime::PumpCursorDrivenUpdates() {
  if (!attached_to_glue_ || widget_runtime_ == nullptr) {
    return;
  }

  bool cursor_motion = false;
  if (widget_runtime_->ConsumeDeferredHitTestRefresh(&cursor_motion)) {
    RefreshHoveredWidgetFromCachedCursor(cursor_motion);
  }
}

void GlueLuaRuntime::ClearHoveredWidget() {
  if (hovered_widget_.empty()) {
    return;
  }
  if (widget_runtime_ != nullptr) {
    widget_runtime_->SetHovered(hovered_widget_, false);
  }
  hovered_widget_.clear();
}

bool GlueLuaRuntime::ApplyActiveMoveSizingFromCachedCursor() {
  if (widget_runtime_ == nullptr || !active_move_sizing_.active) {
    return false;
  }

  const auto widget = widget_runtime_->GetWidget(active_move_sizing_.widget_name);
  const auto cursor = widget_runtime_->cached_cursor_position();
  if (!widget.has_value() || !cursor.has_value()) {
    active_move_sizing_ = {};
    return false;
  }

  const float cursor_x = static_cast<float>(cursor->first);
  const float cursor_y = static_cast<float>(cursor->second);
  const float dx = cursor_x - active_move_sizing_.cursor_x;
  const float dy = cursor_y - active_move_sizing_.cursor_y;
  active_move_sizing_.cursor_x = cursor_x;
  active_move_sizing_.cursor_y = cursor_y;

  if (dx == 0.0f && dy == 0.0f) {
    return true;
  }

  float left = static_cast<float>(widget->x);
  float top = static_cast<float>(widget->y);
  float right = left + static_cast<float>(widget->width);
  float bottom = top + static_cast<float>(widget->height);

  switch (active_move_sizing_.mode) {
    case 0:
      left += dx;
      top += dy;
      break;
    case 1:
      top += dy;
      break;
    case 2:
      right += dx;
      top += dy;
      break;
    case 3:
      left += dx;
      break;
    case 4:
      left += dx;
      right += dx;
      top += dy;
      bottom += dy;
      break;
    case 5:
      right += dx;
      break;
    case 6:
      left += dx;
      bottom += dy;
      break;
    case 7:
      bottom += dy;
      break;
    case 8:
    default:
      right += dx;
      bottom += dy;
      break;
  }

  if (widget->clamped_to_screen) {
    openwow::ui::RectEdgesYDown clamped_rect{
        .left = left,
        .top = top,
        .right = right,
        .bottom = bottom,
    };
    openwow::ui::ClampRectEdgesYDownPreservingSpan(
        &clamped_rect,
        openwow::ui::RectBoundsYDown{
            .min_left = 0.0f,
            .min_top = 0.0f,
            .max_right = static_cast<float>(widget_runtime_->viewport_width()),
            .max_bottom = static_cast<float>(widget_runtime_->viewport_height()),
        });
    left = clamped_rect.left;
    top = clamped_rect.top;
    right = clamped_rect.right;
    bottom = clamped_rect.bottom;
  }

  const auto drag_rect = openwow::ui::framexml::FrameRect{
      .x = static_cast<int>(std::lround(left)),
      .y = static_cast<int>(std::lround(top)),
      .width = static_cast<int>(std::lround(right - left)),
      .height = static_cast<int>(std::lround(bottom - top)),
  };
  const auto relative_rect =
      ResolveGlueMoveSizingReferenceRect(*widget_runtime_, active_move_sizing_.relative_name);
  const float effective_scale =
      ComputeGlueMoveSizingEffectiveScale(*widget_runtime_, active_move_sizing_.widget_name);
  ApplyGlueLiveMoveSizingRectToRuntime(*widget_runtime_, active_move_sizing_.widget_name,
                                       active_move_sizing_.relative_name, relative_rect, drag_rect,
                                       effective_scale);
  widget_runtime_->ResolveLayout(widget_runtime_->viewport_width(), widget_runtime_->viewport_height());
  return true;
}

void GlueLuaRuntime::RefreshHoveredWidgetFromCachedCursor(const bool motion) {
  if (widget_runtime_ == nullptr) {
    return;
  }
  if (ApplyActiveMoveSizingFromCachedCursor()) {
    return;
  }
  const auto cursor = widget_runtime_->cached_cursor_position();
  if (!cursor.has_value()) {
    return;
  }

  const auto hovered =
      widget_runtime_->HitTestMouseTarget(cursor->first, cursor->second);
  const std::string next_widget = hovered.has_value() ? hovered->name : std::string();
  if (next_widget == hovered_widget_) {
    return;
  }

  if (!hovered_widget_.empty()) {
    widget_runtime_->SetHovered(hovered_widget_, false);
    const auto res = RunWidgetEvent(hovered_widget_,
                                    "OnLeave",
                                    hovered_widget_ + ".OnLeave",
                                    {MakeLuaBool(motion)});
    if (!res.ok) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "GlueLuaRuntime::RefreshHoveredWidgetFromCachedCursor OnLeave failed: widget="
                             + hovered_widget_ + " err=" + res.error);
    }
  }

  hovered_widget_ = next_widget;

  if (!hovered_widget_.empty()) {
    widget_runtime_->SetHovered(hovered_widget_, true);
    const auto res = RunWidgetEvent(hovered_widget_,
                                    "OnEnter",
                                    hovered_widget_ + ".OnEnter",
                                    {MakeLuaBool(motion)});
    if (!res.ok) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                         "GlueLuaRuntime::RefreshHoveredWidgetFromCachedCursor OnEnter failed: widget="
                             + hovered_widget_ + " err=" + res.error);
    }
  }
}

const std::vector<std::string>& GlueLuaRuntime::invocation_history() const {
  return invocation_history_;
}

}

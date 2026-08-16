#include "openwow/ui/game/runtime/world_lua_runtime.h"
#include "openwow/ui/game/runtime/world_ui_runtime_context.h"

#include "openwow/game/aura_lua_bridge.h"
#include "openwow/game/simple_script.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_missile.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/addons_data.h"
#include "openwow/ui/frame_script_events.h"
#include "openwow/ui/game/api/game_lua_api_addon.h"
#include "openwow/ui/game/api/game_lua_api_cast.h"
#include "openwow/ui/game/api/game_lua_api_binding.h"
#include "openwow/ui/game/api/game_lua_api_cvar.h"
#include "openwow/ui/game/api/game_lua_api_faction.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/api/game_lua_api_misc_ui.h"
#include "openwow/ui/game/api/game_lua_api_sound.h"
#include "openwow/ui/game/api/game_lua_api_system.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/lua_addon_memory_tracker.h"
#include "openwow/ui/game/runtime/framexml_runtime_loader.h"
#include "openwow/ui/game/runtime/lua/held_cursor_lua_binding.h"
#include "openwow/ui/game/saved_variables.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/secure_execution.h"
#include "openwow/ui/game/slash_command_handler.h"
#include "openwow/ui/production_lua_surface.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/runtime/lua/frame_script_runtime.h"
#include "openwow/ui/runtime/lua/lua_composition.h"
#include "openwow/vfs/virtual_file_system.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/runtime/time/game_clock.h"

#include <atomic>
#include <utility>

extern "C" {
#include <lua.hpp>
}

namespace openwow::ui::game::runtime {
namespace {

constexpr const char* kFrameMaterializerRegistryKey =
    "openwow.frame_materializer";
constexpr const char* kFrameInputRouterRegistryKey = "openwow.frame_input_router";

constexpr int kLuaWatchdogInstructionBudget = 100000000;
constexpr std::uint32_t kLuaWatchdogMaxReports = 16u;
constexpr std::uint32_t kLuaWatchdogMinIntervalMs = 5000u;

std::atomic<std::uint64_t> g_lua_watchdog_frame{0};

void LuaRunawayWatchdogHook(lua_State* state, lua_Debug* ) {
  static std::uint32_t reports = 0u;
  static std::uint32_t last_report_ms = 0u;
  static std::uint64_t last_crossing_frame = ~std::uint64_t{0};
  const std::uint64_t current_frame =
      g_lua_watchdog_frame.load(std::memory_order_relaxed);
  const bool same_frame = current_frame == last_crossing_frame;
  last_crossing_frame = current_frame;
  if (!same_frame) {
    return;
  }
  if (reports >= kLuaWatchdogMaxReports) {
    return;
  }
  const std::uint32_t now_ms = openwow::core::GameClock::GetTickCount32();
  if (last_report_ms != 0u && now_ms - last_report_ms < kLuaWatchdogMinIntervalMs) {
    return;
  }
  last_report_ms = now_ms;
  ++reports;
  std::string trace = "LuaWatchdog: 100M-instruction budget crossed;";
  lua_Debug frame{};
  for (int level = 0; level < 12 && lua_getstack(state, level, &frame) != 0;
       ++level) {
    if (lua_getinfo(state, "Sln", &frame) == 0) {
      break;
    }
    trace += " #" + std::to_string(level) + " " + frame.short_src + ":" +
             std::to_string(frame.currentline);
    if (frame.name != nullptr) {
      trace += "(" + std::string(frame.name) + ")";
    }
  }
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, trace);
}

void InstallLuaRunawayWatchdog(lua_State* state) {
  lua_sethook(state, LuaRunawayWatchdogHook, LUA_MASKCOUNT,
              kLuaWatchdogInstructionBudget);
}

}

void LuaWatchdogNoteFrame() {
  g_lua_watchdog_frame.fetch_add(1u, std::memory_order_relaxed);
}

namespace {

template <typename T>
T* RegistryPort(lua_State* state, const char* key) {
  lua_getfield(state, LUA_REGISTRYINDEX, key);
  auto* port = static_cast<T*>(lua_touserdata(state, -1));
  lua_pop(state, 1);
  return port;
}

int LuaCreateFrameBinding(lua_State* state) {
  auto* materializer = RegistryPort<FrameMaterializer>(
      state, kFrameMaterializerRegistryKey);
  if (materializer == nullptr) {
    lua_pushnil(state);
    return 1;
  }
  return materializer->CreateFrame(state);
}

}

thread_local const WorldLuaRuntime::TypedContexts*
    WorldLuaRuntime::creating_typed_contexts_ = nullptr;

WorldLuaRuntime::WorldLuaRuntime(GameUIManager& owner) noexcept : owner_(owner) {
  typed_contexts_.cvars = &CVarSystem::Instance();
  typed_contexts_.security = &SecureExecution::Get();
  typed_contexts_.events = &ScriptEventDispatch::Get();
}

WorldLuaRuntime::~WorldLuaRuntime() {
  if (lua_ != nullptr || frame_script_runtime_ != nullptr) {
    Destroy();
  }
}

bool WorldLuaRuntime::Create(openwow::game::WorldSession* session,
                             openwow::game::CursorSurface& cursor) {
  typed_contexts_.security->Reset();
  auto world_surface_adapters = openwow::ui::CreateProductionWorldLuaAdapters();
  auto bindings = openwow::ui::CreateProductionWorldLuaBindings(
      *owner_.display_settings_runtime_, world_surface_adapters.item,
      world_surface_adapters.loot, world_surface_adapters.auction,
      world_surface_adapters.mail, world_surface_adapters.merchant,
      world_surface_adapters.trade);
  typed_contexts_.addons = &openwow::ui::AddOnsData::Get();
  typed_contexts_.runtime_context = owner_.runtime_context_.get();
  typed_contexts_.binding_profiles =
      session != nullptr ? session->binding_profiles() : nullptr;
  typed_contexts_.session = session;
  typed_contexts_.spell_cast_runtime =
      session != nullptr ? &session->spells() : nullptr;
  typed_contexts_.spellbook =
      session != nullptr ? &openwow::game::SpellbookSystem::Get() : nullptr;
  sound_lua_context_ =
      std::make_unique<detail::SoundLuaContext>(
          owner_.sound_runtime_, session, &owner_.movie_recording_runtime_);
  typed_contexts_.sound = sound_lua_context_.get();
  frame_script_runtime_ =
      std::make_unique<openwow::ui::lua::FrameScriptRuntime>();
  const auto* previous_contexts = creating_typed_contexts_;
  creating_typed_contexts_ = &typed_contexts_;
  const bool booted = frame_script_runtime_->BootWorld(
      std::move(bindings), InitializeLuaVm);
  creating_typed_contexts_ = previous_contexts;
  if (!booted) {
    frame_script_runtime_.reset();
    sound_lua_context_.reset();
    typed_contexts_.sound = nullptr;
    return false;
  }
  lua_ = frame_script_runtime_->state();
  owner_.frame_store_.BindLuaState(lua_);
  owner_.retained_layout_.BindLuaState(lua_);
  owner_.frame_input_router_.BindLuaState(lua_);
  owner_.frame_materializer_->BindLuaState(lua_);
  owner_.nameplate_frames_.BindLuaState(lua_);

  openwow::game::simple_script::BindFrameScriptLuaState(lua_);
  openwow::ui::frame_script_events::FrameScript_SetLuaStateTyped(lua_);
  InstallLuaAddonMemoryTracker(lua_);
  RegisterSavedVariableName(SavedVariableRegistrationScope::kAccount,
                            "AuctionHouseFrameAuctionTabs");
  RegisterSavedVariableName(SavedVariableRegistrationScope::kAccount,
                            "AuctionHouseFrameBrowseTabs");
  RegisterSavedVariableName(SavedVariableRegistrationScope::kAccount,
                            "AuctionPrices");

  lua_pushlightuserdata(lua_, owner_.runtime_context_.get());
  lua_setfield(lua_, LUA_REGISTRYINDEX, kWorldUiRuntimeContextRegistryKey);
  lua_pushlightuserdata(lua_, owner_.frame_materializer_.get());
  lua_setfield(lua_, LUA_REGISTRYINDEX, kFrameMaterializerRegistryKey);
  lua_pushlightuserdata(lua_, &owner_.frame_input_router_);
  lua_setfield(lua_, LUA_REGISTRYINDEX, kFrameInputRouterRegistryKey);

  if (owner_.held_cursor_ != nullptr) {
    openwow::ui::game::lua::BindHeldCursor(*lua_, *owner_.held_cursor_);
  }
  if (owner_.vfs_ != nullptr) {
    lua_pushlightuserdata(
        lua_, const_cast<openwow::vfs::VirtualFileSystem*>(owner_.vfs_));
    lua_setfield(lua_, LUA_REGISTRYINDEX, detail::kTextureVfsRegistryKey);
  }
  if (session != nullptr) {
    lua_pushlightuserdata(lua_, session);
    lua_setfield(lua_, LUA_REGISTRYINDEX, detail::kWorldSessionRegistryKey);
    detail::BindInventoryCommerceLuaContext(lua_, session);
    openwow::ui::BindProductionWorldLuaAdapters(
        world_surface_adapters, *session, cursor,
        owner_.frame_event_runtime_.dispatcher(), owner_.mail_stationery_choices_);
    if (const auto* dbc = session->GetDbcLoader(); dbc != nullptr) {
      lua_pushlightuserdata(lua_, const_cast<openwow::data::dbc::DbcLoader*>(dbc));
      lua_setfield(lua_, LUA_REGISTRYINDEX, "openwow.dbc_loader");
    }
  }

  InitializeSlashCommandHandlers(lua_, session);
  InstallLuaRunawayWatchdog(lua_);
  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kInfo,
      "GameUIManager: Lua state created with game API");
  return true;
}

bool WorldLuaRuntime::InitializeLuaVm(openwow::ui::lua::LuaVm& vm) {
  if (creating_typed_contexts_ != nullptr) {
    BindTypedContexts(*vm.state(), *creating_typed_contexts_);
  }
  if (!openwow::ui::InitializeProductionWorldLuaVm(vm)) {
    return false;
  }
  return true;
}

void WorldLuaRuntime::BindTypedContexts(lua_State& state,
                                        const TypedContexts& contexts) {
  detail::BindAddonLuaContext(state, *contexts.addons,
                              *contexts.runtime_context);
  detail::BindCastLuaContext(
      state, contexts.session,
      contexts.spell_cast_runtime != nullptr
          ? &contexts.spell_cast_runtime->GetTargeting()
          : nullptr,
      contexts.spellbook);
  detail::BindKeyBindingLuaContext(state, contexts.binding_profiles);
  detail::BindCVarLuaContext(state, contexts.cvars, contexts.security,
                             contexts.events);
  detail::BindSoundLuaContext(state, *contexts.sound);
}

void WorldLuaRuntime::CompleteFrameXmlLoad() {
  simple_script_ = openwow::game::simple_script::SimpleScript_Create();
  openwow::game::SpellMissileMotionRegistry::Get().BindScript(simple_script_);
}

void WorldLuaRuntime::Destroy() {
  ClearSavedVariableRegistrations();
  if (lua_ == nullptr) {
    owner_.frame_input_router_.Reset();
    owner_.frame_store_.ClearAfterLuaDestroyed();
    owner_.movie_frame_runtime_.Shutdown();
    owner_.frame_event_runtime_.Shutdown();
    openwow::game::simple_script::BindFrameScriptLuaState(nullptr);
    openwow::ui::frame_script_events::FrameScript_SetLuaStateTyped(nullptr);
    frame_script_runtime_.reset();
    sound_lua_context_.reset();
    typed_contexts_.sound = nullptr;
    typed_contexts_.security->Reset();
    return;
  }

  openwow::game::SpellMissileMotionRegistry::Get().BindScript(nullptr);
  openwow::game::simple_script::SimpleScript_Destroy(simple_script_);
  simple_script_ = nullptr;
  detail::ClearClickFrameLookupCache(lua_);
  frame_api::ClearNamedFontObjectRegistry(lua_);
  owner_.frame_store_.ReleaseAllLuaBindingsWhileValid();
  owner_.nameplate_frames_.BindLuaState(nullptr);
  owner_.frame_materializer_->ClearLuaState();
  owner_.frame_store_.DestroyNativeObjectsWhileLuaValid();
  owner_.movie_frame_runtime_.Shutdown();
  owner_.frame_event_runtime_.Shutdown();
  openwow::game::simple_script::BindFrameScriptLuaState(nullptr);
  openwow::ui::frame_script_events::FrameScript_SetLuaStateTyped(nullptr);
  UninstallLuaAddonMemoryTracker(lua_);
  lua_ = nullptr;
  owner_.frame_store_.BindLuaState(nullptr);
  owner_.retained_layout_.BindLuaState(nullptr);
  owner_.frame_input_router_.Reset();
  frame_script_runtime_.reset();
  sound_lua_context_.reset();
  typed_contexts_.sound = nullptr;
  typed_contexts_.security->Reset();
}

openwow::ui::lua::NativeBindingCatalog
WorldLuaRuntime::FrameNativeBindingCatalog() {
  static constexpr openwow::ui::LuaGlobalBinding kBindings[] = {
      {"GetText", detail::LuaGetText},
      {"GetNumFrames", detail::LuaGetNumFrames},
      {"EnumerateFrames", detail::LuaEnumerateFrames},
      {"CreateFont", frame_api::LuaCreateFont},
      {"CreateFrame", LuaCreateFrameBinding},
      {"GetFramesRegisteredForEvent", detail::LuaGetFramesRegisteredForEvent},
      {"GetCurrentKeyBoardFocus", detail::LuaGetCurrentKeyBoardFocus},
  };
  return openwow::ui::lua::NativeFunctionCatalog(
      "ui.framescript.frames", openwow::ui::lua::BindingScope::kShared,
      kBindings);
}

void WorldLuaRuntime::InstallFrameRuntimeCallbacks(lua_State* state) {
  if (state == nullptr) {
    return;
  }
  lua_pushcfunction(state, [](lua_State* lua) -> int {
    const char* name = luaL_optstring(lua, 1, nullptr);
    if (name != nullptr) {
      if (auto* input = RegistryPort<FrameInputRouter>(
              lua, kFrameInputRouterRegistryKey);
          input != nullptr) {
        input->SetFocus(name);
      }
    }
    return 0;
  });
  lua_setfield(state, LUA_REGISTRYINDEX, "openwow.set_focus");
  lua_pushcfunction(state, [](lua_State* lua) -> int {
    const char* name = luaL_optstring(lua, 1, nullptr);
    if (name != nullptr) {
      if (auto* input = RegistryPort<FrameInputRouter>(
              lua, kFrameInputRouterRegistryKey);
          input != nullptr) {
        input->ClearFocusIfOwnedBy(name);
      }
    }
    return 0;
  });
  lua_setfield(state, LUA_REGISTRYINDEX, "openwow.clear_focus");
}

void WorldLuaRuntime::UninstallFrameRuntimeCallbacks(lua_State* state) {
  if (state == nullptr) {
    return;
  }
  lua_pushnil(state);
  lua_setfield(state, LUA_REGISTRYINDEX, "openwow.set_focus");
  lua_pushnil(state);
  lua_setfield(state, LUA_REGISTRYINDEX, "openwow.clear_focus");
}

}

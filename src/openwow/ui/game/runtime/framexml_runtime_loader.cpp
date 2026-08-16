#include "openwow/ui/game/runtime/framexml_runtime_loader.h"

#include "openwow/game/actions/bindings/application/binding_profiles.h"
#include "openwow/game/localization.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/addon_manager.h"
#include "openwow/ui/addons_data.h"
#include "openwow/ui/framexml/framexml_name_utils.h"
#include "openwow/ui/game/api/game_lua_api_addon.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/framescript/core/frame_font_runtime.h"
#include "openwow/ui/game/framescript/core/frame_method_registry.h"
#include "openwow/ui/game/framescript/widgets/texture_asset_probe.h"
#include "openwow/ui/game/framescript/xml/frame_xml_bootstrap.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/game_ui_scale.h"
#include "openwow/ui/game/runtime/frame_template_expander.h"
#include "openwow/ui/game/runtime/world_lua_runtime.h"
#include "openwow/ui/game/secure_execution.h"
#include "openwow/ui/game/ui_load_status_log.h"
#include "openwow/ui/lua_c_api_convenience.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/ui/widgets/script_object.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/text/ascii.h"

extern "C" {
#include <lua.hpp>
}

#include <algorithm>
#include <chrono>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace openwow::ui::game::runtime {
namespace {

constexpr const char* kDefaultFrameXmlTocPath =
    "/Interface/FrameXML/FrameXML.toc";
constexpr const char* kKeyBindingRegistryKey = "openwow.key_binding_manager";

openwow::game::BindingProfiles* GetBindingProfiles(lua_State* lua) {
  lua_getfield(lua, LUA_REGISTRYINDEX, kKeyBindingRegistryKey);
  auto* bindings = static_cast<openwow::game::BindingProfiles*>(
      lua_touserdata(lua, -1));
  lua_pop(lua, 1);
  return bindings;
}

bool CallLuaObjectMethod(lua_State* lua, int object_index, const char* method,
                         int argument_count,
                         const std::function<void(lua_State*)>& push_arguments) {
  const int top = lua_gettop(lua);
  object_index = lua_absindex(lua, object_index);
  lua_getfield(lua, object_index, method);
  if (lua_isfunction(lua, -1) == 0) {
    lua_settop(lua, top);
    return false;
  }
  lua_pushvalue(lua, object_index);
  push_arguments(lua);
  if (lua_pcall(lua, argument_count + 1, 0, 0) == 0) {
    return true;
  }
  const char* error = lua_tostring(lua, -1);
  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kWarn,
      std::string("GameUIManager: ") + method + " failed: " +
          (error != nullptr ? error : "Lua error"));
  lua_settop(lua, top);
  return false;
}

struct MaterializedFrameCounts {
  std::uint64_t frames{0};
  std::uint64_t textures{0};
  std::uint64_t font_strings{0};
};

MaterializedFrameCounts CountMaterializedFrameTypes(const FrameStore& objects) {
  MaterializedFrameCounts counts;
  for (const auto& view : objects.CollectStableFrames()) {
    switch (openwow::ui::widgets::ScriptObjectTypeFromName(view.frame->kind)) {
      case openwow::ui::widgets::ScriptObjectType::Texture:
        ++counts.textures;
        break;
      case openwow::ui::widgets::ScriptObjectType::FontString:
        ++counts.font_strings;
        break;
      default:
        ++counts.frames;
        break;
    }
  }
  return counts;
}

void LogFrameXmlDiagnostics(const std::string& path,
                            const std::vector<std::string>& diagnostics) {
  for (const auto& diagnostic : diagnostics) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                              "GameUIManager: XML warning in " + path +
                                  ": " + diagnostic);
  }
}

void AppendFrameXmlLoadResult(UiLoadStatusSink* sink,
                              const std::string& toc_path,
                              const FrameXmlLoadResult& result) {
  if (sink == nullptr) {
    return;
  }
  if (!result.ok) {
    if (!result.error.empty()) {
      if (result.error.rfind("TOC file not found: ", 0) == 0) {
        sink->AppendStatus(
            2, "Couldn't open " + ToWoWClientLogPath(result.error.substr(20)));
      } else {
        sink->AppendStatus(2, result.error);
      }
    }
    return;
  }
  AppendUiLoadEntries(sink, result.diagnostics);
  if (!result.diagnostics.empty()) {
    sink->AppendStatus(0,
                       "** Loading table of contents " +
                           ToWoWClientLogPath(toc_path));
  }
}

int SnapshotLuaTable(lua_State* lua, const int table_index) {
  const int source = lua_absindex(lua, table_index);
  lua_newtable(lua);
  const int snapshot = lua_absindex(lua, -1);
  lua_pushnil(lua);
  while (lua_next(lua, source) != 0) {
    lua_pushvalue(lua, -2);
    lua_pushvalue(lua, -2);
    lua_rawset(lua, snapshot);
    lua_pop(lua, 1);
  }
  return luaL_ref(lua, LUA_REGISTRYINDEX);
}

void RestoreLuaGlobalTable(lua_State* lua, const int snapshot_ref) {
  lua_pushvalue(lua, LUA_GLOBALSINDEX);
  const int globals = lua_absindex(lua, -1);
  lua_newtable(lua);
  const int keys = lua_absindex(lua, -1);
  lua_Integer key_count = 0;
  lua_pushnil(lua);
  while (lua_next(lua, globals) != 0) {
    lua_pop(lua, 1);
    lua_pushvalue(lua, -1);
    lua_rawseti(lua, keys, ++key_count);
  }
  for (lua_Integer index = 1; index <= key_count; ++index) {
    lua_rawgeti(lua, keys, index);
    lua_pushnil(lua);
    lua_rawset(lua, globals);
  }
  lua_pop(lua, 1);
  lua_rawgeti(lua, LUA_REGISTRYINDEX, snapshot_ref);
  const int snapshot = lua_absindex(lua, -1);
  lua_pushnil(lua);
  while (lua_next(lua, snapshot) != 0) {
    lua_pushvalue(lua, -2);
    lua_pushvalue(lua, -2);
    lua_rawset(lua, globals);
    lua_pop(lua, 1);
  }
  lua_pop(lua, 2);
}

int SnapshotNamedFontRegistry(lua_State* lua) {
  lua_getfield(lua, LUA_REGISTRYINDEX,
               frame_api::kNamedFontObjectRegistryKey);
  if (lua_istable(lua, -1) == 0) {
    lua_pop(lua, 1);
    return LUA_NOREF;
  }
  const int snapshot = SnapshotLuaTable(lua, -1);
  lua_pop(lua, 1);
  return snapshot;
}

void RestoreNamedFontRegistry(lua_State* lua, const int snapshot_ref) {
  if (snapshot_ref == LUA_NOREF) {
    lua_pushnil(lua);
  } else {
    lua_rawgeti(lua, LUA_REGISTRYINDEX, snapshot_ref);
  }
  lua_setfield(lua, LUA_REGISTRYINDEX,
               frame_api::kNamedFontObjectRegistryKey);
}

}

FrameXmlRuntimeLoader::FrameXmlRuntimeLoader(GameUIManager& owner) noexcept
    : owner_(owner) {}

void FrameXmlRuntimeLoader::BeginLifetime(
    const openwow::vfs::VirtualFileSystem* vfs) {
  addon_runtime_loader_.reset();
  loader_.BeginLoadLifetime();
  loader_.SetVfs(vfs);
  loader_.SetXmlProcessCallback(
      [this](const std::string& path,
             const openwow::ui::framexml::ParseResult& result,
             const std::size_t group_index) {
        ProcessXmlFrameGroup(path, result, group_index);
      });
  loader_.SetXmlFontProcessCallback(
      [this](const std::string& path,
             const openwow::ui::FontDefinition& definition) {
        RegisterXmlFontDefinition(path, definition);
      });
  lua_State* const lua = owner_.world_lua_runtime_->state();
  auto* const binding_profiles =
      owner_.session_ != nullptr ? owner_.session_->binding_profiles() : nullptr;
  if (vfs != nullptr && lua != nullptr && binding_profiles != nullptr) {
    auto& addons_data = openwow::ui::AddOnsData::Get();
    addon_runtime_loader_ = std::make_unique<AddonRuntimeLoader>(
        owner_, openwow::ui::AddonManager::Get(), addons_data, *binding_profiles,
        *vfs, *lua, SecureExecution::Get());
    detail::BindAddonLuaContext(*lua, addons_data, *owner_.runtime_context_);
  }
}

bool FrameXmlRuntimeLoader::LoadDefaultUI(
    std::function<void(float)> progress_callback,
    UiLoadStatusSink* status_sink) {
  const auto load_started = std::chrono::steady_clock::now();
  CVarSystem::Instance().RegisterDefaults();
  const std::uint32_t interface_version =
      loader_.ReadTocInterfaceVersion(kDefaultFrameXmlTocPath);
  if (interface_version != 0 &&
      interface_version != kRetailFrameXmlInterfaceVersion) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "GameUIManager: FrameXML TOC interface mismatch: expected " +
            std::to_string(kRetailFrameXmlInterfaceVersion) + ", got " +
            std::to_string(interface_version));
    return false;
  }

  auto& addon_manager = openwow::ui::AddonManager::Get();
  auto& addons_data = openwow::ui::AddOnsData::Get();
  if (owner_.vfs_ != nullptr && !addon_manager.HasCompletedDiscovery()) {
    addon_manager.ScanAddons(*owner_.vfs_);
  }
  const AddonRuntimeIdentity& identity = owner_.persistence_identity();

  addons_data.ReloadSavedState(
      identity.account_name, identity.realm_name,
      identity.character_name.empty() ? nullptr
                                      : identity.character_name.c_str());

  TocLoadProgress progress{};
  progress.callback = std::move(progress_callback);
  if (progress.callback) {
    const char* const character_name = identity.character_name.empty()
                                           ? nullptr
                                           : identity.character_name.c_str();
    const std::size_t addon_entry_budget =
        addon_runtime_loader_ != nullptr
            ? addon_runtime_loader_->ComputeStartupTocEntryBudget(character_name)
            : 0u;
    progress.total = loader_.ReadTocVisibleEntryCount(kDefaultFrameXmlTocPath) +
                     static_cast<int>(addon_entry_budget);
  }
  constexpr std::size_t kStockFrameRegistryReserve = 32768u;
  owner_.frame_store_.Reserve(kStockFrameRegistryReserve);
  owner_.retained_layout_.Reserve(kStockFrameRegistryReserve);
  lua_State* const lua = owner_.world_lua_runtime_->state();
  frame_api::ResetTextureValidationPerformanceCounters(lua);

  const auto frame_checkpoint = owner_.frame_store_.registration_checkpoint();
  auto materializer_checkpoint =
      owner_.frame_materializer_->CaptureLoadCheckpoint();
  auto virtual_template_checkpoint =
      openwow::ui::framexml::CaptureVirtualTemplates();
  auto font_checkpoint = font_registry_;
  lua_pushvalue(lua, LUA_GLOBALSINDEX);
  const int global_checkpoint = SnapshotLuaTable(lua, -1);
  lua_pop(lua, 1);
  const int named_font_checkpoint = SnapshotNamedFontRegistry(lua);
  const bool post_bootstrap_complete_checkpoint = post_bootstrap_complete_;
  const std::uint32_t post_bootstrap_count_checkpoint = post_bootstrap_count_;
  bool transaction_open = true;
  const auto rollback = [&]() {
    if (!transaction_open) return;
    owner_.frame_materializer_->EndDefaultFrameXmlLoad();
    owner_.frame_store_.RollbackRegistrationsAfter(frame_checkpoint);
    owner_.frame_materializer_->RestoreLoadCheckpoint(
        std::move(materializer_checkpoint));
    openwow::ui::framexml::RestoreVirtualTemplates(
        std::move(virtual_template_checkpoint));
    font_registry_ = std::move(font_checkpoint);
    RestoreLuaGlobalTable(lua, global_checkpoint);
    RestoreNamedFontRegistry(lua, named_font_checkpoint);
    post_bootstrap_complete_ = post_bootstrap_complete_checkpoint;
    post_bootstrap_count_ = post_bootstrap_count_checkpoint;
    luaL_unref(lua, LUA_REGISTRYINDEX, global_checkpoint);
    if (named_font_checkpoint != LUA_NOREF) {
      luaL_unref(lua, LUA_REGISTRYINDEX, named_font_checkpoint);
    }
    transaction_open = false;
  };

  active_progress_ = progress.callback ? &progress : nullptr;
  groups_since_progress_pulse_ = 0;
  last_progress_pulse_ = 0.0F;
  const auto clear_progress = [this]() {
    active_progress_ = nullptr;
    groups_since_progress_pulse_ = 0;
  };
  FrameXmlBootstrapOptions options;
  owner_.frame_materializer_->BeginDefaultFrameXmlLoad();
  owner_.retained_layout_.SetBootstrapActive(true);
  auto bootstrap = LoadFrameXmlWithIntegrity(
      lua, owner_.vfs_, loader_, GetBindingProfiles(lua), options,
      progress.callback ? &progress : nullptr, status_sink);
  AppendFrameXmlLoadResult(status_sink, kDefaultFrameXmlTocPath,
                           bootstrap.frame_xml);
  if (!bootstrap.ok) {
    rollback();
    owner_.retained_layout_.SetBootstrapActive(false);
    clear_progress();
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                              "GameUIManager: FrameXML load failed: " +
                                  bootstrap.error);
    return false;
  }

  std::string missing_required_roots;
  using ScriptObjectType = openwow::ui::widgets::ScriptObjectType;
  for (const auto& [required_name, required_type] :
       {std::pair{std::string_view("UIParent"), ScriptObjectType::Frame},
        std::pair{std::string_view("GameTooltip"),
                  ScriptObjectType::GameTooltip}}) {
    const std::string name(required_name);
    const auto* instantiated = owner_.frame_store_.FindFrame(name);
    const auto lua_ref = owner_.frame_store_.FindLuaRef(name);
    if (lua_ref.has_value() &&
        owner_.frame_materializer_->IsDefaultDeclarationIdentity(name,
                                                                 *lua_ref) &&
        instantiated != nullptr &&
        openwow::ui::widgets::IsScriptTypeKindOf(
            openwow::ui::widgets::ScriptObjectTypeFromName(instantiated->kind),
            required_type)) {
      continue;
    }
    if (!missing_required_roots.empty()) {
      missing_required_roots += ", ";
    }
    missing_required_roots.append(required_name);
  }
  if (!missing_required_roots.empty()) {
    const std::string message =
        "GameUIManager: FrameXML did not instantiate required root frame(s): " +
        missing_required_roots;
    if (status_sink != nullptr) {
      status_sink->AppendStatus(2, message);
    }
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, message);
    rollback();
    owner_.retained_layout_.SetBootstrapActive(false);
    clear_progress();
    return false;
  }
  if (!CompletePostFrameXmlBootstrap(status_sink)) {
    rollback();
    owner_.retained_layout_.SetBootstrapActive(false);
    clear_progress();
    return false;
  }
  owner_.frame_materializer_->EndDefaultFrameXmlLoad();
  luaL_unref(lua, LUA_REGISTRYINDEX, global_checkpoint);
  if (named_font_checkpoint != LUA_NOREF) {
    luaL_unref(lua, LUA_REGISTRYINDEX, named_font_checkpoint);
  }
  transaction_open = false;
  owner_.retained_layout_.SetBootstrapActive(false);

  const auto counts = CountMaterializedFrameTypes(owner_.frame_store_);
  owner_.performance_counters_.default_frame_xml_materialized_objects =
      counts.frames + counts.textures + counts.font_strings;
  owner_.performance_counters_.default_frame_xml_materialized_frames =
      counts.frames;
  owner_.performance_counters_.default_frame_xml_materialized_textures =
      counts.textures;
  owner_.performance_counters_.default_frame_xml_materialized_font_strings =
      counts.font_strings;
  owner_.performance_counters_.default_frame_xml_template_roots =
      owner_.frame_materializer_->template_count();
  owner_.performance_counters_.default_frame_xml_template_nodes =
      owner_.frame_materializer_->template_node_count();
  if (addon_runtime_loader_ != nullptr) {
    addon_runtime_loader_->LoadStartup(AddonRuntimeLoadContext{
        .identity = identity,
        .status_sink = status_sink,
        .progress = progress.callback ? &progress : nullptr,
    });
  }
  clear_progress();
  openwow::game::SyncLocalizedGlobalStrings(lua);
  default_ui_loaded_ = true;
  owner_.retained_layout_.Invalidate();
  owner_.world_lua_runtime_->CompleteFrameXmlLoad();

  const auto texture_validation =
      frame_api::GetTextureValidationPerformanceCounters(lua);
  owner_.performance_counters_.texture_validation_requests =
      texture_validation.requests;
  owner_.performance_counters_.texture_validation_cache_hits =
      texture_validation.cache_hits;
  owner_.performance_counters_.texture_validation_source_reads =
      texture_validation.source_reads;
  owner_.performance_counters_.default_ui_load_duration_ns =
      static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::steady_clock::now() - load_started)
              .count());
  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kInfo,
      "GameUIManager: LoadDefaultUI complete — " +
          std::to_string(bootstrap.frame_xml.xml_files_loaded) + " XML, " +
          std::to_string(bootstrap.frame_xml.lua_files_loaded) + " Lua, " +
          std::to_string(owner_.frame_store_.size()) + " frames");
  return true;
}

bool FrameXmlRuntimeLoader::LoadToc(
    const std::string& toc_path, UiLoadStatusSink* status_sink,
    TocLoadProgress* progress, const std::string_view addon_name,
    openwow::core::MD5Context* digest) {
  lua_State* const lua = owner_.world_lua_runtime_->state();
  const auto frame_checkpoint = owner_.frame_store_.registration_checkpoint();
  auto materializer_checkpoint =
      owner_.frame_materializer_->CaptureLoadCheckpoint();
  auto virtual_template_checkpoint =
      openwow::ui::framexml::CaptureVirtualTemplates();
  auto font_checkpoint = font_registry_;
  lua_pushvalue(lua, LUA_GLOBALSINDEX);
  const int global_checkpoint = SnapshotLuaTable(lua, -1);
  lua_pop(lua, 1);
  const int named_font_checkpoint = SnapshotNamedFontRegistry(lua);

  TocLoadProgress* const previous_progress = active_progress_;
  const float previous_pulse = last_progress_pulse_;
  const bool continuing = previous_progress != nullptr &&
                          previous_progress == progress;
  active_progress_ = progress != nullptr && progress->callback ? progress : nullptr;
  groups_since_progress_pulse_ = 0;
  if (!continuing) {
    last_progress_pulse_ = progress != nullptr && progress->total > 0
                               ? static_cast<float>(progress->completed) /
                                     static_cast<float>(progress->total)
                               : 0.0F;
  }
  auto result = loader_.LoadToc(lua, toc_path, progress, digest, addon_name);
  active_progress_ = previous_progress;
  groups_since_progress_pulse_ = 0;
  if (!continuing) {
    last_progress_pulse_ = previous_pulse;
  }
  AppendFrameXmlLoadResult(status_sink, toc_path, result);
  if (!result.ok) {
    owner_.frame_store_.RollbackRegistrationsAfter(frame_checkpoint);
    owner_.frame_materializer_->RestoreLoadCheckpoint(
        std::move(materializer_checkpoint));
    openwow::ui::framexml::RestoreVirtualTemplates(
        std::move(virtual_template_checkpoint));
    font_registry_ = std::move(font_checkpoint);
    RestoreLuaGlobalTable(lua, global_checkpoint);
    RestoreNamedFontRegistry(lua, named_font_checkpoint);
    owner_.retained_layout_.Invalidate();
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                              "GameUIManager: TOC load failed: " +
                                  (result.error.empty()
                                       ? std::to_string(result.file_failures) +
                                             " file(s) failed"
                                       : result.error));
  } else {
    owner_.retained_layout_.Invalidate();
  }

  luaL_unref(lua, LUA_REGISTRYINDEX, global_checkpoint);
  if (named_font_checkpoint != LUA_NOREF) {
    luaL_unref(lua, LUA_REGISTRYINDEX, named_font_checkpoint);
  }
  return result.ok;
}

void FrameXmlRuntimeLoader::Reset() {
  addon_runtime_loader_.reset();
  if (lua_State* const lua = owner_.world_lua_runtime_->state(); lua != nullptr) {
    detail::BindAddonLuaContext(*lua, openwow::ui::AddOnsData::Get(),
                                *owner_.runtime_context_);
  }
  font_registry_.Clear();
  active_progress_ = nullptr;
  groups_since_progress_pulse_ = 0;
  last_progress_pulse_ = 0.0F;
  post_bootstrap_complete_ = false;
  default_ui_loaded_ = false;
  post_bootstrap_count_ = 0;
}

void FrameXmlRuntimeLoader::RegisterXmlFontDefinition(
    const std::string& path,
    const openwow::ui::FontDefinition& definition) {
  lua_State* const lua = owner_.world_lua_runtime_->state();
  if (lua == nullptr || definition.name.empty()) {
    return;
  }
  font_registry_.Register(definition);
  const auto resolved = font_registry_.Get(definition.name);
  if (!resolved.has_value()) {
    return;
  }
  const int top = lua_gettop(lua);
  lua_getglobal(lua, "CreateFont");
  lua_pushlstring(lua, definition.name.c_str(), definition.name.size());
  if (lua_pcall(lua, 1, 1, 0) != 0 || lua_istable(lua, -1) == 0) {
    const char* error = lua_tostring(lua, -1);
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "GameUIManager: unable to create FrameXML font " + definition.name +
            " from " + path + ": " +
            (error != nullptr ? error : "CreateFont returned no font object"));
    lua_settop(lua, top);
    return;
  }
  const int font = lua_absindex(lua, -1);
  if (!definition.inherits.empty()) {
    CallLuaObjectMethod(lua, font, "SetFontObject", 1, [&](lua_State* state) {
      lua_pushlstring(state, definition.inherits.c_str(),
                      definition.inherits.size());
    });
  }
  if (!resolved->font_file.empty() && resolved->has_height) {
    std::string flags;
    const std::string outline = openwow::text::ToLowerAscii(resolved->outline);
    if (outline == "thick") {
      flags = "THICKOUTLINE";
    } else if (outline == "normal" || outline == "outline") {
      flags = "OUTLINE";
    }
    if (resolved->monochrome) {
      flags += flags.empty() ? "MONOCHROME" : "|MONOCHROME";
    }
    CallLuaObjectMethod(lua, font, "SetFont", 3, [&](lua_State* state) {
      lua_pushlstring(state, resolved->font_file.c_str(),
                      resolved->font_file.size());
      lua_pushnumber(state,
                     openwow::ui::StoredUiHorizontalCoordinateToPixels(
                         resolved->height));
      lua_pushlstring(state, flags.c_str(), flags.size());
    });
  }
  if (resolved->has_color) {
    CallLuaObjectMethod(lua, font, "SetTextColor", 4, [&](lua_State* state) {
      lua_pushnumber(state, resolved->color.r);
      lua_pushnumber(state, resolved->color.g);
      lua_pushnumber(state, resolved->color.b);
      lua_pushnumber(state, resolved->color.a);
    });
  }
  if (resolved->shadow.has_value()) {
    const auto& shadow = *resolved->shadow;
    CallLuaObjectMethod(lua, font, "SetShadowColor", 4,
                        [&](lua_State* state) {
                          lua_pushnumber(state, shadow.color.r);
                          lua_pushnumber(state, shadow.color.g);
                          lua_pushnumber(state, shadow.color.b);
                          lua_pushnumber(state, shadow.color.a);
                        });
    CallLuaObjectMethod(lua, font, "SetShadowOffset", 2,
                        [&](lua_State* state) {
                          lua_pushnumber(
                              state,
                              openwow::ui::StoredUiHorizontalCoordinateToPixels(
                                  shadow.offset.x));
                          lua_pushnumber(
                              state,
                              openwow::ui::StoredUiHorizontalCoordinateToPixels(
                                  shadow.offset.y));
                        });
  }
  if (resolved->has_justify_h) {
    CallLuaObjectMethod(lua, font, "SetJustifyH", 1, [&](lua_State* state) {
      lua_pushlstring(state, resolved->justify_h.c_str(),
                      resolved->justify_h.size());
    });
  }
  if (resolved->has_justify_v) {
    CallLuaObjectMethod(lua, font, "SetJustifyV", 1, [&](lua_State* state) {
      lua_pushlstring(state, resolved->justify_v.c_str(),
                      resolved->justify_v.size());
    });
  }
  if (resolved->has_spacing) {
    CallLuaObjectMethod(lua, font, "SetSpacing", 1, [&](lua_State* state) {
      lua_pushnumber(state, openwow::ui::StoredUiHorizontalCoordinateToPixels(
                                resolved->spacing));
    });
  }
  if (resolved->has_indented_word_wrap) {
    CallLuaObjectMethod(
        lua, font, "SetIndentedWordWrap", 1, [&](lua_State* state) {
          lua_pushboolean(state, resolved->indented_word_wrap ? 1 : 0);
        });
  }
  lua_settop(lua, top);
}

void FrameXmlRuntimeLoader::ProcessXmlFrameGroup(
    const std::string& path,
    const openwow::ui::framexml::ParseResult& result,
    const std::size_t group_index) {
  if (group_index >= result.top_level_groups.size()) {
    return;
  }
  if (group_index == 0) {
    LogFrameXmlDiagnostics(path, result.diagnostics);
  }
  const auto& group = result.top_level_groups[group_index];
  if (group.frame_count == 0 || group.first_frame >= result.frames.size()) {
    return;
  }
  const std::size_t end =
      std::min(result.frames.size(), group.first_frame + group.frame_count);
  std::vector<openwow::ui::framexml::UiFrame> frames;
  frames.reserve(end - group.first_frame);
  for (std::size_t index = group.first_frame; index < end; ++index) {
    frames.push_back(result.frames[index]);
  }

  const auto declaring_source =
      SecureExecution::Get().CurrentTaint(owner_.world_lua_runtime_->state());
  for (auto& frame : frames) {
    openwow::ui::framexml::StampXmlScriptProvenance(frame, declaring_source);
  }
  RegisterParsedFrameGroup(path, frames);
  constexpr std::size_t kFrameXmlGroupsPerProgressPulse = 64u;
  if (active_progress_ != nullptr && active_progress_->callback &&
      active_progress_->total > 0 &&
      ++groups_since_progress_pulse_ >= kFrameXmlGroupsPerProgressPulse) {
    groups_since_progress_pulse_ = 0;
    const float file_fraction =
        static_cast<float>(group_index + 1u) /
        static_cast<float>(
            std::max<std::size_t>(1u, result.top_level_groups.size()));
    const float progress =
        (static_cast<float>(active_progress_->completed) + file_fraction) /
        static_cast<float>(active_progress_->total);
    last_progress_pulse_ = std::max(last_progress_pulse_, progress);
    active_progress_->callback(last_progress_pulse_);
    ++owner_.performance_counters_.frame_xml_progress_pulses;
  }
}

void FrameXmlRuntimeLoader::RegisterParsedFrameGroup(
    const std::string& path,
    const std::vector<openwow::ui::framexml::UiFrame>& frames) {
  if (frames.empty()) {
    return;
  }
  if (owner_.frame_materializer_->loading_default_frame_xml()) {
    owner_.performance_counters_.default_frame_xml_parsed_declarations +=
        frames.size();
  }
  std::unordered_map<std::string,
                     std::vector<openwow::ui::framexml::UiFrame>>
      file_templates;
  CollectFrameTemplates(frames, file_templates);
  for (const auto& frame : frames) {
    if (frame.virtual_template && !frame.name.empty()) {
      openwow::ui::framexml::RegisterVirtualTemplate(frame);
    }
  }
  std::unordered_set<std::string> template_owned_frames;
  template_owned_frames.reserve(frames.size());
  for (const auto& [template_root, template_frames] : file_templates) {
    (void)template_root;
    for (const auto& frame : template_frames) {
      if (!frame.name.empty()) {
        template_owned_frames.insert(frame.name);
      }
    }
  }
  std::unordered_map<std::string, std::string_view> parent_by_frame;
  parent_by_frame.reserve(frames.size() * 2);
  for (const auto& frame : frames) {
    if (!frame.name.empty()) {
      parent_by_frame.insert_or_assign(frame.name, frame.parent);
    }
  }
  owner_.frame_materializer_->AddTemplates(file_templates);
  const auto is_template_owned = [&](const auto& frame) {
    if (!frame.name.empty() && template_owned_frames.contains(frame.name)) {
      return true;
    }
    std::string parent = frame.parent;
    constexpr int kMaxTemplateParentDepth = 64;
    for (int depth = 0; depth < kMaxTemplateParentDepth && !parent.empty();
         ++depth) {
      if (file_templates.contains(parent) ||
          template_owned_frames.contains(parent)) {
        return true;
      }
      const auto it = parent_by_frame.find(parent);
      if (it == parent_by_frame.end()) {
        break;
      }
      parent = it->second;
    }
    return false;
  };
  std::vector<openwow::ui::framexml::UiFrame> concrete_frames;
  concrete_frames.reserve(frames.size());
  for (const auto& frame : frames) {
    if (!is_template_owned(frame)) {
      concrete_frames.push_back(frame);
    }
  }
  if (owner_.frame_materializer_->loading_default_frame_xml()) {
    owner_.performance_counters_.default_frame_xml_concrete_declarations +=
        concrete_frames.size();
  }
  if (!concrete_frames.empty()) {
    auto root = std::move(concrete_frames.front());
    std::vector<openwow::ui::framexml::UiFrame> children;
    children.reserve(concrete_frames.size() - 1);
    children.insert(children.end(),
                    std::make_move_iterator(concrete_frames.begin() + 1),
                    std::make_move_iterator(concrete_frames.end()));

    if (owner_.frame_materializer_->InstantiateFrameTree(
            std::move(root), std::move(children), 0, false, false,
            owner_.frame_materializer_->loading_default_frame_xml()) ==
        LUA_NOREF) {
      openwow::diagnostics::Log(
          openwow::diagnostics::LogLevel::kWarn,
          "GameUIManager: frame tree instantiation produced no root frame for " +
              path);
    }
  }
}

bool FrameXmlRuntimeLoader::CompletePostFrameXmlBootstrap(
    UiLoadStatusSink* status_sink) {
  if (post_bootstrap_complete_) {
    return true;
  }
  if (owner_.world_lua_runtime_->state() == nullptr) {
    constexpr const char* kError =
        "GameUIManager: post-FrameXML bootstrap requires Lua state";
    if (status_sink != nullptr) {
      status_sink->AppendStatus(1, kError);
    }
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn, kError);
    return false;
  }
  RegisterGameUiScaleCallbacks();
  SyncGameUiScaleFromCVars(owner_, true);
  owner_.retained_layout_.InvalidateGraph();
  owner_.frame_traversal_index_.InvalidateHierarchy();
  post_bootstrap_complete_ = true;
  ++post_bootstrap_count_;
  openwow::diagnostics::Log(
      openwow::diagnostics::LogLevel::kInfo,
      "GameUIManager: post-FrameXML bootstrap complete");
  return true;
}

}

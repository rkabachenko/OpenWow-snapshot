#include "openwow/ui/game/runtime/world_ui_runtime_host.h"

#include "openwow/game/achievements/application/tracked_achievement_state.h"
#include "openwow/game/talent_info.h"
#include "openwow/game/threat_system.h"
#include "openwow/game/world_session.h"
#include "openwow/input/input_manager.h"
#include "openwow/net/client_services.h"
#include "openwow/render/models/characters/model_portrait.h"
#include "openwow/ui/framexml/framexml_parser.h"
#include "openwow/ui/game/api/game_lua_api_profession.h"
#include "openwow/ui/game/autocomplete.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/runtime/framexml_runtime_loader.h"
#include "openwow/ui/game/runtime/world_lua_runtime.h"
#include "openwow/ui/game/saved_variables.h"
#include "openwow/ui/game/tooltip_system.h"
#include "openwow/ui/game/ui_coordination.h"
#include "openwow/ui/game/ui_error_manager.h"
#include "openwow/foundation/diagnostics/logging.h"

namespace openwow::ui::game::runtime {

WorldUiRuntimeHost::WorldUiRuntimeHost(GameUIManager& owner) noexcept
    : owner_(owner) {}

bool WorldUiRuntimeHost::initialized() const noexcept {
  return owner_.world_lua_runtime_->state() != nullptr;
}

bool WorldUiRuntimeHost::loaded() const noexcept {
  return owner_.frame_xml_loader_->default_ui_loaded();
}

bool WorldUiRuntimeHost::Initialize(
    const openwow::vfs::VirtualFileSystem* vfs,
    openwow::game::WorldSession* session,
    openwow::game::CursorSurface& cursor) {
  if (initialized()) {
    return true;
  }

  openwow::ui::framexml::ClearVirtualTemplates();
  owner_.vfs_ = vfs;
  owner_.render_resources_->SetVirtualFileSystem(vfs);
  owner_.session_ = session;
  owner_.movie_playback_adapter_.BindSession(session);
  if (session != nullptr) {
    session->BindWorldUiRuntime(owner_.runtime_context_.get());
    session->SetMovieCallbacks({
        .on_trigger_movie = [this](const std::uint32_t movie_id) {
          owner_.movie_playback_adapter_.Trigger(movie_id);
        },
    });
  }
  owner_.world_map_.BindWorldSession(session);
  TooltipSystem::Get().BindWorldSession(session);
  owner_.performance_counters_ = {};
  owner_.frame_materializer_->ResetMetrics();
  persistence_identity_ = {};
  const AddonRuntimeIdentity identity{
      .account_name = openwow::net::ClientServices::Instance().GetAccountName(),
      .realm_name = openwow::net::GetRealmName(),
      .character_name = session != nullptr ? session->pending_character_name()
                                           : std::string{},
  };
  if (const auto resolved =
          AddonRuntimeLoader::ResolvePersistenceIdentity(identity);
      resolved.has_value()) {
    persistence_identity_ = *resolved;
  } else if (session != nullptr) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "GameUIManager: persistence disabled for invalid world identity");
  }

  if (!owner_.world_lua_runtime_->Create(session, cursor)) {
    if (session != nullptr) {
      session->SetMovieCallbacks({});
      session->BindWorldUiRuntime(nullptr);
    }
    owner_.movie_playback_adapter_.Shutdown();
    owner_.world_map_.BindWorldSession(nullptr);
    TooltipSystem::Get().BindWorldSession(nullptr);
    owner_.session_ = nullptr;
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
                              "GameUIManager: failed to create Lua state");
    return false;
  }

  openwow::input::InputManager::Get().SyncMousePositionFromWindow();
  lua_State* const lua = owner_.world_lua_runtime_->state();
  owner_.frame_event_runtime_.Initialize(lua, session);
  owner_.movie_frame_runtime_.BindLuaState(lua);
  owner_.movie_frame_runtime_.BindVirtualFileSystem(vfs);
  owner_.world_map_.SetMapUpdateCallback({});
  owner_.world_map_.TeardownRuntimeData();
  if (session != nullptr) {
    if (const auto* dbc = session->GetDbcLoader(); dbc != nullptr) {
      owner_.world_map_.InitFromDbc(*dbc);
    }
  }
  owner_.world_map_.SetMapUpdateCallback([this]() {
    owner_.frame_event_runtime_.dispatcher().FireEvent(events::WORLD_MAP_UPDATE);
  });

  owner_.frame_xml_loader_->BeginLifetime(vfs);
  if (!owner_.render_resources_->Restore()) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "GameUIManager: UiRenderer init failed (UI rendering disabled)");
  }

  owner_.render_resources_->SetRuntimeActive(true);
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                            "GameUIManager: initialized");
  return true;
}

bool WorldUiRuntimeHost::LoadDefaultUI(
    std::function<void(float)> progress_callback,
    UiLoadStatusSink* status_sink) {
  if (!initialized()) {
    return false;
  }
  if (loaded()) {
    return true;
  }
  if (!owner_.frame_xml_loader_->LoadDefaultUI(std::move(progress_callback),
                                                status_sink)) {
    return false;
  }
  return true;
}

bool WorldUiRuntimeHost::LoadToc(const std::string& toc_path,
                               UiLoadStatusSink* status_sink,
                               TocLoadProgress* progress,
                               const std::string_view addon_name,
                               openwow::core::MD5Context* digest) {
  return initialized() && owner_.frame_xml_loader_->LoadToc(
                             toc_path, status_sink, progress, addon_name, digest);
}

void WorldUiRuntimeHost::SaveSavedVariables() {
  lua_State* const lua = owner_.world_lua_runtime_->state();
  if (!initialized() || lua == nullptr) {
    return;
  }
  SaveAllSavedVariables(lua, persistence_identity_.account_name,
                         persistence_identity_.realm_name,
                         persistence_identity_.character_name);
  if (auto* const addon_runtime_loader =
          owner_.frame_xml_loader_->addon_runtime_loader();
      addon_runtime_loader != nullptr) {
    (void)addon_runtime_loader->SaveLoadedSavedVariables(persistence_identity_);
  }
}

void WorldUiRuntimeHost::Shutdown() {
  owner_.frame_input_router_.SetRunningMacroInputButtonProvider({});
  owner_.lifecycle_commands_ = nullptr;
  if (!initialized()) {
    return;
  }

  if (owner_.session_ != nullptr) {
    owner_.session_->SetMovieCallbacks({});
    owner_.session_->BindWorldUiRuntime(nullptr);
  }

  owner_.render_resources_->model_surfaces().clear();
  owner_.render_resources_->model_diagnostics().clear();
  owner_.minimap_surface_submitter_ = {};
  openwow::ui::UnitFrameRegistry::Get().Reset();
  openwow::ui::ModalStack::Get().Reset();
  openwow::ui::StaticPopupManager::Get().Reset();
  openwow::ui::PanelManager::Get().Reset();
  owner_.minimap_state_.Reset();
  owner_.world_map_.SetMapUpdateCallback({});
  owner_.world_map_.BindWorldSession(nullptr);
  owner_.world_map_.TeardownRuntimeData();
  openwow::ui::game::detail::ResetProfessionSkillUiState();
  owner_.movie_frame_runtime_.Shutdown();
  owner_.movie_playback_adapter_.Shutdown();
  owner_.frame_event_runtime_.Shutdown();
  owner_.render_resources_->Release();
  owner_.render_resources_->SetRuntimeActive(false);
  owner_.mail_stationery_choices_.Reset();
  owner_.frame_xml_loader_->Reset();
  owner_.world_lua_runtime_->Destroy();
  TooltipSystem::Get().BindWorldSession(nullptr);
  AutoComplete::Get().Clear();
  openwow::game::TrackedAchievementState::Get().Destroy();
  openwow::game::ThreatSystem::Get().ClearThreatLists();
  openwow::game::TalentInfoStore::Get().ShutdownClearGameUiData();

  owner_.frame_store_.ClearAfterLuaDestroyed();
  owner_.frame_materializer_->Reset();
  openwow::ui::framexml::ClearVirtualTemplates();
  owner_.retained_layout_.Clear();
  owner_.frame_traversal_index_.Clear();
  owner_.frame_input_router_.Reset();
  owner_.vfs_ = nullptr;
  owner_.session_ = nullptr;
  persistence_identity_ = {};
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                            "GameUIManager: shutdown");
}

}

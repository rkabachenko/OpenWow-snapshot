#pragma once

#include "openwow/game/world_scene.h"
#include "openwow/game/world_environment_state.h"
#include "openwow/game/unit_sound_dispatch.h"
#include "openwow/game/objects/unit/unit_presentation_runtime.h"
#include "openwow/render/scene/world_frame.h"
#include "openwow/game/character_world_runtime.h"
#include "openwow/net/transport/packet_queue.h"
#include "openwow/game/input_controller.h"
#include "openwow/game/idle_billing_controller.h"
#include "openwow/game/movement_controller.h"
#include "openwow/game/actions/bindings/application/binding_profiles.h"
#include "openwow/game/actions/bindings/adapters/platform/sdl_binding_input_runtime.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/game/inventory/item_interaction_lease.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/inventory/items/item_definitions.h"
#include "openwow/game/loading_screen_progress_pump.h"
#include "openwow/game/player_npc_interaction.h"
#include "openwow/game/targeting.h"
#include "openwow/game/session_event_bridge.h"
#include "openwow/game/cinematic_player.h"
#include "openwow/audio/playback/audio_engine.h"
#include "openwow/ui/game/minimap.h"
#include "openwow/ui/game/minimap_integration.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/chat_frame.h"
#include "openwow/ui/game/loading_screen.h"
#include "openwow/ui/game/loot_frame.h"
#include "openwow/ui/surfaces/game/runtime/zone_ui_state.h"
#include "openwow/ui/surfaces/game/runtime/world_ui_lifecycle.h"
#include "openwow/ui/surfaces/game/adapters/protocol/world_ui_session_command_adapter.h"
#include "openwow/ui/surfaces/game/adapters/settings/world_ui_voice_settings_adapter.h"
#include "openwow/ui/surfaces/game/adapters/held_cursor/held_cursor_presentation.h"
#include "openwow/ui/surfaces/game/adapters/held_cursor/world_held_cursor_source.h"
#include "openwow/world/streaming/world_map.h"
#include "openwow/game/actions/held_cursor/adapters/platform/cursor_surface.h"
#include "openwow/game/death_manager.h"
#include "openwow/game/loading_screen_world_entry_gate.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

struct SDL_Window;

namespace openwow::render::api {
class RendererContext;
}
namespace openwow::render {
class DebugDrawRenderer;
class FloatingTextRenderer;
class PostProcess;
class TextureManager;
namespace m2 {
class M2System;
}
}

namespace openwow::ui::display {
class ProductionDisplaySettingsRuntime;
}
namespace openwow::audio { class SoundRuntime; }
namespace openwow::net::wotlk { struct WorldPacket; }
namespace openwow::ui::framexml { struct FrameRect; }

namespace openwow::game {

struct TransferPendingInfo;
class CGUnit_C;
class CGGameObject_C;

using SendPacketFn =
    std::function<bool(const openwow::net::wotlk::WorldPacket&)>;

enum class SceneState : std::uint8_t {
  kGlue = 0,
  kLoading,
  kInWorld,
};

class GameLoop : private openwow::render::api::RendererDeviceLifecycleObserver {
 public:
  struct IsolatedRuntime final {};

  GameLoop(IsolatedRuntime, openwow::render::TextureManager& texture_manager,
            openwow::render::m2::M2System& m2_system,
            openwow::audio::SoundRuntime& sound_runtime);

  GameLoop(openwow::ui::display::ProductionDisplaySettingsRuntime& runtime,
            openwow::render::TextureManager& texture_manager,
            openwow::render::m2::M2System& m2_system,
            openwow::audio::SoundRuntime& sound_runtime);
  ~GameLoop();

  GameLoop(const GameLoop&) = delete;
  GameLoop& operator=(const GameLoop&) = delete;

  bool Initialize(int screen_width, int screen_height);

  void Tick(float dt);

  void Shutdown();

  void EnterWorld(std::uint32_t map_id, float x, float y, float z,
                  float orientation, const std::string& map_name = {});
  void PrepareWorldEntry(std::uint32_t map_id, float x, float y, float z,
                         const std::string& map_name = {});
  bool FinalizePreparedWorldEntry(std::uint32_t map_id, float x, float y, float z,
                                  float orientation, const std::string& map_name = {});
  void AbortPreparedWorldEntry();

  void LeaveWorld();

  [[nodiscard]] SceneState state() const { return state_; }
  [[nodiscard]] bool IsInWorld() const { return state_ == SceneState::kInWorld; }
  [[nodiscard]] bool IsLoading() const { return state_ == SceneState::kLoading; }

  [[nodiscard]] WorldScene& world_scene() { return world_scene_; }
  [[nodiscard]] const WorldScene& world_scene() const { return world_scene_; }

  [[nodiscard]] const world::CameraFramePose& frame_camera_pose() const {
    return world_scene_.camera().frame_pose();
  }

  [[nodiscard]] openwow::render::PostProcess& post_process() { return post_process_; }
  [[nodiscard]] const openwow::render::PostProcess& post_process() const { return post_process_; }

  [[nodiscard]] InputController& input() { return input_; }
  [[nodiscard]] const InputController& input() const { return input_; }

  [[nodiscard]] TargetingSystem& targeting() { return targeting_; }
  [[nodiscard]] const TargetingSystem& targeting() const { return targeting_; }

  [[nodiscard]] openwow::ui::game::GameUIManager& game_ui() { return game_ui_; }
  [[nodiscard]] const openwow::ui::game::GameUIManager& game_ui() const { return game_ui_; }

  [[nodiscard]] openwow::ui::game::ChatFrame& chat_frame() { return chat_frame_; }
  [[nodiscard]] const openwow::ui::game::ChatFrame& chat_frame() const { return chat_frame_; }

  [[nodiscard]] openwow::render::FloatingTextRenderer& floating_text() { return floating_text_; }
  [[nodiscard]] const openwow::render::FloatingTextRenderer& floating_text() const { return floating_text_; }

  [[nodiscard]] DeathManager& death_manager() { return death_manager_; }
  [[nodiscard]] const DeathManager& death_manager() const { return death_manager_; }

  [[nodiscard]] CinematicPlayer& cinematic_player() { return cinematic_player_; }
  [[nodiscard]] const CinematicPlayer& cinematic_player() const { return cinematic_player_; }

  [[nodiscard]] openwow::ui::game::GameLoadingScreen& loading_screen() { return loading_screen_; }
  [[nodiscard]] const openwow::ui::game::GameLoadingScreen& loading_screen() const { return loading_screen_; }
  [[nodiscard]] std::uint64_t loading_screen_render_submissions() const noexcept {
    return loading_screen_render_submissions_;
  }
  [[nodiscard]] std::uint64_t loading_screen_self_presented_frames() const noexcept {
    return loading_screen_self_presented_frames_;
  }
  [[nodiscard]] std::uint64_t loading_screen_coalesced_callbacks() const noexcept {
    return loading_screen_coalesced_callbacks_;
  }

  [[nodiscard]] openwow::ui::game::MinimapIntegration& minimap_integration() { return minimap_; }
  [[nodiscard]] const openwow::ui::game::MinimapIntegration& minimap_integration() const { return minimap_; }

  [[nodiscard]] openwow::ui::game::LootFrame& loot_frame() { return loot_frame_; }
  [[nodiscard]] const openwow::ui::game::LootFrame& loot_frame() const { return loot_frame_; }

  [[nodiscard]] openwow::ui::game::MinimapIntegration& minimap() {
    return minimap_;
  }
  [[nodiscard]] const openwow::ui::game::MinimapIntegration& minimap() const {
    return minimap_;
  }

  [[nodiscard]] CursorSurface& cursor_manager() { return cursor_manager_; }
  [[nodiscard]] const CursorSurface& cursor_manager() const { return cursor_manager_; }
  [[nodiscard]] actions::held_cursor::HeldCursor& held_cursor() {
    return held_cursor_;
  }
  [[nodiscard]] ItemDefinitions& item_definitions() noexcept { return item_definitions_; }

  void SetVfs(const openwow::vfs::VirtualFileSystem* vfs) {
    vfs_ = vfs;
    binding_profiles_.SetVfs(vfs);
  }

  void SetCharacterWorldRuntime(CharacterWorldRuntime* runtime);

  void SetPacketQueue(openwow::net::PacketQueue* queue) { packet_queue_ = queue; }

  void SetScreenSize(int width, int height);
  [[nodiscard]] int screen_width() const { return screen_width_; }
  [[nodiscard]] int screen_height() const { return screen_height_; }

  void HandleMouseDelta(float dx, float dy);

  void BeginCameraFreelook();
  void EndCameraFreelook();

  void HandleScrollDelta(float delta);

  void OnLeftClickWorld(float screen_x, float screen_y);

  void OnRightClickWorld(float screen_x, float screen_y);

  [[nodiscard]] MovementController& movement_controller();
  [[nodiscard]] const MovementController& movement_controller() const;

  [[nodiscard]] BindingProfiles& binding_profiles() { return binding_profiles_; }
  [[nodiscard]] const BindingProfiles& binding_profiles() const { return binding_profiles_; }
  [[nodiscard]] actions::bindings::adapters::platform::
      SdlBindingInputRuntime& binding_input() {
    return binding_input_;
  }

  void SetSendPacketFn(SendPacketFn fn);

  void SetClientTimeFn(std::function<std::uint32_t()> fn);

  void SetBlockingLoadingEventPump(std::function<void()> pump);

  void SetFileLoader(world::WorldMap::LoadFileCallback callback);

  void SetPrefixFileLoader(
      std::function<std::vector<std::uint8_t>(const std::string&, std::size_t)>
          callback);
  void SetLoadingScreenArchivePathProbe(
      std::function<bool(const std::string&)> probe);

  void SetDbcLoader(const openwow::data::dbc::DbcLoader* dbc);

  void SetWindow(SDL_Window* window) { window_ = window; }

  void SetRendererContext(
      openwow::render::api::RendererContext* renderer_context);

 private:
  struct RenderResources;

  struct UnitGroundStateMemo {
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};
    std::uint64_t world_token{0u};
    UnitSoundGroundState state{};
  };
  std::unordered_map<std::uint64_t, UnitGroundStateMemo>
      unit_ground_state_memo_;
  friend UnitSoundGroundState ResolveUnitSoundGroundStateForLoop(
      GameLoop& loop, const CGUnit_C& unit, const float* event_position);

  struct GroundContactProbeMemo {
    std::array<float, 3> origin{};
    float max_distance{0.0f};
    std::uint64_t facet_revision{0u};
    CalcGroundPosCollisionResult result{};
  };
  std::unordered_map<std::uint64_t, GroundContactProbeMemo>
      ground_contact_probe_memo_;
  friend CalcGroundPosCollisionResult ResolveCalcGroundPosForLoop(
      GameLoop& loop, const CGUnit_C& unit,
      const std::array<float, 3>& origin, float max_distance);
  explicit GameLoop(
      openwow::ui::display::ProductionDisplaySettingsRuntime* runtime,
      openwow::render::TextureManager& texture_manager,
      openwow::render::m2::M2System& m2_system,
      openwow::audio::SoundRuntime& sound_runtime);

  void TickGlue(float dt);
  void TickLoading(float dt);
  void TickInWorld(float dt);

  void UpdateSelectionDecals();

  void SubmitUnitSelectionDecals(const CGUnit_C &target_unit,
                                 ObjectGuid target_guid,
                                 std::uint32_t now_ms);

  [[nodiscard]] float ResolveGameObjectRingRadius(
      const CGGameObject_C &game_object) const;
  [[nodiscard]] openwow::ui::game::WorldUiLifecycleOperations
  CreateWorldUiLifecycleOperations();
  bool StartWorldUiRuntime(
      openwow::ui::game::WorldUiGeneration generation,
      std::function<void(float)> progress_callback);
  void PrepareWorldUiForPlayerLeave(
      openwow::ui::game::WorldUiStopReason reason);
  void PrepareWorldUiForPlayerLogout(
      openwow::ui::game::WorldUiStopReason reason);
  void PersistWorldUiState(
      openwow::ui::game::WorldUiStopReason reason);
  void DetachWorldUiRuntimeCallbacks(
      openwow::ui::game::WorldUiGeneration generation);
  void DestroyWorldUiRuntime(
      openwow::ui::game::WorldUiGeneration generation,
      openwow::ui::game::WorldUiStopReason reason);
  void FireWorldUiLifecycleEvent(
      openwow::ui::game::WorldUiLifecycleEvent event);
  [[nodiscard]] bool HasLocalPlayerForWorldUi() const;
  bool PrepareLocalPlayerForWorldUi();
  void RunPreEnterLocalPlayerWorldUiSetup();
  void RunLocalPlayerWorldUiFanout();
  void RestoreWorldUiAccountData(
      openwow::ui::game::WorldUiGeneration generation,
      openwow::ui::game::WorldUiAccountDataSlot slot,
      openwow::ui::game::WorldUiAccountDataCompletion completion);
  void CancelWorldUiAccountData(
      openwow::ui::game::WorldUiGeneration generation);
  [[nodiscard]] bool IsWorldUiReloadBlockedByCinematic() const;
  void PrepareWorldUiForReload();
  void AttachWorldUiMacroPresentation();
  void DetachWorldUiMacroPresentation();
  void ShutdownCursorSurface();
  void ApplyFileLoaderBindings();
  std::string ResolveMapName(std::uint32_t map_id, const std::string& map_name) const;
  void ResetWorldStateUiRuntimeForEnterWorld();
  void ResetEnterWorldRuntimeState();
  void PrepareWorldEntryRuntime();
  void FinalizeWorldEntryRuntime();
  void WaitForTrialStartRacePreloadGate();
  void PrimeTransferPendingLoadingScreen(const TransferPendingInfo& pending);
  void HandleLoadingScreenProgress(
      openwow::screens::LoadingProgressInput input, float progress);
  void PresentBlockingLoadingFrame();
  [[nodiscard]] bool RenderLoadingScreenOverlay();
  void ShowLoadingScreen(std::uint32_t map_id);
  void HideLoadingScreen();
  void RefreshLoadingWorldEntryState(float dt);

  void UpdateLoadingTrackedPlayerState();
  [[nodiscard]] LoadingScreenWorldEntryGateState BuildLoadingScreenWorldEntryGateState() const;
  void ArmTransportWorldEntryHoldForRidingPlayer();
  [[nodiscard]] bool ShouldKeepLoadingScreenVisibleForWorldEntry() const;
  [[nodiscard]] bool TryCompleteLoadingScreenWorldEntry();
  std::size_t PumpWorldEntryProtocolControlPackets();

  void UpdateNetwork();
  void ProcessInput(float dt);
  void ReleaseClickToMove(WorldSession* session);

  void PublishMoverFramePose(float dt, std::uint32_t client_time_ms);
  void ResolveFrameCameraPose(float dt);

  void ApplyCameraTargetAlpha();

  std::uint64_t last_camera_alpha_target_{0};
  void UpdateSoundListenerForFrame();
  void BindMovementCollisionSource();
  void RenderWorld(float dt);
  void UpdateWorldFrameMouseover(float dt);
  void SyncWorldFrameCursorContext();
  void SyncWorldMouseoverCursor();
  void NoteUserActivity();
  void HandlePerFrameWorldMaintenance(std::uint32_t current_tick_ms);
  void SynchronizeZoneUiState();
  void ClearCorpseProximityState();
  void UpdateCorpseProximityState(bool force_refire_current_event = false);
  [[nodiscard]] bool IsCorpseProximityActive() const;
  [[nodiscard]] const char* GetCorpseProximityEventName() const;

  [[nodiscard]] bool CorpseProximityEventsSuppressed() const;
  void OnRendererDeviceWillReset() override;
  void OnRendererDeviceReady(
      openwow::render::api::DeviceGeneration generation) override;

  [[nodiscard]] WorldSession* world_session() const noexcept;

  openwow::render::TextureManager& texture_manager_;
  openwow::render::m2::M2System& m2_system_;
  openwow::audio::SoundRuntime& sound_runtime_;
  std::unique_ptr<RenderResources> render_resources_;
  openwow::render::WorldFrame world_frame_;
  openwow::render::WorldOverlayMetrics world_overlay_metrics_{};
  WorldEnvironmentState world_environment_;
  WorldScene world_scene_;
  InputController input_;
  MovementController offline_movement_;
  BindingProfiles binding_profiles_;
  actions::bindings::adapters::platform::SdlBindingInputRuntime
      binding_input_;
  TargetingSystem targeting_;
  ItemDefinitions item_definitions_;
  openwow::ui::game::WorldHeldCursorSource held_cursor_source_;
  openwow::ui::game::HeldCursorPresentation held_cursor_presentation_;
  actions::held_cursor::HeldCursor held_cursor_;
  openwow::ui::game::GameUIManager game_ui_;
  openwow::ui::game::SessionEventBridge event_bridge_;
  openwow::ui::game::ChatFrame chat_frame_;
  openwow::ui::game::GameLoadingScreen loading_screen_;
  openwow::render::FloatingTextRenderer& floating_text_;
  DeathManager death_manager_;
  CursorSurface cursor_manager_;
  CinematicPlayer cinematic_player_;
  openwow::render::PostProcess& post_process_;
  openwow::render::DebugDrawRenderer& debug_draw_renderer_;
  openwow::ui::game::MinimapIntegration minimap_;

  const openwow::ui::framexml::FrameRect* cached_minimap_rect_{nullptr};
  std::uint64_t cached_minimap_rect_generation_{
      std::numeric_limits<std::uint64_t>::max()};
  openwow::ui::game::LootFrame loot_frame_;
  IdleBillingController idle_billing_;
  AreaTriggerSystem area_trigger_system_;
  openwow::ui::game::ZoneUiState zone_ui_state_;
  openwow::ui::game::WorldUiSessionCommandAdapter
      world_ui_session_commands_;
  openwow::ui::game::WorldUiVoiceSettingsAdapter
      world_ui_entry_settings_;
  openwow::ui::game::WorldUiLifecycle world_ui_lifecycle_;

  CharacterWorldRuntime* character_world_runtime_{nullptr};
  openwow::net::PacketQueue* packet_queue_{nullptr};

  std::unordered_set<std::uint16_t> logged_unhandled_opcodes_;
  const openwow::data::dbc::DbcLoader* dbc_{nullptr};

  SDL_Window* window_{nullptr};
  openwow::render::api::RendererContext* renderer_context_{nullptr};
  bool renderer_observer_registered_{false};
  openwow::render::api::DeviceGeneration renderer_device_generation_{};

  SendPacketFn send_packet_fn_;

  std::function<std::uint32_t()> client_time_fn_;

  std::uint32_t prev_actions_{0};
  bool ctm_owns_auto_forward_{false};

  float ctm_facing_turn_rate_rad_per_sec_{0.0f};
  std::uint32_t ctm_driven_action_generation_{0};

  bool ctm_facing_aligned_once_{false};

  std::uint32_t ctm_no_progress_ticks_{0};

  bool ctm_has_last_tick_position_{false};
  float ctm_last_tick_x_{0.0f};
  float ctm_last_tick_y_{0.0f};
  float ctm_last_tick_z_{0.0f};

  SceneState state_{SceneState::kGlue};
  float player_x_{0.0f};
  float player_y_{0.0f};
  float player_z_{0.0f};
  float player_orientation_{0.0f};
  std::int32_t synchronized_zone_map_id_{-1};
  std::int32_t synchronized_zone_id_{-1};
  std::int32_t synchronized_area_id_{-1};
  std::uint32_t current_map_id_{0};
  std::string current_map_name_;
  int screen_width_{1280};
  int screen_height_{720};
  bool initialized_{false};

  bool prepared_world_entry_{false};

  bool corpse_proximity_active_{false};

  std::uint64_t prev_mouseover_guid_{0};

  LoadingScreenProgressPump loading_screen_progress_pump_;

  bool mouseover_cursor_ui_owned_{false};
  std::uint64_t loading_screen_render_submissions_{0};
  std::uint64_t loading_screen_self_presented_frames_{0};
  std::uint64_t loading_screen_coalesced_callbacks_{0};
  std::uint64_t blocking_loading_frame_number_{0};

  world::WorldMap::LoadFileCallback file_loader_;

  std::function<std::vector<std::uint8_t>(const std::string&, std::size_t)>
      prefix_file_loader_;
  std::function<bool(const std::string&)> loading_screen_archive_path_probe_;

  const openwow::vfs::VirtualFileSystem* vfs_{nullptr};
};

}

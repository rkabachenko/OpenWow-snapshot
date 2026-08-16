#pragma once

#include "openwow/core/login_state_handler.h"
#include "openwow/game/queue_position.h"
#include "openwow/net/wotlk/protocol/world_protocol.h"
#include "openwow/ui/addon_manager.h"
#include "openwow/ui/glue/glue_game_state.h"
#include "openwow/ui/glue/glue_script_events.h"
#include "openwow/ui/glue/glue_widget_runtime.h"
#include "openwow/ui/screens/character_select_screen.h"
#include "openwow/ui/screens/login_screen.h"
#include "openwow/ui/screens/realm_list_screen.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace openwow::net::wotlk {
class AuthProtocol;
}

namespace openwow::client {

struct GlueFlowContext {
  openwow::ui::glue::GlueGameState* game_state{nullptr};
  openwow::ui::glue::GlueWidgetRuntime* glue_widgets{nullptr};
  openwow::ui::screens::LoginScreen* login_screen{nullptr};
  openwow::ui::screens::RealmListScreen* realm_screen{nullptr};
  openwow::ui::screens::CharacterSelectScreen* character_screen{nullptr};
  openwow::net::wotlk::RealmSession* realm_session{nullptr};

  const openwow::vfs::VirtualFileSystem* addon_discovery_vfs{nullptr};
  std::string auth_host;
  std::uint16_t auth_port{0};
  std::string* auth_session_token{nullptr};
  bool* show_error{nullptr};

  float dt{0.016f};

  std::function<std::string(const std::string&)> resolve_glue_string;
  openwow::ui::glue::GlueEventCallback fire_glue_event;

  std::function<void(bool , const std::string& )> set_login_status;

  std::function<void()> after_login_success;

  std::function<bool(std::uint8_t class_id, std::uint8_t race_id,
                     float* out_xyz)> setup_char_login_camera;

  std::function<void(std::uint32_t map_id, float x, float y, float z,
                     std::uint8_t race_id)> enter_world_init;

  std::function<void()> abort_enter_world_init;

  std::function<void(std::uint32_t map_id, float x, float y, float z, float orientation)> after_enter_world;
};

struct GlueFlowState {

  enum class Phase : std::uint8_t {
    kIdle              = 0,
    kAuthInProgress    = 1,
    kRealmListPending  = 2,
    kWorldConnecting   = 3,
    kCharListPending   = 4,
    kCharCreating      = 5,
    kCharDeleting      = 6,
    kCharRenaming      = 7,
    kCharCustomizing   = 8,
    kEnteringWorld     = 9,
    kWorldEnter        = 10,
    kError             = 11,
    kDisconnecting     = 12,
    kCharDeclining     = 13,
  };

  Phase phase{Phase::kIdle};
  std::atomic_bool cancel_requested{false};

  std::shared_ptr<openwow::net::wotlk::AuthProtocol> auth_protocol;
  std::optional<std::future<openwow::net::wotlk::AuthResult>> auth_future;
  std::shared_future<std::vector<openwow::ui::AddonInfo>>
      addon_discovery_future;
  bool addon_discovery_publish_pending{false};
  bool addon_discovery_published{false};
  std::optional<std::future<openwow::net::wotlk::RealmListFetchResult>> realm_future;
  std::optional<std::future<openwow::net::wotlk::WorldAuthResult>> world_connect_future;
  std::optional<std::future<openwow::net::wotlk::CharacterListResult>> charlist_future;
  std::optional<std::future<openwow::net::wotlk::CharacterCreateResult>> char_create_future;
  std::optional<std::future<openwow::net::wotlk::CharacterDeleteResult>> char_delete_future;
  std::optional<std::future<openwow::net::wotlk::CharacterRenameResult>> char_rename_future;
  std::optional<std::future<openwow::net::wotlk::CharacterDeclinedNamesResult>> char_decline_future;
  std::optional<std::future<openwow::net::wotlk::CharacterCustomizeResult>> char_customize_future;
  std::optional<std::future<openwow::net::wotlk::CharacterFactionOrRaceChangeResult>> char_faction_change_future;
  std::optional<std::future<openwow::net::wotlk::CharacterFactionOrRaceChangeResult>> char_race_change_future;
  std::optional<std::future<openwow::net::wotlk::WorldEnterResult>> world_enter_future;

  std::string session_token;
  bool status_dialog_open{false};
  bool realm_fetch_transitions{false};
  bool pending_account_messages_available{false};
  bool matrix_challenge_announced{false};
  bool matrix_submission_observed{false};
  bool pin_challenge_announced{false};
  bool pin_submission_observed{false};
  bool token_challenge_announced{false};
  bool token_submission_observed{false};
  std::string last_status_text;
  openwow::core::LoginStateDialogHandler login_state_dialog_handler;

  std::shared_ptr<openwow::game::QueuePositionTracker>
      world_connect_queue_progress;
  bool world_connect_used_fcm_dialog{false};

  std::atomic_uint32_t realm_addon_callback_epoch{0};
  std::atomic_uint32_t pending_realm_addon_list_update_epoch{0};
  std::uint32_t next_realm_addon_callback_epoch{0};

  openwow::ui::glue::StatusDialogType status_dialog_type{
      openwow::ui::glue::StatusDialogType::kNone};

  std::string error_message;
  float error_display_timer{0.0f};

  float disconnect_timer{0.0f};
  bool disconnect_requested{false};
};

void PumpGlueFlow(GlueFlowContext& ctx, GlueFlowState& state);

void CancelGlueFlowNetworkOperations(GlueFlowContext& ctx,
                                     GlueFlowState& state);

void RequestDisconnect(GlueFlowContext& ctx, GlueFlowState& state);

void EnterErrorState(GlueFlowContext& ctx, GlueFlowState& state,
                     const std::string& message);

void RecoverFromError(GlueFlowContext& ctx, GlueFlowState& state);

}

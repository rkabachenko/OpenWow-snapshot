#pragma once

#include "openwow/net/wotlk/realm_list.h"
#include "openwow/net/wotlk/protocol/world_protocol.h"
#include "openwow/ui/glue/glue_lua_value.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::ui::glue {

namespace detail {
class LegacyAdlerRandom;
}

class GlueBackgroundController;
class GlueCharSelectScene;
class GlueModelFFXWidget;
using GlueEventCallback =
    std::function<void(const std::string &, const std::vector<GlueLuaValue> &)>;
using GlueStringResolverCallback =
    std::function<std::string(std::string_view)>;
using GlueRealmPacketSendCallback =
    std::function<bool(const openwow::net::wotlk::WorldPacket &)>;

enum class StatusDialogType : std::uint8_t {
  kNone = 0,
  kCancel = 1,
  kOkay = 2,
};

enum class ScanDllStatus : std::int32_t {
  kIdle = 0,
  kRunning = 1,
  kError = 2,
  kComplete = 3,
};

struct ScanDllState {

  ScanDllStatus status{ScanDllStatus::kIdle};
  bool finished{true};
  bool continue_anyway_blocked{false};
  std::string result_primary_text;
  std::string result_secondary_text;
};

struct GlueGameState {
  detail::LegacyAdlerRandom* customization_random{nullptr};
  struct LoginRequest {
    bool pending{false};
    std::string username;
    std::string password;
  };

  struct CharCreateRequest {
    bool pending{false};
    std::string name;
    std::uint8_t race{1};
    std::uint8_t cls{1};
    std::uint8_t gender{0};
    std::uint8_t skin{0};
    std::uint8_t face{0};
    std::uint8_t hair_style{0};
    std::uint8_t hair_color{0};
    std::uint8_t facial_hair{0};
  };

  struct CharDeleteRequest {
    bool pending{false};
    std::uint64_t guid{0};
  };

  struct CharRenameRequest {
    bool pending{false};
    std::uint64_t guid{0};
    std::string new_name;
  };

  struct CharDeclineRequest {
    bool pending{false};
    std::uint64_t guid{0};
    std::string base_name;
    std::array<std::string, 5> forms{};
  };

  struct CharCustomizeRequest {
    bool pending{false};
    std::uint64_t guid{0};
    std::string name;
    std::uint8_t gender{0};
    std::uint8_t skin{0};
    std::uint8_t face{0};
    std::uint8_t hair_style{0};
    std::uint8_t hair_color{0};
    std::uint8_t facial_hair{0};
  };

  struct CharFactionOrRaceChangeRequest {
    bool pending{false};
    std::uint64_t guid{0};
    std::string name;
    std::uint8_t gender{0};
    std::uint8_t skin{0};
    std::uint8_t face{0};
    std::uint8_t hair_style{0};
    std::uint8_t hair_color{0};
    std::uint8_t facial_hair{0};
    std::uint8_t race{0};
  };

  struct CreateCustomizationCacheEntry {
    int skin{0};
    int face{0};
    int hair_style{0};
    int hair_color{0};
    int facial_hair{0};
  };

  std::vector<openwow::net::wotlk::RealmInfo> realms;
  std::vector<openwow::net::wotlk::CharacterSummary> characters;
  bool connected{false};
  int selected_realm_index{-1};

  int selected_realm_category_actual_index{-1};
  std::array<int, 4> realm_sort_keys{{0, 1, 2, 3}};
  std::array<bool, 4> realm_sort_descending{{false, false, false, false}};

  void ResetRealmListCategoryState() {
    selected_realm_category_actual_index = -1;
    realm_sort_keys = {0, 1, 2, 3};
    realm_sort_descending = {false, false, false, false};
  }
  int selected_character_index{-1};
  int customize_source_character_index{-1};

  std::vector<std::string> changed_option_warnings;
  ScanDllState scan_dll;

  std::array<std::uint32_t, 12> race_class_restriction_masks{};
  LoginRequest login_request;
  CharCreateRequest char_create_request;
  CharDeleteRequest char_delete_request;
  CharRenameRequest char_rename_request;
  CharDeclineRequest char_decline_request;
  CharCustomizeRequest char_customize_request;
  CharFactionOrRaceChangeRequest char_faction_change_request;
  CharFactionOrRaceChangeRequest char_race_change_request;

  int create_race{1};
  int create_class{1};
  int create_sex{0};
  int create_skin{0};
  int create_skin_cycle_anchor{0};

  int create_face{0};
  int create_hair_style{0};
  int create_hair_color{0};
  int create_facial_hair{0};

  static constexpr std::size_t kCreateCustomizationCacheSlotCount = 24;
  std::array<std::optional<CreateCustomizationCacheEntry>,
             kCreateCustomizationCacheSlotCount> create_customization_cache{};

  void ClearCreateCustomizationCache() {
    create_customization_cache.fill(std::nullopt);
  }

  float create_facing{0.0f};
  float select_facing{0.0f};

  std::string char_select_background;

  std::weak_ptr<GlueModelFFXWidget> char_select_model_frame;

  std::string char_select_model_frame_name;
  std::weak_ptr<GlueModelFFXWidget> char_customize_model_frame;

  std::string char_customize_frame_name;
  std::string char_customize_background;

  std::string loading_status_text;
  std::string loading_zone_name;
  std::string loading_tip;

  std::string current_screen;

  bool wants_login{false};

  bool wants_cancel_auth_login{false};

  bool wants_cancel_login{false};

  bool wants_character_list_refresh{false};
  bool wants_realm_list_refresh{false};

  bool request_realm_list_show_dialog{false};

  bool request_realm_list_dialog_opened{false};

  bool wants_cancel_realm_list_query{false};

  bool wants_realm_list_dialog_cancelled{false};
  bool wants_quit{false};
  bool wants_enter_world{false};
  bool wants_world_connect{false};
  bool wants_create_character{false};
  bool wants_delete_character{false};
  bool wants_rename_character{false};
  bool wants_decline_character{false};
  bool wants_customize_character{false};
  bool wants_faction_change{false};
  bool wants_race_change{false};
  bool wants_dismiss_dialog{false};

  StatusDialogType status_dialog_type{StatusDialogType::kNone};

  std::array<std::uint8_t, 40> session_key_raw{};
  bool session_key_valid{false};

  GlueEventCallback fire_event;

  GlueStringResolverCallback resolve_glue_string;

  GlueRealmPacketSendCallback send_realm_packet;

  static void SecureClearString(std::string& value) {
    std::fill(value.begin(), value.end(), '\0');
    value.clear();
  }

  void StageLoginRequest(std::string username, std::string password) {
    login_request.pending = true;
    login_request.username = std::move(username);
    SecureClearString(login_request.password);
    login_request.password = password;
    SecureClearString(password);
  }

  [[nodiscard]] LoginRequest ConsumeLoginRequest() {
    LoginRequest request;
    request.pending = login_request.pending;
    request.username = std::move(login_request.username);
    request.password = login_request.password;
    ResetLoginRequest();
    return request;
  }

  void ResetLoginRequest() {
    login_request.pending = false;
    login_request.username.clear();
    SecureClearString(login_request.password);
  }

  GlueBackgroundController *background_controller{nullptr};

  GlueCharSelectScene *char_select_scene{nullptr};

  GlueCharSelectScene *char_customize_scene{nullptr};

  std::function<void(const std::string &, const std::string &)> on_screen_transition;
};

[[nodiscard]] inline const openwow::net::wotlk::CharacterSummary *
GetCustomizationSourceCharacter(const GlueGameState &state) {
  const int index = state.customize_source_character_index;
  if (index < 0 || index >= static_cast<int>(state.characters.size())) {
    return nullptr;
  }
  return &state.characters[static_cast<std::size_t>(index)];
}

}

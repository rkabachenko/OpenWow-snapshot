
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "openwow/foundation/text/ascii.h"

namespace openwow::ui::glue {

inline constexpr std::uint8_t kGlueEventCount = 41;

enum class GlueScriptEvent : std::uint8_t {
    SetGlueScreen             =  0,
    StartGlueMusic            =  1,
    DisconnectedFromServer    =  2,
    OpenStatusDialog          =  3,
    UpdateStatusDialog        =  4,
    CloseStatusDialog         =  5,
    AddonListUpdate           =  6,
    CharacterListUpdate       =  7,
    UpdateSelectedCharacter   =  8,
    OpenRealmList             =  9,
    GetPreferredRealmInfo     = 10,
    UpdateSelectedRace        = 11,
    SelectLastCharacter       = 12,
    SelectFirstCharacter      = 13,
    GlueScreenshotSucceeded   = 14,
    GlueScreenshotFailed      = 15,
    PatchUpdateProgress       = 16,
    PatchDownloaded           = 17,
    SuggestRealm              = 18,
    SuggestRealmWrongPvp      = 19,
    SuggestRealmWrongCategory = 20,
    ShowServerAlert           = 21,
    FramesLoaded              = 22,
    ForceRenameCharacter      = 23,
    ForceDeclineCharacter     = 24,
    ShowSurveyNotification    = 25,
    PlayerEnterPin            = 26,
    ClientAccountMismatch     = 27,
    PlayerEnterMatrix         = 28,
    ScandllError              = 29,
    ScandllDownloading        = 30,
    ScandllFinished           = 31,
    ServerSplitNotice         = 32,
    TimerAlert                = 33,
    AccountMessagesAvailable       = 34,
    AccountMessagesHeadersLoaded   = 35,
    AccountMessagesBodyLoaded      = 36,
    ClientTrial               = 37,
    PlayerEnterToken          = 38,
    GameAccountsUpdated       = 39,
    ClientConverted           = 40,
};

inline constexpr std::array<const char*, kGlueEventCount> kGlueEventNames = {
    "SET_GLUE_SCREEN",
    "START_GLUE_MUSIC",
    "DISCONNECTED_FROM_SERVER",
    "OPEN_STATUS_DIALOG",
    "UPDATE_STATUS_DIALOG",
    "CLOSE_STATUS_DIALOG",
    "ADDON_LIST_UPDATE",
    "CHARACTER_LIST_UPDATE",
    "UPDATE_SELECTED_CHARACTER",
    "OPEN_REALM_LIST",
    "GET_PREFERRED_REALM_INFO",
    "UPDATE_SELECTED_RACE",
    "SELECT_LAST_CHARACTER",
    "SELECT_FIRST_CHARACTER",
    "GLUE_SCREENSHOT_SUCCEEDED",
    "GLUE_SCREENSHOT_FAILED",
    "PATCH_UPDATE_PROGRESS",
    "PATCH_DOWNLOADED",
    "SUGGEST_REALM",
    "SUGGEST_REALM_WRONG_PVP",
    "SUGGEST_REALM_WRONG_CATEGORY",
    "SHOW_SERVER_ALERT",
    "FRAMES_LOADED",
    "FORCE_RENAME_CHARACTER",
    "FORCE_DECLINE_CHARACTER",
    "SHOW_SURVEY_NOTIFICATION",
    "PLAYER_ENTER_PIN",
    "CLIENT_ACCOUNT_MISMATCH",
    "PLAYER_ENTER_MATRIX",
    "SCANDLL_ERROR",
    "SCANDLL_DOWNLOADING",
    "SCANDLL_FINISHED",
    "SERVER_SPLIT_NOTICE",
    "TIMER_ALERT",
    "ACCOUNT_MESSAGES_AVAILABLE",
    "ACCOUNT_MESSAGES_HEADERS_LOADED",
    "ACCOUNT_MESSAGES_BODY_LOADED",
    "CLIENT_TRIAL",
    "PLAYER_ENTER_TOKEN",
    "GAME_ACCOUNTS_UPDATED",
    "CLIENT_CONVERTED",
};

inline const char* FindGlueFrameScriptEventName(
    const std::string_view event_name) noexcept {
  for (const char* known_name : kGlueEventNames) {
    if (openwow::text::EqualsIgnoreCaseAscii(event_name, known_name)) {
      return known_name;
    }
  }
  return nullptr;
}

inline bool IsGlueFrameScriptEventName(
    const std::string_view event_name) noexcept {
  return FindGlueFrameScriptEventName(event_name) != nullptr;
}

constexpr const char* GlueEventName(GlueScriptEvent ev) noexcept {
    const auto id = static_cast<std::uint8_t>(ev);
    return (id < kGlueEventCount) ? kGlueEventNames[id] : nullptr;
}

}

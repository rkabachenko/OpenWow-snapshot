
#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/core/client_init.h"
#include "openwow/core/storm_error.h"
#include "openwow/core/storm_string.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/platform/system/os_system_info.h"
#include "openwow/core/screenshot_system.h"
#include "openwow/data/async_file_read.h"
#include "openwow/data/streaming_init.h"
#include "openwow/game/localization.h"
#include "openwow/net/client_services.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/api/game_lua_api_sound.h"
#include "openwow/ui/game/script_cvar_lua.h"
#include "openwow/ui/glue/cgluemgr.h"
#include "openwow/ui/glue/glue_lua_api_internal.h"
#include "openwow/ui/glue/glue_lua_shared_handlers.h"
#include "openwow/ui/glue/legal_notice_sync.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/lua_client_environment.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/ui/lua_result_capacity.h"
#include "openwow/ui/lua_tick_count.h"
#include "openwow/ui/retail_client_build.h"
#include "openwow/ui/script_server_name.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <optional>
#include <random>
#include <string_view>

namespace openwow::ui::glue::detail {

namespace {

constexpr std::string_view kSavedAccountNameCVar = "accountName";
constexpr std::string_view kSavedAccountListCVar = "accountList";
constexpr std::size_t kBuildInfoPlatformBufferSize = 1024;

std::optional<openwow::ui::game::CVarSystem::CVarSnapshot> LookupGlueCVar(
    openwow::ui::game::CVarSystem &sys, const char *name) {
  return sys.LookupCVarByName(name != nullptr ? name : "");
}

int PushGlueCVarString(lua_State *state, const std::string_view cvar_name) {
  const auto value = openwow::ui::game::CVarSystem::Instance().GetCVar(
      std::string(cvar_name));
  lua_pushstring(state, value.c_str());
  return 1;
}

int SetGlueCVarFromLuaString(lua_State *state,
                             const std::string_view cvar_name,
                             const char *usage) {
  if (!lua_isstring(state, 1)) {
    return luaL_error(state, usage);
  }

  const char *value = lua_tostring(state, 1);
  (void)openwow::ui::game::CVarSystem::Instance().SetRegisteredCVarValue(
      std::string(cvar_name),
      value != nullptr ? std::string(value) : std::string());
  return 0;
}

}

const char *CreditsTextFilenameForVersion(int version_index) {
  if (version_index == 1) {
    return "credits.html";
  }
  if (version_index == 2) {
    return "credits_BC.html";
  }
  return "credits_LK.html";
}

std::string UnescapeMinimalHtmlEntities(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char ch = text[i];
    if (ch != '&') {
      out.push_back(ch);
      continue;
    }
    const std::size_t semi = text.find(';', i + 1);
    if (semi == std::string_view::npos || semi - i > 16) {
      out.push_back(ch);
      continue;
    }
    const auto entity = text.substr(i + 1, semi - i - 1);
    auto emit = [&](char v) {
      out.push_back(v);
      i = semi;
    };
    if (entity == "nbsp") {
      emit(' ');
      continue;
    }
    if (entity == "amp") {
      emit('&');
      continue;
    }
    if (entity == "lt") {
      emit('<');
      continue;
    }
    if (entity == "gt") {
      emit('>');
      continue;
    }
    if (entity == "quot") {
      emit('"');
      continue;
    }
    if (entity == "#39" || entity == "apos") {
      emit('\'');
      continue;
    }

    if (!entity.empty() && entity.front() == '#') {
      int base = 10;
      std::size_t start = 1;
      if (entity.size() >= 2 && (entity[1] == 'x' || entity[1] == 'X')) {
        base = 16;
        start = 2;
      }
      int value = 0;
      bool ok = false;
      for (std::size_t j = start; j < entity.size(); ++j) {
        const char e = entity[j];
        int digit = -1;
        if (e >= '0' && e <= '9')
          digit = e - '0';
        else if (base == 16 && e >= 'a' && e <= 'f')
          digit = 10 + (e - 'a');
        else if (base == 16 && e >= 'A' && e <= 'F')
          digit = 10 + (e - 'A');
        else {
          digit = -1;
        }
        if (digit < 0 || digit >= base) {
          ok = false;
          break;
        }
        ok = true;
        value = value * base + digit;
        if (value > 0x10FFFF) {
          ok = false;
          break;
        }
      }
      if (ok) {

        if (value >= 0x20 && value <= 0x7E) {
          emit(static_cast<char>(value));
          continue;
        }

        emit(' ');
        continue;
      }
    }
    out.push_back(ch);
  }
  return out;
}

std::string StripHtmlToPlainText(std::string_view html) {
  std::string out;
  out.reserve(html.size());
  bool in_tag = false;
  std::string tag;
  tag.reserve(32);

  auto flush_tag = [&]() {
    if (tag.empty()) {
      return;
    }
    std::string lower = tag;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "br" || lower == "br/" || lower == "/p" || lower == "p" || lower == "/div" ||
        lower == "div" || lower == "/tr" || lower == "tr") {
      if (out.empty() || out.back() != '\n') {
        out.push_back('\n');
      }
    }
    tag.clear();
  };

  for (std::size_t i = 0; i < html.size(); ++i) {
    const char ch = html[i];
    if (in_tag) {
      if (ch == '>') {
        in_tag = false;
        flush_tag();
        continue;
      }
      if (tag.size() < 64) {
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {

          continue;
        }
        tag.push_back(ch);
      }
      continue;
    }
    if (ch == '<') {
      in_tag = true;
      tag.clear();
      continue;
    }
    out.push_back(ch);
  }
  if (in_tag) {
    flush_tag();
  }

  out = UnescapeMinimalHtmlEntities(out);

  std::string normalized;
  normalized.reserve(out.size());
  bool last_space = false;
  int consecutive_newlines = 0;
  for (char ch : out) {
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      last_space = false;
      ++consecutive_newlines;
      if (consecutive_newlines <= 2) {
        normalized.push_back('\n');
      }
      continue;
    }
    consecutive_newlines = 0;
    if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
      if (!last_space) {
        normalized.push_back(' ');
        last_space = true;
      }
      continue;
    }
    last_space = false;
    normalized.push_back(ch);
  }
  return normalized;
}

std::string CollapseAccountPipes(std::string_view username) {
  std::string out;
  out.reserve(username.size());
  for (std::size_t i = 0; i < username.size(); ++i) {
    const char ch = username[i];
    if (ch == '|' && (i + 1) < username.size() && username[i + 1] == '|') {
      out.push_back('|');
      ++i;
      continue;
    }
    out.push_back(ch);
  }
  return out;
}

std::string ResolveGlueString(std::string_view key) {
  return openwow::game::Localization::Get().GetString(std::string(key),
                                                      std::string(key));
}

void FireOkayStatusDialog(GlueGameState& state, const std::string& message) {
  if (!state.fire_event) {
    return;
  }

  state.fire_event("OPEN_STATUS_DIALOG",
                   {MakeLuaString("OKAY"), MakeLuaString(message)});
}

void FireCancelStatusDialog(GlueGameState& state) {
  if (!state.fire_event) {
    return;
  }

  state.fire_event("OPEN_STATUS_DIALOG", {MakeLuaString("CANCEL")});
}

int LuaShowUIPanel(lua_State *state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (!name.empty()) {
    auto *glue_runtime = GetGlueRuntime(state);
    if (glue_runtime != nullptr) {
      glue_runtime->UpdateVisibilityCacheFor(name);
    }
    if (auto *widget_runtime = GetWidgetRuntime(state); widget_runtime != nullptr) {
      widget_runtime->Show(name);
    }
    if (glue_runtime != nullptr) {

      glue_runtime->PumpVisibilityTransitionsFor(name, 16);
    }
  }
  return 0;
}

int LuaHideUIPanel(lua_State *state) {
  const auto name = WidgetNameFromArg(state, 1);
  if (!name.empty()) {
    auto *glue_runtime = GetGlueRuntime(state);
    if (glue_runtime != nullptr) {
      glue_runtime->UpdateVisibilityCacheFor(name);
    }
    if (auto *widget_runtime = GetWidgetRuntime(state); widget_runtime != nullptr) {
      widget_runtime->Hide(name);
    }
    if (glue_runtime != nullptr) {
      glue_runtime->PumpVisibilityTransitionsFor(name, 16);
    }
  }
  return 0;
}

int LuaShowChangedOptionWarnings(lua_State *state) {

  const auto *game_state = GetGameState(state);
  if (game_state == nullptr || game_state->changed_option_warnings.empty()) {
    lua_pushnil(state);
    return 1;
  }

  lua_pushnumber(state, 1.0);
  return 1;
}

int LuaGetChangedOptionWarnings(lua_State *state) {
  const auto *game_state = GetGameState(state);
  if (game_state == nullptr || game_state->changed_option_warnings.empty()) {
    return 0;
  }

  const int result_count = openwow::ui::ReserveLuaResultCapacity(
      state, game_state->changed_option_warnings.size(),
      "changed option warnings");
  for (const auto &warning : game_state->changed_option_warnings) {
    lua_pushlstring(state, warning.data(), warning.size());
  }

  return result_count;
}

int LuaAcceptChangedOptionWarnings(lua_State *state) {
  if (auto *game_state = GetGameState(state); game_state != nullptr) {
    game_state->changed_option_warnings.clear();
  }
  return 0;
}

int LuaSetCurrentScreen(lua_State *state) {
  if (lua_isstring(state, 1) == 0) {
    return luaL_error(state, "Usage: SetCurrentScreen(\"screen\")");
  }

  const char *name = lua_tostring(state, 1);
  auto *gs = GetGameState(state);
  if (gs != nullptr) {
    CGlueMgr_SetGlueScreen(*gs, name != nullptr ? name : "");
  }

  if (!openwow::data::IsStreamingInitialized()) {
    (void)openwow::data::AsyncFileRead_WaitAll();
  }

  return 0;
}

int LuaPlayGlueMusic(lua_State *state) {
  if (!lua_isstring(state, 1)) {
    luaL_error(state, "Usage: PlayGlueMusic(\"filename\")");
  }

  const char *track = lua_tostring(state, 1);
  if (auto *host = GetGlueHost(state); host != nullptr) {
    host->PlayGlueMusic(track ? std::string(track) : std::string());
  }
  return 0;
}

int LuaPlayGlueAmbience(lua_State *state) {

  if (!lua_isstring(state, 1)) {
    luaL_error(state, "Usage: PlayGlueAmbience(\"sound name\", \"(optional)fade in time\")");
  }
  double fade = -1.0;
  if (lua_isnumber(state, 2)) {
    fade = lua_tonumber(state, 2);
  }
  const char *track = lua_tostring(state, 1);
  if (auto *host = GetGlueHost(state); host != nullptr) {
    host->PlayGlueAmbience(track ? std::string(track) : std::string(), fade);
  }
  return 0;
}

int LuaStopGlueAmbience(lua_State *state) {

  if (auto *host = GetGlueHost(state); host != nullptr) {
    host->StopGlueAmbience();
  }
  return 0;
}

int LuaStopGlueMusic(lua_State *state) {

  if (auto *host = GetGlueHost(state); host != nullptr) {
    host->StopGlueMusic();
  }
  return 0;
}

int LuaPlayCreditsMusic(lua_State *state) {
  if (!lua_isstring(state, 1)) {
    luaL_error(state, "Usage: PlayCreditsMusic( \"Sound kit name\" )");
  }

  const char *track = lua_tostring(state, 1);
  if (auto *host = GetGlueHost(state); host != nullptr) {
    host->PlayCreditsMusic(track ? std::string(track) : std::string());
  }
  return 0;
}

int LuaGetCursorPosition(lua_State *state) {
  int viewport_w = 0;
  int viewport_h = 0;
  if (auto *runtime = GetWidgetRuntime(state); runtime != nullptr) {
    viewport_w = runtime->viewport_width();
    viewport_h = runtime->viewport_height();
  }
  double x = 0.0;
  double y = 0.0;
  if (auto *host = GetGlueHost(state); host != nullptr) {
    const auto p = host->GetCursorPositionDdc(viewport_w, viewport_h);
    const auto projected = openwow::ui::ProjectBottomLeftPixelCursorToUiScript(
        static_cast<float>(p.first), static_cast<float>(p.second),
        static_cast<float>(viewport_h));
    x = projected.first;
    y = projected.second;
  }
  lua_pushnumber(state, static_cast<lua_Number>(x));
  lua_pushnumber(state, static_cast<lua_Number>(y));
  return 2;
}

int LuaIsShiftKeyDown(lua_State *state) {
  bool down = false;
  if (auto *host = GetGlueHost(state); host != nullptr) {
    down = host->IsShiftKeyDown();
  }
  lua_pushwowbool(state, down);
  return 1;
}

int LuaGetTime(lua_State *state) {
  return openwow::ui::LuaGetTickCountSeconds(state);
}

int LuaGetGameTime(lua_State *state) {
  const auto *const game_time = GetGameTimeData(state);
  lua_pushnumber(state, game_time != nullptr ? game_time->hour : -1);
  lua_pushnumber(state, game_time != nullptr ? game_time->minute : -1);
  return 2;
}

int LuaScriptFileAccessDenied(lua_State *state) {
  return luaL_error(state, "Access Denied");
}

int LuaGetScreenWidth(lua_State *state) {

  if (auto *runtime = GetWidgetRuntime(state); runtime != nullptr) {
    lua_pushnumber(
        state,
        static_cast<double>(openwow::ui::GetUiScriptScreenWidth(
            static_cast<float>(runtime->viewport_width()),
            static_cast<float>(runtime->viewport_height()))));
    return 1;
  }
  lua_pushnumber(
      state, static_cast<double>(openwow::ui::GetUiScriptScreenWidth()));
  return 1;
}

int LuaGetScreenHeight(lua_State *state) {

  if (auto *runtime = GetWidgetRuntime(state); runtime != nullptr) {
    (void)runtime;
    lua_pushnumber(state, static_cast<double>(openwow::ui::GetUiScriptScreenHeight()));
    return 1;
  }
  lua_pushnumber(
      state, static_cast<double>(openwow::ui::GetUiScriptScreenHeight()));
  return 1;
}

int LuaGetBuildInfo(lua_State *state) {

  const std::string version_label =
      openwow::game::ResolveLocalizedGlobalString(state, "VERSION", -1, 0);

  (void)openwow::game::ResolveLocalizedGlobalString(state, "RELEASE_BUILD", -1, 0);
  const std::string release_build_label =
      openwow::game::ResolveLocalizedGlobalString(state, "RELEASE_BUILD", -1, 0);
  std::array<char, kBuildInfoPlatformBufferSize> release_build_platform{};
  openwow::core::SStrPrintf(release_build_platform.data(),
                            release_build_platform.size(),
                            "%s,%s%s",
                            release_build_label.c_str(),
                            " Intel",
                            "");

  lua_pushstring(state, version_label.c_str());
  lua_pushstring(state, release_build_platform.data());
  lua_pushstring(state, openwow::ui::kRetailClientVersion);
  lua_pushstring(state, openwow::ui::kRetailClientBuildNumber);
  lua_pushstring(state, openwow::ui::kRetailClientBuildDate);
  return 5;
}

int LuaGetClientExpansionLevel(lua_State *state) {
  const auto expansion_level = static_cast<double>(openwow::core::GetExpansionLevel() + 1u);
  lua_pushnumber(state, expansion_level);
  return 1;
}

int LuaGetNumCharacters(lua_State *state) {
  const auto *gs = GetGameState(state);
  lua_pushnumber(state, gs != nullptr ? static_cast<double>(gs->characters.size()) : 0.0);
  return 1;
}

std::string LuaCheckModelFrameName(lua_State *state, const char *usage) {
  if (lua_isstring(state, 1) == 0) {
    luaL_error(state, "%s", usage);
  }

  const char *frame_name = lua_tostring(state, 1);
  return frame_name != nullptr ? std::string(frame_name) : std::string();
}

int LuaSetCharSelectModelFrame(lua_State *state) {

  const std::string frame_name =
      LuaCheckModelFrameName(state, "Usage: SetCharSelectModelFrame(\"frameName\")");
  auto *gs = GetGameState(state);
  auto *runtime = GetWidgetRuntime(state);
  if (gs != nullptr && runtime != nullptr && gs->background_controller != nullptr) {
    gs->background_controller->SetCharSelectModelFrame(*gs, *runtime, frame_name);
  }
  return 0;
}

int LuaIsMacClient(lua_State *state) {
  return openwow::ui::PushRetailLuaClientPlatformQuery(
      state, openwow::ui::LuaClientPlatform::kMac);
}

int LuaGetMovieResolution(lua_State *state) {
  auto &sys = openwow::ui::game::CVarSystem::Instance();
  const auto resolution = sys.LookupCVarByName("gxResolution");
  if (!resolution.has_value()) {

    openwow::core::ExitWithCode(1);
  }

  int width = 0;
  int height = 0;
  (void)std::sscanf(resolution->value.c_str(), "%dx%d", &width, &height);
  lua_pushnumber(state, static_cast<double>(width));
  return 1;
}

int LuaGetCVar(lua_State *state) {

  const char *key = ReadGlueStringArgWithUsage(state, 1, "Usage: GetCVar(\"cvar\")");
  auto &sys = openwow::ui::game::CVarSystem::Instance();
  const auto snapshot = LookupGlueCVar(sys, key);
  if (!snapshot.has_value()) {
    return luaL_error(state, "Couldn't find CVar named '%s'", key);
  }
  lua_pushstring(state, snapshot->value.c_str());
  return 1;
}

int LuaSetCVar(lua_State *state) {
  if (lua_isstring(state, 1) == 0) {
    return luaL_error(state, "Usage: SetCVar(\"cvar\", value [, \"scriptCvar\")");
  }

  const char *key = lua_tostring(state, 1);
  auto &sys = openwow::ui::game::CVarSystem::Instance();
  const auto snapshot = LookupGlueCVar(sys, key);
  if (!snapshot.has_value()) {
    return luaL_error(state, "Couldn't find CVar named '%s'", key);
  }
  const auto flags = snapshot->flags;
  if (openwow::ui::game::HasFlag(flags, openwow::ui::game::CVarFlags::ServerSent)) {
    return luaL_error(state, "\"%s\" is read-only", key);
  }

  const char *value = lua_tostring(state, 2);
  if (value == nullptr) {
    value = "0";
  }

  (void)sys.SetRegisteredCVarValueDirect(snapshot->registered_name, value);
  return 0;
}

int LuaGetCVarBool(lua_State *state) {
  const char *key = ReadGlueStringArgWithUsage(state, 1, "Usage: GetCVarBool(\"cvar\")");
  auto &sys = openwow::ui::game::CVarSystem::Instance();
  const auto snapshot = LookupGlueCVar(sys, key);
  lua_pushwowbool(state, snapshot.has_value() && sys.GetCVarBool(snapshot->registered_name));
  return 1;
}

int LuaGetCVarDefault(lua_State *state) {

  const char *key = ReadGlueStringArgWithUsage(state, 1, "Usage: GetCVarDefault(\"cvar\")");
  auto &sys = openwow::ui::game::CVarSystem::Instance();
  const auto snapshot = LookupGlueCVar(sys, key);
  if (!snapshot.has_value()) {
    return luaL_error(state, "Couldn't find CVar named '%s'", key);
  }
  if (!snapshot->has_default_value) {
    lua_pushnil(state);
  } else {
    lua_pushstring(state, snapshot->default_value.c_str());
  }
  return 1;
}

int LuaIsStreamingTrial(lua_State *) {

  return openwow::ui::ReturnExistingLuaTopWhen(
      openwow::data::IsOnlineModeActive());
}

int LuaGetServerName(lua_State *state) {
  std::optional<openwow::net::SelectedRealmScriptMetadata> selected_realm;
  const auto *gs = GetGameState(state);
  if (gs != nullptr && gs->selected_realm_index >= 0 &&
      gs->selected_realm_index < static_cast<int>(gs->realms.size())) {
    const auto &realm = gs->realms[static_cast<std::size_t>(gs->selected_realm_index)];
    selected_realm = {
        .category = realm.timezone,
        .realm_type = static_cast<std::uint32_t>(realm.type),
        .is_pvp_flag = realm.is_pvp_flag,
    };
  }

  const auto server_name =
      openwow::ui::BuildScriptServerNameResult(selected_realm, gs == nullptr);
  lua_pushstring(state, server_name.realm_name.c_str());
  lua_pushwowbool(state, server_name.player_killing_allowed);
  lua_pushwowbool(state, server_name.roleplaying);
  lua_pushwowbool(state, server_name.pvp_flag);
  return 4;
}

int LuaGetSavedAccountName(lua_State *state) {
  return PushGlueCVarString(state, kSavedAccountNameCVar);
}

int LuaSetSavedAccountName(lua_State *state) {

  return SetGlueCVarFromLuaString(
      state, kSavedAccountNameCVar,
      "Usage: SetSavedAccountName(\"accountName\")");
}

int LuaGetSavedAccountList(lua_State *state) {
  return PushGlueCVarString(state, kSavedAccountListCVar);
}

int LuaSetSavedAccountList(lua_State *state) {

  return SetGlueCVarFromLuaString(
      state, kSavedAccountListCVar,
      "Usage: SetSavedAccountList(\"accountList\")");
}

int LuaEULAAccepted(lua_State *state) {
  if (LegalNoticeState::Get().IsAccepted(LegalNoticeId::kEula)) {
    lua_pushnumber(state, 1.0);
  } else {
    lua_pushnil(state);
  }
  return 1;
}

int LuaTOSAccepted(lua_State *state) {

  if (LegalNoticeState::Get().IsAccepted(LegalNoticeId::kTos)) {
    lua_pushnumber(state, 1.0);
  } else {
    lua_pushnil(state);
  }
  return 1;
}

int LuaTerminationWithoutNoticeAccepted(lua_State *state) {

  if (LegalNoticeState::Get().IsAccepted(LegalNoticeId::kTerminationWithoutNotice)) {
    lua_pushnumber(state, 1.0);
  } else {
    lua_pushnil(state);
  }
  return 1;
}

int LuaIsScanDLLFinished(lua_State *state) {
  const auto *gs = GetGameState(state);
  lua_pushwowbool(state, gs == nullptr ? true : gs->scan_dll.finished);
  return 1;
}

int LuaScanningAccepted(lua_State *state) {
  if (LegalNoticeState::Get().IsAccepted(LegalNoticeId::kScanning)) {
    lua_pushnumber(state, 1.0);
  } else {
    lua_pushnil(state);
  }
  return 1;
}

int LuaContestAccepted(lua_State *state) {
  if (LegalNoticeState::Get().IsAccepted(LegalNoticeId::kContest)) {
    lua_pushnumber(state, 1.0);
  } else {
    lua_pushnil(state);
  }
  return 1;
}

int LuaSetCharCustomizeFrame(lua_State *state) {

  const std::string frame_name =
      LuaCheckModelFrameName(state, "Usage: SetCharCustomizeFrame(\"frameName\")");
  auto *gs = GetGameState(state);
  auto *runtime = GetWidgetRuntime(state);
  if (gs != nullptr && runtime != nullptr && gs->background_controller != nullptr) {
    gs->background_controller->SetCharCustomizeModelFrame(*gs, *runtime, frame_name);
  }
  return 0;
}

int LuaDefaultServerLogin(lua_State *state) {
  if (!lua_isstring(state, 1) || !lua_isstring(state, 2)) {
    return luaL_error(state, "Usage: DefaultServerLogin(\"accountName\", \"password\")");
  }

  auto *gs = GetGameState(state);
  if (gs == nullptr) {
    return 0;
  }

  if (!gs->scan_dll.finished
      || !LegalNoticeState::Get().IsAccepted(LegalNoticeId::kTos)
      || !LegalNoticeState::Get().IsAccepted(LegalNoticeId::kEula)
      || CGlueMgr_GetStateValue() != static_cast<int>(GlueState::kIdle)) {
    return 0;
  }

  const char *username_text = lua_tostring(state, 1);
  if (username_text == nullptr || *username_text == '\0') {
    gs->status_dialog_type = StatusDialogType::kOkay;
    FireOkayStatusDialog(*gs, ResolveGlueString("LOGIN_ENTER_NAME"));
    return 0;
  }

  const char *password_text = lua_tostring(state, 2);
  if (password_text == nullptr || *password_text == '\0') {
    gs->status_dialog_type = StatusDialogType::kOkay;
    FireOkayStatusDialog(*gs, ResolveGlueString("LOGIN_ENTER_PASSWORD"));
    return 0;
  }

  const std::string username = CollapseAccountPipes(std::string_view(username_text));
  gs->StageLoginRequest(username, password_text);
  gs->status_dialog_type = StatusDialogType::kCancel;
  CGlueMgr_SetStateValue(static_cast<int>(GlueState::kAuthenticating));
  openwow::net::ClientServices::Instance().Login(username, gs->login_request.password);
  FireCancelStatusDialog(*gs);
  gs->wants_login = true;
  return 0;
}

int LuaLaunchURL(lua_State *state) {
  if (!lua_isstring(state, 1)) {
    return luaL_error(state, "Usage: LaunchURL(\"URL\")");
  }
  const char *url = lua_tostring(state, 1);

  if (url != nullptr && url[0] != '\0') {
    if (auto *host = GetGlueHost(state); host != nullptr) {
      host->OpenUrl(url);
    }
  }
  return 0;
}

int LuaGetCreditsText(lua_State *state) {
  if (lua_isnumber(state, 1) == 0) {
    return luaL_error(state, "Usage: Script_GetCreditText(versionIndex)");
  }

  const int credits_type = static_cast<int>(lua_tonumber(state, 1));
  const auto *runtime = GetGlueRuntime(state);
  const auto *vfs = runtime != nullptr ? runtime->vfs() : nullptr;
  if (vfs == nullptr) {
    return 1;
  }

  const auto html = vfs->ReadTextFile(CreditsTextFilenameForVersion(credits_type));
  if (!html.has_value()) {

    return 1;
  }

  lua_pushstring(state, html->c_str());
  return 1;
}

int LuaAcceptEULA(lua_State *state) {
  (void)state;
  LegalNoticeState::Get().Accept(LegalNoticeId::kEula);
  return 0;
}

int LuaAcceptTOS(lua_State *state) {
  (void)state;
  LegalNoticeState::Get().Accept(LegalNoticeId::kTos);
  return 0;
}

int LuaAcceptTerminationWithoutNotice(lua_State *state) {
  (void)state;
  LegalNoticeState::Get().Accept(LegalNoticeId::kTerminationWithoutNotice);
  return 0;
}

int LuaAcceptScanning(lua_State *state) {
  (void)state;
  LegalNoticeState::Get().Accept(LegalNoticeId::kScanning);
  return 0;
}

int LuaAcceptContest(lua_State *state) {
  (void)state;
  LegalNoticeState::Get().Accept(LegalNoticeId::kContest);
  return 0;
}

int LuaQuitGame(lua_State *state) {
  (void)state;
  openwow::core::RequestClientShutdownWithErrorCode(0);
  return 0;
}

int LuaStopAllSFX(lua_State *state) {

  const float fade_out =
      static_cast<float>(openwow::ui::LuaToNumberOrZero(state, 1));
  auto* runtime = GetGlueRuntime(state);
  if (runtime == nullptr) return luaL_error(state, "glue sound runtime is unavailable");
  runtime->sound_runtime().StopAllSoundEffects(fade_out);
  return 0;
}

int LuaScreenshot(lua_State *state) {
  (void)state;
  (void)openwow::core::ScreenshotSystem::Instance().CaptureScreenshot(
      openwow::core::ScreenshotRequestDomain::GlueUi);
  return 0;
}

int LuaHideCursor(lua_State *state) {
  if (auto *host = GetGlueHost(state); host != nullptr) {
    host->SetCursorVisible(false);
  }
  return 0;
}

int LuaShowCursor(lua_State *state) {
  if (auto *host = GetGlueHost(state); host != nullptr) {
    host->SetCursorVisible(true);
  }
  return 0;
}

int LuaGetCVarAbsoluteMax(lua_State *state) {
  const char *key = ReadGlueStringArgWithUsage(state, 1, "Usage: GetCVarAbsoluteMax(\"cvar\")");
  return openwow::ui::game::detail::PushScriptCVarRangeByName(
      state, key, openwow::ui::game::detail::ScriptCVarLookupMode::kGlue,
      openwow::ui::ScriptCVarRangeQuery::kAbsoluteMax);
}

int LuaGetCVarAbsoluteMin(lua_State *state) {
  const char *key = ReadGlueStringArgWithUsage(state, 1, "Usage: GetCVarAbsoluteMin(\"cvar\")");
  return openwow::ui::game::detail::PushScriptCVarRangeByName(
      state, key, openwow::ui::game::detail::ScriptCVarLookupMode::kGlue,
      openwow::ui::ScriptCVarRangeQuery::kAbsoluteMin);
}

int LuaIsSystemSupported(lua_State *state) {
  const std::uint32_t cpu_features = openwow::core::OsSystemInfoDetector::Instance().Init();
  if ((cpu_features & openwow::core::kCpuFeature_SSE) != 0) {
    lua_pushnumber(state, 1.0);
  } else {
    lua_pushnil(state);
  }
  return 1;
}

}


#include "openwow/core/client_init.h"
#include "openwow/core/console.h"
#include "openwow/core/game_subsystems.h"
#include "openwow/core/gxcvar.h"
#include "openwow/core/storm_string.h"
#include "openwow/core/storm_thread.h"
#include "openwow/data/streaming_init.h"
#include "openwow/game/account_msg.h"
#include "openwow/game/account_data.h"
#include "openwow/game/battlenet_login.h"
#include "openwow/game/localization.h"
#include "openwow/game/name_declension.h"
#include "openwow/game/name_validation.h"
#include "openwow/net/adapters/presentation/char_enum_display.h"
#include "openwow/net/client_services.h"
#include "openwow/net/auth/login_matrix_challenge.h"
#include "openwow/net/login_patch_download.h"
#include "openwow/net/auth/login_pin_challenge.h"
#include "openwow/net/login_survey_download.h"
#include "openwow/net/login_survey_result.h"
#include "openwow/net/auth/login_token_challenge.h"
#include "openwow/net/realm_config_tables.h"
#include "openwow/network/protocol/wotlk/opcodes.h"
#include "openwow/net/wotlk/protocol/packet_sender.h"
#include "openwow/ui/addons_data.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/api/game_lua_api_sound.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/script_addon_lua.h"
#include "openwow/ui/game/script_cvar_lua.h"
#include "openwow/ui/glue/cgluemgr.h"
#include "openwow/ui/glue/character_creation.h"
#include "openwow/ui/glue/character_customization_randomizer.h"
#include "openwow/ui/glue/glue_charselect_scene.h"
#include "openwow/ui/glue/glue_lua_api_internal.h"
#include "openwow/ui/glue/glue_patch_apply.h"
#include "openwow/ui/glue/glue_script_events.h"
#include "openwow/ui/glue/legal_notice_sync.h"
#include "openwow/ui/glue/random_name_dictionary.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/lua_call_helpers.h"
#include "openwow/ui/lua_client_environment.h"
#include "openwow/ui/localized_text_lua.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/ui/name_declension_lua.h"
#include "openwow/ui/script_locale.h"
#include "openwow/ui/lua_result_capacity.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/vfs/sfile_core.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace openwow::ui::glue::detail {

using openwow::ui::glue::MakeLuaBool;
using openwow::ui::glue::MakeLuaNumber;
using openwow::ui::glue::MakeLuaString;

namespace {

int NormalizeLuaCharacterSelectionIndex(const double lua_index_one_based,
                                        const std::size_t character_count) {
  long long zero_based_index = -1;
  if (std::isfinite(lua_index_one_based) &&
      lua_index_one_based >= static_cast<double>(std::numeric_limits<int>::min()) &&
      lua_index_one_based <= static_cast<double>(std::numeric_limits<int>::max())) {
    const int truncated_index_one_based = static_cast<int>(lua_index_one_based);
    zero_based_index = static_cast<long long>(truncated_index_one_based) - 1;
    if (zero_based_index < -1 ||
        zero_based_index >= static_cast<long long>(character_count)) {
      zero_based_index = -1;
    }
  }

  int selected_index = zero_based_index < 0 ? 0 : static_cast<int>(zero_based_index);
  if (selected_index >= static_cast<int>(character_count)) {
    selected_index = 0;
  }
  return selected_index;
}

openwow::game::BattlenetLogin &RequireBattlenetLogin(lua_State *state) {
  auto &client_services = openwow::net::ClientServices::Instance();
  if (!client_services.HasLoginConnection()) {
    luaL_error(state, "No login available");
  }
  if (client_services.GetLoginConnectionType() != openwow::net::LoginConnectionType::kBattleNet) {
    luaL_error(state, "Wrong login type");
  }

  auto *login = client_services.GetBattlenetLogin();
  if (login == nullptr) {
    luaL_error(state, "No login available");
  }

  if (auto *game_state = GetGameState(state); game_state != nullptr) {
    const auto fire_event = game_state->fire_event;
    const auto resolve_string = game_state->resolve_glue_string;
    login->SetGlueEventCallback(
        [fire_event, resolve_string](const openwow::game::BattlenetGlueEvent event) {
          if (!fire_event) {
            return;
          }
          switch (event) {
          case openwow::game::BattlenetGlueEvent::kGameAccountsUpdated:
            fire_event("GAME_ACCOUNTS_UPDATED", {});
            break;
          case openwow::game::BattlenetGlueEvent::kSurveyRequested: {
            const std::string message = resolve_string
                                            ? resolve_string("LOGIN_STATE_SURVEY")
                                            : "LOGIN_STATE_SURVEY";
            fire_event("OPEN_STATUS_DIALOG",
                       {MakeLuaString("CANCEL"), MakeLuaString(message)});
            break;
          }
          }
        });
  }

  return *login;
}

std::uint32_t LuaNumberToZeroBasedU32Index(const lua_Number value) {

  return openwow::ui::ClampLuaNumberToU32(value) - 1u;
}

std::int64_t LuaNumberToZeroBasedI32Index(const lua_Number value) {

  return static_cast<std::int64_t>(openwow::ui::TruncateLuaNumberToI32(value)) - 1;
}

std::uint32_t ReadGameAccountIndex(lua_State *state, const char *usage) {
  if (lua_isnumber(state, 1) == 0) {
    luaL_error(state, "%s", usage);
  }

  return LuaNumberToZeroBasedU32Index(lua_tonumber(state, 1));
}

void ExecuteGxRestartConsoleCommand() {
  openwow::core::ida::Console_ExecuteGraphicsRestart();
}

void ApplyRestoreVideoDefaultsByMode(openwow::ui::game::CVarSystem &sys,
                                     const openwow::core::DisplayCallbackMode mode) {

  (void)openwow::core::ida::RefreshStartupGraphicsQualityProfileFromDetectedHardware();

  (void)sys.SetRegisteredCVarValueDirect("fixedFunction", "0");
  if (mode == openwow::core::DisplayCallbackMode::ResetGamma) {

    (void)openwow::core::ida::GxApplyRegisteredDefaultDisplayCVars();
    openwow::ui::game::ScriptEventDispatch::Get().FireEvent(
        openwow::ui::game::events::DISPLAY_SIZE_CHANGED);
    (void)openwow::core::ida::GxRestartCurrentDisplayMode();
  }

  openwow::core::DispatchVideoDefaultsModeCallbacks(static_cast<std::uint32_t>(mode));
}

void OpenCancelStatusDialog(GlueGameState *state) {
  if (state == nullptr) {
    return;
  }

  state->status_dialog_type = StatusDialogType::kCancel;
  if (state->fire_event) {
    state->fire_event("OPEN_STATUS_DIALOG", {MakeLuaString("CANCEL")});
  }
}

void ApplyRestoreVideoResolutionDefaults(openwow::ui::game::CVarSystem &sys) {

  ApplyRestoreVideoDefaultsByMode(sys, openwow::core::DisplayCallbackMode::ResetGamma);
}

void ApplyRestoreVideoStereoDefaults(openwow::ui::game::CVarSystem &sys) {

  ApplyRestoreVideoDefaultsByMode(sys, openwow::core::DisplayCallbackMode::ApplyStereo);
}

bool ParseLegacyBoolString(const char *text, bool default_value) {
  if (text == nullptr || text[0] == '\0') {
    return default_value;
  }

  switch (text[0]) {
  case '0':
  case 'F':
  case 'N':
  case 'f':
  case 'n':
    return false;
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9':
  case 'T':
  case 'Y':
  case 't':
  case 'y':
    return true;
  default:
    break;
  }

  if (openwow::text::EqualsIgnoreCaseAscii(text, "off") ||
      openwow::text::EqualsIgnoreCaseAscii(text, "disabled")) {
    return false;
  }
  if (openwow::text::EqualsIgnoreCaseAscii(text, "on") ||
      openwow::text::EqualsIgnoreCaseAscii(text, "enabled")) {
    return true;
  }
  return default_value;
}

bool ParseLegacyOptionalBool(lua_State *state, int index, bool default_value) {
  switch (lua_type(state, index)) {
  case LUA_TNIL:
    return false;
  case LUA_TBOOLEAN:
    return lua_toboolean(state, index) != 0;
  case LUA_TNUMBER:

    return openwow::ui::TruncateLuaNumberToI32(lua_tonumber(state, index)) != 0;
  case LUA_TSTRING:
    return ParseLegacyBoolString(lua_tostring(state, index), default_value);
  default:
    return default_value;
  }
}

LegacyAdlerRandom &RequireGlueCustomizationRandom(lua_State* state) {
  auto* const game_state = GetGameState(state);
  if (game_state == nullptr || game_state->customization_random == nullptr) {
    luaL_error(state, "Glue random runtime is not bound");
  }
  return *game_state->customization_random;
}

struct RandomNameDictionaryCache {
  const openwow::data::dbc::DbcLoader *dbc{nullptr};
  std::uint32_t race_id{0};
  std::uint32_t sex{0};
  RandomNameDictionary dictionary;

  void RebuildIfNeeded(const openwow::data::dbc::DbcLoader *loader, const std::uint32_t race,
                       const std::uint32_t gender) {
    if (dbc == loader && race_id == race && sex == gender) {
      return;
    }

    dbc = loader;
    race_id = race;
    sex = gender;
    if (dbc == nullptr) {
      dictionary = {};
      return;
    }

    dictionary.Rebuild(dbc->name_gen(), race_id, sex);
  }
};

bool IsRandomNameAcceptedByGlue(const std::string &name) {
  if (name.empty()) {
    return true;
  }

  const auto length = name.size();
  if (length < 4u || length > 10u) {
    return false;
  }

  return openwow::game::ValidateGlueCharacterName(name) ==
         openwow::game::NameValidationResult::kOk;
}

GlueSpawnProcessFn &QuitGameAndRunLauncherSpawnProcess() {
  static GlueSpawnProcessFn spawn_process = &openwow::core::SThread_SpawnProcess;
  return spawn_process;
}

void QuitGameAndRunLauncherImpl() {
  openwow::core::RequestClientShutdownWithErrorCode(0);
#if defined(__APPLE__)
  constexpr const char *kRetailLauncherApplication = "World of Warcraft Launcher.app";
#else
  constexpr const char *kRetailLauncherApplication = "Launcher.exe";
#endif
  (void)QuitGameAndRunLauncherSpawnProcess()(kRetailLauncherApplication, nullptr, 0, 0);
}

void SendRealmSplitPacket(lua_State *state, const std::uint32_t split_state) {
  (void)SendGlueRealmPacket(
      state, openwow::net::wotlk::PacketSender::BuildRealmSplit(split_state));
}

struct RealmLoadStats {
  double mean{1.0};
  double spread{0.0};
};

struct RealmDisplayBucket {
  std::size_t actual_index{0};
  const openwow::net::RealmCategoryRecord *category{nullptr};
  std::vector<std::size_t> realm_indices;
};

struct RealmDisplayLayout {
  std::vector<openwow::net::RealmCategoryRecord> categories;
  std::vector<RealmDisplayBucket> buckets;
  std::vector<std::size_t> flat_realm_indices;
  bool has_category_registry{false};
};

struct RealmSortContext {
  const GlueGameState *game_state{nullptr};
  RealmLoadStats load_stats{};
};

thread_local const RealmSortContext *g_realm_sort_context = nullptr;

const char *RealmCategoryNameOrUnknown(const openwow::net::RealmCategoryRecord *category) {
  if (category == nullptr) {
    return "UNKNOWN";
  }
  return category->name.c_str();
}

std::optional<std::size_t>
FindRealmCategoryActualIndex(const std::vector<openwow::net::RealmCategoryRecord> &categories,
                             const std::uint8_t category_id) {
  for (std::size_t i = 0; i < categories.size(); ++i) {
    if (categories[i].id == category_id) {
      return i;
    }
  }
  return std::nullopt;
}

RealmLoadStats ComputeRealmLoadStats(const GlueGameState &state) {
  RealmLoadStats stats{};
  if (state.realms.size() <= 1u) {
    return stats;
  }

  double population_sum = 0.0;
  for (const auto &realm : state.realms) {
    population_sum += static_cast<double>(realm.population);
  }

  stats.mean = population_sum / static_cast<double>(state.realms.size());

  double squared_deviation_sum = 0.0;
  for (const auto &realm : state.realms) {
    const double delta = static_cast<double>(realm.population) - stats.mean;
    squared_deviation_sum += delta * delta;
  }

  const double sample_variance =
      squared_deviation_sum / static_cast<double>(state.realms.size() - 1u);
  stats.spread = std::sqrt(sample_variance) * 0.60000002384185791;
  return stats;
}

double ComputeRealmLoadValue(const openwow::net::wotlk::RealmInfo &realm,
                             const RealmLoadStats &stats) {
  if (realm.is_full) {
    return 2.0;
  }
  if (realm.is_new) {
    return -3.0;
  }
  if (realm.is_recommended) {
    return -2.0;
  }

  const double population = static_cast<double>(realm.population);
  if (population < (stats.mean - stats.spread)) {
    return -1.0;
  }
  if (population > (stats.mean + stats.spread)) {
    return 1.0;
  }
  return 0.0;
}

struct RealmTypePreferenceFlags {
  bool wants_pvp{false};
  bool wants_rp{false};
};

RealmTypePreferenceFlags GetRealmTypePreferenceFlags(
    const openwow::net::wotlk::RealmType type) {
  const auto config = openwow::net::RealmConfigTables::Get().FindRealmTypeConfig(
      static_cast<std::uint32_t>(type));
  if (!config.has_value()) {
    return {};
  }
  return {
      .wants_pvp = config->player_killing_allowed,
      .wants_rp = config->roleplaying,
  };
}

int CompareRealmMode(const openwow::net::wotlk::RealmInfo &left,
                     const openwow::net::wotlk::RealmInfo &right) {
  const auto left_mode = GetRealmTypePreferenceFlags(left.type);
  const auto right_mode = GetRealmTypePreferenceFlags(right.type);

  if (left_mode.wants_rp != right_mode.wants_rp) {
    return left_mode.wants_rp ? 1 : -1;
  }
  if (left_mode.wants_pvp != right_mode.wants_pvp) {
    return left_mode.wants_pvp ? 1 : -1;
  }
  return 0;
}

int CompareRealmIndices(const std::size_t left_index, const std::size_t right_index,
                        const RealmSortContext &context) {
  const auto &left = context.game_state->realms[left_index];
  const auto &right = context.game_state->realms[right_index];

  for (std::size_t position = 0; position < context.game_state->realm_sort_keys.size();
       ++position) {
    int comparison = 0;

    switch (context.game_state->realm_sort_keys[position]) {
    case 0:
      if (left.num_characters == 0 && right.num_characters == 0) {
        comparison = 0;
      } else if (left.num_characters > right.num_characters) {
        comparison = -1;
      } else {
        comparison = 1;
      }
      break;
    case 1: {
      const double left_load = ComputeRealmLoadValue(left, context.load_stats);
      const double right_load = ComputeRealmLoadValue(right, context.load_stats);
      if (left_load > right_load) {
        comparison = 1;
      } else if (left_load < right_load) {
        comparison = -1;
      }
      break;
    }
    case 2:
      comparison =
          openwow::core::SStrCmpUTF8NoCase(left.name.c_str(), right.name.c_str(), 0x7FFFFFFF);
      break;
    case 3:
      comparison = CompareRealmMode(left, right);
      break;
    default:
      break;
    }

    if (comparison != 0) {
      if (context.game_state->realm_sort_descending[position]) {
        comparison = -comparison;
      }
      return comparison;
    }
  }

  return 0;
}

int RealmDisplayQsortComparator(const void *left, const void *right) {
  if (g_realm_sort_context == nullptr) {
    return 0;
  }

  const auto left_index = *static_cast<const std::size_t *>(left);
  const auto right_index = *static_cast<const std::size_t *>(right);
  return CompareRealmIndices(left_index, right_index, *g_realm_sort_context);
}

RealmDisplayLayout BuildRealmDisplayLayout(const GlueGameState &state) {
  RealmDisplayLayout layout;
  layout.categories = openwow::net::RealmConfigTables::Get().Categories();
  layout.has_category_registry = !layout.categories.empty();

  if (layout.has_category_registry) {
    layout.buckets.resize(layout.categories.size());
    for (std::size_t i = 0; i < layout.categories.size(); ++i) {
      layout.buckets[i].actual_index = i;
      layout.buckets[i].category = &layout.categories[i];
    }

    for (std::size_t realm_index = 0; realm_index < state.realms.size(); ++realm_index) {
      const auto actual_index =
          FindRealmCategoryActualIndex(layout.categories, state.realms[realm_index].timezone);
      if (!actual_index.has_value()) {
        continue;
      }
      layout.buckets[*actual_index].realm_indices.push_back(realm_index);
    }
  } else {
    RealmDisplayBucket bucket;
    bucket.actual_index = 0;
    bucket.realm_indices.reserve(state.realms.size());
    for (std::size_t realm_index = 0; realm_index < state.realms.size(); ++realm_index) {
      bucket.realm_indices.push_back(realm_index);
    }
    layout.buckets.push_back(std::move(bucket));
  }

  const RealmSortContext context{
      .game_state = &state,
      .load_stats = ComputeRealmLoadStats(state),
  };

  g_realm_sort_context = &context;
  for (auto &bucket : layout.buckets) {
    if (bucket.realm_indices.size() > 1u) {
      std::qsort(bucket.realm_indices.data(), bucket.realm_indices.size(), sizeof(std::size_t),
                 RealmDisplayQsortComparator);
    }
    if (!bucket.realm_indices.empty()) {
      layout.flat_realm_indices.insert(layout.flat_realm_indices.end(),
                                       bucket.realm_indices.begin(), bucket.realm_indices.end());
    }
  }
  g_realm_sort_context = nullptr;

  return layout;
}

std::size_t MapVisibleCategoryIndex(const RealmDisplayLayout &layout,
                                    const std::int64_t visible_index) {
  if (visible_index < 0) {
    return 0;
  }

  int visible_category = -1;
  for (const auto &bucket : layout.buckets) {
    if (bucket.realm_indices.empty()) {
      continue;
    }

    ++visible_category;
    if (visible_category == visible_index) {
      return bucket.actual_index;
    }
  }

  return 0;
}

const RealmDisplayBucket *FindRealmDisplayBucket(const RealmDisplayLayout &layout,
                                                 const std::size_t actual_index) {
  if (actual_index >= layout.buckets.size()) {
    return nullptr;
  }
  return &layout.buckets[actual_index];
}

const openwow::net::RealmCategoryRecord *
ResolveVisibleRealmCategory(const RealmDisplayLayout &layout, const int lua_category_index) {
  const auto *bucket = FindRealmDisplayBucket(
      layout, MapVisibleCategoryIndex(layout,
                                      static_cast<std::int64_t>(lua_category_index) - 1));
  if (bucket == nullptr) {
    return nullptr;
  }
  return bucket->category;
}

bool RealmBlockedByPreferredSuggestionFlags(const openwow::net::wotlk::RealmInfo &realm) {

  return realm.is_offline || realm.is_pvp_flag;
}

bool RealmMatchesPreferredSuggestion(const openwow::net::wotlk::RealmInfo &realm,
                                     const bool wants_pvp, const bool wants_rp) {
  const auto realm_flags = GetRealmTypePreferenceFlags(realm.type);
  return realm_flags.wants_pvp == wants_pvp && realm_flags.wants_rp == wants_rp;
}

std::optional<std::size_t> FindPreferredRealmSuggestion(const GlueGameState &state,
                                                        const RealmDisplayLayout &layout,
                                                        const std::size_t actual_category_index,
                                                        const bool wants_pvp, const bool wants_rp) {
  const auto *bucket = FindRealmDisplayBucket(layout, actual_category_index);
  if (bucket == nullptr) {
    return std::nullopt;
  }

  std::optional<std::size_t> full_match;
  for (std::size_t bucket_index = 0; bucket_index < bucket->realm_indices.size(); ++bucket_index) {
    const auto realm_index = bucket->realm_indices[bucket_index];
    const auto &realm = state.realms[realm_index];
    if (RealmBlockedByPreferredSuggestionFlags(realm)) {
      continue;
    }
    if (realm.is_new) {
      return bucket_index;
    }
    if (!RealmMatchesPreferredSuggestion(realm, wants_pvp, wants_rp)) {
      continue;
    }
    if (!realm.is_full) {
      return bucket_index;
    }

    full_match = bucket_index;
  }

  return full_match;
}

bool IsCurrentClientLocaleAllowedForRealmCategory(
    const openwow::net::RealmCategoryRecord &category) {
  if (category.locale_mask == 0u) {
    return true;
  }

  const auto locale_index =
      static_cast<std::uint32_t>(openwow::net::ClientServices::Instance().GetCurrentLocale());
  if (locale_index >= 32u) {
    return false;
  }

  return (category.locale_mask & (1u << locale_index)) != 0u;
}

int PushOneOrNoResults(lua_State *state, const bool condition) {
  if (!condition) {
    return 0;
  }

  lua_pushnumber(state, 1.0);
  return 1;
}

void PushEmptyRealmInfoTuple(lua_State *state) {
  lua_pushnil(state);
  lua_pushnumber(state, 0.0);
  lua_pushnil(state);
  lua_pushnil(state);
  lua_pushnil(state);
  lua_pushnil(state);
  lua_pushnil(state);
  lua_pushnumber(state, 0.0);
  lua_pushnil(state);
  lua_pushnil(state);
  lua_pushnil(state);
  lua_pushnil(state);
  lua_pushnil(state);
  lua_pushnil(state);
}

void UpdateRealmSortState(GlueGameState &state, const int sort_type) {
  std::size_t position = state.realm_sort_keys.size();
  for (std::size_t i = 0; i < state.realm_sort_keys.size(); ++i) {
    if (state.realm_sort_keys[i] == sort_type) {
      position = i;
      break;
    }
  }

  if (position >= state.realm_sort_keys.size()) {
    return;
  }

  bool descending = state.realm_sort_descending[position];
  if (position == 0u) {
    descending = !descending;
  }

  for (std::size_t i = position; i > 0u; --i) {
    state.realm_sort_keys[i] = state.realm_sort_keys[i - 1u];
    state.realm_sort_descending[i] = state.realm_sort_descending[i - 1u];
  }

  state.realm_sort_keys[0] = sort_type;
  state.realm_sort_descending[0] = descending;
}

}

int LuaGetNumRealms(lua_State *state) {
  const auto *gs = GetGameState(state);
  if (gs == nullptr) {
    lua_pushnumber(state, 0.0);
    return 1;
  }

  const auto layout = BuildRealmDisplayLayout(*gs);
  if (lua_isnumber(state, 1) != 0) {
    const std::int32_t category =
        openwow::ui::TruncateLuaNumberToI32(lua_tonumber(state, 1));
    const auto actual_index = MapVisibleCategoryIndex(
        layout, static_cast<std::int64_t>(category) - 1);
    const auto *bucket = FindRealmDisplayBucket(layout, actual_index);
    lua_pushnumber(state,
                   static_cast<double>(bucket != nullptr ? bucket->realm_indices.size() : 0u));
    return 1;
  }

  lua_pushnumber(state, static_cast<double>(layout.flat_realm_indices.size()));
  return 1;
}

namespace {

void PushRealmInfoTuple(lua_State *state, const GlueGameState &gs,
                        const openwow::net::wotlk::RealmInfo &realm) {
  const auto realm_type_flags = GetRealmTypePreferenceFlags(realm.type);
  const double load = ComputeRealmLoadValue(realm, ComputeRealmLoadStats(gs));
  const char *current_realm_name = openwow::net::GetRealmName();

  lua_pushstring(state, realm.name.c_str());
  lua_pushnumber(state, static_cast<double>(realm.num_characters));
  if (!openwow::data::IsOnlineModeActive() && realm.is_offline) {
    lua_pushnumber(state, 1.0);
  } else {
    lua_pushnil(state);
  }
  lua_pushwowbool(state, realm.is_pvp_flag);
  if (current_realm_name != nullptr && current_realm_name[0] != '\0' &&
      openwow::core::SStrCmpUTF8NoCase(realm.name.c_str(), current_realm_name, 0x7FFFFFFF) == 0) {
    lua_pushnumber(state, 1.0);
  } else {
    lua_pushnil(state);
  }
  lua_pushwowbool(state, realm_type_flags.wants_pvp);
  lua_pushwowbool(state, realm_type_flags.wants_rp);
  lua_pushnumber(state, load);
  lua_pushwowbool(state, realm.locked);

  if (realm.has_version_data) {
    lua_pushnumber(state, static_cast<double>(realm.version_major));
    lua_pushnumber(state, static_cast<double>(realm.version_minor));
    lua_pushnumber(state, static_cast<double>(realm.version_revision));
    lua_pushnumber(state, static_cast<double>(realm.version_build));
    lua_pushnumber(state, static_cast<double>(
                              static_cast<std::uint32_t>(realm.type)));
  } else {
    lua_pushnil(state);
    lua_pushnil(state);
    lua_pushnil(state);
    lua_pushnil(state);
    lua_pushnil(state);
  }
}

std::optional<std::size_t> ResolveFlatRealmIndex(const RealmDisplayLayout &layout,
                                                 const std::uint32_t one_based_index) {
  if (layout.buckets.empty()) {
    return std::nullopt;
  }

  std::uint32_t remaining = one_based_index - 1u;
  std::optional<std::size_t> found;
  for (const auto &bucket : layout.buckets) {
    const auto count = static_cast<std::uint32_t>(bucket.realm_indices.size());
    if (remaining < count) {
      found = bucket.realm_indices[remaining];
    } else {
      remaining -= count;
    }
  }
  return found;
}

}

int LuaGetRealmInfo(lua_State *state) {
  if (lua_isnumber(state, 1) == 0) {
    return luaL_error(state, "Usage: GetRealmInfo(category, index)");
  }

  const auto *gs = GetGameState(state);
  if (gs == nullptr) {
    PushEmptyRealmInfoTuple(state);
    return 14;
  }

  const auto layout = BuildRealmDisplayLayout(*gs);
  const bool category_mode = lua_isnumber(state, 2) != 0;

  const openwow::net::wotlk::RealmInfo *realm = nullptr;
  if (category_mode) {
    const std::int32_t category =
        openwow::ui::TruncateLuaNumberToI32(lua_tonumber(state, 1));
    const std::uint32_t realm_index =
        LuaNumberToZeroBasedU32Index(lua_tonumber(state, 2));
    const auto actual_index = MapVisibleCategoryIndex(
        layout, static_cast<std::int64_t>(category) - 1);
    const auto *bucket = FindRealmDisplayBucket(layout, actual_index);
    if (bucket != nullptr &&
        static_cast<std::size_t>(realm_index) < bucket->realm_indices.size()) {
      realm = &gs->realms[bucket->realm_indices[static_cast<std::size_t>(realm_index)]];
    }
  } else if (const auto flat_index = ResolveFlatRealmIndex(
                 layout, openwow::ui::ClampLuaNumberToU32(lua_tonumber(state, 1)));
             flat_index.has_value()) {
    realm = &gs->realms[*flat_index];
  }

  if (realm == nullptr) {
    PushEmptyRealmInfoTuple(state);
    return 14;
  }

  PushRealmInfoTuple(state, *gs, *realm);
  return 14;
}

int LuaGetRealmCategories(lua_State *state) {
  const auto *gs = GetGameState(state);
  if (gs == nullptr) {
    lua_pushstring(state, "UNKNOWN");
    return 1;
  }

  const auto layout = BuildRealmDisplayLayout(*gs);
  int pushed = 0;
  for (const auto &bucket : layout.buckets) {
    if (bucket.realm_indices.empty()) {
      continue;
    }
    lua_pushstring(state, RealmCategoryNameOrUnknown(bucket.category));
    ++pushed;
  }

  if (pushed != 0) {
    return pushed;
  }

  if (!layout.buckets.empty()) {
    lua_pushstring(state, RealmCategoryNameOrUnknown(layout.buckets.front().category));
  } else {
    lua_pushstring(state, "UNKNOWN");
  }
  return 1;
}

int LuaRequestRealmList(lua_State *state) {
  auto *gs = GetGameState(state);
  if (gs != nullptr) {
    openwow::ui::glue::CGlueMgr_RequestRealmList(*gs, ParseLegacyOptionalBool(state, 1, false));
  }
  return 0;
}

int LuaCancelRealmListQuery(lua_State *state) {
  auto *gs = GetGameState(state);
  if (gs != nullptr) {
    gs->wants_cancel_realm_list_query = true;
  }
  return 0;
}

int LuaRealmListUpdateRate(lua_State *state) {
  const auto update_rate = openwow::net::ClientServices::Instance().GetLoginConnectionType() ==
                                   openwow::net::LoginConnectionType::kBattleNet
                               ? 4.0
                               : 5.0;
  lua_pushnumber(state, update_rate);
  return 1;
}

int LuaChangeRealm(lua_State *state) {
  if (lua_isnumber(state, 1) == 0) {
    return luaL_error(state, "Usage: ChangeRealm(category, index)");
  }

  auto *gs = GetGameState(state);
  if (gs == nullptr) {
    return 0;
  }

  const auto layout = BuildRealmDisplayLayout(*gs);
  const bool category_mode = lua_isnumber(state, 2) != 0;
  std::optional<std::size_t> selected_realm_index;

  if (category_mode) {
    const std::int32_t category =
        openwow::ui::TruncateLuaNumberToI32(lua_tonumber(state, 1));
    const auto actual_index = MapVisibleCategoryIndex(
        layout, static_cast<std::int64_t>(category) - 1);
    gs->selected_realm_category_actual_index = static_cast<int>(actual_index);

    const auto *bucket = FindRealmDisplayBucket(layout, actual_index);
    const std::uint32_t realm_index =
        LuaNumberToZeroBasedU32Index(lua_tonumber(state, 2));
    if (bucket != nullptr &&
        static_cast<std::size_t>(realm_index) < bucket->realm_indices.size()) {
      selected_realm_index = bucket->realm_indices[static_cast<std::size_t>(realm_index)];
    }
  } else {

    selected_realm_index = ResolveFlatRealmIndex(
        layout, openwow::ui::ClampLuaNumberToU32(lua_tonumber(state, 1)));
  }

  if (selected_realm_index.has_value()) {
    gs->selected_realm_index = static_cast<int>(*selected_realm_index);
    openwow::ui::glue::CGlueMgr_ConnectToRealm(*gs);
  }

  return 0;
}

int LuaRealmListDialogCancelled(lua_State *state) {
  auto *gs = GetGameState(state);
  if (gs != nullptr && openwow::text::ToLowerAscii(gs->current_screen) == "login") {
    gs->wants_realm_list_dialog_cancelled = true;
  }
  return 0;
}

std::string_view LookupGlueRaceDisplayName(lua_State *state, int race_id, int sex_id);
std::string_view LookupGlueClassDisplayName(lua_State *state, int class_id, int sex_id);
std::string_view LookupGlueAreaDisplayName(lua_State *state, std::uint32_t area_id);
std::uint8_t LookupGlueRaceRequiredExpansionLevel(lua_State *state, int race_id);
void PushLuaStringView(lua_State *state, std::string_view value);

int LuaGetCharacterInfo(lua_State *state) {
  if (lua_isnumber(state, 1) == 0) {
    luaL_error(state, "Usage: GetCharacterInfo(index)");
  }

  const auto *gs = GetGameState(state);
  const std::int64_t character_index =
      LuaNumberToZeroBasedI32Index(lua_tonumber(state, 1));
  if (gs == nullptr || character_index < 0 ||
      static_cast<std::uint64_t>(character_index) >= gs->characters.size()) {
    lua_pushnil(state);
    lua_pushnil(state);
    lua_pushnil(state);
    lua_pushnumber(state, 0.0);
    lua_pushnumber(state, 0.0);
    lua_pushnil(state);
    lua_pushnil(state);
    lua_pushnil(state);
    lua_pushnil(state);
    lua_pushnil(state);
    return 10;
  }
  const auto &character = gs->characters[static_cast<std::size_t>(character_index)];
  const std::string_view race_name = LookupGlueRaceDisplayName(
      state, static_cast<int>(character.race_id), static_cast<int>(character.gender));
  const std::string_view class_name = LookupGlueClassDisplayName(
      state, static_cast<int>(character.class_id), static_cast<int>(character.gender));
  const std::string_view zone_name = LookupGlueAreaDisplayName(state, character.zone_id);

  lua_pushstring(state, character.name.c_str());

  PushLuaStringView(state, race_name);

  PushLuaStringView(state, class_name);

  lua_pushnumber(state, static_cast<double>(character.level));

  if (!zone_name.empty()) {
    PushLuaStringView(state, zone_name);
  } else {
    lua_pushnil(state);
  }

  lua_pushnumber(state, character.gender == 0u ? 2.0 : 3.0);

  lua_pushboolean(state, (character.char_flags & 0x2000u) != 0 ? 1 : 0);

  lua_pushboolean(state, (character.at_login_flags & 0x1u) != 0 ? 1 : 0);

  lua_pushboolean(state, (character.at_login_flags & 0x100000u) != 0 ? 1 : 0);

  lua_pushboolean(state, (character.at_login_flags & 0x10000u) != 0 ? 1 : 0);
  return 10;
}

int LuaSelectCharacter(lua_State *state) {
  if (lua_isnumber(state, 1) == 0) {
    return luaL_error(state, "Usage: SelectCharacter(index)");
  }

  auto *gs = GetGameState(state);
  if (gs == nullptr) {
    return 0;
  }

  const int idx =
      NormalizeLuaCharacterSelectionIndex(lua_tonumber(state, 1), gs->characters.size());
  gs->selected_character_index = idx;

  const bool has_selected_character = idx < static_cast<int>(gs->characters.size());
  if (has_selected_character) {
    gs->select_facing = 0.0f;
  }

  if (gs->char_select_scene != nullptr) {
    gs->char_select_scene->SyncFromGameState(*gs);
    if (has_selected_character) {

      gs->char_select_scene->ApplySelectFacing(gs->select_facing);
    }
  }

  if (gs->fire_event) {
    gs->fire_event("UPDATE_SELECTED_CHARACTER",
                   {openwow::ui::glue::MakeLuaNumber(static_cast<double>(idx + 1))});
  }
  return 0;
}

int LuaEnterWorld(lua_State *state) {
  auto *gs = GetGameState(state);
  if (gs != nullptr) {
    CGlueMgr_EnterWorld(*gs);
  }
  return 0;
}

int LuaGetCharacterListUpdate(lua_State *state) {
  auto *gs = GetGameState(state);
  if (gs == nullptr) {
    return 0;
  }

  CGlueMgr_ResetCharacterListDisplay(*gs);
  CGlueMgr_RequestCharacterList(*gs);
  return 0;
}

int LuaReadyForAccountDataTimes(lua_State *state) {
  (void)SendGlueRealmPacket(
      state,
      openwow::net::wotlk::PacketSender::BuildReadyForAccountDataTimes());
  return 0;
}

int LuaCreateCharacter(lua_State *state) {
  auto *gs = GetGameState(state);
  if (gs == nullptr)
    return 0;

  const char *name = lua_tostring(state, 1);

  SubmitCharacterCreation(*gs, name);
  return 0;
}

int LuaDeclineCharacter(lua_State *state) {
  if (lua_isnumber(state, 1) == 0) {
    return luaL_error(state, "Usage: DeclineCharacter(index, name1, name2, name3, name4, name5)");
  }

  std::array<std::string, 5> declined_forms;
  for (int i = 0; i < 5; ++i) {
    const char *s = lua_tostring(state, i + 2);
    if (s == nullptr || s[0] == '\0')
      return 0;
    declined_forms[static_cast<std::size_t>(i)] = s;
  }

  auto *gs = GetGameState(state);
  if (gs == nullptr)
    return 0;

  const std::int64_t idx = LuaNumberToZeroBasedI32Index(lua_tonumber(state, 1));
  if (idx < 0 || idx > std::numeric_limits<int>::max() ||
      static_cast<std::uint64_t>(idx) >= gs->characters.size()) {
    return 0;
  }
  const auto *character = CGlueMgr_GetCharacterEntry(*gs, static_cast<int>(idx));
  if (character == nullptr)
    return 0;

  if ((character->char_flags & 0x02000000u) != 0) {
    return 0;
  }

  if (CGlueMgr_SendDeclinedCharacterNames(*gs, character->id, declined_forms)) {
    lua_pushnumber(state, 1.0);
  } else {
    lua_pushnil(state);
  }
  return 1;
}

int LuaDeclineName(lua_State *state) {
  return openwow::ui::LuaDeclineName(state);
}

int LuaDeleteCharacter(lua_State *state) {
  if (lua_isnumber(state, 1) == 0) {
    return luaL_error(state, "Usage: DeleteCharacter(index)");
  }

  auto *gs = GetGameState(state);
  if (gs == nullptr)
    return 0;

  const std::int64_t idx = LuaNumberToZeroBasedI32Index(lua_tonumber(state, 1));
  if (idx < 0 || idx > std::numeric_limits<int>::max() ||
      static_cast<std::uint64_t>(idx) >= gs->characters.size()) {
    return 0;
  }
  const auto *character = CGlueMgr_GetCharacterEntry(*gs, static_cast<int>(idx));
  if (character == nullptr)
    return 0;

  CGlueMgr_SendCharDelete(*gs, character->id);
  return 0;
}

int LuaRenameCharacter(lua_State *state) {
  if (lua_isnumber(state, 1) == 0) {
    return luaL_error(state, "Usage: RenameCharacter(index, name)");
  }
  if (!lua_isstring(state, 2))
    return 0;

  const char *new_name = lua_tostring(state, 2);
  if (new_name == nullptr || new_name[0] == '\0')
    return 0;

  auto *gs = GetGameState(state);
  if (gs == nullptr)
    return 0;

  const std::int64_t idx = LuaNumberToZeroBasedI32Index(lua_tonumber(state, 1));
  if (idx < 0 || idx > std::numeric_limits<int>::max() ||
      static_cast<std::uint64_t>(idx) >= gs->characters.size()) {
    return 0;
  }
  const auto *character = CGlueMgr_GetCharacterEntry(*gs, static_cast<int>(idx));
  if (character == nullptr)
    return 0;

  if ((character->char_flags & 0x4000u) == 0)
    return 0;

  if (openwow::core::SStrCmpUTF8NoCase(new_name, character->name.c_str(), 0x7FFFFFFF) == 0) {
    if (gs->fire_event) {
      gs->fire_event("FORCE_RENAME_CHARACTER", {MakeLuaString("CHAR_CREATE_NAME_IN_USE")});
    }
    return 0;
  }

  if (CGlueMgr_SendCharRename(*gs, character->id, new_name)) {
    lua_pushnumber(state, 1.0);
  } else {
    lua_pushnil(state);
  }
  return 1;
}

int LuaIsConnectedToServer(lua_State *state) {
  const auto *gs = GetGameState(state);
  if (gs != nullptr && gs->connected) {
    lua_pushnumber(state, 1.0);
  } else {
    lua_pushnil(state);
  }
  return 1;
}

int LuaDisconnectFromServer(lua_State *state) {
  auto& client_services = openwow::net::ClientServices::Instance();
  if (client_services.IsWorldConnected()) {
    CGlueMgr_RequestSilentDisconnect(GetGameState(state));
    client_services.DisconnectAndCleanup();
  }

  client_services.Disconnect();
  return 0;
}

int LuaCancelLogin(lua_State *state) {
  if (auto *gs = GetGameState(state); gs != nullptr) {
    gs->wants_cancel_auth_login = true;
  }
  return 0;
}

int LuaGetNumAddOns(lua_State *state) {
  lua_pushnumber(state, static_cast<lua_Number>(openwow::ui::AddOnsData::Get().GetAddonCount()));
  return 1;
}

int LuaGetAddOnInfo(lua_State *state) {
  auto &addons_data = openwow::ui::AddOnsData::Get();
  const std::string &addon_name =
      openwow::ui::game::detail::RequireScriptAddonNameByIndex(
          state, "Usage: GetAddOnInfo(index)");
  const char *title = addons_data.GetMetadata(addon_name.c_str(), "Title");
  const char *notes = addons_data.GetMetadata(addon_name.c_str(), "Notes");
  const auto loadability = addons_data.EvaluateLoadability(addon_name.c_str(), false, nullptr);

  lua_pushstring(state, addon_name.c_str());
  if (title != nullptr) {
    lua_pushstring(state, title);
  } else {
    lua_pushnil(state);
  }
  if (notes != nullptr) {
    lua_pushstring(state, notes);
  } else {
    lua_pushnil(state);
  }
  if (const char *url = addons_data.GetAddonUrl(addon_name.c_str()); url != nullptr) {
    lua_pushstring(state, url);
  } else {
    lua_pushnil(state);
  }
  if (loadability.loadable) {
    lua_pushnumber(state, 1.0);
  } else {
    lua_pushnil(state);
  }
  if (loadability.reason == openwow::ui::AddonStatusLabel::Loadable &&
      loadability.dependency_reason == openwow::ui::AddonStatusLabel::Loadable) {
    lua_pushnil(state);
  } else {
    std::string reason_storage;
    lua_pushstring(state, openwow::ui::AddOnsData::FormatLoadReason(
                              loadability.reason, loadability.dependency_reason, reason_storage));
  }
  lua_pushstring(state, addons_data.GetSecurityLabel(addon_name.c_str()));
  if (addons_data.HasNewVersion(addon_name.c_str())) {
    lua_pushnumber(state, 1.0);
  } else {
    lua_pushnil(state);
  }
  return 8;
}

int LuaGetAddOnDependencies(lua_State *state) {
  auto &addons_data = openwow::ui::AddOnsData::Get();
  const std::string &addon_name =
      openwow::ui::game::detail::RequireScriptAddonNameByIndex(
          state, "Usage: GetAddOnDependencies(index)");
  const auto *addon = addons_data.FindAddon(addon_name);
  if (addon == nullptr) {
    return 0;
  }

  return openwow::ui::game::detail::PushStringList(state, addon->dependencies);
}

int LuaGetAddOnEnableState(lua_State *state) {
  const auto query = openwow::ui::game::detail::RequireScriptAddonCharacterAndNameByIndex(
      state, "Usage: GetAddOnEnableState(\"character\", index)");
  const int enabled_state = openwow::ui::AddOnsData::Get().GetCharacterLoadState(
      query.addon_name->c_str(), query.character_name, true);
  lua_pushnumber(state, static_cast<lua_Number>(enabled_state));
  return 1;
}

static void GlueSetAddonEnabledByIndex(lua_State *state, bool enabled,
                                       const char *usage_error) {
  const auto query =
      openwow::ui::game::detail::RequireScriptAddonCharacterAndNameByIndex(state, usage_error);
  openwow::ui::AddOnsData::Get().SetSavedAddonEnabled(
      query.addon_name->c_str(), query.character_name, enabled);
}

int LuaEnableAddOn(lua_State *state) {
  GlueSetAddonEnabledByIndex(
      state, true,
      "Usage: EnableAddOn(\"character\", index)");
  return 0;
}

int LuaDisableAddOn(lua_State *state) {
  GlueSetAddonEnabledByIndex(
      state, false,
      "Usage: DisableAddOn(\"character\", index)");
  return 0;
}

static int GlueSetAllAddonsEnabled(lua_State *state, const bool enabled) {
  const char *character_name = lua_tostring(state, 1);
  openwow::ui::AddOnsData::Get().SetAllVisibleAddonsSavedEnabled(character_name,
                                                                 enabled);
  return 0;
}

int LuaGlueEnableAllAddOns(lua_State *state) {
  return GlueSetAllAddonsEnabled(state, true);
}

int LuaGlueDisableAllAddOns(lua_State *state) {
  return GlueSetAllAddonsEnabled(state, false);
}

int LuaResetAddOns(lua_State *state) {
  (void)state;

  openwow::ui::AddOnsData::Get().ReloadSavedStates();
  return 0;
}

int LuaSaveAddOns(lua_State *state) {
  (void)state;

  openwow::ui::AddOnsData::Get().SaveSavedStates();
  return 0;
}

int LuaSetAddonVersionCheck(lua_State *state) {
  const bool enabled = ScriptReadBoolArgOrDefault(state, 1, true);
  auto &sys = openwow::ui::game::CVarSystem::Instance();
  sys.SetCVar("checkAddonVersion", enabled ? "1" : "0");
  return 0;
}

int LuaIsAddonVersionCheckEnabled(lua_State *state) {
  auto &sys = openwow::ui::game::CVarSystem::Instance();
  lua_pushwowbool(state, sys.GetCVarBool("checkAddonVersion"));
  return 1;
}

int LuaIsWindowsClient(lua_State *state) {
  return openwow::ui::PushRetailLuaClientPlatformQuery(
      state, openwow::ui::LuaClientPlatform::kWindows);
}

int LuaIsLinuxClient(lua_State *state) {
  return openwow::ui::PushRetailLuaClientPlatformQuery(
      state, openwow::ui::LuaClientPlatform::kLinux);
}

int LuaIsInvalidLocale(lua_State *state) {
  if (lua_isnumber(state, 1) == 0) {
    return luaL_error(state, "Usage: Script_IsInvalidLocale(category)");
  }

  const auto *gs = GetGameState(state);
  if (gs == nullptr) {
    return 0;
  }

  const auto layout = BuildRealmDisplayLayout(*gs);
  const std::int32_t category =
      openwow::ui::TruncateLuaNumberToI32(lua_tonumber(state, 1));
  const auto *realm_category = ResolveVisibleRealmCategory(layout, category);
  if (realm_category == nullptr || IsCurrentClientLocaleAllowedForRealmCategory(*realm_category)) {
    return 0;
  }

  const auto &client_services = openwow::net::ClientServices::Instance();
  return PushOneOrNoResults(state, !client_services.BypassesRealmCategoryLocaleValidation());
}

int LuaGetNumGameAccounts(lua_State *state) {
  auto &login = RequireBattlenetLogin(state);
  lua_pushnumber(state, static_cast<lua_Number>(login.GetNumGameAccounts()));
  return 1;
}

int LuaGetGameAccountInfo(lua_State *state) {
  const auto index = ReadGameAccountIndex(state, "Usage: GetGameAccountInfo(index)");
  auto &login = RequireBattlenetLogin(state);
  const char *account_name = login.GetGameAccountName(index);
  if (account_name == nullptr) {
    return 0;
  }

  lua_pushstring(state, account_name);
  lua_pushnumber(state, static_cast<lua_Number>(login.GetGameAccountId(index)));
  return 2;
}

int LuaSetGameAccount(lua_State *state) {
  const auto index = ReadGameAccountIndex(state, "Usage: SetGameAccount(index)");
  auto &login = RequireBattlenetLogin(state);
  lua_pushboolean(state, login.SetGameAccount(index) ? 1 : 0);
  return 1;
}

int LuaGetBillingPlan(lua_State *state) {
  const auto billing_plan = openwow::net::ClientServices::Instance().GetBillingPlan();
  lua_pushnumber(state, static_cast<lua_Number>(billing_plan.plan));
  lua_pushnumber(state, static_cast<lua_Number>(billing_plan.is_game_room));
  lua_pushnumber(state, static_cast<lua_Number>(billing_plan.is_paid_time));
  return 3;
}

int LuaGetBillingTimeRemaining(lua_State *state) {
  lua_pushnumber(state, static_cast<lua_Number>(
                            openwow::net::ClientServices::Instance().GetBillingTimeRemaining()));
  return 1;
}

int LuaGetBillingTimeRested(lua_State *state) {
  lua_pushnumber(state, static_cast<lua_Number>(
                            openwow::net::ClientServices::Instance().GetBillingTimeRested()));
  return 1;
}

struct WotlkRaceInfo {
  int id;
  const char *name;
  const char *tag;
  int faction;
};

constexpr WotlkRaceInfo kWotlkRaces[] = {
    {1, "Human", "Human", 0},        {3, "Dwarf", "Dwarf", 0},
    {4, "Night Elf", "NightElf", 0}, {7, "Gnome", "Gnome", 0},
    {11, "Draenei", "Draenei", 0},   {2, "Orc", "Orc", 1},
    {5, "Undead", "Scourge", 1},     {6, "Tauren", "Tauren", 1},
    {8, "Troll", "Troll", 1},        {10, "Blood Elf", "BloodElf", 1},
};

constexpr int kNumWotlkRaces = static_cast<int>(sizeof(kWotlkRaces) / sizeof(kWotlkRaces[0]));

struct WotlkClassInfo {
  int id;
  const char *name;
  const char *tag;
  std::uint8_t required_expansion;
};

constexpr WotlkClassInfo kWotlkClasses[] = {
    {1, "Warrior", "WARRIOR", 0}, {2, "Paladin", "PALADIN", 0},
    {3, "Hunter", "HUNTER", 0},   {4, "Rogue", "ROGUE", 0},
    {5, "Priest", "PRIEST", 0},   {6, "Death Knight", "DEATHKNIGHT", 2},
    {7, "Shaman", "SHAMAN", 0},   {8, "Mage", "MAGE", 0},
    {9, "Warlock", "WARLOCK", 0}, {11, "Druid", "DRUID", 0},
};

constexpr int kNumWotlkClasses = static_cast<int>(sizeof(kWotlkClasses) / sizeof(kWotlkClasses[0]));

struct GlueAvailableRaceInfo {
  int race_id = 0;
  std::string_view name;
  std::string_view tag;
  int faction_id = 0;
};

struct GlueFactionGroupInfo {
  std::uint32_t mask_bit = 0;
  int faction_id = 0;
  bool valid = false;
};

struct GlueFactionStrings {
  std::string_view name;
  std::string_view token;
  bool valid = false;
};

struct GlueAvailableRaceList {
  std::vector<GlueAvailableRaceInfo> races;
  bool from_dbc = false;
};

constexpr std::uint8_t kClassRoleFlags[12] = {
    0x00,
    0x0B,
    0x0F,
    0x09,
    0x09,
    0x0D,
    0x0B,
    0x0D,
    0x09,
    0x09,
    0x00,
    0x0F,
};

constexpr int kSexMapping[3] = {2, 3, 1};

bool IsValidRaceClass(lua_State *state, int race_id, int class_id) {
  if (const auto *dbc = GetDbcLoader(state); dbc != nullptr && !dbc->char_base_info().empty()) {
    for (const auto &combo : dbc->char_base_info()) {
      if (static_cast<int>(combo.race_id) == race_id &&
          static_cast<int>(combo.class_id) == class_id) {
        return true;
      }
    }
    return false;
  }

  return IsWotlkRaceClassCombination(race_id, class_id);
}

struct GlueCreateClassEntry {
  int class_id = 0;
  bool has_definition = false;
};

std::vector<GlueCreateClassEntry> GetAvailableCreateClassesForRace(lua_State *state, int race_id) {
  if (const auto *dbc = GetDbcLoader(state); dbc != nullptr && !dbc->char_base_info().empty()) {
    std::vector<GlueCreateClassEntry> available_classes;
    available_classes.reserve(dbc->char_base_info().size());
    for (const auto &combo : dbc->char_base_info().entries()) {
      if (static_cast<int>(combo.race_id) != race_id) {
        continue;
      }

      const auto *class_entry = dbc->chr_classes().LookupEntry(combo.class_id);
      available_classes.push_back({
          .class_id = static_cast<int>(combo.class_id),
          .has_definition = class_entry != nullptr,
      });
    }
    return available_classes;
  }

  std::vector<GlueCreateClassEntry> available_classes;
  available_classes.reserve(kNumWotlkClasses);
  for (int i = 0; i < kNumWotlkClasses; ++i) {
    if (!IsValidRaceClass(state, race_id, kWotlkClasses[i].id)) {
      continue;
    }
    available_classes.push_back({
        .class_id = kWotlkClasses[i].id,
        .has_definition = true,
    });
  }
  return available_classes;
}

int FindAvailableCreateClassIndex(const std::vector<GlueCreateClassEntry> &available_classes,
                                  int class_id) {
  for (std::size_t i = 0; i < available_classes.size(); ++i) {
    if (available_classes[i].has_definition && available_classes[i].class_id == class_id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

int FindClassIndex(const std::vector<int> &class_ids, int class_id) {
  for (std::size_t i = 0; i < class_ids.size(); ++i) {
    if (class_ids[i] == class_id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

const WotlkRaceInfo *FindWotlkRaceInfo(int race_id);
const WotlkClassInfo *FindWotlkClassInfo(int class_id);

std::uint32_t MakeGlueFactionGroupMask(const std::uint32_t mask_id) {
  if (mask_id == 0u) {
    return 0u;
  }

  return 1u << (mask_id & 31u);
}

GlueFactionGroupInfo FindGlueFactionGroup(const openwow::data::dbc::DbcLoader &dbc,
                                          std::string_view token, int faction_id) {
  for (const auto &entry : dbc.faction_group()) {
    if (!openwow::text::EqualsIgnoreCaseAscii(entry.internal_name, token)) {
      continue;
    }

    const std::uint32_t mask_bit = MakeGlueFactionGroupMask(entry.mask_id);
    if (mask_bit == 0u) {
      return {};
    }

    return {
        .mask_bit = mask_bit,
        .faction_id = faction_id,
        .valid = true,
    };
  }

  return {};
}

GlueFactionStrings ResolveGlueFactionStrings(const openwow::data::dbc::DbcLoader &dbc,
                                             const std::uint32_t faction_group_bits) {
  for (const auto &entry : dbc.faction_group()) {
    const std::uint32_t mask_bit = MakeGlueFactionGroupMask(entry.mask_id);
    if (mask_bit == 0u) {
      continue;
    }
    if ((faction_group_bits & mask_bit) == 0u) {
      continue;
    }

    return {
        .name = entry.name,
        .token = entry.internal_name,
        .valid = true,
    };
  }

  return {};
}

GlueFactionStrings ResolveGlueFactionByDisplayGroup(
    const openwow::data::dbc::DbcLoader &dbc, const int faction_id) {
  const std::string_view token = faction_id == 0
                                     ? std::string_view{"Alliance"}
                                 : faction_id == 1
                                     ? std::string_view{"Horde"}
                                     : std::string_view{};
  if (token.empty()) {
    return {};
  }

  for (const auto &entry : dbc.faction_group()) {
    if (openwow::text::EqualsIgnoreCaseAscii(entry.internal_name, token)) {
      return {
          .name = entry.name,
          .token = entry.internal_name,
          .valid = true,
      };
    }
  }
  return {};
}

GlueFactionStrings ResolveGlueFactionForRaceFromDbc(const openwow::data::dbc::DbcLoader &dbc,
                                                    const int race_id) {
  const auto *const race = dbc.chr_races().LookupEntry(static_cast<std::uint32_t>(race_id));
  if (race == nullptr) {
    return {};
  }

  const auto *const faction_template = dbc.faction_template().LookupEntry(race->faction_id);
  if (faction_template == nullptr) {
    return {};
  }

  return ResolveGlueFactionStrings(dbc, faction_template->faction_group);
}

GlueFactionStrings ResolveGlueFactionForRaceFallback(const int race_id) {
  const auto *const race = FindWotlkRaceInfo(race_id);
  if (race == nullptr) {
    return {};
  }

  const std::string_view token =
      race->faction == 0 ? std::string_view{"Alliance"} : std::string_view{"Horde"};
  return {
      .name = token,
      .token = token,
      .valid = true,
  };
}

void PushLuaStringView(lua_State *state, const std::string_view value) {
  lua_pushlstring(state, value.data() != nullptr ? value.data() : "", value.size());
}

std::vector<GlueAvailableRaceInfo> BuildGlueAvailableRacesFromFallback() {
  std::vector<GlueAvailableRaceInfo> races;
  races.reserve(kNumWotlkRaces);
  for (const auto &race : kWotlkRaces) {
    races.push_back({
        .race_id = race.id,
        .name = race.name,
        .tag = race.tag,
        .faction_id = race.faction,
    });
  }
  return races;
}

std::vector<GlueAvailableRaceInfo>
BuildGlueAvailableRacesFromDbc(const openwow::data::dbc::DbcLoader &dbc) {

  std::vector<GlueAvailableRaceInfo> races;
  if (dbc.chr_races().empty() || dbc.faction_group().empty() ||
      dbc.faction_template().empty()) {
    return races;
  }

  const std::array<GlueFactionGroupInfo, 2> faction_groups = {{
      FindGlueFactionGroup(dbc, "Alliance", 0),
      FindGlueFactionGroup(dbc, "Horde", 1),
  }};

  for (const auto &faction_group : faction_groups) {
    if (!faction_group.valid) {
      continue;
    }

    for (const auto &race : dbc.chr_races()) {
      if ((race.flags & 1u) != 0) {
        continue;
      }

      const auto *const faction_template = dbc.faction_template().LookupEntry(race.faction_id);
      if (faction_template == nullptr) {
        continue;
      }
      if ((faction_template->faction_group & faction_group.mask_bit) == 0) {
        continue;
      }

      races.push_back({
          .race_id = static_cast<int>(race.id),
          .name = race.name,
          .tag = race.client_file_string,
          .faction_id = faction_group.faction_id,
      });
    }
  }

  return races;
}

GlueAvailableRaceList LoadGlueAvailableRaces(lua_State *state) {
  if (const auto *dbc = GetDbcLoader(state); dbc != nullptr) {
    auto races = BuildGlueAvailableRacesFromDbc(*dbc);
    if (!races.empty()) {
      return {
          .races = std::move(races),
          .from_dbc = true,
      };
    }
  }

  return {
      .races = BuildGlueAvailableRacesFromFallback(),
      .from_dbc = false,
  };
}

std::vector<GlueAvailableRaceInfo> GetGlueAvailableRaces(lua_State *state) {
  return LoadGlueAvailableRaces(state).races;
}

std::vector<int> GetGlueAvailableClassIds(lua_State *state) {
  std::vector<int> class_ids;
  if (const auto *dbc = GetDbcLoader(state); dbc != nullptr && !dbc->chr_classes().empty()) {
    class_ids.reserve(dbc->chr_classes().size());
    for (const auto &cls : dbc->chr_classes().entries()) {
      class_ids.push_back(static_cast<int>(cls.id));
    }
    return class_ids;
  }

  class_ids.reserve(kNumWotlkClasses);
  for (const auto &cls : kWotlkClasses) {
    class_ids.push_back(cls.id);
  }
  return class_ids;
}

const WotlkRaceInfo *FindWotlkRaceInfo(const int race_id) {
  for (const auto &race : kWotlkRaces) {
    if (race.id == race_id) {
      return &race;
    }
  }

  return nullptr;
}

const WotlkClassInfo *FindWotlkClassInfo(const int class_id) {
  for (const auto &cls : kWotlkClasses) {
    if (cls.id == class_id) {
      return &cls;
    }
  }

  return nullptr;
}

std::string_view LookupGlueRaceDisplayName(lua_State *state, const int race_id, const int sex_id) {
  if (const auto *dbc = GetDbcLoader(state); dbc != nullptr) {
    if (const auto *race = dbc->chr_races().LookupEntry(static_cast<std::uint32_t>(race_id));
        race != nullptr) {
      const std::string_view name = race->DisplayNameForSex(static_cast<std::uint32_t>(sex_id));
      if (!name.empty()) {
        return name;
      }
    }
  }

  if (const auto *race = FindWotlkRaceInfo(race_id); race != nullptr) {
    return race->name;
  }

  return {};
}

std::string_view LookupGlueAreaDisplayName(lua_State *state, const std::uint32_t area_id) {
  if (area_id == 0) {
    return {};
  }

  if (const auto *dbc = GetDbcLoader(state); dbc != nullptr) {
    if (const auto *area = dbc->area_table().LookupEntry(area_id);
        area != nullptr && !area->name.empty()) {
      return area->name;
    }
  }

  return {};
}

int RemapGlueBackgroundRaceId(int race_id) {
  if (race_id == 7) {
    return 3;
  }
  if (race_id == 8) {
    return 2;
  }
  return race_id;
}

int GetGlueSelectedSexIndex(lua_State *state) {
  const auto *gs = GetGameState(state);
  return gs != nullptr ? gs->create_sex : 0;
}

std::string_view LookupGlueRaceClientFileString(lua_State *state, const int race_id) {
  if (const auto *dbc = GetDbcLoader(state); dbc != nullptr) {
    if (const auto *race = dbc->chr_races().LookupEntry(static_cast<std::uint32_t>(race_id));
        race != nullptr && !race->client_file_string.empty()) {
      return race->client_file_string;
    }
  }

  return {};
}

std::string_view LookupGlueRaceTag(lua_State *state, const int race_id) {
  if (const std::string_view race_tag = LookupGlueRaceClientFileString(state, race_id);
      !race_tag.empty()) {
    return race_tag;
  }

  if (const auto *race = FindWotlkRaceInfo(race_id); race != nullptr) {
    return race->tag;
  }

  return {};
}

std::string_view LookupGlueClassDisplayName(lua_State *state, const int class_id,
                                            const int sex_id) {
  if (const auto *dbc = GetDbcLoader(state); dbc != nullptr) {
    if (const auto *cls = dbc->chr_classes().LookupEntry(static_cast<std::uint32_t>(class_id));
        cls != nullptr) {
      return cls->DisplayNameForSex(static_cast<std::uint32_t>(sex_id));
    }
  }

  if (const auto *cls = FindWotlkClassInfo(class_id); cls != nullptr) {
    return cls->name;
  }

  return {};
}

std::string_view LookupGlueClassClientFileString(lua_State *state, const int class_id) {
  if (const auto *dbc = GetDbcLoader(state); dbc != nullptr) {
    if (const auto *cls = dbc->chr_classes().LookupEntry(static_cast<std::uint32_t>(class_id));
        cls != nullptr && !cls->client_file_string.empty()) {
      return cls->client_file_string;
    }
  }

  return {};
}

std::string_view LookupGlueClassTag(lua_State *state, const int class_id) {
  if (const std::string_view class_tag = LookupGlueClassClientFileString(state, class_id);
      !class_tag.empty()) {
    return class_tag;
  }

  if (const auto *cls = FindWotlkClassInfo(class_id); cls != nullptr) {
    return cls->tag;
  }

  return {};
}

std::string_view LookupGlueBackgroundRaceToken(lua_State *state, const int race_id) {
  return LookupGlueRaceClientFileString(state, RemapGlueBackgroundRaceId(race_id));
}

std::string_view LookupGlueBackgroundClassToken(lua_State *state, const int class_id) {
  return class_id == 6 ? LookupGlueClassClientFileString(state, 6) : std::string_view{};
}

std::uint8_t LookupGlueClassRequiredExpansionLevel(lua_State *state, const int class_id) {
  if (const auto *dbc = GetDbcLoader(state); dbc != nullptr) {
    if (const auto *cls = dbc->chr_classes().LookupEntry(static_cast<std::uint32_t>(class_id));
        cls != nullptr) {
      return static_cast<std::uint8_t>(std::min<std::uint32_t>(
          cls->required_expansion, std::numeric_limits<std::uint8_t>::max()));
    }
  }

  if (const auto *cls = FindWotlkClassInfo(class_id); cls != nullptr) {
    return cls->required_expansion;
  }

  return 0;
}

std::uint8_t LookupGlueRaceRequiredExpansionLevel(lua_State *state, const int race_id) {
  if (const auto *dbc = GetDbcLoader(state); dbc != nullptr) {
    if (const auto *race = dbc->chr_races().LookupEntry(static_cast<std::uint32_t>(race_id));
        race != nullptr) {
      return static_cast<std::uint8_t>(std::min<std::uint32_t>(
          race->required_expansion, std::numeric_limits<std::uint8_t>::max()));
    }
  }

  return GetRaceRequiredExpansionLevel(race_id);
}

int FindGlueAvailableRaceIndex(const std::vector<GlueAvailableRaceInfo> &races, int race_id) {
  for (int index = 0; index < static_cast<int>(races.size()); ++index) {
    if (races[static_cast<std::size_t>(index)].race_id == race_id) {
      return index;
    }
  }
  return -1;
}

constexpr int kCreateCustomizationSelector = 0;

CharacterCustomizationState MakeCustomizationState(const GlueGameState &state);
void StoreCustomizationState(GlueGameState &state,
                             const CharacterCustomizationState &customization);

void ResetCreateCustomizationState(GlueGameState &state) {
  state.create_skin = 0;
  state.create_skin_cycle_anchor = 0;
  state.create_face = 0;
  state.create_hair_style = 0;
  state.create_hair_color = 0;
  state.create_facial_hair = 0;
}

void RandomizeCreateCustomizationAppearance(lua_State *state, GlueGameState &game_state,
                                            LegacyAdlerRandom &rng,
                                            const int class_id_for_randomization,
                                            const bool reset_values,
                                            const CharacterCustomizationRandomizationOrder order) {
  if (const auto *dbc = GetDbcLoader(state); dbc != nullptr && !dbc->char_sections().empty()) {
    auto customization = reset_values ? CharacterCustomizationState{
                                           .race_id = game_state.create_race,
                                           .sex_id = game_state.create_sex,
                                           .class_id = class_id_for_randomization,
                                           .skin = 0,
                                           .skin_selection_anchor = 0,
                                           .face = 0,
                                           .hair_style = 0,
                                           .hair_color = 0,
                                           .facial_hair = 0,
                                       }
                                       : MakeCustomizationState(game_state);
    customization.class_id = class_id_for_randomization;
    RandomizeCharacterCustomizationWithDbc(
        customization, dbc->char_sections().entries(),
        dbc->character_facial_hair_styles().entries(),
        [&rng](const std::size_t count) { return rng.SelectOrdinal(count); }, order,
        kCreateCustomizationSelector);
    StoreCustomizationState(game_state, customization);
  }
}

void RefreshCreateCustomizationDisplay(GlueGameState &state);

void ResetCharCustomizeState(lua_State *state, GlueGameState &game_state, LegacyAdlerRandom &rng) {
  game_state.customize_source_character_index = -1;
  if (game_state.char_customize_scene != nullptr) {

    game_state.char_customize_scene->ReleaseContent();
  }

  const auto races = GetGlueAvailableRaces(state);
  if (races.empty()) {
    ResetCreateCustomizationState(game_state);
    return;
  }

  const std::uint8_t expansion_level = openwow::net::ClientServices::Instance().GetExpansionLevel();
  game_state.create_sex = static_cast<int>(rng.Next() >> 31);

  for (;;) {
    const auto ordinal = static_cast<std::size_t>(rng.SelectOrdinal(races.size()));
    const int race_id = races[ordinal].race_id;
    if (expansion_level < LookupGlueRaceRequiredExpansionLevel(state, race_id)) {
      continue;
    }
    game_state.create_race = race_id;
    openwow::vfs::SetDataPreloadSelectedRace(game_state.create_race);
    break;
  }

  game_state.create_class = 0;
  ResetCreateCustomizationState(game_state);
  RandomizeCreateCustomizationAppearance(state, game_state, rng, 0, true,
                                         CharacterCustomizationRandomizationOrder::SetupModel);

  std::vector<int> available_class_ids;
  const auto available_classes = GetAvailableCreateClassesForRace(state, game_state.create_race);
  available_class_ids.reserve(available_classes.size());
  for (const auto &cls : available_classes) {
    if (!cls.has_definition) {
      continue;
    }
    if (LookupGlueClassRequiredExpansionLevel(state, cls.class_id) > expansion_level) {
      continue;
    }
    available_class_ids.push_back(cls.class_id);
  }

  if (!available_class_ids.empty()) {
    const auto ordinal = static_cast<std::size_t>(rng.SelectOrdinal(available_class_ids.size()));
    game_state.create_class = available_class_ids[ordinal];
  }

  RefreshCreateCustomizationDisplay(game_state);
}

CharacterCustomizationState MakeCustomizationState(const GlueGameState &state) {
  return CharacterCustomizationState{
      .race_id = state.create_race,
      .sex_id = state.create_sex,
      .class_id = state.create_class,
      .skin = state.create_skin,
      .skin_selection_anchor = state.create_skin_cycle_anchor,
      .face = state.create_face,
      .hair_style = state.create_hair_style,
      .hair_color = state.create_hair_color,
      .facial_hair = state.create_facial_hair,
  };
}

void StoreCustomizationState(GlueGameState &state,
                             const CharacterCustomizationState &customization) {
  state.create_skin = customization.skin;
  state.create_skin_cycle_anchor = customization.skin_selection_anchor;
  state.create_face = customization.face;
  state.create_hair_style = customization.hair_style;
  state.create_hair_color = customization.hair_color;
  state.create_facial_hair = customization.facial_hair;
}

constexpr std::uint32_t kAtLoginCustomize = 0x1u;
constexpr std::uint32_t kAtLoginFactionChange = 0x10000u;
constexpr std::uint32_t kAtLoginRaceChange = 0x100000u;

std::optional<std::size_t> CreateCustomizationCacheIndex(const int race_ordinal,
                                                         const int sex_id) {
  if (race_ordinal < 0 || sex_id < 0 || sex_id > 2) {
    return std::nullopt;
  }
  const auto index = static_cast<std::size_t>(race_ordinal * 2 + sex_id);
  return index < GlueGameState::kCreateCustomizationCacheSlotCount
             ? std::optional<std::size_t>{index}
             : std::nullopt;
}

void SaveCreateCustomizationAppearanceToCache(GlueGameState &state,
                                              const int race_ordinal) {
  const auto cache_index = CreateCustomizationCacheIndex(race_ordinal, state.create_sex);
  if (!cache_index.has_value()) {
    return;
  }

  state.create_customization_cache[*cache_index] = GlueGameState::CreateCustomizationCacheEntry{
      .skin = state.create_skin,
      .face = state.create_face,
      .hair_style = state.create_hair_style,
      .hair_color = state.create_hair_color,
      .facial_hair = state.create_facial_hair,
  };
}

const GlueGameState::CreateCustomizationCacheEntry *
FindCreateCustomizationAppearanceCache(const GlueGameState &state, const int race_ordinal,
                                       const int sex_id) {
  const auto cache_index = CreateCustomizationCacheIndex(race_ordinal, sex_id);
  if (!cache_index.has_value()) {
    return nullptr;
  }

  const auto &slot = state.create_customization_cache[*cache_index];
  return slot.has_value() ? &slot.value() : nullptr;
}

void ApplyCreateCustomizationAppearanceFromCache(
    GlueGameState &state, const GlueGameState::CreateCustomizationCacheEntry &entry) {
  state.create_skin = entry.skin;
  state.create_skin_cycle_anchor = entry.skin;
  state.create_face = entry.face;
  state.create_hair_style = entry.hair_style;
  state.create_hair_color = entry.hair_color;
  state.create_facial_hair = entry.facial_hair;
}

void ApplyCharacterSummaryToCreateSelection(
    GlueGameState &state, const openwow::net::wotlk::CharacterSummary &character) {
  state.create_race = static_cast<int>(character.race_id);
  state.create_class = static_cast<int>(character.class_id);
  state.create_sex = static_cast<int>(character.gender);
  state.create_skin = static_cast<int>(character.skin);
  state.create_skin_cycle_anchor = state.create_skin;
  state.create_face = static_cast<int>(character.face);
  state.create_hair_style = static_cast<int>(character.hair_style);
  state.create_hair_color = static_cast<int>(character.hair_color);
  state.create_facial_hair = static_cast<int>(character.facial_hair);
}

void RefreshCreateCustomizationPreview(GlueGameState &state) {
  if (state.char_customize_scene != nullptr) {
    state.char_customize_scene->RefreshCreateFromGameState(state);
  }
}

void RefreshCreateCustomizationDisplay(GlueGameState &state) {
  RefreshCreateCustomizationPreview(state);
}

std::vector<int> GetExpansionEligibleCreateClassIds(lua_State *state, const int race_id) {
  std::vector<int> class_ids;
  const std::uint8_t expansion_level = openwow::net::ClientServices::Instance().GetExpansionLevel();
  const auto available_classes = GetAvailableCreateClassesForRace(state, race_id);
  class_ids.reserve(available_classes.size());
  for (const auto &available_class : available_classes) {
    if (!available_class.has_definition) {
      continue;
    }
    if (LookupGlueClassRequiredExpansionLevel(state, available_class.class_id) > expansion_level) {
      continue;
    }
    class_ids.push_back(available_class.class_id);
  }
  return class_ids;
}

void PickRandomAllowedCreateClassForRace(lua_State *state, GlueGameState &game_state,
                                         LegacyAdlerRandom &rng) {
  if (IsValidRaceClass(state, game_state.create_race, game_state.create_class)) {
    return;
  }

  const auto allowed_class_ids = GetExpansionEligibleCreateClassIds(state, game_state.create_race);
  const auto class_id = PickRandomAllowedClass(allowed_class_ids, rng);
  if (class_id.has_value()) {
    game_state.create_class = *class_id;
  }
}

void NormalizeCreateCustomizationStateWithDbc(lua_State *state, GlueGameState &game_state) {
  const auto *dbc = GetDbcLoader(state);
  if (dbc == nullptr || dbc->char_sections().empty()) {
    return;
  }

  auto customization = MakeCustomizationState(game_state);
  if (NormalizeCharacterCustomizationForInit(customization, dbc->char_sections().entries(),
                                             dbc->character_facial_hair_styles().entries(),
                                             kCreateCustomizationSelector)) {
    StoreCustomizationState(game_state, customization);
  }
}

bool CycleSkinCustomizationWithDbc(
    CharacterCustomizationState &state,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharSectionsEntry> &store,
    const int delta) {
  return CycleSkinCustomizationSelection(state, store.entries(), delta,
                                         kCreateCustomizationSelector);
}

bool CycleHairColorCustomizationWithDbc(
    CharacterCustomizationState &state,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharSectionsEntry> &store,
    const int delta) {
  return CycleHairColorCustomizationSelection(state, store.entries(), delta,
                                              kCreateCustomizationSelector);
}

bool CycleFaceCustomizationWithDbc(
    CharacterCustomizationState &state,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharSectionsEntry> &store,
    const int delta) {
  return CycleFaceCustomizationSelection(state, store.entries(), delta,
                                         kCreateCustomizationSelector);
}

bool CycleHairStyleCustomizationWithDbc(
    CharacterCustomizationState &state,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharSectionsEntry> &sections,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharacterFacialHairStylesEntry> &styles,
    const int delta) {
  return CycleHairStyleCustomizationSelection(state, sections.entries(), styles.entries(), delta,
                                              kCreateCustomizationSelector);
}

bool CycleFacialHairCustomizationWithDbc(
    CharacterCustomizationState &state,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharSectionsEntry> &sections,
    const openwow::data::dbc::DbcStore<openwow::data::dbc::CharacterFacialHairStylesEntry> &styles,
    const int delta) {
  return CycleFacialHairCustomizationSelection(state, sections.entries(), styles.entries(), delta,
                                               kCreateCustomizationSelector);
}

int LuaGetAvailableRaces(lua_State *state) {
  const auto races = GetGlueAvailableRaces(state);
  const int result_count = openwow::ui::ReserveLuaResultCapacity(
      state, races.size(), 3u, "available-race results");
  const int sex_id = GetGlueSelectedSexIndex(state);
  const std::uint8_t expansion_level = openwow::net::ClientServices::Instance().GetExpansionLevel();
  for (const auto &race : races) {
    PushLuaStringView(state, LookupGlueRaceDisplayName(state, race.race_id, sex_id));
    PushLuaStringView(state, race.tag);
    lua_pushnumber(
        state,
        expansion_level >= LookupGlueRaceRequiredExpansionLevel(state, race.race_id) ? 1.0 : 0.0);
  }
  return result_count;
}

int LuaGetAvailableClasses(lua_State *state) {
  const int sex_id = GetGlueSelectedSexIndex(state);
  const std::uint8_t expansion_level = openwow::net::ClientServices::Instance().GetExpansionLevel();

  if (const auto *dbc = GetDbcLoader(state); dbc != nullptr && !dbc->chr_classes().empty()) {
    const int result_count = openwow::ui::ReserveLuaResultCapacity(
        state, dbc->chr_classes().size(), 3u, "available-class results");
    for (const auto &cls : dbc->chr_classes().entries()) {
      PushLuaStringView(state, cls.DisplayNameForSex(static_cast<std::uint32_t>(sex_id)));
      PushLuaStringView(state, cls.client_file_string);
      lua_pushnumber(state, expansion_level >= cls.required_expansion ? 1.0 : 0.0);
    }
    return result_count;
  }

  const auto class_ids = GetGlueAvailableClassIds(state);
  const int result_count = openwow::ui::ReserveLuaResultCapacity(
      state, class_ids.size(), 3u, "available-class results");
  for (const int class_id : class_ids) {
    PushLuaStringView(state, LookupGlueClassDisplayName(state, class_id, sex_id));
    PushLuaStringView(state, LookupGlueClassTag(state, class_id));
    lua_pushnumber(state, expansion_level >= LookupGlueClassRequiredExpansionLevel(state, class_id)
                              ? 1.0
                              : 0.0);
  }
  return result_count;
}

int LuaGetClassesForRace(lua_State *state) {
  const auto *gs = GetGameState(state);
  const int race_id = gs != nullptr ? gs->create_race : 1;
  const int sex_id = GetGlueSelectedSexIndex(state);
  const std::uint8_t expansion_level = openwow::net::ClientServices::Instance().GetExpansionLevel();
  const auto classes = GetAvailableCreateClassesForRace(state, race_id);
  const int result_count = openwow::ui::ReserveLuaResultCapacity(
      state, classes.size(), 3u, "race-class results");
  for (const auto &cls : classes) {
    if (!cls.has_definition) {
      lua_pushnil(state);
      lua_pushnil(state);
      lua_pushnil(state);
      continue;
    }

    PushLuaStringView(state, LookupGlueClassDisplayName(state, cls.class_id, sex_id));
    PushLuaStringView(state, LookupGlueClassTag(state, cls.class_id));
    lua_pushnumber(
        state,
        expansion_level >= LookupGlueClassRequiredExpansionLevel(state, cls.class_id) ? 1.0 : 0.0);
  }
  return result_count;
}

int LuaGetNameForRace(lua_State *state) {
  const auto *gs = GetGameState(state);
  const auto *dbc = GetDbcLoader(state);
  if (gs == nullptr || dbc == nullptr) {
    lua_pushnil(state);
    lua_pushnil(state);
    return 2;
  }

  const auto *race = dbc->chr_races().LookupEntry(static_cast<std::uint32_t>(gs->create_race));
  if (race == nullptr) {
    lua_pushnil(state);
    lua_pushnil(state);
    return 2;
  }

  const std::string_view race_name =
      race->DisplayNameForSex(static_cast<std::uint32_t>(gs->create_sex));
  lua_pushlstring(state, race_name.data() != nullptr ? race_name.data() : "", race_name.size());
  lua_pushlstring(state,
                  race->client_file_string.data() != nullptr ? race->client_file_string.data() : "",
                  race->client_file_string.size());
  return 2;
}

int LuaGetFactionForRace(lua_State *state) {
  if (!lua_isnumber(state, 1)) {
    return luaL_error(state, "Usage: GetFactionForRace(index)");
  }

  const auto available_races = LoadGlueAvailableRaces(state);
  const auto &races = available_races.races;
  const std::uint32_t index = LuaNumberToZeroBasedU32Index(lua_tonumber(state, 1));
  if (static_cast<std::size_t>(index) >= races.size()) {
    lua_pushnil(state);
    lua_pushnil(state);
    return 2;
  }

  const auto &race = races[static_cast<std::size_t>(index)];
  GlueFactionStrings faction;
  if (available_races.from_dbc) {

    faction = ResolveGlueFactionByDisplayGroup(*GetDbcLoader(state), race.faction_id);
    if (!faction.valid) {
      faction = ResolveGlueFactionForRaceFromDbc(*GetDbcLoader(state), race.race_id);
    }
  } else {
    faction = ResolveGlueFactionForRaceFallback(race.race_id);
  }

  if (!faction.valid) {
    lua_pushnil(state);
    lua_pushnil(state);
    return 2;
  }

  PushLuaStringView(state, faction.name);
  PushLuaStringView(state, faction.token);
  return 2;
}

int LuaGetSelectedRace(lua_State *state) {
  const auto *gs = GetGameState(state);
  if (gs == nullptr) {
    lua_pushnumber(state, 0.0);

    return 1;
  }
  const auto races = GetGlueAvailableRaces(state);
  const int race_index = FindGlueAvailableRaceIndex(races, gs->create_race);
  if (race_index >= 0) {
    lua_pushnumber(state, static_cast<double>(race_index + 1));
    return 1;
  }
  lua_pushnumber(state, 0.0);

  return 1;
}

int LuaGetSelectedClass(lua_State *state) {
  const auto *gs = GetGameState(state);
  const auto *dbc = GetDbcLoader(state);
  if (gs == nullptr || dbc == nullptr) {
    return 0;
  }

  const auto *selected_class =
      dbc->chr_classes().LookupEntry(static_cast<std::uint32_t>(gs->create_class));
  if (selected_class == nullptr) {
    return 0;
  }

  int class_index = -1;
  for (std::size_t index = 0; index < dbc->chr_classes().entries().size(); ++index) {
    if (static_cast<int>(dbc->chr_classes().entries()[index].id) == gs->create_class) {

      class_index = static_cast<int>(index);
    }
  }
  if (class_index < 0) {
    return 0;
  }

  PushLuaStringView(state,
                    selected_class->DisplayNameForSex(static_cast<std::uint32_t>(gs->create_sex)));
  PushLuaStringView(state, selected_class->client_file_string);
  lua_pushnumber(state, static_cast<double>(class_index + 1));
  const int class_id = gs->create_class;
  const std::uint8_t flags = (class_id >= 0 && class_id < 12) ? kClassRoleFlags[class_id] : 0;
  lua_pushboolean(state, (flags & 2) != 0);
  lua_pushboolean(state, (flags & 4) != 0);
  lua_pushboolean(state, (flags & 8) != 0);
  return 6;
}

int LuaGetSelectedSex(lua_State *state) {
  const auto *gs = GetGameState(state);
  const int sex = (gs != nullptr) ? gs->create_sex : 0;
  const int mapped = (sex >= 0 && sex <= 2) ? kSexMapping[sex] : 2;
  lua_pushnumber(state, static_cast<double>(mapped));
  return 1;
}

static void SelectCharacterCreationRace(lua_State *state,
                                        GlueGameState &game_state,
                                        const std::uint32_t race_index) {
  const auto races = GetGlueAvailableRaces(state);
  if (static_cast<std::size_t>(race_index) >= races.size()) {
    return;
  }

  const int race_id = races[static_cast<std::size_t>(race_index)].race_id;
  if (race_id == game_state.create_race) {
    return;
  }

  SaveCreateCustomizationAppearanceToCache(
      game_state, FindGlueAvailableRaceIndex(races, game_state.create_race));

  if (game_state.char_customize_scene != nullptr) {
    game_state.char_customize_scene->ReleaseContent();
  }
  game_state.create_race = race_id;
  bool rebuilt_from_scratch = false;
  if (const auto *character = GetCustomizationSourceCharacter(game_state);
      character != nullptr &&
      static_cast<int>(character->race_id) == race_id &&
      (character->at_login_flags &
       (kAtLoginFactionChange | kAtLoginRaceChange)) != 0u) {
    ApplyCharacterSummaryToCreateSelection(game_state, *character);
  } else if (const auto *cached = FindCreateCustomizationAppearanceCache(
                 game_state, static_cast<int>(race_index), game_state.create_sex);
             cached != nullptr) {
    ApplyCreateCustomizationAppearanceFromCache(game_state, *cached);
  } else {
    auto &rng = RequireGlueCustomizationRandom(state);
    PickRandomAllowedCreateClassForRace(state, game_state, rng);
    ResetCreateCustomizationState(game_state);
    RandomizeCreateCustomizationAppearance(
        state, game_state, rng, game_state.create_class, true,
        CharacterCustomizationRandomizationOrder::SetupModel);
    rebuilt_from_scratch = true;
  }

  if (!rebuilt_from_scratch) {
    auto &rng = RequireGlueCustomizationRandom(state);
    PickRandomAllowedCreateClassForRace(state, game_state, rng);
  }
  openwow::vfs::SetDataPreloadSelectedRace(game_state.create_race);
  NormalizeCreateCustomizationStateWithDbc(state, game_state);
  RefreshCreateCustomizationDisplay(game_state);
  if (game_state.background_controller != nullptr) {
    game_state.background_controller->OnCharacterPreviewRebuilt();
  }
}

int LuaSetSelectedRace(lua_State *state) {
  if (lua_isnumber(state, 1) == 0) {
    return luaL_error(state, "Usage: SetSelectedRace(index)");
  }

  auto *game_state = GetGameState(state);
  if (game_state == nullptr) {
    return 0;
  }

  SelectCharacterCreationRace(
      state, *game_state,
      LuaNumberToZeroBasedU32Index(lua_tonumber(state, 1)));
  return 0;
}

int LuaSetSelectedClass(lua_State *state) {
  if (lua_isnumber(state, 1) == 0) {
    return luaL_error(state, "Usage: SetSelectedClass(index)");
  }

  auto *gs = GetGameState(state);
  if (gs == nullptr)
    return 0;

  const std::uint32_t index = LuaNumberToZeroBasedU32Index(lua_tonumber(state, 1));
  const auto class_ids = GetGlueAvailableClassIds(state);

  if (static_cast<std::size_t>(index) >= class_ids.size()) {
    return 0;
  }

  const int class_id = class_ids[static_cast<std::size_t>(index)];
  const auto available_classes = GetAvailableCreateClassesForRace(state, gs->create_race);
  if (FindAvailableCreateClassIndex(available_classes, class_id) < 0) {
    return 0;
  }

  if (gs->char_customize_scene != nullptr) {
    gs->char_customize_scene->ReleaseContent();
  }
  gs->create_class = class_id;
  NormalizeCreateCustomizationStateWithDbc(state, *gs);
  RefreshCreateCustomizationDisplay(*gs);
  if (gs->background_controller != nullptr) {
    gs->background_controller->OnCharacterPreviewRebuilt();
  }
  return 0;
}

int LuaSetSelectedSex(lua_State *state) {
  if (lua_isnumber(state, 1) == 0) {
    return luaL_error(state, "Usage: SetSelectedSex(index)");
  }

  auto *gs = GetGameState(state);
  if (gs == nullptr)
    return 0;

  const std::int32_t sex_id =
      openwow::ui::TruncateLuaNumberToI32(lua_tonumber(state, 1));
  for (int i = 0; i < static_cast<int>(std::size(kSexMapping)); ++i) {
    if (sex_id == kSexMapping[i]) {
      if (gs->create_sex == i) {
        return 0;
      }

      const auto races = GetGlueAvailableRaces(state);
      const int race_ordinal = FindGlueAvailableRaceIndex(races, gs->create_race);
      SaveCreateCustomizationAppearanceToCache(*gs, race_ordinal);
      if (gs->char_customize_scene != nullptr) {
        gs->char_customize_scene->ReleaseContent();
      }
      gs->create_sex = i;
      if (const auto *character = GetCustomizationSourceCharacter(*gs);
          character != nullptr && static_cast<int>(character->gender) == i &&
          (character->at_login_flags & kAtLoginCustomize) != 0u) {
        ApplyCharacterSummaryToCreateSelection(*gs, *character);
      } else if (const auto *cached =
                     FindCreateCustomizationAppearanceCache(*gs, race_ordinal, gs->create_sex);
                 cached != nullptr) {
        ApplyCreateCustomizationAppearanceFromCache(*gs, *cached);
      } else {
        ResetCreateCustomizationState(*gs);
        auto &rng = RequireGlueCustomizationRandom(state);
        RandomizeCreateCustomizationAppearance(
            state, *gs, rng, gs->create_class, true,
            CharacterCustomizationRandomizationOrder::SetupModel);
      }

      openwow::vfs::SetDataPreloadSelectedRace(gs->create_race);
      NormalizeCreateCustomizationStateWithDbc(state, *gs);
      RefreshCreateCustomizationDisplay(*gs);
      if (gs->background_controller != nullptr) {
        gs->background_controller->OnCharacterPreviewRebuilt();
      }
      return 0;
    }
  }

  return 0;
}

int LuaIsRaceClassValid(lua_State *state) {
  if (!lua_isnumber(state, 1) || !lua_isnumber(state, 2)) {
    return luaL_error(state, "Usage: IsRaceClassValid(raceIndex, classIndex)");
  }

  const auto races = GetGlueAvailableRaces(state);
  const auto class_ids = GetGlueAvailableClassIds(state);
  const std::int64_t race_idx =
      LuaNumberToZeroBasedI32Index(lua_tonumber(state, 1));
  const std::int64_t class_idx =
      LuaNumberToZeroBasedI32Index(lua_tonumber(state, 2));
  if (class_idx < 0 ||
      static_cast<std::uint64_t>(class_idx) >= class_ids.size()) {
    return 0;
  }

  int race_id = 0;
  if (race_idx >= 0 && static_cast<std::uint64_t>(race_idx) < races.size()) {
    race_id = races[static_cast<std::size_t>(race_idx)].race_id;
  }

  const int class_id = class_ids[static_cast<std::size_t>(class_idx)];
  if (IsValidRaceClass(state, race_id, class_id)) {
    lua_pushnumber(state, 1.0);
  } else {
    lua_pushnil(state);
  }
  return 1;
}

int LuaIsRaceClassRestricted(lua_State *state) {
  if (!lua_isnumber(state, 1) || !lua_isnumber(state, 2)) {
    return luaL_error(state, "Usage: Script_IsRaceClassRestricted(raceID, classID)");
  }
  const std::uint32_t race_id =
      openwow::ui::ClampLuaNumberToU32(lua_tonumber(state, 1));
  const std::uint32_t class_id =
      openwow::ui::ClampLuaNumberToU32(lua_tonumber(state, 2));

  if (race_id < 1u || race_id > 11u || race_id == 9u) {
    return luaL_error(state, "Script_IsRaceClassRestricted: unsupported race ID(%d)",
                      openwow::ui::SignedI32FromU32Bits(race_id));
  }

  const auto *gs = GetGameState(state);
  const std::uint32_t mask =
      gs != nullptr ? gs->race_class_restriction_masks[static_cast<std::size_t>(race_id)] : 0u;

  const double result = ((mask >> (class_id & 31u)) & 1u) != 0u ? 1.0 : 0.0;
  lua_pushnumber(state, result);
  return 1;
}

int LuaGetSelectedCategory(lua_State *state) {
  const auto *gs = GetGameState(state);
  if (gs == nullptr || gs->selected_realm_category_actual_index < 0) {
    lua_pushnumber(state, 1.0);
    return 1;
  }

  const auto layout = BuildRealmDisplayLayout(*gs);
  int visible_category = -1;
  for (const auto &bucket : layout.buckets) {
    if (bucket.realm_indices.empty()) {
      continue;
    }

    ++visible_category;
    if (static_cast<int>(bucket.actual_index) >= gs->selected_realm_category_actual_index) {
      lua_pushnumber(state, static_cast<double>(visible_category + 1));
      return 1;
    }
  }

  lua_pushnumber(state, 1.0);
  return 1;
}

int LuaGetCVarMax(lua_State *state) {

  const char *key = ReadGlueStringArgWithUsage(state, 1, "Usage: GetCVarMax(\"cvar\")");
  return openwow::ui::game::detail::PushScriptCVarRangeByName(
      state, key, openwow::ui::game::detail::ScriptCVarLookupMode::kGlue,
      openwow::ui::ScriptCVarRangeQuery::kMax);
}
int LuaGetCVarMin(lua_State *state) {

  const char *key = ReadGlueStringArgWithUsage(state, 1, "Usage: GetCVarMin(\"cvar\")");
  return openwow::ui::game::detail::PushScriptCVarRangeByName(
      state, key, openwow::ui::game::detail::ScriptCVarLookupMode::kGlue,
      openwow::ui::ScriptCVarRangeQuery::kMin);
}

static constexpr float kRadToDeg = 57.29578f;
static constexpr float kDegToRad = 0.017453292f;

int LuaGetCharacterCreateFacing(lua_State *s) {
  const auto *gs = GetGameState(s);
  float rad = (gs != nullptr) ? gs->create_facing : 0.0f;
  lua_pushnumber(s, static_cast<double>(rad * kRadToDeg));
  return 1;
}

int LuaGetCharacterSelectFacing(lua_State *s) {
  const auto *gs = GetGameState(s);
  float rad = (gs != nullptr) ? gs->select_facing : 0.0f;
  lua_pushnumber(s, static_cast<double>(rad * kRadToDeg));
  return 1;
}

int LuaSetCharacterCreateFacing(lua_State *s) {
  if (!lua_isnumber(s, 1)) {
    return luaL_error(s, "Usage: SetCharacterCreateFacing(degrees)");
  }

  auto *gs = GetGameState(s);
  if (gs != nullptr) {
    const float deg = static_cast<float>(lua_tonumber(s, 1));
    gs->create_facing = deg * kDegToRad;
    if (gs->char_customize_scene != nullptr &&
        gs->char_customize_scene->HasInitializedSelectedCharacterDisplay()) {
      gs->char_customize_scene->ApplyCreateFacing(gs->create_facing);
    }
  }
  return 1;
}

int LuaSetCharacterSelectFacing(lua_State *s) {
  if (!lua_isnumber(s, 1)) {
    return luaL_error(s, "Usage: SetCharacterSelectFacing(degrees)");
  }

  auto *gs = GetGameState(s);
  if (gs != nullptr) {
    const float deg = static_cast<float>(lua_tonumber(s, 1));
    const float radians = deg * kDegToRad;

    auto *scene = gs->char_select_scene;
    if (scene != nullptr && scene->HasConstructedSelectedCharacterDisplay()) {
      gs->select_facing = radians;

      if (scene->HasActiveSelectedCharacterDisplay()) {
        scene->ApplySelectFacing(gs->select_facing);
      }
    }
  }
  return 1;
}

int LuaSetCharSelectBackground(lua_State *s) {
  if (!lua_isstring(s, 1)) {
    return luaL_error(s, "Usage: SetCharSelectBackground(\"filename\")");
  }

  const char *raw = lua_tostring(s, 1);
  auto *gs = GetGameState(s);
  if (gs != nullptr) {
    if (auto *runtime = GetWidgetRuntime(s);
        runtime != nullptr && gs->background_controller != nullptr) {
      gs->background_controller->SetCharSelectBackground(*gs, *runtime, raw != nullptr ? raw : "");
    }
  }
  return 0;
}

int LuaUpdateCustomizationScene(lua_State *s) {
  auto *gs = GetGameState(s);
  if (gs != nullptr) {
    RefreshCreateCustomizationPreview(*gs);
  }
  return 0;
}

int LuaUpdateSelectionCustomizationScene(lua_State *s) {
  (void)s;
  return 0;
}

int LuaUpdateCustomizationBackground(lua_State *state) {
  (void)state;
  return 0;
}

int LuaCycleCharCustomization(lua_State *state) {
  if (!lua_isnumber(state, 1) || !lua_isnumber(state, 2)) {
    return luaL_error(state, "Usage: CycleCharCustomization(index, delta)");
  }

  const std::uint32_t index =
      openwow::ui::ClampLuaNumberToU32(lua_tonumber(state, 1)) - 1u;
  const std::int32_t delta =
      openwow::ui::TruncateLuaNumberToI32(lua_tonumber(state, 2));
  auto *gs = GetGameState(state);
  if (gs == nullptr)
    return 0;
  if (delta == 0)
    return 0;
  if (const auto *dbc = GetDbcLoader(state); dbc != nullptr && !dbc->char_sections().empty()) {
    auto customization = MakeCustomizationState(*gs);
    bool changed = false;
    switch (index) {
    case 0:
      changed = CycleSkinCustomizationWithDbc(customization, dbc->char_sections(), delta);
      break;
    case 1:
      changed = CycleFaceCustomizationWithDbc(customization, dbc->char_sections(), delta);
      break;
    case 2:
      changed = CycleHairStyleCustomizationWithDbc(customization, dbc->char_sections(),
                                                   dbc->character_facial_hair_styles(), delta);
      break;
    case 3:
      changed = CycleHairColorCustomizationWithDbc(customization, dbc->char_sections(), delta);
      break;
    case 4:
      changed = CycleFacialHairCustomizationWithDbc(customization, dbc->char_sections(),
                                                    dbc->character_facial_hair_styles(), delta);
      break;
    default:
      return 0;
    }
    if (changed) {
      StoreCustomizationState(*gs, customization);
    }

    RefreshCreateCustomizationDisplay(*gs);
    return 0;
  }

  return 0;
}

int LuaRandomizeCharCustomization(lua_State *state) {
  auto *gs = GetGameState(state);
  if (gs == nullptr)
    return 0;

  auto &rng = RequireGlueCustomizationRandom(state);
  RandomizeCreateCustomizationAppearance(state, *gs, rng, gs->create_class, false,
                                         CharacterCustomizationRandomizationOrder::ScriptRandomize);

  RefreshCreateCustomizationDisplay(*gs);
  return 0;
}

int LuaResetCharCustomize(lua_State *state) {
  auto *gs = GetGameState(state);

  if (gs == nullptr || gs->char_customize_model_frame.expired())
    return 0;
  auto &rng = RequireGlueCustomizationRandom(state);
  ResetCharCustomizeState(state, *gs, rng);
  return 0;
}

int LuaGetFacialHairCustomization(lua_State *state) {
  const auto *gs = GetGameState(state);
  const auto *dbc = GetDbcLoader(state);
  if (gs != nullptr && dbc != nullptr) {
    const auto *race =
        dbc->chr_races().LookupEntry(static_cast<std::uint32_t>(gs->create_race));
    if (race != nullptr) {
      std::string_view token;
      switch (gs->create_sex) {
      case 0:
        token = race->facial_hair_male;
        break;
      case 1:
        token = race->facial_hair_female;
        break;
      case 2:

        token = race->hair_customization;
        break;
      default:
        lua_pushstring(state, "NORMAL");
        return 1;
      }

      lua_pushlstring(state, token.data(), token.size());
      return 1;
    }
  }
  lua_pushstring(state, "NORMAL");
  return 1;
}

int LuaGetHairCustomization(lua_State *state) {
  const auto *gs = GetGameState(state);
  const auto *dbc = GetDbcLoader(state);
  if (gs != nullptr && dbc != nullptr) {
    const auto *race =
        dbc->chr_races().LookupEntry(static_cast<std::uint32_t>(gs->create_race));
    if (race != nullptr) {

      lua_pushlstring(state, race->hair_customization.data(),
                      race->hair_customization.size());
      return 1;
    }
  }
  lua_pushstring(state, "NORMAL");
  return 1;
}

int LuaSetCharCustomizeBackground(lua_State *s) {
  if (!lua_isstring(s, 1)) {
    return luaL_error(s, "Usage: SetCharCustomizeBackground(\"filename\")");
  }

  const char *raw = lua_tostring(s, 1);
  auto *gs = GetGameState(s);
  if (gs != nullptr) {
    if (auto *runtime = GetWidgetRuntime(s);
        runtime != nullptr && gs->background_controller != nullptr) {
      gs->background_controller->SetCharCustomizeBackground(*gs, *runtime,
                                                            raw != nullptr ? raw : "");
    }
  }
  return 0;
}

int LuaCustomizeExistingCharacter(lua_State *s) {
  if (lua_isnumber(s, 1) == 0) {
    return luaL_error(s, "Usage: CustomizeExistingCharacter(index)");
  }

  auto *gs = GetGameState(s);
  if (gs == nullptr)
    return 0;

  const std::uint32_t idx = LuaNumberToZeroBasedU32Index(lua_tonumber(s, 1));
  if (idx > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
      static_cast<std::size_t>(idx) >= gs->characters.size())
    return 0;

  gs->customize_source_character_index = idx;
  gs->ClearCreateCustomizationCache();

  const auto &ch = gs->characters[static_cast<std::size_t>(idx)];
  ApplyCharacterSummaryToCreateSelection(*gs, ch);
  openwow::vfs::SetDataPreloadSelectedRace(gs->create_race);
  NormalizeCreateCustomizationStateWithDbc(s, *gs);
  RefreshCreateCustomizationDisplay(*gs);
  return 0;
}

int LuaSetRealmSplitState(lua_State *s) {
  if (lua_isnumber(s, 1) == 0) {
    return luaL_error(s, "Usage: SetRealmSplitState(1 or 2)");
  }

  const auto split_state =
      static_cast<std::uint32_t>(
          openwow::ui::TruncateLuaNumberToI32(lua_tonumber(s, 1)));
  if (split_state <= 2) {
    SendRealmSplitPacket(s, split_state);
  }

  return 0;
}

int LuaRequestRealmSplitInfo(lua_State *s) {
  SendRealmSplitPacket(s, 0xFFFFFFFFu);
  return 0;
}

int LuaIsTournamentRealmCategory(lua_State *s) {
  if (lua_isnumber(s, 1) == 0) {
    return luaL_error(s, "Usage: IsTournamentRealmCategory(category)");
  }

  const auto *gs = GetGameState(s);
  if (gs == nullptr) {
    return 0;
  }

  const auto layout = BuildRealmDisplayLayout(*gs);
  const std::int32_t category =
      openwow::ui::TruncateLuaNumberToI32(lua_tonumber(s, 1));
  const auto *realm_category = ResolveVisibleRealmCategory(layout, category);
  return PushOneOrNoResults(s, realm_category != nullptr && realm_category->tournament());
}

int LuaIsInvalidTournamentRealmCategory(lua_State *s) {
  if (lua_isnumber(s, 1) == 0) {
    return luaL_error(s, "Usage: IsInvalidTournamentRealmCategory(category)");
  }

  const auto *gs = GetGameState(s);
  if (gs == nullptr) {
    return 0;
  }

  const auto layout = BuildRealmDisplayLayout(*gs);
  const std::int32_t category =
      openwow::ui::TruncateLuaNumberToI32(lua_tonumber(s, 1));
  const auto *realm_category = ResolveVisibleRealmCategory(layout, category);
  if (realm_category == nullptr || !realm_category->tournament()) {
    return 0;
  }

  const auto &client_services = openwow::net::ClientServices::Instance();
  return PushOneOrNoResults(s, !client_services.BypassesTournamentRealmCategoryValidation() &&
                                   !client_services.BypassesRealmCategoryLocaleValidation());
}
int LuaGetNumDeclensionSets(lua_State *s) {
  if (!lua_isstring(s, 1)) {
    return luaL_error(s, "Usage: GetNumDeclensionSets(\"name\", gender)");
  }

  const char *name = lua_tostring(s, 1);
  const int gender_index = openwow::ui::ReadLuaDeclensionGenderIndex(s, 2);

  lua_pushnumber(s, static_cast<lua_Number>(openwow::game::declension::GetNumSets(
                        name != nullptr ? name : "", gender_index)));
  return 1;
}

int LuaSetClearConfigData(lua_State *s) {
  bool clear_flag = true;
  if (lua_type(s, 1) == LUA_TBOOLEAN) {
    clear_flag = lua_toboolean(s, 1) != 0;
  }
  openwow::game::AccountData::Get().SetConfigCacheClearEnabled(clear_flag);
  return 0;
}

int LuaRestoreVideoEffectsDefaults(lua_State *s) {
  (void)s;
  auto &sys = openwow::ui::game::CVarSystem::Instance();
  ApplyRestoreVideoDefaultsByMode(sys,
                                  openwow::core::DisplayCallbackMode::ApplyStartupUiFaster);
  return 0;
}

int LuaRestoreVideoResolutionDefaults(lua_State *s) {
  (void)s;
  auto &sys = openwow::ui::game::CVarSystem::Instance();
  ApplyRestoreVideoResolutionDefaults(sys);
  return 0;
}

int LuaRestoreVideoStereoDefaults(lua_State *s) {
  (void)s;
  auto &sys = openwow::ui::game::CVarSystem::Instance();
  ApplyRestoreVideoStereoDefaults(sys);
  return 0;
}

int LuaRestartGx(lua_State *s) {
  (void)s;
  ExecuteGxRestartConsoleCommand();
  return 0;
}

int LuaShowContestNotice(lua_State *s) {
  if (LegalNoticeState::Get().ShouldShow(LegalNoticeId::kContest)) {
    lua_pushnumber(s, 1.0);
  } else {
    lua_pushnil(s);
  }
  return 1;
}

int LuaShowEULANotice(lua_State *s) {
  if (LegalNoticeState::Get().ShouldShow(LegalNoticeId::kEula)) {
    lua_pushnumber(s, 1.0);
  } else {
    lua_pushnil(s);
  }
  return 1;
}

int LuaShowScanningNotice(lua_State *s) {
  if (LegalNoticeState::Get().ShouldShow(LegalNoticeId::kScanning)) {
    lua_pushnumber(s, 1.0);
  } else {
    lua_pushnil(s);
  }
  return 1;
}

int LuaShowTOSNotice(lua_State *s) {
  if (LegalNoticeState::Get().ShouldShow(LegalNoticeId::kTos)) {
    lua_pushnumber(s, 1.0);
  } else {
    lua_pushnil(s);
  }
  return 1;
}

int LuaShowTerminationWithoutNoticeNotice(lua_State *s) {
  if (LegalNoticeState::Get().ShouldShow(LegalNoticeId::kTerminationWithoutNotice)) {
    lua_pushnumber(s, 1.0);
  } else {
    lua_pushnil(s);
  }
  return 1;
}

int LuaScanDLLStart(lua_State *s) {
  if (!lua_isstring(s, 1) || !lua_isstring(s, 2)) {
    return luaL_error(s, "Usage: ScanDLLStart(\"VersionURL\", \"DLLURL\")");
  }

  auto *gs = GetGameState(s);
  if (gs != nullptr) {
    gs->scan_dll.finished = true;
    if (gs->fire_event) {
      gs->fire_event("SCANDLL_FINISHED",
                     {MakeLuaString("OK"), MakeLuaString(""), MakeLuaNumber(0.0)});
    }
  }
  return 0;
}

int LuaScanDLLContinueAnyway(lua_State *s) {
  lua_settop(s, 0);
  if (auto *gs = GetGameState(s); gs != nullptr && !gs->scan_dll.continue_anyway_blocked &&
                                  (gs->scan_dll.status == ScanDllStatus::kError ||
                                   gs->scan_dll.status == ScanDllStatus::kComplete)) {
    gs->scan_dll.finished = true;
  }
  return 0;
}

int LuaSurveyNotificationDone(lua_State *s) {
  const int argument_count = lua_gettop(s);
  const bool accepted = ScriptReadBoolArgOrDefault(s, 1, false);
  auto &result_bridge = openwow::net::LoginSurveyResultBridge::Get();
  if (accepted) {
    (void)result_bridge.SubmitPendingResult();
  }
  result_bridge.ClearPendingResult();
  openwow::net::LoginSurveyDownloadBridge::Get().Clear();

  if (argument_count == 0) {
    lua_pushnil(s);
  }
  return 1;
}

int LuaPINEntered(lua_State *s) {
  constexpr int kMinimumPositionCount = 4;
  constexpr int kMaximumPositionCount = 10;
  std::array<std::uint8_t, kMaximumPositionCount> positions{};
  int count = 0;

  for (int arg_index = 1; arg_index <= kMaximumPositionCount; ++arg_index) {
    if (lua_type(s, arg_index) <= LUA_TNIL) {
      break;
    }
    if (!lua_isnumber(s, arg_index)) {
      return luaL_error(s, "Usage: PINEntered(4-10 key positions) - non-numeric arg #%d",
                        count + 1);
    }

    const lua_Number value = lua_tonumber(s, arg_index);
    positions[static_cast<std::size_t>(count++)] =
        static_cast<std::uint8_t>(openwow::ui::TruncateLuaNumberToI32(value));
  }

  if (count < kMinimumPositionCount || count > kMaximumPositionCount) {
    return luaL_error(s, "Usage: PINEntered(4-10 key positions) - bad number of args %d", count);
  }

  OpenCancelStatusDialog(GetGameState(s));

  auto& client_services = openwow::net::ClientServices::Instance();
  if (client_services.GetLoginConnectionType() ==
      openwow::net::LoginConnectionType::kBattleNet) {
    if (auto* login = client_services.GetBattlenetLogin(); login != nullptr) {
      login->RequestVirtualKeypadPIN(static_cast<unsigned int>(count),
                                     positions.data());
    }
  } else {
    openwow::net::LoginPinChallengeBridge::Get().SubmitPositions(
        std::span<const std::uint8_t>(positions.data(),
                                     static_cast<std::size_t>(count)));
  }

  positions.fill(0);
  return 0;
}

int LuaTokenEntered(lua_State *s) {
  if (lua_isstring(s, 1) == 0) {
    return 0;
  }

  const char *token = lua_tostring(s, 1);
  OpenCancelStatusDialog(GetGameState(s));
  const char *token_text = token != nullptr ? token : "";
  auto &client_services = openwow::net::ClientServices::Instance();
  if (client_services.GetLoginConnectionType() ==
      openwow::net::LoginConnectionType::kBattleNet) {
    if (auto *login = client_services.GetBattlenetLogin(); login != nullptr) {
      login->SubmitToken(token_text);
    }
  } else {
    openwow::net::LoginTokenChallengeBridge::Get().SubmitToken(token_text);
  }
  return 0;
}

int LuaMatrixEntered(lua_State *s) {
  if (lua_isnumber(s, 1) == 0) {
    return luaL_error(s, "Usage: MatrixEntered(number)");
  }

  const std::uint32_t digit = openwow::ui::ClampLuaNumberToU32(lua_tonumber(s, 1));
  if (digit > 9) {
    return luaL_error(s, "MatrixEntered: number must be 0 to 9");
  }

  auto &client_services = openwow::net::ClientServices::Instance();
  if (auto *login = client_services.GetBattlenetLogin();
      login != nullptr && login->HasPendingMatrixCardEntry()) {
    login->EnterMatrixCard(static_cast<std::uint8_t>(digit));
  } else if (client_services.GetLoginConnectionType() !=
             openwow::net::LoginConnectionType::kBattleNet) {
    openwow::net::LoginMatrixChallengeBridge::Get().EnterDigit(
        static_cast<std::uint8_t>(digit));
  }
  return 0;
}

int LuaMatrixCommit(lua_State *s) {
  auto &client_services = openwow::net::ClientServices::Instance();
  bool finalized = false;
  if (auto *login = client_services.GetBattlenetLogin(); login != nullptr) {
    finalized = login->CommitPendingMatrixCardEntry();
  } else if (client_services.GetLoginConnectionType() !=
             openwow::net::LoginConnectionType::kBattleNet) {
    finalized = openwow::net::LoginMatrixChallengeBridge::Get().CommitEntry();
  }
  if (finalized) {
    OpenCancelStatusDialog(GetGameState(s));
  }
  return 0;
}

int LuaMatrixRevert(lua_State * ) {
  auto &client_services = openwow::net::ClientServices::Instance();
  if (auto *login = client_services.GetBattlenetLogin();
      login != nullptr && login->HasPendingMatrixCardEntry()) {
    login->RevertMatrixCard();
  } else if (client_services.GetLoginConnectionType() !=
             openwow::net::LoginConnectionType::kBattleNet) {
    openwow::net::LoginMatrixChallengeBridge::Get().RevertEntry();
  }
  return 0;
}

int LuaGetMatrixCoordinates(lua_State *s) {
  std::optional<openwow::net::LoginMatrixCoordinates> coordinates;
  auto &client_services = openwow::net::ClientServices::Instance();
  if (auto *login = client_services.GetBattlenetLogin(); login != nullptr) {
    std::uint32_t first = 0;
    std::uint32_t second = 0;
    if (login->GetPendingMatrixCardCoordinates(first, second)) {
      coordinates = openwow::net::LoginMatrixCoordinates{
          .column = first,
          .row = second,
      };
    }
  } else if (client_services.GetLoginConnectionType() !=
             openwow::net::LoginConnectionType::kBattleNet) {
    coordinates = openwow::net::LoginMatrixChallengeBridge::Get().coordinates();
  }
  if (!coordinates.has_value()) {
    lua_pushnil(s);
    lua_pushnil(s);
    lua_pushnil(s);
    return 3;
  }

  lua_pushnumber(s, 1.0);
  lua_pushnumber(s, static_cast<lua_Number>(coordinates->column));
  lua_pushnumber(s, static_cast<lua_Number>(coordinates->row));
  return 3;
}

int LuaStatusDialogClick(lua_State *s) {
  auto *gs = GetGameState(s);
  if (gs == nullptr)
    return 0;

  gs->wants_cancel_login = true;
  return 0;
}

int LuaGetRandomName(lua_State *s) {
  static RandomNameDictionaryCache cache;

  const auto *dbc = GetDbcLoader(s);
  const auto *gs = GetGameState(s);
  const std::uint32_t race_id = gs != nullptr ? static_cast<std::uint32_t>(gs->create_race) : 0u;
  const std::uint32_t sex = gs != nullptr ? static_cast<std::uint32_t>(gs->create_sex) : 0u;

  cache.RebuildIfNeeded(dbc, race_id, sex);

  std::string name;
  if (!cache.dictionary.empty()) {
    auto &rng = RequireGlueCustomizationRandom(s);
    do {
      name = cache.dictionary.Generate(
          [&rng](const std::uint32_t count) { return rng.SelectOrdinal(count); }, 14u);
    } while (!IsRandomNameAcceptedByGlue(name));
  }

  lua_pushstring(s, name.c_str());
  return 1;
}

static int PushPaidChangeRaceIndex(lua_State *s) {
  const auto *gs = GetGameState(s);
  if (gs != nullptr) {
    if (const auto *character = GetCustomizationSourceCharacter(*gs); character != nullptr) {
      const auto races = GetGlueAvailableRaces(s);
      const int race_index =
          FindGlueAvailableRaceIndex(races, static_cast<int>(character->race_id));
      if (race_index >= 0) {
        lua_pushnumber(s, static_cast<double>(race_index + 1));
        return 1;
      }
    }
  }
  lua_pushnumber(s, 0.0);
  return 1;
}

int LuaPaidChange_GetCurrentRaceIndex(lua_State *s) {
  return PushPaidChangeRaceIndex(s);
}

int LuaPaidChange_GetPreviousRaceIndex(lua_State *s) {
  lua_pushnumber(s, 0.0);
  return 1;
}

int LuaPaidChange_GetCurrentClassIndex(lua_State *s) {
  const auto *gs = GetGameState(s);
  if (gs != nullptr) {
    if (const auto *character = GetCustomizationSourceCharacter(*gs); character != nullptr) {
      const auto class_ids = GetGlueAvailableClassIds(s);
      for (std::size_t index = 0; index < class_ids.size(); ++index) {
        if (class_ids[index] == static_cast<int>(character->class_id)) {
          lua_pushnumber(s, static_cast<double>(index + 1u));
          return 1;
        }
      }
    }
  }
  lua_pushnumber(s, 0.0);
  return 1;
}

int LuaGetCreateBackgroundModel(lua_State *s) {
  if (openwow::data::IsOnlineModeActive()) {
    lua_pushstring(s, "CharacterSelect");
    return 1;
  }

  const auto *gs = GetGameState(s);
  if (gs == nullptr) {
    lua_pushstring(s, "");
    return 1;
  }

  if (gs->create_class == 6) {
    const std::string_view class_tag = LookupGlueBackgroundClassToken(s, gs->create_class);
    if (!class_tag.empty()) {
      PushLuaStringView(s, class_tag);
      return 1;
    }
  }

  const std::string_view race_tag = LookupGlueBackgroundRaceToken(s, gs->create_race);
  if (!race_tag.empty()) {
    PushLuaStringView(s, race_tag);
    return 1;
  }

  lua_pushstring(s, "");
  return 1;
}

int LuaPaidChange_GetName(lua_State *s) {
  if (const auto *gs = GetGameState(s); gs != nullptr) {
    if (const auto *character = GetCustomizationSourceCharacter(*gs); character != nullptr) {
      lua_pushstring(s, character->name.c_str());
      return 1;
    }
  }
  lua_pushnil(s);
  return 1;
}

namespace {

void RequireAccountMsgHeadersLoaded(lua_State *state, const char *function_name) {
  if (openwow::game::AccountMsg::Get().GetState() != openwow::game::AccountMsgState::kLoaded) {
    luaL_error(state, "%s() failed because the headers are not loaded", function_name);
  }
}

std::int32_t RequireLoadedAccountMsgIndex(lua_State *state, const char *function_name) {
  auto &account_msg = openwow::game::AccountMsg::Get();
  RequireAccountMsgHeadersLoaded(state, function_name);

  if (lua_isnumber(state, 1) == 0) {
    luaL_error(state, "Usage: %s(number)", function_name);
  }

  const auto raw_index = openwow::ui::TruncateLuaNumberToI32(lua_tonumber(state, 1));
  if (raw_index < 0 || raw_index >= static_cast<int>(account_msg.GetHeaderCount())) {
    luaL_error(state, "Usage: %s(number) failed because the number is out of range", function_name);
  }

  return raw_index;
}

}

int LuaAccountMsg_GetNumUnreadMsgs(lua_State *s) {
  RequireAccountMsgHeadersLoaded(s, "AccountMsg_GetNumUnreadMsgs");
  lua_pushnumber(s, static_cast<lua_Number>(openwow::game::AccountMsg::Get().GetNumUnreadMsgs()));
  return 1;
}

int LuaAccountMsg_GetIndexNextUnreadMsg(lua_State *s) {
  RequireAccountMsgHeadersLoaded(s, "AccountMsg_GetHeaderSubject");

  std::int32_t start_index = -1;
  if (lua_isnumber(s, 1) != 0) {

    start_index = openwow::ui::TruncateLuaNumberToI32(lua_tonumber(s, 1));
  }

  auto &account_msg = openwow::game::AccountMsg::Get();
  const auto index = account_msg.GetIndexNextUnreadMsg(start_index);
  const auto header_count = static_cast<std::int32_t>(account_msg.GetHeaderCount());

  lua_pushnumber(s, static_cast<lua_Number>(index));

  std::int32_t priority = 0;
  if (index >= 0 && index < header_count) {
    priority = account_msg.GetHeaderPriority(static_cast<std::uint32_t>(index));
  }

  lua_pushnumber(s, static_cast<lua_Number>(priority));
  return 2;
}

int LuaAccountMsg_GetHeaderSubject(lua_State *s) {
  const auto index = RequireLoadedAccountMsgIndex(s, "AccountMsg_GetHeaderSubject");
  const auto subject =
      openwow::game::AccountMsg::Get().GetHeaderSubject(static_cast<std::uint32_t>(index));
  lua_pushoptstring(s, subject);
  return 1;
}

int LuaAccountMsg_LoadBody(lua_State *s) {
  const auto index = RequireLoadedAccountMsgIndex(s, "AccountMsg_LoadBody");
  auto &account_msg = openwow::game::AccountMsg::Get();
  if (auto *glue_runtime = GetGlueRuntime(s); glue_runtime != nullptr) {
    account_msg.SetBodyLoadedNotifier(glue_runtime->MakeAsyncRegisteredEventPoster(
        GlueEventName(GlueScriptEvent::AccountMessagesBodyLoaded)));
  } else {
    account_msg.SetBodyLoadedNotifier({});
  }
  const auto message_id = account_msg.ResolveHeaderMessageId(static_cast<std::uint32_t>(index));
  if (!message_id.has_value() || !account_msg.LoadBody(*message_id)) {
    return luaL_error(s, "AccountMsg_LoadHeaders() failed");
  }
  return 0;
}

int LuaAccountMsg_LoadHeaders(lua_State *s) {
  lua_settop(s, 0);
  auto &account_msg = openwow::game::AccountMsg::Get();
  if (auto *glue_runtime = GetGlueRuntime(s); glue_runtime != nullptr) {
    account_msg.SetHeadersLoadedNotifier(glue_runtime->MakeAsyncRegisteredEventPoster(
        GlueEventName(GlueScriptEvent::AccountMessagesHeadersLoaded)));
  } else {
    account_msg.SetHeadersLoadedNotifier({});
  }
  if (!account_msg.LoadHeaders()) {
    return luaL_error(s, "AccountMsg_LoadHeaders() failed");
  }
  return 0;
}

int LuaAccountMsg_SetMsgRead(lua_State *s) {
  const auto index = RequireLoadedAccountMsgIndex(s, "AccountMsg_SetMsgRead");
  if (!openwow::game::AccountMsg::Get().SetMsgRead(static_cast<std::uint32_t>(index))) {
    return luaL_error(s, "Usage: AccountMsg_SetMsgRead(number) failed due to an URL error");
  }
  return 0;
}

int LuaAccountMsg_GetHeaderPriority(lua_State *s) {
  const auto index = RequireLoadedAccountMsgIndex(s, "AccountMsg_GetHeaderPriority");
  const auto priority =
      openwow::game::AccountMsg::Get().GetHeaderPriority(static_cast<std::uint32_t>(index));
  lua_pushnumber(s, static_cast<lua_Number>(priority));
  return 1;
}

int LuaAccountMsg_GetNumTotalMsgs(lua_State *s) {
  RequireAccountMsgHeadersLoaded(s, "AccountMsg_GetNumTotalMsgs");
  lua_pushnumber(s, static_cast<lua_Number>(openwow::game::AccountMsg::Get().GetHeaderCount()));
  return 1;
}

int LuaAccountMsg_GetNumUnreadUrgentMsgs(lua_State *s) {
  RequireAccountMsgHeadersLoaded(s, "AccountMsg_GetNumUnreadMsgs");
  lua_pushnumber(
      s, static_cast<lua_Number>(openwow::game::AccountMsg::Get().GetNumUnreadUrgentMsgs()));
  return 1;
}

int LuaAccountMsg_GetIndexHighestPriorityUnreadMsg(lua_State *s) {
  RequireAccountMsgHeadersLoaded(s, "AccountMsg_GetHeaderSubject");

  auto &account_msg = openwow::game::AccountMsg::Get();
  const auto index = account_msg.GetIndexHighestPriorityUnreadMsg();
  const auto header_count = static_cast<std::int32_t>(account_msg.GetHeaderCount());

  lua_pushnumber(s, static_cast<lua_Number>(index));

  std::int32_t priority = 0;
  if (index >= 0 && index < header_count) {
    priority = account_msg.GetHeaderPriority(static_cast<std::uint32_t>(index));
  }

  lua_pushnumber(s, static_cast<lua_Number>(priority));
  return 2;
}

int LuaAccountMsg_GetBody(lua_State *s) {
  const auto &account_msg = openwow::game::AccountMsg::Get();
  if (account_msg.GetBodyState() != openwow::game::AccountMsgState::kLoaded) {
    return luaL_error(s, "AccountMsg_GetBody() failed because the body was not loaded");
  }

  lua_pushoptstring(s, account_msg.GetBody().body_text);
  return 1;
}

int LuaLaunchAddOnURL(lua_State *s) {
  const std::string &addon_name =
      openwow::ui::game::detail::RequireScriptAddonNameByIndex(s, "Usage: LaunchAddOnURL(index)");
  const char *url = openwow::ui::AddOnsData::Get().GetAddonUrl(addon_name.c_str());

  if (url != nullptr && url[0] != '\0') {
    if (auto *host = GetGlueHost(s); host != nullptr) {
      host->OpenUrl(url);
    }
  }
  return 0;
}

int LuaQuitGameAndRunLauncher(lua_State *state) {
  (void)state;
  QuitGameAndRunLauncherImpl();
  return 0;
}
int LuaConsoleExec(lua_State *s) {
  if (lua_isstring(s, 1) == 0) {
    return luaL_error(s, "Usage: ConsoleExec(\"console_command\")");
  }

  const char *command = lua_tostring(s, 1);
  openwow::core::ida::Console_Execute(command != nullptr ? command : "", false);
  return 0;
}

int LuaGetPatchDownloadProgress(lua_State *s) {
  lua_pushnumber(s,
                 static_cast<lua_Number>(openwow::ui::glue::CGlueMgr_GetPatchDownloadProgress()));
  return 1;
}

int LuaPatchDownloadApply(lua_State *s) {
  const auto *gs = GetGameState(s);
  const GlueEventCallback fire_event = gs != nullptr ? gs->fire_event : GlueEventCallback{};
  const PatchDownloadApplyResult patch_result = ApplyDownloadedPatchWithResult(
      {}, [&](const PatchDownloadApplyResult failure) {
        CGlueMgr_HandlePatchFailure(fire_event, static_cast<int>(failure), 0);
      });
  if (patch_result == PatchDownloadApplyResult::kSuccess) {
    openwow::core::RequestClientShutdownWithErrorCode(0);
  }
  return 0;
}

int LuaPatchDownloadCancel(lua_State *s) {
  openwow::net::LoginPatchDownloadBridge::Get().AbortActiveDownload();
  openwow::net::ClientServices::Instance().CancelLoginFileTransfer();
  CGlueMgr_ResetStateToIdle();
  openwow::net::ClientServices::Instance().Disconnect();

  if (auto *gs = GetGameState(s); gs != nullptr && gs->fire_event) {
    Login_SetScreen(gs->fire_event, "login");
  }

  return 0;
}

int LuaGetLocale(lua_State *state) {
  const std::string locale = openwow::ui::ScriptActiveLocaleName();
  lua_pushstring(state, locale.c_str());
  return 1;
}

int LuaGetText(lua_State *state) {
  return openwow::ui::LuaGetLocalizedGlobalText(state);
}

int LuaIsTrialAccount(lua_State *state) {
  auto &client_services = openwow::net::ClientServices::Instance();
  if (client_services.HasLoginConnection()) {
    lua_pushwowbool(state, client_services.IsLoginConnectionTrialAccount());
    return 1;
  }

  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  if (!cvars.Exists("converted")) {
    lua_pushnil(state);
    return 1;
  }

  lua_pushwowbool(state, cvars.GetCVarInt("converted") == 0);
  return 1;
}

int LuaGetAccountExpansionLevel(lua_State *state) {
  return openwow::ui::PushLuaLiveExpansionLevel(state);
}

int LuaSetUsesToken(lua_State *state) {
  if (lua_type(state, 1) != LUA_TBOOLEAN) {
    return luaL_error(state, "Usage: SetUsesToken( 0 | 1 )");
  }

  auto &sys = openwow::ui::game::CVarSystem::Instance();
  sys.SetCVar("g_accountUsesToken", lua_toboolean(state, 1) ? "1" : "0", true);
  return 0;
}

int LuaGetUsesToken(lua_State *state) {
  const bool uses_token =
      openwow::ui::game::CVarSystem::Instance().GetCVarBool("g_accountUsesToken");
  lua_pushboolean(state, uses_token ? 1 : 0);
  return 1;
}

int LuaSortRealms(lua_State *state) {
  const char *criteria = lua_isstring(state, 1) ? lua_tostring(state, 1) : nullptr;
  if (criteria == nullptr) {
    return luaL_error(state, "Usgae: SortRealms(\"type\")");
  }
  const std::string crit(criteria);

  int sort_type = 2;
  if (openwow::text::EqualsIgnoreCaseAscii(crit, "load")) {
    sort_type = 1;
  } else if (openwow::text::EqualsIgnoreCaseAscii(crit, "characters")) {
    sort_type = 0;
  } else if (openwow::text::EqualsIgnoreCaseAscii(crit, "mode")) {
    sort_type = 3;
  }

  if (auto *gs = GetGameState(state); gs != nullptr) {
    UpdateRealmSortState(*gs, sort_type);
    if (gs->fire_event) {
      gs->fire_event("OPEN_REALM_LIST", {});
    }
  }
  return 0;
}

int LuaSetPreferredInfo(lua_State *state) {
  if (lua_isnumber(state, 1) == 0) {
    return luaL_error(state, "Usage: SetPreferredInfo(index, pvp, rp)");
  }
  const std::int32_t category_idx =
      openwow::ui::TruncateLuaNumberToI32(lua_tonumber(state, 1));
  const bool wants_pvp = ScriptReadBoolArgOrDefault(state, 2, false);
  const bool wants_rp = ScriptReadBoolArgOrDefault(state, 3, false);

  auto *gs = GetGameState(state);
  if (gs == nullptr) {
    return 0;
  }

  const auto layout = BuildRealmDisplayLayout(*gs);
  if (!layout.has_category_registry) {
    return 0;
  }
  const auto actual_category_index = MapVisibleCategoryIndex(
      layout, static_cast<std::int64_t>(category_idx) - 1);
  gs->selected_realm_category_actual_index = static_cast<int>(actual_category_index);

  if (gs->fire_event == nullptr) {
    return 0;
  }

  const auto suggestion =
      FindPreferredRealmSuggestion(*gs, layout, actual_category_index, wants_pvp, wants_rp);
  if (suggestion.has_value()) {
    gs->fire_event("SUGGEST_REALM", {MakeLuaNumber(static_cast<double>(category_idx)),
                                     MakeLuaNumber(static_cast<double>(*suggestion + 1u))});
  } else {
    gs->fire_event("OPEN_REALM_LIST", {});
  }

  return 0;
}

int LuaGetSelectBackgroundModel(lua_State *state) {
  if (lua_isnumber(state, 1) == 0) {
    return luaL_error(state, "Usage: GetSelectBackgroundModel(index)");
  }

  const std::int64_t idx = LuaNumberToZeroBasedI32Index(lua_tonumber(state, 1));
  if (openwow::data::IsOnlineModeActive()) {
    lua_pushstring(state, "CharacterSelect");
    return 1;
  }

  auto *gs = GetGameState(state);

  if (gs == nullptr || idx < 0 || static_cast<std::uint64_t>(idx) >= gs->characters.size()) {
    PushLuaStringView(state, LookupGlueBackgroundRaceToken(state, 2));
    return 1;
  }

  const auto &character = gs->characters[static_cast<std::size_t>(idx)];
  if (character.class_id == 6) {
    PushLuaStringView(state, LookupGlueBackgroundClassToken(state, character.class_id));
  } else {
    PushLuaStringView(state, LookupGlueBackgroundRaceToken(state, character.race_id));
  }
  return 1;
}

GlueSpawnProcessFn
SetQuitGameAndRunLauncherSpawnProcessForTesting(GlueSpawnProcessFn spawn_process) {
  auto &current_spawn_process = QuitGameAndRunLauncherSpawnProcess();
  const auto previous_spawn_process = current_spawn_process;
  current_spawn_process =
      spawn_process != nullptr ? spawn_process : &openwow::core::SThread_SpawnProcess;
  return previous_spawn_process;
}

}

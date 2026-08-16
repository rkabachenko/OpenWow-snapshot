#include "openwow/ui/lua_c_api_convenience.h"

#include "openwow/ui/game/api/game_lua_api_system.h"
#include "openwow/game/actions/held_cursor/held_cursor.h"
#include "openwow/ui/game/camera_lua_bindings.h"
#include "openwow/ui/game/api/held_cursor_lua_api.h"
#include "openwow/ui/game/api/game_lua_api_internal.h"
#include "openwow/ui/game/runtime/lua/held_cursor_lua_binding.h"
#include "openwow/ui/game/framescript/core/frame_region_geometry.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/ui/surfaces/game/runtime/system_message_dispatch.h"
#include "openwow/ui/game/lua_addon_memory_tracker.h"
#include "openwow/ui/game/lua_cpu_profiler.h"
#include "openwow/ui/game/lua_mouse_button_context.h"
#include "openwow/ui/game/saved_variables.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/nameplate_system.h"
#include "openwow/ui/game/ui_error_manager.h"
#include "openwow/ui/game/ui_coordination.h"
#include "openwow/ui/lua_binding_registry.h"
#include "openwow/ui/lua_base_overrides.h"
#include "openwow/ui/lua_client_environment.h"
#include "openwow/ui/lua_numeric.h"
#include "openwow/ui/lua_post_hook_closure.h"
#include "openwow/ui/lua_result_capacity.h"
#include "openwow/ui/lua_tick_count.h"
#include "openwow/ui/retail_client_build.h"
#include "openwow/ui/display/settings/adapters/platform/display_mode_catalog.h"
#include "openwow/ui/script_server_name.h"
#include "openwow/ui/ui_aspect_scales.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/ui/game/runtime/world_ui_runtime_context.h"
#include "openwow/ui/game/runtime/movie_recording_runtime.h"
#include "openwow/core/client_init.h"
#include "openwow/core/console.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/screenshot_system.h"
#include "openwow/ui/surfaces/game/bindings/world_ui_lifecycle_lua_adapter.h"
#include "openwow/data/formats/dbc/dbc_enums.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/streaming_init.h"
#include "openwow/game/battlefield_info.h"
#include "openwow/game/combat_rating.h"
#include "openwow/game/actions/held_cursor/adapters/platform/cursor_surface.h"
#include "openwow/game/group_system.h"
#include "openwow/game/interaction_sender.h"
#include "openwow/game/inventory/player_inventory_replica.h"
#include "openwow/game/actions/bindings/application/binding_profiles.h"
#include "openwow/game/actions/bindings/adapters/retail/modified_click_adapter.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/lcd_system.h"
#include "openwow/game/movement_info.h"
#include "openwow/game/object_types.h"
#include "openwow/game/rest_state.h"
#include "openwow/game/spell_action.h"
#include "openwow/game/tracking_system.h"
#include "openwow/game/unit_frame_data.h"
#include "openwow/game/vehicle_helpers.h"
#include "openwow/game/vehicle_passenger.h"
#include "openwow/game/vehicle_system.h"
#include "openwow/input/input_manager.h"
#include "openwow/net/client_services.h"
#include "openwow/net/wotlk/wow_client_connection.h"
#include "openwow/ui/game/event_dispatcher.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/secure_execution.h"
#include "openwow/ui/game/world_map_system.h"

#include "openwow/render/models/characters/portrait_icon_texture.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace openwow::ui::game::detail {

using namespace openwow::game;

static constexpr const char *kKeyBindingRegistryKey = "openwow.key_binding_manager";
static constexpr int kAssistUnitNoTargetMessage = 199;
static constexpr int kAssistUnitUnknownUnitMessage = 314;
static constexpr std::uint32_t kStuckSpellId = 7355;

struct StaticConstantRegistration {
  const char *name;
  int value;
};

constexpr StaticConstantRegistration kStaticConstantRegistrations[] = {
    {"STATIC_CONSTANTS", 0},
    {"Loot", 1},
    {"AuctionHouse", 2},
    {"Mail", 3},
    {"Chat", 4},
    {"Movement", 5},
    {"Spell", 6},
};

static runtime::WorldUiRuntimeContext *GetGameUiManager(lua_State *L);

static BindingProfiles *GetBindingProfiles(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, kKeyBindingRegistryKey);
  auto *mgr = static_cast<BindingProfiles *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  return mgr;
}

static void CallLuaErrorHandlerIfPresent(lua_State *L, int error_index,
                                         const char* error_handler_registry_key) {
  error_index = lua_absindex(L, error_index);
  lua_getfield(L, LUA_REGISTRYINDEX, error_handler_registry_key);
  if (lua_isfunction(L, -1) == 0) {
    lua_pop(L, 1);
    return;
  }

  lua_pushvalue(L, error_index);
  if (lua_pcall(L, 1, 0, 0) != 0) {
    lua_pop(L, 1);
  }
}

static void PushLuaUnsignedInt(lua_State *L, const std::int32_t value) {
  lua_pushnumber(L, static_cast<lua_Number>(static_cast<std::uint32_t>(value)));
}

static std::uint16_t ResolveModifierState(lua_State *L) {
  if (const auto modifier_state = GetCurrentModifierStateOverride(L); modifier_state.has_value()) {
    return *modifier_state;
  }
  return static_cast<std::uint16_t>(SDL_GetModState());
}

static void PushModifierQueryResult(lua_State *L, SDL_Keymod mask) {
  lua_pushwowbool(L, (ResolveModifierState(L) & mask) != 0);
}

static void DisplayAssistUnitLookupFailure(const std::string &unit_id) {
  if (!unit_id.empty() && !openwow::text::EqualsIgnoreCaseAscii(unit_id, "target")) {
    DisplaySystemMessage(kAssistUnitUnknownUnitMessage, unit_id.c_str());
    return;
  }

  DisplaySystemMessage(kAssistUnitNoTargetMessage);
}

struct WeaponEnchantLuaTriplet {
  bool has_expiration = false;
  std::uint32_t expiration_ms = 0;
  std::int16_t charges = 0;
};

static const CGPlayer_C *GetActivePlayerForSession(const WorldSession &session) {
  return session.objects().GetActivePlayer();
}

static const CGItem_C *GetPlayerEquippedItem(const WorldSession &session, const CGPlayer_C &player,
                                             const std::uint8_t slot) {
  const auto item_guid = player.GetEquippedItem(slot);
  if (item_guid.IsEmpty()) {
    return nullptr;
  }

  return session.objects().GetItem(item_guid);
}

static std::optional<WeaponEnchantLuaTriplet>
ResolveWeaponEnchantLuaTriplet(lua_State *L, const std::uint8_t slot) {
  const auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return std::nullopt;
  }

  const auto *player = GetActivePlayerForSession(*session);
  if (player == nullptr) {
    return std::nullopt;
  }

  const auto *item = GetPlayerEquippedItem(*session, *player, slot);
  if (item == nullptr) {
    return std::nullopt;
  }

  if (item->GetEnchantIdIfVisible(kEnchantSlotTemporary) == 0) {
    return std::nullopt;
  }

  return WeaponEnchantLuaTriplet{
      .has_expiration = item->GetEnchantDurationFieldIfVisible(kEnchantSlotTemporary) != 0,
      .expiration_ms = item->GetEnchantTimeRemainingMs(kEnchantSlotTemporary),
      .charges = item->GetEnchantChargesIfVisible(kEnchantSlotTemporary),
  };
}

static void PushWeaponEnchantLuaTriplet(lua_State *L,
                                        const std::optional<WeaponEnchantLuaTriplet> &triplet) {
  if (!triplet.has_value()) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return;
  }

  lua_pushwowbool(L, true);
  if (triplet->has_expiration) {
    lua_pushnumber(L, static_cast<lua_Number>(triplet->expiration_ms));
  } else {
    lua_pushnil(L);
  }
  lua_pushnumber(L, static_cast<lua_Number>(triplet->charges));
}

static constexpr std::string_view kPortraitPetTexture =
    "Interface\\CharacterFrame\\TemporaryPortrait-Pet";
static constexpr std::string_view kPortraitVehicleOrganicTexture =
    "Interface\\CharacterFrame\\TemporaryPortrait-Vehicle-Organic";
static constexpr std::string_view kPortraitVehicleMechanicalTexture =
    "Interface\\CharacterFrame\\TemporaryPortrait-Vehicle-Mechanical";

static std::string_view ResolveTemporaryPortraitTexturePathForVehicleSeat(
    const openwow::data::dbc::VehicleSeatEntry *seat_entry) {
  if (seat_entry == nullptr) {
    return kPortraitPetTexture;
  }

  switch (seat_entry->temporary_portrait_type) {
  case 0:
    return kPortraitVehicleOrganicTexture;
  case 1:
    return kPortraitVehicleMechanicalTexture;
  default:
    return kPortraitPetTexture;
  }
}

static std::optional<std::string_view>
TryResolveTrackedPartyControlledTemporaryPortraitTexturePath(const WorldSession &session,
                                                             const ObjectGuid guid) {
  if (guid.IsEmpty()) {
    return std::nullopt;
  }

  auto &group_system = GroupSystem::Get();
  int party_index = -1;
  if (!group_system.FindPartyMemberByControlledUnitGuid(
          session.objects(), guid.GetRawValue(), &party_index) ||
      party_index < 0) {
    return std::nullopt;
  }

  const auto owner_guid =
      group_system.GetTrackedPartyMemberGuid(static_cast<std::uint32_t>(party_index));
  if (owner_guid == 0) {
    return std::nullopt;
  }

  const auto cached = session.party_stats().GetCachedMember(owner_guid);
  if (!cached.has_value() || cached->stats.pet_guid != guid.GetRawValue()) {
    return std::nullopt;
  }

  if (cached->stats.vehicle_seat == 0) {
    return kPortraitPetTexture;
  }

  return ResolveTemporaryPortraitTexturePathForVehicleSeat(
      openwow::game::LookupVehicleSeatEntryById(session, cached->stats.vehicle_seat));
}

static int PushPortraitTextureTarget(lua_State *L) {
  if (lua_isstring(L, 1) != 0) {
    const std::string texture_name = SafeLuaString(L, 1);
    lua_getglobal(L, texture_name.c_str());
    if (lua_istable(L, -1) != 0) {
      const int texture_index = lua_absindex(L, -1);
      const char *type_name = openwow::ui::BorrowRawLuaStringField(L, texture_index, "__ow_type");
      if (HasLuaScriptObjectThis(L, texture_index) && TextureMatchesObjectType(type_name)) {
        return texture_index;
      }
    }

    lua_pop(L, 1);
    return luaL_error(L, "SetPortraitToTexture(): Couldn't find texture named '%s'",
                      texture_name.c_str());
  }

  if (lua_type(L, 1) != LUA_TTABLE) {
    const std::string texture_name = SafeLuaString(L, 1);
    return luaL_error(L, "SetPortraitToTexture(): Couldn't find texture named '%s'",
                      texture_name.c_str());
  }

  lua_pushvalue(L, 1);
  const int texture_index = lua_absindex(L, -1);
  if (!HasLuaScriptObjectThis(L, texture_index)) {
    lua_pop(L, 1);
    return luaL_error(L, "SetPortraitToTexture(): Couldn't find 'this' in texture object");
  }

  const char *type_name = openwow::ui::BorrowRawLuaStringField(L, texture_index, "__ow_type");
  if (!TextureMatchesObjectType(type_name)) {
    lua_pop(L, 1);
    return luaL_error(L, "SetPortraitToTexture(): Wrong object type, expected texture");
  }

  return texture_index;
}

static void SetMaskedPortraitIconTexture(lua_State *L, const int texture_index,
                                         const std::string_view source_path) {
  BindPortraitTexturePath(L, texture_index,
                          openwow::render::BuildPortraitIconTextureKey(source_path));
}

static const CreatureTemplateInfo *LookupCreatureTemplateForGuid(const WorldSession &session,
                                                                 const ObjectGuid guid) {
  if (!guid.HasEntry()) {
    return nullptr;
  }

  return session.query_cache().GetCreatureTemplate(guid.GetEntry());
}

static std::uint32_t GetPrimaryDisplayId(const CreatureTemplateInfo *creature_template) {
  if (creature_template == nullptr) {
    return 0;
  }

  for (const auto display_id : creature_template->display_ids) {
    if (display_id != 0) {
      return display_id;
    }
  }

  return 0;
}

static std::optional<std::string> TryResolvePortraitIconTexturePath(
  const WorldObject* const object) {
  const char* const portrait_name =
      object != nullptr ? object->GetPortraitTextureName() : nullptr;

  if (portrait_name == nullptr || *portrait_name == '\0') {
    return std::nullopt;
  }

  return std::string(kItemIconTexturePathPrefix) + portrait_name;
}

static std::uint32_t ResolvePortraitDisplayId(const WorldSession &session,
                                              const WorldObject *object, const ObjectGuid guid,
                                              const std::string &unit_id) {
  if (object != nullptr) {
    return object->GetDisplayId();
  }

  const auto snapshot = UnitFrameDataProvider::Get().GetUnitData(unit_id);
  if (snapshot.has_data) {
    if (snapshot.display_id != 0) {
      return snapshot.display_id;
    }

    if (const auto *creature_template = LookupCreatureTemplateForGuid(session, snapshot.guid);
        creature_template != nullptr) {
      return GetPrimaryDisplayId(creature_template);
    }
  }

  if (const auto *creature_template = LookupCreatureTemplateForGuid(session, guid);
      creature_template != nullptr) {
    return GetPrimaryDisplayId(creature_template);
  }

  return 0;
}

static std::string ResolveTemporaryPortraitTexturePath(const ObjectGuid guid) {
  if (guid.IsVehicle()) {
    auto &vehicle_system = VehicleSystem::Get();
    if (const auto *vehicle_info = vehicle_system.GetCurrentVehicle();
        vehicle_info != nullptr && vehicle_info->guid == guid) {
      if (vehicle_info->skinType == 0) {
        return std::string(kPortraitVehicleOrganicTexture);
      }
      if (vehicle_info->skinType == 1) {
        return std::string(kPortraitVehicleMechanicalTexture);
      }
    }
  }

  return std::string(kPortraitPetTexture);
}

static runtime::MovieRecordingRuntime *GetMovieRecordingRuntime(lua_State *L) {
  auto *context = GetGameUiManager(L);
  return context != nullptr ? &context->movie_recording_runtime() : nullptr;
}

static bool IsMovieRecordingForceEnabled() {
  const auto &cvars = CVarSystem::Instance();
  return cvars.Exists("MovieRecordingForceEnable") &&
         cvars.GetCVarBool("MovieRecordingForceEnable");
}

static bool IsMovieRecordingSupported(lua_State *L) {
  const auto *recording = GetMovieRecordingRuntime(L);
  return recording != nullptr &&
         recording->IsSupported(IsMovieRecordingForceEnabled());
}

static constexpr int kDefaultMovieRecordingViewportWidth = 1024;
static constexpr int kDefaultMovieRecordingViewportHeight = 768;
static constexpr double kMovieRecordingRawFrameBytesPerPixel = 1.5;
static constexpr double kMovieRecordingFrameHeaderBytes = 16.0;
static constexpr std::uint32_t kBytesPerKiB = 1024;
static constexpr std::uint32_t kBytesPerMiB = kBytesPerKiB * kBytesPerKiB;
static constexpr std::uint32_t kBytesPerGiB = kBytesPerMiB * kBytesPerKiB;
static constexpr std::uint32_t kMovieRecordingKiBFormatCutoff = kBytesPerKiB + 1;
static constexpr std::uint32_t kMovieRecordingMiBFormatCutoff = kBytesPerMiB + 1;
static constexpr std::uint32_t kMovieRecordingGiBFormatCutoff = kBytesPerGiB + 1;
static constexpr std::uint64_t kSecondsPerMinute = 60;
static constexpr std::uint64_t kSecondsPerHour = kSecondsPerMinute * kSecondsPerMinute;

static void PushMovieRecordingBool(lua_State *L, const bool value) {
  lua_pushboolean(L, value ? 1 : 0);
}

static float ResolveMovieRecordingAspectScale(lua_State *L) {
  if (const auto *mgr = GetGameUiManager(L); mgr != nullptr) {
    return mgr->screen_height() / mgr->screen_width();
  }

  if (const auto display_mode =
          openwow::ui::display::platform::CurrentDisplayMode();
      display_mode.has_value() && display_mode->width > 0) {
    return static_cast<float>(display_mode->height) /
           static_cast<float>(display_mode->width);
  }

  return static_cast<float>(kDefaultMovieRecordingViewportHeight) /
         static_cast<float>(kDefaultMovieRecordingViewportWidth);
}

static std::uint32_t SaturatingMovieRecordingUInt32(const double value) {

  if (std::isnan(value)) {
    return std::uint32_t{0x80000000};
  }
  if (value <= 0.0) {
    return 0;
  }

  constexpr double kUInt32MaxAsDouble =
      static_cast<double>(std::numeric_limits<std::uint32_t>::max());
  if (value >= kUInt32MaxAsDouble) {
    return std::numeric_limits<std::uint32_t>::max();
  }

  return static_cast<std::uint32_t>(value);
}

static float MovieRecordingUInt32AsFloat(const std::uint32_t value) {

  return static_cast<float>(value >> 16U) * 65536.0F +
         static_cast<float>(value & 0xFFFFU);
}

static std::uint32_t ReadMovieRecordingWidth(lua_State *L) {
  return SaturatingMovieRecordingUInt32(lua_tonumber(L, 1));
}

static std::uint32_t ReadMovieRecordingHeight(lua_State *L) {
  return SaturatingMovieRecordingUInt32(lua_tonumber(L, 1) * ResolveMovieRecordingAspectScale(L));
}

static float ReadMovieRecordingToggleFrameRate() {
  const auto snapshot =
      CVarSystem::Instance().GetCVarSnapshot("MovieRecordingFramerate");
  if (!snapshot.has_value()) {
    return 0.0F;
  }

  const float value = snapshot->current_float_value;
  if (value < 10.0F) {
    switch (snapshot->current_int_value) {
      case 2:
        return 0.5F;
      case 3:
        return 1.0F / 3.0F;
      case 4:
        return 0.25F;
      default:
        break;
    }
  }
  return value;
}

static runtime::MovieRecordingOptions ReadMovieRecordingToggleOptions() {
  const auto &cvars = CVarSystem::Instance();
  return {
      .width = static_cast<std::uint32_t>(
          cvars.GetCVarInt("MovieRecordingWidth")),

      .height = 0,
      .frame_rate = ReadMovieRecordingToggleFrameRate(),
      .capture_sound = cvars.GetCVarBool("MovieRecordingSound"),
      .codec = static_cast<std::uint32_t>(
          cvars.GetCVarInt("MovieRecordingCompression")),
      .quality = static_cast<std::uint32_t>(
          cvars.GetCVarInt("MovieRecordingQuality")),
      .capture_gui = cvars.GetCVarBool("MovieRecordingGUI"),
      .capture_cursor = cvars.GetCVarBool("MovieRecordingCursor"),
      .force_enable = IsMovieRecordingForceEnabled(),
  };
}

static std::uint32_t AlignMovieRecordingDimension(const std::uint32_t value) {
  return value & ~std::uint32_t{3};
}

static std::uint32_t MovieRecordingAudioBytesPerSecond() {
  constexpr std::uint32_t kRetailMovieRecordingAudioSampleRate = 44100;
  constexpr std::uint32_t kRetailMovieRecordingAudioChannels = 2;
  constexpr std::uint32_t kRetailMovieRecordingAudioBytesPerChannel = 4;
  return kRetailMovieRecordingAudioSampleRate * kRetailMovieRecordingAudioChannels *
         kRetailMovieRecordingAudioBytesPerChannel;
}

static std::uint32_t ComputeMovieRecordingDataRateBytesPerSecond(
    const std::uint32_t width, const std::uint32_t height, const double frame_rate,
    const bool capture_sound) {

  const std::uint32_t pixel_count =
      AlignMovieRecordingDimension(width) *
      AlignMovieRecordingDimension(height);
  const float video_rate =
      MovieRecordingUInt32AsFloat(pixel_count) *
      static_cast<float>(frame_rate);
  const double raw_bytes_per_second =
      static_cast<double>(video_rate) *
          kMovieRecordingRawFrameBytesPerPixel +
      kMovieRecordingFrameHeaderBytes;
  std::uint32_t bytes_per_second =
      SaturatingMovieRecordingUInt32(raw_bytes_per_second);
  if (capture_sound) {

    bytes_per_second += MovieRecordingAudioBytesPerSecond();
  }

  return bytes_per_second;
}

static std::filesystem::path PrepareDefaultMovieRecordingDirectory() {
  std::error_code error;
  const std::filesystem::path movies{"Movies"};
  std::filesystem::create_directories(movies, error);
  if (!error && std::filesystem::is_directory(movies, error) && !error) {
    return movies;
  }
  return std::filesystem::path{"."};
}

static bool CanWriteMovieRecordingDirectory(
    const std::filesystem::path &directory) {
  std::array<char, 32> timestamp{};
  const std::time_t now = std::time(nullptr);
  const std::tm *local_time = std::localtime(&now);
  if (local_time == nullptr ||
      std::strftime(timestamp.data(), timestamp.size(), "%m%d%y_%H%M%S",
                    local_time) == 0) {
    return false;
  }

  const std::filesystem::path probe =
      directory / (std::string{"MovieBlizTest"} + timestamp.data());
  std::FILE *file = std::fopen(probe.string().c_str(), "wb");
  if (file == nullptr) {
    return false;
  }

  std::fclose(file);
  std::error_code ignored;
  std::filesystem::remove(probe, ignored);
  return true;
}

static std::filesystem::path ResolveMovieRecordingOutputDirectory() {
  auto &cvars = CVarSystem::Instance();
  std::string configured =
      cvars.Exists("MovieRecordingPath") ? cvars.GetCVar("MovieRecordingPath")
                                        : std::string{};
  if (const auto nul = configured.find('\0'); nul != std::string::npos) {
    configured.resize(nul);
  }

  if (configured.size() > 0xF0u) {
    configured.clear();
  }
  std::replace(configured.begin(), configured.end(), '\\', '/');
  while (!configured.empty() && configured.back() == '/') {
    configured.pop_back();
  }

  if (configured.empty()) {
    return PrepareDefaultMovieRecordingDirectory();
  }

  const std::filesystem::path requested{configured};
  std::error_code error;
  if (!std::filesystem::is_directory(requested, error) || error ||
      !CanWriteMovieRecordingDirectory(requested)) {
    return PrepareDefaultMovieRecordingDirectory();
  }
  return requested;
}

static std::uint64_t ResolveMovieRecordingAvailableBytes() {
  std::error_code error;
  const auto space =
      std::filesystem::space(ResolveMovieRecordingOutputDirectory(), error);
  if (error) {
    return 0;
  }

  return static_cast<std::uint64_t>(space.available);
}

static std::uint64_t TruncateMovieRecordingDurationMicroseconds(
    const double value) {
  constexpr double kUInt64SignBitAsDouble = 9223372036854775808.0;
  constexpr double kUInt64LimitAsDouble = 18446744073709551616.0;
  if (!std::isfinite(value) || value < 0.0 || value >= kUInt64LimitAsDouble) {

    return 0;
  }
  if (value < kUInt64SignBitAsDouble) {
    return static_cast<std::uint64_t>(value);
  }
  return (std::uint64_t{1} << 63U) +
         static_cast<std::uint64_t>(value - kUInt64SignBitAsDouble);
}

static std::uint64_t ComputeMovieRecordingMaxLengthSeconds(
    lua_State *L, const std::uint32_t width, const std::uint32_t height,
    const double frame_rate, const bool capture_sound) {
  if (!IsMovieRecordingSupported(L)) {
    return 0;
  }

  const std::uint32_t bytes_per_second =
      ComputeMovieRecordingDataRateBytesPerSecond(width, height, frame_rate, capture_sound);
  constexpr std::uint64_t kMinimumMovieRecordingFreeBytes = 0x20000000u;
  const std::uint64_t available_bytes = ResolveMovieRecordingAvailableBytes();
  if (available_bytes <= kMinimumMovieRecordingFreeBytes ||
      bytes_per_second == 0) {
    return 0;
  }

  const double duration_microseconds =
      static_cast<double>(available_bytes) /
      (static_cast<double>(bytes_per_second) / 1'000'000.0);
  return TruncateMovieRecordingDurationMicroseconds(duration_microseconds) /
         1'000'000u;
}

static void PushMovieRecordingTime(lua_State *L, std::uint64_t seconds) {
  const std::uint64_t hours = seconds / kSecondsPerHour;
  seconds %= kSecondsPerHour;
  const std::uint64_t minutes = seconds / kSecondsPerMinute;
  seconds %= kSecondsPerMinute;

  std::array<char, 64> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "%llu:%02llu:%02llu",
                static_cast<unsigned long long>(hours),
                static_cast<unsigned long long>(minutes),
                static_cast<unsigned long long>(seconds));
  lua_pushstring(L, buffer.data());
}

static void PushMovieRecordingDataRate(lua_State *L, const std::uint32_t bytes_per_second) {
  std::array<char, 64> buffer{};
  if (bytes_per_second < kMovieRecordingKiBFormatCutoff) {
    std::snprintf(buffer.data(), buffer.size(), "%u B/s", bytes_per_second);
  } else if (bytes_per_second < kMovieRecordingMiBFormatCutoff) {
    const float rate =
        MovieRecordingUInt32AsFloat(bytes_per_second) * (1.0F / 1024.0F);
    std::snprintf(buffer.data(), buffer.size(), "%.03f KB/s",
                  static_cast<double>(rate));
  } else if (bytes_per_second < kMovieRecordingGiBFormatCutoff) {
    const float rate =
        MovieRecordingUInt32AsFloat(bytes_per_second) * (1.0F / 1048576.0F);
    std::snprintf(buffer.data(), buffer.size(), "%.03f MB/s",
                  static_cast<double>(rate));
  } else {
    const float rate =
        MovieRecordingUInt32AsFloat(bytes_per_second) * (1.0F / 1073741824.0F);
    std::snprintf(buffer.data(), buffer.size(), "%.03f GB/s",
                  static_cast<double>(rate));
  }

  lua_pushstring(L, buffer.data());
}

int LuaGameGetTime(lua_State *L) {
  return openwow::ui::LuaGetTickCountSeconds(L);
}

int LuaGetGameTime(lua_State *L) {
  std::int32_t hour = -1;
  std::int32_t minute = -1;
  auto *session = GetWorldSession(L);
  if (session != nullptr) {
    const auto& game_time = session->session().game_time_data();
    hour = game_time.hour;
    minute = game_time.minute;
  }
  lua_pushnumber(L, static_cast<lua_Number>(hour));
  lua_pushnumber(L, static_cast<lua_Number>(minute));
  return 2;
}

static constexpr const char *kGameUiMgrKey = "openwow.world_ui_runtime_context";

static runtime::WorldUiRuntimeContext *GetGameUiManager(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, kGameUiMgrKey);
  auto *mgr = static_cast<runtime::WorldUiRuntimeContext *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  return mgr;
}

static std::optional<lua_Number> UiParentEffectiveScale(lua_State* state) {
  lua_getglobal(state, "UIParent");
  if (lua_istable(state, -1) == 0) {
    lua_pop(state, 1);
    return std::nullopt;
  }

  const lua_Number scale = frame_api::ComputeFrameEffectiveScale(state, -1);
  lua_pop(state, 1);
  return scale;
}

static EventDispatcher *GetGameEvents(lua_State *L) {
  auto *manager = GetGameUiManager(L);
  return manager != nullptr ? &manager->frame_events().dispatcher() : nullptr;
}

int LuaGetFramerate(lua_State *L) {

  lua_pushnumber(
      L, static_cast<lua_Number>(
             static_cast<float>(openwow::core::GameClock::Instance().SmoothedFPS())));
  return 1;
}

int LuaGetDebugStats([[maybe_unused]] lua_State *L) {
  return 0;
}

int LuaGetNetStats(lua_State *L) {

  double latency_ms = 0.0;
  if (const auto *session = GetWorldSession(L)) {
    latency_ms =
        static_cast<double>(session->latency_tracker().GetLatency());
  }
  lua_pushnumber(L, 0.0);
  lua_pushnumber(L, 0.0);
  lua_pushnumber(L, latency_ms);
  return 3;
}

static const openwow::game::CGPlayer_C *GetLocalPlayer(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return nullptr;
  return session->objects().GetLocalPlayerTyped();
}

int LuaGetCombatRating(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetCombatRating(ratingIndex)");
  }
  const auto rating_index =
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1));
  if (rating_index < 1u || rating_index > 25u) {
    return luaL_error(L, "ratingIndex is in the range 1 .. %d", 25);
  }
  const auto idx = rating_index - 1u;
  const auto *player = GetLocalPlayer(L);
  if (!player) {
    lua_pushnumber(L, 0);
    return 1;
  }
  lua_pushnumber(
      L, static_cast<lua_Number>(
             player->GetCombatRating(static_cast<std::uint8_t>(idx))));
  return 1;
}

int LuaGetCombatRatingBonus(lua_State *L) {
  if (!lua_isnumber(L, 1)) {
    return luaL_error(L, "Usage: GetCombatRatingBonus(ratingIndex)");
  }
  const auto rating_index =
      openwow::ui::SaturateLuaNumberToU32(lua_tonumber(L, 1));
  if (rating_index < 1u || rating_index > 25u) {
    return luaL_error(L, "ratingIndex is in the range 1 .. %d", 25);
  }
  const auto idx = rating_index - 1u;
  const auto *player = GetLocalPlayer(L);
  if (!player) {
    lua_pushnumber(L, 0.0);
    return 1;
  }

  if (const auto *const dbc = GetDbcLoader(L); dbc != nullptr) {
    lua_pushnumber(
        L, static_cast<lua_Number>(openwow::game::ComputeCombatRatingBonus(
               *player, *dbc, static_cast<std::uint8_t>(idx))));
    return 1;
  }

  lua_pushnumber(L, 0.0);
  return 1;
}

int LuaGetDodgeChance(lua_State *L) {
  const auto *player = GetLocalPlayer(L);
  if (!player) {
    lua_pushnumber(L, 0);
    return 1;
  }
  lua_pushnumber(L, static_cast<lua_Number>(player->GetFloat(PLAYER_DODGE_PERCENTAGE)));
  return 1;
}

int LuaGetParryChance(lua_State *L) {
  const auto *player = GetLocalPlayer(L);
  if (!player) {
    lua_pushnumber(L, 0);
    return 1;
  }
  lua_pushnumber(L, static_cast<lua_Number>(player->GetFloat(PLAYER_PARRY_PERCENTAGE)));
  return 1;
}

int LuaGetBlockChance(lua_State *L) {
  const auto *player = GetLocalPlayer(L);
  if (!player) {
    lua_pushnumber(L, 0);
    return 1;
  }
  lua_pushnumber(L, static_cast<lua_Number>(player->GetFloat(PLAYER_BLOCK_PERCENTAGE)));
  return 1;
}

int LuaGetShieldBlock(lua_State *L) {
  const auto *player = GetLocalPlayer(L);
  if (!player) {
    lua_pushnumber(L, 0);
    return 1;
  }

  lua_pushnumber(L, static_cast<lua_Number>(player->GetUInt32(PLAYER_SHIELD_BLOCK)));
  return 1;
}

int LuaGetRangedCritChance(lua_State *L) {
  const auto *player = GetLocalPlayer(L);
  if (!player) {
    lua_pushnumber(L, 0);
    return 1;
  }
  lua_pushnumber(L, static_cast<lua_Number>(player->GetFloat(PLAYER_RANGED_CRIT_PERCENTAGE)));
  return 1;
}

int LuaGetSpellCritChance(lua_State *L) {
  const auto school =
      static_cast<std::uint32_t>(openwow::ui::TruncateLuaNumberToI32(
          lua_tonumber(L, 1))) -
      1u;
  if (school >= 7u)
    luaL_error(L, "Usage: GetSpellCritChance(school)");
  const auto *player = GetLocalPlayer(L);
  if (!player) {
    lua_pushnumber(L, 0);
    return 1;
  }
  const auto field = static_cast<std::uint16_t>(
      PLAYER_SPELL_CRIT_PERCENTAGE1 + school);
  lua_pushnumber(L, static_cast<lua_Number>(player->GetFloat(field)));
  return 1;
}

int LuaGetSpellBonusDamage(lua_State *L) {
  const auto school =
      static_cast<std::uint32_t>(openwow::ui::TruncateLuaNumberToI32(
          lua_tonumber(L, 1))) -
      1u;
  if (school >= 7u)
    luaL_error(L, "Usage: GetSpellBonusDamage(school)");
  const auto *player = GetLocalPlayer(L);
  if (!player) {
    lua_pushnumber(L, 0);
    return 1;
  }
  lua_pushnumber(
      L, static_cast<lua_Number>(
             player->GetSpellBonusDamage(static_cast<std::uint8_t>(school))));
  return 1;
}

int LuaGetSpellBonusHealing(lua_State *L) {
  const auto *player = GetLocalPlayer(L);
  if (!player) {
    lua_pushnumber(L, 0);
    return 1;
  }
  lua_pushnumber(L, static_cast<lua_Number>(static_cast<std::int32_t>(
                        player->GetUInt32(PLAYER_FIELD_MOD_HEALING_DONE_POS))));
  return 1;
}

int LuaGetExpertise(lua_State *L) {

  const auto *player = GetLocalPlayer(L);
  if (!player) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 2;
  }
  lua_pushnumber(L, static_cast<lua_Number>(player->GetUInt32(PLAYER_EXPERTISE)));
  lua_pushnumber(L, static_cast<lua_Number>(player->GetUInt32(PLAYER_OFFHAND_EXPERTISE)));
  return 2;
}

int LuaGetExpertisePercent(lua_State *L) {
  constexpr float kExpertiseToPercent = 0.25f;
  const auto *player = GetLocalPlayer(L);
  if (!player) {
    lua_pushnumber(L, 0);
    lua_pushnumber(L, 0);
    return 2;
  }
  lua_pushnumber(L, static_cast<lua_Number>(
                        static_cast<float>(static_cast<std::int32_t>(
                            player->GetUInt32(PLAYER_EXPERTISE))) *
                        kExpertiseToPercent));
  lua_pushnumber(L, static_cast<lua_Number>(
                        static_cast<float>(static_cast<std::int32_t>(
                            player->GetUInt32(PLAYER_OFFHAND_EXPERTISE))) *
                        kExpertiseToPercent));
  return 2;
}

int LuaGetManaRegen(lua_State *L) {
  const auto *player = GetLocalPlayer(L);
  if (!player ||
      player->State().GetPowerType() != static_cast<std::uint8_t>(openwow::game::PowerType::kMana)) {
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    return 2;
  }

  constexpr float kLuaRegenBias = 0.001f;
  lua_pushnumber(L, static_cast<lua_Number>(
                        player->GetPowerRegenRate(static_cast<std::uint8_t>(
                            openwow::game::PowerType::kMana)) +
                        kLuaRegenBias));
  lua_pushnumber(L, static_cast<lua_Number>(
                        player->GetPowerRegenRateInterrupted(
                            static_cast<std::uint8_t>(
                                openwow::game::PowerType::kMana)) +
                        kLuaRegenBias));
  return 2;
}

int LuaTargetUnit(lua_State *L) {
  auto *session = GetWorldSession(L);
  auto *targeting = GetTargetingSystem(L);
  if (!session || !targeting)
    return 0;

  const std::string unit_id = SafeLuaString(L, 1);
  if (unit_id.empty())
    return 0;

  const bool exact_match = ReadClientBoolArgOrDefault(L, 2, false);
  auto guid =
      ResolveGameUiLookup(session, unit_id, openwow::game::kTypeMaskUnit, 0, exact_match, true);
  if (guid.IsEmpty())
    return 0;

  if (!GameUI_CanPerformTaintForbiddenAction()) {
    return 0;
  }

  targeting->SetTarget(guid.GetRawValue());
  return 0;
}

int LuaClearTarget(lua_State *L) {
  auto *targeting = GetTargetingSystem(L);
  if (!targeting) {
    lua_pushnil(L);
    return 1;
  }

  bool had_target = targeting->HasTarget();
  if (GameUI_CanPerformTaintForbiddenAction()) {
    targeting->ClearTarget();
  }
  if (had_target)
    lua_pushnumber(L, 1.0);
  else
    lua_pushnil(L);
  return 1;
}

int LuaAssistUnit(lua_State *L) {
  auto *session = GetWorldSession(L);
  auto *targeting = GetTargetingSystem(L);
  if (!session || !targeting)
    return 0;

  const std::string unit_id = SafeLuaString(L, 1);
  const bool exact_match = ReadClientBoolArgOrDefault(L, 2, false);
  auto guid =
      ResolveGameUiLookup(session, unit_id, openwow::game::kTypeMaskUnit, 0, exact_match, false);
  const auto *assist_unit = guid.IsEmpty() ? nullptr : session->objects().GetUnit(guid);
  if (assist_unit == nullptr) {
    DisplayAssistUnitLookupFailure(unit_id);
    return 0;
  }

  if (!GameUI_CanPerformTaintForbiddenAction()) {
    return 0;
  }

  targeting->AssistUnit(guid.GetRawValue());
  return 0;
}

static void SetScriptFocusTarget(lua_State *L,
                                 ::openwow::game::WorldSession &session,
                                 const ::openwow::game::ObjectGuid guid) {
  if (session.objects().GetFocusTargetGuid() == guid) {
    return;
  }
  if (!GameUI_CanPerformTaintForbiddenAction()) {
    return;
  }
  session.objects().SetFocusTarget(guid);
  if (auto *dispatcher = GetGameEvents(L); dispatcher != nullptr) {
    dispatcher->FireEvent("PLAYER_FOCUS_CHANGED");
  }
}

int LuaFocusUnit(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;
  const std::string unit_id = UnitIdArg(L, 1);

  const auto guid = unit_id.empty() ? session->objects().GetTargetGuid()
                                    : ResolveUnitId(session, unit_id);
  SetScriptFocusTarget(L, *session, guid);
  return 0;
}

int LuaClearFocus(lua_State *L) {
  if (auto *session = GetWorldSession(L); session != nullptr) {
    SetScriptFocusTarget(L, *session, ::openwow::game::ObjectGuid());
  }
  return 0;
}

int LuaGameGetCursorPosition(lua_State *L) {
  const auto [x_pixels, y_top_left_pixels] = openwow::input::InputManager::Get().GetMousePosition();
  float viewport_height = 0.0f;
  if (const auto *mgr = GetGameUiManager(L); mgr != nullptr) {
    viewport_height = mgr->screen_height();
  }
  const auto [script_x, script_y] =
      openwow::ui::ProjectTopLeftPixelCursorToUiScript(
          static_cast<float>(x_pixels), static_cast<float>(y_top_left_pixels),
          viewport_height);

  lua_pushnumber(L, static_cast<lua_Number>(script_x));
  lua_pushnumber(L, static_cast<lua_Number>(script_y));
  return 2;
}

int LuaSetCursor(lua_State *L) {

  if (lua_type(L, 1) == LUA_TNIL) {
    lua_getfield(L, LUA_REGISTRYINDEX, "openwow.cursor_manager");
    auto *cm = static_cast<::openwow::game::CursorSurface *>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    if (cm)
      cm->ResetCursor();
    return 0;
  }
  const char *cursor_type = lua_tostring(L, 1);
  if (!cursor_type)
    return luaL_error(L, "Usage: SetCursor(\"cursor\" or nil)");
  lua_getfield(L, LUA_REGISTRYINDEX, "openwow.cursor_manager");
  auto *cm = static_cast<::openwow::game::CursorSurface *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  if (cm && cm->SetCursorFromLua(cursor_type)) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaResetCursor(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "openwow.cursor_manager");
  auto *cm = static_cast<::openwow::game::CursorSurface *>(lua_touserdata(L, -1));
  lua_pop(L, 1);
  if (cm) {
    cm->ResetCursor();
  }
  return 0;
}

int LuaAutoEquipCursorItem(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session == nullptr)
    return 0;

  (void)::openwow::ui::game::detail::AutoEquipHeldCursorItem(*session);
  return 0;
}

int LuaDeleteCursorItem(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session == nullptr || session->objects().GetActivePlayer() == nullptr) {
    return 0;
  }

  auto* held_cursor = ::openwow::ui::game::lua::FindHeldCursor(*L);
  const auto* held_item =
      held_cursor != nullptr ? held_cursor->live_item() : nullptr;
  if (held_item == nullptr || held_item->item.IsEmpty()) {
    return 0;
  }

  std::uint8_t source_bag = 0;
  std::uint8_t source_slot = 0;
  if (!ResolveHeldCursorServerCoords(*held_cursor, &source_bag, &source_slot)) {
    return 0;
  }

  if (held_item->item.CantBeDestroyed()) {
    DisplaySystemMessage(360);
    return 0;
  }

  const auto held_item_guid = held_item->item.guid;
  session->interaction().SendDestroyItem(source_bag, source_slot,
                                         held_item->auxiliary_value);
  held_cursor->Clear();
  if (held_item_guid != 0) {
    GameUI_OnMouseoverUnitEnter(held_item_guid);
  }
  if (RefreshAllActionSlotValidation(*session)) {
    ScriptEventDispatch::Get().FireActionbarUpdateUsable();
  }
  return 0;
}

int LuaLogout(lua_State *L) {
  (void)L;
  openwow::net::ClientServices::Instance().RequestLogout();
  return 0;
}

int LuaForceLogout(lua_State *L) {
  (void)L;
  openwow::net::ClientServices::Instance().ForceLogout();
  return 0;
}

int LuaCancelLogout(lua_State *L) {
  (void)L;

  if (!GameUI_CanPerformHardwareEventAction()) {
    return 0;
  }

  openwow::net::ClientServices::Instance().RequestLogoutCancel();
  return 0;
}

int LuaOpeningCinematic(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session) {
    session->interaction().SendOpeningCinematic();
  }
  return 0;
}

int LuaGetBillingTimeRested(lua_State *L) {
  lua_pushnumber(
      L, static_cast<lua_Number>(openwow::net::ClientServices::Instance().GetBillingTimeRested()));
  return 1;
}

int LuaGetRestState(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
  }

  const auto *player = session->objects().GetActivePlayer();
  if (!player) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
  }

  const std::uint8_t rest_byte = player->GetRestState();
  const auto *dbc = GetDbcLoader(L);
  const auto *entry =
      dbc != nullptr ? dbc->exhaustion().LookupEntry(rest_byte) : nullptr;
  if (entry == nullptr) {
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    return 3;
  }

  lua_pushnumber(L, static_cast<lua_Number>(entry->id));
  lua_pushlstring(L, entry->name.data(), entry->name.size());
  lua_pushnumber(L, static_cast<lua_Number>(entry->factor));
  return 3;
}

int LuaGetXPExhaustion(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnil(L);
    return 1;
  }
  const auto *player = session->objects().GetActivePlayer();
  if (!player) {
    lua_pushnil(L);
    return 1;
  }

  constexpr auto kRestedState = static_cast<std::uint8_t>(RestState::Rested);
  if (player->GetRestState() != kRestedState) {
    lua_pushnil(L);
    return 1;
  }

  const auto *dbc = GetDbcLoader(L);
  const auto *entry =
      dbc != nullptr ? dbc->exhaustion().LookupEntry(kRestedState) : nullptr;
  if (entry == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const std::uint32_t rest_xp = player->GetUInt32(PLAYER_REST_STATE_EXPERIENCE);
  const float exhaustion =
      static_cast<float>(rest_xp) * entry->factor;
  lua_pushnumber(L, static_cast<lua_Number>(exhaustion));
  return 1;
}

int LuaIsResting(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnil(L);
    return 1;
  }
  const auto *player = session->objects().GetActivePlayer();
  if (!player) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushwowbool(L, player->IsResting());
  return 1;
}

int LuaIsMounted(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnil(L);
    return 1;
  }
  const auto *player = session->objects().GetActivePlayer();
  if (!player) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushwowbool(L, player->Mount().IsMountedStateActive(*player));
  return 1;
}

int LuaDismount(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    return 0;
  }

  auto *player = session->objects().GetActivePlayer();
  if (player == nullptr || player->State().IsDead() ||
      player->State().IsTaxiFlight()) {
    return 0;
  }

  session->interaction().SendCancelMountAura();
  player->Mount().ApplyDisplayChange(*player, *session, 0);
  return 0;
}

int LuaIsSwimming(lua_State *L) {
  const auto *const session = GetWorldSession(L);
  const auto *const player =
      session != nullptr ? session->objects().GetActivePlayer() : nullptr;
  if (player == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushwowbool(L, player->Movement().IsInWater());
  return 1;
}

int LuaIsFalling(lua_State *L) {
  const auto *const session = GetWorldSession(L);
  const auto *const player =
      session != nullptr ? session->objects().GetActivePlayer() : nullptr;
  if (player == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const auto &movement = player->GetMovementInfo();
  lua_pushwowbool(L, movement.HasFlag(kMoveFlagFalling) &&
                         !movement.HasFlag(kMoveFlagRoot));
  return 1;
}

int LuaIsFlying(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnil(L);
    return 1;
  }

  const auto *player = session->objects().GetActivePlayer();
  if (player == nullptr) {
    lua_pushnil(L);
    return 1;
  }

  const CGUnit_C *movement_owner = player;
  const auto *passenger = player->Vehicle().GetVehiclePassengerComponent();
  if (passenger != nullptr &&
      passenger->GetTransitionState() ==
          VehiclePassengerTransitionType::kAttached) {
    movement_owner = passenger->GetVehicleUnit();
    if (movement_owner == nullptr) {
      lua_pushnil(L);
      return 1;
    }
  }
  lua_pushwowbool(L,
                  movement_owner->GetMovementInfo().HasFlag(kMoveFlagFlying));
  return 1;
}

int LuaIsStealthed(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session) {
    lua_pushnil(L);
    return 1;
  }
  const auto *player = session->objects().GetActivePlayer();
  if (!player) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushwowbool(L, player->State().IsStealth());
  return 1;
}

int LuaGetWeaponEnchantInfo(lua_State *L) {
  PushWeaponEnchantLuaTriplet(L, ResolveWeaponEnchantLuaTriplet(L, InventorySlots::kMainHand));
  PushWeaponEnchantLuaTriplet(L, ResolveWeaponEnchantLuaTriplet(L, InventorySlots::kOffHand));
  return 6;
}

int LuaGetBuildInfo(lua_State *L) {
  lua_pushstring(L, openwow::ui::kRetailClientVersion);
  lua_pushstring(L, openwow::ui::kRetailClientBuildNumber);
  lua_pushstring(L, openwow::ui::kRetailClientBuildDate);
  lua_pushnumber(L, openwow::ui::kRetailInterfaceVersion);
  return 4;
}

int LuaIsWindowsClient(lua_State *L) {
  return openwow::ui::PushRetailLuaClientPlatformQuery(
      L, openwow::ui::LuaClientPlatform::kWindows);
}

int LuaIsMacClient(lua_State *L) {
  return openwow::ui::PushRetailLuaClientPlatformQuery(
      L, openwow::ui::LuaClientPlatform::kMac);
}

int LuaIsLinuxClient(lua_State *L) {
  return openwow::ui::PushRetailLuaClientPlatformQuery(
      L, openwow::ui::LuaClientPlatform::kLinux);
}

int LuaGetExpansionLevel(lua_State *L) {
  return openwow::ui::PushLuaLiveExpansionLevel(L);
}

int LuaApi_IsConsoleActive(lua_State *L) {
  if (openwow::core::ida::IsConsoleVisible()) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaApi_IsDebugBuild(lua_State *L) {
  lua_pushboolean(L, 0);
  return 1;
}

int LuaApi_IsStreamingMode(lua_State *) {

  return openwow::ui::ReturnExistingLuaTopWhen(
      openwow::data::IsStreamingInitialized());
}

int LuaApi_RegisterStaticConstants(lua_State *L) {
  if (lua_type(L, 1) != LUA_TTABLE) {
    return luaL_error(L, "Usage: RegisterStaticConstants(table)");
  }

  for (const auto &constant : kStaticConstantRegistrations) {
    lua_pushstring(L, constant.name);
    lua_pushnumber(L, static_cast<lua_Number>(constant.value));
    lua_settable(L, 1);
  }

  return 0;
}

int LuaApi_SetConsoleKey(lua_State *L) {
  namespace ida = openwow::core::ida;
  if (lua_isstring(L, 1)) {
    ida::SetConsoleToggleKeyCode(ida::ResolveConsoleToggleKeyCode(lua_tostring(L, 1)));
  } else {
    ida::SetConsoleToggleKeyCode(ida::kDefaultConsoleToggleKeyCode);
  }
  return 0;
}

int LuaApi_SetEuropeanNumbers(lua_State *L) {
  const bool enabled = ScriptReadBoolArgOrDefault(L, 1, true);
  openwow::game::LCD_SetEuropeanNumbers(enabled ? 1 : 0);
  return 0;
}

int LuaGetRealmName(lua_State *L) {
  const auto server_name = openwow::ui::BuildScriptServerNameResult();
  lua_pushstring(L, server_name.realm_name.c_str());
  return 1;
}

int LuaGetScriptCPUUsage(lua_State *L) {
  lua_pushnumber(L, GetTotalLuaCpuUsageMilliseconds(L));
  return 1;
}

int LuaResetCPUUsage(lua_State *L) {
  ResetLuaCpuUsage(L);
  return 0;
}

int LuaIsShiftKeyDown(lua_State *L) {
  PushModifierQueryResult(L, KMOD_SHIFT);
  return 1;
}

int LuaIsControlKeyDown(lua_State *L) {
  PushModifierQueryResult(L, KMOD_CTRL);
  return 1;
}

int LuaIsAltKeyDown(lua_State *L) {
  PushModifierQueryResult(L, KMOD_ALT);
  return 1;
}

int LuaIsModifierKeyDown(lua_State *L) {
  PushModifierQueryResult(L, static_cast<SDL_Keymod>(KMOD_SHIFT | KMOD_CTRL | KMOD_ALT));
  return 1;
}

int LuaIsLeftShiftKeyDown(lua_State *L) {
  PushModifierQueryResult(L, KMOD_LSHIFT);
  return 1;
}

int LuaIsRightShiftKeyDown(lua_State *L) {
  PushModifierQueryResult(L, KMOD_RSHIFT);
  return 1;
}

int LuaIsLeftControlKeyDown(lua_State *L) {
  PushModifierQueryResult(L, KMOD_LCTRL);
  return 1;
}

int LuaIsRightControlKeyDown(lua_State *L) {
  PushModifierQueryResult(L, KMOD_RCTRL);
  return 1;
}

int LuaIsLeftAltKeyDown(lua_State *L) {
  PushModifierQueryResult(L, KMOD_LALT);
  return 1;
}

int LuaIsRightAltKeyDown(lua_State *L) {
  PushModifierQueryResult(L, KMOD_RALT);
  return 1;
}

int LuaIsModifiedClick(lua_State *L) {
  auto *mgr = GetBindingProfiles(L);
  const char *action = lua_tostring(L, 1);
  const std::string mouse_button =
      lua_adapter::CurrentMouseButtonOverride(L).value_or("");

  lua_pushwowbool(
      L, mgr != nullptr &&
             openwow::game::actions::bindings::adapters::retail::
                 IsModifiedClickActive(*mgr, action, ResolveModifierState(L),
                                       mouse_button));
  return 1;
}

int LuaSetModifiedClick(lua_State *L) {
  if (lua_isstring(L, 1) == 0) {
    return luaL_error(L, "Usage: SetModifiedClick(\"action\", \"binding\")");
  }

  const char *action = lua_tostring(L, 1);
  auto *mgr = GetBindingProfiles(L);
  const char *key = lua_tostring(L, 2);

  if (mgr == nullptr ||
      !openwow::game::actions::bindings::adapters::retail::
          SetModifiedClickBinding(*mgr, action, key ? key : "")) {
    return luaL_error(L, "SetModifiedClick(): Unknown action (%s) or binding (%s)",
                      action ? action : "(null)", key ? key : "(null)");
  }
  return 0;
}

int LuaGetModifiedClick(lua_State *L) {
  if (lua_isstring(L, 1) == 0) {
    return luaL_error(L, "Usage: GetModifiedClick(\"action\")");
  }

  const char *action = lua_tostring(L, 1);
  auto *mgr = GetBindingProfiles(L);
  const auto binding =
      mgr != nullptr && action != nullptr
          ? openwow::game::actions::bindings::adapters::retail::
                GetModifiedClickBinding(*mgr, action)
          : std::nullopt;
  if (binding) {
    lua_pushstring(L, binding->c_str());
    return 1;
  }
  lua_pushnil(L);
  return 1;
}

static int ExecuteRunScriptChunk(lua_State* L, const char* code,
                                 const std::size_t code_length,
                                 const char* error_handler_registry_key) {
  if (openwow::ui::LoadClientLuaChunk(L, std::string_view(code, code_length), code) != 0) {
    CallLuaErrorHandlerIfPresent(L, -1, error_handler_registry_key);
    lua_pop(L, 1);
    return 0;
  }

  int errfunc = 0;
  lua_getfield(L, LUA_REGISTRYINDEX, error_handler_registry_key);
  if (lua_isfunction(L, -1) != 0) {
    lua_insert(L, -2);
    errfunc = lua_gettop(L) - 1;
  } else {
    lua_pop(L, 1);
  }

  const int status = lua_pcall(L, 0, 0, errfunc);
  if (errfunc != 0) {
    lua_remove(L, errfunc);
  }
  if (status != 0) {
    lua_pop(L, 1);
  }
  return 0;
}

int LuaRunScript(lua_State *L) {
  if (lua_isstring(L, 1) == 0) {
    return 0;
  }

  const char *code = lua_tostring(L, 1);
  if (code == nullptr || code[0] == '\0') {
    return 0;
  }

  const std::size_t code_length = std::strlen(code);

  const auto profile = GetSecureLuaBindingProfile(L);
  const char* const error_handler_registry_key =
      profile == SecureLuaBindingProfile::Glue
          ? openwow::ui::kGlueLuaErrorHandlerRegistryKey
          : openwow::ui::kGameLuaErrorHandlerRegistryKey;
  if (profile == SecureLuaBindingProfile::Glue) {
    SecureExecution::SecureScope scope(L);
    return ExecuteRunScriptChunk(L, code, code_length,
                                 error_handler_registry_key);
  }
  SecureExecution::InsecureScope scope(L, "");
  return ExecuteRunScriptChunk(L, code, code_length,
                               error_handler_registry_key);
}

int LuaIsLoggedIn(lua_State *L) {
  const auto *session = GetWorldSession(L);

  lua_pushwowbool(L, session != nullptr && session->IsInWorld());
  return 1;
}

int LuaGameMovieFinished(lua_State *L) {
  if (auto* runtime = runtime::WorldUiRuntimeContext::FromLua(L);
      runtime != nullptr) {
    runtime->movie_runtime().CompleteServerMovie();
  }
  return 0;
}

int LuaStopCinematic(lua_State *L) {
  if (auto *session = GetWorldSession(L)) {
    session->StopCinematicFromScript();
  }
  return 0;
}

int LuaScreenshot([[maybe_unused]] lua_State *L) {
  (void)openwow::core::ScreenshotSystem::Instance().CaptureScreenshot(
      openwow::core::ScreenshotRequestDomain::GameUi);
  return 0;
}

static std::string ResolveMovieRecordingLocalizedText(
    lua_State *L, const char *localized_global) {
  lua_getglobal(L, localized_global);
  const char *localized = lua_tostring(L, -1);
  const std::string message =
      localized != nullptr && *localized != '\0' ? localized : localized_global;
  lua_pop(L, 1);
  return message;
}

static void DisplayMovieRecordingWarning(lua_State *L,
                                         const char *localized_global) {
  const std::string message =
      ResolveMovieRecordingLocalizedText(L, localized_global);
  openwow::ui::UIErrorManager::Get().AddErrorMessage(message);
  ScriptEventDispatch::Get().FireUiErrorMessage(message);
}

int LuaMovieRecordingToggle(lua_State *L) {
  auto *recording = GetMovieRecordingRuntime(L);
  const auto result = recording != nullptr
                          ? recording->Toggle(ReadMovieRecordingToggleOptions())
                          : runtime::MovieRecordingToggleResult::kUnsupported;
  switch (result) {
    case runtime::MovieRecordingToggleResult::kStartedOrStopped:
      break;
    case runtime::MovieRecordingToggleResult::kDiskFull:
      DisplayMovieRecordingWarning(L, "MOVIE_RECORDING_WARNING_DISK_FULL");
      break;
    case runtime::MovieRecordingToggleResult::kCompressionActive:
      DisplayMovieRecordingWarning(L, "MOVIE_RECORDING_WARNING_COMPRESSING");
      break;
    case runtime::MovieRecordingToggleResult::kUnsupported:
      DisplayMovieRecordingWarning(L, "MOVIE_RECORDING_WARNING_REQUIREMENTS");
      break;
  }
  return 0;
}

int LuaMovieRecordingCancel(lua_State *L) {
  if (auto *recording = GetMovieRecordingRuntime(L);
      recording != nullptr &&
      recording->IsSupported(IsMovieRecordingForceEnabled())) {
    recording->Cancel();
  }
  return 0;
}

int LuaMovieRecordingIsRecording(lua_State *L) {
  const auto *recording = GetMovieRecordingRuntime(L);
  PushMovieRecordingBool(
      L, recording != nullptr && recording->IsRecording());
  return 1;
}

int LuaMovieRecordingIsCompressing(lua_State *L) {
  const auto *recording = GetMovieRecordingRuntime(L);
  PushMovieRecordingBool(
      L, recording != nullptr && recording->IsCompressing());
  return 1;
}

int LuaMovieRecordingGetProgress(lua_State *L) {
  const auto *recording = GetMovieRecordingRuntime(L);
  const auto progress = recording != nullptr
                            ? recording->CompressionProgress()
                            : runtime::MovieCompressionProgress{};
  PushMovieRecordingBool(L, progress.complete);
  lua_pushnumber(L, static_cast<lua_Number>(progress.fraction));
  return 2;
}

int LuaMovieRecordingGetViewportWidth(lua_State *L) {
  if (const auto *mgr = GetGameUiManager(L); mgr != nullptr) {
    lua_pushnumber(
        L, static_cast<lua_Number>(
               static_cast<std::int32_t>(mgr->screen_width())));
    return 1;
  }

  if (const auto display_mode =
          openwow::ui::display::platform::CurrentDisplayMode();
      display_mode) {
    lua_pushnumber(L, static_cast<lua_Number>(display_mode->width));
    return 1;
  }

  lua_pushnumber(L, kDefaultMovieRecordingViewportWidth);
  return 1;
}

int LuaMovieRecordingGetAspectRatio(lua_State *L) {
  lua_pushnumber(L, static_cast<lua_Number>(ResolveMovieRecordingAspectScale(L)));
  return 1;
}

int LuaMovieRecordingIsSupported(lua_State *L) {
  PushMovieRecordingBool(L, IsMovieRecordingSupported(L));
  return 1;
}

int LuaMovieRecordingIsCodecSupported(lua_State *L) {
  const auto *recording = GetMovieRecordingRuntime(L);
  const auto codec = SaturatingMovieRecordingUInt32(lua_tonumber(L, 1));
  PushMovieRecordingBool(
      L, recording != nullptr && recording->IsCodecSupported(codec));
  return 1;
}

int LuaMovieRecordingIsCursorRecordingSupported(lua_State *L) {
  const auto *recording = GetMovieRecordingRuntime(L);
  PushMovieRecordingBool(
      L, recording != nullptr && recording->IsCursorRecordingSupported());
  return 1;
}

int LuaMovieRecordingMaxLength(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
    return luaL_error(L, "Usage: MovieRecording_MaxLength(width, framerate, capturesound)");
  }

  PushMovieRecordingTime(
      L, ComputeMovieRecordingMaxLengthSeconds(L, ReadMovieRecordingWidth(L),
                                               ReadMovieRecordingHeight(L),
                                               lua_tonumber(L, 2),
                                               openwow::ui::ScriptReadBoolArgOrDefault(
                                                   L, 3, false)));
  return 1;
}

int LuaMovieRecordingDataRate(lua_State *L) {
  if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
    return luaL_error(L, "Usage: MovieRecording_DataRate(width, framerate, capturesound)");
  }

  PushMovieRecordingDataRate(
      L, ComputeMovieRecordingDataRateBytesPerSecond(ReadMovieRecordingWidth(L),
                                                     ReadMovieRecordingHeight(L),
                                                     lua_tonumber(L, 2),
                                                     openwow::ui::ScriptReadBoolArgOrDefault(
                                                         L, 3, false)));
  return 1;
}

int LuaMovieRecordingGetTime(lua_State *L) {
  const auto *recording = GetMovieRecordingRuntime(L);
  const auto microseconds = recording != nullptr
                                ? recording->RecordingTimeMicroseconds()
                                : 0u;
  PushMovieRecordingTime(L, microseconds / 1'000'000u);
  return 1;
}

int LuaMovieRecordingGetMovieFullPath(lua_State *L) {
  const auto *recording = GetMovieRecordingRuntime(L);
  const std::string path =
      recording != nullptr ? recording->MovieFullPath() : std::string{};
  lua_pushstring(L, path.c_str());
  return 1;
}

int LuaMovieRecordingSearchUncompressedMovie(lua_State *L) {
  if (auto *recording = GetMovieRecordingRuntime(L); recording != nullptr) {
    recording->SearchUncompressedMovies(lua_toboolean(L, 1) != 0);
  }
  return 0;
}

int LuaMovieRecordingQueueMovieToCompress(lua_State *L) {
  const char *path = lua_tostring(L, 1);
  if (path == nullptr) {
    return 0;
  }

  constexpr std::size_t kQueuedMoviePathMaximumLength = 0x103;
  std::string queued_path{path};
  queued_path.resize(
      std::min(queued_path.size(), kQueuedMoviePathMaximumLength));
  if (auto *recording = GetMovieRecordingRuntime(L); recording != nullptr) {
    recording->QueueMovieToCompress(std::move(queued_path));
  }
  return 0;
}

int LuaMovieRecordingDeleteMovie(lua_State *L) {
  const char *path = lua_tostring(L, 1);
  if (auto *recording = GetMovieRecordingRuntime(L); recording != nullptr) {
    recording->DeleteMovie(
        path != nullptr ? std::optional<std::string>{path} : std::nullopt);
  }
  return 0;
}

int LuaMovieRecordingToggleGUI(lua_State *L) {
  auto &cvars = CVarSystem::Instance();
  if (!cvars.Exists("MovieRecordingGUI")) {
    return 0;
  }

  const bool enabled = cvars.GetCVarInt("MovieRecordingGUI") == 0;
  (void)cvars.SetCVar("MovieRecordingGUI", enabled ? "1" : "0", true);

  if (const auto *session = GetWorldSession(L); session != nullptr) {
    const std::string message = ResolveMovieRecordingLocalizedText(
        L, enabled ? "MOVIE_RECORDING_GUI_ON" : "MOVIE_RECORDING_GUI_OFF");
    openwow::game::ChatFrame_DisplayMessage(
        session->objects(), message.c_str(),
        openwow::game::ChatDisplayType::kSystem, nullptr, 0, nullptr, nullptr,
        nullptr, 0, 0, 0, 0, 0, nullptr);
  }
  return 0;
}

int LuaGetMapInfo(lua_State *L) {
  auto *wm = WorldMapStateOrNull(L);
  if (wm == nullptr) {
    lua_pushnil(L);
    PushLuaUnsignedInt(L, 0);
    PushLuaUnsignedInt(L, 0);
    return 3;
  }
  const auto info = wm->GetLegacyMapInfo();

  if (!info.has_name) {
    lua_pushnil(L);
  } else {
    lua_pushstring(L, info.internal_name.c_str());
  }
  PushLuaUnsignedInt(L, info.raw_metric);
  PushLuaUnsignedInt(L, info.padded_metric);
  return 3;
}

int LuaSetPortraitTexture(lua_State *L) {
  const LuaCallFrame call{L};
  const int texture_index = ValidateTextureWidgetArgument(L);
  if (lua_isstring(L, 2) == 0) {
    return luaL_error(L, "Usage: SetPortraitTexture(texture, \"unit\")");
  }

  const std::string unit_id = SafeLuaString(L, 2);
  auto *session = GetWorldSession(L);
  if (session == nullptr) {
    ClearPortraitState(L, texture_index);
    return call.boolean(false);
  }

  const ObjectGuid guid = ResolveUnitId(session, unit_id);
  const WorldObject *object = ResolveUnit(session, unit_id);

  if (object != nullptr && object->IsUnit()) {
    BindPortraitUnitToken(L, texture_index, unit_id);
    return call.boolean(true);
  }

  if (const auto icon_path = TryResolvePortraitIconTexturePath(object);
      icon_path.has_value()) {
    SetMaskedPortraitIconTexture(L, texture_index, *icon_path);
    return call.boolean(true);
  }

  if (const auto tracked_party_portrait =
          TryResolveTrackedPartyControlledTemporaryPortraitTexturePath(*session, guid);
      tracked_party_portrait.has_value()) {
    BindPortraitTexturePath(L, texture_index, *tracked_party_portrait);
    return call.boolean(true);
  }

  const auto snapshot = UnitFrameDataProvider::Get().GetUnitData(unit_id);
  const ObjectGuid tracked_guid = !guid.IsEmpty() ? guid : snapshot.guid;
  if (!tracked_guid.IsEmpty() || snapshot.has_data) {
    const std::uint32_t display_id =
        ResolvePortraitDisplayId(*session, object, tracked_guid, unit_id);
    if (snapshot.has_data || display_id != 0) {
      BindPortraitTexturePath(L, texture_index, ResolveTemporaryPortraitTexturePath(tracked_guid));
      return call.boolean(true);
    }
  }

  ClearPortraitState(L, texture_index);
  return call.boolean(false);
}

int LuaSetPortraitToTexture(lua_State *L) {
  const int texture_index = PushPortraitTextureTarget(L);
  if (lua_isstring(L, 2) != 0) {
    const std::string source_path = SafeLuaString(L, 2);
    if (!source_path.empty()) {
      SetMaskedPortraitIconTexture(L, texture_index, source_path);
    }
  }
  lua_pop(L, 1);
  return 0;
}

int LuaQuit(lua_State *L) {
  (void)L;
  openwow::net::ClientServices::Instance().RequestQuit();
  return 0;
}

int LuaForceQuit(lua_State *L) {
  (void)L;
  openwow::core::RequestClientShutdownWithErrorCode(0);
  return 0;
}

int LuaAcceptResurrect(lua_State *L) {
  auto *session = GetWorldSession(L);
  auto *death_manager = GetDeathManager(L);
  if (!session || !death_manager)
    return 0;
  if (session->objects().GetLocalPlayerTyped() == nullptr) {
    return 0;
  }
  death_manager->AcceptResurrect();
  return 0;
}

int LuaDeclineResurrect(lua_State *L) {
  auto *session = GetWorldSession(L);
  auto *death_manager = GetDeathManager(L);
  if (!session || !death_manager)
    return 0;
  if (session->objects().GetLocalPlayerTyped() == nullptr) {
    return 0;
  }
  death_manager->DeclineResurrect();
  return 0;
}

int LuaRepopMe(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  const auto *player = session->objects().GetLocalPlayerTyped();
  if (player == nullptr || !player->State().IsDead()) {
    return 0;
  }

  session->interaction().SendRepopRequest(false);
  return 0;
}

int LuaRetrieveCorpse(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  auto *dm = GetDeathManager(L);
  if (dm && session->objects().GetLocalPlayerTyped() != nullptr) {
    dm->DeclineResurrect();
  }

  session->interaction().SendReclaimCorpse();
  return 0;
}

int LuaGetCorpseRecoveryDelay(lua_State *L) {
  auto *dm = GetDeathManager(L);
  if (dm) {
    lua_pushnumber(L, dm->GetCorpseRecoveryDelaySeconds(core::GameClock::GetTickCount32()));
    return 1;
  }
  lua_pushnumber(L, 0);
  return 1;
}

int LuaGetReleaseTimeRemaining(lua_State *L) {
  auto *dm = GetDeathManager(L);
  if (dm) {
    lua_pushnumber(L, dm->GetReleaseTimeRemainingSeconds(core::GameClock::GetTickCount32()));
    return 1;
  }
  lua_pushnumber(L, 0);
  return 1;
}

int LuaResurrectGetOfferer(lua_State *L) {
  auto *death_manager = GetDeathManager(L);
  auto *session = GetWorldSession(L);
  if (death_manager != nullptr && session != nullptr) {
    const auto request_name_query = [session](const std::uint64_t guid) {
      if (guid == 0) {
        return;
      }

      (void)session->query_cache().RequestNameQuery(guid);
    };

    if (const auto name = death_manager->ResolveResurrectOffererForLua(session->query_cache(),
                                                                       request_name_query);
        name) {
      lua_pushstring(L, name->c_str());
      return 1;
    }
  }
  lua_pushnil(L);
  return 1;
}

int LuaResurrectHasSickness(lua_State *L) {
  if (auto *death_manager = GetDeathManager(L);
      death_manager != nullptr && death_manager->resurrect_has_sickness()) {
    lua_pushnumber(L, 1.0);
    return 1;
  }
  lua_pushnil(L);
  return 1;
}

int LuaResurrectHasTimer(lua_State *L) {
  bool suppress_timer = false;
  if (const auto *dbc = GetDbcLoader(L); dbc != nullptr) {
    const auto *session = GetWorldSession(L);
    const auto current_map_id =
        session != nullptr ? session->current_map_id() : 0u;
    if (const auto *entry = dbc->map().LookupEntry(current_map_id); entry != nullptr) {
      suppress_timer =
          entry->map_type == static_cast<std::uint32_t>(openwow::data::dbc::MapType::kArena);
    }
  }

  if (auto *death_manager = GetDeathManager(L);
      death_manager != nullptr && death_manager->resurrect_has_timer() && !suppress_timer) {
    lua_pushnumber(L, 1.0);
    return 1;
  }
  lua_pushnil(L);
  return 1;
}

int LuaRegisterForSave(lua_State *L) {
  if (!SecureExecution::Get().IsSecure(L)) {
    return luaL_error(L, "RegisterForSave() is only available to Blizzard scripts");
  }

  if (lua_isstring(L, 1) == 0) {
    return luaL_error(L, "Usage: RegisterForSave(\"variable\")");
  }

  openwow::ui::game::RegisterSavedVariableName(SavedVariableRegistrationScope::kAccount,
                                               lua_tostring(L, 1));
  return 0;
}

int LuaReloadUI(lua_State *L) {

  if (!GameUI_CanPerformHardwareEventAction()) {
    return 0;
  }
  openwow::ui::game::RequestWorldUiReload(L);
  return 0;
}

int LuaStuck(lua_State *L) {
  if (!GameUI_CanPerformProtectedAction(protected_action_kind::kSpellCast)) {
    return 0;
  }
  if (const auto *session = GetWorldSession(L); session != nullptr) {
    (void)SpellAction_ValidateAndInitiateCast(
        *session, kStuckSpellId, 0, -1,
        0);
  }
  return 0;
}

int LuaCameraZoomIn(lua_State *L) {
  const float amount = ReadOptionalCameraZoomIncrement(L);
  if (auto* manager = runtime::WorldUiRuntimeContext::FromLua(L); manager != nullptr) {
    ZoomActiveCamera(manager->world_camera(), true, amount);
  }
  return 0;
}

int LuaCameraZoomOut(lua_State *L) {
  const float amount = ReadOptionalCameraZoomIncrement(L);
  if (auto* manager = runtime::WorldUiRuntimeContext::FromLua(L); manager != nullptr) {
    ZoomActiveCamera(manager->world_camera(), false, amount);
  }
  return 0;
}

int LuaGetFramesRegisteredForEvent(lua_State *L) {
  if (lua_isstring(L, 1) == 0) {
    return luaL_error(L, "Usage: GetFramesRegisteredForEvent(\"event\")");
  }

  const char *event_name_arg = lua_tostring(L, 1);
  const std::string event_name = event_name_arg != nullptr ? event_name_arg : "";

  std::vector<int> frame_refs;
  if (const auto *mgr = GetGameUiManager(L); mgr != nullptr) {
    const auto registered_frames =
        mgr->frame_events().dispatcher().GetFramesRegisteredForEvent(event_name);
    frame_refs.reserve(registered_frames.size());
    for (const int ref : registered_frames) {
      if (ref == LUA_NOREF || ref == LUA_REFNIL) {
        continue;
      }
      frame_refs.push_back(ref);
    }
  }

  if (frame_refs.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      lua_checkstack(L, static_cast<int>(frame_refs.size())) == 0) {
    return luaL_error(L, "GetFramesRegisteredForEvent(%s): Stack overflow",
                      event_name_arg);
  }
  const int result_count = static_cast<int>(frame_refs.size());

  for (const int ref : frame_refs) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
  }

  return result_count;
}

int LuaGetScreenHeight(lua_State *L) {

  constexpr lua_Number kRetailScriptScreenHeight = 768.0;
  const lua_Number scale = UiParentEffectiveScale(L).value_or(1.0);
  lua_pushnumber(L, kRetailScriptScreenHeight / scale);
  return 1;
}

int LuaGetScreenWidth(lua_State *L) {

  const lua_Number scale = UiParentEffectiveScale(L).value_or(1.0);
  const lua_Number script_width =
      static_cast<lua_Number>(openwow::ui::GetUiScriptScreenWidth());
  lua_pushnumber(L, script_width / scale);
  return 1;
}

int LuaInCinematic(lua_State *L) {
  auto *session = GetWorldSession(L);
  bool active = session && session->spell_visual().cinematic_active();
  if (active) {
    lua_pushnumber(L, 1.0);
  } else {
    lua_pushnil(L);
  }
  return 1;
}

int LuaIsOutOfBounds(lua_State *L) {
  constexpr std::uint32_t kPlayerFlagsOutOfBounds = 0x00004000u;
  const auto *session = GetWorldSession(L);
  const auto *player =
      session != nullptr ? session->objects().GetActivePlayer() : nullptr;
  if (player != nullptr &&
      (player->GetPlayerFlags() & kPlayerFlagsOutOfBounds) != 0u) {
    FrameScript_PushNumber(L, 1.0);
  } else {
    FrameScript_PushNil(L);
  }
  return 1;
}

int LuaRequestTimePlayed(lua_State *L) {
  auto *session = GetWorldSession(L);
  if (!session)
    return 0;

  session->interaction().SendPlayedTime(true);
  return 0;
}

int LuaUpdateAddOnCPUUsage(lua_State *L) {
  UpdateLuaAddonCpuUsage(L);
  return 0;
}

int LuaUpdateAddOnMemoryUsage(lua_State *L) {
  InstallLuaAddonMemoryTracker(L);
  RefreshLuaAddonMemoryUsage(L);
  return 0;
}

int LuaRetailReleaseDebugCommand([[maybe_unused]] lua_State* state) {
  return 0;
}

int LuaGetWaterDetailRetail(lua_State* state) {
  lua_pushnumber(state, 0.0);
  return 1;
}

int LuaGetTimeToWellRestedRetail(lua_State* state) {
  lua_pushnil(state);
  return 1;
}

}

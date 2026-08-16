#include "openwow/core/client_misc.h"
#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/audio/playback/sound_engine.h"
#include "openwow/core/client_misc_internal.h"
#include "openwow/core/console.h"
#include "openwow/core/decimal_parse.h"
#include "openwow/runtime/scheduling/evt_sched.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/core/login_state_handler.h"
#include "openwow/core/init_subsystems.h"
#include "openwow/platform/adapters/sdl/platform_layer.h"
#include "openwow/core/storm_alloc.h"
#include "openwow/core/storm_string.h"
#include "openwow/core/storm_utils.h"
#include "openwow/data/async_file_read.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_table_registry.h"
#include "openwow/data/streaming_init.h"
#include "openwow/debug/diagnostics/debug_console.h"
#include "openwow/game/declined_words.h"
#include "openwow/game/comsat_client.h"
#include "openwow/game/input_control.h"
#include "openwow/game/object_effect_system.h"
#include "openwow/game/spell_visual_pipeline.h"
#include "openwow/game/spellbook_system.h"
#include "openwow/game/world_map_continent_lookup.h"
#include "openwow/game/zone_text_system.h"
#include "openwow/game/spell_visual_system.h"
#include "openwow/net/client_services_packet_sender.h"
#include "openwow/net/realm_config_tables.h"
#include "openwow/render/backend/bgfx/renderer_context_services.h"
#include "openwow/render/models/animation/spline.h"
#include "openwow/render/ui/ui_shaders.h"
#include "openwow/net/wotlk/glue_startup_handlers.h"
#include "openwow/screens/loading_screen_manager.h"
#include "openwow/ui/addons_data.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/platform/filesystem/filesystem.h"
#include "openwow/storage/persistence/profile_paths.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#endif

#ifndef _WIN32
#include <unistd.h>
#include "openwow/foundation/compiler/printf_format.h"
#endif

namespace openwow::core {

static uint32_t dword_AC3228 = 0;

namespace {

struct RetailDebugCommandState {
  std::mutex mutex;
  RetailDebugCommandBindings bindings;
};

struct WorldportDestination {
  std::uint32_t map_id = 0;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float orientation = 0.0f;
};

constexpr float kWorldportDegreesToRadians = 0.017453292f;

RetailDebugCommandState &MutableRetailDebugCommandState() {
  static RetailDebugCommandState state;
  return state;
}

[[nodiscard]] RetailDebugCommandBindings GetRetailDebugCommandBindingsSnapshot() {
  auto &state = MutableRetailDebugCommandState();
  std::scoped_lock lock(state.mutex);
  return state.bindings;
}

[[nodiscard]] bool IsWorldportDigit(char ch) {
  return static_cast<unsigned char>(ch - '0') <= 9u;
}

[[nodiscard]] std::vector<std::string_view> TokenizeWorldportArguments(
    std::string_view raw_args) {
  static constexpr std::string_view kDelimiters = "\t\r\n\" ";

  std::vector<std::string_view> tokens;
  std::size_t offset = raw_args.find_first_not_of(kDelimiters);
  while (offset != std::string_view::npos) {
    const std::size_t end = raw_args.find_first_of(kDelimiters, offset);
    tokens.push_back(raw_args.substr(offset, end - offset));
    offset = raw_args.find_first_not_of(kDelimiters, end);
  }

  return tokens;
}

void SendWorldportPacket(const WorldportDestination &destination) {
  openwow::net::wotlk::WorldPacket packet(
      openwow::net::wotlk::Opcode::CMSG_WORLD_TELEPORT);
  packet.AppendU32(openwow::core::PlatformLayer::GetCPUCoreCount());
  packet.AppendU32(destination.map_id);
  packet.AppendU32(0u);
  packet.AppendU32(0u);
  packet.AppendFloat(destination.x);
  packet.AppendFloat(destination.y);
  packet.AppendFloat(destination.z);
  packet.AppendFloat(destination.orientation);
  (void)openwow::net::ClientServices__SendPacket(packet);
}

void ExecuteWorldportCommand(std::string_view raw_args) {
  const RetailDebugCommandBindings bindings = GetRetailDebugCommandBindingsSnapshot();
  if (!bindings.get_active_player_state) {
    return;
  }

  const std::optional<RetailDebugActivePlayerState> active_player_state =
      bindings.get_active_player_state();
  if (!active_player_state.has_value()) {
    return;
  }

  const std::string raw_args_text(raw_args);
  WorldportDestination destination{};
  const bool numeric_mode =
      !raw_args.empty() && IsWorldportDigit(raw_args.front());

  if (numeric_mode) {
    const std::vector<std::string_view> tokens = TokenizeWorldportArguments(raw_args);
    if (tokens.empty()) {
      openwow::core::ida::ConsoleAddLine(
          "Usage: worldport <continentID> [x y z] [facing]",
          openwow::core::ida::COLOR_WARNING);
      return;
    }

    destination.map_id = openwow::core::ParseSignedDecimalLikeSub76F0D0(tokens[0]);
    destination.x = active_player_state->x;
    destination.y = active_player_state->y;
    destination.z = active_player_state->z;
    destination.orientation = active_player_state->orientation;

    if (tokens.size() >= 2) {
      destination.x = ParseFloatLikeSub76FB80(tokens[1]);
    }
    if (tokens.size() >= 3) {
      destination.y = ParseFloatLikeSub76FB80(tokens[2]);
    }
    if (tokens.size() >= 4) {
      destination.z = ParseFloatLikeSub76FB80(tokens[3]);
    }
    if (tokens.size() >= 5) {
      destination.orientation =
          ParseFloatLikeSub76FB80(tokens[4]) * kWorldportDegreesToRadians;
    }
  } else {

    openwow::core::ida::ConsoleLog("Could not find location: %s",
                                   raw_args_text.c_str());
    return;
  }

  const bool is_valid_map = bindings.is_valid_map_id &&
                            bindings.is_valid_map_id(destination.map_id);
  if (!is_valid_map) {
    openwow::core::ida::ConsoleLogColored("Bad world number: %i\n",
                                          openwow::core::ida::COLOR_ERROR,
                                          static_cast<std::int32_t>(destination.map_id));
    return;
  }

  SendWorldportPacket(destination);
}

void ResizeDynamicElementBuffer(LegacyResizableBufferView buffer, std::uint32_t new_count,
                                std::size_t element_size, const char *tag) {
  ResizeLegacyArrayStoragePreservingPrefix(buffer, new_count, element_size, tag);
}

std::uint32_t ResolveDynamicOverlayGrowQuantum(LoadingScreenDynamicOverlayVertices *thisPtr,
                                               std::uint32_t requested_count) {
  if (requested_count >= 10) {
    thisPtr->growth_quantum = 10;
    return 10;
  }

  if (requested_count == 0) {
    return 1;
  }

  std::uint32_t quantum = 1;
  while ((quantum << 1) != 0 && (quantum << 1) <= requested_count) {
    quantum <<= 1;
  }
  return quantum;
}

void ZeroDynamicElementVert(DynamicElementVert *base, std::uint32_t index) {
  if (!base) {
    return;
  }

  auto *vert = base + index;
  vert->x = 0.0f;
  vert->y = 0.0f;
  vert->z = 0.0f;
  vert->u = 0.0f;
  vert->v = 0.0f;
  vert->color = 0;
}

struct LoadingScreenMapPoint {
  float x = 0.0f;
  float y = 0.0f;
};

constexpr float kLoadingScreenSplineSampleScale = 66.666672f;
constexpr float kLoadingScreenMarkerHalfWidth = 0.02f;
constexpr float kLoadingScreenMarkerHalfHeight = 0.0265f;
constexpr float kLoadingScreenRibbonHalfWidth = 0.01f;
constexpr float kLoadingScreenRibbonHalfHeight = 0.015f;
constexpr std::uint32_t kLoadingScreenMarkerColor = 0xFFFFFFFFu;
constexpr std::size_t kLoadingScreenMaxIntermediatePoints = 8;

[[nodiscard]] const data::dbc::WorldMapContinentEntry *
FindWorldMapContinent(std::span<const data::dbc::WorldMapContinentEntry> world_map_continents,
                      std::uint32_t map_id) {
  const auto continent_index =
      openwow::game::FindLastWorldMapContinentIndexByMapId(world_map_continents, map_id);
  if (continent_index < 0) {
    return nullptr;
  }

  return &world_map_continents[static_cast<std::size_t>(continent_index)];
}

bool ProjectTaxiNodeToLoadingScreen(
    const data::dbc::TaxiPathNodeEntry &node,
    std::span<const data::dbc::WorldMapContinentEntry> world_map_continents,
    LoadingScreenMapPoint *out_point) {
  if (!out_point) {
    return false;
  }

  const auto *continent = FindWorldMapContinent(world_map_continents, node.map_id);
  if (!continent) {
    return false;
  }

  out_point->x = continent->continent_offset_x * 0.015968064f + 0.5f -
                 node.y * 0.00002994012f * continent->scale;

  const float projected_y = continent->continent_offset_y * 0.023952097f + 0.5f -
                            node.x * 0.000044910183f * continent->scale;
  out_point->y = 1.0f - projected_y;
  return true;
}

void WriteDynamicOverlayVertex(DynamicElementVert *vertex, float x, float y, float z, float u,
                               float v, std::uint32_t color) {
  vertex->x = x;
  vertex->y = y;
  vertex->z = z;
  vertex->u = u;
  vertex->v = v;
  vertex->color = color;
}

constexpr std::array<std::size_t, 12> kSpellVisualLegacyKitFieldOffsets = {
    8u, 16u, 24u, 88u, 92u, 100u, 12u, 56u, 60u, 96u, 4u, 20u,
};

template <typename Callback>
void ForEachKnownSpellVisualRecord(const data::dbc::DbcLoader &dbc_loader, Callback &&callback) {
  const auto &known_spells =
      openwow::game::SpellbookSystem::Get().GetKnownSpellList();
  for (const auto &spell_info : known_spells) {
    if (!spell_info.is_known || spell_info.spell_id == 0) {
      continue;
    }

    const auto *spell = dbc_loader.spell().LookupEntry(spell_info.spell_id);
    if (spell == nullptr) {
      continue;
    }

    for (const std::uint32_t spell_visual_id : spell->spell_visual) {
      if (spell_visual_id == 0) {
        continue;
      }

      const auto *visual = dbc_loader.spell_visual().LookupEntry(spell_visual_id);
      if (visual == nullptr) {
        continue;
      }

      callback(*visual);
    }
  }
}

bool RequestSpellVisualKitEffectModelPreloads(
    const data::dbc::SpellVisualKitEntry &kit,
    const data::dbc::DbcStore<data::dbc::SpellVisualEffectNameEntry> &effects) {
  const std::array<std::uint32_t, 12> effect_ids = {
      kit.head_effect,       kit.chest_effect,      kit.base_effect,
      kit.left_hand_effect,  kit.right_hand_effect, kit.breath_effect,
      kit.left_weapon_effect, kit.right_weapon_effect, kit.special1_effect,
      kit.special2_effect,   kit.special3_effect,   kit.world_effect,
  };

  bool requested_any = false;
  for (const std::uint32_t effect_id : effect_ids) {
    if (effect_id == 0) {
      continue;
    }

    const auto *effect = effects.LookupEntry(effect_id);
    if (effect == nullptr) {
      continue;
    }

    requested_any |= openwow::game::RequestSpellVisualEffectModelPreload(*effect);
  }

  return requested_any;
}

void PreloadKnownSpellMissileModels(const data::dbc::DbcLoader &dbc_loader) {
  const auto &effects = dbc_loader.spell_visual_effect_name();
  ForEachKnownSpellVisualRecord(dbc_loader, [&](const data::dbc::SpellVisualEntry &visual) {
    if (visual.has_missile == 0 || visual.missile_model == 0) {
      return;
    }

    const auto *effect = effects.LookupEntry(static_cast<std::uint32_t>(visual.missile_model));
    if (effect == nullptr) {
      return;
    }

    (void)openwow::game::RequestSpellVisualEffectModelPreload(*effect);
  });
}

void PreloadKnownSpellVisualKitFieldOffset(
    const data::dbc::DbcLoader &dbc_loader,
    const std::size_t field_offset) {
  static_assert(sizeof(data::dbc::SpellVisualEntry) == 32u * sizeof(std::uint32_t));
  static_assert(std::is_trivially_copyable_v<data::dbc::SpellVisualEntry>);

  const auto field_index = field_offset / sizeof(std::uint32_t);
  const auto &kits = dbc_loader.spell_visual_kit();
  const auto &effects = dbc_loader.spell_visual_effect_name();

  ForEachKnownSpellVisualRecord(dbc_loader, [&](const data::dbc::SpellVisualEntry &visual) {
    std::array<std::uint32_t, 32> raw_fields{};
    std::memcpy(raw_fields.data(), &visual, sizeof(visual));

    const std::uint32_t kit_id = raw_fields[field_index];
    if (kit_id == 0) {
      return;
    }

    const auto *kit = kits.LookupEntry(kit_id);
    if (kit == nullptr) {
      return;
    }

    (void)RequestSpellVisualKitEffectModelPreloads(*kit, effects);
  });
}

void PreloadKnownSpellVisualStreamingResources(const data::dbc::DbcLoader &dbc_loader) {
  PreloadKnownSpellMissileModels(dbc_loader);
  for (const std::size_t field_offset : kSpellVisualLegacyKitFieldOffsets) {
    PreloadKnownSpellVisualKitFieldOffset(dbc_loader, field_offset);
  }
}

void WriteLoadingScreenEndpointMarkers(DynamicElementVert *vertices,
                                       const LoadingScreenMapPoint &current_segment_end,
                                       const LoadingScreenMapPoint &next_segment_start) {
  if (!vertices) {
    return;
  }

  WriteDynamicOverlayVertex(vertices + 0, current_segment_end.x - kLoadingScreenMarkerHalfWidth,
                            current_segment_end.y - kLoadingScreenMarkerHalfHeight, 0.0f, 0.5f,
                            0.5f, kLoadingScreenMarkerColor);
  WriteDynamicOverlayVertex(vertices + 1, current_segment_end.x + kLoadingScreenMarkerHalfWidth,
                            current_segment_end.y - kLoadingScreenMarkerHalfHeight, 0.0f, 1.0f,
                            0.5f, kLoadingScreenMarkerColor);
  WriteDynamicOverlayVertex(vertices + 2, current_segment_end.x - kLoadingScreenMarkerHalfWidth,
                            current_segment_end.y + kLoadingScreenMarkerHalfHeight, 0.0f, 0.5f,
                            0.0f, kLoadingScreenMarkerColor);
  WriteDynamicOverlayVertex(vertices + 3, current_segment_end.x + kLoadingScreenMarkerHalfWidth,
                            current_segment_end.y + kLoadingScreenMarkerHalfHeight, 0.0f, 1.0f,
                            0.0f, kLoadingScreenMarkerColor);

  WriteDynamicOverlayVertex(vertices + 4, next_segment_start.x - kLoadingScreenMarkerHalfWidth,
                            next_segment_start.y - kLoadingScreenMarkerHalfHeight, 0.0f, 0.0f, 0.5f,
                            kLoadingScreenMarkerColor);
  WriteDynamicOverlayVertex(vertices + 5, next_segment_start.x + kLoadingScreenMarkerHalfWidth,
                            next_segment_start.y - kLoadingScreenMarkerHalfHeight, 0.0f, 0.5f, 0.5f,
                            kLoadingScreenMarkerColor);
  WriteDynamicOverlayVertex(vertices + 6, next_segment_start.x - kLoadingScreenMarkerHalfWidth,
                            next_segment_start.y + kLoadingScreenMarkerHalfHeight, 0.0f, 0.0f, 0.0f,
                            kLoadingScreenMarkerColor);
  WriteDynamicOverlayVertex(vertices + 7, next_segment_start.x + kLoadingScreenMarkerHalfWidth,
                            next_segment_start.y + kLoadingScreenMarkerHalfHeight, 0.0f, 0.5f, 0.0f,
                            kLoadingScreenMarkerColor);
}

void WriteLoadingScreenRibbonQuads(DynamicElementVert *vertices, const render::CSpline &spline,
                                   std::uint32_t sample_count) {
  if (!vertices || sample_count == 0) {
    return;
  }

  const float step = 1.0f / static_cast<float>(sample_count);
  float sample_t = 0.0f;
  for (std::uint32_t sample_index = 0; sample_index <= sample_count; ++sample_index) {
    const float t = std::clamp(sample_t, 0.0f, 1.0f);
    const auto sample = spline.Evaluate(t, render::CSpline::kArcLengthParameterMode);
    auto *quad = vertices + sample_index * 4u;

    WriteDynamicOverlayVertex(quad + 0, sample.x - kLoadingScreenRibbonHalfWidth,
                              sample.y - kLoadingScreenRibbonHalfHeight, sample.z, 0.0f, 1.0f, 0);
    WriteDynamicOverlayVertex(quad + 1, sample.x + kLoadingScreenRibbonHalfWidth,
                              sample.y - kLoadingScreenRibbonHalfHeight, sample.z, 0.5f, 1.0f, 0);
    WriteDynamicOverlayVertex(quad + 2, sample.x - kLoadingScreenRibbonHalfWidth,
                              sample.y + kLoadingScreenRibbonHalfHeight, sample.z, 0.0f, 0.5f, 0);
    WriteDynamicOverlayVertex(quad + 3, sample.x + kLoadingScreenRibbonHalfWidth,
                              sample.y + kLoadingScreenRibbonHalfHeight, sample.z, 0.5f, 0.5f, 0);
    sample_t += step;
  }
}

detail::AsyncIORegisterDependencies MakeAsyncIORegisterDependencies() {
  return {
      .initialize_async_io =
          [](std::uint32_t thread_sleep, std::uint32_t handler_timeout) {
            openwow::data::AsyncIO_Initialize(thread_sleep, handler_timeout);
            return openwow::data::IsStreamingInitialized() ? 1 : 0;
          },
  };
}

detail::AsyncIORegisterDependencies &MutableAsyncIORegisterDependencies() {
  static detail::AsyncIORegisterDependencies deps = MakeAsyncIORegisterDependencies();
  return deps;
}

void NoOpGameCleanupStep() {}

void UnregisterDebugConsoleCommand(const char *name) {
  if (!name || *name == '\0') {
    return;
  }

  openwow::debug::DebugConsole::Get().UnregisterCommand(name);
}

int ShutdownCombatData() {
  openwow::data::CombatData_Shutdown();
  return 0;
}

void ClearGameCleanupFlag() {
  dword_AC3228 = 0;
}

detail::GameCleanupDependencies MakeGameCleanupDependencies() {

  return {
      .shutdown_world_audio = NoOpGameCleanupStep,
      .clear_declined_words = [] { openwow::game::DeclinedWords::Get().Clear(); },
      .shutdown_spell_visuals = openwow::game::SpellVisuals_Shutdown,
      .shutdown_chat_log = openwow::game::input::ChatLog_Shutdown_Thunk,

      .shutdown_login = [] {
        openwow::net::wotlk::AccountData_UnregisterOpcodeHandlers();
        openwow::net::RealmConfigTables::Get()
            .ClearSelectedRealmPlayerKillingAllowed();
        openwow::core::LoginConsoleDiagnostics::Instance().Shutdown();
      },
      .shutdown_addon_data = [] { openwow::ui::AddOnsData::Get().Clear(); },
      .shutdown_auxiliary_lookup = NoOpGameCleanupStep,
      .shutdown_dance_studio = NoOpGameCleanupStep,
      .unregister_query_opcodes = NoOpGameCleanupStep,
      .destroy_db_cache = NoOpGameCleanupStep,
      .reserved_cleanup = NoOpGameCleanupStep,
      .shutdown_character_components = NoOpGameCleanupStep,
      .clear_virtual_frames = NoOpGameCleanupStep,
      .shutdown_framexml_runtime = NoOpGameCleanupStep,
      .shutdown_voice_chat = NoOpGameCleanupStep,
      .shutdown_object_effect_data_store =
          [] { openwow::game::ObjectEffectDataStore::Instance().Shutdown(); },
      .shutdown_sound_system = [](int) {},
      .shutdown_frame_script = NoOpGameCleanupStep,

      .shutdown_sound_engine_data = NoOpGameCleanupStep,
      .cleanup_render_targets = GxRenderTarget_Cleanup,
      .cleanup_render_bootstrap = RenderBootstrap_FpsCleanup,
      .unregister_console_command = UnregisterDebugConsoleCommand,
      .shutdown_combat_data = ShutdownCombatData,
      .clear_cleanup_flag = ClearGameCleanupFlag,
  };
}

detail::GameCleanupDependencies &MutableGameCleanupDependencies() {
  static detail::GameCleanupDependencies deps = MakeGameCleanupDependencies();
  return deps;
}

}

static uint32_t *dword_B2FA04 = nullptr;

static uint32_t *dword_B2FA08 = nullptr;

static float flt_B2FEDC = 0.0f;

static char byte_B2FED9 = 0;

static char byte_B302F0[0x400] = {};

struct LoadingScreenStormInitState {
  std::mutex mutex;
  bool has_pending_params = false;
  StormInitParamsBlob cached_params{};
};

struct UiShaderInitState {
  LoadingScreenElementCatalogStorage taxi_path_catalog_storage;
  LoadingScreenElementCatalog taxi_path_catalog{};
  LoadingScreenWorldBackgroundGeometry world_background_geometry{};
  std::vector<openwow::data::dbc::TaxiPathNodeEntry> taxi_path_nodes;
  std::vector<openwow::data::dbc::LoadingScreenTaxiSplinesEntry>
      loading_screen_taxi_splines;
  std::vector<openwow::data::dbc::WorldMapContinentEntry> world_map_continents;
  bool initialized = false;
};

UiShaderInitState &MutableUiShaderInitState() {
  static UiShaderInitState state;
  return state;
}

detail::UiShaderInitDependencies &MutableUiShaderInitDependencies() {
  static detail::UiShaderInitDependencies deps{
      .prewarm_ui_program = &openwow::render::ui::PrewarmUiProgram,
  };
  return deps;
}

LoadingScreenDynamicMapChangeAssets &MutableLoadingScreenDynamicMapChangeAssets() {
  static LoadingScreenDynamicMapChangeAssets assets;
  return assets;
}

LoadingScreenStormInitState &MutableLoadingScreenStormInitState() {
  static LoadingScreenStormInitState state;
  return state;
}

namespace {

const openwow::data::dbc::TaxiPathNodeEntry *FindTaxiPathNodeById(
    const std::span<const openwow::data::dbc::TaxiPathNodeEntry> taxi_path_nodes,
    const std::int32_t node_id) {
  const auto match = std::find_if(
      taxi_path_nodes.begin(), taxi_path_nodes.end(), [node_id](const auto &node) {
        return static_cast<std::int32_t>(node.id) == node_id;
      });
  return match == taxi_path_nodes.end() ? nullptr : &*match;
}

}

bool SErrGetLastLogPath(char *buf, int buf_size);
int SThread_SpawnProcess(const char *application_name, const char *command_line,
                         std::uintptr_t wait_callback, std::intptr_t callback_arg);

static void SStrCopy(char *dst, const char *src, int max_len) {
  if (dst && src) {
    std::strncpy(dst, src, static_cast<size_t>(max_len) - 1);
    dst[max_len - 1] = '\0';
  }
}

static char *SStrCat(char *dst, const char *src, int buf_size) {
  if (dst && src) {
    std::strncat(dst, src, static_cast<size_t>(buf_size) - std::strlen(dst) - 1);
  }
  return dst;
}

OPENWOW_PRINTF_FORMAT(3, 4) static void SStrPrintf(char *dst, int size,
                                                              const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(dst, static_cast<size_t>(size), fmt, args);
  va_end(args);
}

struct MovementRuntimeState {
  std::mutex mutex;
  char configured_log_path[260] = {};
  std::FILE *log_file_handle = nullptr;

  std::uint32_t runtime_flags = 0;
  std::uint32_t previous_update_tick_ms = 0;
  std::uint32_t current_update_tick_ms = 0;
  std::uint64_t update_count = 0;

  std::uint32_t current_transport_context = 0;
  std::uint32_t previous_transport_context = 0;

  std::uint32_t current_movement_timestamp_ms = 0;
  std::uint32_t previous_movement_timestamp_ms = 0;
  bool pending_transport_time2 = false;
};

MovementRuntimeState &MutableMovementRuntimeState() {
  static MovementRuntimeState state;
  return state;
}

detail::MovementRuntimeTickCountProvider &MutableMovementRuntimeTickCountProvider() {
  static detail::MovementRuntimeTickCountProvider provider = nullptr;
  return provider;
}

std::uint32_t ReadMovementRuntimeTickCount() {
  const auto provider = MutableMovementRuntimeTickCountProvider();
  return provider != nullptr ? provider() : GameClock::GetTickCount32();
}

bool HasPositiveMovementRuntimeTickDelta(const std::uint32_t current_tick_ms,
                                         const std::uint32_t previous_tick_ms) {
  return static_cast<std::int32_t>(current_tick_ms - previous_tick_ms) > 0;
}

int MovementRuntime_PeriodicUpdateEvent(void * , int );

int LegacyMovementRuntimePeriodicUpdateCallback() {
  static const int callback = EvtSched_RegisterLegacyCallback(
      reinterpret_cast<std::intptr_t>(&MovementRuntime_PeriodicUpdateEvent));
  return callback;
}

std::filesystem::path ComposeMoveLogRegistryFallbackPath(const char *key, const char *value_name) {
  auto path = openwow::storage::persistence::GetDefaultProfileRoot() /
              "registry" / "current_user";
  path /= "Software";
  path /= "Blizzard Entertainment";

  if (key && *key) {
    std::string segment;
    for (const char ch : std::string_view(key)) {
      if (ch == '\\') {
        if (!segment.empty()) {
          path /= segment;
          segment.clear();
        }
      } else {
        segment.push_back(ch);
      }
    }
    if (!segment.empty()) {
      path /= segment;
    }
  }

  if (value_name && *value_name) {
    path /= std::string(value_name) + ".txt";
  }

  return path;
}

bool ReadMoveLogRegistryFallback(const char *key, const char *value_name, char *out, int out_size) {
  if (!out || out_size <= 0) {
    return false;
  }

  out[0] = '\0';
  const auto path = ComposeMoveLogRegistryFallbackPath(key, value_name);
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return false;
  }

  std::string value((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
    value.pop_back();
  }

  std::snprintf(out, static_cast<std::size_t>(out_size), "%s", value.c_str());
  return true;
}

void WriteMoveLogRegistryFallback(const char *key, const char *value_name, const char *data) {
  const auto path = ComposeMoveLogRegistryFallbackPath(key, value_name);
  const std::string content = data ? std::string(data) : std::string();
  (void)openwow::platform::filesystem::AtomicWriteFile(path, content);
}

#if defined(_WIN32)
std::wstring WidenMoveLogRegistryText(const char *text) {
  if (!text) {
    return {};
  }

  const int wide_chars = ::MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
  if (wide_chars <= 0) {
    return {};
  }

  std::wstring wide(static_cast<std::size_t>(wide_chars), L'\0');
  if (::MultiByteToWideChar(CP_ACP, 0, text, -1, wide.data(), wide_chars) != wide_chars) {
    return {};
  }

  wide.pop_back();
  return wide;
}
#endif

bool ReadMoveLogRegistryValue(const char *key, const char *value_name, std::uint8_t ,
                              char *out, int out_size) {
  if (!key || !*key || !value_name || !*value_name || !out || out_size <= 0) {
    if (out && out_size > 0) {
      out[0] = '\0';
    }
    return false;
  }

#if defined(_WIN32)
  const std::wstring full_path =
      WidenMoveLogRegistryText((std::string("Software\\Blizzard Entertainment\\") + key).c_str());
  const std::wstring wide_value_name = WidenMoveLogRegistryText(value_name);
  if (!full_path.empty() && !wide_value_name.empty()) {
    HKEY handle = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, full_path.c_str(), 0, KEY_READ, &handle) ==
        ERROR_SUCCESS) {
      std::wstring value(static_cast<std::size_t>(out_size), L'\0');
      DWORD bytes = static_cast<DWORD>(value.size() * sizeof(wchar_t));
      DWORD reg_type = 0;
      const LSTATUS query = ::RegQueryValueExW(handle, wide_value_name.c_str(), nullptr, &reg_type,
                                               reinterpret_cast<BYTE *>(value.data()), &bytes);
      ::RegCloseKey(handle);
      if (query == ERROR_SUCCESS && (reg_type == REG_SZ || reg_type == REG_EXPAND_SZ)) {
        if (!value.empty()) {
          value.back() = L'\0';
        }
        const int written =
            ::WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1, out, out_size, nullptr, nullptr);
        if (written > 0) {
          return true;
        }
      }
    }
  }
  if (out_size > 0) {
    out[0] = '\0';
  }
  return false;
#else
  return ReadMoveLogRegistryFallback(key, value_name, out, out_size);
#endif
}

void WriteMoveLogRegistryValue(const char *key, const char *value_name, std::uint8_t ,
                               const char *data) {
  if (!key || !*key || !value_name || !*value_name || !data) {
    return;
  }

#if defined(_WIN32)
  const std::wstring full_path =
      WidenMoveLogRegistryText((std::string("Software\\Blizzard Entertainment\\") + key).c_str());
  const std::wstring wide_value_name = WidenMoveLogRegistryText(value_name);
  const std::wstring wide_data = WidenMoveLogRegistryText(data);
  if (full_path.empty() || wide_value_name.empty() || wide_data.empty()) {
    return;
  }

  HKEY handle = nullptr;
  if (::RegCreateKeyExW(HKEY_CURRENT_USER, full_path.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr,
                        &handle, nullptr) != ERROR_SUCCESS) {
    return;
  }

  const DWORD byte_count = static_cast<DWORD>((wide_data.size() + 1) * sizeof(wchar_t));
  (void)::RegSetValueExW(handle, wide_value_name.c_str(), 0, REG_SZ,
                         reinterpret_cast<const BYTE *>(wide_data.c_str()), byte_count);
  (void)::RegCloseKey(handle);
#else
  WriteMoveLogRegistryFallback(key, value_name, data);
#endif
}

void EnsureSplineOptCVarRegistered() {
  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  cvars.RegisterCVar("SplineOpt", "1", openwow::ui::game::CVarFlags::Archive,
                     "toggles use of spline coll optimization");
}

void InitializeMovementRuntime(const char *resolved_path) {
  auto &state = MutableMovementRuntimeState();
  const std::uint32_t now_tick_ms = ReadMovementRuntimeTickCount();

  std::lock_guard lock(state.mutex);
  std::memset(state.configured_log_path, 0, sizeof(state.configured_log_path));
  SStrCopy(state.configured_log_path, resolved_path ? resolved_path : "",
           static_cast<int>(sizeof(state.configured_log_path)));
  state.runtime_flags = 1u;
  state.previous_update_tick_ms = now_tick_ms;
  state.current_update_tick_ms = now_tick_ms;
  state.update_count = 0;
  state.current_transport_context = 0;
  state.previous_transport_context = 0;
  state.current_movement_timestamp_ms = 0;
  state.previous_movement_timestamp_ms = 0;
  state.pending_transport_time2 = false;

  EnsureSplineOptCVarRegistered();
}

void CloseMovementRuntimeLogFile() {
  auto& state = MutableMovementRuntimeState();
  if (state.log_file_handle) {
    std::fclose(state.log_file_handle);
    state.log_file_handle = nullptr;
  }
}

int MovementRuntime_PeriodicUpdateEvent(void * , int ) {
  auto &state = MutableMovementRuntimeState();
  const std::uint32_t now_tick_ms = ReadMovementRuntimeTickCount();

  std::lock_guard lock(state.mutex);
  if (!HasPositiveMovementRuntimeTickDelta(now_tick_ms, state.previous_update_tick_ms)) {
    return 1;
  }

  state.current_update_tick_ms = now_tick_ms;
  state.previous_update_tick_ms = now_tick_ms;
  ++state.update_count;
  return 1;
}

void ScheduleMovementRuntimePeriodicUpdate(float priority) {
  const int callback = LegacyMovementRuntimePeriodicUpdateCallback();
  (void)EvtContext_UnregisterCurrentHandler(6u, callback);
  EvtContext_RegisterCurrentHandler(6u, callback, 0, priority);
}

bool TryGetMovementRuntimeTimestampFloor(std::uint32_t &timestamp_floor_ms) {
  const auto& state = MutableMovementRuntimeState();
  if ((state.runtime_flags & 1u) == 0) {
    return false;
  }

  timestamp_floor_ms = state.current_update_tick_ms;
  return true;
}

void CMovementRuntime_PushPreviousTransportContext(std::uint32_t transport_id) {
  auto& state = MutableMovementRuntimeState();
  std::lock_guard lock(state.mutex);
  state.previous_transport_context = state.current_transport_context;
  state.current_transport_context = transport_id;
}

void CMovementRuntime_SetMovementTimestamp(const std::uint32_t timestamp_ms) {
  auto& state = MutableMovementRuntimeState();
  std::lock_guard lock(state.mutex);
  state.previous_movement_timestamp_ms = state.current_movement_timestamp_ms;
  state.current_movement_timestamp_ms = timestamp_ms;
}

std::uint32_t CMovementRuntime_GetMovementTimestamp() {
  auto& state = MutableMovementRuntimeState();
  std::lock_guard lock(state.mutex);
  return state.current_movement_timestamp_ms;
}

void CMovementRuntime_RestoreMovementTimestampState(
    const std::uint32_t current_timestamp_ms,
    const std::uint32_t previous_timestamp_ms) {
  auto& state = MutableMovementRuntimeState();
  std::lock_guard lock(state.mutex);
  state.current_movement_timestamp_ms = current_timestamp_ms;
  state.previous_movement_timestamp_ms = previous_timestamp_ms;
}

void CMovementRuntime_MarkTransportTimestampTransition() {
  auto& state = MutableMovementRuntimeState();
  std::lock_guard lock(state.mutex);
  state.pending_transport_time2 =
      state.current_movement_timestamp_ms != state.previous_movement_timestamp_ms;
}

bool CMovementRuntime_TakePendingTransportTime2(std::uint32_t& time2_ms) {
  auto& state = MutableMovementRuntimeState();
  std::lock_guard lock(state.mutex);
  if (!state.pending_transport_time2) {
    return false;
  }

  time2_ms = state.previous_movement_timestamp_ms;
  state.pending_transport_time2 = false;
  return true;
}

detail::MoveLogFileDependencies MakeMoveLogFileDependencies() {
  return {
      .read_registry_string = ReadMoveLogRegistryValue,
      .write_registry_string = WriteMoveLogRegistryValue,
      .get_process_id = Storm_GetCurrentProcessId,
      .initialize_movement_runtime = InitializeMovementRuntime,
      .schedule_periodic_update = ScheduleMovementRuntimePeriodicUpdate,
  };
}

detail::MoveLogFileDependencies &MutableMoveLogFileDependencies() {
  static detail::MoveLogFileDependencies deps = MakeMoveLogFileDependencies();
  return deps;
}

struct CVar_Stub {
  const char *name;
  int intValue;
};

static CVar_Stub s_asyncThreadSleep = {"asyncThreadSleep", 0};
static CVar_Stub s_asyncHandlerTimeout = {"asyncHandlerTimeout", 100};
static CVar_Stub s_timingMethod = {"timingMethod", 0};
static CVar_Stub s_timingTestError = {"timingTestError", 0};

static std::uint32_t ParseStormSignedDecimalPrefix(std::string_view text) {
  if (text.empty()) {
    return 0;
  }

  std::size_t index = 0;
  const bool negative = text.front() == '-';
  if (negative) {
    index = 1;
    if (index == text.size()) {
      return 0;
    }
  }

  const std::uint32_t first_digit =
      static_cast<unsigned char>(text[index]) - static_cast<unsigned char>('0');
  if (first_digit >= 10u) {
    return 0;
  }

  std::uint32_t value = first_digit;
  ++index;
  while (index < text.size()) {
    const std::uint32_t digit =
        static_cast<unsigned char>(text[index]) - static_cast<unsigned char>('0');
    if (digit >= 10u) {
      break;
    }

    value = value * 10u + digit;
    ++index;
  }

  if (negative) {
    return 0u - value;
  }

  return value;
}

static CVar_Stub *FindClientMiscCVarStub(const char *name) {
  if (!name) {
    return nullptr;
  }

  if (std::strcmp(name, s_asyncThreadSleep.name) == 0) {
    return &s_asyncThreadSleep;
  }
  if (std::strcmp(name, s_asyncHandlerTimeout.name) == 0) {
    return &s_asyncHandlerTimeout;
  }
  if (std::strcmp(name, s_timingMethod.name) == 0) {
    return &s_timingMethod;
  }
  if (std::strcmp(name, s_timingTestError.name) == 0) {
    return &s_timingTestError;
  }

  return nullptr;
}

static CVar_Stub *CVar_LookupByName(const char *name) {
  auto *stub = FindClientMiscCVarStub(name);
  if (!stub) {
    return nullptr;
  }

  stub->intValue = openwow::ui::game::CVarSystem::Instance().GetCVarInt(stub->name);
  return stub;
}

static CVar_Stub *CVar_Register(const char *name, const char *desc, int flags,
                                const char *default_val,
                                openwow::ui::game::CVarValidationCallback callback, int p6,
                                int , int , int ) {
  if (!name || *name == '\0' || !default_val) {
    return nullptr;
  }

  auto *stub = FindClientMiscCVarStub(name);
  if (!stub) {
    return nullptr;
  }

  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  const auto ui_flags =
      static_cast<openwow::ui::game::CVarFlags>(static_cast<std::uint32_t>(flags));

  cvars.RegisterNativeCVar(name, default_val, ui_flags, desc ? desc : "",
                           std::move(callback), 0.0f, 0.0f, p6);

  stub->intValue = cvars.GetCVarInt(name);
  return stub;
}

static bool AsyncIO_ValidateThreadSleepMax100(const std::string &, const std::string &,
                                              const std::string &new_value) {
  return ParseStormSignedDecimalPrefix(new_value) <= 100u;
}

static bool AsyncIO_ValidateHandlerTimeout20To250(const std::string &, const std::string &,
                                                  const std::string &new_value) {
  return ParseStormSignedDecimalPrefix(new_value) - 20u <= 0xE6u;
}

static void ConsoleLog(const char *fmt, ...) {
  (void)fmt;

}

static void Console_UnregisterCommand(const char *name) {
  UnregisterDebugConsoleCommand(name);
}

static int GetCurrentTimingMethod() {
  return static_cast<int>(GameClock::Instance().GetTimingMethod());
}

static const char *GetTimingMethodName(int method) {
  return TimingMethodNameFromIdaValue(method).data();
}

struct RealmInfo_Stub {
  char padding[6];
  char name[256];
  char type[256];
};

static RealmInfo_Stub s_realmInfo = {};

static RealmInfo_Stub *GetRealmInfo() {
  return &s_realmInfo;
}

static const char *GetLocalizedString(const char * , int , int ) {
  return "";
}

std::string BuildCrashLocalZoneName() {
  const auto &zone_text = openwow::game::ZoneTextSystem::Get();
  const std::string subzone_name = zone_text.GetCurrentSubZoneName();
  const std::string zone_name = zone_text.GetCurrentZoneName();

  if (!subzone_name.empty()) {
    if (!zone_name.empty() && subzone_name != zone_name) {
      return subzone_name + ", " + zone_name;
    }
    return subzone_name;
  }

  return zone_name;
}

void fn_delete_array(void **thisPtr) {

  uint32_t count = reinterpret_cast<uintptr_t>(thisPtr[8]);
  for (uint32_t i = 0; i < count; ++i) {

    void *element = thisPtr[i];
    uint32_t *vtable = *reinterpret_cast<uint32_t **>(&element);
    auto destructor = reinterpret_cast<void (*)(void *)>(vtable[1]);
    destructor(element);

    void *ptr = thisPtr[i];
    if (ptr) {
      SMemFree(ptr, "delete", -1, 0);
    }
  }
}

void MoveLogFile_ref() {

  detail::ExecuteMoveLogFile(MutableMoveLogFileDependencies());
}

void detail::ExecuteMoveLogFile(const MoveLogFileDependencies &deps) {
  assert(deps.read_registry_string);
  assert(deps.write_registry_string);
  assert(deps.get_process_id);
  assert(deps.initialize_movement_runtime);
  assert(deps.schedule_periodic_update);

  char path[260];
  std::memset(path, 0, sizeof(path));

  if (!deps.read_registry_string("World of Warcraft\\Client", "MoveLogFile", 0, path, 0x104) ||
      !path[0]) {
    deps.write_registry_string("World of Warcraft\\Client", "MoveLogFile", 0, "ClientMovement.txt");
    SStrCopy(path, "ClientMovement.txt", 260);
  }

  const std::uint32_t pid = deps.get_process_id();
  char *dot = SStrChr(path, '.');
  if (dot) {
    int remaining = static_cast<int>(dot - path + 260);
    SStrPrintf(dot, remaining, "%04d.txt", pid);
  }

  deps.initialize_movement_runtime(path);
  deps.schedule_periodic_update(2.0f);
}

int detail::ExecuteAsyncIORegisterCVars(const AsyncIORegisterDependencies &deps) {
  assert(deps.initialize_async_io);

  dword_B2FA04 = reinterpret_cast<uint32_t *>(
      CVar_Register("asyncThreadSleep", "Engine option: Async read thread sleep", 1, "0",
                    AsyncIO_ValidateThreadSleepMax100, 0, 0, 0, 0));
  dword_B2FA08 = reinterpret_cast<uint32_t *>(
      CVar_Register("asyncHandlerTimeout", "Engine option: Async read main thread timeout", 1,
                    "100", AsyncIO_ValidateHandlerTimeout20To250, 0, 0, 0, 0));

  auto *cvar_sleep = reinterpret_cast<CVar_Stub *>(dword_B2FA04);
  auto *cvar_timeout = reinterpret_cast<CVar_Stub *>(dword_B2FA08);
  return deps.initialize_async_io(static_cast<std::uint32_t>(cvar_sleep->intValue),
                                  static_cast<std::uint32_t>(cvar_timeout->intValue));
}

void detail::SetAsyncIORegisterDependenciesForTests(AsyncIORegisterDependencies deps) {
  MutableAsyncIORegisterDependencies() = std::move(deps);
}

void detail::ResetAsyncIORegisterDependenciesForTests() {
  MutableAsyncIORegisterDependencies() = MakeAsyncIORegisterDependencies();
}

void detail::SetMoveLogFileDependenciesForTests(MoveLogFileDependencies deps) {
  MutableMoveLogFileDependencies() = std::move(deps);
}

void detail::ResetMoveLogFileDependenciesForTests() {
  MutableMoveLogFileDependencies() = MakeMoveLogFileDependencies();
}

void detail::InitializeMovementRuntimeForTests(const char *resolved_path) {
  InitializeMovementRuntime(resolved_path);
}

int detail::DispatchMovementRuntimePeriodicUpdateForTests() {
  return MovementRuntime_PeriodicUpdateEvent(nullptr, 0);
}

detail::MovementRuntimeSnapshot detail::GetMovementRuntimeSnapshotForTests() {
  auto &state = MutableMovementRuntimeState();
  std::lock_guard lock(state.mutex);

  detail::MovementRuntimeSnapshot snapshot;
  snapshot.configured_log_path = state.configured_log_path;
  snapshot.has_log_file_handle = state.log_file_handle != nullptr;
  snapshot.runtime_flags = state.runtime_flags;
  snapshot.previous_update_tick_ms = state.previous_update_tick_ms;
  snapshot.current_update_tick_ms = state.current_update_tick_ms;
  snapshot.update_count = state.update_count;
  snapshot.current_transport_context = state.current_transport_context;
  snapshot.previous_transport_context = state.previous_transport_context;
  return snapshot;
}

void detail::SetMovementRuntimeTickCountProviderForTests(
    detail::MovementRuntimeTickCountProvider provider) {
  MutableMovementRuntimeTickCountProvider() = provider;
}

void detail::ResetMovementRuntimeTickCountProviderForTests() {
  MutableMovementRuntimeTickCountProvider() = nullptr;
}

void detail::CloseMovementRuntimeLogFileForTests() {
  CloseMovementRuntimeLogFile();
}

void detail::SetMovementRuntimeLogFileHandleForTests(std::FILE *handle) {
  auto &state = MutableMovementRuntimeState();
  std::lock_guard lock(state.mutex);
  state.log_file_handle = handle;
}

int detail::ExecuteGameCleanup(const GameCleanupDependencies &deps) {
  assert(deps.shutdown_world_audio);
  assert(deps.clear_declined_words);
  assert(deps.shutdown_spell_visuals);
  assert(deps.shutdown_chat_log);
  assert(deps.shutdown_login);
  assert(deps.shutdown_addon_data);
  assert(deps.shutdown_auxiliary_lookup);
  assert(deps.shutdown_dance_studio);
  assert(deps.unregister_query_opcodes);
  assert(deps.destroy_db_cache);
  assert(deps.reserved_cleanup);
  assert(deps.shutdown_character_components);
  assert(deps.clear_virtual_frames);
  assert(deps.shutdown_framexml_runtime);
  assert(deps.shutdown_voice_chat);
  assert(deps.shutdown_object_effect_data_store);
  assert(deps.shutdown_sound_system);
  assert(deps.shutdown_frame_script);
  assert(deps.shutdown_sound_engine_data);
  assert(deps.cleanup_render_targets);
  assert(deps.cleanup_render_bootstrap);
  assert(deps.unregister_console_command);
  assert(deps.shutdown_combat_data);
  assert(deps.clear_cleanup_flag);

  deps.shutdown_world_audio();
  deps.clear_declined_words();
  deps.shutdown_spell_visuals();
  deps.shutdown_chat_log();
  deps.shutdown_login();
  deps.shutdown_addon_data();
  deps.shutdown_auxiliary_lookup();
  deps.shutdown_dance_studio();
  deps.unregister_query_opcodes();
  deps.destroy_db_cache();
  deps.reserved_cleanup();
  deps.shutdown_character_components();
  deps.clear_virtual_frames();
  deps.shutdown_framexml_runtime();
  deps.shutdown_voice_chat();
  deps.shutdown_object_effect_data_store();
  deps.shutdown_sound_system(0);
  deps.shutdown_frame_script();
  deps.shutdown_sound_engine_data();
  deps.cleanup_render_targets();
  deps.cleanup_render_bootstrap();
  deps.unregister_console_command("reloadUI");
  deps.unregister_console_command("perf");
  deps.unregister_console_command("timingInfo");
  const int result = deps.shutdown_combat_data();
  deps.clear_cleanup_flag();
  return result;
}

void detail::SetGameCleanupDependenciesForTests(GameCleanupDependencies deps) {
  MutableGameCleanupDependencies() = std::move(deps);
}

void detail::ResetGameCleanupDependenciesForTests() {
  MutableGameCleanupDependencies() = MakeGameCleanupDependencies();
}

std::uint32_t detail::GetGameCleanupFlagForTests() {
  return dword_AC3228;
}

void detail::SetGameCleanupFlagForTests(std::uint32_t value) {
  dword_AC3228 = value;
}

int AsyncIO_RegisterCVars() {

  return detail::ExecuteAsyncIORegisterCVars(MutableAsyncIORegisterDependencies());
}

int GameCleanup() {

  return detail::ExecuteGameCleanup(MutableGameCleanupDependencies());
}

int fn_timingMethod() {

  int desired = CVar_LookupByName("timingMethod")->intValue;
  int selected = GetCurrentTimingMethod();
  int error = CVar_LookupByName("timingTestError")->intValue;

  const char *desired_name = GetTimingMethodName(desired);
  ConsoleLog("Timing method desired: %d - %s", desired, desired_name);

  const char *selected_name = GetTimingMethodName(selected);
  ConsoleLog("Timing method selected: %d - %s", selected, selected_name);

  ConsoleLog("Timing test error: %d", error);
  return 1;
}

int CompareFunction(const char **a1, const char **a2) {

  return SStrCmpNoCaseCollate(*a1, *a2, 0x7FFFFFFF);
}

char *AppendRealmInfoToCrashDump(char *buf, int buf_size) {

  RealmInfo_Stub *info = GetRealmInfo();
  if (info && info->name[0] && info->type[0]) {
    SStrCat(buf, "Realm: ", buf_size);
    SStrCat(buf, info->name, buf_size);
    SStrCat(buf, " [", buf_size);
    SStrCat(buf, info->type, buf_size);
    SStrCat(buf, "]", buf_size);
  } else {
    SStrCat(buf, "Realm: ???", buf_size);
  }
  return SStrCat(buf, "\r\n", buf_size);
}

int detail::ExecuteLaunchWowError(const LaunchWowErrorDependencies &deps) {
  char log_path[260];
  if (deps.get_last_log_path && deps.get_last_log_path(log_path, 260)) {
    char cmd[520];
    SStrPrintf(cmd, 0x208, "%s %s", "WowError.exe", log_path);
    if (deps.spawn_process) {
      deps.spawn_process("WowError.exe", cmd, 0, 0);
    }
  }
  return 1;
}

int LaunchWowError(int , int , int , int , int ) {

  return detail::ExecuteLaunchWowError({
      .get_last_log_path = SErrGetLastLogPath,
      .spawn_process = SThread_SpawnProcess,
  });
}

char *AppendLocalZoneInfoToCrashDump(void *obj, char *buf,
                                     int buf_size) {

  (void)obj;
  SStrCat(buf, "Local Zone: ", buf_size);
  const std::string zone_name = BuildCrashLocalZoneName();
  if (!zone_name.empty()) {
    SStrCat(buf, zone_name.c_str(), buf_size);
  }
  return SStrCat(buf, "\r\n", buf_size);
}

void SetRetailDebugCommandBindings(RetailDebugCommandBindings bindings) {
  auto &state = MutableRetailDebugCommandState();
  {
    std::scoped_lock lock(state.mutex);
    state.bindings = std::move(bindings);
  }
}

std::optional<RetailDebugActivePlayerState> GetRetailDebugActivePlayerState() {
  const RetailDebugCommandBindings bindings = GetRetailDebugCommandBindingsSnapshot();
  if (!bindings.get_active_player_state) {
    return std::nullopt;
  }
  return bindings.get_active_player_state();
}

std::optional<RetailDebugObjectManagerStatus> GetRetailDebugObjectManagerStatus() {
  const RetailDebugCommandBindings bindings = GetRetailDebugCommandBindingsSnapshot();
  if (!bindings.get_object_manager_status) {
    return std::nullopt;
  }
  return bindings.get_object_manager_status();
}

const openwow::data::dbc::DbcLoader *GetRetailDebugDbcLoader() {
  const RetailDebugCommandBindings bindings = GetRetailDebugCommandBindingsSnapshot();
  return bindings.get_dbc_loader ? bindings.get_dbc_loader() : nullptr;
}

void Console_RegisterDebugCommands() {
  openwow::debug::DebugConsole::Get().RegisterRawCommand(
      "worldport", "Port to a specific map/coordinate",
      [](std::string_view raw_args) -> std::string {
        ExecuteWorldportCommand(raw_args);
        return {};
      });
}

void Console_UnregisterDebugCommands() {

  static constexpr const char *kDebugCommands[] = {
      "port",       "charmport", "worldport", "setrawpos",
      "showplayer", "togglehelm", "togglecloak",
  };

  for (const char *command : kDebugCommands) {
    Console_UnregisterCommand(command);
  }
}

LoadingScreenWorldBackgroundGeometry LoadingScreen_InitWorldBackgroundQuads() {
  constexpr std::uint32_t kTileWidth = 256;
  constexpr std::uint32_t kTileHeight = 256;
  constexpr std::uint32_t kTilesPerRow = 4;
  constexpr std::uint32_t kTileRows = 3;
  constexpr std::uint32_t kViewportWidth = 1002;
  constexpr std::uint32_t kViewportHeight = 668;

  LoadingScreenWorldBackgroundGeometry geometry{};

  for (std::uint32_t row = 0; row < kTileRows; ++row) {
    const std::uint32_t tile_top_px = row * kTileHeight;
    const std::uint32_t tile_bottom_px = std::min(tile_top_px + kTileHeight, kViewportHeight);

    for (std::uint32_t column = 0; column < kTilesPerRow; ++column) {
      const std::uint32_t tile_left_px = column * kTileWidth;
      const std::uint32_t tile_right_px = std::min(tile_left_px + kTileWidth, kViewportWidth);

      const float left = static_cast<float>(tile_left_px) / static_cast<float>(kViewportWidth);
      const float right = static_cast<float>(tile_right_px) / static_cast<float>(kViewportWidth);
      const float bottom =
          1.0f - static_cast<float>(tile_bottom_px) / static_cast<float>(kViewportHeight);
      const float top =
          1.0f - static_cast<float>(tile_top_px) / static_cast<float>(kViewportHeight);
      const float u_max =
          static_cast<float>(tile_right_px - tile_left_px) / static_cast<float>(kTileWidth);
      const float v_max =
          static_cast<float>(tile_bottom_px - tile_top_px) / static_cast<float>(kTileHeight);

      const std::size_t base =
          (row * kTilesPerRow + column) * kLoadingScreenWorldBackgroundVerticesPerTile;

      geometry.positions[base + 0] = {left, bottom, 0.0f};
      geometry.positions[base + 1] = {right, bottom, 0.0f};
      geometry.positions[base + 2] = {left, top, 0.0f};
      geometry.positions[base + 3] = {right, top, 0.0f};

      geometry.texcoords[base + 0] = {0.0f, v_max};
      geometry.texcoords[base + 1] = {u_max, v_max};
      geometry.texcoords[base + 2] = {0.0f, 0.0f};
      geometry.texcoords[base + 3] = {u_max, 0.0f};
    }
  }

  return geometry;
}

void InitGameSubsystems_InitializeUiShaders(const openwow::data::dbc::DbcLoader &dbc_loader) {
  auto &state = MutableUiShaderInitState();
  const auto taxi_path_group_count =
      dbc_loader.taxi_path().empty() ? 0u : dbc_loader.taxi_path().max_id() + 1u;

  if (const auto prewarm_ui_program = MutableUiShaderInitDependencies().prewarm_ui_program) {
    (void)prewarm_ui_program();
  }

  state.taxi_path_nodes = dbc_loader.taxi_path_node().entries();
  state.loading_screen_taxi_splines =
      dbc_loader.loading_screen_taxi_splines().entries();
  state.world_map_continents = dbc_loader.world_map_continent().entries();
  (void)LoadingScreen_BuildTaxiPathCatalogFromTaxiPathNodes(
      state.taxi_path_catalog_storage,
      std::span<const openwow::data::dbc::TaxiPathNodeEntry>(
          dbc_loader.taxi_path_node().entries().data(),
          dbc_loader.taxi_path_node().entries().size()),
      taxi_path_group_count);
  state.taxi_path_catalog = state.taxi_path_catalog_storage.AsCatalog();
  state.world_background_geometry = LoadingScreen_InitWorldBackgroundQuads();
  state.initialized = true;
}

const LoadingScreenElementCatalog *GetLoadingScreenTaxiPathCatalog() {
  auto &state = MutableUiShaderInitState();
  return state.initialized ? &state.taxi_path_catalog : nullptr;
}

const LoadingScreenWorldBackgroundGeometry &GetLoadingScreenWorldBackgroundGeometry() {
  return MutableUiShaderInitState().world_background_geometry;
}

bool detail::BuildLoadingScreenMapChangeOverlayFromData(
    LoadingScreenDynamicMapChangeAssets *assets, const LoadingScreenElementCatalog *catalog,
    std::span<const openwow::data::dbc::TaxiPathNodeEntry> taxi_path_nodes,
    std::span<const openwow::data::dbc::LoadingScreenTaxiSplinesEntry>
        loading_screen_taxi_splines,
    std::span<const openwow::data::dbc::WorldMapContinentEntry> world_map_continents,
    std::uint32_t path_segment_index, std::uint32_t loading_path_id) {
  if (!assets || !catalog || !catalog->groups || loading_path_id >= catalog->group_count) {
    return false;
  }

  const auto &group = catalog->groups[loading_path_id];
  if (group.segment_count == 0 || !group.segment_indices) {
    return false;
  }

  const auto spline_record = std::find_if(
      loading_screen_taxi_splines.begin(), loading_screen_taxi_splines.end(),
      [loading_path_id, path_segment_index](const auto &entry) {
        return entry.taxi_path_id == loading_path_id &&
               entry.leg_index == path_segment_index;
      });
  if (spline_record == loading_screen_taxi_splines.end()) {
    return false;
  }

  std::int32_t unused_segment_start = 0;
  std::int32_t current_segment_end = 0;
  if (!LoadingScreen_TryGetTaxiPathInfo(catalog, loading_path_id, path_segment_index,
                                        &unused_segment_start, &current_segment_end)) {
    return false;
  }

  const auto next_segment_index = (path_segment_index + 1u) % group.segment_count;
  std::int32_t next_segment_start = 0;
  std::int32_t unused_next_segment_end = 0;
  if (!LoadingScreen_TryGetTaxiPathInfo(catalog, loading_path_id, next_segment_index,
                                        &next_segment_start, &unused_next_segment_end)) {
    return false;
  }

  const auto *current_end_node = FindTaxiPathNodeById(taxi_path_nodes, current_segment_end);
  const auto *next_start_node = FindTaxiPathNodeById(taxi_path_nodes, next_segment_start);
  if (!current_end_node || !next_start_node) {
    return false;
  }

  LoadingScreenMapPoint current_end_point;
  LoadingScreenMapPoint next_start_point;
  if (!ProjectTaxiNodeToLoadingScreen(*current_end_node, world_map_continents,
                                      &current_end_point) ||
      !ProjectTaxiNodeToLoadingScreen(*next_start_node, world_map_continents, &next_start_point)) {
    return false;
  }

  std::vector<render::C3Vector> control_points;
  control_points.reserve(kLoadingScreenMaxIntermediatePoints + 4u);
  control_points.push_back({current_end_point.x, current_end_point.y, 0.0f});
  control_points.push_back({current_end_point.x, current_end_point.y, 0.0f});

  for (std::size_t index = 0; index < kLoadingScreenMaxIntermediatePoints; ++index) {
    const float x = spline_record->loc_x[index];
    const float y = spline_record->loc_y[index];
    if (x <= 0.0f && y <= 0.0f) {
      break;
    }
    control_points.push_back({x, y, 0.0f});
  }

  control_points.push_back({next_start_point.x, next_start_point.y, 0.0f});
  control_points.push_back({next_start_point.x, next_start_point.y, 0.0f});

  render::CSpline spline{render::CSpline::CurveType::kCatmullRom};
  spline.SetControlPoints(control_points);

  auto sample_count = static_cast<int>(spline.GetTotalLength() * kLoadingScreenSplineSampleScale);
  if (sample_count < 1) {
    sample_count = 1;
  }

  auto &overlay_vertices = assets->overlay_vertices;
  LoadingScreen_DynamicOverlayVertices_EnsureCount(
      &overlay_vertices, 4u * static_cast<std::uint32_t>(sample_count) + 12u);
  if (!overlay_vertices.vertices) {
    return false;
  }

  WriteLoadingScreenEndpointMarkers(overlay_vertices.vertices, current_end_point, next_start_point);
  WriteLoadingScreenRibbonQuads(overlay_vertices.vertices + 8, spline,
                                static_cast<std::uint32_t>(sample_count));
  return true;
}

void detail::SetUiShaderInitDependenciesForTests(UiShaderInitDependencies deps) {
  MutableUiShaderInitDependencies() = deps;
}

void detail::ResetUiShaderInitDependenciesForTests() {
  MutableUiShaderInitDependencies() = UiShaderInitDependencies{
      .prewarm_ui_program = &openwow::render::ui::PrewarmUiProgram,
  };
}

void detail::ResetUiShaderInitStateForTests() {
  MutableUiShaderInitState() = UiShaderInitState{};
}

bool LoadingScreen_BuildMapChangeOverlay(std::uint32_t path_segment_index,
                                         std::uint32_t loading_path_id) {
  auto &state = MutableUiShaderInitState();
  if (!state.initialized) {
    return false;
  }

  return detail::BuildLoadingScreenMapChangeOverlayFromData(
      &MutableLoadingScreenDynamicMapChangeAssets(), &state.taxi_path_catalog,
      state.taxi_path_nodes, state.loading_screen_taxi_splines,
      state.world_map_continents, path_segment_index, loading_path_id);
}

bool LoadingScreen_BuildMapChangeOverlayForPreviousMap(
    const std::uint32_t loading_path_id, const std::uint32_t previous_map_id) {
  auto &state = MutableUiShaderInitState();
  if (!state.initialized || loading_path_id >= state.taxi_path_catalog.group_count ||
      !state.taxi_path_catalog.groups) {
    return false;
  }

  const auto &group = state.taxi_path_catalog.groups[loading_path_id];
  if (group.segment_count == 0 || !group.segment_indices) {
    return false;
  }

  for (std::uint32_t segment_index = 0; segment_index < group.segment_count;
       ++segment_index) {
    std::int32_t start_node_id = 0;
    std::int32_t unused_end_node_id = 0;
    if (!LoadingScreen_TryGetTaxiPathInfo(&state.taxi_path_catalog, loading_path_id,
                                          segment_index, &start_node_id,
                                          &unused_end_node_id)) {
      return false;
    }

    const auto *start_node = FindTaxiPathNodeById(state.taxi_path_nodes, start_node_id);
    if (!start_node) {
      return false;
    }
    if (start_node->map_id == previous_map_id) {
      return LoadingScreen_BuildMapChangeOverlay(segment_index, loading_path_id);
    }
  }

  return false;
}

namespace {

int IgnoreStormInitEvent(void * , int ) {
  return 0;
}

int LegacyStormInitIgnoreCallback() {
  static const int callback = EvtSched_RegisterLegacyCallback(
      reinterpret_cast<std::intptr_t>(&IgnoreStormInitEvent));
  return callback;
}

int LegacyStormInitStoreCallback() {
  static const int callback = EvtSched_RegisterLegacyCallback(
      reinterpret_cast<std::intptr_t>(&Storm_StoreInitParams));
  return callback;
}

void RegisterStormInitHandler(std::uint32_t event_type, int callback) {
  EvtContext_RegisterCurrentHandler(event_type, callback, 0, 8.0f);
}

void UnregisterStormInitHandler(std::uint32_t event_type, int callback) {
  (void)EvtContext_UnregisterCurrentHandler(event_type, callback);
}

}

int Storm_StoreInitParams(void *event_data, int ) {

  if (event_data == nullptr) {
    return 0;
  }

  StormInitParamsBlob params{};
  std::memcpy(params.words.data(), event_data, sizeof(params.words));

  auto &state = MutableLoadingScreenStormInitState();
  std::scoped_lock lock(state.mutex);
  state.cached_params = params;
  state.has_pending_params = true;
  return 0;
}

void LoadingScreen_RegisterStormInitHandlers() {

  const int ignore_callback = LegacyStormInitIgnoreCallback();
  const int store_callback = LegacyStormInitStoreCallback();
  RegisterStormInitHandler(1u, ignore_callback);
  RegisterStormInitHandler(0x22u, ignore_callback);
  RegisterStormInitHandler(9u, ignore_callback);
  RegisterStormInitHandler(0xBu, ignore_callback);
  RegisterStormInitHandler(0xCu, ignore_callback);
  RegisterStormInitHandler(0xDu, ignore_callback);
  RegisterStormInitHandler(0x23u, store_callback);

  auto &state = MutableLoadingScreenStormInitState();
  std::scoped_lock lock(state.mutex);
  state.has_pending_params = false;
}

bool Storm_ClearInitHandlersAndReplayCachedParams() {

  StormInitParamsBlob cached_params{};
  const int ignore_callback = LegacyStormInitIgnoreCallback();
  const int store_callback = LegacyStormInitStoreCallback();

  UnregisterStormInitHandler(1u, ignore_callback);
  UnregisterStormInitHandler(0x22u, ignore_callback);
  UnregisterStormInitHandler(9u, ignore_callback);
  UnregisterStormInitHandler(0xBu, ignore_callback);
  UnregisterStormInitHandler(0xCu, ignore_callback);
  UnregisterStormInitHandler(0xDu, ignore_callback);
  UnregisterStormInitHandler(0x23u, store_callback);

  {
    auto &state = MutableLoadingScreenStormInitState();
    std::scoped_lock lock(state.mutex);
    if (!state.has_pending_params) {
      return false;
    }

    cached_params = state.cached_params;
  }

  return EvtContext_PostEventPayload(0, 0x23u, cached_params.words.data(),
                                     sizeof(cached_params.words));
}

const char *LoadingScreen_SetTextSource(const char *text) {
  openwow::screens::LoadingScreenManager::Get().SetTextSource(text);
  return text;
}

const char *fn_TRIAL_LOADING_MESSAGE(char enable) {

  const char *msg = GetLocalizedString("TRIAL_LOADING_MESSAGE", -1, 0);
  auto &loading_screen = openwow::screens::LoadingScreenManager::Get();

  flt_B2FEDC = 0.0f;
  byte_B2FED9 = enable;
  if (enable) {

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-security"
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
    SStrPrintf(byte_B302F0, 0x400, msg);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    loading_screen.SetTrialLoadingMessage(true);
    return LoadingScreen_SetTextSource(byte_B302F0);
  }
  loading_screen.SetTrialLoadingMessage(false);
  return msg;
}

bool LoadingScreen_HasRenderLayer() {

  return openwow::screens::LoadingScreenManager::Get().IsVisible();
}

int GxDrawEnabled() {
  return openwow::render::IsRendererContextActive() ? 1 : 0;
}

int GxRenderEnabled() {
  return openwow::render::IsRendererContextActive() ? 1 : 0;
}

void DynamicElementVert_Resize(void **thisPtr,
                               uint32_t new_count) {

  ResizeDynamicElementBuffer(LegacyResizableBufferView(thisPtr), new_count,
                             sizeof(DynamicElementVert), "au:DynamicElemen");
}

void LoadingScreenTaxiPathInfo_Resize(void **thisPtr,
                                      uint32_t new_count) {

  ResizeDynamicElementBuffer(LegacyResizableBufferView(thisPtr), new_count,
                             sizeof(LoadingScreenTaxiPathInfo), "au:LoadingScreen");
}

void LoadingScreen_DynamicOverlayVertices_EnsureCount(LoadingScreenDynamicOverlayVertices *thisPtr,
                                                      std::uint32_t new_count) {

  const auto old_count = thisPtr->count;
  if (new_count <= old_count) {
    thisPtr->count = new_count;
    return;
  }

  if (new_count > thisPtr->capacity) {
    auto growth_quantum = thisPtr->growth_quantum;
    if (growth_quantum == 0) {
      growth_quantum = ResolveDynamicOverlayGrowQuantum(thisPtr, new_count);
    }

    std::uint32_t rounded_capacity = new_count;
    const auto remainder = new_count % growth_quantum;
    if (remainder != 0) {
      rounded_capacity = new_count + growth_quantum - remainder;
    }

    DynamicElementVert_Resize(reinterpret_cast<void **>(thisPtr), rounded_capacity);
  }

  auto zeroed_count = old_count;
  if (new_count - zeroed_count >= 4) {
    auto block_offset = zeroed_count;
    auto block_count = ((new_count - zeroed_count - 4) >> 2) + 1;
    zeroed_count += 4 * block_count;
    while (block_count-- > 0) {
      ZeroDynamicElementVert(thisPtr->vertices, block_offset + 0);
      ZeroDynamicElementVert(thisPtr->vertices, block_offset + 1);
      ZeroDynamicElementVert(thisPtr->vertices, block_offset + 2);
      ZeroDynamicElementVert(thisPtr->vertices, block_offset + 3);
      block_offset += 4;
    }
  }

  while (zeroed_count < new_count) {
    ZeroDynamicElementVert(thisPtr->vertices, zeroed_count);
    ++zeroed_count;
  }

  thisPtr->count = new_count;
}

LoadingScreenDynamicMapChangeAssets &LoadingScreen_GetDynamicMapChangeAssets() {
  return MutableLoadingScreenDynamicMapChangeAssets();
}

void LoadingScreen_CleanupDynamicMapChangeAssets() {

  auto &assets = MutableLoadingScreenDynamicMapChangeAssets();

  assets.dynamic_elements_loaded = false;
  assets.world_tile_texture_count = 0;

  auto &overlay_vertices = assets.overlay_vertices;
  if (overlay_vertices.vertices) {
    SMemFree(overlay_vertices.vertices, "au:DynamicElemen", -2, 0);
  }

  overlay_vertices.capacity = 0;
  overlay_vertices.count = 0;
  overlay_vertices.vertices = nullptr;
}

void LoadingScreen_CleanupResources(openwow::audio::SoundRuntime& sound_runtime) {

  openwow::screens::LoadingScreenManager::Get().Hide();
  (void)Storm_ClearInitHandlersAndReplayCachedParams();
  LoadingScreen_CleanupDynamicMapChangeAssets();
  LoadingScreen_SetTextSource(nullptr);
  sound_runtime.ResetZoneAndScriptMusicRuntime();
  sound_runtime.ResetZoneAmbienceRuntime();

  if (!openwow::data::IsStreamingInitialized()) {
    return;
  }

  const auto *dbc_loader = GetRetailDebugDbcLoader();
  if (dbc_loader == nullptr) {
    return;
  }

  PreloadKnownSpellVisualStreamingResources(*dbc_loader);
}

namespace {

void EmitLoadingScreenTaxiPathInfo(LoadingScreenElementCatalogStorage &catalog,
                                   std::uint32_t group_index, std::int32_t start_element_id,
                                   std::int32_t end_element_id) {
  assert(group_index < catalog.groups.size());

  auto &group = catalog.groups[group_index];
  group.segment_indices.push_back(static_cast<std::uint32_t>(catalog.taxi_path_infos.size()));
  catalog.taxi_path_infos.push_back(LoadingScreenTaxiPathInfo{start_element_id, end_element_id});
}

}

void LoadingScreenElementCatalogStorage::Reset(std::uint32_t group_count) {
  groups.clear();
  groups.resize(group_count);
  taxi_path_infos.clear();
  group_views_.clear();
}

LoadingScreenElementCatalog LoadingScreenElementCatalogStorage::AsCatalog() const {
  group_views_.resize(groups.size());
  for (std::size_t i = 0; i < groups.size(); ++i) {
    const auto &group = groups[i];
    group_views_[i].reserved_0 = 0;
    group_views_[i].segment_count = static_cast<std::uint32_t>(group.segment_indices.size());
    group_views_[i].segment_indices =
        group.segment_indices.empty() ? nullptr : group.segment_indices.data();
    group_views_[i].reserved_c = 0;
  }

  LoadingScreenElementCatalog catalog{};
  catalog.group_count = static_cast<std::uint32_t>(group_views_.size());
  catalog.groups = group_views_.empty() ? nullptr : group_views_.data();
  catalog.taxi_path_infos = taxi_path_infos.empty() ? nullptr : taxi_path_infos.data();
  return catalog;
}

std::uint32_t LoadingScreen_BuildTaxiPathCatalog(
    LoadingScreenElementCatalogStorage &catalog,
    std::span<const LoadingScreenCatalogSourceElement> source_elements,
    std::uint32_t group_count) {

  catalog.Reset(group_count);

  const LoadingScreenCatalogSourceElement *previous = nullptr;
  std::int32_t pending_start_element_id = 0;
  std::int32_t pending_end_element_id = 0;

  for (const auto &current : source_elements) {
    if (previous && previous->group_index != current.group_index) {
      if (pending_start_element_id > 0) {
        EmitLoadingScreenTaxiPathInfo(catalog, previous->group_index, pending_start_element_id,
                                      pending_end_element_id);
      }
      pending_start_element_id = 0;
    }

    if ((current.flags & 0x1u) != 0) {
      EmitLoadingScreenTaxiPathInfo(catalog, current.group_index, pending_start_element_id,
                                    pending_end_element_id);
      pending_start_element_id = 0;
    } else {
      if (previous && previous->group_index == current.group_index &&
          previous->boundary_key != current.boundary_key) {
        EmitLoadingScreenTaxiPathInfo(catalog, previous->group_index, pending_start_element_id,
                                      pending_end_element_id);
        pending_start_element_id = 0;
      }

      if ((current.flags & 0x2u) != 0) {
        pending_end_element_id = current.element_id;
        if (pending_start_element_id == 0) {
          pending_start_element_id = current.element_id;
        }
      }
    }

    previous = &current;
  }

  return static_cast<std::uint32_t>(source_elements.size());
}

std::uint32_t LoadingScreen_BuildTaxiPathCatalogFromTaxiPathNodes(
    LoadingScreenElementCatalogStorage &catalog,
    std::span<const openwow::data::dbc::TaxiPathNodeEntry> taxi_path_nodes,
    std::uint32_t group_count) {
  std::vector<LoadingScreenCatalogSourceElement> source_elements;
  source_elements.reserve(taxi_path_nodes.size());

  for (const auto &node : taxi_path_nodes) {
    source_elements.push_back({
        .element_id = static_cast<std::int32_t>(node.id),
        .group_index = node.path_id,
        .boundary_key = static_cast<std::int32_t>(node.map_id),
        .flags = node.flags,
    });
  }

  return LoadingScreen_BuildTaxiPathCatalog(catalog, source_elements, group_count);
}

bool LoadingScreen_TryGetTaxiPathInfo(const LoadingScreenElementCatalog *thisPtr,
                                      std::uint32_t group_idx, std::uint32_t segment_idx,
                                      std::int32_t *out_start_element_id,
                                      std::int32_t *out_end_element_id) {

  if (!thisPtr || group_idx >= thisPtr->group_count || !thisPtr->groups) {
    return false;
  }

  const auto &group = thisPtr->groups[group_idx];
  if (segment_idx >= group.segment_count || !group.segment_indices || !thisPtr->taxi_path_infos) {
    return false;
  }

  const auto pair_index = group.segment_indices[segment_idx];
  const auto &pair = thisPtr->taxi_path_infos[pair_index];
  if (pair.start_element_id <= 0 || pair.end_element_id <= 0) {
    return false;
  }

  *out_start_element_id = pair.start_element_id;
  *out_end_element_id = pair.end_element_id;
  return true;
}

int LoadingScreen_FindElementByTexture(const LoadingScreenElementCatalog *thisPtr, void *context,
                                       std::uint32_t group_idx,
                                       int texture_id) {

  if (!thisPtr || group_idx >= thisPtr->group_count || !thisPtr->groups || !context) {
    return -1;
  }

  const auto &group = thisPtr->groups[group_idx];
  if (group.segment_count == 0) {
    return -1;
  }

  const auto context_addr = reinterpret_cast<std::uintptr_t>(context);
  auto *resolver = reinterpret_cast<std::uintptr_t *>(context_addr + 24);
  auto *resolver_vtable = reinterpret_cast<const std::uintptr_t *>(*resolver);
  auto resolve_element = reinterpret_cast<int *(*)(std::uintptr_t *, int)>(resolver_vtable[1]);

  for (std::uint32_t i = 0; i < group.segment_count; ++i) {
    std::int32_t start_element_id = 0;
    std::int32_t end_element_id = 0;
    if (!LoadingScreen_TryGetTaxiPathInfo(thisPtr, group_idx, i, &start_element_id,
                                          &end_element_id)) {
      break;
    }

    auto *tex = resolve_element(resolver, start_element_id);
    if (tex && tex[3] == texture_id) {
      return static_cast<int>(i);
    }
  }

  return -1;
}

void C3VectorNTempest_Destructor(C3VectorNTempestOwnedBuffers *thisPtr) {

  if (thisPtr->slot_109_buffer) {
    SMemFree(thisPtr->slot_109_buffer, "m", -2, 0);
  }

  if (thisPtr->slot_79_buffer) {
    SMemFree(thisPtr->slot_79_buffer, "au:C3VectorNTem", -2, 0);
  }
}

void GxRenderTarget_Cleanup() {

}

}

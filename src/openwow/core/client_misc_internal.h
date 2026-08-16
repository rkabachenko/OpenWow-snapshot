#pragma once

#include "openwow/core/client_misc.h"
#include "openwow/data/formats/dbc/dbc_entries_world.h"
#include "openwow/data/formats/dbc/dbc_structures.h"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <span>

namespace openwow::core::detail {

struct AsyncIORegisterDependencies {
  std::function<int(std::uint32_t thread_sleep, std::uint32_t handler_timeout)> initialize_async_io;
};

int ExecuteAsyncIORegisterCVars(const AsyncIORegisterDependencies &deps);
void SetAsyncIORegisterDependenciesForTests(AsyncIORegisterDependencies deps);
void ResetAsyncIORegisterDependenciesForTests();

struct MoveLogFileDependencies {
  std::function<bool(const char *key, const char *value_name, std::uint8_t type, char *out,
                     int out_size)>
      read_registry_string;
  std::function<void(const char *key, const char *value_name, std::uint8_t type, const char *data)>
      write_registry_string;
  std::function<std::uint32_t()> get_process_id;
  std::function<void(const char *resolved_path)> initialize_movement_runtime;
  std::function<void(float priority)> schedule_periodic_update;
};

void ExecuteMoveLogFile(const MoveLogFileDependencies &deps);
void SetMoveLogFileDependenciesForTests(MoveLogFileDependencies deps);
void ResetMoveLogFileDependenciesForTests();

using MovementRuntimeTickCountProvider = std::uint32_t (*)();

struct MovementRuntimeSnapshot {
  std::string configured_log_path;
  bool has_log_file_handle = false;
  std::uint32_t runtime_flags = 0;
  std::uint32_t previous_update_tick_ms = 0;
  std::uint32_t current_update_tick_ms = 0;
  std::uint64_t update_count = 0;
  std::uint32_t current_transport_context = 0;
  std::uint32_t previous_transport_context = 0;
};

void InitializeMovementRuntimeForTests(const char *resolved_path);
int DispatchMovementRuntimePeriodicUpdateForTests();
MovementRuntimeSnapshot GetMovementRuntimeSnapshotForTests();
void SetMovementRuntimeTickCountProviderForTests(MovementRuntimeTickCountProvider provider);
void ResetMovementRuntimeTickCountProviderForTests();
void CloseMovementRuntimeLogFileForTests();
void SetMovementRuntimeLogFileHandleForTests(std::FILE *handle);

struct GameCleanupDependencies {
  std::function<void()> shutdown_world_audio;
  std::function<void()> clear_declined_words;
  std::function<void()> shutdown_spell_visuals;
  std::function<void()> shutdown_chat_log;
  std::function<void()> shutdown_login;
  std::function<void()> shutdown_addon_data;
  std::function<void()> shutdown_auxiliary_lookup;
  std::function<void()> shutdown_dance_studio;
  std::function<void()> unregister_query_opcodes;
  std::function<void()> destroy_db_cache;
  std::function<void()> reserved_cleanup;
  std::function<void()> shutdown_character_components;
  std::function<void()> clear_virtual_frames;
  std::function<void()> shutdown_framexml_runtime;
  std::function<void()> shutdown_voice_chat;
  std::function<void()> shutdown_object_effect_data_store;
  std::function<void(int reinitialize)> shutdown_sound_system;
  std::function<void()> shutdown_frame_script;
  std::function<void()> shutdown_sound_engine_data;
  std::function<void()> cleanup_render_targets;
  std::function<void()> cleanup_render_bootstrap;
  std::function<void(const char *name)> unregister_console_command;
  std::function<int()> shutdown_combat_data;
  std::function<void()> clear_cleanup_flag;
};

int ExecuteGameCleanup(const GameCleanupDependencies &deps);
void SetGameCleanupDependenciesForTests(GameCleanupDependencies deps);
void ResetGameCleanupDependenciesForTests();
std::uint32_t GetGameCleanupFlagForTests();
void SetGameCleanupFlagForTests(std::uint32_t value);

struct LaunchWowErrorDependencies {
  std::function<bool(char *buf, int size)> get_last_log_path;
  std::function<int(const char *exe, const char *cmd, std::uintptr_t wait_callback,
                    std::intptr_t callback_arg)>
      spawn_process;
};

int ExecuteLaunchWowError(const LaunchWowErrorDependencies &deps);

struct UiShaderInitDependencies {
  bool (*prewarm_ui_program)() = nullptr;
};

void SetUiShaderInitDependenciesForTests(UiShaderInitDependencies deps);
void ResetUiShaderInitDependenciesForTests();
void ResetUiShaderInitStateForTests();

bool BuildLoadingScreenMapChangeOverlayFromData(
    LoadingScreenDynamicMapChangeAssets *assets, const LoadingScreenElementCatalog *catalog,
    std::span<const openwow::data::dbc::TaxiPathNodeEntry> taxi_path_nodes,
    std::span<const openwow::data::dbc::LoadingScreenTaxiSplinesEntry>
        loading_screen_taxi_splines,
    std::span<const openwow::data::dbc::WorldMapContinentEntry> world_map_continents,
    std::uint32_t path_segment_index, std::uint32_t loading_path_id);

}

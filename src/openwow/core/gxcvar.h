
namespace openwow::vfs {
class VirtualFileSystem;
}
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct SDL_Window;

namespace openwow::ui::game {
class CVarSystem;
}

namespace openwow::core::ida {

struct HardwareInfo;

struct MacHardwareSpecs {
  std::int32_t cpu_type = 0;
  std::int32_t cpu_subtype = 0;
  std::uint32_t cpu_count = 0;
  std::uint64_t cpu_frequency_hz = 0;
  std::uint64_t bus_frequency_hz = 0;
  std::uint64_t l2_cache_bytes = 0;
  std::uint64_t l3_cache_bytes = 0;
  std::uint64_t memory_bytes = 0;
  std::uint64_t video_memory_bytes = 0;
  bool required_sysctls_available = true;
};

struct MacHardwareClassification {
  std::uint32_t cpu_idx = 0;
  std::uint32_t tier = 0;
  std::uint32_t video_memory_mb = 0;
  bool has_ddr_memory = false;
};

[[nodiscard]] MacHardwareClassification
ClassifyMacHardware(const MacHardwareSpecs &specs);

struct StartupGraphicsQualityProfile {
  float farclip = 350.0f;
  std::uint32_t shadow_level = 1;
  std::uint32_t ground_effect_density = 16;
  bool ui_faster_enabled = true;
  std::uint32_t max_lights = 4;
  bool specular_enabled = false;
  std::uint32_t water_lod = 0;
  float particle_density = 1.0f;
  std::uint32_t base_mip = 0;
};

bool GxCVar_StereoEnabledCallback(void *cvar, const char *old_val, const char *new_val,
                                  int user_data);

[[nodiscard]] std::uint32_t GetMaxFps();
void SetMaxFps(std::uint32_t value);
[[nodiscard]] std::uint32_t GetMaxFpsBk();
void SetMaxFpsBk(std::uint32_t value);

void GxCVarRegister();

[[nodiscard]] int GetAllowMultisampleFbo();

void GxCVarInitializeRuntime(const char* window_title);

bool GxApplyPendingDisplayCVars();

struct GxDisplayCVarState {
  std::string color_bits = "24";
  std::string depth_bits = "24";
  int window_mode = 0;
  int width = 1024;
  int height = 768;
  int refresh_rate = 75;
  int triple_buffer = 0;
  int v_sync = 1;
  int aspect = 1;
  int maximize = 0;
  int cursor = 1;
  int multisample = 1;
  float multisample_quality = 0.0f;
  int fix_lag = 0;
};

struct GxRestartRuntimeState {
  GxDisplayCVarState active_state{};
  GxDisplayCVarState last_good_state{};
  GxDisplayCVarState default_state{};
  GxDisplayCVarState validated_default_state{};
  std::string window_title = "World of Warcraft";
};

void ValidateFormatMonitor(GxDisplayCVarState &state);

bool GxApplyDisplayCVarState(const GxDisplayCVarState &state);

bool GxApplyRegisteredDefaultDisplayCVars();

bool GxRestartCurrentDisplayMode();

struct GxDeviceOverrideState {
  std::array<bool, 9> enabled{};
  std::array<int, 9> values{};
};

struct GxStereoRuntimeState {
  bool enabled = false;
  float convergence = 0.0f;
  float separation = 0.0f;
};

struct WindowResizeLockRuntimeState {
  bool resize_locked = false;
  bool has_bound_window = false;
  std::uint32_t apply_count = 0;
};

void RegisterWindowResizeLockCVarCallback(openwow::ui::game::CVarSystem &cvars, SDL_Window *window);
[[nodiscard]] bool ApplyCurrentWindowResizeLockCVar(openwow::ui::game::CVarSystem &cvars,
                                                    SDL_Window *window);
void ClearWindowResizeLockBoundWindow();

void ConsoleDeviceInitialize(const char *window_title);

void DetectHardware(const openwow::vfs::VirtualFileSystem *vfs = nullptr);

[[nodiscard]] const HardwareInfo *GetDetectedHardwareInfoIfReady();

[[nodiscard]] const StartupGraphicsQualityProfile *GetStartupGraphicsQualityProfile();

bool RefreshStartupGraphicsQualityProfileFromDetectedHardware();

void InitializeStartupHardwareDetectionState(const openwow::vfs::VirtualFileSystem *vfs = nullptr);
[[nodiscard]] bool ShouldReplayStartupDisplaySettings();

[[noreturn]] void FatalError_Exit(int error_id, const std::string &error_text);

bool CGxMonitorMode_Validate(void *cvar, const char *old_val, const char *new_val, int user_data);

void CommandHistoryPush(const std::string &command);
[[nodiscard]] const char *CommandHistoryGetRelativeEntry(std::uint32_t relative_index);
[[nodiscard]] bool CommandHistoryBrowseOlder(const char **out_entry);
[[nodiscard]] bool CommandHistoryBrowseNewer(const char **out_entry);
void ResetCommandHistoryNavigation();

struct HardwareInfo {
  uint16_t vendor_id = 0;
  uint16_t reserved = 0;
  uint32_t device_id = 0;
  uint32_t subsys_id = 0;
  uint32_t driver_hi = 0;
  uint32_t driver_lo = 0;
  uint32_t cpu_idx = 0;
  uint32_t video_id = 0;
  uint32_t sound_idx = 0;
  uint32_t mem_idx = 0;
  uint32_t video_profile_ptr = 0;
  uint32_t quality_table_ptr = 0;
  uint32_t hw_caps_ptr = 0;
  uint32_t mac_tier = 0;
  uint32_t mac_vram_mb = 0;
  bool mac_has_ddr_memory = false;
  bool mac_classification_available = false;
  bool has_video_profile = false;
  bool video_profile_disables_texture_atlas = false;

  uint32_t startup_farclip_index = 0;
  uint32_t startup_shadow_level = 1;
  uint32_t startup_ground_effect_density_index = 0;
  bool startup_ui_faster_enabled = true;
  uint32_t startup_max_lights = 4;
  bool startup_specular_enabled = false;
  uint32_t startup_water_lod_index = 0;
  uint32_t startup_particle_density_index = 0;
  uint32_t startup_base_mip = 0;
};

void SetDetectedHardwareInfoForTests(const HardwareInfo &hardware_info);
void ClearDetectedHardwareInfoForTests();
void SetHardwareChangedForTests(bool changed);
void SetHardwareRegistryValueForTests(const char *value_name, std::uint32_t value);
[[nodiscard]] bool GetHardwareRegistryValueForTests(const char *value_name,
                                                    std::uint32_t *out_value);
void ClearHardwareRegistryValuesForTests();
void SetHardwareChangePromptResponseForTests(int response_code);
void ClearHardwareChangePromptResponseForTests();
void SetStartupGraphicsQualityProfileForTests(const StartupGraphicsQualityProfile &profile);
void ClearStartupGraphicsQualityProfileForTests();
[[nodiscard]] GxRestartRuntimeState GetGxRestartRuntimeStateForTests();
void SetGxRestartRuntimeStateForTests(const GxRestartRuntimeState &state);
void SetGxRestartApplyResponsesForTests(std::vector<std::optional<GxDisplayCVarState>> responses);
void ClearGxRestartApplyResponsesForTests();
[[nodiscard]] GxDeviceOverrideState GetGxDeviceOverrideStateForTests();
void ResetGxDeviceOverrideStateForTests();
[[nodiscard]] GxStereoRuntimeState GetGxStereoRuntimeStateForTests();
void ResetGxStereoRuntimeStateForTests();
[[nodiscard]] WindowResizeLockRuntimeState GetWindowResizeLockRuntimeStateForTests();
void ResetWindowResizeLockRuntimeStateForTests();
void ResetCommandHistoryForTests();
}

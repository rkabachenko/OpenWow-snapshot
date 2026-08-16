
#include "openwow/core/gxcvar.h"

#include <SDL2/SDL.h>
#include <StormLib.h>

#include "openwow/core/console.h"
#include "openwow/core/decimal_parse.h"
#include "openwow/platform/adapters/sdl/platform_layer.h"
#include "openwow/core/storm_cmd.h"
#include "openwow/core/storm_error.h"
#include "openwow/core/storm_string.h"
#include "openwow/core/window_manager.h"
#include "openwow/data/formats/dbc/dbc_file.h"
#include "openwow/game/client_config.h"
#include "openwow/platform/process/os_platform.h"
#include "openwow/platform/window/window_manager.h"
#include "openwow/vfs/sfile_core.h"
#include "openwow/vfs/virtual_file_system.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

#include "openwow/debug/diagnostics/debug_console.h"
#include "openwow/render/backend/bgfx/retail_render_profile.h"
#include "openwow/render/platform/renderer_backend_selection.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/display/settings/adapters/platform/display_mode_catalog.h"
#include "openwow/foundation/text/ascii.h"

namespace openwow::core::ida {

static HardwareInfo s_hw_info{};
static bool s_hw_detected = false;
static bool s_hw_changed = false;
static StartupGraphicsQualityProfile s_startup_graphics_quality_profile{};
static bool s_startup_hw_detect_requested = false;
static bool s_startup_hardware_state_captured = false;
static bool s_startup_graphics_quality_profile_initialized = false;
static bool s_pending_gx_restart = false;
static GxStereoRuntimeState s_gx_stereo_runtime{};
static bool s_startup_graphics_quality_profile_test_override = false;
static GxRestartRuntimeState s_gx_restart_runtime{};
static std::vector<std::optional<GxDisplayCVarState>> s_gx_restart_apply_responses;
static std::size_t s_gx_restart_apply_response_index = 0;
static GxDeviceOverrideState s_gx_device_override_state{};
static std::uint32_t s_max_fps = 200;

static std::uint32_t s_max_fps_bk = 30;

static int s_allow_multisample_fbo = 1;
static std::optional<std::unordered_map<std::string, std::uint32_t>> s_hardware_registry_overrides;
static std::optional<int> s_hardware_change_prompt_response_override;

void ParseAndStoreGxOverrideString(std::string_view command_string);

struct WindowResizeLockCallbackState {
  std::mutex mutex;
  openwow::ui::game::CVarSystem *cvars = nullptr;
  SDL_Window *window = nullptr;
  bool resize_locked = false;
  std::uint32_t apply_count = 0;
};

WindowResizeLockCallbackState &GetWindowResizeLockCallbackState() {
  static WindowResizeLockCallbackState state;
  return state;
}

static constexpr int kHistoryRingSize = 32;
static constexpr int kHistoryEntryLen = 1024;
static char s_history_ring[kHistoryRingSize][kHistoryEntryLen] = {};
static int s_history_write_cursor = 0;
static int s_history_browse_index = -1;

constexpr std::array<std::string_view, 14> kPendingDisplayCVarNames{{
    "gxColorBits",
    "gxDepthBits",
    "gxWindow",
    "gxResolution",
    "gxRefresh",
    "gxTripleBuffer",
    "gxApi",
    "gxVSync",
    "gxAspect",
    "gxMaximize",
    "gxCursor",
    "gxMultisample",
    "gxMultisampleQuality",
    "gxFixLag",
}};

constexpr std::size_t kGxOverrideSlotCount = 9;

constexpr std::array<openwow::ui::display::platform::ScreenResolution, 7>
    kValidateFormatMonitorFallbackResolutions{{
        {1600, 1200},
        {1280, 1024},
        {1280, 960},
        {1152, 864},
        {1024, 768},
        {800, 600},
        {640, 480},
    }};

constexpr const char *kUnableToSetRequestedDisplayMode = "unable to set requested display mode";
constexpr const char *kUnableToSetLastGoodMode = "unable to set last good mode";
constexpr const char *kUnableToSetDefaultFormat = "unable to set default format";
constexpr std::array<std::string_view, 6> kRetailGxApiNames{{
    "OpenGL",
    "D3D9",
    "D3D9Ex",
    "D3D10",
    "D3D11",
    "GLL",
}};

bool IsRetailGxApiAvailable(const std::size_t index) {
  using openwow::render::api::RendererBackend;
  switch (index) {
    case 0:
    case 5:
      return openwow::render::IsRendererBackendSupported(
          RendererBackend::OpenGL);
    case 1:
    case 2:
    case 3:
    case 4:
      return openwow::render::IsRendererBackendSupported(
          RendererBackend::Direct3D11);
    default:
      return false;
  }
}

constexpr const char *kVideoHardwareDbcPath = "DBFilesClient\\VideoHardware.dbc";
constexpr const char *kHardwareRegistrySubKey = "World of Warcraft\\Client";
constexpr const char *kHardwareCpuRegistryValue = "HWCpuIdx";
constexpr const char *kHardwareMemRegistryValue = "HWMemIdx";
constexpr const char *kHardwareVideoRegistryValue = "HWVideoID";
constexpr const char *kHardwareSoundRegistryValue = "HWSoundIdx";
constexpr const char *kHardwareChangedPromptTitle = "World of Warcraft";
constexpr const char *kHardwareChangedPromptText = "Hardware changed.  Reload default settings?";
constexpr std::uint32_t kVideoHardwareExpectedFieldCount = 23;
constexpr std::uint32_t kVideoHardwareExpectedRecordSize = 92;
constexpr std::uint32_t kWildCardVideoHardwareVendorId = 0xFFFFu;
constexpr std::array<float, 4> kStartupFarclipTable{{200.0f, 277.0f, 300.0f, 350.0f}};
constexpr std::array<std::uint32_t, 4> kStartupGroundEffectDensityTable{{8u, 12u, 16u, 24u}};
constexpr std::array<std::uint32_t, 4> kStartupWaterLodTable{{0u, 0u, 0u, 1u}};
constexpr std::array<float, 8> kStartupParticleDensityTable{{
    0.3f,
    0.4f,
    0.5f,
    0.6f,
    0.9f,
    1.0f,
    1.0f,
    1.0f,
}};

struct GxRegistrationDefaults {
  std::string window = "0";
  std::string maximize = "0";
  std::string color_bits = "24";
  std::string depth_bits = "24";
  std::string resolution = "1024x768";
  std::string refresh = "75";
  std::string triple_buffer = "0";
  std::string api = "D3D9";
  std::string v_sync = "1";
  std::string aspect = "1";
  std::string cursor = "1";
  std::string multisample = "1";
  std::string multisample_quality = "0.0";
  std::string fix_lag = "0";
};

class ScopedSFileHandle {
public:
  ~ScopedSFileHandle() {
    if (handle_ != 0) {
      (void)SFileCloseFile(reinterpret_cast<HANDLE>(static_cast<intptr_t>(handle_)));
    }
  }

  [[nodiscard]] int get() const { return handle_; }
  [[nodiscard]] int *out_parameter() { return &handle_; }

private:
  int handle_ = 0;
};

[[nodiscard]] bool ReadVideoHardwareDbcWord(const int file_handle, std::uint32_t &out_word) {
  return openwow::vfs::SFile_ReadFile(file_handle, &out_word, sizeof(out_word), nullptr, 0, 0) != 0;
}

[[nodiscard]] bool ReadHardwareRegistryValue(const char *value_name, std::uint32_t *out_value) {
  if (!value_name || !*value_name || !out_value) {
    return false;
  }

  if (s_hardware_registry_overrides.has_value()) {
    const auto it = s_hardware_registry_overrides->find(value_name);
    if (it == s_hardware_registry_overrides->end()) {
      return false;
    }
    *out_value = it->second;
    return true;
  }

  return openwow::core::ReadRegistryValue(kHardwareRegistrySubKey, value_name, 0, out_value);
}

void WriteHardwareRegistryValue(const char *value_name, const std::uint32_t value) {
  if (!value_name || !*value_name) {
    return;
  }

  if (s_hardware_registry_overrides.has_value()) {
    (*s_hardware_registry_overrides)[value_name] = value;
    return;
  }

  (void)openwow::core::WriteRegistryValue(kHardwareRegistrySubKey, value_name, 0, value);
}

[[nodiscard]] bool IsStoredVideoIdCompatibleAlias(const std::uint32_t video_id) {
  return video_id == 1u || video_id == 168u || video_id == 169u || video_id == 170u;
}

[[nodiscard]] int ShowHardwareChangedPrompt() {
  if (s_hardware_change_prompt_response_override.has_value()) {
    return *s_hardware_change_prompt_response_override;
  }

  static constexpr SDL_MessageBoxButtonData kButtons[] = {
      {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Yes"},
      {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 1, "No"},
  };
  const SDL_MessageBoxData dialog{
      SDL_MESSAGEBOX_INFORMATION,
      SDL_GL_GetCurrentWindow(),
      kHardwareChangedPromptTitle,
      kHardwareChangedPromptText,
      SDL_arraysize(kButtons),
      kButtons,
      nullptr,
  };

  int button_id = 1;
  if (SDL_ShowMessageBox(&dialog, &button_id) == 0 && button_id >= 0) {
    return button_id == 0 ? 0 : 1;
  }

  return openwow::platform::ShowMessageBox(kHardwareChangedPromptText, kHardwareChangedPromptTitle,
                                           openwow::platform::MessageBoxButtons::kYesNo);
}

[[nodiscard]] bool
DetectHardware_CheckStoredHardwareAndPromptReset(const HardwareInfo &hardware_info) {
  std::uint32_t stored_cpu_idx = hardware_info.cpu_idx;
  std::uint32_t stored_mem_idx = hardware_info.mem_idx;
  std::uint32_t stored_video_id = hardware_info.video_id;
  std::uint32_t stored_sound_idx = hardware_info.sound_idx;

  if (!ReadHardwareRegistryValue(kHardwareCpuRegistryValue, &stored_cpu_idx)) {
    stored_cpu_idx = hardware_info.cpu_idx;
  }
  if (!ReadHardwareRegistryValue(kHardwareMemRegistryValue, &stored_mem_idx)) {
    stored_mem_idx = hardware_info.mem_idx;
  }
  if (!ReadHardwareRegistryValue(kHardwareVideoRegistryValue, &stored_video_id)) {
    stored_video_id = hardware_info.video_id;
  }
  if (!ReadHardwareRegistryValue(kHardwareSoundRegistryValue, &stored_sound_idx)) {
    stored_sound_idx = hardware_info.sound_idx;
  }

  const bool matches_current_snapshot = hardware_info.cpu_idx == stored_cpu_idx &&
                                        hardware_info.mem_idx == stored_mem_idx &&
                                        (IsStoredVideoIdCompatibleAlias(stored_video_id) ||
                                         hardware_info.video_id == stored_video_id) &&
                                        hardware_info.sound_idx == stored_sound_idx;

  const bool reload_defaults = !matches_current_snapshot && ShowHardwareChangedPrompt() == 0;

  WriteHardwareRegistryValue(kHardwareCpuRegistryValue, hardware_info.cpu_idx);
  WriteHardwareRegistryValue(kHardwareMemRegistryValue, hardware_info.mem_idx);
  WriteHardwareRegistryValue(kHardwareVideoRegistryValue, hardware_info.video_id);
  WriteHardwareRegistryValue(kHardwareSoundRegistryValue, hardware_info.sound_idx);
  return reload_defaults;
}

openwow::data::dbc::DbcFile LoadVideoHardwareDbcFile(
    const openwow::vfs::VirtualFileSystem *vfs) {

  if (!vfs) {
    return {};
  }

  auto bytes = vfs->ReadFileBytes(kVideoHardwareDbcPath);
  if (!bytes.has_value()) {
    openwow::core::SErrFatalError_VArgs(0x85100079, "Unable to open %s", kVideoHardwareDbcPath);
  }

  std::size_t cursor = 0;
  std::uint32_t signature = 0;
  if (cursor + sizeof(signature) > bytes->size()) {
    openwow::core::SErrFatalError_VArgs(0x85100079, "Unable to read signature from %s",
                                        kVideoHardwareDbcPath);
  }
  std::memcpy(&signature, bytes->data() + cursor, sizeof(signature));
  cursor += sizeof(signature);

  if (signature != openwow::data::dbc::DbcHeader::kWdbcSignature) {
    openwow::core::SErrFatalError_VArgs(0x85100079, "Invalid signature 0x%x from %s", signature,
                                        kVideoHardwareDbcPath);
  }

  std::uint32_t record_count = 0;
  if (cursor + sizeof(record_count) > bytes->size()) {
    openwow::core::SErrFatalError_VArgs(0x85100079, "Unable to read record count from %s",
                                        kVideoHardwareDbcPath);
  }
  std::memcpy(&record_count, bytes->data() + cursor, sizeof(record_count));
  cursor += sizeof(record_count);

  if (record_count == 0) {
    return {};
  }

  std::uint32_t field_count = 0;
  if (cursor + sizeof(field_count) > bytes->size()) {
    openwow::core::SErrFatalError_VArgs(0x85100079, "Unable to read column count from %s",
                                        kVideoHardwareDbcPath);
  }
  std::memcpy(&field_count, bytes->data() + cursor, sizeof(field_count));
  cursor += sizeof(field_count);

  if (field_count != kVideoHardwareExpectedFieldCount) {
    openwow::core::SErrFatalError_VArgs(
        0x85100079, "%s has wrong number of columns (found %i, expected %i)", kVideoHardwareDbcPath,
        field_count, kVideoHardwareExpectedFieldCount);
  }

  std::uint32_t record_size = 0;
  if (cursor + sizeof(record_size) > bytes->size()) {
    openwow::core::SErrFatalError_VArgs(0x85100079, "Unable to read row size from %s",
                                        kVideoHardwareDbcPath);
  }
  std::memcpy(&record_size, bytes->data() + cursor, sizeof(record_size));
  cursor += sizeof(record_size);

  if (record_size != kVideoHardwareExpectedRecordSize) {
    openwow::core::SErrFatalError_VArgs(0x85100079, "%s has wrong row size (found %i, expected %i)",
                                        kVideoHardwareDbcPath, record_size,
                                        kVideoHardwareExpectedRecordSize);
  }

  std::uint32_t string_block_size = 0;
  if (cursor + sizeof(string_block_size) > bytes->size()) {
    openwow::core::SErrFatalError_VArgs(0x85100079, "Unable to read string size from %s",
                                        kVideoHardwareDbcPath);
  }
  std::memcpy(&string_block_size, bytes->data() + cursor, sizeof(string_block_size));
  cursor += sizeof(string_block_size);

  const auto expected_size = sizeof(openwow::data::dbc::DbcHeader) +
                             static_cast<std::size_t>(record_count) * record_size +
                             string_block_size;
  if (bytes->size() < expected_size) {
    openwow::core::SErrFatalCondition("%s: File too short", kVideoHardwareDbcPath);
  }

  openwow::data::dbc::DbcFile file;
  if (file.LoadFromBytes(std::move(*bytes)) != openwow::data::dbc::DbcError::kOk) {
    openwow::core::SErrFatalCondition("%s: Cannot parse DBC", kVideoHardwareDbcPath);
  }
  return file;
}

bool TryPopulateVideoHardwareProfile(const openwow::data::dbc::DbcFile &file,
                                     const std::uint32_t vendor_id, const std::uint32_t device_id,
                                     HardwareInfo &hardware_info) {
  const auto row_count = file.record_count();

  for (std::uint32_t row = 1; row < row_count; ++row) {
    if (file.GetUInt32(row, 1) != vendor_id || file.GetUInt32(row, 2) != device_id) {
      continue;
    }

    hardware_info.video_id = file.GetUInt32(row, 0);
    hardware_info.has_video_profile = true;
    hardware_info.startup_farclip_index = file.GetUInt32(row, 3);
    hardware_info.startup_shadow_level = file.GetUInt32(row, 5);
    hardware_info.startup_ground_effect_density_index = file.GetUInt32(row, 6);
    hardware_info.startup_ui_faster_enabled = file.GetUInt32(row, 9) != 0;
    hardware_info.startup_max_lights = file.GetUInt32(row, 10);
    hardware_info.startup_specular_enabled = file.GetUInt32(row, 11) != 0;
    hardware_info.startup_water_lod_index = file.GetUInt32(row, 12);
    hardware_info.startup_particle_density_index = file.GetUInt32(row, 13);
    hardware_info.startup_base_mip = file.GetUInt32(row, 17);
    hardware_info.video_profile_disables_texture_atlas = !hardware_info.startup_ui_faster_enabled;
    return true;
  }

  return false;
}

template <typename T, std::size_t N>
const T &RequireStartupQualityValue(const std::array<T, N> &table, const std::size_t index,
                                    const char *field_name) {
  if (index >= table.size()) {
    openwow::core::SErrFatalCondition("VideoHardware.dbc: invalid %s index %u", field_name,
                                      static_cast<unsigned int>(index));
  }

  return table[index];
}

int ParseDisplayInteger(const std::string &text, int fallback);
float ParseDisplayFloat(const std::string &text, float fallback);
std::pair<int, int> ParseDisplayResolution(const std::string &text, int fallback_width,
                                           int fallback_height);
GxDisplayCVarState BuildRequestedDisplayCVarState(openwow::ui::game::CVarSystem &sys);
GxDisplayCVarState BuildRegisteredDefaultDisplayState();
GxRegistrationDefaults BuildGxRegistrationDefaults();
void ValidateFormatMonitor(GxDisplayCVarState &state);
void EmitPendingGxRestartLine();
bool WindowResizeLockCVarValidationCallback(const std::string &, const std::string &,
                                            const std::string &new_value);

const char *GetRetailDefaultGxApiCVarValue() {
  return kRetailGxApiNames[1].data();
}

bool IsSimplifiedChineseClientLocale() {
  return openwow::game::ClientConfig::Get().GetLocale() == "zhCN";
}

std::string CanonicalizeRetailColorBits(std::string_view value, std::string_view fallback = "24") {
  switch (openwow::core::ParseSignedDecimalLikeSub76F0D0(value)) {
  case 16:
    return "16";
  case 24:
    return "24";
  case 30:
    return "30";
  default:
    return std::string(fallback);
  }
}

std::string CanonicalizeRetailDepthBits(std::string_view value, std::string_view fallback = "24") {
  switch (openwow::core::ParseSignedDecimalLikeSub76F0D0(value)) {
  case 16:
    return "16";
  case 24:
    return "24";
  case 32:
    return "32";
  default:
    return std::string(fallback);
  }
}

int NormalizeRetailBooleanDisplayCVar(std::string_view value) {
  return openwow::core::ParseSignedDecimalLikeSub76F0D0(value) != 0 ? 1 : 0;
}

int NormalizeRetailSignedDisplayCVar(std::string_view value) {
  return static_cast<int>(openwow::core::ParseSignedDecimalLikeSub76F0D0(value));
}

int NormalizeRetailMultisample(std::string_view value) {
  int multisample = static_cast<int>(openwow::core::ParseSignedDecimalLikeSub76F0D0(value));
  if (multisample <= 1) {
    return 1;
  }
  if (multisample >= 16) {
    return 16;
  }
  return multisample;
}

float NormalizeRetailMultisampleQuality(std::string_view value) {
  const double parsed = openwow::core::ParseFloatLikeSub76FB80(value);
  if (parsed < 0.0) {
    return 0.0f;
  }
  if (parsed >= 1.0) {
    return 1.0f;
  }
  return static_cast<float>(parsed);
}

std::string BuildUnsupportedGxApiMessage() {
  std::string message = "unsupported api, must be one of ";
  bool first = true;
  for (std::size_t index = 0; index < kRetailGxApiNames.size(); ++index) {
    if (!IsRetailGxApiAvailable(index)) {
      continue;
    }
    if (!first) {
      message += ", ";
    }
    message += '\'';
    message += kRetailGxApiNames[index];
    message += '\'';
    first = false;
  }
  return message;
}

bool GxCVar_WindowModeValidationCallback(const std::string &, const std::string &,
                                         const std::string &) {
  EmitPendingGxRestartLine();
  return true;
}

bool GxCVar_MaximizeValidationCallback(const std::string &, const std::string &,
                                       const std::string &) {
  EmitPendingGxRestartLine();
  return true;
}

bool GxCVar_ColorBitsValidationCallback(const std::string &, const std::string &,
                                        const std::string &new_value) {
  switch (openwow::core::ParseSignedDecimalLikeSub76F0D0(new_value)) {
  case 16:
  case 24:
  case 30:
    EmitPendingGxRestartLine();
    return true;
  default:
    openwow::debug::DebugConsole::Get().Write("Color bits must be 16, 24, or 30");
    return false;
  }
}

bool GxCVar_DepthBitsValidationCallback(const std::string &, const std::string &,
                                        const std::string &new_value) {
  switch (openwow::core::ParseSignedDecimalLikeSub76F0D0(new_value)) {
  case 16:
  case 24:
  case 32:
    EmitPendingGxRestartLine();
    return true;
  default:
    openwow::debug::DebugConsole::Get().Write("Depth bits must be 16, 24, or 32");
    return false;
  }
}

bool GxCVar_TripleBufferValidationCallback(const std::string &, const std::string &,
                                           const std::string &new_value) {
  const std::uint32_t parsed = openwow::core::ParseSignedDecimalLikeSub76F0D0(new_value);
  if (parsed >= 2) {
    openwow::debug::DebugConsole::Get().Write("TripleBuffer must be 0 or 1");
    return false;
  }

  EmitPendingGxRestartLine();
  return true;
}

bool GxCVar_ApiValidationCallback(const std::string &, const std::string &,
                                  const std::string &new_value) {
  for (std::size_t index = 0; index < kRetailGxApiNames.size(); ++index) {
    if (!IsRetailGxApiAvailable(index)) {
      continue;
    }
    if (openwow::text::EqualsIgnoreCaseAscii(new_value, kRetailGxApiNames[index])) {
      openwow::debug::DebugConsole::Get().Write("GxApi set pending gxRestart");
      return true;
    }
  }

  openwow::debug::DebugConsole::Get().Write(BuildUnsupportedGxApiMessage());
  return false;
}

bool GxCVar_VSyncValidationCallback(const std::string &, const std::string &, const std::string &) {
  EmitPendingGxRestartLine();
  return true;
}

bool GxCVar_AspectValidationCallback(const std::string &, const std::string &,
                                     const std::string &) {
  EmitPendingGxRestartLine();
  return true;
}

bool GxCVar_CursorValidationCallback(const std::string &, const std::string &,
                                     const std::string &) {
  EmitPendingGxRestartLine();
  return true;
}

bool GxCVar_MultisampleValidationCallback(const std::string &, const std::string &,
                                          const std::string &) {
  EmitPendingGxRestartLine();
  return true;
}

bool GxCVar_MultisampleQualityValidationCallback(const std::string &, const std::string &,
                                                 const std::string &) {
  EmitPendingGxRestartLine();
  return true;
}

bool GxCVar_FixLagValidationCallback(const std::string &, const std::string &,
                                     const std::string &) {
  EmitPendingGxRestartLine();
  return true;
}

bool GxCVar_OverrideValidationCallback(const std::string &, const std::string &,
                                       const std::string &new_value) {
  ParseAndStoreGxOverrideString(new_value);
  return true;
}

std::uint32_t GetMaxFps() { return s_max_fps; }

void SetMaxFps(std::uint32_t value) { s_max_fps = value; }

std::uint32_t GetMaxFpsBk() { return s_max_fps_bk; }

void SetMaxFpsBk(std::uint32_t value) { s_max_fps_bk = value; }

static std::uint32_t ClampFpsCap(std::uint32_t raw) {
  if (raw >= 1 && raw <= 7)
    return 8;
  return raw;
}

bool GxCVar_MaxFPSValidationCallback(const std::string &, const std::string &,
                                     const std::string &new_value) {
  const auto raw = static_cast<std::uint32_t>(ParseSignedDecimalLikeSub76F0D0(new_value));
  SetMaxFps(ClampFpsCap(raw));
  return true;
}

bool GxCVar_MaxFPSBkValidationCallback(const std::string &, const std::string &,
                                       const std::string &new_value) {
  const auto raw = static_cast<std::uint32_t>(ParseSignedDecimalLikeSub76F0D0(new_value));
  SetMaxFpsBk(ClampFpsCap(raw));
  return true;
}

bool ApplyWindowResizeLockValue(std::string_view value, SDL_Window *window_override) {
  auto &callback_state = GetWindowResizeLockCallbackState();
  const bool resize_locked = ParseSignedDecimalLikeSub76F0D0(value) != 0;

  SDL_Window *window = window_override;
  {
    std::lock_guard lock(callback_state.mutex);
    if (window_override != nullptr) {
      callback_state.window = window_override;
    }
    window = callback_state.window;
    callback_state.resize_locked = resize_locked;
    ++callback_state.apply_count;
  }

  if (window != nullptr) {
    SDL_SetWindowResizable(window, resize_locked ? SDL_FALSE : SDL_TRUE);
  }
  return resize_locked;
}

WindowMode ToWindowMode(const int window_mode) {
  switch (window_mode) {
  case 0:
    return WindowMode::Fullscreen;
  case 2:
    return WindowMode::FullscreenDesktop;
  default:
    return WindowMode::Windowed;
  }
}

openwow::platform::WindowMode ToPlatformWindowMode(const WindowMode mode) {
  switch (mode) {
  case WindowMode::Fullscreen:
    return openwow::platform::WindowMode::Fullscreen;
  case WindowMode::FullscreenDesktop:
    return openwow::platform::WindowMode::WindowedFullscreen;
  case WindowMode::Windowed:
  case WindowMode::BorderlessWindowed:
    return openwow::platform::WindowMode::Windowed;
  }
  return openwow::platform::WindowMode::Windowed;
}

bool ApplyDisplayStateToRuntimeObjects(const GxDisplayCVarState &state) {
  auto &window_manager = WindowManager::Instance();
  auto &platform_window = openwow::platform::WindowManager::Get();
  const WindowMode mode = ToWindowMode(state.window_mode);
  if (state.width <= 0 || state.height <= 0) {
    return false;
  }

  const auto width = static_cast<std::uint32_t>(state.width);
  const auto height = static_cast<std::uint32_t>(state.height);
  const auto refresh_rate = static_cast<std::uint32_t>(
      std::max(0, state.refresh_rate));
  if (platform_window.IsInitialized() &&
      !platform_window.ApplyDisplayMode({
          .mode = ToPlatformWindowMode(mode),
          .pixel_width = width,
          .pixel_height = height,
          .refresh_rate = refresh_rate,
          .maximize = mode == WindowMode::Windowed && state.maximize != 0,
      })) {
    return false;
  }

  window_manager.SetSize(width, height);
  window_manager.SetMode(mode);
  window_manager.SetVSync(state.v_sync != 0);
  window_manager.SetTitle(s_gx_restart_runtime.window_title);
  platform_window.SetTitle(s_gx_restart_runtime.window_title);
  return true;
}

GxDisplayCVarState BuildValidatedDisplayState(const GxDisplayCVarState &state) {
  GxDisplayCVarState validated_state = state;
  ValidateFormatMonitor(validated_state);
  return validated_state;
}

void SeedGxRestartRuntime(openwow::ui::game::CVarSystem &sys, const char *window_title) {
  s_gx_restart_apply_responses.clear();
  s_gx_restart_apply_response_index = 0;
  s_gx_restart_runtime.default_state = BuildRegisteredDefaultDisplayState();
  s_gx_restart_runtime.validated_default_state =
      BuildValidatedDisplayState(s_gx_restart_runtime.default_state);
  s_gx_restart_runtime.active_state =
      BuildValidatedDisplayState(BuildRequestedDisplayCVarState(sys));
  s_gx_restart_runtime.last_good_state = s_gx_restart_runtime.active_state;
  s_gx_restart_runtime.window_title = window_title != nullptr ? window_title : "World of Warcraft";
  (void)GxApplyDisplayCVarState(s_gx_restart_runtime.active_state);
  (void)ApplyDisplayStateToRuntimeObjects(s_gx_restart_runtime.active_state);
}

std::optional<GxDisplayCVarState> TryApplyDisplayState(const GxDisplayCVarState &state) {
  if (s_gx_restart_apply_response_index < s_gx_restart_apply_responses.size()) {
    const auto response = s_gx_restart_apply_responses[s_gx_restart_apply_response_index++];
    if (response.has_value() && !ApplyDisplayStateToRuntimeObjects(*response)) {
      return std::nullopt;
    }
    return response;
  }

  if (!ApplyDisplayStateToRuntimeObjects(state)) {
    return std::nullopt;
  }
  return state;
}

void EmitDisplayRestartFailure(const char *text) {
  openwow::debug::DebugConsole::Get().Write(text);
}

bool ParseStereoEnabledValue(std::string_view value) {
  return openwow::core::ParseSignedDecimalLikeSub76F0D0(value) == 1u;
}

float ParseStereoFloatValue(std::string_view value) {
  return static_cast<float>(openwow::core::ParseFloatLikeSub76FB80(value));
}

bool IsWindowedDisplayModeActive() {
  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  return cvars.Exists("gxWindow") && cvars.GetCVarInt("gxWindow") != 0;
}

bool IsWidescreenDisplayModeEnabled() {
  auto &cvars = openwow::ui::game::CVarSystem::Instance();
  return cvars.Exists("widescreen") && cvars.GetCVarBool("widescreen");
}

void EmitPendingGxRestartLine() {
  s_pending_gx_restart = true;
  openwow::debug::DebugConsole::Get().Write("set pending gxRestart");
}

void EmitInvalidResolutionCatalog(
    const std::vector<openwow::ui::display::platform::ScreenResolution>
        &resolutions) {
  std::string line = "invalid resolution, must be one of ";

  for (std::size_t index = 0; index < resolutions.size(); ++index) {
    const auto &resolution = resolutions[index];
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%dx%d", resolution.width, resolution.height);

    const std::string separator = index == 0 ? "" : ", ";
    if (line.size() > 100) {
      openwow::debug::DebugConsole::Get().Write(line);
      line.clear();
    }

    line += separator;
    line += buffer;
  }

  openwow::debug::DebugConsole::Get().Write(line);
}

bool PrimaryDisplayModesContainRefreshRate(const int refresh_rate) {
  const auto display_modes =
      openwow::ui::display::platform::AvailableDisplayModes();
  return std::any_of(display_modes.begin(), display_modes.end(),
                     [refresh_rate](
                         const openwow::ui::display::platform::
                             DisplayMode& mode) {
                       return mode.refresh_rate == refresh_rate;
                     });
}

int ParseDisplayInteger(const std::string &text, int fallback);
float ParseDisplayFloat(const std::string &text, float fallback);
std::pair<int, int> ParseDisplayResolution(const std::string &text, int fallback_width,
                                           int fallback_height);

int MonitorFormatBitsPerPixel(const std::string &color_bits) {
  return ParseDisplayInteger(color_bits, 24) == 16 ? 16 : 32;
}

bool WindowedRequestFitsCurrentDisplay(const GxDisplayCVarState &state) {
  const auto current_mode =
      openwow::ui::display::platform::CurrentDisplayMode();
  return current_mode.has_value() && current_mode->width >= state.width &&
         current_mode->height >= state.height;
}

void ValidateMonitorResolutionAgainstDisplayModes(
    GxDisplayCVarState& state,
    const std::vector<
        openwow::ui::display::platform::DisplayMode>& display_modes) {
  if (state.window_mode != 0 && WindowedRequestFitsCurrentDisplay(state)) {
    return;
  }

  int candidate_width = state.width;
  int candidate_height = state.height;
  int fallback_index = -1;

  while (true) {
    if (!display_modes.empty()) {
      const auto exact_match = std::find_if(
          display_modes.begin(), display_modes.end(),
          [candidate_width, candidate_height](
              const openwow::ui::display::platform::DisplayMode& mode) {
            return mode.width == candidate_width && mode.height == candidate_height;
          });
      if (exact_match != display_modes.end()) {
        state.width = candidate_width;
        state.height = candidate_height;
        return;
      }
    }

    ++fallback_index;
    while (fallback_index < static_cast<int>(kValidateFormatMonitorFallbackResolutions.size()) &&
           (kValidateFormatMonitorFallbackResolutions[static_cast<std::size_t>(fallback_index)]
                    .width >= candidate_width ||
            kValidateFormatMonitorFallbackResolutions[static_cast<std::size_t>(fallback_index)]
                    .height >= candidate_height)) {
      ++fallback_index;
    }

    if (fallback_index >= static_cast<int>(kValidateFormatMonitorFallbackResolutions.size())) {
      return;
    }

    candidate_width =
        kValidateFormatMonitorFallbackResolutions[static_cast<std::size_t>(fallback_index)].width;
    candidate_height =
        kValidateFormatMonitorFallbackResolutions[static_cast<std::size_t>(fallback_index)].height;
  }
}

void ValidateMonitorRefreshAgainstDisplayModes(
    GxDisplayCVarState& state,
    const std::vector<
        openwow::ui::display::platform::DisplayMode>& display_modes) {
  const int requested_refresh = state.refresh_rate;
  const int requested_bits_per_pixel = MonitorFormatBitsPerPixel(state.color_bits);
  int lowest_matching_refresh = 9999;

  for (const auto &mode : display_modes) {
    if (mode.width != state.width || mode.height != state.height ||
        mode.bits_per_pixel != requested_bits_per_pixel) {
      continue;
    }

    if (mode.refresh_rate < lowest_matching_refresh) {
      lowest_matching_refresh = mode.refresh_rate;
    }
    if (mode.refresh_rate == requested_refresh) {
      return;
    }
  }

  if (lowest_matching_refresh == 9999) {
    ConsoleLog("ValidateFormatMonitor(): unable to find monitor refresh");
    lowest_matching_refresh = 60;
  }

  ConsoleLog("ValidateFormatMonitor(): invalid refresh rate %d, set to %d", requested_refresh,
             lowest_matching_refresh);
  state.refresh_rate = lowest_matching_refresh;
}

std::string GetPendingOrCurrentDisplayCVar(openwow::ui::game::CVarSystem &sys,
                                           const std::string_view name) {
  const std::string pending = sys.GetPendingValue(std::string(name));
  if (!pending.empty()) {
    return pending;
  }
  return sys.GetCVar(std::string(name));
}

GxDisplayCVarState BuildRequestedDisplayCVarState(openwow::ui::game::CVarSystem &sys) {
  GxDisplayCVarState state;

  state.color_bits = CanonicalizeRetailColorBits(GetPendingOrCurrentDisplayCVar(sys, "gxColorBits"),
                                                 state.color_bits);
  state.depth_bits = CanonicalizeRetailDepthBits(GetPendingOrCurrentDisplayCVar(sys, "gxDepthBits"),
                                                 state.depth_bits);
  state.window_mode =
      NormalizeRetailBooleanDisplayCVar(GetPendingOrCurrentDisplayCVar(sys, "gxWindow"));
  const auto resolution = ParseDisplayResolution(
      GetPendingOrCurrentDisplayCVar(sys, "gxResolution"), state.width, state.height);
  state.width = resolution.first;
  state.height = resolution.second;
  state.refresh_rate =
      NormalizeRetailSignedDisplayCVar(GetPendingOrCurrentDisplayCVar(sys, "gxRefresh"));
  state.triple_buffer =
      NormalizeRetailSignedDisplayCVar(GetPendingOrCurrentDisplayCVar(sys, "gxTripleBuffer"));
  state.v_sync = NormalizeRetailSignedDisplayCVar(GetPendingOrCurrentDisplayCVar(sys, "gxVSync"));
  state.aspect = NormalizeRetailBooleanDisplayCVar(GetPendingOrCurrentDisplayCVar(sys, "gxAspect"));
  state.maximize =
      NormalizeRetailBooleanDisplayCVar(GetPendingOrCurrentDisplayCVar(sys, "gxMaximize"));
  state.cursor = NormalizeRetailBooleanDisplayCVar(GetPendingOrCurrentDisplayCVar(sys, "gxCursor"));
  state.multisample =
      NormalizeRetailMultisample(GetPendingOrCurrentDisplayCVar(sys, "gxMultisample"));
  state.multisample_quality = NormalizeRetailMultisampleQuality(
      GetPendingOrCurrentDisplayCVar(sys, "gxMultisampleQuality"));
  state.fix_lag =
      NormalizeRetailBooleanDisplayCVar(GetPendingOrCurrentDisplayCVar(sys, "gxFixLag"));
  return state;
}

bool GxCVar_StereoConvergenceValidationCallback(const std::string &, const std::string &,
                                                const std::string &new_value) {

  s_gx_stereo_runtime.convergence = ParseStereoFloatValue(new_value);
  return true;
}

bool GxCVar_StereoSeparationValidationCallback(const std::string &, const std::string &,
                                               const std::string &new_value) {

  s_gx_stereo_runtime.separation = ParseStereoFloatValue(new_value);
  return true;
}

void SyncStereoCVarStateToRendererState(openwow::ui::game::CVarSystem &sys) {
  if (sys.Exists("gxStereoEnabled")) {
    const bool enabled = ParseStereoEnabledValue(sys.GetCVar("gxStereoEnabled"));
    s_gx_stereo_runtime.enabled = enabled;
  }
}

void CaptureStartupHardwareDetectionState(openwow::ui::game::CVarSystem &sys,
                                          const openwow::vfs::VirtualFileSystem *vfs) {
  if (s_startup_hardware_state_captured) {
    return;
  }

  if (!s_hw_detected) {
    DetectHardware(vfs);
  }

  (void)RefreshStartupGraphicsQualityProfileFromDetectedHardware();

  const auto &startup_cmd = openwow::core::StormCmd::Instance();
  const bool command_line_hw_detect =
      startup_cmd.IsCommandEnabled(openwow::core::StartupCommandId::kHwDetect);
  const bool cvar_hw_detect = sys.Exists("hwDetect") && sys.GetCVarInt("hwDetect") != 0;

  s_startup_hw_detect_requested = command_line_hw_detect || cvar_hw_detect;
  if (s_startup_hw_detect_requested && sys.Exists("hwDetect")) {
    (void)sys.SetCVar("hwDetect", "0", true);
  }

  s_startup_hardware_state_captured = true;
}

std::string FormatDisplayInteger(int value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%d", value);
  return buffer;
}

std::string FormatDisplayResolution(int width, int height) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%dx%d", width, height);
  return buffer;
}

std::string FormatDisplayFloat(float value) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%f", static_cast<double>(value));
  return buffer;
}

int ParseDisplayInteger(const std::string &text, int fallback) {
  if (text.empty()) {
    return fallback;
  }

  try {
    return std::stoi(text);
  } catch (...) {
    return fallback;
  }
}

float ParseDisplayFloat(const std::string &text, float fallback) {
  if (text.empty()) {
    return fallback;
  }

  try {
    return std::stof(text);
  } catch (...) {
    return fallback;
  }
}

std::pair<int, int> ParseDisplayResolution(const std::string &text, int fallback_width,
                                           int fallback_height) {
  int width = fallback_width;
  int height = fallback_height;
  if (std::sscanf(text.c_str(), "%dx%d", &width, &height) == 2) {
    return {width, height};
  }

  return {fallback_width, fallback_height};
}

std::string GetDefaultDisplaySeedValue(openwow::ui::game::CVarSystem &sys, const char *name,
                                       const char *fallback) {
  const std::string key = name;
  if (sys.HasCVarDefault(key)) {
    return sys.GetCVarDefault(key);
  }

  const std::string current_value = sys.GetCVar(key);
  if (!current_value.empty()) {
    return current_value;
  }

  return fallback;
}

GxDisplayCVarState BuildRegisteredDefaultDisplayState() {
  auto &sys = openwow::ui::game::CVarSystem::Instance();

  GxDisplayCVarState state;
  state.color_bits = GetDefaultDisplaySeedValue(sys, "gxColorBits", state.color_bits.c_str());
  state.depth_bits = GetDefaultDisplaySeedValue(sys, "gxDepthBits", state.depth_bits.c_str());
  state.window_mode =
      ParseDisplayInteger(GetDefaultDisplaySeedValue(sys, "gxWindow", "0"), state.window_mode);
  const auto [width, height] = ParseDisplayResolution(
      GetDefaultDisplaySeedValue(sys, "gxResolution", "1024x768"), state.width, state.height);
  state.width = width;
  state.height = height;
  state.refresh_rate =
      ParseDisplayInteger(GetDefaultDisplaySeedValue(sys, "gxRefresh", "75"), state.refresh_rate);
  state.triple_buffer = ParseDisplayInteger(GetDefaultDisplaySeedValue(sys, "gxTripleBuffer", "0"),
                                            state.triple_buffer);
  state.v_sync = ParseDisplayInteger(GetDefaultDisplaySeedValue(sys, "gxVSync", "1"), state.v_sync);
  state.aspect =
      ParseDisplayInteger(GetDefaultDisplaySeedValue(sys, "gxAspect", "1"), state.aspect);
  state.maximize =
      ParseDisplayInteger(GetDefaultDisplaySeedValue(sys, "gxMaximize", "0"), state.maximize);
  state.cursor =
      ParseDisplayInteger(GetDefaultDisplaySeedValue(sys, "gxCursor", "1"), state.cursor);
  state.multisample =
      ParseDisplayInteger(GetDefaultDisplaySeedValue(sys, "gxMultisample", "1"), state.multisample);
  state.multisample_quality = ParseDisplayFloat(
      GetDefaultDisplaySeedValue(sys, "gxMultisampleQuality", "0.0"), state.multisample_quality);
  state.fix_lag =
      ParseDisplayInteger(GetDefaultDisplaySeedValue(sys, "gxFixLag", "0"), state.fix_lag);
  return state;
}

GxRegistrationDefaults BuildGxRegistrationDefaults() {
  GxRegistrationDefaults defaults;
  if (IsSimplifiedChineseClientLocale()) {
    defaults.window = "1";
    defaults.maximize = "1";
  }

  const auto default_resolution =
      openwow::ui::display::platform::DefaultScreenResolution();
  if (default_resolution.width > 0 && default_resolution.height > 0) {
    defaults.resolution =
        FormatDisplayResolution(default_resolution.width, default_resolution.height);
  }

  defaults.api = GetRetailDefaultGxApiCVarValue();
  return defaults;
}

std::string ConsumeDelimitedToken(std::string_view &cursor, const std::string_view delimiters) {
  while (!cursor.empty() && delimiters.find(cursor.front()) != std::string_view::npos) {
    cursor.remove_prefix(1);
  }

  std::string token;
  while (!cursor.empty() && delimiters.find(cursor.front()) == std::string_view::npos) {
    if (token.size() < 0xFFu) {
      token.push_back(cursor.front());
    }
    cursor.remove_prefix(1);
  }

  if (!cursor.empty()) {
    cursor.remove_prefix(1);
  }

  return token;
}

int ParseGxOverrideNumber(const std::string &token) {
  return static_cast<int>(std::strtol(token.c_str(), nullptr, 10));
}

int RemapTextureFilterOverride(const int value) {
  switch (value) {
  case 0:
  case 1:
  case 2:
    return 1;
  case 3:
    return 2;
  case 4:
    return 3;
  case 5:
    return 7;
  case 6:
    return 8;
  case 7:
    return 9;
  case 8:
    return 10;
  case 10:
    return 12;
  case 11:
    return 13;
  default:
    return value;
  }
}

void ParseAndStoreGxOverrideString(std::string_view command_string) {
  while (!command_string.empty()) {
    const std::string slot_token = ConsumeDelimitedToken(command_string, " ,");
    const std::string value_token = ConsumeDelimitedToken(command_string, " ;");
    if (slot_token.empty() || value_token.empty()) {
      continue;
    }

    const int slot = ParseGxOverrideNumber(slot_token);
    if (slot < 0 || slot >= static_cast<int>(kGxOverrideSlotCount)) {
      continue;
    }

    int value = ParseGxOverrideNumber(value_token);
    if (slot == 0) {
      value = RemapTextureFilterOverride(value);
    }

    s_gx_device_override_state.enabled[static_cast<std::size_t>(slot)] = true;
    s_gx_device_override_state.values[static_cast<std::size_t>(slot)] = value;
  }
}

void ParseCurrentGxOverrideCVar(openwow::ui::game::CVarSystem &sys) {
  if (!sys.Exists("gxOverride")) {
    return;
  }

  ParseAndStoreGxOverrideString(sys.GetCVar("gxOverride"));
}

void ApplyStartupCommandLineOverridesToGxCVars() {
  auto &sys = openwow::ui::game::CVarSystem::Instance();
  const auto &startup_cmd = openwow::core::StormCmd::Instance();

  if (startup_cmd.IsCommandEnabled(openwow::core::StartupCommandId::kResolution800x600)) {
    sys.SetCVar("gxResolution", "800x600", true);
  } else if (startup_cmd.IsCommandEnabled(openwow::core::StartupCommandId::kResolution1024x768)) {
    sys.SetCVar("gxResolution", "1024x768", true);
  } else if (startup_cmd.IsCommandEnabled(openwow::core::StartupCommandId::kResolution1280x960)) {
    sys.SetCVar("gxResolution", "1280x960", true);
  } else if (startup_cmd.IsCommandEnabled(openwow::core::StartupCommandId::kResolution1280x1024)) {
    sys.SetCVar("gxResolution", "1280x1024", true);
  } else if (startup_cmd.IsCommandEnabled(openwow::core::StartupCommandId::kResolution1600x1200)) {
    sys.SetCVar("gxResolution", "1600x1200", true);
  }

  if (startup_cmd.IsCommandEnabled(openwow::core::StartupCommandId::kFullscreen)) {
    sys.SetCVar("gxWindow", "0", true);
  } else if (startup_cmd.IsCommandEnabled(openwow::core::StartupCommandId::kWindowed)) {
    sys.SetCVar("gxWindow", "1", true);
  }

  if (startup_cmd.IsCommandEnabled(openwow::core::StartupCommandId::kOpenGl)) {
    sys.SetCVar("gxApi", "OpenGL", true);
  }
  if (startup_cmd.IsCommandEnabled(openwow::core::StartupCommandId::kD3D)) {
    sys.SetCVar("gxApi", "D3D9", true);
  }
  if (startup_cmd.IsCommandEnabled(openwow::core::StartupCommandId::kD3D9Ex)) {
    sys.SetCVar("gxApi", "D3D9Ex", true);
  }

  const std::string gx_override =
      startup_cmd.GetCommandString(openwow::core::StartupCommandId::kGxOverride);
  if (!gx_override.empty()) {
    sys.SetCVar("gxOverride", gx_override, true);
  }
}

bool GxCVar_StereoEnabledCallback(void * , const char * , const char *new_val,
                                  int ) {
  const bool enabled = ParseStereoEnabledValue(new_val ? new_val : "");
  s_gx_stereo_runtime.enabled = enabled;
  openwow::debug::DebugConsole::Get().Write("set pending gxRestart");
  return true;
}

bool GxCVar_RefreshValidationCallback(void * , const char * ,
                                      const char *new_val, int ) {
  const int requested_refresh =
      static_cast<int>(openwow::core::ParseUnsignedDecimalLikeSub76F140(new_val ? new_val : ""));
  if (!PrimaryDisplayModesContainRefreshRate(requested_refresh)) {
    openwow::debug::DebugConsole::Get().Write("Unsupported refresh rate");
    return false;
  }

  EmitPendingGxRestartLine();
  return true;
}

bool WindowResizeLockCVarValidationCallback(const std::string &, const std::string &,
                                            const std::string &new_value) {
  (void)ApplyWindowResizeLockValue(new_value, nullptr);
  return true;
}

bool AllowMultisampleFboCVarValidationCallback(const std::string &, const std::string &,
                                                const std::string &new_value) {
  s_allow_multisample_fbo = std::atoi(new_value.c_str());
  EmitPendingGxRestartLine();
  return true;
}

bool UseNvShadersCVarValidationCallback(const std::string &, const std::string &,
                                        const std::string &) {
  if (openwow::render::GetRendererType() != openwow::render::RendererType::OpenGL) {
    openwow::debug::DebugConsole::Get().Write(
        "Current gxApi is not GLL - the value is only relevant when using GLL gxApi.  "
        "The new value will still be set.");
  }
  openwow::debug::DebugConsole::Get().Write(
      "New value will take effect when app is restarted.");
  return true;
}

int GetAllowMultisampleFbo() {
  return s_allow_multisample_fbo;
}

bool GxApplyPendingDisplayCVars() {
  auto &sys = openwow::ui::game::CVarSystem::Instance();

  bool applied_any = false;
  for (const std::string_view name : kPendingDisplayCVarNames) {
    applied_any = sys.ApplyPendingValue(std::string(name)) || applied_any;
  }
  return applied_any;
}

void ValidateFormatMonitor(GxDisplayCVarState &state) {
  const auto display_modes =
      openwow::ui::display::platform::AvailableDisplayModes();
  ValidateMonitorResolutionAgainstDisplayModes(state, display_modes);
  ValidateMonitorRefreshAgainstDisplayModes(state, display_modes);
}

bool GxApplyDisplayCVarState(const GxDisplayCVarState &state) {
  auto &sys = openwow::ui::game::CVarSystem::Instance();

  (void)sys.SetRegisteredCVarValueDirect("gxColorBits", state.color_bits);
  (void)sys.SetRegisteredCVarValueDirect("gxDepthBits", state.depth_bits);
  (void)sys.SetRegisteredCVarValueDirect("gxWindow", FormatDisplayInteger(state.window_mode));
  (void)sys.SetRegisteredCVarValueDirect("gxResolution",
                                         FormatDisplayResolution(state.width, state.height));
  (void)sys.SetRegisteredCVarValueDirect("gxRefresh", FormatDisplayInteger(state.refresh_rate));
  (void)sys.SetRegisteredCVarValueDirect("gxTripleBuffer",
                                         state.triple_buffer > 0 ? "1" : "0");
  (void)sys.SetRegisteredCVarValueDirect("gxVSync", FormatDisplayInteger(state.v_sync));
  (void)sys.SetRegisteredCVarValueDirect("gxAspect", FormatDisplayInteger(state.aspect));
  (void)sys.SetRegisteredCVarValueDirect("gxMaximize", FormatDisplayInteger(state.maximize));
  (void)sys.SetRegisteredCVarValueDirect("gxCursor", FormatDisplayInteger(state.cursor));
  (void)sys.SetRegisteredCVarValueDirect("gxMultisample", FormatDisplayInteger(state.multisample));
  (void)sys.SetRegisteredCVarValueDirect("gxMultisampleQuality",
                                         FormatDisplayFloat(state.multisample_quality));
  (void)sys.SetRegisteredCVarValueDirect("gxFixLag", FormatDisplayInteger(state.fix_lag));
  return GxApplyPendingDisplayCVars();
}

bool GxApplyRegisteredDefaultDisplayCVars() {
  return GxApplyDisplayCVarState(BuildRegisteredDefaultDisplayState());
}

bool ApplyGxRestart(const bool parse_gx_override) {
  auto &sys = openwow::ui::game::CVarSystem::Instance();
  if (parse_gx_override) {
    ParseCurrentGxOverrideCVar(sys);
  }
  const GxDisplayCVarState requested_state =
      BuildValidatedDisplayState(BuildRequestedDisplayCVarState(sys));

  if (const auto applied_state = TryApplyDisplayState(requested_state)) {
    s_gx_restart_runtime.active_state = *applied_state;
    s_gx_restart_runtime.last_good_state = *applied_state;
    (void)GxApplyPendingDisplayCVars();
  } else {
    EmitDisplayRestartFailure(kUnableToSetRequestedDisplayMode);

    if (const auto applied_last_good = TryApplyDisplayState(s_gx_restart_runtime.last_good_state)) {
      s_gx_restart_runtime.active_state = *applied_last_good;
      (void)GxApplyDisplayCVarState(*applied_last_good);
    } else {
      EmitDisplayRestartFailure(kUnableToSetLastGoodMode);

      if (const auto applied_default = TryApplyDisplayState(s_gx_restart_runtime.default_state)) {
        s_gx_restart_runtime.active_state = *applied_default;
        s_gx_restart_runtime.last_good_state = *applied_default;
        (void)GxApplyDisplayCVarState(*applied_default);
      } else {
        EmitDisplayRestartFailure(kUnableToSetDefaultFormat);

        if (const auto applied_validated_default =
                TryApplyDisplayState(s_gx_restart_runtime.validated_default_state)) {
          s_gx_restart_runtime.active_state = *applied_validated_default;
          s_gx_restart_runtime.last_good_state = *applied_validated_default;
          (void)GxApplyDisplayCVarState(*applied_validated_default);
        }
      }
    }
  }

  s_pending_gx_restart = false;
  return true;
}

bool ConsoleCommand_GxRestartImpl() {
  return ApplyGxRestart(true);
}

bool GxRestartCurrentDisplayMode() {

  return ApplyGxRestart(false);
}

void GxCVarRegister() {
  using F = openwow::ui::game::CVarFlags;
  auto &sys = openwow::ui::game::CVarSystem::Instance();
  const auto kPendingDisplayFlags = F::Archive | F::ReadOnly;
  const GxRegistrationDefaults defaults = BuildGxRegistrationDefaults();

  const auto register_cvar = [&](const char *name, const std::string &default_value, const F flags,
                                 const char *description,
                                 const openwow::ui::game::CVarValidationCallback &callback = {}) {
    const bool has_startup_value = sys.HasStartupValue(name);
    sys.RegisterNativeCVar(name, default_value, flags, description, callback,
                           0.0f, 0.0f, 1);
    if (!has_startup_value) {
      (void)sys.SetCVar(name, default_value, true);
    }
  };

  register_cvar("widescreen", "1", F::Archive, "Allow widescreen support");
  register_cvar("gxWindow", defaults.window, kPendingDisplayFlags, "toggle fullscreen/window",
                GxCVar_WindowModeValidationCallback);
  register_cvar("gxMaximize", defaults.maximize, kPendingDisplayFlags, "maximize game window",
                GxCVar_MaximizeValidationCallback);
  register_cvar("gxColorBits", defaults.color_bits, kPendingDisplayFlags, "color bits",
                GxCVar_ColorBitsValidationCallback);
  register_cvar("gxDepthBits", defaults.depth_bits, kPendingDisplayFlags, "depth bits",
                GxCVar_DepthBitsValidationCallback);
  register_cvar("gxResolution", defaults.resolution, kPendingDisplayFlags, "resolution",
                [](const std::string &, const std::string &, const std::string &new_value) {
                  return CGxMonitorMode_Validate(nullptr, nullptr, new_value.c_str(), 0);
                });
  register_cvar("gxRefresh", defaults.refresh, kPendingDisplayFlags, "refresh rate",
                [](const std::string &, const std::string &, const std::string &new_value) {
                  return GxCVar_RefreshValidationCallback(nullptr, nullptr, new_value.c_str(), 0);
                });
  register_cvar("gxTripleBuffer", defaults.triple_buffer, kPendingDisplayFlags, "triple buffer",
                GxCVar_TripleBufferValidationCallback);
  register_cvar("gxApi", defaults.api, kPendingDisplayFlags, "graphics api",
                GxCVar_ApiValidationCallback);
  register_cvar("gxVSync", defaults.v_sync, kPendingDisplayFlags, "vsync on or off",
                GxCVar_VSyncValidationCallback);
  register_cvar("gxAspect", defaults.aspect, kPendingDisplayFlags, "constrain window aspect",
                GxCVar_AspectValidationCallback);
  register_cvar("gxCursor", defaults.cursor, kPendingDisplayFlags, "toggle hardware cursor",
                GxCVar_CursorValidationCallback);
  register_cvar("gxMultisample", defaults.multisample, kPendingDisplayFlags, "multisample",
                GxCVar_MultisampleValidationCallback);
  register_cvar("gxMultisampleQuality", defaults.multisample_quality, kPendingDisplayFlags,
                "multisample quality", GxCVar_MultisampleQualityValidationCallback);
  register_cvar("gxFixLag", defaults.fix_lag, kPendingDisplayFlags, "prevent cursor lag",
                GxCVar_FixLagValidationCallback);
  register_cvar("gxStereoEnabled", "0", F::Archive, "Enable stereoscopic rendering",
                [](const std::string &, const std::string &, const std::string &new_value) {
                  return GxCVar_StereoEnabledCallback(nullptr, nullptr, new_value.c_str(), 0);
                });
  register_cvar("gxOverride", "", F::Archive, "gx overrides", GxCVar_OverrideValidationCallback);
  register_cvar("maxFPS", "200", F::Archive, "Set FPS limit", GxCVar_MaxFPSValidationCallback);
  register_cvar("maxFPSBk", "30", F::Archive, "Set background FPS limit",
                GxCVar_MaxFPSBkValidationCallback);
  register_cvar("videoOptionsVersion", "0", kPendingDisplayFlags, "Video options version");
  register_cvar("windowResizeLock", "0", F::Archive, "prevent resizing in windowed mode",
                WindowResizeLockCVarValidationCallback);
  register_cvar("AllowMultisampleFBO", "1", F::None,
                "Allow use of FBO's when rendering to multisampled back buffer",
                AllowMultisampleFboCVarValidationCallback);
  register_cvar(
      "UseNVShaders", "1", F::Archive,
      "Enable/Disable use of nvvp3 and nvfp2 shaders.  Set to 1 to enable on nVidia cards, "
      "set to 2 to force these shaders even on ATi Cards. Only relevant when using the GLL "
      "gxApi.",
      UseNvShadersCVarValidationCallback);
  register_cvar("fixedFunction", "0", kPendingDisplayFlags, "Force fixed function rendering");
}

void GxCVarInitializeRuntime(const char *window_title) {
  auto &sys = openwow::ui::game::CVarSystem::Instance();
  auto &console = openwow::debug::DebugConsole::Get();

  ApplyStartupCommandLineOverridesToGxCVars();
  console.RegisterCommand("gxRestart", "Restart graphics subsystem",
                          [](const std::vector<std::string> & ) -> std::string {
                            (void)ConsoleCommand_GxRestartImpl();
                            return {};
                          }, 0, "", 1);

  sys.RegisterNativeCVar("gxStereoConvergence", "1",
                         openwow::ui::game::CVarFlags::Archive,
                         "Set stereoscopic rendering convergence depth",
                         GxCVar_StereoConvergenceValidationCallback, 0.0f,
                         0.0f, 1);
  sys.RegisterNativeCVar("gxStereoSeparation", "25",
                         openwow::ui::game::CVarFlags::Archive,
                         "Set stereoscopic rendering separation percentage",
                         GxCVar_StereoSeparationValidationCallback, 0.0f,
                         0.0f, 1);
  SyncStereoCVarStateToRendererState(sys);

  (void)sys.SetCVar("videoOptionsVersion", "3", true);

  SeedGxRestartRuntime(sys, window_title);
  ParseCurrentGxOverrideCVar(sys);
  SetConsoleInputRoutingInitialized(openwow::core::StormCmd::Instance().IsCommandEnabled(
      openwow::core::StartupCommandId::kConsole));
}

void RegisterWindowResizeLockCVarCallback(openwow::ui::game::CVarSystem &cvars,
                                          SDL_Window *window) {
  auto &callback_state = GetWindowResizeLockCallbackState();
  {
    std::lock_guard lock(callback_state.mutex);
    callback_state.cvars = &cvars;
    callback_state.window = window;
  }
  cvars.SetValidationCallback("windowResizeLock", WindowResizeLockCVarValidationCallback);
}

bool ApplyCurrentWindowResizeLockCVar(openwow::ui::game::CVarSystem &cvars, SDL_Window *window) {
  if (!cvars.Exists("windowResizeLock")) {
    return ApplyWindowResizeLockValue("0", window);
  }

  return ApplyWindowResizeLockValue(cvars.GetCVar("windowResizeLock"), window);
}

void ClearWindowResizeLockBoundWindow() {
  auto &callback_state = GetWindowResizeLockCallbackState();
  std::lock_guard lock(callback_state.mutex);
  callback_state.window = nullptr;
}

void ConsoleDeviceInitialize(const char *window_title) {
  auto &sys = openwow::ui::game::CVarSystem::Instance();

  sys.RegisterCVar("hwDetect", "1",
                   openwow::ui::game::CVarFlags::Archive,
                   "do hardware detection", 0.0f, 0.0f, 1);

  DetectHardware(nullptr);
  CaptureStartupHardwareDetectionState(sys, nullptr);

  GxCVarRegister();
  GxCVarInitializeRuntime(window_title);
}

MacHardwareClassification ClassifyMacHardware(
    const MacHardwareSpecs &specs) {
  MacHardwareClassification result;
  result.video_memory_mb =
      static_cast<std::uint32_t>(specs.video_memory_bytes >> 20u);

  result.has_ddr_memory =
      !(specs.bus_frequency_hz < 110000001ull ||
        (specs.bus_frequency_hz < 140000001ull &&
         (specs.l3_cache_bytes < 0x100000ull ||
          specs.l2_cache_bytes < 0x80000ull)));

  if (!specs.required_sysctls_available) {
    return result;
  }

  std::uint32_t tier = 3;
  if (specs.cpu_count < 2u) {
    tier = 2;
  }
  if (specs.bus_frequency_hz < 500000000ull) {
    tier = 2;
  }
  if (specs.memory_bytes < 0x30000000ull) {
    tier = 2;
  }
  if (specs.cpu_frequency_hz < 1100000000ull) {
    tier = 1;
  }
  if (specs.video_memory_bytes < 0x4000000ull) {
    tier = 1;
  }
  if (specs.cpu_type == 0x12 && specs.cpu_subtype < 0x0B) {
    tier = 0;
  }
  if (specs.video_memory_bytes < 0x2000001ull) {
    tier = 0;
  }
  if (!result.has_ddr_memory) {
    tier = 0;
  }
  if (specs.l2_cache_bytes < 0x40001ull &&
      specs.l3_cache_bytes < 0x100000ull) {
    tier = 0;
  }
  if (specs.memory_bytes < 0x20000000ull) {
    tier = 0;
    result.cpu_idx = 0;
  } else {
    result.cpu_idx = tier >> 1u;
  }
  result.tier = tier;
  return result;
}

#if defined(__APPLE__)
template <typename T>
bool ReadMacHardwareSysctl(const char *name, T &value) {
  std::size_t size = sizeof(value);
  return ::sysctlbyname(name, &value, &size, nullptr, 0) == 0 &&
         size == sizeof(value);
}

MacHardwareSpecs CollectMacHardwareSpecs(
    const std::uint64_t video_memory_bytes) {
  MacHardwareSpecs specs;
  specs.video_memory_bytes = video_memory_bytes;

  const bool has_cpu_type = ReadMacHardwareSysctl("hw.cputype", specs.cpu_type);
  const bool has_cpu_subtype =
      ReadMacHardwareSysctl("hw.cpusubtype", specs.cpu_subtype);
  const bool has_cpu_count = ReadMacHardwareSysctl("hw.ncpu", specs.cpu_count);
  const bool has_cpu_frequency =
      ReadMacHardwareSysctl("hw.cpufrequency_max", specs.cpu_frequency_hz);
  const bool has_bus_frequency =
      ReadMacHardwareSysctl("hw.busfrequency_max", specs.bus_frequency_hz);
  (void)ReadMacHardwareSysctl("hw.l2cachesize", specs.l2_cache_bytes);
  (void)ReadMacHardwareSysctl("hw.l3cachesize", specs.l3_cache_bytes);
  const bool has_memory = ReadMacHardwareSysctl("hw.memsize", specs.memory_bytes);
  specs.required_sysctls_available =
      has_cpu_type && has_cpu_subtype && has_cpu_count &&
      has_cpu_frequency && has_bus_frequency && has_memory;
  return specs;
}
#endif

void DetectHardware(const openwow::vfs::VirtualFileSystem *vfs) {
  s_hw_info = {};
  s_startup_graphics_quality_profile = {};
  s_startup_graphics_quality_profile_initialized = false;

  const auto video_hardware = LoadVideoHardwareDbcFile(vfs);

  const auto processor_frequency_hz = openwow::platform::OS_GetProcessorFrequency();
  s_hw_info.cpu_idx = processor_frequency_hz > 1500000000ull ? 1u : 0u;

  openwow::core::DisplayAdapterIdentity adapter_identity{};
  const bool has_adapter_identity =
      openwow::core::PlatformLayer::TryGetPrimaryDisplayAdapterIdentity(
          adapter_identity);
  if (has_adapter_identity) {
    s_hw_info.vendor_id = adapter_identity.vendor_id;
    s_hw_info.device_id = adapter_identity.device_id;
    s_hw_info.driver_hi = adapter_identity.driver_hi;
    s_hw_info.driver_lo = adapter_identity.driver_lo;
  }

#if defined(__APPLE__)
  const MacHardwareSpecs mac_specs = CollectMacHardwareSpecs(
      has_adapter_identity ? adapter_identity.video_memory_bytes : 0u);
  const MacHardwareClassification mac_classification =
      ClassifyMacHardware(mac_specs);
  s_hw_info.mac_tier = mac_classification.tier;
  s_hw_info.mac_vram_mb = mac_classification.video_memory_mb;
  s_hw_info.mac_has_ddr_memory = mac_classification.has_ddr_memory;
  s_hw_info.mac_classification_available =
      mac_specs.required_sysctls_available;
  if (s_hw_info.mac_classification_available) {
    s_hw_info.cpu_idx = mac_classification.cpu_idx;
  }
#endif

  if (!TryPopulateVideoHardwareProfile(video_hardware, s_hw_info.vendor_id, s_hw_info.device_id,
                                       s_hw_info) &&
      !TryPopulateVideoHardwareProfile(video_hardware, kWildCardVideoHardwareVendorId,
                                       s_hw_info.device_id, s_hw_info)) {

    std::fprintf(stderr, "[OpenWoW] GPU not found in VideoHardware.dbc (vendor=%u, device=%u), using default settings\n",
                 s_hw_info.vendor_id, s_hw_info.device_id);
    s_hw_info.video_id = 0;
    s_hw_info.has_video_profile = true;
    s_hw_info.startup_farclip_index = 1;
    s_hw_info.startup_shadow_level = 1;
    s_hw_info.startup_ground_effect_density_index = 1;
    s_hw_info.startup_ui_faster_enabled = true;
    s_hw_info.startup_max_lights = 4;
    s_hw_info.startup_specular_enabled = true;
    s_hw_info.startup_water_lod_index = 1;
    s_hw_info.startup_particle_density_index = 1;
    s_hw_info.startup_base_mip = 0;
    s_hw_info.video_profile_disables_texture_atlas = false;
  }

  s_hw_detected = true;
  s_hw_changed = DetectHardware_CheckStoredHardwareAndPromptReset(s_hw_info);
}

void InitializeStartupHardwareDetectionState(const openwow::vfs::VirtualFileSystem *vfs) {
  auto &sys = openwow::ui::game::CVarSystem::Instance();
  CaptureStartupHardwareDetectionState(sys, vfs);
}

const HardwareInfo *GetDetectedHardwareInfoIfReady() {
  if (!s_hw_detected) {
    return nullptr;
  }

  return &s_hw_info;
}

bool RefreshStartupGraphicsQualityProfileFromDetectedHardware() {
  if (s_startup_graphics_quality_profile_test_override) {
    return true;
  }

  if (!s_hw_detected || !s_hw_info.has_video_profile) {
    return false;
  }

  const std::size_t quality_column = s_hw_info.cpu_idx != 0 ? 1u : 0u;
  StartupGraphicsQualityProfile profile;
  profile.farclip = RequireStartupQualityValue(
      kStartupFarclipTable,
      static_cast<std::size_t>(2u * s_hw_info.startup_farclip_index + quality_column), "farclip");
  profile.shadow_level = s_hw_info.startup_shadow_level;
  profile.ground_effect_density = RequireStartupQualityValue(
      kStartupGroundEffectDensityTable,
      static_cast<std::size_t>(s_hw_info.startup_ground_effect_density_index),
      "groundEffectDensity");
  profile.ui_faster_enabled = s_hw_info.startup_ui_faster_enabled;
  profile.max_lights = s_hw_info.startup_max_lights;
  profile.specular_enabled = s_hw_info.startup_specular_enabled;
  profile.water_lod = RequireStartupQualityValue(
      kStartupWaterLodTable,
      static_cast<std::size_t>(2u * s_hw_info.startup_water_lod_index + quality_column),
      "waterLOD");
  profile.particle_density = RequireStartupQualityValue(
      kStartupParticleDensityTable,
      static_cast<std::size_t>(2u * s_hw_info.startup_particle_density_index + quality_column),
      "particleDensity");
  profile.base_mip = s_hw_info.startup_base_mip;

  s_startup_graphics_quality_profile = profile;
  s_startup_graphics_quality_profile_initialized = true;
  return true;
}

const StartupGraphicsQualityProfile *GetStartupGraphicsQualityProfile() {
  if (!s_startup_graphics_quality_profile_test_override &&
      !s_startup_graphics_quality_profile_initialized) {
    (void)RefreshStartupGraphicsQualityProfileFromDetectedHardware();
  }

  return &s_startup_graphics_quality_profile;
}

bool ShouldReplayStartupDisplaySettings() {
  return s_startup_hw_detect_requested || s_hw_changed;
}

void SetDetectedHardwareInfoForTests(const HardwareInfo &hardware_info) {
  s_hw_info = hardware_info;
  s_hw_detected = true;
  s_startup_graphics_quality_profile_initialized = false;
}

void ClearDetectedHardwareInfoForTests() {
  s_hw_info = {};
  s_hw_detected = false;
  s_hw_changed = false;
  s_startup_hw_detect_requested = false;
  s_startup_hardware_state_captured = false;
  s_startup_graphics_quality_profile = {};
  s_startup_graphics_quality_profile_initialized = false;
  s_startup_graphics_quality_profile_test_override = false;
  s_hardware_registry_overrides.reset();
  s_hardware_change_prompt_response_override.reset();
  openwow::core::PlatformLayer::ClearPrimaryDisplayAdapterIdentityOverrideForTests();
}

void SetHardwareChangedForTests(bool changed) {
  s_hw_detected = true;
  s_hw_changed = changed;
  s_startup_graphics_quality_profile_initialized = false;
}

void SetHardwareRegistryValueForTests(const char *value_name, const std::uint32_t value) {
  if (!value_name || !*value_name) {
    return;
  }

  if (!s_hardware_registry_overrides.has_value()) {
    s_hardware_registry_overrides.emplace();
  }
  (*s_hardware_registry_overrides)[value_name] = value;
}

bool GetHardwareRegistryValueForTests(const char *value_name, std::uint32_t *out_value) {
  return ReadHardwareRegistryValue(value_name, out_value);
}

void ClearHardwareRegistryValuesForTests() {
  s_hardware_registry_overrides.reset();
}

void SetHardwareChangePromptResponseForTests(const int response_code) {
  s_hardware_change_prompt_response_override = response_code;
}

void ClearHardwareChangePromptResponseForTests() {
  s_hardware_change_prompt_response_override.reset();
}

void SetStartupGraphicsQualityProfileForTests(const StartupGraphicsQualityProfile &profile) {
  s_startup_graphics_quality_profile = profile;
  s_startup_graphics_quality_profile_initialized = true;
  s_startup_graphics_quality_profile_test_override = true;
}

void ClearStartupGraphicsQualityProfileForTests() {
  s_startup_graphics_quality_profile = {};
  s_startup_graphics_quality_profile_initialized = false;
  s_startup_graphics_quality_profile_test_override = false;
}

GxRestartRuntimeState GetGxRestartRuntimeStateForTests() {
  return s_gx_restart_runtime;
}

void SetGxRestartRuntimeStateForTests(const GxRestartRuntimeState &state) {
  s_gx_restart_runtime = state;
  (void)ApplyDisplayStateToRuntimeObjects(s_gx_restart_runtime.active_state);
}

void SetGxRestartApplyResponsesForTests(std::vector<std::optional<GxDisplayCVarState>> responses) {
  s_gx_restart_apply_responses = std::move(responses);
  s_gx_restart_apply_response_index = 0;
}

void ClearGxRestartApplyResponsesForTests() {
  s_gx_restart_apply_responses.clear();
  s_gx_restart_apply_response_index = 0;
}

GxDeviceOverrideState GetGxDeviceOverrideStateForTests() {
  return s_gx_device_override_state;
}

void ResetGxDeviceOverrideStateForTests() {
  s_gx_device_override_state = {};
}

GxStereoRuntimeState GetGxStereoRuntimeStateForTests() {
  return s_gx_stereo_runtime;
}

void ResetGxStereoRuntimeStateForTests() {
  s_gx_stereo_runtime = {};
}

[[noreturn]] void FatalError_Exit(int error_id, const std::string &error_text) {
  std::fprintf(stderr, "FATAL ERROR (id=%d): %s\n", error_id, error_text.c_str());
  std::exit(1);
}

bool CGxMonitorMode_Validate(void * , const char * , const char *new_val,
                             int ) {
  if (!new_val) {
    return false;
  }

  openwow::ui::display::platform::ScreenResolution requested{
      .width = -1,
      .height = -1,
  };
  char sep = 0;

  std::sscanf(new_val, "%d%c%d", &requested.width, &sep, &requested.height);
  if (IsWindowedDisplayModeActive()) {
    EmitPendingGxRestartLine();
    return true;
  }

  const auto allowed_resolutions =
      openwow::ui::display::platform::BuildFullscreenResolutionCatalog(
          IsWidescreenDisplayModeEnabled(), true);
  const bool found = std::any_of(
      allowed_resolutions.begin(), allowed_resolutions.end(),
      [&requested](
          const openwow::ui::display::platform::ScreenResolution&
              resolution) {
        return resolution.width == requested.width && resolution.height == requested.height;
      });
  if (!found) {
    EmitInvalidResolutionCatalog(allowed_resolutions);
    return false;
  }

  EmitPendingGxRestartLine();
  return true;
}

const char *CommandHistoryGetRelativeEntry(const std::uint32_t relative_index) {
  const std::uint32_t slot =
      (static_cast<std::uint32_t>(s_history_write_cursor) - relative_index - 1u) &
      static_cast<std::uint32_t>(kHistoryRingSize - 1);
  return s_history_ring[slot];
}

bool CommandHistoryBrowseOlder(const char **out_entry) {
  if (out_entry == nullptr || s_history_browse_index == kHistoryRingSize - 1) {
    return false;
  }

  ++s_history_browse_index;
  *out_entry = CommandHistoryGetRelativeEntry(static_cast<std::uint32_t>(s_history_browse_index));
  return true;
}

bool CommandHistoryBrowseNewer(const char **out_entry) {
  if (out_entry == nullptr || s_history_browse_index == -1) {
    return false;
  }

  const int next_browse_index = s_history_browse_index - 1;
  *out_entry = next_browse_index >= 0
                   ? CommandHistoryGetRelativeEntry(static_cast<std::uint32_t>(next_browse_index))
                   : "";
  s_history_browse_index = next_browse_index;
  return true;
}

void ResetCommandHistoryNavigation() {
  s_history_browse_index = -1;
}

void CommandHistoryPush(const std::string &command) {
  std::strncpy(s_history_ring[s_history_write_cursor], command.c_str(), kHistoryEntryLen - 1);
  s_history_ring[s_history_write_cursor][kHistoryEntryLen - 1] = '\0';
  s_history_write_cursor = (s_history_write_cursor + 1) & (kHistoryRingSize - 1);
}

WindowResizeLockRuntimeState GetWindowResizeLockRuntimeStateForTests() {
  auto &callback_state = GetWindowResizeLockCallbackState();
  std::lock_guard lock(callback_state.mutex);
  return {
      .resize_locked = callback_state.resize_locked,
      .has_bound_window = callback_state.window != nullptr,
      .apply_count = callback_state.apply_count,
  };
}

void ResetWindowResizeLockRuntimeStateForTests() {
  auto &callback_state = GetWindowResizeLockCallbackState();
  std::lock_guard lock(callback_state.mutex);
  callback_state.cvars = nullptr;
  callback_state.window = nullptr;
  callback_state.resize_locked = false;
  callback_state.apply_count = 0;
}

void ResetCommandHistoryForTests() {
  std::memset(s_history_ring, 0, sizeof(s_history_ring));
  s_history_write_cursor = 0;
  s_history_browse_index = -1;
}

}

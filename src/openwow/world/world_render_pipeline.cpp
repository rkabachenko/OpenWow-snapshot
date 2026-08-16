#include "openwow/world/world_render_pipeline.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "openwow/core/console.h"
#include "openwow/debug/diagnostics/debug_console.h"

namespace openwow::world {

namespace {

constexpr std::uint32_t kCWorldInitializeRenderFlagDefaults = 0x07104B73u;

constexpr std::uint32_t kDetailDoodadAlphaDefault = 0x80u;

constexpr std::uint32_t kDetailDoodadAlphaExclusiveUpperBound = 0x100u;

constexpr float kCharacterAmbientMultiplyDefault = 1.0f;
constexpr bool kCharacterAmbientOverriddenDefault = false;

constexpr float kCharacterAmbientMinimum = 0.0f;
constexpr float kCharacterAmbientMaximum = 1.0f;

constexpr std::uint32_t kMaxTerrainLodDefault = 3u;
constexpr std::uint32_t kMaxTerrainLodFloor = 2u;
constexpr std::uint32_t kMaxTerrainLodExclusiveCeiling = 4u;
constexpr std::uint32_t kMaxTerrainLodCeiling = 3u;

constexpr bool kSimpleDoodadsDefault = false;

constexpr int kWaterRipplesDefault = 1;

constexpr float kShadowModComponentMinimum = 0.0f;
constexpr float kShadowModComponentMaximum = 1.0f;
constexpr float kShadowModComponentScale = 255.0f;
constexpr std::size_t kShadowModComponentCount = 4u;

constexpr float kTerrainShadowModDefaultComponent = 0.7f;
constexpr float kTerrainShadowModDefaultAlpha = 1.0f;

constexpr char kWaterParticulatesDisabledMessage[] = "Particulates disabled";
constexpr char kWaterParticulatesEnabledMessage[] = "Particulates enabled";
constexpr char kDetailDoodadsDisabledMessage[] = "Detail doodads disabled.";
constexpr char kDetailDoodadsEnabledMessage[] = "Detail doodads enabled.";
constexpr char kTerrainCullingDisabledMessage[] = "Terrain culling disabled.";
constexpr char kTerrainCullingEnabledMessage[] = "Terrain culling enabled.";
constexpr char kTerrainShadowDisabledMessage[] = "Terrain shadow disabled.";
constexpr char kTerrainShadowEnabledMessage[] = "Terrain shadow enabled.";
constexpr char kTerrainLowDetailDisabledMessage[] = "Terrain low detail disabled.";
constexpr char kTerrainLowDetailEnabledMessage[] = "Terrain low detail enabled.";
constexpr char kSimpleDoodadsDisabledMessage[] = "Simple doodads disabled.";
constexpr char kSimpleDoodadsEnabledMessage[] = "Simple doodads enabled.";
constexpr char kDetailDoodadAlphaRangeMessage[] = "Alpha ref range 0 - 255.";
constexpr char kCharacterAmbientRangeMessage[] = "Ambient multiply range 0.0 - 1.0.";
constexpr char kShadowModRangeMessage[] = "Color values must be in range (0.0,1.0).";

constexpr int kWorldRenderConsoleCategory = 1;

struct WorldRenderState {

  std::uint32_t render_flags = kCWorldInitializeRenderFlagDefaults;
  std::uint32_t detail_doodad_alpha = kDetailDoodadAlphaDefault;
  float character_ambient_multiply = kCharacterAmbientMultiplyDefault;
  bool character_ambient_overridden = kCharacterAmbientOverriddenDefault;
  std::uint32_t max_terrain_lod = kMaxTerrainLodDefault;
  bool simple_doodads_enabled = kSimpleDoodadsDefault;
  int water_ripples = kWaterRipplesDefault;
  std::array<float, 4> terrain_shadow_mod{
      kTerrainShadowModDefaultComponent, kTerrainShadowModDefaultComponent,
      kTerrainShadowModDefaultComponent, kTerrainShadowModDefaultAlpha};
};

WorldRenderState& State() {
  static WorldRenderState state;
  return state;
}

void WriteConsoleLine(const char* const text) {

  openwow::core::ida::ConsoleAddLine(text, openwow::core::ida::COLOR_DEFAULT);
}

int ToggleRenderFlag(const WorldRenderFlag flag, const char* const disabled_message,
                     const char* const enabled_message) {
  const bool was_enabled = CWorld_HasRenderFlag(flag);
  WriteConsoleLine(was_enabled ? disabled_message : enabled_message);
  CWorld_SetRenderFlag(flag, !was_enabled);
  return 1;
}

}

bool CWorld_HasRenderFlag(const WorldRenderFlag flag) {
  return (State().render_flags & static_cast<std::uint32_t>(flag)) != 0u;
}

void CWorld_SetRenderFlag(const WorldRenderFlag flag, const bool enabled) {
  auto& flags = State().render_flags;
  if (enabled) {
    flags |= static_cast<std::uint32_t>(flag);
  } else {
    flags &= ~static_cast<std::uint32_t>(flag);
  }
}

std::uint32_t CWorld_GetDetailDoodadAlpha() { return State().detail_doodad_alpha; }

float CWorld_GetCharacterAmbientMultiply() {
  return State().character_ambient_multiply;
}

bool CWorld_IsCharacterAmbientOverridden() {
  return State().character_ambient_overridden;
}

void CWorld_UpdateCharacterAmbientMultiply(const float area_ambient_multiplier,
                                           const float delta_time_seconds) {
  auto& state = State();
  if (state.character_ambient_overridden) {

    return;
  }

  const float target = area_ambient_multiplier + area_ambient_multiplier + 1.0f;
  const float delta = target - state.character_ambient_multiply;

  constexpr float kSnapEpsilon = 0.01f;
  constexpr float kSnapDeltaTimeSeconds = 1.0f;
  if (std::fabs(delta) <= kSnapEpsilon || delta_time_seconds >= kSnapDeltaTimeSeconds) {
    state.character_ambient_multiply = target;
    return;
  }

  state.character_ambient_multiply += delta * delta_time_seconds;
}

std::uint32_t CWorld_GetMaxTerrainLod() { return State().max_terrain_lod; }

bool CWorld_AreSimpleDoodadsEnabled() { return State().simple_doodads_enabled; }

int CWorld_GetWaterRippleSetting() { return State().water_ripples; }

bool CWorld_AreWaterRipplesEnabled() { return State().water_ripples != 0; }

std::array<float, 4> CWorld_GetTerrainShadowModColor() {
  return State().terrain_shadow_mod;
}

int CWorld_OnShowDetailDoodadsCommand() {
  return ToggleRenderFlag(WorldRenderFlag::kDetailDoodads, kDetailDoodadsDisabledMessage,
                          kDetailDoodadsEnabledMessage);
}

int CWorld_OnShowCullCommand() {
  return ToggleRenderFlag(WorldRenderFlag::kTerrainCulling, kTerrainCullingDisabledMessage,
                          kTerrainCullingEnabledMessage);
}

int CWorld_OnShowShadowCommand() {
  return ToggleRenderFlag(WorldRenderFlag::kTerrainShadows, kTerrainShadowDisabledMessage,
                          kTerrainShadowEnabledMessage);
}

int CWorld_OnShowLowDetailCommand() {
  return ToggleRenderFlag(WorldRenderFlag::kTerrainLowDetail, kTerrainLowDetailDisabledMessage,
                          kTerrainLowDetailEnabledMessage);
}

int CWorld_OnWaterParticulatesCommand() {
  return ToggleRenderFlag(WorldRenderFlag::kWaterParticulates,
                          kWaterParticulatesDisabledMessage, kWaterParticulatesEnabledMessage);
}

int CWorld_OnShowSimpleDoodadsCommand() {
  auto& state = State();
  WriteConsoleLine(state.simple_doodads_enabled ? kSimpleDoodadsDisabledMessage
                                                : kSimpleDoodadsEnabledMessage);
  state.simple_doodads_enabled = !state.simple_doodads_enabled;
  return 1;
}

int CWorld_OnMaxLodCommand(const char* const raw_args) {
  int parsed = 0;
  if (raw_args == nullptr || std::sscanf(raw_args, "%d", &parsed) != 1) {
    return 1;
  }

  auto value = static_cast<std::uint32_t>(parsed);
  if (value < kMaxTerrainLodExclusiveCeiling) {
    if (value < kMaxTerrainLodFloor) {
      value = kMaxTerrainLodFloor;
    }
  } else {
    value = kMaxTerrainLodCeiling;
  }
  State().max_terrain_lod = value;
  return 1;
}

int CWorld_OnWaterRipplesCommand(const char* const raw_args) {
  int parsed = 0;
  if (raw_args == nullptr || std::sscanf(raw_args, "%d", &parsed) != 1) {
    return 1;
  }

  State().water_ripples = parsed;
  return 1;
}

int CWorld_OnDetailDoodadAlphaCommand(const char* const raw_args) {
  int parsed = 0;
  if (raw_args == nullptr || std::sscanf(raw_args, "%d", &parsed) != 1) {
    return 1;
  }

  const auto value = static_cast<std::uint32_t>(parsed);
  if (value < kDetailDoodadAlphaExclusiveUpperBound) {
    State().detail_doodad_alpha = value;
    return 1;
  }

  WriteConsoleLine(kDetailDoodadAlphaRangeMessage);
  return 1;
}

int CWorld_OnCharacterAmbientCommand(const char* const raw_args) {
  float parsed = 0.0f;
  if (raw_args == nullptr || std::sscanf(raw_args, "%f", &parsed) != 1) {
    return 1;
  }

  if (parsed >= kCharacterAmbientMinimum && parsed <= kCharacterAmbientMaximum) {
    auto& state = State();
    state.character_ambient_overridden = parsed > kCharacterAmbientMinimum;
    state.character_ambient_multiply = parsed + parsed + 1.0f;
    return 1;
  }

  WriteConsoleLine(kCharacterAmbientRangeMessage);
  return 1;
}

int CWorld_OnSetShadowCommand(const char* const raw_args) {
  std::array<float, kShadowModComponentCount> parsed{};
  if (raw_args == nullptr ||
      std::sscanf(raw_args, "%f %f %f %f", &parsed[0], &parsed[1], &parsed[2], &parsed[3]) !=
          static_cast<int>(kShadowModComponentCount)) {
    WriteConsoleLine(kShadowModRangeMessage);
    return 0;
  }

  for (const float component : parsed) {
    if (component < kShadowModComponentMinimum || component > kShadowModComponentMaximum) {
      WriteConsoleLine(kShadowModRangeMessage);
      return 0;
    }
  }

  const auto quantize = [](const float component) {
    return static_cast<float>(static_cast<int>(component * kShadowModComponentScale)) /
           kShadowModComponentScale;
  };

  State().terrain_shadow_mod = {quantize(parsed[1]), quantize(parsed[2]), quantize(parsed[3]),
                                quantize(parsed[0])};
  return 1;
}

void CWorld_ResetRenderStateToInitializeDefaults() { State() = WorldRenderState{}; }

void RegisterWorldRenderConsoleCommands() {
  auto& console = openwow::debug::DebugConsole::Get();

  const auto register_toggle = [&console](const char* const name, int (*handler)()) {
    console.RegisterCommand(
        name, "",
        [handler](const std::vector<std::string>& ) -> std::string {
          (void)handler();
          return {};
        },
        0, "", kWorldRenderConsoleCategory);
  };

  const auto register_parsed = [&console](const char* const name, int (*handler)(const char*)) {
    console.RegisterRawCommand(
        name, "",
        [handler](const std::string_view raw_args) -> std::string {
          (void)handler(std::string(raw_args).c_str());
          return {};
        },
        "", kWorldRenderConsoleCategory);
  };

  register_toggle("showDetailDoodads", &CWorld_OnShowDetailDoodadsCommand);
  register_parsed("maxLOD", &CWorld_OnMaxLodCommand);
  register_toggle("showCull", &CWorld_OnShowCullCommand);
  register_parsed("setShadow", &CWorld_OnSetShadowCommand);
  register_parsed("waterRipples", &CWorld_OnWaterRipplesCommand);
  register_toggle("waterParticulates", &CWorld_OnWaterParticulatesCommand);
  register_toggle("showShadow", &CWorld_OnShowShadowCommand);
  register_toggle("showLowDetail", &CWorld_OnShowLowDetailCommand);
  register_toggle("showSimpleDoodads", &CWorld_OnShowSimpleDoodadsCommand);
  register_parsed("detailDoodadAlpha", &CWorld_OnDetailDoodadAlphaCommand);
  register_parsed("characterAmbient", &CWorld_OnCharacterAmbientCommand);
}

void CWorld_Initialize() {
  CWorld_ResetRenderStateToInitializeDefaults();
  RegisterWorldRenderConsoleCommands();
}

}

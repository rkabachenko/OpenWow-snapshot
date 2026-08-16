#pragma once

#include <array>
#include <cstdint>

namespace openwow::world {

enum class WorldRenderFlag : std::uint32_t {

  kTerrainCulling = 0x00000020u,

  kTerrainShadows = 0x00000040u,

  kDetailDoodads = 0x00100000u,

  kWaterParticulates = 0x02000000u,

  kTerrainLowDetail = 0x04000000u,
};

[[nodiscard]] bool CWorld_HasRenderFlag(WorldRenderFlag flag);
void CWorld_SetRenderFlag(WorldRenderFlag flag, bool enabled);

[[nodiscard]] std::uint32_t CWorld_GetDetailDoodadAlpha();

[[nodiscard]] float CWorld_GetCharacterAmbientMultiply();
[[nodiscard]] bool CWorld_IsCharacterAmbientOverridden();

void CWorld_UpdateCharacterAmbientMultiply(float area_ambient_multiplier,
                                            float delta_time_seconds);

[[nodiscard]] std::uint32_t CWorld_GetMaxTerrainLod();

[[nodiscard]] bool CWorld_AreSimpleDoodadsEnabled();

[[nodiscard]] int CWorld_GetWaterRippleSetting();
[[nodiscard]] bool CWorld_AreWaterRipplesEnabled();

[[nodiscard]] std::array<float, 4> CWorld_GetTerrainShadowModColor();

int CWorld_OnShowDetailDoodadsCommand();
int CWorld_OnMaxLodCommand(const char* raw_args);
int CWorld_OnShowCullCommand();
int CWorld_OnSetShadowCommand(const char* raw_args);
int CWorld_OnWaterRipplesCommand(const char* raw_args);
int CWorld_OnWaterParticulatesCommand();
int CWorld_OnShowShadowCommand();
int CWorld_OnShowLowDetailCommand();
int CWorld_OnShowSimpleDoodadsCommand();
int CWorld_OnDetailDoodadAlphaCommand(const char* raw_args);
int CWorld_OnCharacterAmbientCommand(const char* raw_args);

void CWorld_ResetRenderStateToInitializeDefaults();

void RegisterWorldRenderConsoleCommands();

void CWorld_Initialize();

}

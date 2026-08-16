
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace openwow::core {

enum class DisplayCallbackMode : uint32_t {
    ResetGamma          = 0,
    ApplyStartupUiFaster = 1,
    ApplyStereo         = 2,
};

using VideoDefaultsModeCallback = void (*)(std::uint32_t mode);

void DisplaySettingsCallback(DisplayCallbackMode mode);
void DispatchDisplaySettingsCallback(std::uint32_t mode);

void RegisterVideoDefaultsModeCallback(VideoDefaultsModeCallback callback);
void UnregisterVideoDefaultsModeCallback(VideoDefaultsModeCallback callback);
void DispatchVideoDefaultsModeCallbacks(std::uint32_t mode);
void ClearVideoDefaultsModeCallbacksForTests();

enum class StartupScreen {
    Movie,
    Login,
};

StartupScreen DetermineStartupScreen(uint8_t startup_level,
                                     int movie_cvar_value,
                                     int expansion_movie_cvar_value);

enum class SubsystemId : uint32_t {

    MemoryStormConsole,
    AsyncIOCVars,
    TextureFunctions,
    ModelBlobLoad,
    LiquidTexLoad,
    FontInit,
    ConsoleStartup,
    TextureFilteringCVar,
    UIFasterCVar,
    TextureCacheSizeCVar,
    M2RegisterCVars,
    DisplayCallbackRegister,
    DisplayCallbackBootstrap,
    M2SystemInit,
    TextureFilterClamp,

    ScheduleSubEvent,
    ClientErrorDisplayStateInit,
    RenderBootstrap,
    ClientRegisterCVars,
    DBClientInit,
    UIShaderInit,
    SoundInit,
    EffectDataLoad,
    FrameXmlTypeCapabilities,
    GlueEventNames,
    WorldEventNames,
    EffectIdTableInit,
    ComponentTextureCVars,
    ClientServicesInit,
    OpcodeRegister253,
    VoiceChatInit,
    StartupQueryHandlers,
    DBCacheLoad,
    WorldVideoOptions,
    WorldInit,
    WorldShadowTextures,
    InputControlInit,
    GlueUIInit,
    PendingStringProcess,
    DataPreloadThread,
    StartupScreenSelect,
    SpellVisuals,
    ScheduleFinalEvent,

    ChatLogShutdown,
    LoginShutdown,
    QueryOpcodesUnregister,
    DBCacheDestroyAll,
    CharacterComponentShutdown,
    GxRenderTargetCleanup,
    CMapObjCleanup,
    CombatDataShutdown,

    AccountDataUnregisterOpcodeHandlers,
    CharacterCreateShutdown,
    KBSystemShutdown,
    RealmCategoryListDestroy,

    DBCacheCreatureDestroy,
    DBCacheGameObjectDestroy,
    DBCacheItemNameDestroy,
    DBCacheItemDestroy,
    DBCacheNpcTextDestroy,
    DBCachePageTextDestroy,
    DBCacheQuestDestroy,
    DBCacheSaveToFile,

    CharacterComponentFreeHairGeosets,

    ConsoleAndFontShutdown,
    M2SystemShutdown,
    ModelBlobShutdown,
    AsyncFileShutdown,
    LightListShutdown,
    HeapUsageUnregisterCmd,
    ObjectHeapShutdown,

    Count,
};

struct SubsystemInfo {
    SubsystemId id;
    std::string name;
    bool        initialized = false;
    bool        conditional = false;
};

std::vector<SubsystemInfo> GetInitSubsystemOrder();

std::vector<SubsystemInfo> GetShutdownSubsystemOrder();

}

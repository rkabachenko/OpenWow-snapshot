
#include "openwow/core/game_subsystems.h"

#include "openwow/core/gxcvar.h"
#include "openwow/render/backend/bgfx/retail_render_profile.h"
#include "openwow/ui/game/cvar_system.h"

#include <string>
#include <vector>

namespace openwow::core {

namespace {

struct VideoDefaultsModeCallbackRegistry {
    void Register(const VideoDefaultsModeCallback callback) {
        callbacks.push_back(callback);
    }

    void Unregister(const VideoDefaultsModeCallback callback) {
        for (std::size_t index = 0; index < callbacks.size(); ++index) {
            if (callbacks[index] != callback) {
                continue;
            }

            callbacks[index] = callbacks.back();
            callbacks.pop_back();
        }
    }

    void Dispatch(const std::uint32_t mode) const {
        for (std::size_t index = 0; index < callbacks.size(); ++index) {
            callbacks[index](mode);
        }
    }

    void Clear() {
        callbacks.clear();
    }

    std::vector<VideoDefaultsModeCallback> callbacks;
};

VideoDefaultsModeCallbackRegistry& GetVideoDefaultsModeCallbackRegistry() {
    static VideoDefaultsModeCallbackRegistry registry;
    return registry;
}

void ForceRetailVideoDefault(openwow::ui::game::CVarSystem& cvars,
                             const char* const name,
                             const std::string& value) {

    if (cvars.Exists(name)) {
        (void)cvars.SetRegisteredCVarValueDirect(name, value);
    }
}

void ResetRetailVideoDefault(openwow::ui::game::CVarSystem& cvars,
                             const char* const name) {

    if (cvars.Exists(name)) {
        cvars.ResetCVar(name);
    }
}

void ApplyRetailVideoEffectsDefaults(openwow::ui::game::CVarSystem& cvars) {

    const auto* const profile = openwow::core::ida::GetStartupGraphicsQualityProfile();
    if (profile == nullptr) {
        return;
    }

    ForceRetailVideoDefault(cvars, "farclip", std::to_string(profile->farclip));
    ForceRetailVideoDefault(cvars, "shadowLevel", std::to_string(profile->shadow_level));
    ForceRetailVideoDefault(cvars, "MaxLights", std::to_string(profile->max_lights));
    ForceRetailVideoDefault(cvars, "specular", profile->specular_enabled ? "1" : "0");
    ForceRetailVideoDefault(cvars, "waterLOD", std::to_string(profile->water_lod));
    ForceRetailVideoDefault(cvars, "particleDensity", std::to_string(profile->particle_density));
    ForceRetailVideoDefault(cvars, "baseMip", std::to_string(profile->base_mip));
    ForceRetailVideoDefault(cvars, "groundEffectDensity",
                            std::to_string(profile->ground_effect_density));

    ResetRetailVideoDefault(cvars, "environmentDetail");
    ForceRetailVideoDefault(cvars, "groundEffectDist", "70.0");
    for (const char* const name : {"weatherDensity", "extShadowQuality", "ffxDeath", "ffxGlow"}) {
        ResetRetailVideoDefault(cvars, name);
    }

    const auto caps = openwow::render::QueryWotlkRendererCapabilities();
    const auto effects =
        openwow::render::ResolveWotlkVideoEffectsDefaults(
            static_cast<std::uint32_t>(caps.gx_device_class));
    ForceRetailVideoDefault(cvars, "particleDensity", effects.particle_density);
    ForceRetailVideoDefault(cvars, "projectedTextures",
                            effects.projected_textures ? "1" : "0");
}

}

void DisplaySettingsCallback(DisplayCallbackMode mode) {
    auto& cvars = openwow::ui::game::CVarSystem::Instance();
    const auto set_default = [&](const char* name) {
        if (!cvars.Exists(name)) {
            return false;
        }
        return cvars.SetRegisteredCVarValueDirect(name, cvars.GetCVarDefault(name));
    };

    switch (mode) {
        case DisplayCallbackMode::ResetGamma: {
            (void)cvars.SetRegisteredCVarValueDirect("DesktopGamma", "0");
            (void)cvars.ApplyPendingValue("DesktopGamma");

            (void)cvars.SetRegisteredCVarValueDirect("Gamma", "1.0");
            (void)cvars.ApplyPendingValue("Gamma");

            (void)set_default("windowResizeLock");
            break;
        }

        case DisplayCallbackMode::ApplyStartupUiFaster: {
            ApplyRetailVideoEffectsDefaults(cvars);
            if (cvars.Exists("UIFaster")) {
                const auto* startup_profile =
                    openwow::core::ida::GetStartupGraphicsQualityProfile();
                if (startup_profile != nullptr) {
                    (void)cvars.SetRegisteredCVarValueDirect(
                        "UIFaster",
                        startup_profile->ui_faster_enabled ? "1" : "0");
                }
            }
            break;
        }

        case DisplayCallbackMode::ApplyStereo: {

            (void)set_default("gxStereoEnabled");
            (void)set_default("gxStereoConvergence");
            (void)set_default("gxStereoSeparation");
            break;
        }
    }
}

void DispatchDisplaySettingsCallback(const std::uint32_t mode) {
    DisplaySettingsCallback(static_cast<DisplayCallbackMode>(mode));
}

void RegisterVideoDefaultsModeCallback(const VideoDefaultsModeCallback callback) {
    GetVideoDefaultsModeCallbackRegistry().Register(callback);
}

void UnregisterVideoDefaultsModeCallback(const VideoDefaultsModeCallback callback) {
    GetVideoDefaultsModeCallbackRegistry().Unregister(callback);
}

void DispatchVideoDefaultsModeCallbacks(const std::uint32_t mode) {
    GetVideoDefaultsModeCallbackRegistry().Dispatch(mode);
}

void ClearVideoDefaultsModeCallbacksForTests() {
    GetVideoDefaultsModeCallbackRegistry().Clear();
}

StartupScreen DetermineStartupScreen(uint8_t startup_level,
                                     int movie_cvar_value,
                                     int expansion_movie_cvar_value) {
    if (startup_level == 1) {

        if (expansion_movie_cvar_value == 1) {
            return StartupScreen::Movie;
        }
        return StartupScreen::Login;
    }

    if (movie_cvar_value == 1) {
        return StartupScreen::Movie;
    }
    return StartupScreen::Login;
}

std::vector<SubsystemInfo> GetInitSubsystemOrder() {
    return {

        {SubsystemId::MemoryStormConsole,      "MemoryStorm_RegisterConsoleCommands"},
        {SubsystemId::AsyncIOCVars,            "AsyncIO_RegisterCVars"},
        {SubsystemId::TextureFunctions,        "Texture_func9"},
        {SubsystemId::ModelBlobLoad,           "ModelBlob_LoadGlobalStartupBlob"},
        {SubsystemId::LiquidTexLoad,           "world\\liquid.tex", false, true},
        {SubsystemId::FontInit,                "FontInit_SetModeAndLoadDefaults(0)"},
        {SubsystemId::ConsoleStartup,          "ConsoleAndFont_StartupInitialize"},
        {SubsystemId::TextureFilteringCVar,    "CVar:textureFilteringMode"},
        {SubsystemId::UIFasterCVar,            "CVar:UIFaster"},
        {SubsystemId::TextureCacheSizeCVar,    "CVar:textureCacheSize"},
        {SubsystemId::M2RegisterCVars,         "M2_RegisterCVars"},
        {SubsystemId::DisplayCallbackRegister, "Register:DisplaySettingsCallback"},
        {SubsystemId::DisplayCallbackBootstrap,"Bootstrap:DisplaySettingsCallback(0,1,2)"},
        {SubsystemId::M2SystemInit,             "M2System_Init"},
        {SubsystemId::TextureFilterClamp,      "ClampStartup(Tier+Anisotropy)"},

        {SubsystemId::ScheduleSubEvent,        "ScheduleEvent(5,sub_4020E0)"},
        {SubsystemId::ClientErrorDisplayStateInit,
         "ClientErrorDisplay_InitRuntimeState"},
        {SubsystemId::RenderBootstrap,         "sub_40B710(font/string_batch_init)"},
        {SubsystemId::ClientRegisterCVars,     "Client_RegisterCVars"},
        {SubsystemId::DBClientInit,            "DBClient_Initialize"},
        {SubsystemId::UIShaderInit,            "InitGameSubsystems_InitializeUiShaders"},
        {SubsystemId::SoundInit,               "SoundInterface_Initialize(0)"},
        {SubsystemId::EffectDataLoad,          "CObjectEffect__LoadEffectData"},
        {SubsystemId::FrameXmlTypeCapabilities, "FrameXMLTypeCapabilities"},
        {SubsystemId::GlueEventNames,          "InitGlueEventNames"},
        {SubsystemId::WorldEventNames,         "InitWorldEventNames"},
        {SubsystemId::EffectIdTableInit,       "HardcodedEffectIdTable_Initialize"},
        {SubsystemId::ComponentTextureCVars,   "ComponentTexture_RegisterCVars"},
        {SubsystemId::ClientServicesInit,      "ClientServices__Initialize"},
        {SubsystemId::OpcodeRegister253,       "RegisterOpcodeHandler(253)"},
        {SubsystemId::VoiceChatInit,           "VoiceChat_Initialize", false, true},
        {SubsystemId::StartupQueryHandlers,    "RegisterStartupQueryHandlers"},
        {SubsystemId::DBCacheLoad,             "DBCache_LoadStartupQueryCaches"},
        {SubsystemId::WorldVideoOptions,       "CWorld__RegisterVideoOptions"},
        {SubsystemId::WorldInit,               "CWorld__Initialize"},
        {SubsystemId::WorldShadowTextures,     "CWorld__InitShadowTextures"},
        {SubsystemId::InputControlInit,        "InputControl_StartupInitialize"},
        {SubsystemId::GlueUIInit,              "GlueUI_Initialize"},
        {SubsystemId::PendingStringProcess,    "StartupPendingString_Process"},
        {SubsystemId::DataPreloadThread,       "StartDataPreloadThreadIfNeeded", false, true},
        {SubsystemId::StartupScreenSelect,     "StartupScreen(movie/login)"},
        {SubsystemId::SpellVisuals,            "InitSpellVisuals"},
        {SubsystemId::ScheduleFinalEvent,      "ScheduleEvent(7,sub_401FD0)"},
    };
}

std::vector<SubsystemInfo> GetShutdownSubsystemOrder() {

    return {
        {SubsystemId::DisplayCallbackRegister, "Unregister:DisplaySettingsCallback"},
        {SubsystemId::GlueUIInit,              "WorldToGlueTeardown(sub_406510)"},

        {SubsystemId::ChatLogShutdown,           "ChatLog_Shutdown(0x5FD430)"},
        {SubsystemId::LoginShutdown,             "Login_Shutdown(0x4DBBC0)"},
        {SubsystemId::QueryOpcodesUnregister,    "QueryOpcodes_Unregister(0x6355E0)"},
        {SubsystemId::DBCacheDestroyAll,         "DBCache_DestroyAll(0x635540)"},
        {SubsystemId::CharacterComponentShutdown,"CCharacterComponent_Shutdown(0x4F2320)"},
        {SubsystemId::VoiceChatInit,             "VoiceChat_Shutdown", false, true},
        {SubsystemId::GxRenderTargetCleanup,     "GxRenderTarget_Cleanup(0x409620)"},
        {SubsystemId::CMapObjCleanup,            "RenderBootstrap_FpsCleanup(0x40B320)"},
        {SubsystemId::CombatDataShutdown,        "CombatData_Shutdown(0x634C60)"},

        {SubsystemId::OpcodeRegister253,         "UnregisterOpcodeHandler(253)"},
        {SubsystemId::ClientServicesInit,        "ClientServices__FullLogout"},
        {SubsystemId::WorldInit,                 "CWorld__Shutdown(0x7E2C80)"},
        {SubsystemId::SoundInit,                 "SoundInterface_Shutdown(0x7837F0)"},
        {SubsystemId::ConsoleAndFontShutdown,    "ConsoleAndFont_Shutdown(0x7662A0)"},
        {SubsystemId::FontInit,                  "FontSubsystem_Shutdown(0x4A8D10)"},
        {SubsystemId::M2SystemShutdown,           "M2System_Shutdown(0x81C750)"},
        {SubsystemId::ModelBlobShutdown,         "ModelBlob_Shutdown(0x4BB1C0)"},
        {SubsystemId::TextureFunctions,          "Texture_func10"},
        {SubsystemId::AsyncFileShutdown,         "AsyncFile_Shutdown(0x4BAC50)"},
        {SubsystemId::LightListShutdown,         "LightList_Shutdown(0x6BC360)"},
        {SubsystemId::HeapUsageUnregisterCmd,    "HeapUsage_UnregisterConsoleCmd(0x4D27D0)"},
        {SubsystemId::ObjectHeapShutdown,        "CObjectHeap_Shutdown(0x4D3580)"},
    };
}

}

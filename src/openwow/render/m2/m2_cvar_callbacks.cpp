
#include "openwow/render/m2/m2_cvar_callbacks.h"
#include "openwow/core/console.h"
#include "openwow/core/decimal_parse.h"
#include "openwow/input/input_manager.h"
#include "openwow/ui/game/cvar_system.h"

#include <atomic>
#include <array>
#include <string_view>

namespace openwow::render {

namespace {
    constexpr std::uint32_t kLeftCtrlScancode = 224;

    std::atomic<uint32_t> g_m2GlobalFlags{0};

    struct M2CVarDefinition {
        const char* name;
        const char* default_value;
        const char* description;
    };

    constexpr std::array<M2CVarDefinition, 8> kM2CVarDefinitions{{
        {"M2UseZFill", "1", "z-fill transparent objects"},
        {"M2UseClipPlanes", "1",
         "use clip planes for sorting transparent objects"},
        {"M2UseThreads", "1", "multithread model animations"},
        {"M2BatchDoodads", "1",
         "combine doodads to reduce batch count"},
        {"M2BatchParticles", "1",
         "combine particle emitters to reduce batch count"},
        {"M2ForceAdditiveParticleSort", "0",
         "force all particles to sort as though they were additive"},
        {"M2Faster", "1",
         "end user control of scene optimization mode - (0-3)"},
        {"M2FasterDebug", "0",
         "programmer control of scene optimization mode"},
    }};

    std::uint32_t ParseCurrentUnsignedCVar(std::string_view name) {
        auto& cvars = openwow::ui::game::CVarSystem::Instance();
        if (!cvars.Exists(std::string(name))) {
            return 0;
        }
        return openwow::core::ParseSignedDecimalLikeSub76F0D0(
            cvars.GetCVar(std::string(name)));
    }

    bool ShouldSuppressM2DebugHundredsOverride() {

        return openwow::input::InputManager::Get().IsKeyDown(
            kLeftCtrlScancode);
    }

    bool IsCurrentM2CVarNonZero(openwow::ui::game::CVarSystem& cvars,
                                std::string_view name) {
        return openwow::core::ParseSignedDecimalLikeSub76F0D0(
                   cvars.GetCVar(std::string(name))) != 0;
    }

    std::uint32_t ComputeM2RegisterFlags(
        openwow::ui::game::CVarSystem& cvars) {
        std::uint32_t flags = kM2Flag_ShaderInit;
        if (IsCurrentM2CVarNonZero(cvars, "M2UseZFill")) {
            flags |= kM2Flag_UseZFill;
        }
        if (IsCurrentM2CVarNonZero(cvars, "M2UseClipPlanes")) {
            flags |= kM2Flag_UseClipPlanes;
        }
        if (IsCurrentM2CVarNonZero(cvars, "M2UseThreads")) {
            flags |= kM2Flag_UseThreads;
        }
        if (IsCurrentM2CVarNonZero(cvars, "M2BatchDoodads")) {
            flags |= kM2Flag_BatchDoodads;
        }
        if (IsCurrentM2CVarNonZero(cvars, "M2BatchParticles")) {
            flags |= kM2Flag_BatchParticles;
        }
        if (IsCurrentM2CVarNonZero(cvars, "M2ForceAdditiveParticleSort")) {
            flags |= kM2Flag_ForceAdditiveSort;
        }
        return flags;
    }

    void ToggleM2GlobalFlagWithConsoleLine(
        std::uint32_t currentFlags,
        std::uint32_t flagBit,
        bool enabled,
        const char* enabledLine,
        const char* disabledLine) {
        if (enabled) {
            SetM2GlobalFlags(currentFlags | flagBit);
            openwow::core::ida::ConsoleAddLine(
                enabledLine,
                openwow::core::ida::COLOR_DEFAULT);
            return;
        }

        SetM2GlobalFlags(currentFlags & ~flagBit);
        openwow::core::ida::ConsoleAddLine(
            disabledLine,
            openwow::core::ida::COLOR_DEFAULT);
    }
}

void RegisterM2CVarDefaults(openwow::ui::game::CVarSystem& cvars) {
    using openwow::ui::game::CVarFlags;

    for (const auto& definition : kM2CVarDefinitions) {
        cvars.RegisterCVar(definition.name,
                           definition.default_value,
                           CVarFlags::Archive,
                           definition.description);
    }
}

uint32_t GetM2GlobalFlags() {
    return g_m2GlobalFlags.load(std::memory_order_relaxed);
}

void SetM2GlobalFlags(uint32_t flags) {
    g_m2GlobalFlags.store(flags, std::memory_order_relaxed);
}

std::uint32_t OrM2OptimizationModeBits(std::uint16_t bits) {
    const std::uint32_t mode_bits = bits & kM2Flag_OptimizationModeMask;
    g_m2GlobalFlags.fetch_or(mode_bits, std::memory_order_relaxed);
    return mode_bits;
}

std::uint16_t ComputeM2OptimizationModeBits(
    std::uint32_t faster_value,
    std::uint32_t faster_debug_value,
    bool suppress_debug_hundreds_override) {
    const auto faster = static_cast<std::int32_t>(faster_value);
    const auto faster_debug = static_cast<std::int32_t>(faster_debug_value);
    std::uint32_t mode_bits = 0;

    switch (faster) {
        case 0:
            if (faster_debug == 0) {
                return 0;
            }
            switch (faster_debug % 10) {
                case 1:
                    mode_bits = kM2Flag_OptimizationModeLow;
                    break;
                case 2:
                    mode_bits = kM2Flag_OptimizationModeMid;
                    break;
                case 3:
                    mode_bits = kM2Flag_OptimizationModeHigh;
                    break;
                default:
                    break;
            }
            if (((faster_debug / 100) % 10) != 0
                && suppress_debug_hundreds_override) {
                return 0;
            }
            return static_cast<std::uint16_t>(mode_bits);

        case 1:
            return kM2Flag_OptimizationModeHigh;

        case 2:
        case 3:
            return kM2Flag_OptimizationModeLow;

        default:
            return 0;
    }
}

bool CVar_M2BatchParticles_Callback(
    const std::string& ,
    const std::string& ,
    const std::string& newValue) {
    const std::uint32_t enabled =
        openwow::core::ParseSignedDecimalLikeSub76F0D0(newValue);
    ToggleM2GlobalFlagWithConsoleLine(
        GetM2GlobalFlags(),
        kM2Flag_BatchParticles,
        enabled != 0,
        "Particle batching enabled.",
        "Particle batching disabled.");

    return true;
}

bool CVar_M2BatchDoodads_Callback(
    const std::string& ,
    const std::string& ,
    const std::string& newValue) {
    const std::uint32_t enabled =
        openwow::core::ParseSignedDecimalLikeSub76F0D0(newValue);
    ToggleM2GlobalFlagWithConsoleLine(
        GetM2GlobalFlags(),
        kM2Flag_BatchDoodads,
        enabled != 0,
        "Doodad batching enabled.",
        "Doodad batching disabled.");

    return true;
}

bool CVar_M2ForceAdditiveSort_Callback(
    const std::string& ,
    const std::string& ,
    const std::string& newValue) {
    const std::uint32_t enabled =
        openwow::core::ParseSignedDecimalLikeSub76F0D0(newValue);
    ToggleM2GlobalFlagWithConsoleLine(
        GetM2GlobalFlags(),
        kM2Flag_ForceAdditiveSort,
        enabled != 0,
        "Sorting all particles as though they were additive.",
        "Sorting particles normally.");

    return true;
}

bool CVar_M2Faster_Callback(
    const std::string& ,
    const std::string& ,
    const std::string& newValue) {
    const std::uint32_t requested_faster =
        openwow::core::ParseSignedDecimalLikeSub76F0D0(newValue);
    const std::uint32_t current_debug =
        ParseCurrentUnsignedCVar("M2FasterDebug");
    OrM2OptimizationModeBits(ComputeM2OptimizationModeBits(
        requested_faster,
        current_debug,
        ShouldSuppressM2DebugHundredsOverride()));
    return true;
}

bool CVar_M2FasterDebug_Callback(
    const std::string& ,
    const std::string& ,
    const std::string& newValue) {
    const std::uint32_t current_faster =
        ParseCurrentUnsignedCVar("M2Faster");
    const std::uint32_t requested_debug =
        openwow::core::ParseSignedDecimalLikeSub76F0D0(newValue);
    OrM2OptimizationModeBits(ComputeM2OptimizationModeBits(
        current_faster,
        requested_debug,
        ShouldSuppressM2DebugHundredsOverride()));
    return true;
}

std::uint32_t M2_RegisterCVars() {
    auto& cvars = openwow::ui::game::CVarSystem::Instance();
    RegisterM2CVarDefaults(cvars);
    RegisterM2CVarCallbacks();

    const std::uint32_t flags = ComputeM2RegisterFlags(cvars);
    SetM2GlobalFlags(flags);
    return flags;
}

void RegisterM2CVarCallbacks() {
    auto& cvars = openwow::ui::game::CVarSystem::Instance();

    cvars.SetValidationCallback("M2BatchDoodads",
        CVar_M2BatchDoodads_Callback);
    cvars.SetValidationCallback("M2BatchParticles",
        CVar_M2BatchParticles_Callback);
    cvars.SetValidationCallback("M2ForceAdditiveParticleSort",
        CVar_M2ForceAdditiveSort_Callback);
    cvars.SetValidationCallback("M2Faster",
        CVar_M2Faster_Callback);
    cvars.SetValidationCallback("M2FasterDebug",
        CVar_M2FasterDebug_Callback);
}

}

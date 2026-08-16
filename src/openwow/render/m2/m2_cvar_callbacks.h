
#pragma once

#include <cstdint>
#include <string>

namespace openwow::ui::game {
class CVarSystem;
}

namespace openwow::render {

enum M2GlobalRenderFlags : uint32_t {
    kM2Flag_UseZFill               = 0x1,
    kM2Flag_UseClipPlanes          = 0x2,
    kM2Flag_UseThreads             = 0x4,
    kM2Flag_ShaderInit             = 0x8,
    kM2Flag_BatchDoodads           = 0x20,
    kM2Flag_BatchParticles         = 0x80,
    kM2Flag_ForceAdditiveSort      = 0x100,
    kM2Flag_OptimizationModeMask   = 0xE000,
    kM2Flag_OptimizationModeLow    = 0x2000,
    kM2Flag_OptimizationModeMid    = 0x6000,
    kM2Flag_OptimizationModeHigh   = 0xE000,
};

void RegisterM2CVarDefaults(openwow::ui::game::CVarSystem& cvars);

uint32_t GetM2GlobalFlags();

void SetM2GlobalFlags(uint32_t flags);

std::uint32_t OrM2OptimizationModeBits(std::uint16_t bits);

[[nodiscard]] std::uint16_t ComputeM2OptimizationModeBits(
    std::uint32_t faster_value,
    std::uint32_t faster_debug_value,
    bool suppress_debug_hundreds_override);

bool CVar_M2BatchDoodads_Callback(
    const std::string& name,
    const std::string& oldValue,
    const std::string& newValue);

bool CVar_M2BatchParticles_Callback(
    const std::string& name,
    const std::string& oldValue,
    const std::string& newValue);

bool CVar_M2ForceAdditiveSort_Callback(
    const std::string& name,
    const std::string& oldValue,
    const std::string& newValue);

bool CVar_M2Faster_Callback(
    const std::string& name,
    const std::string& oldValue,
    const std::string& newValue);

bool CVar_M2FasterDebug_Callback(
    const std::string& name,
    const std::string& oldValue,
    const std::string& newValue);

[[nodiscard]] std::uint32_t M2_RegisterCVars();

void RegisterM2CVarCallbacks();

}

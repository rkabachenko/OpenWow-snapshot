
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openwow::core::ida {

enum CVarFlag : uint32_t {
    CVAR_REGISTERED      = 0x001,
    CVAR_READONLY        = 0x002,
    CVAR_IMMUTABLE       = 0x004,
    CVAR_SCOPE_ACCOUNT   = 0x010,
    CVAR_SCOPE_CHARACTER = 0x020,
    CVAR_NOTINCONFIG     = 0x080,
    CVAR_CONSOLE_RDONLY  = 0x100,
    CVAR_LOOKUP_SIGNBIT  = 0x80000000,
};

using CVarValidationCB = std::function<bool(void* cvar, const char* old_val,
                                            const char* new_val, int user_data)>;

struct CVarSnapshot {
    std::string registered_name;
    std::string value;
    float current_float_value = 0.0f;
    std::int32_t current_int_value = 0;
    std::string default_value;
    bool has_default_value = false;
    std::string startup_value;
    bool has_startup_value = false;
    std::string pending_value;
    bool has_pending_value = false;
    std::uint32_t flags = 0;
    std::string description;
    float min_value = 0.0f;
    float max_value = 0.0f;
    bool has_limits = false;
    std::uint32_t change_counter = 0;
    std::uint8_t info_bits = 0;
    bool has_validation_callback = false;
};

bool CVar_EnsureWTFDirectory(const std::string& path);

void CVar_SetDirtyFlag();

void CVar_MarkValueDirty();

int CVar_ParseConfigBuffer(const std::string& content);

int CVar_LoadFromFile(const std::string& filename);

[[nodiscard]] std::optional<CVarSnapshot> CVar_FindByName(const char* name);

bool CVar_WriteSingleToFile(const std::string& name, const std::string& value,
                            std::string& output);

int CVar_AppendAllToBuffer(char* buffer, int buffer_size,
                           std::uint32_t scope_filter,
                           std::uint32_t exclusion_flags);

int CVar_FlushToFile();

void CVar_LoadConfig(const std::string& config_filename);

void CVar_SetConfigFileOverride(const std::string& config_file_path);

void CVar_Cleanup();

int GetBuildString();

}

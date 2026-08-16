
#include "openwow/core/cvar.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "openwow/data/startup_filesystem_state.h"
#include "openwow/core/console.h"
#include "openwow/debug/diagnostics/debug_console.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/vfs/sfile_core.h"

namespace openwow::core::ida {

static std::atomic_bool s_dirty_flag{false};

static bool     s_dirty_flag2 = false;

static bool     s_config_loaded = false;

static std::string s_config_filename;

static std::filesystem::path s_config_file_override;

namespace {

std::filesystem::path BuildNativePath(std::string path) {

    std::replace(path.begin(), path.end(), '\\',
                 static_cast<char>(std::filesystem::path::preferred_separator));
    return std::filesystem::path(path);
}

std::filesystem::path GetStartupPathFallbackRoot() {
    std::error_code ec;
    const std::filesystem::path current_directory =
        std::filesystem::current_path(ec);
    if (ec) {
        return {};
    }

    return current_directory;
}

std::filesystem::path ResolveConfigPathForIo(
    const std::filesystem::path& path) {
    const std::filesystem::path native_path = BuildNativePath(path.string());
    if (native_path.is_absolute()) {
        return native_path.lexically_normal();
    }

    const auto& startup_state = openwow::data::GetStartupFileSystemState();
    if (!startup_state.executable_base_path.empty()) {
        return (BuildNativePath(startup_state.executable_base_path) / native_path)
            .lexically_normal();
    }

    const std::filesystem::path fallback_root = GetStartupPathFallbackRoot();
    if (fallback_root.empty()) {
        return native_path;
    }

    return (fallback_root / native_path).lexically_normal();
}

std::filesystem::path ResolvePrimaryConfigPathForIo() {
    if (!s_config_file_override.empty()) {
        return ResolveConfigPathForIo(s_config_file_override);
    }
    return ResolveConfigPathForIo(std::filesystem::path("WTF") /
                                  s_config_filename);
}

enum class EnsureWTFPathState {
    kMissing,
    kDirectory,
    kOther,
};

EnsureWTFPathState QueryEnsureWTFPathState(const std::filesystem::path& path) {
    const std::string path_string = path.string();
    switch (openwow::vfs::FileSystem_GetPathType(path_string.c_str())) {
        case openwow::vfs::FileSystemPathType::kMissing:
            return EnsureWTFPathState::kMissing;
        case openwow::vfs::FileSystemPathType::kDirectory:
            return EnsureWTFPathState::kDirectory;
        case openwow::vfs::FileSystemPathType::kRegularFile:
            return EnsureWTFPathState::kOther;
    }

    return EnsureWTFPathState::kOther;
}

bool EnsureWTFPathPrefix(const std::filesystem::path& path) {

    switch (QueryEnsureWTFPathState(path)) {
        case EnsureWTFPathState::kDirectory:
            return true;
        case EnsureWTFPathState::kOther:
            return false;
        case EnsureWTFPathState::kMissing:
            break;
    }

    const std::string path_string = path.string();
    return openwow::vfs::FileSystem_CreateDirectory(path_string.c_str(), false);
}

bool IsConsoleSetTokenDelimiter(const char ch) {

    return ch == ' ' || ch == ',' || ch == ';' || ch == '\t' || ch == '"' ||
           ch == '\r' || ch == '\n';
}

bool ParseConsoleSetToken(const std::string_view input, std::size_t& offset,
                          const std::size_t capacity, std::string& token) {
    token.clear();
    bool quoted = false;

    while (offset < input.size() && IsConsoleSetTokenDelimiter(input[offset])) {
        if (input[offset] == '"') {
            quoted = true;
            ++offset;
            break;
        }
        ++offset;
    }
    if (offset >= input.size()) {
        return quoted;
    }

    while (offset < input.size()) {
        const char ch = input[offset];
        if (quoted) {
            if (ch == '"') {
                ++offset;
                return true;
            }
        } else if (IsConsoleSetTokenDelimiter(ch)) {

            if (ch != '"') {
                ++offset;
            }
            return true;
        }

        if (token.size() + 1 < capacity) {
            token.push_back(ch);
        }
        ++offset;
    }
    return true;
}

bool ParseConsoleSetArguments(std::string_view raw_args, std::string& name,
                              std::string& value) {
    std::size_t offset = 0;
    if (!ParseConsoleSetToken(raw_args, offset, 0x40u, name) || name.empty()) {
        return false;
    }

    if (!ParseConsoleSetToken(raw_args, offset, 0x800u, value)) {
        value.clear();
        return true;
    }
    return true;
}

std::string ConsoleSetCVarHandler(std::string_view raw_args) {
    std::string name;
    std::string value;
    if (!ParseConsoleSetArguments(raw_args, name, value)) {
        return "Usage: SET <cvar> <value>";
    }

    auto& sys = openwow::ui::game::CVarSystem::Instance();

    (void)sys.SetCVar(name, value);
    return "";
}

std::string ParseCVarCommandName(const std::string_view raw_args) {
    std::size_t offset = 0;
    std::string name;
    (void)ParseConsoleSetToken(raw_args, offset, 0x40u, name);
    return name;
}

std::string ConsoleResetCVarHandler(const std::string_view raw_args,
                                    const bool reset_to_startup) {
    auto& sys = openwow::ui::game::CVarSystem::Instance();
    const std::string name = ParseCVarCommandName(raw_args);
    if (!name.empty()) {
        if (!sys.Exists(name)) {
            ConsoleAddLine("No such cvar \"" + name + "\"\n", COLOR_ERROR);
            return {};
        }
        if (reset_to_startup) {
            sys.ResetToStartup(name);
        } else {
            sys.ResetCVar(name);
        }
        return {};
    }

    ConsoleAddLine(reset_to_startup ? "Resetting all cvars\n"
                                    : "Restoring all cvars\n",
                   COLOR_DEFAULT);
    for (const auto& registered_name : sys.GetAllNames()) {
        if (reset_to_startup) {
            sys.ResetToStartup(registered_name);
        } else {
            sys.ResetCVar(registered_name);
        }
    }
    return {};
}

void AppendBoundedCVarListSuffix(std::string& line,
                                 const std::string_view suffix) {
    constexpr std::size_t kRetailCVarListCapacity = 0x100u;
    if (line.size() >= kRetailCVarListCapacity - 1u) {
        return;
    }
    line.append(suffix.substr(
        0, (kRetailCVarListCapacity - 1u) - line.size()));
}

std::string ConsoleListCVarsHandler(std::string_view) {
    auto& sys = openwow::ui::game::CVarSystem::Instance();
    for (const auto& name : sys.GetAllNames()) {
        const auto snapshot = sys.GetCVarSnapshot(name);
        if (!snapshot.has_value()) {
            continue;
        }

        std::array<char, 0x100> formatted{};
        std::snprintf(formatted.data(), formatted.size(), "  \"%s\" is \"%s\"",
                      snapshot->registered_name.c_str(), snapshot->value.c_str());
        std::string line(formatted.data());
        if (snapshot->has_default_value &&
            !openwow::text::EqualsIgnoreCaseAscii(snapshot->value,
                                                  snapshot->default_value)) {
            std::snprintf(formatted.data(), formatted.size(), " (default \"%s\")",
                          snapshot->default_value.c_str());
            AppendBoundedCVarListSuffix(line, formatted.data());
        }
        if (snapshot->has_startup_value &&
            !openwow::text::EqualsIgnoreCaseAscii(snapshot->value,
                                                  snapshot->startup_value)) {
            std::snprintf(formatted.data(), formatted.size(), " (reset \"%s\")",
                          snapshot->startup_value.c_str());
            AppendBoundedCVarListSuffix(line, formatted.data());
        }
        ConsoleAddLine(line, COLOR_DEFAULT);
    }
    return {};
}

void RegisterCVarConsoleCommands() {
    auto& console = openwow::debug::DebugConsole::Get();

    console.RegisterRawCommand("SET", "Set the value of a CVar",
                               ConsoleSetCVarHandler, "Set the value of a CVar", 5);

    console.RegisterRawCommand(
        "cvar_reset", "Set the value of a CVar to it's startup value",
        [](const std::string_view raw_args) {
            return ConsoleResetCVarHandler(raw_args, true);
        },
        "Set the value of a CVar to it's startup value", 5);

    console.RegisterRawCommand(
        "cvar_default", "Set the value of a CVar to it's coded default value",
        [](const std::string_view raw_args) {
            return ConsoleResetCVarHandler(raw_args, false);
        },
        "Set the value of a CVar to it's coded default value", 5);

    console.RegisterRawCommand("cvarlist", "List cvars",
                               ConsoleListCVarsHandler, "List cvars", 5);
}

}

bool CVar_EnsureWTFDirectory(const std::string& path) {
    if (path.empty()) return false;

    const std::filesystem::path p = ResolveConfigPathForIo(path);
    std::filesystem::path current;
    for (const auto& component : p) {
        current /= component;
        if (!EnsureWTFPathPrefix(current)) {
            return false;
        }
    }
    return true;
}

void CVar_SetDirtyFlag() {
    s_dirty_flag2 = true;
}

void CVar_MarkValueDirty() {
    s_dirty_flag.store(true, std::memory_order_release);
}

int CVar_ParseConfigBuffer(const std::string& content) {
    if (content.empty()) return 1;

    RegisterCVarConsoleCommands();

    std::string_view data = content;

    if (data.size() >= 3 &&
        static_cast<unsigned char>(data[0]) == 0xEF &&
        static_cast<unsigned char>(data[1]) == 0xBB &&
        static_cast<unsigned char>(data[2]) == 0xBF) {
        data.remove_prefix(3);
    }
    if (const std::size_t nul = data.find('\0'); nul != std::string_view::npos) {
        data = data.substr(0, nul);
    }

    auto& console = openwow::debug::DebugConsole::Get();
    std::size_t cursor = 0;
    while (cursor < data.size()) {

        while (cursor < data.size() &&
               (data[cursor] == '\r' || data[cursor] == '\n')) {
            ++cursor;
        }
        if (cursor == data.size()) {
            break;
        }

        std::size_t end = data.find_first_of("\r\n", cursor);
        if (end == std::string_view::npos) {
            end = data.size();
        }

        const std::size_t line_size = std::min<std::size_t>(end - cursor, 0x7FFu);
        const std::string line(data.substr(cursor, line_size));
        cursor = end;

        if (line.size() >= 4) {
            std::string prefix = line.substr(0, 4);
            std::transform(prefix.begin(), prefix.end(), prefix.begin(),
                           [](unsigned char c) { return std::toupper(c); });
            if (prefix == "SET ") {

                console.Execute(line, false);
            }
        }
    }
    return 1;
}

int CVar_LoadFromFile(const std::string& filename) {

    auto try_open_and_read =
        [](const std::string& path) -> std::optional<std::string> {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return std::nullopt;
        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    };

    auto content =
        try_open_and_read(ResolveConfigPathForIo(filename).string());
    if (!content.has_value()) {
        content = try_open_and_read(
            ResolveConfigPathForIo(std::filesystem::path("WTF") / filename)
                .string());
        if (!content.has_value()) return 0;

    }

    return CVar_ParseConfigBuffer(content.value());

}

std::optional<CVarSnapshot> CVar_FindByName(const char* name) {
    if (name == nullptr) {
        return std::nullopt;
    }

    const auto snapshot =
        openwow::ui::game::CVarSystem::Instance().GetCVarSnapshot(name);
    if (!snapshot.has_value()) {
        return std::nullopt;
    }

    CVarSnapshot result;
    result.registered_name = snapshot->registered_name;
    result.value = snapshot->value;
    result.current_float_value = snapshot->current_float_value;
    result.current_int_value = snapshot->current_int_value;
    result.default_value = snapshot->default_value;
    result.has_default_value = snapshot->has_default_value;
    result.startup_value = snapshot->startup_value;
    result.has_startup_value = snapshot->has_startup_value;
    result.pending_value = snapshot->pending_value;
    result.has_pending_value = snapshot->has_pending_value;
    result.flags = static_cast<std::uint32_t>(snapshot->flags);
    result.description = snapshot->description;
    result.min_value = snapshot->min_value;
    result.max_value = snapshot->max_value;
    result.has_limits = snapshot->has_limits;
    result.change_counter = snapshot->change_counter;
    result.info_bits = snapshot->info_bits;
    result.has_validation_callback = snapshot->has_validation_callback;
    return result;
}

bool CVar_WriteSingleToFile(const std::string& name, const std::string& value,
                            std::string& output) {
    char buf[260];
    std::snprintf(buf, sizeof(buf), "SET %s \"%s\"\n", name.c_str(), value.c_str());
    output += buf;
    return true;
}

int CVar_AppendAllToBuffer(char* buffer, int buffer_size,
                           std::uint32_t scope_filter,
                           std::uint32_t exclusion_flags) {
    if (!buffer || buffer_size <= 0) return 1;

    using openwow::ui::game::CVarSerializationScope;
    std::optional<CVarSerializationScope> scope;
    switch (scope_filter & 0x30u) {
        case 0x00u: scope = CVarSerializationScope::kConfigFile; break;
        case 0x10u: scope = CVarSerializationScope::kAccountDataSlot0; break;
        case 0x20u: scope = CVarSerializationScope::kAccountDataSlot1; break;
        default: return 1;
    }

    const std::string serialized =
        openwow::ui::game::CVarSystem::Instance().SerializeConfig(
            *scope,
            static_cast<openwow::ui::game::CVarFlags>(exclusion_flags));

    std::size_t current_len = 0;
    while (current_len < static_cast<std::size_t>(buffer_size)
           && buffer[current_len] != '\0') {
        ++current_len;
    }
    if (current_len >= static_cast<std::size_t>(buffer_size)) return 1;
    const std::size_t copy_len = std::min(
        serialized.size(), static_cast<std::size_t>(buffer_size) - current_len - 1);
    std::memcpy(buffer + current_len, serialized.data(), copy_len);
    buffer[current_len + copy_len] = '\0';

    return 1;
}

int CVar_FlushToFile() {

    if (!s_dirty_flag.exchange(false, std::memory_order_acq_rel)) return 1;

    if (s_config_filename.empty()) {
        s_dirty_flag.store(true, std::memory_order_release);
        return 0;
    }

    const std::filesystem::path path = ResolvePrimaryConfigPathForIo();

    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
    }

    auto& cvar_sys = openwow::ui::game::CVarSystem::Instance();

    if (cvar_sys.SaveToFile(path.string())) {
        return 1;
    }

    s_dirty_flag.store(true, std::memory_order_release);
    return 0;
}

void CVar_LoadConfig(const std::string& config_filename) {
    s_config_filename = config_filename;
    s_config_loaded = true;

    const std::filesystem::path config_path = ResolvePrimaryConfigPathForIo();
    CVar_EnsureWTFDirectory(config_path.parent_path().string());

    RegisterCVarConsoleCommands();

    if (s_config_file_override.empty()) {

        CVar_LoadFromFile(s_config_filename);
    } else {
        CVar_LoadFromFile(config_path.string());
    }
}

void CVar_SetConfigFileOverride(const std::string& config_file_path) {
    s_config_file_override = BuildNativePath(config_file_path).lexically_normal();
}

void CVar_Cleanup() {
    s_config_loaded = false;
    CVar_FlushToFile();

    auto& console = openwow::debug::DebugConsole::Get();
    console.UnregisterCommand("SET");
    console.UnregisterCommand("cvar_reset");
    console.UnregisterCommand("cvar_default");
    console.UnregisterCommand("cvarlist");

    openwow::ui::game::CVarSystem::Instance().Clear();
    s_config_filename.clear();
    s_config_file_override.clear();
}

int GetBuildString() {
    auto& console = openwow::debug::DebugConsole::Get();
    console.Write(std::string(kRetailI386BuildString));
    return 1;
}

}

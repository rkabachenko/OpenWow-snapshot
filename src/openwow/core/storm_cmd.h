
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace openwow::core {

enum class CmdValueType : uint32_t {
    kNone    = 0x00000,
    kNumeric = 0x10000,
    kString  = 0x20000,
};

enum class CmdCategory : uint32_t {
    kNormal          = 0x0000000,
    kOptional        = 0x1000000,
    kOptionalDeferred = 0x2000000,
};

enum class StartupCommandId : uint32_t {
    kD3D = 0,
    kD3D9Ex = 1,
    kDataDir = 2,
    kNoLagFix = 3,
    kLoadFile = 4,
    kGameType = 5,
    kOpenGl = 6,
    kSoftwareTnl = 7,
    kTimeDemo = 8,
    kResolutionOverride = 9,
    kDepthOverride = 10,
    kDetailOverride = 11,
    kSoundOverride = 12,
    kFullscreen = 13,
    kSampleRate22050 = 14,
    kNoWarnings = 15,
    kResolution800x600 = 16,
    kResolution1024x768 = 17,
    kResolution1280x960 = 18,
    kResolution1280x1024 = 19,
    kResolution1600x1200 = 20,
    kUpToDate = 21,
    kBitDepth16 = 22,
    kNoFixLag = 24,
    kNoSound = 26,
    kSoundChaos = 27,
    kDepth16 = 29,
    kDepth24 = 30,
    kDepth32 = 31,
    kWindowed = 32,
    kConsole = 35,
    kHwDetect = 36,
    kGxOverride = 39,
};

using CmdDefHandlerFn = int(*)(void* context, const char* value);

struct CmdDefInitEntry {
    uint32_t flags = 0;
    uint32_t id = 0;
    const char* name = nullptr;

    void* reserved = nullptr;
    CmdDefHandlerFn handler = nullptr;
    void* variable = nullptr;
    uint32_t variable_bytes = 0;
    uint32_t set_mask = 1;
    uint32_t clear_mask = 0xFFFFFFFFu;
};

struct CmdDefCallbackView {
    uint32_t flags = 0;
    uint32_t id = 0;
    const char* name = nullptr;
    void* variable = nullptr;
    uint32_t set_mask = 0;
    uint32_t clear_mask = 0;

    std::uintptr_t value = 0;
};

struct CmdDef {
    uint32_t flags = 0;
    uint32_t id = 0;
    char name[16] = {};
    CmdDefHandlerFn handler = nullptr;
    int32_t active = 0;
    char* string_value = nullptr;
    int32_t name_len = 0;
    int32_t int_value = 0;
    int32_t pending_update = 0;
    bool default_on = false;
    void* variable = nullptr;
    uint32_t variable_bytes = 0;
    uint32_t set_mask = 1;
    uint32_t clear_mask = 0xFFFFFFFFu;
};

class CmdDefList {
public:
    CmdDefList() = default;
    ~CmdDefList();

    void Clear();

    void Add(CmdDef* def);
    void Remove(CmdDef* def);

    CmdDef* FindByName(const char* name) const;
    CmdDef* FindById(uint32_t id) const;

    CmdDef* First() const;
    CmdDef* Next(CmdDef* current) const;

    const std::vector<CmdDef*>& entries() const { return entries_; }

private:
    std::vector<CmdDef*> entries_;
};

class StormCmd {
public:
    static StormCmd& Instance();

    bool InitErrorStrings(std::span<const CmdDefInitEntry> entries);
    bool InitErrorStrings(const void* list, uint32_t count);

    bool RegisterCommand(uint32_t flags, uint32_t id, const char* name,
                         void* variable, uint32_t variable_bytes,
                         uint32_t set_mask, uint32_t clear_mask,
                         CmdDefHandlerFn handler);

    int Shutdown();

    int GetBuffer(uint32_t id, char* buffer, uint32_t bufferchars);

    bool ParseCommandLine(const char* cmdline, bool use_env,
                          int (*handler)(int), void (*completion)(void*));

    bool InitCommandLine(int (*handler)(int), void (*completion)(void*));

    [[nodiscard]] bool IsCommandEnabled(uint32_t id) const;
    [[nodiscard]] bool IsCommandEnabled(StartupCommandId id) const {
        return IsCommandEnabled(static_cast<uint32_t>(id));
    }

    [[nodiscard]] std::string GetCommandString(uint32_t id) const;
    [[nodiscard]] std::string GetCommandString(StartupCommandId id) const {
        return GetCommandString(static_cast<uint32_t>(id));
    }

    void SetProcessCommandLineOverrideForTests(std::optional<std::string> cmdline);

    CmdDefList& NormalList() { return normal_list_; }
    CmdDefList& OptionalList() { return optional_list_; }
    const CmdDefList& NormalList() const { return normal_list_; }
    const CmdDefList& OptionalList() const { return optional_list_; }

    bool ProcessArgument(CmdDef* def, std::string_view arg_text,
                         std::size_t& consumed);

    bool ProcessSwitch(std::string_view switch_text, CmdDef*& out_cmd,
                       void (*completion)(void*));

    bool ProcessTokenLoop(std::string_view command_line, std::size_t& cursor,
                          CmdDef*& pending_cmd, CmdDef*& current_node,
                          int (*handler)(int), void (*completion)(void*));

private:
    StormCmd() = default;

    CmdDefList normal_list_;
    CmdDefList optional_list_;
    bool added_optional_ = false;
    mutable std::mutex mutex_;
};

void SCmd_ParseBooleanFlag(void* cmdDef, const char* value);

void SCmd_ParseInteger(void* cmdDef, const char* value, uint32_t* bytesConsumed);

void SCmd_ParseString(void* cmdDef, const char* value, uint32_t* bytesConsumed);

int SCmd_ParseFile(const char* filename, StormCmd& storm_cmd,
                    CmdDef*& pending_cmd, CmdDef*& current_node,
                    int (*handler)(int), void (*completion)(void*));

enum class SCmdErrorCode : int32_t {
    kInvalidParameter = static_cast<int32_t>(0x85100065),

    kBadSyntax        = static_cast<int32_t>(0x8510006D),

    kOpenFailed       = 110,

};

struct SCmdErrorInfo {
    int32_t     error_code;
    const char* context;
    const char* formatted_message;
};

void SCmd_ReportError(const char* context, SCmdErrorCode error_code,
                      void (*callback)(void*));

}

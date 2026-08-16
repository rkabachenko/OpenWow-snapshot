
#include "storm_cmd.h"

#include "openwow/core/storm_error.h"
#include "openwow/platform/process/os_platform.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string_view>

namespace openwow::core {

namespace {

std::optional<std::string>& ProcessCommandLineOverride() {
    static std::optional<std::string> override;
    return override;
}

[[nodiscard]] CmdValueType ValueTypeForFlags(const uint32_t flags) {
    return static_cast<CmdValueType>(flags & 0x30000);
}

[[nodiscard]] bool CommandNamesEqual(const CmdDef& def, const char* name) {
    return std::strcmp(def.name, name) == 0;
}

[[nodiscard]] bool StartsWithCaseInsensitive(const std::string_view value,
                                             const std::string_view prefix) {
    if (prefix.size() > value.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        const auto left =
            static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(value[i])));
        const auto right =
            static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(prefix[i])));
        if (left != right) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] uint32_t ReadExternalDword(const CmdDef& def) {
    const auto* const bytes = static_cast<const uint8_t*>(def.variable);
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8u) |
           (static_cast<uint32_t>(bytes[2]) << 16u) |
           (static_cast<uint32_t>(bytes[3]) << 24u);
}

void WriteExternalDword(const CmdDef& def, const uint32_t value) {
    auto* const bytes = static_cast<uint8_t*>(def.variable);
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8u);
    bytes[2] = static_cast<uint8_t>(value >> 16u);
    bytes[3] = static_cast<uint8_t>(value >> 24u);
}

void WriteExternalIntegerBytes(const CmdDef& def) {
    if (!def.variable || def.variable_bytes == 0) {
        return;
    }
    const uint32_t value = static_cast<uint32_t>(def.int_value);
    auto* const output = static_cast<uint8_t*>(def.variable);
    const uint32_t size =
        std::min<uint32_t>(def.variable_bytes, sizeof(def.int_value));
    for (uint32_t index = 0; index < size; ++index) {
        output[index] = static_cast<uint8_t>(value >> (index * 8u));
    }
}

[[nodiscard]] bool IsTokenDelimiter(const char ch) {
    return ch == ' ' || ch == ',' || ch == ';' || ch == '"' ||
           ch == '\t' || ch == '\n' || ch == '\r' || ch == '\x1a';
}

[[nodiscard]] bool ConsumeNextToken(const std::string_view source,
                                    std::size_t& cursor,
                                    std::string& token,
                                    bool& was_quoted) {
    token.clear();
    was_quoted = false;
    bool inside_quotes = false;

    while (cursor < source.size() && IsTokenDelimiter(source[cursor])) {
        if (source[cursor] == '"') {
            was_quoted = true;
            inside_quotes = true;
            ++cursor;
            break;
        }
        ++cursor;
    }
    if (cursor >= source.size()) {
        return false;
    }

    while (cursor < source.size()) {
        const char ch = source[cursor];

        if (ch == '"') {
            was_quoted = true;
            ++cursor;
            if (inside_quotes) {

                break;
            }
            inside_quotes = true;
            continue;
        }

        if (!inside_quotes && IsTokenDelimiter(ch)) {
            ++cursor;
            break;
        }

        if (token.size() < 255) {
            token.push_back(ch);
        }
        ++cursor;
    }

    return !token.empty();
}

CmdDef* FindByIdAcrossLists(const CmdDefList& normal_list,
                           const CmdDefList& optional_list,
                           const uint32_t id) {
    if (CmdDef* const def = normal_list.FindById(id)) {
        return def;
    }
    return optional_list.FindById(id);
}

CmdDef* FindBestPrefixMatch(const CmdDefList& list,
                            const std::string_view token,
                            std::size_t& matched_length) {
    CmdDef* best = nullptr;
    for (CmdDef* const def : list.entries()) {
        const std::string_view name(def->name,
                                    static_cast<std::size_t>(def->name_len));
        if (name.empty() || name.size() <= matched_length ||
            name.size() > token.size()) {
            continue;
        }
        const bool exact_case = (def->flags & 0x100u) != 0;
        const bool matches = exact_case
                                 ? token.substr(0, name.size()) == name
                                 : StartsWithCaseInsensitive(token, name);
        if (!matches) {
            continue;
        }
        matched_length = name.size();
        best = def;
    }
    return best;
}

CmdDef* FindBestPrefixMatch(const StormCmd& storm_cmd,
                            const std::string_view token,
                            std::size_t& matched_length) {
    CmdDef* best = FindBestPrefixMatch(storm_cmd.NormalList(), token, matched_length);
    if (CmdDef* const optional_match =
            FindBestPrefixMatch(storm_cmd.OptionalList(), token, matched_length)) {
        best = optional_match;
    }
    return best;
}

CmdDef* FindNextSamePrefixMatch(const StormCmd& storm_cmd,
                                const std::string_view matched_name,
                                const CmdDef* after_def) {
    CmdDef* best = nullptr;
    std::size_t best_len = matched_name.size() - 1;

    for (const CmdDefList* list :
         {&storm_cmd.NormalList(), &storm_cmd.OptionalList()}) {
        bool past_start = false;
        for (CmdDef* const candidate : list->entries()) {
            if (candidate == after_def) {
                past_start = true;
                continue;
            }
            if (!past_start) {
                continue;
            }
            const std::string_view cand_name(
                candidate->name,
                static_cast<std::size_t>(candidate->name_len));
            if (cand_name.empty() || cand_name.size() <= best_len ||
                cand_name.size() > matched_name.size()) {
                continue;
            }
            const bool exact_case = (candidate->flags & 0x100u) != 0;
            const bool matches =
                exact_case
                    ? matched_name.substr(0, cand_name.size()) == cand_name
                    : StartsWithCaseInsensitive(matched_name, cand_name);
            if (matches) {
                best_len = cand_name.size();
                best = candidate;
            }
        }
    }
    return best;
}

void ReplaceStringValue(CmdDef& def, const std::string_view value) {
    std::free(def.string_value);
    def.string_value = nullptr;

    char* const copy = static_cast<char*>(std::malloc(value.size() + 1));
    if (!copy) {
        return;
    }
    std::memcpy(copy, value.data(), value.size());
    copy[value.size()] = '\0';
    def.string_value = copy;
}

std::int32_t ParseStormNumeric32(const char* text,
                                 const bool signed_parse,
                                 char** end) {
    if (signed_parse) {
        errno = 0;
        const long long parsed = std::strtoll(text, end, 0);
        if (errno == ERANGE || parsed > std::numeric_limits<std::int32_t>::max()) {
            return std::numeric_limits<std::int32_t>::max();
        }
        if (parsed < std::numeric_limits<std::int32_t>::min()) {
            return std::numeric_limits<std::int32_t>::min();
        }
        return static_cast<std::int32_t>(parsed);
    }

    errno = 0;
    const unsigned long long parsed = std::strtoull(text, end, 0);
    if (errno == ERANGE || parsed > std::numeric_limits<std::uint32_t>::max()) {
        return static_cast<std::int32_t>(std::numeric_limits<std::uint32_t>::max());
    }
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(parsed));
}

bool ParseCommandValue(CmdDef& def, const std::string_view token,
                       std::size_t& value_consumed) {
    value_consumed = 0;
    const auto value_type = ValueTypeForFlags(def.flags);
    switch (value_type) {
        case CmdValueType::kNone: {

            bool enabled = !def.default_on;
            if (!token.empty()) {
                if (token.front() == '-') {
                    enabled = false;
                    value_consumed = 1;
                } else if (token.front() == '+') {
                    enabled = true;
                    value_consumed = 1;
                }
            }
            const uint32_t preserved =
                static_cast<uint32_t>(def.int_value) & ~def.clear_mask;
            def.int_value = static_cast<int32_t>(
                enabled ? preserved | def.set_mask : preserved);
            if (def.variable) {
                const uint32_t external_preserved =
                    ReadExternalDword(def) & ~def.clear_mask;
                WriteExternalDword(
                    def, enabled ? external_preserved | def.set_mask
                                 : external_preserved);
            }
            return true;
        }
        case CmdValueType::kNumeric: {

            const std::string bounded_token(token);
            char* end = nullptr;
            def.int_value =
                ParseStormNumeric32(bounded_token.c_str(),
                                    (def.flags & 1u) != 0, &end);
            value_consumed = end ? static_cast<std::size_t>(
                                       end - bounded_token.c_str())
                                 : 0;
            WriteExternalIntegerBytes(def);

            return true;
        }
        case CmdValueType::kString:
            ReplaceStringValue(def, token);
            if (def.variable && def.variable_bytes != 0) {
                const std::size_t copy_size = std::min<std::size_t>(
                    token.size(), def.variable_bytes - 1u);
                if (copy_size != 0) {
                    std::memcpy(def.variable, token.data(), copy_size);
                }
                static_cast<char*>(def.variable)[copy_size] = '\0';
            }
            value_consumed = token.size();
            return true;
    }
    return false;
}

void MirrorDuplicateValue(CmdDef& target, const CmdDef& source) {
    target.active = source.active;
    target.int_value = source.int_value;
    if (ValueTypeForFlags(source.flags) == CmdValueType::kString) {
        if (source.string_value) {
            ReplaceStringValue(target, source.string_value);
        } else {
            std::free(target.string_value);
            target.string_value = nullptr;
        }
    }
}

void SyncDuplicateCommands(const StormCmd& storm_cmd, const CmdDef& source) {
    for (const CmdDefList* const list : {&storm_cmd.NormalList(),
                                         &storm_cmd.OptionalList()}) {
        for (CmdDef* const def : list->entries()) {
            if (def == &source || def->id != source.id ||
                ValueTypeForFlags(def->flags) != ValueTypeForFlags(source.flags)) {
                continue;
            }
            MirrorDuplicateValue(*def, source);
        }
    }
}

bool ApplyCommandValue(const StormCmd& cmd, CmdDef& def,
                       const std::string_view value_text,
                       std::size_t& consumed) {
    consumed = 0;
    if (!ParseCommandValue(def, value_text, consumed)) {
        return false;
    }
    def.active = 1;
    if (def.handler) {
        const std::uintptr_t callback_value =
            ValueTypeForFlags(def.flags) == CmdValueType::kString
                ? reinterpret_cast<std::uintptr_t>(def.string_value)
                : static_cast<std::uint32_t>(def.int_value);
        CmdDefCallbackView view{
            .flags = def.flags,
            .id = def.id,
            .name = def.name,
            .variable = def.variable,
            .set_mask = def.set_mask,
            .clear_mask = def.clear_mask,
            .value = callback_value,
        };
        const std::string bounded_value(value_text);
        if (!def.handler(&view, bounded_value.c_str())) {
            return false;
        }
    }
    SyncDuplicateCommands(cmd, def);
    return true;
}

}

void SCmd_ReportError(const char* context, const SCmdErrorCode error_code,
                      void (*callback)(void*)) {

    char formatted[256] = {};
    const char* safe_context = context ? context : "";
    switch (error_code) {
        case SCmdErrorCode::kInvalidParameter:
            std::snprintf(formatted, sizeof(formatted), "Invalid argument: %s",
                          safe_context);
            break;
        case SCmdErrorCode::kBadSyntax:
            std::strncpy(formatted, "The syntax of the command is incorrect.",
                        sizeof(formatted) - 1);
            formatted[sizeof(formatted) - 1] = '\0';
            break;
        case SCmdErrorCode::kOpenFailed:
            std::snprintf(formatted, sizeof(formatted),
                          "Unable to open response file: %s", safe_context);
            break;
        default:
            return;
    }

    if (formatted[0] != '\0') {
        const std::size_t len = std::strlen(formatted);
        if (len + 1 < sizeof(formatted)) {
            formatted[len] = '\n';
            formatted[len + 1] = '\0';
        }
    }

    SErrSetLastError(static_cast<int>(error_code));

    SCmdErrorInfo info{};
    info.error_code       = static_cast<int32_t>(error_code);
    info.context          = context;
    info.formatted_message = formatted;
    callback(&info);
}

CmdDefList::~CmdDefList() {
    Clear();
}

void CmdDefList::Clear() {
    for (auto* def : entries_) {
        if (def->string_value) {
            std::free(def->string_value);
            def->string_value = nullptr;
        }
        delete def;
    }
    entries_.clear();
}

void CmdDefList::Add(CmdDef* def) {
    entries_.push_back(def);
}

void CmdDefList::Remove(CmdDef* def) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (*it == def) {
            entries_.erase(it);
            return;
        }
    }
}

CmdDef* CmdDefList::FindByName(const char* name) const {
    for (auto* def : entries_) {
        if (CommandNamesEqual(*def, name)) return def;
    }
    return nullptr;
}

CmdDef* CmdDefList::FindById(uint32_t id) const {
    for (auto* def : entries_) {
        if (def->id == id) return def;
    }
    return nullptr;
}

CmdDef* CmdDefList::First() const {
    return entries_.empty() ? nullptr : entries_.front();
}

CmdDef* CmdDefList::Next(CmdDef* current) const {
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i] == current && i + 1 < entries_.size()) {
            return entries_[i + 1];
        }
    }
    return nullptr;
}

StormCmd& StormCmd::Instance() {
    static StormCmd inst;
    return inst;
}

bool StormCmd::InitErrorStrings(std::span<const CmdDefInitEntry> entries) {
    if (entries.data() == nullptr) {
        return false;
    }
    if (entries.empty()) {
        return true;
    }

    for (const CmdDefInitEntry& entry : entries) {
        if (!RegisterCommand(entry.flags, entry.id, entry.name,
                             entry.variable, entry.variable_bytes,
                             entry.set_mask, entry.clear_mask,
                             entry.handler)) {
            return false;
        }
    }
    return true;
}

bool StormCmd::RegisterCommand(const uint32_t flags, const uint32_t id,
                               const char* name, void* variable,
                               const uint32_t variable_bytes,
                               const uint32_t set_mask,
                               const uint32_t clear_mask,
                               const CmdDefHandlerFn handler) {
    const char* const safe_name = name ? name : "";
    const std::size_t name_length = std::strlen(safe_name);
    const uint32_t category = flags & 0x3000000u;
    const CmdValueType value_type = ValueTypeForFlags(flags);
    if (name_length >= 16 || (!variable && variable_bytes != 0) ||
        (category == 0 && name_length == 0) ||
        (variable && value_type == CmdValueType::kNone &&
         variable_bytes != sizeof(uint32_t))) {
        SErrSetLastError(87);
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (category == 0x2000000u && added_optional_) {
        SErrSetLastError(87);
        return false;
    }

    std::unique_ptr<CmdDef> def(new (std::nothrow) CmdDef());
    if (!def) {
        return false;
    }
    def->flags = flags;
    def->id = id;
    def->handler = handler;
    def->name_len = static_cast<int32_t>(name_length);
    std::memcpy(def->name, safe_name, name_length);
    def->name[name_length] = '\0';
    def->active = 0;
    def->default_on = value_type == CmdValueType::kNone && (flags & 1u) != 0;
    def->variable = variable;
    def->variable_bytes = variable_bytes;
    def->set_mask = set_mask;
    def->clear_mask = clear_mask;
    def->int_value = def->default_on ? static_cast<int32_t>(set_mask) : 0;

    if (category == 0x1000000u) {
        added_optional_ = true;
    }
    CmdDef* const owned = def.release();
    if (category == 0) {
        normal_list_.Add(owned);
    } else {
        optional_list_.Add(owned);
    }
    return true;
}

bool StormCmd::InitErrorStrings(const void* list, uint32_t count) {
    return InitErrorStrings(std::span(
        static_cast<const CmdDefInitEntry*>(list),
        static_cast<std::size_t>(count)));
}

int StormCmd::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto* list : {&normal_list_, &optional_list_}) {
        for (auto* def : list->entries()) {
            if ((def->flags & 0x30000) == 0x20000 && def->string_value) {
                std::free(def->string_value);
                def->string_value = nullptr;
            }
        }
    }

    normal_list_.Clear();
    optional_list_.Clear();
    added_optional_ = false;
    return 1;
}

int StormCmd::GetBuffer(uint32_t id, char* buffer, uint32_t bufferchars) {
    if (!buffer) return 0;
    buffer[0] = '\0';
    if (!bufferchars) return 0;

    std::lock_guard<std::mutex> lock(mutex_);
    if (CmdDef* const def =
            FindByIdAcrossLists(normal_list_, optional_list_, id)) {
        if (def->string_value) {
            std::strncpy(buffer, def->string_value, bufferchars - 1);
            buffer[bufferchars - 1] = '\0';
        }
        return 1;
    }
    return 0;
}

bool StormCmd::ParseCommandLine(const char* cmdline, const bool use_env,
                                int (*handler)(int),
                                void (*completion)(void*)) {
    if (!cmdline) {
        return false;
    }

    const std::string_view command_line(cmdline);
    std::size_t cursor = 0;

    if (use_env) {
        std::string discard;
        bool discard_quoted = false;
        (void)ConsumeNextToken(command_line, cursor, discard, discard_quoted);
    }

    CmdDef* pending_cmd = nullptr;

    CmdDef* current_node = normal_list_.First();

    return ProcessTokenLoop(command_line, cursor, pending_cmd, current_node,
                            handler, completion);
}

bool StormCmd::ProcessArgument(CmdDef* def, const std::string_view arg_text,
                               std::size_t& consumed) {
    consumed = 0;

    if (!def) {
        return true;
    }

    const std::string_view matched_name(def->name,
                                        static_cast<std::size_t>(def->name_len));

    while (def != nullptr) {
        std::size_t local_consumed = 0;
        if (!ApplyCommandValue(*this, *def, arg_text, local_consumed)) {
            return false;
        }

        if (local_consumed > consumed) {
            consumed = local_consumed;
        }

        def = FindNextSamePrefixMatch(*this, matched_name, def);
    }

    return true;
}

bool StormCmd::ProcessSwitch(std::string_view switch_text, CmdDef*& out_cmd,
                             void (*completion)(void*)) {
    out_cmd = nullptr;

    if (switch_text.empty()) {
        return true;
    }

    std::string_view remaining = switch_text;

    while (!remaining.empty()) {
        std::size_t matched_length = 0;
        CmdDef* const def = FindBestPrefixMatch(*this, remaining,
                                                 matched_length);

        if (!def) {
            if (completion) {
                const std::string temp(remaining);
                SCmd_ReportError(temp.c_str(), SCmdErrorCode::kInvalidParameter,
                                 completion);
            }
            return false;
        }

        std::string_view value = remaining.substr(matched_length);
        while (!value.empty() &&
               (value.front() == '=' || value.front() == ':')) {
            value.remove_prefix(1);
        }

        out_cmd = def;

        if (value.empty() &&
            (def->flags & 0x30000u) != 0) {
            def->active = 1;
            return true;
        }

        std::size_t arg_consumed = 0;
        if (!ProcessArgument(def, value, arg_consumed)) {
            return false;
        }
        out_cmd = nullptr;

        remaining = value.substr(arg_consumed);

    }

    return true;
}

bool StormCmd::ProcessTokenLoop(const std::string_view command_line,
                                std::size_t& cursor,
                                CmdDef*& pending_cmd,
                                CmdDef*& current_node,
                                int (*handler)(int),
                                void (*completion)(void*)) {
    std::string token;
    bool was_quoted = false;

    while (ConsumeNextToken(command_line, cursor, token, was_quoted)) {
        if (token.empty()) {
            continue;
        }

        if (token.front() == '@' && !was_quoted) {
            if (!SCmd_ParseFile(token.c_str() + 1, *this, pending_cmd,
                                current_node, handler, completion)) {
                return false;
            }
            continue;
        }

        if ((token.front() == '-' || token.front() == '/') && !was_quoted) {
            pending_cmd = nullptr;
            const std::string_view command_token(token.c_str() + 1,
                                                 token.size() - 1);
            CmdDef* matched_def = nullptr;
            if (!ProcessSwitch(command_token, matched_def, completion)) {
                return false;
            }

            if (matched_def != nullptr) {
                pending_cmd = matched_def;
            }
            continue;
        }

        if (pending_cmd != nullptr) {
            std::size_t arg_consumed = 0;
            if (!ProcessArgument(pending_cmd, token, arg_consumed)) {
                return false;
            }
            pending_cmd = nullptr;
            continue;
        }

        if (current_node != nullptr) {
            std::size_t discard_consumed = 0;
            if (!ApplyCommandValue(*this, *current_node, token,
                                    discard_consumed)) {
                return false;
            }
            current_node = NormalList().Next(current_node);
            continue;
        }

        if (handler) {
            if (!handler(static_cast<int>(reinterpret_cast<intptr_t>(token.c_str())))) {
                return false;
            }
            continue;
        }
        if (completion) {
            SCmd_ReportError(token.c_str(), SCmdErrorCode::kInvalidParameter,
                             completion);
        }
        return false;
    }

    return true;
}

bool StormCmd::InitCommandLine(int (*handler)(int),
                               void (*completion)(void*)) {
    std::optional<std::string> command_line_override;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        command_line_override = ProcessCommandLineOverride();
    }
    const std::string command_line = command_line_override.has_value()
                                         ? *command_line_override
                                         : openwow::platform::OS_GetCommandLine();
    return ParseCommandLine(command_line.c_str(), true, handler, completion);
}

bool StormCmd::IsCommandEnabled(const uint32_t id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (CmdDef* const def =
            FindByIdAcrossLists(normal_list_, optional_list_, id)) {
        return def->int_value != 0;
    }
    return false;
}

std::string StormCmd::GetCommandString(const uint32_t id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (CmdDef* const def =
            FindByIdAcrossLists(normal_list_, optional_list_, id)) {
        return def->string_value ? std::string(def->string_value) : std::string();
    }
    return {};
}

void StormCmd::SetProcessCommandLineOverrideForTests(
    std::optional<std::string> cmdline) {
    std::lock_guard<std::mutex> lock(mutex_);
    ProcessCommandLineOverride() = std::move(cmdline);
}

void SCmd_ParseBooleanFlag(void* cmdDef, const char* value) {
    if (!cmdDef || !value) return;
    auto* const def = static_cast<CmdDef*>(cmdDef);
    const bool enabled = (*value == '-')
                             ? false
                             : ((*value == '+') ? true : !def->default_on);
    const uint32_t preserved =
        static_cast<uint32_t>(def->int_value) & ~def->clear_mask;
    def->int_value = static_cast<int32_t>(
        enabled ? preserved | def->set_mask : preserved);
    if (def->variable) {
        const uint32_t external_preserved =
            ReadExternalDword(*def) & ~def->clear_mask;
        WriteExternalDword(
            *def, enabled ? external_preserved | def->set_mask
                          : external_preserved);
    }
}

void SCmd_ParseInteger(void* cmdDef, const char* value,
                        uint32_t* bytesConsumed) {
    if (!cmdDef || !value || !bytesConsumed) return;
    auto* const def = static_cast<CmdDef*>(cmdDef);
    char* endPtr = nullptr;
    def->int_value = ParseStormNumeric32(value, (def->flags & 1u) != 0, &endPtr);

    if (endPtr) {
        *bytesConsumed = static_cast<uint32_t>(endPtr - value);
    } else {
        *bytesConsumed = static_cast<uint32_t>(std::strlen(value));
    }
    WriteExternalIntegerBytes(*def);
}

void SCmd_ParseString(void* cmdDef, const char* value,
                       uint32_t* bytesConsumed) {
    if (!cmdDef || !value || !bytesConsumed) return;

    *bytesConsumed = static_cast<uint32_t>(std::strlen(value));
    auto& def = *static_cast<CmdDef*>(cmdDef);
    ReplaceStringValue(def, value);
    if (def.variable && def.variable_bytes != 0) {
        const std::size_t length = std::strlen(value);
        const std::size_t copy_size =
            std::min<std::size_t>(length, def.variable_bytes - 1u);
        if (copy_size != 0) {
            std::memcpy(def.variable, value, copy_size);
        }
        static_cast<char*>(def.variable)[copy_size] = '\0';
    }
}

int SCmd_ParseFile(const char* filename, StormCmd& storm_cmd,
                    CmdDef*& pending_cmd, CmdDef*& current_node,
                    int (*handler)(int), void (*completion)(void*)) {
    if (!filename) {
        if (completion) {
            SCmd_ReportError(filename, SCmdErrorCode::kOpenFailed, completion);
        }
        return 0;
    }

    FILE* f = std::fopen(filename, "rb");
    if (!f) {
        if (completion) {
            SCmd_ReportError(filename, SCmdErrorCode::kOpenFailed, completion);
        }
        return 0;
    }

    std::fseek(f, 0, SEEK_END);
    const long fileSize = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    if (fileSize <= 0) {
        std::fclose(f);
        return 1;
    }

    char* buffer =
        static_cast<char*>(std::malloc(static_cast<size_t>(fileSize) + 1));
    if (!buffer) {
        std::fclose(f);
        return 0;
    }

    const size_t bytesRead =
        std::fread(buffer, 1, static_cast<size_t>(fileSize), f);
    std::fclose(f);
    buffer[bytesRead] = '\0';

    const std::string_view contents(buffer, bytesRead);
    std::size_t cursor = 0;
    const bool result =
        storm_cmd.ProcessTokenLoop(contents, cursor, pending_cmd, current_node,
                                   handler, completion);

    std::free(buffer);
    return result ? 1 : 0;
}

}

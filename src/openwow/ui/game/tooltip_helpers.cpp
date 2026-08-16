
#include "openwow/ui/game/tooltip_helpers.h"

#include "openwow/ui/game/tooltip_internal.h"

#include "openwow/core/storm_string.h"
#include "openwow/foundation/text/ascii.h"
#include "openwow/game/localization.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/frame_script_events.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <lua.hpp>

namespace openwow::ui::game {

int ParseTooltipTexCoords(int start_index, void* lua_state, float* out_coords) {
    if (!lua_state || !out_coords) return start_index;

    auto* L = static_cast<lua_State*>(lua_state);

    out_coords[1] = static_cast<float>(lua_tonumber(L, start_index));
    out_coords[3] = static_cast<float>(lua_tonumber(L, start_index + 1));
    out_coords[0] = static_cast<float>(lua_tonumber(L, start_index + 2));
    out_coords[2] = static_cast<float>(lua_tonumber(L, start_index + 3));

    return start_index + 4;
}

const char* GetColorblindStatSuffix(int stat_index) {

    static constexpr std::array<std::pair<std::string_view, std::string_view>, 5>
        kSuffixes = {{
            {"COLORBLIND_DIFFICULTY_IMPOSSIBLE", " (Impossible)"},
            {"COLORBLIND_DIFFICULTY_VERY_DIFFICULT", " (Very Difficult)"},
            {"COLORBLIND_DIFFICULTY_DIFFICULT", " (Difficult)"},
            {"COLORBLIND_DIFFICULTY_STANDARD", " (Standard)"},
            {"COLORBLIND_DIFFICULTY_TRIVIAL", " (Trivial)"},
        }};
    static thread_local std::string suffix;

    if (!CVarSystem::Instance().GetCVarBool("colorblindMode") ||
        stat_index < 0 || static_cast<std::size_t>(stat_index) >= kSuffixes.size()) {
        return "";
    }

    const auto& [key, fallback] = kSuffixes[static_cast<std::size_t>(stat_index)];
    suffix = openwow::game::Localization::Get().GetString(
        std::string(key), std::string(fallback));
    return suffix.c_str();
}

struct TooltipEventEntry {
    const char* name;
    int offset;
    const char* wrapper_format;
};

static const TooltipEventEntry s_tooltip_events[] = {
    {"OnTooltipSetDefaultAnchor", 1236, nullptr},
    {"OnTooltipCleared",          1244, nullptr},
    {"OnTooltipAddMoney",         1252, "return function(self,cost,maxcost) %s end"},
    {"OnTooltipSetUnit",          1260, nullptr},
    {"OnTooltipSetItem",          1268, nullptr},
    {"OnTooltipSetSpell",         1276, nullptr},
    {"OnTooltipSetQuest",         1284, nullptr},
    {"OnTooltipSetAchievement",   1292, nullptr},
    {"OnTooltipSetEquipmentSet",  1300, nullptr},
    {"OnTooltipSetFrameStack",    1308, nullptr},
};

int RegisterTooltipScriptHandler(void* tooltip, const char* event_name,
                                 void* handler_info) {
    if (!tooltip || !event_name) return 0;

    for (const auto& entry : s_tooltip_events) {
        if (entry.name && openwow::text::EqualsIgnoreCaseAscii(event_name, entry.name)) {

            if (entry.wrapper_format && handler_info) {

            }

            return entry.offset;
        }
    }

    return 0;
}

void LoadTooltipXMLAttributes(void* tooltip, void* xml_node, void* a3,
                              void* a4, int a5) {
    if (!tooltip || !xml_node) return;

    (void)a3; (void)a4; (void)a5;
}

static const uint32_t s_quality_colors[8] = {
    0xFF9D9D9D,
    0xFFFFFFFF,
    0xFF1EFF00,
    0xFF0070DD,
    0xFFA335EE,
    0xFFFF8000,
    0xFFE6CC80,
    0xFFE6CC80,
};

const void* GetItemQualityColorPtr(uint32_t quality) {
    if (quality >= 8) quality = 1;
    return &s_quality_colors[quality];
}

static const char* const s_quality_hex_colors[8] = {
    "|cff9d9d9d",
    "|cffffffff",
    "|cff1eff00",
    "|cff0070dd",
    "|cffa335ee",
    "|cffff8000",
    "|cffe6cc80",
    "|cffe6cc80",
};

const char* GetItemQualityHexColor(uint32_t quality) {
    if (quality >= 8) quality = 1;
    return s_quality_hex_colors[quality];
}

static char s_talent_link_buf[1024];

static const char* s_talent_color = "|cff4e96f7";

const char* BuildTalentLink(const uint32_t* talent_id, const char* talent_name,
                            int talent_rank) {
    static const char kEmpty[] = "";

    if (!talent_id || !talent_name)
        return kEmpty;

    const char* color = s_talent_color;
    const char* reset = "|r";

    if (!color || !*color)
        reset = "";

    std::snprintf(s_talent_link_buf, sizeof(s_talent_link_buf),
                  "%s|Htalent:%d:%d|h[%s]|h%s",
                  color, *talent_id, talent_rank, talent_name, reset);

    return s_talent_link_buf;
}

static constexpr uint64_t kMsPerDay_u    = 86400000ULL;
static constexpr uint64_t kMsPerHour_u   = 3600000ULL;

static constexpr uint64_t kMsPerMinute_u = 60000ULL;
static constexpr uint64_t kMsPerSecond_u = 1000ULL;
static constexpr size_t   kUnitBufSize   = 256;
static constexpr size_t   kResultBufSize = 1024;
static constexpr int      kMaxUnits      = 2;

char* FormatMultiUnitDurationText(char* output, uint32_t output_size,
                                  uint64_t duration_ms,
                                  const char* wrapper_format_key) {
    if (!output || !wrapper_format_key) return output;

    output[0] = '\0';

    char unit_buf[kUnitBufSize] = {};
    char result_buf[kResultBufSize] = {};

    int units_shown = 0;
    uint64_t remaining = duration_ms;

    auto& loc = openwow::game::Localization::Get();

    if (remaining >= kMsPerDay_u) {
        auto fmt = loc.GetString("DAYS_ABBR", "DAYS_ABBR");
        tooltip_internal::FormatTooltipTextFromGlobalString(
            unit_buf, kUnitBufSize, fmt.c_str(),
            static_cast<uint32_t>(remaining / kMsPerDay_u));
        openwow::core::SStrCat(result_buf, unit_buf, kResultBufSize);
        remaining %= kMsPerDay_u;
        units_shown = 1;
    }

    if (remaining >= kMsPerHour_u) {
        if (result_buf[0]) {
            auto delim = loc.GetString("TIME_UNIT_DELIMITER", " ");
            openwow::core::SStrCat(result_buf, delim.c_str(), kResultBufSize);
        }
        auto fmt = loc.GetString("HOURS_ABBR", "HOURS_ABBR");
        tooltip_internal::FormatTooltipTextFromGlobalString(
            unit_buf, kUnitBufSize, fmt.c_str(),
            static_cast<uint32_t>(remaining / kMsPerHour_u));
        openwow::core::SStrCat(result_buf, unit_buf, kResultBufSize);
        remaining %= kMsPerHour_u;
        ++units_shown;
    }

    if (units_shown < kMaxUnits) {
        if (remaining >= kMsPerMinute_u) {
            if (result_buf[0]) {
                auto delim = loc.GetString("TIME_UNIT_DELIMITER", " ");
                openwow::core::SStrCat(result_buf, delim.c_str(), kResultBufSize);
            }
            auto fmt = loc.GetString("MINUTES_ABBR", "MINUTES_ABBR");
            tooltip_internal::FormatTooltipTextFromGlobalString(
                unit_buf, kUnitBufSize, fmt.c_str(),
                static_cast<uint32_t>(remaining / kMsPerMinute_u));
            openwow::core::SStrCat(result_buf, unit_buf, kResultBufSize);
            remaining %= kMsPerMinute_u;
            ++units_shown;
        }

        if (units_shown < kMaxUnits && remaining > 0) {
            if (result_buf[0]) {
                auto delim = loc.GetString("TIME_UNIT_DELIMITER", " ");
                openwow::core::SStrCat(result_buf, delim.c_str(), kResultBufSize);
            }
            auto fmt = loc.GetString("SECONDS_ABBR", "SECONDS_ABBR");
            tooltip_internal::FormatTooltipTextFromGlobalString(
                unit_buf, kUnitBufSize, fmt.c_str(),
                static_cast<uint32_t>(remaining / kMsPerSecond_u));
            openwow::core::SStrCat(result_buf, unit_buf, kResultBufSize);
        }
    }

    auto wrapper_fmt = loc.GetString(std::string(wrapper_format_key),
                                     std::string(wrapper_format_key));
    tooltip_internal::FormatTooltipTextFromGlobalString(
        output, output_size, wrapper_fmt.c_str(), result_buf);

    return output;
}

static constexpr float kMsPerHour = 3600000.0f;
static constexpr float kMsPerDay  = 24.0f * kMsPerHour;

void FormatDurationText(char* output, uint32_t output_size, float duration_ms,
                        const char* prefix_key, int count_param) {
    if (!output || !prefix_key) return;

    output[0] = '\0';

    const char* key_suffix;
    float value;

    if (duration_ms >= kMsPerDay) {
        key_suffix = "_DAYS";
        value = duration_ms / kMsPerDay;
    } else if (duration_ms >= kMsPerHour) {
        key_suffix = "_HOURS";
        value = duration_ms / kMsPerHour;
    } else if (duration_ms >= 60000.0f) {
        key_suffix = "_MIN";
        value = duration_ms * 0.000016666667f;
    } else {
        key_suffix = "_SEC";
        value = duration_ms * 0.001f;
    }

    if (value < 0.0f) value = 0.0f;

    char key_buf[256];
    std::snprintf(key_buf, sizeof(key_buf), "%s%s", prefix_key, key_suffix);

    const std::string localized =
        openwow::game::Localization::Get().GetString(
            std::string(key_buf), std::string(key_buf));

    if (count_param) {
        tooltip_internal::FormatTooltipTextFromGlobalString(
            output, output_size, localized.c_str(), count_param,
            static_cast<double>(value));
    } else {
        tooltip_internal::FormatTooltipTextFromGlobalString(
            output, output_size, localized.c_str(),
            static_cast<double>(value));
    }
}

enum AnchorPoint {
    ANCHOR_TOP = 0,
    ANCHOR_TOPLEFT = 1,
    ANCHOR_LEFT = 2,
    ANCHOR_BOTTOMLEFT = 3,
    ANCHOR_BOTTOM_UNUSED = 4,
    ANCHOR_BOTTOMRIGHT_UNUSED = 5,
    ANCHOR_RIGHT = 6,
    ANCHOR_BOTTOMRIGHT = 7,
    ANCHOR_BOTTOM = 8,
};

void UpdateTooltipLayout(void* tooltip, bool force_update) {
    if (!tooltip) return;

    auto* t = static_cast<uint32_t*>(tooltip);
    uint32_t owner = t[167];
    if (!owner) return;

    uint32_t mode = t[168];
    if (mode == 10) return;

    if (force_update || mode != 9) {
    }

    float offsetX = *reinterpret_cast<float*>(&t[248]);
    float offsetY = *reinterpret_cast<float*>(&t[249]);

    void* owner_frame = (owner != 0) ? reinterpret_cast<void*>(owner + 32) : nullptr;

    switch (mode) {
        case 0:

            break;
        case 1:
            break;
        case 2:
            break;
        case 3:
            break;
        case 4:
            break;
        case 5:
            break;
        case 6:
            break;
        case 7:
            break;
        default:
            break;
    }

    (void)offsetX; (void)offsetY; (void)owner_frame;
}

namespace {
const uint32_t* g_itemSetThresholdTable = nullptr;
}

void SetItemSetThresholdTableForSort(const uint32_t* thresholds) {
    g_itemSetThresholdTable = thresholds;
}

int CompareItemSetSpellThreshold(const void* a1, const void* a2) {
    auto idx_a = static_cast<uint8_t>(*static_cast<const uint8_t*>(a1));
    auto idx_b = static_cast<uint8_t>(*static_cast<const uint8_t*>(a2));

    uint32_t thresh_a = g_itemSetThresholdTable[idx_a];
    uint32_t thresh_b = g_itemSetThresholdTable[idx_b];

    if (thresh_a > thresh_b)
        return 1;
    if (thresh_a < thresh_b)
        return -1;

    return (idx_a < idx_b) ? -1 : 1;
}

int FrameStackInfo_Compare(const void* lhs, const void* rhs) {
    auto* a = static_cast<const int32_t*>(lhs);
    auto* b = static_cast<const int32_t*>(rhs);

    int32_t strata_a = a[256];
    int32_t strata_b = b[256];
    if (strata_a < strata_b) return 1;
    if (strata_a > strata_b) return -1;

    int32_t level_a = a[257];
    int32_t level_b = b[257];
    if (level_a < level_b) return 1;
    if (level_a > level_b) return -1;

    return openwow::core::SStrCmpNoCase(
        static_cast<const char*>(lhs),
        static_cast<const char*>(rhs),
        0x7FFFFFFFu);
}

int FireOnTooltipSetDefaultAnchor(int* tooltip) {
    using openwow::ui::frame_script_events::CSimpleAnim_FireScriptHandler;
    using openwow::ui::frame_script_events::ScriptHandlerRef;

    auto* handler_slot = reinterpret_cast<ScriptHandlerRef*>(tooltip + 309);
    if (handler_slot->scriptRef) {
        return CSimpleAnim_FireScriptHandler(tooltip, handler_slot, 0, 0);
    }
    return reinterpret_cast<intptr_t>(handler_slot);
}

}

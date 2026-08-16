#pragma once

#include <cstdint>
#include <string_view>

namespace openwow::data::dbc { class DbcLoader; }
namespace openwow::game {
class CGItem_C;
class CGPlayer_C;
class ObjectManager;
struct ItemTemplate;
}
namespace openwow::ui::game {

const char* GetStatModifierGlobalStringName(std::uint32_t modifier_id,
                                            char* output, int output_size);
const char* FormatAchievementLink(int achievement_id, std::uint64_t player_guid,
                                  std::uint8_t completed, int month, int day,
                                  int year, std::uint32_t criteria1,
                                  std::uint32_t criteria2, std::uint32_t criteria3,
                                  std::uint32_t criteria4);
const char* FormatQuestLink(std::uint32_t quest_id, std::int32_t quest_level,
                            std::string_view title,
                            const openwow::game::CGPlayer_C* player);
const char* FormatGlyphLink(int slot_id, int glyph_id);
int Tooltip_ComputeScalingLevel(const openwow::game::ItemTemplate& item,
                                std::uint32_t context_level,
                                std::uint64_t owner_guid, bool force_default,
                                const openwow::game::ObjectManager* objects = nullptr,
                                const openwow::data::dbc::DbcLoader* dbc = nullptr,
                                std::uint64_t merchant_guid = 0);
}

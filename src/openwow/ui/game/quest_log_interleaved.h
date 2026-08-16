#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {
class WorldSession;
struct QuestTemplate;
class CGPlayer_C;
}

namespace openwow::ui::game::detail {

struct InterleavedQuestLogEntry {
  bool is_header = false;
  std::string header_name;
  bool collapsed = false;
  std::int32_t sort_key = 0;
  std::size_t quest_log_index = 0;
};

struct QuestLogView {
  std::vector<InterleavedQuestLogEntry> entries;
  std::vector<std::int32_t> sort_order;
  std::size_t visible_count = 0;
  std::size_t quest_count = 0;
};

QuestLogView BuildQuestLogView(openwow::game::WorldSession &session);
int ResolveInterleavedToQuestIndex(openwow::game::WorldSession &session,
                                   int interleaved_1based);
std::uint32_t ResolveQuestIdFromInterleavedIndex(openwow::game::WorldSession &session,
                                                 int interleaved_1based);
int FindInterleavedQuestIndexById(openwow::game::WorldSession &session,
                                  std::uint32_t quest_id);
bool IsVisibleQuestFailedById(openwow::game::WorldSession &session, std::uint32_t quest_id);
std::optional<std::int32_t> ResolveQuestSortKeyFromInterleavedIndex(
    openwow::game::WorldSession &session, int interleaved_1based);
int ResolveQuestSortSlotFromInterleavedIndex(openwow::game::WorldSession &session,
                                             int interleaved_1based);
int FindSelectedInterleavedQuestIndex(openwow::game::WorldSession &session,
                                      std::uint32_t selected_quest_id);
void SetAllQuestLogHeadersCollapsed(bool collapsed);
void SetQuestLogHeaderCollapsedBySortSlot(std::size_t sort_slot, bool collapsed);
std::int32_t DecodeQuestSortKey(std::uint32_t raw_sort_key);
int ResolveQuestTemplateDisplayLevel(const ::openwow::game::QuestTemplate &quest_template,
                                     const ::openwow::game::CGPlayer_C *player);
std::vector<InterleavedQuestLogEntry>
BuildInterleavedQuestLog(::openwow::game::WorldSession &session);

}

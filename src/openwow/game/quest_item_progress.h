
#pragma once

#include <cstdint>

namespace openwow::game {

class WorldSession;
struct QuestTemplate;

bool UpdateQuestItemObjectiveProgress(const QuestTemplate &quest,
                                      std::uint32_t item_id,
                                      std::uint32_t count_received,
                                      std::uint32_t total_count_after,
                                      WorldSession &session);

void Player_CheckQuestItemObjectiveProgress(std::uint32_t item_id,
                                            std::uint32_t count_received,
                                            std::uint32_t total_count_after,
                                            WorldSession &session);

}

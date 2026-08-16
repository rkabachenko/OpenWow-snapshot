#pragma once

#include "openwow/game/async_query_channel.h"

#include <cstdint>
#include <string>

namespace openwow::game {
class WorldSession;
}

namespace openwow::ui::game {

struct QuestRequirementQueryCallbacks {
  openwow::game::AsyncQueryChannel::Callback on_quest_template_query;
  openwow::game::AsyncQueryChannel::Callback on_gameobject_template_query;
  openwow::game::AsyncQueryChannel::Callback on_creature_template_query;
  openwow::game::AsyncQueryChannel::Callback on_item_template_query;
};

struct QuestLeaderboardLine {
  std::string text;
  std::string type;
  bool finished = false;
};

openwow::game::AsyncQueryChannel::Callback
BuildQuestRequirementQueryCallback(std::string failure_message);

int CountQuestLeaderboardObjectives(openwow::game::WorldSession &session, std::uint32_t quest_id,
                                    const QuestRequirementQueryCallbacks &callbacks);

bool BuildQuestLeaderboardLine(openwow::game::WorldSession &session, std::uint32_t quest_id,
                               int objective_index, bool include_progress,
                               const QuestRequirementQueryCallbacks &callbacks,
                               QuestLeaderboardLine *out);

}

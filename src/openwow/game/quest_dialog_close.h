#pragma once

#include "openwow/game/object_guid.h"

#include <cstdint>

namespace openwow::game {

class QuestManager;
class WorldSession;

struct QuestDialogCloseState {
  ObjectGuid interaction_guid;
  ObjectGuid secondary_guid;
  std::uint32_t quest_id = 0;
  bool is_open = false;
  bool close_on_decline = false;
};

[[nodiscard]] QuestDialogCloseState GetActiveQuestDialogCloseState(const QuestManager &quests);

void CloseQuestDialogLikeIda58CA70(WorldSession &session, const QuestDialogCloseState &dialog,
                                   bool keep_dialog_open,
                                   bool notify_server_for_shared_player_dialog);

}

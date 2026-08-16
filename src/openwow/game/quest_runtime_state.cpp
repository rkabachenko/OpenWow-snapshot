#include "openwow/game/quest_runtime_state.h"

#include "openwow/game/quest_log.h"
#include "openwow/game/quest_poi.h"
#include "openwow/game/quest_query_bridge.h"

namespace openwow::game {

void PrepareQuestRuntimeStateForLogout() {
  QuestLog::Get().PrepareForLogout();
  QuestQueryBridge::Get().Reset();
  QuestPOIData::Get().Clear();
}

void FinalizeQuestRuntimeStateAfterLogout() {
  QuestLog::Get().Reset();
}

}

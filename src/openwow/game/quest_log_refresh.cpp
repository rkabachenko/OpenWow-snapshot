
#include "openwow/game/quest_log_refresh.h"

#include "openwow/core/console.h"

namespace openwow::game {

namespace {

std::int32_t s_pending_quest_template_queries = 0;

}

std::int32_t& GetPendingQuestTemplateQueryCount() {
    return s_pending_quest_template_queries;
}

void QuestTemplateQueryCallback_RefreshQuestLog(bool success) {
    if (!success) {
        core::ida::ConsoleAddLine("Invalid quest log entry", 0);
        return;
    }

    bool all_resolved = (s_pending_quest_template_queries == 0);
    if (s_pending_quest_template_queries > 0) {
        all_resolved = (--s_pending_quest_template_queries == 0);
    }

    if (all_resolved) {
        CGQuestLog_RefreshFromPlayerState(true);
    }
}

}

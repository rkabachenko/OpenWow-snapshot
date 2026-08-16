#pragma once

#include <cstdint>

namespace openwow::game {

[[nodiscard]] std::int32_t& GetPendingQuestTemplateQueryCount();

void QuestTemplateQueryCallback_RefreshQuestLog(bool success);

void CGQuestLog_RefreshFromPlayerState(bool rebuild);

}

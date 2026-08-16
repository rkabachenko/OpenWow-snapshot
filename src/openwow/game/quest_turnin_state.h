#pragma once

#include <cstdint>

namespace openwow::game {

class WorldSession;

[[nodiscard]] std::int32_t DecodeQuestMoneyRequirement(std::uint32_t raw_reward_money);
[[nodiscard]] bool IsQuestTurnInReady(const WorldSession &session, std::uint32_t quest_id);

}

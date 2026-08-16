#pragma once

#include "openwow/game/object_guid.h"

#include <chrono>
#include <cstdint>
#include <optional>

namespace openwow::game {
class WorldSession;
}

namespace openwow::game::combat::death::ui {

void RefreshAreaSpiritHealer(WorldSession& session);
[[nodiscard]] bool AcceptAreaSpiritHeal(WorldSession& session);
void CancelAreaSpiritHeal(WorldSession& session);

void SetAreaSpiritHealerCountdown(WorldSession& session,
                                  ObjectGuid healer,
                                  std::chrono::milliseconds delay);
[[nodiscard]] std::chrono::seconds GetAreaSpiritHealerRemainingTime(
    const WorldSession& session);
void InteractWithSpiritGuide(WorldSession& session, ObjectGuid spirit_guide);

[[nodiscard]] bool AcceptSpiritHealerXpLoss(WorldSession& session);

[[nodiscard]] std::optional<std::int32_t> HandleSpiritHealerConfirm(
    WorldSession& session, ObjectGuid healer);
[[nodiscard]] std::int32_t GetSpiritHealerXpLoss(const WorldSession& session);

}

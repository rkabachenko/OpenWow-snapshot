#pragma once

#include <cstdint>

#include "openwow/game/world_session_fwd.h"

namespace openwow::game {

[[nodiscard]] bool ReadableTextHasNextPage(const WorldSession& session);
[[nodiscard]] std::uint32_t GetReadableGameObjectFirstPageId(
    const WorldSession& session, std::uint64_t guid);
[[nodiscard]] std::uint64_t GetActiveReadableInteractionGuid(
    const WorldSession& session);
[[nodiscard]] bool ToggleOrBeginReadableObjectInteraction(
    WorldSession& session, std::uint64_t guid);
void HandleReadItemOk(WorldSession& session, std::uint64_t guid);
void HandleReadItemFailed(WorldSession& session, std::uint64_t guid,
                          std::uint32_t status,
                          std::uint32_t translation_delay_ms);
void ReloadReadableObjectAfterAsyncDependency(WorldSession& session,
                                              std::uint64_t owner_guid);
void LoadCurrentReadableTextPage(WorldSession& session,
                                 bool include_active_player_context);

void CloseReadableObjectInteraction(WorldSession& session);

}

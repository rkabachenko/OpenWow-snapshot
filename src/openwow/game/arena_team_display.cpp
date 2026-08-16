
#include "openwow/game/arena_team_display.h"

#include <algorithm>
#include <cmath>

namespace openwow::game {

void ArenaTeamDisplay::SetTeam(ArenaTeamSize size,
                               const ArenaTeamDisplayInfo& info) {
    teams_[size] = info;

    teams_[size].size = size;
}

std::optional<ArenaTeamDisplayInfo> ArenaTeamDisplay::GetTeam(
    ArenaTeamSize size) const {
    auto it = teams_.find(size);
    if (it == teams_.end()) return std::nullopt;
    return it->second;
}

bool ArenaTeamDisplay::HasTeam(ArenaTeamSize size) const {
    return teams_.count(size) > 0;
}

size_t ArenaTeamDisplay::GetTeamCount() const {
    return teams_.size();
}

size_t ArenaTeamDisplay::GetMemberCount(ArenaTeamSize size) const {
    auto it = teams_.find(size);
    if (it == teams_.end()) return 0;
    return it->second.members.size();
}

float ArenaTeamDisplay::GetWinRate(ArenaTeamSize size) const {
    auto it = teams_.find(size);
    if (it == teams_.end()) return 0.0f;

    const auto& info = it->second;
    if (info.seasonGames == 0) return 0.0f;

    const float rate = static_cast<float>(info.seasonWins) /
                       static_cast<float>(info.seasonGames);

    return std::clamp(rate, 0.0f, 1.0f);
}

std::vector<ArenaTeamSize> ArenaTeamDisplay::GetTeamSizes() const {
    std::vector<ArenaTeamSize> sizes;
    sizes.reserve(teams_.size());
    for (const auto& [sz, _] : teams_) {
        sizes.push_back(sz);
    }
    return sizes;
}

void ArenaTeamDisplay::Reset() {
    teams_.clear();
}

}

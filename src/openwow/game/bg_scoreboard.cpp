
#include "openwow/game/bg_scoreboard.h"

namespace openwow::game {

int BGScoreboard::FindIndex(ObjectGuid guid) const {
    for (size_t i = 0; i < scores_.size(); ++i) {
        if (scores_[i].guid.GetRawValue() == guid.GetRawValue())
            return static_cast<int>(i);
    }
    return -1;
}

void BGScoreboard::SetScores(const std::vector<BGScoreEntry>& scores,
                             BGType type) {
    scores_ = scores;
    bgType_ = type;
}

const std::vector<BGScoreEntry>& BGScoreboard::GetScores() const {
    return scores_;
}

std::optional<BGScoreEntry> BGScoreboard::GetScore(ObjectGuid guid) const {
    int idx = FindIndex(guid);
    if (idx < 0) return std::nullopt;
    return scores_[static_cast<size_t>(idx)];
}

std::optional<BGScoreEntry> BGScoreboard::GetMyScore(
    ObjectGuid localGuid) const {
    return GetScore(localGuid);
}

std::vector<BGScoreEntry> BGScoreboard::GetAllianceScores() const {
    std::vector<BGScoreEntry> result;
    for (const auto& e : scores_) {
        if (e.faction == 0) result.push_back(e);
    }
    return result;
}

std::vector<BGScoreEntry> BGScoreboard::GetHordeScores() const {
    std::vector<BGScoreEntry> result;
    for (const auto& e : scores_) {
        if (e.faction == 1) result.push_back(e);
    }
    return result;
}

uint32_t BGScoreboard::GetPlayerCount() const {
    return static_cast<uint32_t>(scores_.size());
}

void BGScoreboard::SortByKills() {
    std::sort(scores_.begin(), scores_.end(),
              [](const BGScoreEntry& a, const BGScoreEntry& b) {
                  return a.kills > b.kills;
              });
}

void BGScoreboard::SortByDeaths() {
    std::sort(scores_.begin(), scores_.end(),
              [](const BGScoreEntry& a, const BGScoreEntry& b) {
                  return a.deaths > b.deaths;
              });
}

void BGScoreboard::SortByHonor() {
    std::sort(scores_.begin(), scores_.end(),
              [](const BGScoreEntry& a, const BGScoreEntry& b) {
                  return a.honorGained > b.honorGained;
              });
}

void BGScoreboard::SortByDamage() {
    std::sort(scores_.begin(), scores_.end(),
              [](const BGScoreEntry& a, const BGScoreEntry& b) {
                  return a.damage > b.damage;
              });
}

void BGScoreboard::SortByHealing() {
    std::sort(scores_.begin(), scores_.end(),
              [](const BGScoreEntry& a, const BGScoreEntry& b) {
                  return a.healing > b.healing;
              });
}

std::string BGScoreboard::GetBGSpecificHeader(uint32_t columnIndex) const {
    if (columnIndex >= bgSpecificHeaders_.size()) return {};
    return bgSpecificHeaders_[columnIndex];
}

void BGScoreboard::SetBGSpecificHeaders(
    const std::vector<std::string>& headers) {
    bgSpecificHeaders_ = headers;
}

void BGScoreboard::Reset() {
    scores_.clear();
    bgType_ = BGType::AlteracValley;
    allianceScore_ = 0;
    hordeScore_ = 0;
    winner_ = -1;
    bgSpecificHeaders_.clear();
    isOpen_ = false;
    timeElapsed_ = 0.0f;
}

void BGScoreboardDisplay::SetBattleground(const std::string& bgName,
                                          uint32_t bgType) {
    bgName_ = bgName;
    bgType_ = bgType;
}

void BGScoreboardDisplay::AddEntry(const BGScoreDisplayEntry& entry) {

    for (auto& e : entries_) {
        if (e.playerGuid == entry.playerGuid) {
            e = entry;
            return;
        }
    }
    entries_.push_back(entry);
}

void BGScoreboardDisplay::ClearEntries() {
    entries_.clear();
}

std::vector<BGScoreDisplayEntry> BGScoreboardDisplay::GetEntries() const {
    return entries_;
}

std::vector<BGScoreDisplayEntry> BGScoreboardDisplay::GetEntriesByFaction(
    uint8_t faction) const {
    std::vector<BGScoreDisplayEntry> result;
    for (const auto& e : entries_) {
        if (e.faction == faction) result.push_back(e);
    }
    return result;
}

namespace {

uint32_t GetColumnValue(const BGScoreDisplayEntry& e, BGScoreColumn col) {
    switch (col) {
        case BGScoreColumn::Kills:       return e.kills;
        case BGScoreColumn::Deaths:      return e.deaths;
        case BGScoreColumn::HonorGained: return e.honorGained;
        case BGScoreColumn::BonusHonor:  return e.bonusHonor;
        case BGScoreColumn::DamageDone:  return e.damageDone;
        case BGScoreColumn::HealingDone: return e.healingDone;
        default: {
            auto it = e.extraStats.find(col);
            if (it != e.extraStats.end()) return it->second;
            return 0;
        }
    }
}

}

std::vector<BGScoreDisplayEntry> BGScoreboardDisplay::SortBy(
    BGScoreColumn col, bool descending) const {
    auto sorted = entries_;
    std::sort(sorted.begin(), sorted.end(),
              [&](const BGScoreDisplayEntry& a,
                  const BGScoreDisplayEntry& b) {
                  auto va = GetColumnValue(a, col);
                  auto vb = GetColumnValue(b, col);
                  return descending ? (va > vb) : (va < vb);
              });
    return sorted;
}

uint32_t BGScoreboardDisplay::GetTeamKills(uint8_t faction) const {
    uint32_t total = 0;
    for (const auto& e : entries_) {
        if (e.faction == faction) total += e.kills;
    }
    return total;
}

uint64_t BGScoreboardDisplay::GetTeamDamage(uint8_t faction) const {
    uint64_t total = 0;
    for (const auto& e : entries_) {
        if (e.faction == faction) total += e.damageDone;
    }
    return total;
}

uint64_t BGScoreboardDisplay::GetTeamHealing(uint8_t faction) const {
    uint64_t total = 0;
    for (const auto& e : entries_) {
        if (e.faction == faction) total += e.healingDone;
    }
    return total;
}

std::optional<BGScoreDisplayEntry>
BGScoreboardDisplay::GetLocalPlayerEntry() const {
    for (const auto& e : entries_) {
        if (e.isLocalPlayer) return e;
    }
    return std::nullopt;
}

size_t BGScoreboardDisplay::GetPlayerCount() const {
    return entries_.size();
}

std::string BGScoreboardDisplay::GetBattlegroundName() const {
    return bgName_;
}

void BGScoreboardDisplay::SetWinner(uint8_t faction) {
    winner_ = faction;
}

std::optional<uint8_t> BGScoreboardDisplay::GetWinner() const {
    return winner_;
}

void BGScoreboardDisplay::Reset() {
    entries_.clear();
    bgName_.clear();
    bgType_ = 0;
    winner_.reset();
}

}

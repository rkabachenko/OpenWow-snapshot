#pragma once

#include "openwow/game/object_guid.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class BGType : uint32_t {
    AlteracValley      = 0,
    WarsongGulch       = 1,
    ArathiBasin        = 2,
    EyeOfTheStorm      = 3,
    StrandOfTheAncients = 4,
    IsleOfConquest      = 5,
    Wintergrasp        = 6,
};

struct BGScoreEntry {
    ObjectGuid guid;
    std::string name;
    uint32_t kills = 0;
    uint32_t deaths = 0;
    uint32_t honorGained = 0;
    uint32_t faction = 0;
    uint32_t classId = 0;
    uint32_t damage = 0;
    uint32_t healing = 0;
    int32_t ratingChange = 0;
    uint32_t rank = 0;
    uint32_t bgSpecific1 = 0;
    uint32_t bgSpecific2 = 0;
    uint32_t bgSpecific3 = 0;
};

class BGScoreboard {
 public:
    BGScoreboard() = default;

    void SetScores(const std::vector<BGScoreEntry>& scores, BGType type);
    [[nodiscard]] const std::vector<BGScoreEntry>& GetScores() const;
    [[nodiscard]] BGType GetBGType() const { return bgType_; }

    [[nodiscard]] std::optional<BGScoreEntry> GetScore(ObjectGuid guid) const;
    [[nodiscard]] std::optional<BGScoreEntry> GetMyScore(
        ObjectGuid localGuid) const;

    [[nodiscard]] std::vector<BGScoreEntry> GetAllianceScores() const;
    [[nodiscard]] std::vector<BGScoreEntry> GetHordeScores() const;

    [[nodiscard]] uint32_t GetAllianceTeamScore() const {
        return allianceScore_;
    }
    void SetAllianceTeamScore(uint32_t score) { allianceScore_ = score; }
    [[nodiscard]] uint32_t GetHordeTeamScore() const { return hordeScore_; }
    void SetHordeTeamScore(uint32_t score) { hordeScore_ = score; }

    [[nodiscard]] uint32_t GetPlayerCount() const;

    void SortByKills();
    void SortByDeaths();
    void SortByHonor();
    void SortByDamage();
    void SortByHealing();

    [[nodiscard]] int32_t GetWinner() const { return winner_; }
    void SetWinner(int32_t winner) { winner_ = winner; }

    [[nodiscard]] std::string GetBGSpecificHeader(
        uint32_t columnIndex) const;
    void SetBGSpecificHeaders(const std::vector<std::string>& headers);

    [[nodiscard]] bool IsOpen() const { return isOpen_; }
    void Open() { isOpen_ = true; }
    void Close() { isOpen_ = false; }

    [[nodiscard]] float GetTimeElapsed() const { return timeElapsed_; }
    void SetTimeElapsed(float t) { timeElapsed_ = t; }

    void Reset();

 private:
    std::vector<BGScoreEntry> scores_;
    BGType bgType_ = BGType::AlteracValley;
    uint32_t allianceScore_ = 0;
    uint32_t hordeScore_ = 0;
    int32_t winner_ = -1;
    std::vector<std::string> bgSpecificHeaders_;
    bool isOpen_ = false;
    float timeElapsed_ = 0.0f;

    [[nodiscard]] int FindIndex(ObjectGuid guid) const;
};

enum class BGScoreColumn : uint8_t {
    Kills,
    Deaths,
    HonorGained,
    BonusHonor,
    DamageDone,
    HealingDone,
    FlagCaptures,
    FlagReturns,
    BasesAssaulted,
    BasesDefended,
};

struct BGScoreDisplayEntry {
    ObjectGuid playerGuid{};
    std::string playerName;
    uint32_t kills = 0;
    uint32_t deaths = 0;
    uint32_t honorGained = 0;
    uint32_t bonusHonor = 0;
    uint32_t damageDone = 0;
    uint32_t healingDone = 0;
    uint8_t faction = 0;
    uint8_t classId = 0;
    bool isLocalPlayer = false;
    std::map<BGScoreColumn, uint32_t> extraStats;
};

class BGScoreboardDisplay {
 public:
    BGScoreboardDisplay() = default;

    void SetBattleground(const std::string& bgName, uint32_t bgType);

    void AddEntry(const BGScoreDisplayEntry& entry);
    void ClearEntries();
    [[nodiscard]] std::vector<BGScoreDisplayEntry> GetEntries() const;
    [[nodiscard]] std::vector<BGScoreDisplayEntry> GetEntriesByFaction(
        uint8_t faction) const;
    [[nodiscard]] std::vector<BGScoreDisplayEntry> SortBy(
        BGScoreColumn col, bool descending = true) const;

    [[nodiscard]] uint32_t GetTeamKills(uint8_t faction) const;
    [[nodiscard]] uint64_t GetTeamDamage(uint8_t faction) const;
    [[nodiscard]] uint64_t GetTeamHealing(uint8_t faction) const;

    [[nodiscard]] std::optional<BGScoreDisplayEntry> GetLocalPlayerEntry() const;

    [[nodiscard]] size_t GetPlayerCount() const;
    [[nodiscard]] std::string GetBattlegroundName() const;

    void SetWinner(uint8_t faction);
    [[nodiscard]] std::optional<uint8_t> GetWinner() const;

    void Reset();

 private:
    std::vector<BGScoreDisplayEntry> entries_;
    std::string bgName_;
    uint32_t bgType_ = 0;
    std::optional<uint8_t> winner_;
};

}

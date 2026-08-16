
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include "openwow/game/object_guid.h"

namespace openwow::game {

enum class PartyInviteType : std::uint8_t {
    Party             = 0,
    Raid              = 1,
    BattlegroundGroup = 2,
};

struct PartyInviteEntry {
    ObjectGuid inviterGuid;
    std::string inviterName;
    PartyInviteType type = PartyInviteType::Party;
    float timeRemaining = 0.0f;
    float maxTime = 0.0f;
};

class PartyInviteDisplay {
 public:
    void SetPendingInvite(PartyInviteEntry invite);
    [[nodiscard]] std::optional<PartyInviteEntry> GetPendingInvite() const;
    [[nodiscard]] bool HasPendingInvite() const;

    void Update(float dt);
    [[nodiscard]] bool IsExpired() const;
    [[nodiscard]] float GetTimeRemaining() const;

    void AcceptInvite();
    void DeclineInvite();
    void CancelInvite();

    [[nodiscard]] std::string GetInviteText() const;

    void SetSuggestRole(std::uint8_t role);
    [[nodiscard]] std::uint8_t GetSuggestedRole() const;

    void Reset();

 private:
    mutable std::mutex mutex_;
    std::optional<PartyInviteEntry> pending_;
    std::uint8_t suggestedRole_ = 0;
    bool accepted_ = false;
    bool declined_ = false;
};

}

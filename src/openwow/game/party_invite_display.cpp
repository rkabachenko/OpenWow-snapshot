
#include "openwow/game/party_invite_display.h"

namespace openwow::game {

void PartyInviteDisplay::SetPendingInvite(PartyInviteEntry invite) {
    std::lock_guard lock(mutex_);
    pending_ = std::move(invite);
    accepted_ = false;
    declined_ = false;
}

std::optional<PartyInviteEntry> PartyInviteDisplay::GetPendingInvite() const {
    std::lock_guard lock(mutex_);
    return pending_;
}

bool PartyInviteDisplay::HasPendingInvite() const {
    std::lock_guard lock(mutex_);
    return pending_.has_value() && !accepted_ && !declined_;
}

void PartyInviteDisplay::Update(float dt) {
    std::lock_guard lock(mutex_);
    if (!pending_.has_value()) return;
    pending_->timeRemaining -= dt;
    if (pending_->timeRemaining < 0.0f) {
        pending_->timeRemaining = 0.0f;
    }
}

bool PartyInviteDisplay::IsExpired() const {
    std::lock_guard lock(mutex_);
    if (!pending_.has_value()) return false;
    return pending_->timeRemaining <= 0.0f;
}

float PartyInviteDisplay::GetTimeRemaining() const {
    std::lock_guard lock(mutex_);
    if (!pending_.has_value()) return 0.0f;
    return pending_->timeRemaining;
}

void PartyInviteDisplay::AcceptInvite() {
    std::lock_guard lock(mutex_);
    if (pending_.has_value()) {
        accepted_ = true;
    }
}

void PartyInviteDisplay::DeclineInvite() {
    std::lock_guard lock(mutex_);
    if (pending_.has_value()) {
        declined_ = true;
    }
}

void PartyInviteDisplay::CancelInvite() {
    std::lock_guard lock(mutex_);
    pending_.reset();
    accepted_ = false;
    declined_ = false;
}

std::string PartyInviteDisplay::GetInviteText() const {
    std::lock_guard lock(mutex_);
    if (!pending_.has_value()) return {};

    const auto& inv = *pending_;
    std::string typeStr;
    switch (inv.type) {
        case PartyInviteType::Party:
            typeStr = "a group";
            break;
        case PartyInviteType::Raid:
            typeStr = "a raid group";
            break;
        case PartyInviteType::BattlegroundGroup:
            typeStr = "a battleground group";
            break;
    }
    return inv.inviterName + " has invited you to join " + typeStr + ".";
}

void PartyInviteDisplay::SetSuggestRole(std::uint8_t role) {
    std::lock_guard lock(mutex_);
    suggestedRole_ = role;
}

std::uint8_t PartyInviteDisplay::GetSuggestedRole() const {
    std::lock_guard lock(mutex_);
    return suggestedRole_;
}

void PartyInviteDisplay::Reset() {
    std::lock_guard lock(mutex_);
    pending_.reset();
    suggestedRole_ = 0;
    accepted_ = false;
    declined_ = false;
}

}

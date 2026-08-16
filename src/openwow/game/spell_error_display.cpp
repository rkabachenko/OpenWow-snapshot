
#include "openwow/game/spell_error_display.h"

#include <algorithm>
#include <cmath>

namespace openwow::game {

std::string SpellErrorDisplay::GetErrorMessage(SpellFailReason reason) {
    switch (reason) {
        case SpellFailReason::OutOfRange:             return "Out of range";
        case SpellFailReason::OutOfMana:              return "Not enough mana";
        case SpellFailReason::NotReady:               return "Ability is not ready yet";
        case SpellFailReason::ItemNotReady:           return "Item is not ready yet";
        case SpellFailReason::NotInControl:           return "You are not in control of your actions";
        case SpellFailReason::Interrupted:            return "Interrupted";
        case SpellFailReason::CantDoThatYetMoving:    return "Can't do that while moving";
        case SpellFailReason::NotEnoughComboPoints:   return "Not enough combo points";
        case SpellFailReason::TargetNotInLineOfSight: return "Target not in line of sight";
        case SpellFailReason::CantUseInCombat:        return "Can't use that in combat";
        case SpellFailReason::NoTarget:               return "You have no target";
        case SpellFailReason::AlreadyAtFullHealth:    return "Target is already at full health";
        case SpellFailReason::AlreadyAtFullPower:     return "Already at full power";
        case SpellFailReason::NothingToDispel:        return "Nothing to dispel";
        case SpellFailReason::TargetTooClose:         return "Target too close";
        case SpellFailReason::TargetIsDead:           return "Your target is dead";
        case SpellFailReason::NoAmmo:                 return "You don't have any ammunition";
        case SpellFailReason::InvalidTarget:          return "Invalid target";
        case SpellFailReason::PlayerIsDead:           return "You are dead";
        case SpellFailReason::TooManyOfItem:          return "You have too many of that item";
        case SpellFailReason::CantDoWhileMoving:      return "Can't do that while moving";
        case SpellFailReason::CantDoWhileMounted:     return "Can't do that while mounted";
        case SpellFailReason::NotMounted:             return "You are not mounted";
        case SpellFailReason::NotWhileShapeshifted:   return "Not while shapeshifted";
        case SpellFailReason::CantUseInThisForm:      return "Can't use that in this form";
        case SpellFailReason::NoPath:                 return "No path available";
        case SpellFailReason::NotBehindTarget:        return "You must be behind your target";
        case SpellFailReason::NotInFrontOfTarget:     return "You must be in front of your target";
        case SpellFailReason::Silenced:               return "You are silenced";
        case SpellFailReason::TargetFriendly:         return "Target is friendly";
        case SpellFailReason::TargetEnemy:            return "Target is hostile";
        case SpellFailReason::TargetNotInParty:       return "Target is not in your party";
        case SpellFailReason::RequiresAreaType:       return "You are in the wrong zone";
        case SpellFailReason::SpellInProgress:        return "Another action is in progress";
        default:                                      return "Spell failed";
    }
}

void SpellErrorDisplay::ShowError(SpellFailReason reason) {
    SpellErrorEntry entry;
    entry.reason = reason;
    entry.message = GetErrorMessage(reason);
    entry.timestamp = elapsed_;
    entry.fadeDuration = errorDuration_;
    errors_.push_back(entry);
}

void SpellErrorDisplay::ShowCustomError(const std::string& message) {
    SpellErrorEntry entry;
    entry.reason = SpellFailReason::OutOfRange;
    entry.message = message;
    entry.timestamp = elapsed_;
    entry.fadeDuration = errorDuration_;
    errors_.push_back(entry);
}

std::optional<SpellErrorEntry> SpellErrorDisplay::GetCurrentError() const {
    if (errors_.empty()) return std::nullopt;
    return errors_.back();
}

std::vector<SpellErrorEntry> SpellErrorDisplay::GetRecentErrors(size_t count) const {
    if (errors_.size() <= count) return errors_;
    return {errors_.end() - static_cast<ptrdiff_t>(count), errors_.end()};
}

float SpellErrorDisplay::GetFadeAlpha() const {
    if (errors_.empty()) return 0.0f;
    const auto& latest = errors_.back();
    const double age = elapsed_ - latest.timestamp;
    if (age >= latest.fadeDuration) return 0.0f;

    return static_cast<float>(1.0 - (age / latest.fadeDuration));
}

void SpellErrorDisplay::SetErrorDuration(float seconds) {
    errorDuration_ = seconds;
}

void SpellErrorDisplay::Update(float dt) {
    elapsed_ += static_cast<double>(dt);

    errors_.erase(
        std::remove_if(errors_.begin(), errors_.end(),
                       [this](const SpellErrorEntry& e) {
                           return (elapsed_ - e.timestamp) >= e.fadeDuration;
                       }),
        errors_.end());
}

void SpellErrorDisplay::Clear() {
    errors_.clear();
    elapsed_ = 0.0;
}

}

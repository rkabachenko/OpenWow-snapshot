
#include "openwow/game/char_services.h"

#include <algorithm>

namespace openwow::game {

bool CharacterServices::RequestService(CharServiceType type,
                                       ObjectGuid charGuid,
                                       const std::string& charName) {

    if (current_request_.has_value() &&
        current_request_->state != CharServiceState::None &&
        current_request_->state != CharServiceState::Completed &&
        current_request_->state != CharServiceState::Failed) {
        return false;
    }

    CharServiceRequest req;
    req.serviceType   = type;
    req.characterGuid = charGuid;
    req.characterName = charName;
    req.state         = CharServiceState::Pending;
    current_request_  = std::move(req);
    return true;
}

void CharacterServices::SetNewName(const std::string& name) {
    if (current_request_) {
        current_request_->newName = name;
    }
}

void CharacterServices::SetNewFaction(std::uint32_t faction) {
    if (current_request_) {
        current_request_->newFaction = faction;
    }
}

void CharacterServices::SetNewRace(std::uint32_t race) {
    if (current_request_) {
        current_request_->newRace = race;
    }
}

void CharacterServices::SetTargetRealm(std::uint32_t realm) {
    if (current_request_) {
        current_request_->targetRealm = realm;
    }
}

std::optional<CharServiceRequest> CharacterServices::GetCurrentRequest() const {
    return current_request_;
}

bool CharacterServices::HasPendingRequest() const {
    return current_request_.has_value() &&
           (current_request_->state == CharServiceState::Pending ||
            current_request_->state == CharServiceState::InProgress);
}

CharServiceState CharacterServices::GetState() const {
    return current_request_ ? current_request_->state : CharServiceState::None;
}

void CharacterServices::SetState(CharServiceState state,
                                 const std::string& errorMsg) {
    if (current_request_) {
        current_request_->state        = state;
        current_request_->errorMessage = errorMsg;
    }
}

void CharacterServices::CancelRequest() {
    if (current_request_ &&
        (current_request_->state == CharServiceState::Pending ||
         current_request_->state == CharServiceState::InProgress)) {
        current_request_->state = CharServiceState::None;
        current_request_.reset();
    }
}

bool CharacterServices::IsNameChangeAvailable(ObjectGuid guid) const {
    auto it = name_change_available_.find(guid.GetRawValue());
    return it != name_change_available_.end() && it->second;
}

void CharacterServices::SetNameChangeAvailable(ObjectGuid guid,
                                               bool available) {
    name_change_available_[guid.GetRawValue()] = available;
}

bool CharacterServices::IsFactionChangeAvailable() const {
    return faction_change_available_;
}

void CharacterServices::SetFactionChangeAvailable(bool available) {
    faction_change_available_ = available;
}

bool CharacterServices::IsRealmTransferAvailable() const {
    return realm_transfer_available_;
}

void CharacterServices::SetRealmTransferAvailable(bool available) {
    realm_transfer_available_ = available;
}

std::string CharacterServices::GetServiceName(CharServiceType type) {
    switch (type) {
        case CharServiceType::NameChange:    return "Name Change";
        case CharServiceType::FactionChange: return "Faction Change";
        case CharServiceType::RaceChange:    return "Race Change";
        case CharServiceType::RealmTransfer: return "Realm Transfer";
        case CharServiceType::Customize:     return "Customize";
        case CharServiceType::Recustomize:   return "Recustomize";
    }
    return "Unknown";
}

void CharacterServices::Reset() {
    current_request_.reset();
    name_change_available_.clear();
    faction_change_available_ = false;
    realm_transfer_available_ = false;
}

}

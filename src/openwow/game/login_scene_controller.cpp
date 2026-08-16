
#include "openwow/game/login_scene_controller.h"

namespace openwow::game {

void LoginSceneController::SetState(LoginScreenState state) { state_ = state; }

LoginScreenState LoginSceneController::GetState() const { return state_; }

std::string LoginSceneController::GetStateName(LoginScreenState state) {
    switch (state) {
        case LoginScreenState::Idle:              return "Idle";
        case LoginScreenState::EnteringCredentials:return "EnteringCredentials";
        case LoginScreenState::Connecting:         return "Connecting";
        case LoginScreenState::Authenticating:     return "Authenticating";
        case LoginScreenState::Connected:          return "Connected";
        case LoginScreenState::FetchingRealmList:  return "FetchingRealmList";
        case LoginScreenState::RealmSelected:      return "RealmSelected";
        case LoginScreenState::Error:              return "Error";
        case LoginScreenState::Disconnected:       return "Disconnected";
    }
    return "Unknown";
}

void LoginSceneController::SetFormData(const LoginFormData& data) { formData_ = data; }
const LoginFormData& LoginSceneController::GetFormData() const    { return formData_; }

void LoginSceneController::SetRealmList(std::vector<std::string> realmNames) {
    realmList_ = std::move(realmNames);
}
const std::vector<std::string>& LoginSceneController::GetRealmList() const { return realmList_; }

void LoginSceneController::SelectRealm(uint32_t index) {
    if (index < static_cast<uint32_t>(realmList_.size()))
        selectedRealm_ = index;
}
std::optional<uint32_t> LoginSceneController::GetSelectedRealm() const { return selectedRealm_; }

void LoginSceneController::SetErrorMessage(std::string msg) { errorMessage_ = std::move(msg); }
const std::string& LoginSceneController::GetErrorMessage() const { return errorMessage_; }

bool LoginSceneController::ShowEULA() const { return !eulaAccepted_; }

void LoginSceneController::AcceptEULA() { eulaAccepted_ = true; }

bool LoginSceneController::HasAcceptedEULA() const { return eulaAccepted_; }

bool LoginSceneController::IsConnecting() const {
    return state_ == LoginScreenState::Connecting ||
           state_ == LoginScreenState::Authenticating;
}

bool LoginSceneController::CanLogin() const {
    return !formData_.username.empty() &&
           !formData_.password.empty() &&
           eulaAccepted_;
}

void LoginSceneController::SetConnectionProgress(float progress) {
    connectionProgress_ = progress;
}

float LoginSceneController::GetConnectionProgress() const { return connectionProgress_; }

std::string LoginSceneController::GetStatusText() const {
    switch (state_) {
        case LoginScreenState::Connecting:        return "Connecting...";
        case LoginScreenState::Authenticating:     return "Authenticating...";
        case LoginScreenState::FetchingRealmList:  return "Retrieving realm list...";
        case LoginScreenState::Connected:          return "Connected";
        case LoginScreenState::Error:              return errorMessage_;
        case LoginScreenState::Disconnected:       return "Disconnected";
        default: break;
    }
    return "";
}

void LoginSceneController::Update([[maybe_unused]] float dt) {

}

void LoginSceneController::Reset() {
    state_ = LoginScreenState::Idle;
    formData_ = {};
    realmList_.clear();
    selectedRealm_.reset();
    errorMessage_.clear();
    eulaAccepted_ = false;
    connectionProgress_ = 0.0f;
}

}

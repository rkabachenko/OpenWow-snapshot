#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class LoginScreenState : uint8_t {
    Idle,
    EnteringCredentials,
    Connecting,
    Authenticating,
    Connected,
    FetchingRealmList,
    RealmSelected,
    Error,
    Disconnected
};

struct LoginFormData {
    std::string username;
    std::string password;
    bool rememberAccount{false};
    bool acceptEula{false};
};

class LoginSceneController {
public:
    void SetState(LoginScreenState state);
    [[nodiscard]] LoginScreenState GetState() const;
    [[nodiscard]] static std::string GetStateName(LoginScreenState state);

    void SetFormData(const LoginFormData& data);
    [[nodiscard]] const LoginFormData& GetFormData() const;

    void SetRealmList(std::vector<std::string> realmNames);
    [[nodiscard]] const std::vector<std::string>& GetRealmList() const;

    void SelectRealm(uint32_t index);
    [[nodiscard]] std::optional<uint32_t> GetSelectedRealm() const;

    void SetErrorMessage(std::string msg);
    [[nodiscard]] const std::string& GetErrorMessage() const;

    bool ShowEULA() const;
    void AcceptEULA();
    [[nodiscard]] bool HasAcceptedEULA() const;

    [[nodiscard]] bool IsConnecting() const;
    [[nodiscard]] bool CanLogin() const;

    void SetConnectionProgress(float progress);
    [[nodiscard]] float GetConnectionProgress() const;

    [[nodiscard]] std::string GetStatusText() const;

    void Update(float dt);
    void Reset();

private:
    LoginScreenState state_{LoginScreenState::Idle};
    LoginFormData    formData_;
    std::vector<std::string> realmList_;
    std::optional<uint32_t>  selectedRealm_;
    std::string errorMessage_;
    bool eulaAccepted_{false};
    float connectionProgress_{0.0f};
};

}

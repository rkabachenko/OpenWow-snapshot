#include "openwow/core/game_state_machine.h"

namespace openwow::core {

GameStateMachine::GameStateMachine() { Reset(); }

void GameStateMachine::SetState(GameStateType state) {
    previousState_ = currentState_;
    currentState_ = state;
    timeInState_ = 0.0f;
}

GameStateType GameStateMachine::GetState() const { return currentState_; }

GameStateType GameStateMachine::GetPreviousState() const { return previousState_; }

std::string GameStateMachine::GetStateName(GameStateType state) {
    switch (state) {
        case GameStateType::Splash:           return "Splash";
        case GameStateType::Login:            return "Login";
        case GameStateType::CharacterSelect:  return "CharacterSelect";
        case GameStateType::CharacterCreate:  return "CharacterCreate";
        case GameStateType::Loading:          return "Loading";
        case GameStateType::InGame:           return "InGame";
        case GameStateType::Cinematic:        return "Cinematic";
        case GameStateType::Disconnect:       return "Disconnect";
    }
    return "Unknown";
}

bool GameStateMachine::CanTransition(GameStateType from, GameStateType to) const {

    if (from == to) return false;

    switch (from) {
        case GameStateType::Splash:
            return to == GameStateType::Login;

        case GameStateType::Login:
            return to == GameStateType::CharacterSelect ||
                   to == GameStateType::Disconnect;

        case GameStateType::CharacterSelect:
            return to == GameStateType::CharacterCreate ||
                   to == GameStateType::Loading ||
                   to == GameStateType::Disconnect ||
                   to == GameStateType::Login;

        case GameStateType::CharacterCreate:
            return to == GameStateType::CharacterSelect ||
                   to == GameStateType::Disconnect;

        case GameStateType::Loading:
            return to == GameStateType::InGame ||
                   to == GameStateType::Disconnect;

        case GameStateType::InGame:
            return to == GameStateType::Loading ||
                   to == GameStateType::Cinematic ||
                   to == GameStateType::Disconnect ||
                   to == GameStateType::CharacterSelect;

        case GameStateType::Cinematic:
            return to == GameStateType::InGame ||
                   to == GameStateType::Disconnect;

        case GameStateType::Disconnect:
            return to == GameStateType::Login ||
                   to == GameStateType::Splash;
    }
    return false;
}

bool GameStateMachine::Transition(GameStateType to, const std::string& reason) {
    if (!CanTransition(currentState_, to)) return false;

    GameTransition t{currentState_, to, reason};
    previousState_ = currentState_;
    currentState_ = to;
    timeInState_ = 0.0f;
    history_.push_back(t);

    for (auto& cb : callbacks_) {
        cb(t);
    }
    return true;
}

const std::vector<GameTransition>& GameStateMachine::GetTransitionHistory() const {
    return history_;
}

float GameStateMachine::GetTimeInState() const { return timeInState_; }

void GameStateMachine::Update(float dt) { timeInState_ += dt; }

bool GameStateMachine::IsInGame() const {
    return currentState_ == GameStateType::InGame ||
           currentState_ == GameStateType::Cinematic;
}

bool GameStateMachine::IsInMenu() const {
    return currentState_ == GameStateType::Login ||
           currentState_ == GameStateType::CharacterSelect ||
           currentState_ == GameStateType::CharacterCreate ||
           currentState_ == GameStateType::Splash;
}

void GameStateMachine::RegisterTransitionCallback(TransitionCallback cb) {
    callbacks_.push_back(std::move(cb));
}

void GameStateMachine::Reset() {
    currentState_ = GameStateType::Splash;
    previousState_ = GameStateType::Splash;
    timeInState_ = 0.0f;
    history_.clear();
    callbacks_.clear();
}

}

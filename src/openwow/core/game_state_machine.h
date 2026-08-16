#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace openwow::core {

enum class GameStateType : std::uint8_t {
    Splash,
    Login,
    CharacterSelect,
    CharacterCreate,
    Loading,
    InGame,
    Cinematic,
    Disconnect,
};

struct GameTransition {
    GameStateType from;
    GameStateType to;
    std::string reason;
};

using TransitionCallback = std::function<void(const GameTransition&)>;

class GameStateMachine {
public:
    GameStateMachine();

    void SetState(GameStateType state);
    [[nodiscard]] GameStateType GetState() const;

    [[nodiscard]] GameStateType GetPreviousState() const;

    [[nodiscard]] static std::string GetStateName(GameStateType state);

    [[nodiscard]] bool CanTransition(GameStateType from, GameStateType to) const;

    bool Transition(GameStateType to, const std::string& reason = "");

    [[nodiscard]] const std::vector<GameTransition>& GetTransitionHistory() const;

    [[nodiscard]] float GetTimeInState() const;

    void Update(float dt);

    [[nodiscard]] bool IsInGame() const;
    [[nodiscard]] bool IsInMenu() const;

    void RegisterTransitionCallback(TransitionCallback cb);

    void Reset();

private:
    GameStateType currentState_{GameStateType::Splash};
    GameStateType previousState_{GameStateType::Splash};
    float timeInState_{0.0f};
    std::vector<GameTransition> history_;
    std::vector<TransitionCallback> callbacks_;
};

}

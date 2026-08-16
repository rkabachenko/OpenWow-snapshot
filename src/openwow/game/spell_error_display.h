#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openwow::game {

enum class SpellFailReason : uint8_t {
    OutOfRange               = 1,
    OutOfMana                = 2,
    NotReady                 = 3,
    ItemNotReady             = 4,
    NotInControl             = 5,
    Interrupted              = 6,
    CantDoThatYetMoving      = 7,
    NotEnoughComboPoints     = 8,
    TargetNotInLineOfSight   = 9,
    CantUseInCombat          = 10,
    NoTarget                 = 11,
    AlreadyAtFullHealth      = 12,
    AlreadyAtFullPower       = 13,
    NothingToDispel          = 14,
    TargetTooClose           = 15,
    TargetIsDead             = 16,
    NoAmmo                   = 17,
    InvalidTarget            = 18,
    PlayerIsDead             = 19,
    TooManyOfItem            = 20,
    CantDoWhileMoving        = 21,
    CantDoWhileMounted       = 22,
    NotMounted               = 23,
    NotWhileShapeshifted     = 24,
    CantUseInThisForm        = 25,
    NoPath                   = 26,
    NotBehindTarget          = 27,
    NotInFrontOfTarget       = 28,
    Silenced                 = 29,
    TargetFriendly           = 30,
    TargetEnemy              = 31,
    TargetNotInParty         = 32,
    RequiresAreaType         = 33,
    SpellInProgress          = 34,
};

struct SpellErrorEntry {
    SpellFailReason reason = SpellFailReason::OutOfRange;
    std::string message;
    double timestamp = 0.0;
    float fadeDuration = 3.0f;
};

class SpellErrorDisplay {
 public:
    void ShowError(SpellFailReason reason);
    void ShowCustomError(const std::string& message);

    [[nodiscard]] std::optional<SpellErrorEntry> GetCurrentError() const;
    [[nodiscard]] std::vector<SpellErrorEntry> GetRecentErrors(size_t count = 10) const;

    [[nodiscard]] static std::string GetErrorMessage(SpellFailReason reason);

    [[nodiscard]] float GetFadeAlpha() const;

    void SetErrorDuration(float seconds);

    void Update(float dt);
    void Clear();

 private:
    std::vector<SpellErrorEntry> errors_;
    float errorDuration_ = 3.0f;
    double elapsed_ = 0.0;
};

}

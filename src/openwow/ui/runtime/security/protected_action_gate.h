#pragma once

#include <cstdint>
#include <string>

struct lua_State;

namespace openwow::ui::game {

enum class ProtectedActionFailureMode : std::uint8_t {
  kForbidden = 0,
  kBlocked = 1,
  kBlockedType4 = 2,
};

namespace protected_action_kind {

inline constexpr int kMovement = 0;

inline constexpr int kUnitSelection = 1;

inline constexpr int kSpellCast = 2;

inline constexpr int kMacroExecution = 3;

inline constexpr int kLootThreshold = 4;

inline constexpr int kTrade = 7;

inline constexpr int kAuction = 8;

inline constexpr int kLookingForGroup = 9;

inline constexpr int kChatMessage = 10;

inline constexpr int kRaidSubgroup = 11;

inline constexpr int kMacroCatalog = 12;

inline constexpr int kKeyBinding = 13;

inline constexpr int kActionSlotMutation = 14;

inline constexpr int kGmTicket = 17;

inline constexpr int kCalendar = 19;

inline constexpr int kBattlefieldQueue = 20;

inline constexpr int kItemEquip = 22;

}

[[nodiscard]] int GameUI_CanPerformProtectedAction(int action_kind);

[[nodiscard]] int GameUI_CanPerformTaintForbiddenAction();

[[nodiscard]] int GameUI_CanPerformHardwareEventAction();
void GameUI_ReportProtectedActionFailure(
    lua_State* state, ProtectedActionFailureMode mode);
void GameUI_ReportProtectedActionFailure(ProtectedActionFailureMode mode);
bool GameUI_TaintLogCVarValidationCallback(
    const std::string& name, const std::string& old_value,
    const std::string& new_value);
bool GameUI_IsTaintLogEventSinkActive();
void GameUI_ResetTaintLogRuntimeForTesting();

}

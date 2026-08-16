#pragma once

#include <cstdint>
#include <string>

namespace openwow::ui::game {

enum class TargetActionType : int {
  kReject1 = 1,
  kReject2 = 2,
  kCompareGuid = 3,
  kCheckGuidType = 4,
  kCheckSelfOrParty = 5,
  kCheckSelfPartyRaid = 6,
};

struct TargetState {
  std::uint32_t flags[4]{0, 0, 0, 0};
  int sentinel[3]{-1, -1, -1};
  int primary_target{0};
  int secondary_target{0};
};

struct ErrorStringState {
  std::string current_error;
};

struct WorldStateValue {
  int current_value{1};
  std::uint64_t revision{0};
};

struct NameplateState {
  bool always_show_nameplates{true};
};

void GameUI_SetBackgroundZoneState(int zone_id, int param);

void GameUI_InitTargetState(TargetState &ts, int initial_target);

void GameUI_LoadSavedVariables(const std::string &account_name, const std::string &realm_name,
                               const std::string &char_name);

void GameUI_OnUnitDespawnCleanup(std::uint64_t guid);

struct CorpseMapFallbackTransform {
  float position[3];
  float facing;
  float view_matrix[16];
};
void GameUI_SetCorpseMapFallbackTransform(CorpseMapFallbackTransform &transform,
                                          const float *position, float facing);

void GameUI_CloseNpcInteraction();

bool GameUI_ValidateTargetForAction(std::uint64_t target_guid, TargetActionType action,
                                    std::uint64_t current_target_guid);

void GameUI_SetCurrentErrorString(ErrorStringState &es, const char *error);

void GameUI_OnMouseoverUnitEnter(std::uint64_t guid);

void GameUI_OnMouseoverUnitLeave(std::uint64_t guid);

void GameUI_SetWorldStateValue(WorldStateValue &ws, int new_value);

void GameUI_UpdateNameplateVisibility(const NameplateState &ns);

[[nodiscard]] bool GameUI_ShouldShowWorldNameplates();

[[nodiscard]] bool GameUI_ShouldShowHighlightedNameplates();

[[nodiscard]] bool GameUI_IsUIVisible();

void GameUI_ResetHighlightedNameplateVisibility();

void GameUI_InitAsyncCharacterRequest(std::uint64_t guid);

int GameUI_OnUnitHighlightUpdate(std::uint64_t guid, bool always_show);

void GameUI_GetUnitModelDisplay(std::uint64_t guid);

void GameUI_RegisterKeyboardEvents();

void GameUI_PollScreenshotCompletions();

[[nodiscard]] bool GameUI_IsActivePlayerPartyOrRaidUnitGuid(std::uint64_t guid);

}

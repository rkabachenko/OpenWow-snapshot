
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/core/cvar.h"
#include "openwow/core/screenshot_system.h"
#include "openwow/game/account_data.h"
#include "openwow/game/commerce/auctions/auction_interaction.h"
#include "openwow/game/group_system.h"
#include "openwow/game/commerce/mail/mail_interaction.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/object_types.h"
#include "openwow/game/world_session.h"
#include "openwow/world/camera/world_camera.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/script_event_dispatch.h"
#include "openwow/ui/game/tooltip_system.h"
#include "openwow/vfs/sfile_core.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace openwow::ui::game {

namespace {

bool g_show_world_nameplates = true;

openwow::game::WorldSession* GetActiveUiWorldSession() {
  auto* const manager = runtime::WorldUiRuntimeContext::FromActiveLua();
  return manager != nullptr ? manager->world_session() : nullptr;
}

void RefreshMouseoverActionValidation() {
  auto *session = GetActiveUiWorldSession();
  if (session == nullptr) {
    return;
  }

  if (detail::RefreshAllActionSlotValidation(*session)) {
    ScriptEventDispatch::Get().FireActionbarUpdateUsable();
  }
}

}

bool GameUI_IsActivePlayerOrPartyUnitGuid(std::uint64_t guid) {
  auto* session = GetActiveUiWorldSession();
  return session != nullptr &&
         openwow::game::GroupSystem::Get().IsActivePlayerOrPartyUnitGuid(
             session->objects(), guid);
}

bool GameUI_IsActivePlayerPartyOrRaidUnitGuid(std::uint64_t guid) {
  auto* session = GetActiveUiWorldSession();
  return session != nullptr &&
         openwow::game::GroupSystem::Get().IsActivePlayerPartyOrRaidUnitGuid(
             session->objects(), guid);
}

bool GameUI_IsActivePlayerOrPartyMemberGuid(std::uint64_t guid) {
  return openwow::game::GroupSystem::Get().IsActivePlayerOrPartyMemberGuid(guid);
}

bool GameUI_IsActivePlayerPartyOrRaidMemberGuid(std::uint64_t guid) {
  return openwow::game::GroupSystem::Get().IsActivePlayerPartyOrRaidMemberGuid(
      guid);
}

namespace {

bool GameUI_ShouldRetainItemMouseover(std::uint64_t item_guid) {
  if (item_guid == 0) {
    return false;
  }

  auto *session = GetActiveUiWorldSession();
  if (session == nullptr) {
    return false;
  }

  if (session->trade().IsLocalPlayerTradeItemGuid(item_guid)) {
    return true;
  }

  if (session->mail().compose().HasDraftAttachmentItemGuid(item_guid)) {
    return true;
  }

  const auto selection =
      session->auction().state().GetSellItemSelection();
  if (selection.item_guid == item_guid) {
    return true;
  }

  return session->auction().state().IsTrackedMultiSellSource(
      item_guid);
}

}

void GameUI_SetBackgroundZoneState(int zone_id, int param) {
  openwow::vfs::SetDataPreloadBackgroundZoneState(zone_id, param);
}

void GameUI_InitTargetState(TargetState &ts, int initial_target) {
  ts.flags[0] = 0;
  ts.flags[1] = 0;
  ts.flags[2] = 0;
  ts.flags[3] = 0;
  ts.sentinel[0] = -1;
  ts.sentinel[1] = -1;
  ts.sentinel[2] = -1;
  ts.primary_target = initial_target;
  ts.secondary_target = initial_target;
}

void GameUI_LoadSavedVariables(const std::string &account_name, const std::string &realm_name,
                               const std::string &char_name) {

  (void)account_name;

  (void)realm_name;
  (void)char_name;

}

void GameUI_OnUnitDespawnCleanup(std::uint64_t guid) {
  auto* const session = GetActiveUiWorldSession();
  auto* const unit = session != nullptr
                         ? openwow::game::CGObject_HasFlags(
                               session->objects(), guid,
                               openwow::game::kTypeMaskUnit)
                         : nullptr;

  if (unit != nullptr) {

    auto &tooltip = TooltipSystem::Get();
    if (tooltip.IsShown()) {
      tooltip.Reset();
      tooltip.HideLiveGameTooltipFrame();
    }

  }

  ScriptEventDispatch::Get().FirePerUnitEvent(events::UNIT_FLAGS, guid);
}

void GameUI_SetCorpseMapFallbackTransform(CorpseMapFallbackTransform &transform,
                                          const float *position, float facing) {
  const float cosine = std::cos(facing);
  const float sine = std::sin(facing);
  const float view_matrix[16] = {
      cosine, sine, 0.0f, 0.0f,
      -sine, cosine, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f,
  };
  std::copy(std::begin(view_matrix), std::end(view_matrix),
            transform.view_matrix);

  transform.position[0] = position[0];
  transform.position[1] = position[1];
  transform.position[2] = position[2];
  transform.facing = facing;
}

void GameUI_CloseNpcInteraction() {

}

bool GameUI_ValidateTargetForAction(std::uint64_t target_guid, TargetActionType action,
                                    std::uint64_t current_target_guid) {
  switch (action) {
  case TargetActionType::kReject1:
  case TargetActionType::kReject2:
    return false;

  case TargetActionType::kCheckGuidType: {

    const std::uint32_t high = static_cast<std::uint32_t>(target_guid >> 32);
    if ((high & 0xF0000000u) != 0)
      return false;

    if ((high & 0x0F07FFFFu) == 0 && static_cast<std::uint32_t>(target_guid) == 0) {
      return false;
    }
    [[fallthrough]];
  }
  case TargetActionType::kCompareGuid:

    if (auto* session = GetActiveUiWorldSession();
        session != nullptr && target_guid != current_target_guid &&
        openwow::game::GroupSystem::Get().IsActivePlayerPartyOrRaidUnitGuid(
            session->objects(), target_guid)) {
      return true;
    }
    return false;

  case TargetActionType::kCheckSelfOrParty:

    if (target_guid == current_target_guid)
      return false;
    return GameUI_IsActivePlayerOrPartyMemberGuid(target_guid);

  case TargetActionType::kCheckSelfPartyRaid:

    if (target_guid == current_target_guid)
      return false;
    return GameUI_IsActivePlayerPartyOrRaidMemberGuid(target_guid);

  default:
    return true;
  }
}

void GameUI_SetCurrentErrorString(ErrorStringState &es, const char *error) {
  if (error != nullptr && error[0] != '\0') {
    es.current_error = error;
  } else {
    es.current_error.clear();
  }
}

void GameUI_OnMouseoverUnitEnter(std::uint64_t guid) {
  (void)guid;
  RefreshMouseoverActionValidation();
}

void GameUI_OnMouseoverUnitLeave(std::uint64_t guid) {
  if (GameUI_ShouldRetainItemMouseover(guid)) {
    return;
  }

  (void)guid;
  RefreshMouseoverActionValidation();
}

void GameUI_SetWorldStateValue(WorldStateValue &ws, int new_value) {
  if (new_value == ws.current_value) {
    return;
  }

  ws.current_value = new_value;
  ++ws.revision;
}

void GameUI_UpdateNameplateVisibility(const NameplateState &ns) {
  g_show_world_nameplates = ns.always_show_nameplates;
}

bool GameUI_ShouldShowWorldNameplates() {
  return g_show_world_nameplates;
}

bool GameUI_ShouldShowHighlightedNameplates() {
  return g_show_world_nameplates ||
         CVarSystem::Instance().GetCVarBool("unitHighlights");
}

bool GameUI_IsUIVisible() { return g_show_world_nameplates; }

void GameUI_ResetHighlightedNameplateVisibility() {
  g_show_world_nameplates = true;
}

void GameUI_InitAsyncCharacterRequest(std::uint64_t guid) {
  if (auto *session = GetActiveUiWorldSession()) {
    session->BeginLevelGrantProposal(guid);
  }
}

int GameUI_OnUnitHighlightUpdate(std::uint64_t guid, bool always_show) {
  (void)guid;
  g_show_world_nameplates = always_show;
  return 1;
}

void GameUI_GetUnitModelDisplay(std::uint64_t guid) {
  auto* const session = GetActiveUiWorldSession();
  if (session == nullptr) {
    return;
  }

  auto& objects = session->objects();
  const openwow::game::ObjectGuid resolved_guid =
      guid != 0 ? openwow::game::ObjectGuid(guid) : objects.GetActivePlayerGuid();
  if (resolved_guid.IsEmpty()) {
    return;
  }

  auto* unit = objects.GetMutableUnit(resolved_guid);
  if (unit == nullptr) {
    return;
  }

  if (session->world_camera() != nullptr) {
    auto& camera = *session->world_camera();
    camera.SetBoundObject(unit->GetGuid().GetRawValue());

    const auto target = unit->GetPosition();
    camera.SetTarget(target.x, target.y, target.z);
  }
}

void GameUI_RegisterKeyboardEvents() {
  auto& account_data = openwow::game::AccountData::Get();

  const std::string& global_config =
      account_data.GetData(openwow::game::AccountDataType::GlobalConfig);
  if (!global_config.empty()) {
    openwow::core::ida::CVar_ParseConfigBuffer(global_config);
  }

  const std::string& per_char_config =
      account_data.GetData(openwow::game::AccountDataType::PerCharacterConfig);
  if (!per_char_config.empty()) {
    openwow::core::ida::CVar_ParseConfigBuffer(per_char_config);
  }

  if (auto* manager = runtime::WorldUiRuntimeContext::FromActiveLua(); manager != nullptr) {
    manager->frame_events().OnVariablesLoaded();
  }
}

void GameUI_PollScreenshotCompletions() {
  auto &dispatch = ScriptEventDispatch::Get();
  if (!dispatch.IsInitialized()) {
    return;
  }

  auto completed = openwow::core::ScreenshotSystem::Instance()
                       .DrainCompletedRequestsForDomain(
                           openwow::core::ScreenshotRequestDomain::GameUi);
  for (const auto &result : completed) {
    if (result.succeeded) {
      dispatch.FireEvent(events::SCREENSHOT_SUCCEEDED);
    } else {
      dispatch.FireEvent(events::SCREENSHOT_FAILED);
    }
  }
}

}

#include "scenario_world_ui_driver.h"

#include "openwow/net/client_services.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/actions/bindings/application/binding_profiles.h"
#include "openwow/game/world_session.h"
#include "openwow/render/scene/world_frame.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_ui_manager.h"
#include "openwow/ui/game/game_ui_core.h"
#include "openwow/ui/game/api/game_lua_api_action.h"
#include "openwow/ui/game/secure_execution.h"
#include "openwow/ui/lua_call_helpers.h"
#include "openwow/foundation/diagnostics/logging.h"

extern "C" {
#include <lua.hpp>
}

#include <array>
#include <cmath>
#include <string_view>

namespace openwow::client {
namespace {

constexpr std::string_view kChatProbeText =
    "|cff66ccffOpenWoW local E2E UI oracle|r";

struct PinnedCreature {
  std::uint32_t entry;
  float x;
  float y;
  float z;
};

constexpr PinnedCreature kPinnedActionTarget{299u, -8934.95F, -132.493F,
                                              83.5312F};

bool MatchesPinnedCreature(const openwow::game::WorldObject& object,
                           const PinnedCreature& pinned) {
  constexpr float kPositionToleranceSq = 1.0F;
  const float dx = object.GetX() - pinned.x;
  const float dy = object.GetY() - pinned.y;
  const float dz = object.GetZ() - pinned.z;
  return object.GetEntry() == pinned.entry &&
         dx * dx + dy * dy + dz * dz <= kPositionToleranceSq;
}

openwow::game::ObjectGuid FindPinnedActionTarget(
    const openwow::game::WorldSession& session) {
  openwow::game::ObjectGuid guid;
  session.objects().ForEach([&](const openwow::game::WorldObject& object) {
    if (!guid.IsEmpty() || !object.IsUnit() ||
        !MatchesPinnedCreature(object, kPinnedActionTarget)) {
      return;
    }
    const auto* const target = session.objects().GetUnit(object.GetGuid());
    if (target != nullptr && target->GetHealth() != 0u) {
      guid = object.GetGuid();
    }
  });
  return guid;
}

std::string DescribeActionTargetAvailability(
    const openwow::game::WorldSession* const session) {
  if (session == nullptr) {
    return "session=absent";
  }
  const auto* const player = session->objects().GetActivePlayer();
  std::size_t loaded_units = 0;
  std::size_t nearby_units = 0;
  std::size_t entry_matches = 0;
  std::string action_entry_poses;
  session->objects().ForEach([&](const openwow::game::WorldObject& object) {
    if (!object.IsUnit() || object.IsPlayer()) {
      return;
    }
    ++loaded_units;
    if (object.GetEntry() == kPinnedActionTarget.entry) {
      ++entry_matches;
      const auto& movement = object.GetMovementUpdate();
      action_entry_poses +=
          " {guid=" + object.GetGuid().ToString() +
          " x=" + std::to_string(object.GetX()) +
          " y=" + std::to_string(object.GetY()) +
          " z=" + std::to_string(object.GetZ()) +
          " flags=" + std::to_string(movement.update_flags) +
          " transport=" +
          std::to_string(movement.movement.transport.guid.GetRawValue()) +
          "}";
    }
    if (player != nullptr) {
      const float dx = object.GetX() - player->GetX();
      const float dy = object.GetY() - player->GetY();
      const float dz = object.GetZ() - player->GetZ();
      nearby_units += dx * dx + dy * dy + dz * dz <= 40.0F * 40.0F ? 1u : 0u;
    }
  });
  const std::string player_pose =
      player != nullptr
          ? "player={x=" + std::to_string(player->GetX()) +
                " y=" + std::to_string(player->GetY()) +
                " z=" + std::to_string(player->GetZ()) + "} "
          : "player=absent ";
  return player_pose + "loadedUnits=" + std::to_string(loaded_units) +
         " nearbyUnits=" + std::to_string(nearby_units) +
         " actionEntryMatches=" + std::to_string(entry_matches) +
         action_entry_poses;
}

void PublishMouseoverGuid(openwow::render::WorldFrame& world_frame,
                          openwow::game::WorldSession& session,
                          const openwow::game::ObjectGuid guid) {

  world_frame.SetExplicitMouseoverGuid(guid);
  session.objects().SetMouseover(guid);
}

class ScopedLuaStack final {
 public:
  explicit ScopedLuaStack(lua_State* state)
      : state_(state), top_(state != nullptr ? lua_gettop(state) : 0) {}
  ~ScopedLuaStack() {
    if (state_ != nullptr) {
      lua_settop(state_, top_);
    }
  }

  ScopedLuaStack(const ScopedLuaStack&) = delete;
  ScopedLuaStack& operator=(const ScopedLuaStack&) = delete;

 private:
  lua_State* state_{nullptr};
  int top_{0};
};

bool PushGlobalTable(lua_State* state, const char* name) {
  lua_getglobal(state, name);
  if (lua_istable(state, -1) == 0) {
    lua_pop(state, 1);
    return false;
  }
  return true;
}

bool ReadFrameShown(lua_State* state, const char* name, bool* shown) {
  if (state == nullptr || name == nullptr || shown == nullptr) {
    return false;
  }
  ScopedLuaStack stack(state);
  if (!PushGlobalTable(state, name)) {
    return false;
  }
  lua_getfield(state, -1, "IsShown");
  if (lua_isfunction(state, -1) == 0) {
    return false;
  }
  lua_pushvalue(state, -2);
  if (lua_pcall(state, 1, 1, 0) != 0) {
    return false;
  }
  *shown = lua_toboolean(state, -1) != 0;
  return true;
}

bool ReadFrameNumber(lua_State* state, const char* frame_name,
                     const char* method, double* value) {
  if (state == nullptr || frame_name == nullptr || method == nullptr ||
      value == nullptr) {
    return false;
  }
  ScopedLuaStack stack(state);
  if (!PushGlobalTable(state, frame_name)) {
    return false;
  }
  lua_getfield(state, -1, method);
  if (lua_isfunction(state, -1) == 0) {
    return false;
  }
  lua_pushvalue(state, -2);
  if (lua_pcall(state, 1, 1, 0) != 0 || lua_isnumber(state, -1) == 0) {
    return false;
  }
  *value = lua_tonumber(state, -1);
  return std::isfinite(*value);
}

bool CallFrameNumberMethod(lua_State* state, const char* frame_name,
                           const char* method, const double value) {
  if (state == nullptr || frame_name == nullptr || method == nullptr) {
    return false;
  }
  ScopedLuaStack stack(state);
  if (!PushGlobalTable(state, frame_name)) {
    return false;
  }
  lua_getfield(state, -1, method);
  if (lua_isfunction(state, -1) == 0) {
    return false;
  }
  lua_pushvalue(state, -2);
  lua_pushnumber(state, static_cast<lua_Number>(value));
  return lua_pcall(state, 2, 0, 0) == 0;
}

std::optional<bool> CallBooleanGlobal(lua_State* state, const char* name,
                                      const char* string_argument = nullptr,
                                      const double* number_argument = nullptr) {
  if (state == nullptr || name == nullptr) {
    return std::nullopt;
  }
  ScopedLuaStack stack(state);
  lua_getglobal(state, name);
  if (lua_isfunction(state, -1) == 0) {
    return std::nullopt;
  }
  int argument_count = 0;
  if (string_argument != nullptr) {
    lua_pushstring(state, string_argument);
    ++argument_count;
  } else if (number_argument != nullptr) {
    lua_pushnumber(state, static_cast<lua_Number>(*number_argument));
    ++argument_count;
  }
  if (lua_pcall(state, argument_count, 1, 0) != 0) {
    return std::nullopt;
  }
  return lua_toboolean(state, -1) != 0;
}

bool AddChatProbe(lua_State* state, double* before, double* after) {
  if (state == nullptr || before == nullptr || after == nullptr) {
    return false;
  }
  ScopedLuaStack stack(state);
  lua_getglobal(state, "DEFAULT_CHAT_FRAME");
  if (lua_istable(state, -1) == 0) {
    lua_pop(state, 1);
    if (!PushGlobalTable(state, "ChatFrame1")) {
      return false;
    }
  }
  const int frame_index = lua_gettop(state);

  const auto read_count = [&]() -> std::optional<double> {
    lua_getfield(state, frame_index, "GetNumMessages");
    if (lua_isfunction(state, -1) == 0) {
      lua_pop(state, 1);
      return std::nullopt;
    }
    lua_pushvalue(state, frame_index);
    if (lua_pcall(state, 1, 1, 0) != 0 || lua_isnumber(state, -1) == 0) {
      lua_pop(state, 1);
      return std::nullopt;
    }
    const double count = lua_tonumber(state, -1);
    lua_pop(state, 1);
    return count;
  };

  const auto initial_count = read_count();
  if (!initial_count.has_value()) {
    return false;
  }
  lua_getfield(state, frame_index, "AddMessage");
  if (lua_isfunction(state, -1) == 0) {
    return false;
  }
  lua_pushvalue(state, frame_index);
  lua_pushlstring(state, kChatProbeText.data(), kChatProbeText.size());
  if (lua_pcall(state, 2, 0, 0) != 0) {
    return false;
  }
  const auto final_count = read_count();
  if (!final_count.has_value()) {
    return false;
  }
  *before = *initial_count;
  *after = *final_count;
  return true;
}

ScenarioWorldUiActionResult MakeFailure(std::string error) {
  return {.error = std::move(error)};
}

}

ScenarioWorldUiActionResult ScenarioWorldUiDriver::Exercise(
    openwow::ui::game::GameUIManager& manager,
    const ScenarioWorldUiAction action) {
  lua_State* const state = manager.lua_state();
  if (state == nullptr || !manager.is_loaded()) {
    return MakeFailure("stock world FrameXML is unavailable");
  }

  switch (action) {
    case ScenarioWorldUiAction::kInjectChatProbe: {
      ScenarioWorldUiActionResult result;
      result.handled = AddChatProbe(state, &result.value_before,
                                    &result.value_after);
      result.state_changed = result.handled &&
                             result.value_after > result.value_before;
      if (!result.handled) {
        result.error = "ChatFrame1 did not accept the stock AddMessage interaction";
      } else if (!result.state_changed) {
        result.error = "ChatFrame1 message count did not advance";
      }
      return result;
    }

    case ScenarioWorldUiAction::kClickActionButton: {

      constexpr double kPrimaryActionSlot = 1.0;
      const bool attack_action =
          CallBooleanGlobal(state, "IsAttackAction", nullptr,
                            &kPrimaryActionSlot)
              .value_or(false);
      const bool target_exists =
          CallBooleanGlobal(state, "UnitExists", "target").value_or(false);
      bool target_invoked = false;
      bool fallback_used = false;
      bool fallback_fixture_loaded = false;
      bool fallback_invoked = false;
      if (attack_action && !target_exists) {
        openwow::ui::game::SecureExecution::SecureScope secure_scope(state);
        target_invoked = openwow::ui::CallLuaGlobalIfFunction(
            state, "TargetNearestEnemy");
        if (target_invoked &&
            !CallBooleanGlobal(state, "UnitExists", "target").value_or(false)) {
          auto* const session = manager.world_session();
          openwow::game::ObjectGuid fallback_guid;
          if (session != nullptr) {
            fallback_guid = FindPinnedActionTarget(*session);
          }
          fallback_fixture_loaded = !fallback_guid.IsEmpty();
          if (session != nullptr && !fallback_guid.IsEmpty()) {

            if (!original_mouseover_guid_.has_value()) {
              original_mouseover_guid_ =
                  manager.world_frame().GetMouseoverGuid().GetRawValue();
            }
            PublishMouseoverGuid(
                manager.world_frame(), *session, fallback_guid);
          }
          fallback_invoked = !fallback_guid.IsEmpty() &&
              openwow::ui::CallLuaGlobalIfFunction(
                  state, "TargetUnit", "mouseover", true);
          fallback_used = fallback_invoked &&
                          CallBooleanGlobal(state, "UnitExists", "target")
                              .value_or(false);
          target_invoked = target_invoked && fallback_invoked;
        }
      }
      if (target_invoked) {
        staged_action_target_ =
            CallBooleanGlobal(state, "UnitExists", "target").value_or(false);
        action_target_fallback_used_ =
            action_target_fallback_used_ || fallback_used;
        if (fallback_used && !action_target_fallback_reported_) {
          openwow::diagnostics::Log(
              openwow::diagnostics::LogLevel::kInfo,
              "Live E2E staged the action target through the pinned neutral-unit fallback");
          action_target_fallback_reported_ = true;
        }
      }
      if (attack_action && !target_exists && !staged_action_target_) {
        std::string error;
        if (!fallback_fixture_loaded) {
          error = "pinned live action target is absent or dead in the client object map (" +
                  DescribeActionTargetAvailability(manager.world_session()) + ")";
        } else if (!fallback_invoked) {
          error = "stock protected TargetUnit mouseover invocation failed";
        } else {
          error = "stock protected TargetUnit mouseover did not publish the selected unit";
        }
        return {
            .handled = target_invoked,
            .fallback_used = action_target_fallback_used_,
            .error = std::move(error),
        };
      }
      const std::uint64_t before =
          openwow::ui::game::detail::ActionUseTransitionSequence();
      const bool invoked = openwow::ui::CallLuaGlobalMethodIfFunction(
          state, "ActionButton1", "Click", "LeftButton");
      const std::uint64_t after =
          openwow::ui::game::detail::ActionUseTransitionSequence();
      const bool transitioned =
          detail::HasActionUseTransition(invoked, before, after);
      bool transient_attack_stopped = true;
      if (transitioned && attack_action &&
          CallBooleanGlobal(state, "IsCurrentAction", nullptr,
                            &kPrimaryActionSlot)
              .value_or(false)) {
        transient_attack_stopped =
            openwow::ui::CallLuaGlobalMethodIfFunction(
                state, "ActionButton1", "Click", "LeftButton") &&
            !CallBooleanGlobal(state, "IsCurrentAction", nullptr,
                               &kPrimaryActionSlot)
                 .value_or(true);
      }
      return {
          .handled = invoked && transient_attack_stopped,
          .state_changed = transitioned,
          .fallback_used = action_target_fallback_used_,
          .value_before = static_cast<double>(before),
          .value_after = static_cast<double>(after),
          .error = !invoked
                       ? "stock ActionButton1:Click interaction failed"
                       : (!transitioned
                              ? "ActionButton1 click produced no packet, cast/cooldown, targeting, or local action transition"
                              : (!transient_attack_stopped
                                     ? "stock ActionButton1 did not stop its transient E2E attack"
                                     : std::string{})),
      };
    }

    case ScenarioWorldUiAction::kZoomMinimap: {
      double zoom = 0.0;
      double levels = 0.0;
      if (!ReadFrameNumber(state, "Minimap", "GetZoom", &zoom) ||
          !ReadFrameNumber(state, "Minimap", "GetZoomLevels", &levels) ||
          levels < 2.0) {
        return MakeFailure("Minimap zoom methods are unavailable");
      }
      if (!original_minimap_zoom_.has_value()) {
        original_minimap_zoom_ = zoom;
      }
      const double next = std::fmod(std::floor(zoom) + 1.0, std::floor(levels));
      if (!CallFrameNumberMethod(state, "Minimap", "SetZoom", next)) {
        return MakeFailure("Minimap:SetZoom failed");
      }
      double observed = zoom;
      const bool read_back =
          ReadFrameNumber(state, "Minimap", "GetZoom", &observed);
      return {
          .handled = read_back,
          .state_changed = read_back && observed != zoom,
          .value_before = zoom,
          .value_after = observed,
          .error = !read_back || observed == zoom
                       ? "Minimap zoom did not change through the stock method"
                       : std::string{},
      };
    }

    case ScenarioWorldUiAction::kEnableNameplates: {
      auto& cvars = openwow::ui::game::CVarSystem::Instance();
      constexpr std::array<std::string_view, 8> kProbeCvars = {
          "nameplateShowFriends",
          "nameplateShowEnemies",
          "nameplateShowFriendlyPets",
          "nameplateShowFriendlyGuardians",
          "nameplateShowFriendlyTotems",
          "nameplateShowEnemyPets",
          "nameplateShowEnemyGuardians",
          "nameplateShowEnemyTotems",
      };
      bool changed = false;
      for (const std::string_view name : kProbeCvars) {
        const std::string key(name);
        if (!cvars.Exists(key)) {
          continue;
        }
        original_cvars_.try_emplace(key, cvars.GetCVar(key));
        changed = cvars.SetCVar(key, "1") || changed;
      }
      std::size_t eligible_nameplates = 0u;
      bool selected_target_force_visible = false;
      if (const auto* const session = manager.world_session();
          session != nullptr) {
        const auto* const player = session->objects().GetActivePlayer();
        const auto selected_target = session->objects().GetTargetGuid();
        if (player != nullptr) {
          session->objects().ForEach([&](const auto& object) {
            if (!object.IsUnit() || object.GetGuid() == player->GetGuid()) {
              return;
            }
            const auto* const unit =
                session->objects().GetUnit(object.GetGuid());
            if (unit == nullptr) {
              return;
            }
            const float dx = object.GetX() - player->GetX();
            const float dy = object.GetY() - player->GetY();
            const float dz = object.GetZ() - player->GetZ();
            const float distance_sq = dx * dx + dy * dy + dz * dz;
            const bool eligible =
                unit->Nameplate().ShouldShow(*unit, *player,
                                             session->objects(), distance_sq);
            if (distance_sq <= 40.0F * 40.0F && eligible) {
              ++eligible_nameplates;
            }
            selected_target_force_visible =
                selected_target_force_visible ||
                (!selected_target.IsEmpty() &&
                 object.GetGuid() == selected_target);
          });
        }
      }
      const bool staged =
          selected_target_force_visible || eligible_nameplates > 0u;
      return {
          .handled = staged,
          .state_changed = staged && (changed || eligible_nameplates > 0u),
          .value_before = 0.0,
          .value_after = static_cast<double>(eligible_nameplates),
          .error = !staged
                       ? "no selected or retail-CVar-eligible unit is loaded for the nameplate probe"
                       : std::string{},
      };
    }

    case ScenarioWorldUiAction::kRestoreTransientState:
      return RestoreTransientState(manager);

    case ScenarioWorldUiAction::kOpenWorldMap: {
      bool before = false;
      if (!ReadFrameShown(state, "WorldMapFrame", &before)) {
        return MakeFailure("WorldMapFrame is unavailable");
      }
      const bool invoked =
          before || openwow::ui::CallLuaGlobalIfFunction(
                        state, "RunBinding",
                        openwow::game::BindingAction::kToggleWorldMap);
      bool after = false;
      const bool observed = ReadFrameShown(state, "WorldMapFrame", &after);
      return {
          .handled = invoked && observed,
          .state_changed = invoked && observed && after,
          .value_before = before ? 1.0 : 0.0,
          .value_after = after ? 1.0 : 0.0,
          .error = invoked && observed && after
                       ? std::string{}
                       : "stock ToggleWorldMap did not show WorldMapFrame",
      };
    }

    case ScenarioWorldUiAction::kCloseWorldMap: {
      bool before = false;
      if (!ReadFrameShown(state, "WorldMapFrame", &before)) {
        return MakeFailure("WorldMapFrame is unavailable");
      }
      const bool invoked =
          !before || openwow::ui::CallLuaGlobalIfFunction(
                         state, "RunBinding",
                         openwow::game::BindingAction::kToggleWorldMap);
      bool after = true;
      const bool observed = ReadFrameShown(state, "WorldMapFrame", &after);
      return {
          .handled = invoked && observed,
          .state_changed = invoked && observed && !after,
          .value_before = before ? 1.0 : 0.0,
          .value_after = after ? 1.0 : 0.0,
          .error = invoked && observed && !after
                       ? std::string{}
                       : "stock ToggleWorldMap did not hide WorldMapFrame",
      };
    }

    case ScenarioWorldUiAction::kOpenCharacterPanel: {
      bool before = false;
      if (!ReadFrameShown(state, "CharacterFrame", &before)) {
        return MakeFailure("CharacterFrame is unavailable");
      }
      const bool invoked = before || openwow::ui::CallLuaGlobalIfFunction(
                                         state, "ToggleCharacter",
                                         "PaperDollFrame");
      bool after = false;
      const bool observed = ReadFrameShown(state, "CharacterFrame", &after);
      return {
          .handled = invoked && observed,
          .state_changed = invoked && observed && after,
          .value_before = before ? 1.0 : 0.0,
          .value_after = after ? 1.0 : 0.0,
          .error = invoked && observed && after
                       ? std::string{}
                       : "stock ToggleCharacter did not show CharacterFrame",
      };
    }

    case ScenarioWorldUiAction::kCloseCharacterPanel: {
      bool before = false;
      if (!ReadFrameShown(state, "CharacterFrame", &before)) {
        return MakeFailure("CharacterFrame is unavailable");
      }
      const bool invoked = !before || openwow::ui::CallLuaGlobalIfFunction(
                                          state, "ToggleCharacter",
                                          "PaperDollFrame");
      bool after = true;
      const bool observed = ReadFrameShown(state, "CharacterFrame", &after);
      return {
          .handled = invoked && observed,
          .state_changed = invoked && observed && !after,
          .value_before = before ? 1.0 : 0.0,
          .value_after = after ? 1.0 : 0.0,
          .error = invoked && observed && !after
                       ? std::string{}
                       : "stock ToggleCharacter did not hide CharacterFrame",
      };
    }

    case ScenarioWorldUiAction::kRequestLogout: {
      const bool invoked =
          openwow::ui::CallLuaGlobalIfFunction(state, "Logout");
      return {
          .handled = invoked,
          .state_changed =
              invoked && openwow::net::ClientServices::Instance()
                             .HasPendingLogoutRequest(),
          .value_before = 0.0,
          .value_after = invoked ? 1.0 : 0.0,
          .error = invoked ? std::string{}
                           : "stock Logout function is unavailable",
      };
    }
  }
  return MakeFailure("unknown world UI action");
}

ScenarioWorldUiActionResult ScenarioWorldUiDriver::RestoreTransientState(
    openwow::ui::game::GameUIManager& manager) {
  lua_State* const state = manager.lua_state();
  bool restored = state != nullptr;
  auto& cvars = openwow::ui::game::CVarSystem::Instance();
  for (const auto& [name, value] : original_cvars_) {
    restored = cvars.SetCVar(name, value) && restored;
  }
  original_cvars_.clear();

  if (state != nullptr && original_minimap_zoom_.has_value()) {
    restored = CallFrameNumberMethod(state, "Minimap", "SetZoom",
                                     *original_minimap_zoom_) &&
               restored;
  }
  original_minimap_zoom_.reset();

  if (auto* const session = manager.world_session();
      session != nullptr && original_mouseover_guid_.has_value()) {
    PublishMouseoverGuid(
        manager.world_frame(), *session,
        openwow::game::ObjectGuid(*original_mouseover_guid_));
  }
  original_mouseover_guid_.reset();

  if (state != nullptr && staged_action_target_) {
    restored = openwow::ui::CallLuaGlobalIfFunction(state, "ClearTarget") &&
               restored;
  }
  staged_action_target_ = false;
  action_target_fallback_used_ = false;
  action_target_fallback_reported_ = false;

  bool shown = false;
  if (state != nullptr && ReadFrameShown(state, "WorldMapFrame", &shown) &&
      shown) {
    restored = openwow::ui::CallLuaGlobalIfFunction(
                   state, "RunBinding",
                   openwow::game::BindingAction::kToggleWorldMap) &&
               restored;
  }
  if (state != nullptr && ReadFrameShown(state, "CharacterFrame", &shown) &&
      shown) {
    restored = openwow::ui::CallLuaGlobalIfFunction(
                   state, "ToggleCharacter", "PaperDollFrame") &&
               restored;
  }

  return {
      .handled = state != nullptr,
      .state_changed = restored,
      .value_before = 1.0,
      .value_after = restored ? 0.0 : 1.0,
      .error = restored ? std::string{}
                        : "temporary world UI state could not be restored",
  };
}

void ScenarioWorldUiDriver::Reset() noexcept {
  original_cvars_.clear();
  original_minimap_zoom_.reset();
  original_mouseover_guid_.reset();
  staged_action_target_ = false;
  action_target_fallback_used_ = false;
  action_target_fallback_reported_ = false;
}

}

#include "openwow/ui/game/capture_point_ui_manager.h"

#include "openwow/runtime/time/game_clock.h"
#include "openwow/game/chat_display.h"
#include "openwow/game/localization.h"
#include "openwow/game/object_guid.h"
#include "openwow/game/object_manager.h"
#include "openwow/game/objects/cggameobject.h"
#include "openwow/game/objects/cgplayer.h"
#include "openwow/game/query_cache.h"
#include "openwow/game/world_state_manager.h"
#include "openwow/game/world_session.h"
#include "openwow/ui/game/world_state_ui_sync.h"
#include "openwow/ui/game/game_ui_manager.h"

#include <algorithm>
#include <cassert>
#include <string>
#include <utility>

namespace openwow::ui::game {
namespace {

constexpr std::uint32_t kCapturePointWarningCooldownMs = 300000u;
constexpr std::uint32_t kPlayerFlagPvP = 0x200u;
constexpr std::uint32_t kUnitFlagTaxiFlight = 0x100000u;

std::int32_t SquareCaptureRadius(const std::int32_t capture_radius) {
  const auto radius = static_cast<std::int64_t>(capture_radius);
  return static_cast<std::int32_t>(radius * radius);
}

bool HasReachedTick(const std::uint32_t current_tick, const std::uint32_t target_tick) {
  return static_cast<std::int32_t>(current_tick - target_tick) >= 0;
}

CapturePointUIManagerState::ConstEntryIterator
FindCapturePointEntryByGuid(const CapturePointUIManagerState &manager,
                            const std::uint64_t object_guid) {
  return std::find_if(
      manager.Entries().cbegin(), manager.Entries().cend(),
      [object_guid](const CapturePointUIEntry &entry) { return entry.object_guid == object_guid; });
}

CapturePointUIManagerState g_capture_point_ui_manager;

}

void CapturePointUIManagerState::AddCapturePoint(const openwow::game::CGGameObject_C &object,
                                                 const std::int32_t point_id,
                                                 const std::int32_t capture_radius) {
  entries_.push_back(CapturePointUIEntry{
      .object_guid = object.GetGuid().GetRawValue(),
      .capture_radius_sq = SquareCaptureRadius(capture_radius),
      .point_id = point_id,
  });
}

CapturePointUIManagerState::ConstEntryIterator
CapturePointUIManagerState::Erase(const ConstEntryIterator entry) {
  assert(entry != entries_.cend());
  return entries_.erase(entry);
}

void CapturePointUIManagerState::Clear() {
  entries_.clear();
}

void CapturePointUIManagerState::SetTickCountProvider(TickCountProvider provider) {
  tick_count_provider_ = std::move(provider);
}

void CapturePointUIManagerState::UseDefaultTickCountProvider() {
  tick_count_provider_ = {};
}

std::uint32_t CapturePointUIManagerState::GetCurrentTickCount() const {
  return tick_count_provider_ ? tick_count_provider_() : openwow::core::GameClock::GetTickCount32();
}

void CapturePointUIManagerState::ArmCaptureWarningCooldown(const std::uint32_t current_tick,
                                                           const std::uint32_t cooldown_ms) {
  next_capture_warning_tick_ = current_tick + cooldown_ms;
}

bool CapturePointUIManagerState::ContainsObjectGuid(const std::uint64_t object_guid) const {
  return FindCapturePointEntryByGuid(*this, object_guid) != entries_.cend();
}

bool CapturePointUIManagerState::ContainsPointId(const std::int32_t point_id) const {
  return std::any_of(entries_.cbegin(), entries_.cend(),
                     [point_id](const CapturePointUIEntry &entry) {
                       return entry.point_id == point_id;
                     });
}

int CapturePointUIManagerNode_Insert(CapturePointUIManagerState &manager,
                                     const openwow::game::CGGameObject_C *object,
                                     const int point_id, const int capture_radius) {
  assert(object != nullptr);
  manager.AddCapturePoint(*object, point_id, capture_radius);
  return point_id;
}

CapturePointUIManagerState::ConstEntryIterator
CapturePointUIManagerNode_Erase(CapturePointUIManagerState &manager,
                                const CapturePointUIManagerState::ConstEntryIterator entry) {
  return manager.Erase(entry);
}

void RegisterCapturePointGameObject(CapturePointUIManagerState &manager,
                                    const openwow::game::CGGameObject_C &object,
                                    const openwow::game::GameObjectTemplateInfo &template_info) {
  if (!object.IsCapturePoint() || manager.ContainsObjectGuid(object.GetGuid().GetRawValue())) {
    return;
  }

  CapturePointUIManagerNode_Insert(manager, &object, static_cast<int>(template_info.raw_data[2]),
                                   static_cast<int>(template_info.raw_data[0]));
}

bool UnregisterCapturePointGameObject(CapturePointUIManagerState &manager,
                                      const std::uint64_t object_guid,
                                      openwow::game::WorldStateManager *world_states) {
  const auto entry = FindCapturePointEntryByGuid(manager, object_guid);
  if (entry == manager.Entries().cend()) {
    return false;
  }

  const std::int32_t point_id = entry->point_id;
  CapturePointUIManagerNode_Erase(manager, entry);

  if (world_states == nullptr || world_states->GetWorldState(point_id) == 0 ||
      manager.ContainsPointId(point_id)) {
    return true;
  }

  world_states->SetWorldState(point_id, 0);
  NotifyWorldStatesChanged();

  RefreshZoneSoundsForActiveMover();
  return true;
}

CapturePointUIManagerState &GetCapturePointUIManagerState() {
  return g_capture_point_ui_manager;
}

void ResetCapturePointUIManagerState() {
  g_capture_point_ui_manager.Clear();
}

void GameUI_CapturePointProximityCheck(CapturePointUIManagerState &manager,
                                       const openwow::game::ObjectManager &object_manager) {
  const auto *active_player = object_manager.GetActivePlayer();
  if (active_player == nullptr) {
    return;
  }

  const std::uint32_t current_tick = manager.GetCurrentTickCount();
  if (!HasReachedTick(current_tick, manager.GetNextCaptureWarningTick())) {
    return;
  }

  if ((active_player->GetPlayerFlags() & kPlayerFlagPvP) != 0 ||
      (active_player->State().GetUnitFlags() & kUnitFlagTaxiFlight) != 0) {
    return;
  }

  for (const auto &entry : manager.Entries()) {
    const auto *capture_point =
        object_manager.GetGameObject(openwow::game::ObjectGuid(entry.object_guid));
    if (capture_point == nullptr ||
        active_player->GetSquaredDistanceToPosition(capture_point->GetPosition()) >=
            static_cast<double>(entry.capture_radius_sq)) {
      continue;
    }

    const std::string message = openwow::game::Localization::Get().GetString(
        "PVP_REQUIRED_FOR_CAPTURE", "PVP_REQUIRED_FOR_CAPTURE");
    openwow::game::ChatFrame_DisplayMessage(object_manager, message.c_str(),
                                            openwow::game::ChatDisplayType::kSystem, nullptr, 0,
                                            nullptr, nullptr, nullptr, 0, 0, 0, 0, 0, nullptr);
    manager.ArmCaptureWarningCooldown(current_tick, kCapturePointWarningCooldownMs);
    return;
  }
}

void GameUI_CapturePointProximityCheck(void *manager_node) {
  auto *manager = static_cast<CapturePointUIManagerState *>(manager_node);
  auto* const ui_manager = runtime::WorldUiRuntimeContext::FromActiveLua();
  const auto* const session = ui_manager != nullptr ? ui_manager->world_session() : nullptr;
  if (manager != nullptr && session != nullptr) {
    GameUI_CapturePointProximityCheck(*manager, session->objects());
  }
}

}

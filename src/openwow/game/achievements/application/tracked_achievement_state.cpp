#include "openwow/game/achievements/application/tracked_achievement_state.h"

#include "openwow/game/versioned_base93_cvar_codec.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/ui/game/game_events.h"
#include "openwow/ui/game/script_event_dispatch.h"

#include <algorithm>
#include <span>
#include <string_view>

namespace openwow::game {
namespace {

constexpr std::string_view kTrackedAchievementsCVarName = "trackedAchievements";

}

TrackedAchievementState& TrackedAchievementState::Get() {
  static TrackedAchievementState instance;
  return instance;
}

void TrackedAchievementState::ResetRuntimeState(const bool loaded) {
  tracked_achievement_ids_.clear();
  loaded_ = loaded;
}

void TrackedAchievementState::Initialize() {
  std::lock_guard lock(mutex_);
  if (initialized_) {
    return;
  }
  ResetRuntimeState(false);
  initialized_ = true;
}

void TrackedAchievementState::Destroy() {
  std::lock_guard lock(mutex_);
  ResetRuntimeState(false);
  initialized_ = false;
}

void TrackedAchievementState::Reset() {
  std::lock_guard lock(mutex_);
  ResetRuntimeState(true);
}

void TrackedAchievementState::SaveTrackedAchievementsToCVar() {
  EnsureLoaded();

  std::string encoded;
  {
    std::lock_guard lock(mutex_);
    std::vector<std::uint32_t> wire_ids;
    wire_ids.reserve(tracked_achievement_ids_.size());
    for (const auto achievement_id : tracked_achievement_ids_) {
      wire_ids.push_back(achievement_id.value);
    }
    encoded = detail::EncodeVersionedBase93Values(
        std::span<const std::uint32_t>(wire_ids));
  }

  ui::game::CVarSystem::Instance().SetCVar(
      std::string(kTrackedAchievementsCVarName), encoded, true);
}

void TrackedAchievementState::LoadTrackedAchievementsFromCVar() {
  auto& cvars = ui::game::CVarSystem::Instance();
  const std::string encoded =
      cvars.GetCVar(std::string(kTrackedAchievementsCVarName));
  if (detail::VersionedBase93NeedsCanonicalRewrite(encoded)) {
    const auto decoded = detail::DecodeVersionedBase93Payload(
        detail::GetVersionedBase93Payload(encoded));
    cvars.SetCVar(std::string(kTrackedAchievementsCVarName),
                  detail::EncodeVersionedBase93Values(decoded), true);
  }

  {
    std::lock_guard lock(mutex_);
    tracked_achievement_ids_.clear();
    loaded_ = true;
  }
  LoadTrackedAchievementsFromString(encoded);
}

void TrackedAchievementState::LoadTrackedAchievementsFromString(
    const std::string& encoded) {
  {
    std::lock_guard lock(mutex_);
    tracked_achievement_ids_.clear();
    loaded_ = true;
  }

  for (const auto achievement_id : detail::DecodeVersionedBase93Payload(
           detail::GetVersionedBase93Payload(encoded))) {
    AddTrackedAchievement(AchievementId{achievement_id});
  }
}

void TrackedAchievementState::AddTrackedAchievement(
    const AchievementId achievement_id) {
  EnsureLoaded();

  bool should_notify = false;
  {
    std::lock_guard lock(mutex_);
    const auto existing = std::find(tracked_achievement_ids_.begin(),
                                    tracked_achievement_ids_.end(),
                                    achievement_id);
    if (existing != tracked_achievement_ids_.end()) {
      should_notify = true;
    } else if (tracked_achievement_ids_.size() < kMaxTrackedAchievements) {
      tracked_achievement_ids_.push_back(achievement_id);
      should_notify = true;
    }
  }

  if (should_notify) {
    FireTrackedAchievementUpdate(achievement_id);
  }
}

void TrackedAchievementState::RemoveTrackedAchievement(
    const AchievementId achievement_id) {
  EnsureLoaded();

  bool removed = false;
  {
    std::lock_guard lock(mutex_);
    const auto existing = std::find(tracked_achievement_ids_.begin(),
                                    tracked_achievement_ids_.end(),
                                    achievement_id);
    if (existing != tracked_achievement_ids_.end()) {
      tracked_achievement_ids_.erase(existing);
      removed = true;
    }
  }

  if (removed) {
    FireTrackedAchievementUpdate(achievement_id);
  }
}

bool TrackedAchievementState::IsTrackedAchievement(
    const AchievementId achievement_id) const {
  EnsureLoaded();
  std::lock_guard lock(mutex_);
  return std::find(tracked_achievement_ids_.begin(),
                   tracked_achievement_ids_.end(), achievement_id) !=
         tracked_achievement_ids_.end();
}

std::size_t TrackedAchievementState::GetNumTrackedAchievements() const {
  EnsureLoaded();
  std::lock_guard lock(mutex_);
  return tracked_achievement_ids_.size();
}

std::vector<AchievementId>
TrackedAchievementState::GetTrackedAchievements() const {
  EnsureLoaded();
  std::lock_guard lock(mutex_);
  return tracked_achievement_ids_;
}

void TrackedAchievementState::EnsureLoaded() const {
  {
    std::lock_guard lock(mutex_);
    if (loaded_) {
      return;
    }
  }
  const_cast<TrackedAchievementState*>(this)
      ->LoadTrackedAchievementsFromCVar();
}

void TrackedAchievementState::FireTrackedAchievementUpdate(
    const AchievementId achievement_id) const {
  ui::game::ScriptEventDispatch::Get().FireGlobalEventWithArgs(
      ui::game::events::TRACKED_ACHIEVEMENT_UPDATE,
      {std::to_string(achievement_id.value)});
}

}

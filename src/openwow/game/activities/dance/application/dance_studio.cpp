#include "openwow/game/activities/dance/application/dance_studio.h"

#include "openwow/game/activities/dance/application/dance_cache_coordinator.h"
#include "openwow/game/activities/dance/application/dance_management_application.h"
#include "openwow/game/activities/dance/application/dance_playback_application.h"
#include "openwow/game/activities/dance/application/known_dance_catalog.h"
#include "openwow/game/activities/dance/model/dance_move_catalog.h"
#include "openwow/game/activities/dance/model/dance_studio_messages.h"
#include "openwow/game/activities/dance/rules/dance_playback_rules.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace openwow::game {

DanceStudioSystem::DanceStudioSystem()
    : known_dances_(std::make_unique<KnownDanceCatalog>()),
      cache_(std::make_unique<DanceCacheCoordinator>()),
      management_(std::make_unique<DanceManagementApplication>(
          *known_dances_, *cache_)),
      playback_(std::make_unique<DancePlaybackApplication>()) {}

DanceStudioSystem::~DanceStudioSystem() = default;

void DanceStudioSystem::Shutdown() {
  ClearKnownDances();
}

void DanceStudioSystem::DestroyDanceCache() {
  cache_->ClearCache();
}

void DanceStudioSystem::ClearPendingQueriesOnLogout() {
  cache_->ClearPendingQueries();
}

void DanceStudioSystem::Reset() {
  Shutdown();
  DestroyDanceCache();
  cache_->ClearPendingQueries();
  dance_query_dispatcher_ = {};
  playback_->ResetSessionState();
}

bool DanceStudioSystem::SendPlayDance(const std::string &dance_name) {
  const KnownDanceSendResult readiness = playback_->CheckKnownDanceSend();
  if (readiness == KnownDanceSendResult::kAlreadyDancing) {
    EmitConsoleLine("WARNING! Cannot start a new dance while one is in progress",
                    ConsoleColor::kError);
    return false;
  }
  if (readiness != KnownDanceSendResult::kReady) {
    return false;
  }

  const auto *entry = FindDanceByName(dance_name);
  if (entry == nullptr) {
    EmitConsoleLine("You haven't created a dance with that name",
                    ConsoleColor::kDefault);
    return false;
  }

  return playback_->SendKnownDance(entry->dance_id, entry->sequence_id);
}

bool DanceStudioSystem::HasDanceByName(const std::string &name) const {
  return FindDanceByName(name) != nullptr;
}

void DanceStudioSystem::PrintDanceLoaded(const DanceCacheRecord &dance_data) const {
  EmitConsoleLine("Dance Loaded:", ConsoleColor::kDefault);

  const std::optional<DancePlayerClass> active_player_class =
      playback_->ActivePlayerClass();
  if (!active_player_class) {
    return;
  }

  const DanceMoveCatalog* dance_move_catalog = playback_->MoveCatalog();
  for (std::size_t index = 0; index < dance_data.moves.size(); ++index) {
    const auto &move = dance_data.moves[index];
    const DanceMoveRecord *move_record =
        dance_move_catalog != nullptr
            ? dance_move_catalog->Lookup(move.move_id)
            : nullptr;
    if (move_record == nullptr) {
      EmitConsoleLine(
          "Unknown dance move (ID: " +
              std::to_string(static_cast<std::uint32_t>(
                  static_cast<std::int32_t>(move.move_id.value))) +
              ")!",
          ConsoleColor::kDefault);
      continue;
    }

    const bool allowed = MeetsDanceMoveRequirements(
        *move_record, active_player_class, playback_->LearnedMoveMask());
    EmitConsoleLine(std::to_string(index + 1) + ") Move: " + move_record->name +
                        " Chance: " +
                        std::to_string(move.chance.value) + "%",
                    allowed ? ConsoleColor::kDefault : ConsoleColor::kError);
  }
}

void DanceStudioSystem::RequestDanceFromCache(const DanceId dance_id) {
  cache_->Invalidate(dance_id);
  if (!dance_query_dispatcher_) {
    return;
  }

  QueueDanceQuery(
      dance_id, [this](const DanceId completed_dance_id,
                       const DanceQueryStatus status) {
        HandleQueryResult(completed_dance_id, status);
      });
  dance_query_dispatcher_(dance_id);
}

void DanceStudioSystem::CacheDance(DanceCacheRecord dance) {
  cache_->Cache(std::move(dance));
}

void DanceStudioSystem::InvalidateDance(const DanceId dance_id) {
  cache_->Invalidate(dance_id);
}

const DanceCacheRecord *DanceStudioSystem::FindCachedDance(
    const DanceId dance_id) const {
  return cache_->Find(dance_id);
}

void DanceStudioSystem::HandleStopDance(const StopDanceCommand& command) {
  playback_->CancelDance(command.unit_guid);
}

namespace {

[[nodiscard]] constexpr DanceSystemMessageId ToSystemMessageId(
    const DanceManagementError error) {
  switch (error) {
  case DanceManagementError::kNameTaken:
    return DanceSystemMessageId{581};
  case DanceManagementError::kMaximumDancesReached:
    return DanceSystemMessageId{582};
  case DanceManagementError::kUnknownDance:
    return DanceSystemMessageId{583};
  }
  return {};
}

void ReportPlaybackStartResult(
    const DancePlaybackStartResult result,
    const DanceStudioSystem::ConsoleSink& console_sink) {
  if (result == DancePlaybackStartResult::kAlreadyDancing && console_sink) {
    console_sink("WARNING! Cannot start a new dance while one is in progress",
                 DanceStudioSystem::ConsoleColor::kError);
  }
}

}

void DanceStudioSystem::HandlePlayDance(const PlayDanceCommand& command) {
  if (const DanceCacheRecord* dance = FindCachedDance(command.dance_id);
      dance != nullptr && dance->checksum == command.checksum) {
    const DancePlaybackStartResult result = playback_->StartResolvedDance(
        command.unit_guid, *dance, command.start_step, command.seed);
    if (result == DancePlaybackStartResult::kAlreadyDancing) {
      EmitConsoleLine("WARNING! Cannot start a new dance while one is in progress",
                      ConsoleColor::kError);
    }
    return;
  }

  QueueDanceQuery(command.dance_id,
                  [this, command](DanceId resolved_dance_id,
                                  DanceQueryStatus status) {
    if (status == DanceQueryStatus::kMissing) {
      return;
    }

    const DanceCacheRecord* dance = FindCachedDance(resolved_dance_id);
    if (dance != nullptr) {
      ReportPlaybackStartResult(
          playback_->StartResolvedDance(command.unit_guid, *dance,
                                        command.start_step, command.seed),
          console_sink_);
    }
  });
  if (dance_query_dispatcher_) {
    dance_query_dispatcher_(command.dance_id);
  }
}

void DanceStudioSystem::HandleDanceQueryFound(DanceCacheRecord dance) {
  CacheDance(std::move(dance));
}

void DanceStudioSystem::HandleDanceQueryMissing(const DanceId dance_id) {
  InvalidateDance(dance_id);
}

void DanceStudioSystem::HandleInvalidateDance(
    const InvalidateDanceCommand& command) {
  InvalidateDance(command.dance_id);
}

void DanceStudioSystem::HandleLearnedDanceMoves(
    const LearnedDanceMovesUpdate& update) {
  SetLearnedDanceMoveMask(update.learned_move_mask);
}

void DanceStudioSystem::SetActivePlayerClass(
    std::optional<DancePlayerClass> player_class) {
  playback_->SetActivePlayerClass(player_class);
}

void DanceStudioSystem::SetLearnedDanceMoveMask(
    const LearnedDanceMoveMask learned_move_mask) {
  playback_->SetLearnedDanceMoveMask(learned_move_mask);
}

void DanceStudioSystem::SetDanceQueryDispatcher(DanceQueryDispatcher dispatcher) {
  dance_query_dispatcher_ = std::move(dispatcher);
}

void DanceStudioSystem::SetActivePlayerDanceStateProvider(ActivePlayerDanceStateProvider provider) {
  playback_->SetActivePlayerDanceStateProvider(std::move(provider));
}

void DanceStudioSystem::SetPlayDanceSender(PlayDanceSender sender) {
  playback_->SetPlayDanceSender(std::move(sender));
}

void DanceStudioSystem::SetConsoleSink(ConsoleSink sink) {
  console_sink_ = std::move(sink);
}

void DanceStudioSystem::SetSystemMessageSink(SystemMessageSink sink) {
  system_message_sink_ = std::move(sink);
}

void DanceStudioSystem::SetNameHasher(NameHasher hasher) {
  known_dances_->SetNameHasher(std::move(hasher));
}

void DanceStudioSystem::SetNameEqual(NameEqual equal) {
  known_dances_->SetNameEqual(std::move(equal));
}

void DanceStudioSystem::SetUnitDanceStateProvider(
    UnitDanceStateProvider provider) {
  playback_->SetUnitDanceStateProvider(std::move(provider));
}

void DanceStudioSystem::SetUnitDanceCanceller(
    UnitDanceCanceller canceller) {
  playback_->SetUnitDanceCanceller(std::move(canceller));
}

void DanceStudioSystem::SetUnitDanceStarter(UnitDanceStarter starter) {
  playback_->SetUnitDanceStarter(std::move(starter));
}

void DanceStudioSystem::BindDanceMoveCatalog(const DanceMoveCatalog& catalog) {
  playback_->BindDanceMoveCatalog(catalog);
}

void DanceStudioSystem::UnbindDanceMoveCatalog() {
  playback_->UnbindDanceMoveCatalog();
}

void DanceStudioSystem::EmitConsoleLine(
    const std::string &text, const ConsoleColor color) const {
  if (console_sink_) {
    console_sink_(text, color);
  }
}

void DanceStudioSystem::QueueDanceQuery(
    const DanceId dance_id, DanceQueryResultCallback callback) {
  cache_->Queue(dance_id, std::move(callback));
}

void DanceStudioSystem::HandleQueryResult(
    const DanceId dance_id, const DanceQueryStatus status) {
  if (status == DanceQueryStatus::kMissing) {
    EmitConsoleLine("Dance no longer exists", ConsoleColor::kError);
    return;
  }

  const DanceCacheRecord *dance = FindCachedDance(dance_id);
  if (dance != nullptr) {
    PrintDanceLoaded(*dance);
  }
}

void DanceStudioSystem::AddKnownDance(
    const std::string &name, const DanceId dance_id,
    const DanceSequenceId sequence_id) {
  known_dances_->Add(name, dance_id, sequence_id);
}

void DanceStudioSystem::RemoveKnownDance(const DanceId dance_id) {
  known_dances_->Remove(dance_id);
}

void DanceStudioSystem::UpdateKnownDance(
    const std::string &name, const DanceId dance_id,
    const DanceSequenceId sequence_id) {
  known_dances_->Update(name, dance_id, sequence_id);
}

const KnownDanceEntry *DanceStudioSystem::FindDanceByName(const std::string &name) const {
  return known_dances_->FindByName(name);
}

const KnownDanceEntry *DanceStudioSystem::FindDanceById(
    const DanceId dance_id) const {
  return known_dances_->FindById(dance_id);
}

void DanceStudioSystem::ClearKnownDances() {
  known_dances_->Clear();
}

void DanceStudioSystem::HandleDanceManagement(
    const DanceManagementNotification& notification) {
  const std::optional<DanceManagementError> error =
      management_->Apply(notification);
  if (error && system_message_sink_) {
    system_message_sink_(ToSystemMessageId(*error));
  }
}

}

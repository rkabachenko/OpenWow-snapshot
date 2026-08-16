#pragma once

#include "openwow/game/activities/dance/model/dance_types.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace openwow::game {

class KnownDanceCatalog;
class DanceCacheCoordinator;
class DanceManagementApplication;
class DancePlaybackApplication;
class DanceMoveCatalog;
struct DanceCacheRecord;
struct DanceManagementNotification;
struct DanceSequence;
struct InvalidateDanceCommand;
struct LearnedDanceMovesUpdate;
struct PlayDanceCommand;
struct StopDanceCommand;
struct KnownDanceEntry;

class DanceStudioSystem final {
 public:
  enum class ConsoleColor : std::uint8_t {
    kDefault,
    kError,
  };

  using ConsoleSink = std::function<void(const std::string &, ConsoleColor)>;
  using SystemMessageSink = std::function<void(DanceSystemMessageId)>;
  using NameHasher = std::function<std::uint32_t(std::string_view)>;
  using NameEqual = std::function<bool(std::string_view, std::string_view)>;
  using UnitDanceStateProvider =
      std::function<std::optional<DancePlaybackState>(DanceUnitGuid)>;
  using UnitDanceCanceller = std::function<void(DanceUnitGuid)>;
  using UnitDanceStarter =
      std::function<void(DanceUnitGuid, DanceSequence,
                         const DanceMoveCatalog&)>;
  using DanceQueryDispatcher = std::function<void(DanceId)>;
  using ActivePlayerDanceStateProvider =
      std::function<std::optional<DancePlaybackState>()>;
  using PlayDanceSender =
      std::function<void(DanceId, DanceSequenceId)>;

  DanceStudioSystem();
  ~DanceStudioSystem();

  void Shutdown();
  void DestroyDanceCache();

  void ClearPendingQueriesOnLogout();

  void Reset();

  [[nodiscard]] bool SendPlayDance(const std::string &dance_name);
  [[nodiscard]] bool HasDanceByName(const std::string &name) const;

  void PrintDanceLoaded(const DanceCacheRecord &dance_data) const;
  void RequestDanceFromCache(DanceId dance_id);

  void CacheDance(DanceCacheRecord dance);
  void InvalidateDance(DanceId dance_id);
  [[nodiscard]] const DanceCacheRecord *FindCachedDance(
      DanceId dance_id) const;

  void HandleStopDance(const StopDanceCommand& command);
  void HandlePlayDance(const PlayDanceCommand& command);
  void HandleDanceQueryFound(DanceCacheRecord dance);
  void HandleDanceQueryMissing(DanceId dance_id);
  void HandleInvalidateDance(const InvalidateDanceCommand& command);
  void HandleLearnedDanceMoves(const LearnedDanceMovesUpdate& update);

  void SetActivePlayerClass(std::optional<DancePlayerClass> player_class);
  void SetLearnedDanceMoveMask(LearnedDanceMoveMask learned_move_mask);
  void SetDanceQueryDispatcher(DanceQueryDispatcher dispatcher);
  void SetActivePlayerDanceStateProvider(
      ActivePlayerDanceStateProvider provider);
  void SetPlayDanceSender(PlayDanceSender sender);
  void SetConsoleSink(ConsoleSink sink);
  void SetSystemMessageSink(SystemMessageSink sink);
  void SetNameHasher(NameHasher hasher);
  void SetNameEqual(NameEqual equal);
  void SetUnitDanceStateProvider(UnitDanceStateProvider provider);
  void SetUnitDanceCanceller(UnitDanceCanceller canceller);
  void SetUnitDanceStarter(UnitDanceStarter starter);
  void BindDanceMoveCatalog(const DanceMoveCatalog& catalog);
  void UnbindDanceMoveCatalog();

  void AddKnownDance(const std::string &name, DanceId dance_id,
                     DanceSequenceId sequence_id);
  void RemoveKnownDance(DanceId dance_id);
  void UpdateKnownDance(const std::string &name, DanceId dance_id,
                        DanceSequenceId sequence_id);
  [[nodiscard]] const KnownDanceEntry *FindDanceByName(
      const std::string &name) const;
  [[nodiscard]] const KnownDanceEntry *FindDanceById(
      DanceId dance_id) const;
  void ClearKnownDances();

  void HandleDanceManagement(const DanceManagementNotification& notification);

 private:
  using DanceQueryResultCallback =
      std::function<void(DanceId, DanceQueryStatus)>;

  void EmitConsoleLine(const std::string &text, ConsoleColor color) const;
  void HandleQueryResult(DanceId dance_id, DanceQueryStatus status);
  void QueueDanceQuery(DanceId dance_id,
                       DanceQueryResultCallback callback);

  std::unique_ptr<KnownDanceCatalog> known_dances_;
  std::unique_ptr<DanceCacheCoordinator> cache_;
  std::unique_ptr<DanceManagementApplication> management_;
  std::unique_ptr<DancePlaybackApplication> playback_;
  ConsoleSink console_sink_;
  SystemMessageSink system_message_sink_;
  DanceQueryDispatcher dance_query_dispatcher_;
};

}

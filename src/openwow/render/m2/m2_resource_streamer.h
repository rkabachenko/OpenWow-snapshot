#pragma once

#include "openwow/runtime/scheduling/thread_pool_system.h"
#include "openwow/render/m2/m2_public_types.h"
#include "openwow/render/resources/textures/texture_manager.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace openwow::render::m2 {

class M2ModelRepository;

[[nodiscard]] M2PreparedResourceBundle PrepareM2ResourceBundle(
    const M2StreamModelPreparer& model_preparer,
    const std::string& model_path,
    const M2StreamFileLoader& loader,
    const M2StreamTexturePreparer& texture_preparer = {},
    const M2ModelEarlyReadyCallback& on_early_ready = {});

class M2ResourceStreamer {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;
  using NowCallback = std::function<TimePoint()>;

  explicit M2ResourceStreamer(M2ModelRepository& repository,
                               M2ResourceStreamerBackend backend,
                               NowCallback now = {});
  ~M2ResourceStreamer();

  M2ResourceStreamer(const M2ResourceStreamer&) = delete;
  M2ResourceStreamer& operator=(const M2ResourceStreamer&) = delete;

  void SetFileLoader(M2StreamFileLoader loader);

  void SetPrefixFileLoader(M2StreamPrefixFileLoader loader);
  void Start(std::uint32_t worker_count);
  void Shutdown();

  void ReleaseDeviceResources();

  [[nodiscard]] M2StreamTicket Acquire(
      const std::string& model_path,
      M2StreamPriority priority = M2StreamPriority::kNormal);
  void Release(M2StreamTicket ticket);
  [[nodiscard]] M2StreamQuery Query(M2StreamTicket ticket) const;

  [[nodiscard]] M2InstanceCreateResult CreateInstance(
      M2StreamTicket ticket) const;

  [[nodiscard]] M2StreamPumpResult Pump(
      const M2StreamCommitBudget& budget = {});

  static constexpr std::chrono::milliseconds kRetailRecycleDelay{10000};
  static constexpr std::chrono::milliseconds kNegativeCacheDelay{10000};

 private:
  enum class Phase : std::uint8_t {
    kPreparing,
    kCommitTextures,
    kCommitModel,
    kReady,
    kFailed,
  };

  struct Record {
    std::uint32_t id{0};
    std::uint64_t generation{0};
    std::uint64_t strong_lease_count{0};
    M2StreamPriority priority{M2StreamPriority::kNormal};
    std::string authored_path;
    Phase phase{Phase::kPreparing};
    std::uint32_t worker_task_id{0};
    std::uint32_t model_id{0};
    std::size_t next_texture{0};
    bool queued_for_commit{false};
    TimePoint recycle_after{};
    TimePoint retry_after{};
    M2ResultStatus status{M2ResultStatus::kNotReady};
    M2ResultReason reason{M2ResultReason::kNone};
    std::string detail;
    M2ModelSpatialInfo spatial_info;
    std::shared_ptr<const M2ModelCollisionGeometry> collision_geometry;

    bool cpu_model_ready{false};

    bool header_bounds_ready{false};
    std::vector<M2ModelTextureDependency> texture_dependencies;
    std::optional<M2PreparedResourceBundle> prepared;
  };

  struct Completion {
    std::uint32_t resource_id{0};
    std::uint64_t generation{0};
    M2PreparedResourceBundle prepared;
  };

  struct EarlyReadyCompletion {
    std::uint32_t resource_id{0};
    std::uint64_t generation{0};
    M2ModelSpatialInfo spatial_info;
    std::shared_ptr<const M2ModelCollisionGeometry> collision_geometry;
  };

  struct HeaderBoundsCompletion {
    std::uint32_t resource_id{0};
    std::uint64_t generation{0};
    M2ModelSpatialInfo spatial_info;
  };

  struct Lease {
    std::uint32_t record_id{0};
    std::uint64_t generation{0};
  };

  struct CompletionMailbox {
    std::mutex mutex;
    std::deque<Completion> completions;
    std::deque<EarlyReadyCompletion> early_completions;
    std::deque<HeaderBoundsCompletion> header_bounds_completions;
  };

  [[nodiscard]] TimePoint Now() const;
  void Schedule(Record& record);

  void DrainHeaderBoundsCompletions();
  void DrainEarlyCompletions();
  void DrainCompletions(M2StreamPumpResult& result);
  void QueueForCommit(Record& record);
  void MarkFailed(Record& record, M2ResultStatus status,
                  M2ResultReason reason, std::string detail);
  bool CommitOne(Record& record, const M2StreamCommitBudget& budget,
                 M2StreamPumpResult& result);
  void RecycleExpired(M2StreamPumpResult& result);
  void RemoveRecord(std::uint32_t resource_id);

  M2ResourceStreamerBackend backend_;
  M2ModelRepository& repository_;
  NowCallback now_;
  M2StreamFileLoader file_loader_;
  M2StreamPrefixFileLoader prefix_file_loader_;
  core::ThreadPoolSystem workers_;
  std::shared_ptr<CompletionMailbox> mailbox_;
  std::unordered_map<std::uint32_t, Record> records_;
  std::unordered_map<std::uint64_t, Lease> leases_;
  std::deque<std::uint32_t> commit_queue_;

  TimePoint next_recycle_scan_{};
  std::uint64_t next_generation_{1};
  std::uint64_t next_lease_id_{1};
};

}

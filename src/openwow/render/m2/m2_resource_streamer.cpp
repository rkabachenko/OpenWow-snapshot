#include "openwow/render/m2/m2_resource_streamer.h"
#include "openwow/render/m2/m2_model_preparation.h"
#include "openwow/render/m2/m2_model_repository.h"

#include "openwow/data/formats/m2/model_path.h"
#include "openwow/data/model/m2_model.h"
#include "openwow/data/texture_cache.h"
#include "openwow/render/backend/bgfx/renderer_context_services.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <thread>
#include <unordered_set>
#include <utility>

namespace openwow::render::m2 {

M2PreparedResourceBundle PrepareM2ResourceBundle(
    const M2StreamModelPreparer& model_preparer,
    const std::string& model_path,
    const M2StreamFileLoader& loader,
    const M2StreamTexturePreparer& texture_preparer,
    const M2ModelEarlyReadyCallback& on_early_ready) {
  M2PreparedResourceBundle bundle;
  auto preparation = model_preparer(model_path, loader, on_early_ready);
  bundle.status = preparation.status;
  bundle.reason = preparation.reason;
  bundle.detail = std::move(preparation.detail);
  if (preparation.status != M2ResultStatus::kReady ||
      !preparation.prepared) {
    return bundle;
  }

  bundle.spatial_info = preparation.prepared->SpatialInfo();
  bundle.collision_geometry = preparation.prepared->CollisionGeometry();
  std::unordered_set<std::uint32_t> seen_texture_rows;
  for (const auto& dependency : preparation.prepared->TextureDependencies()) {
    if (dependency.texture_path.empty() ||
        !seen_texture_rows
             .insert(openwow::data::HashTextureCachePath(
                 dependency.texture_path))
             .second) {
      continue;
    }

    bundle.texture_dependencies.push_back(dependency);

    if (!IsRendererContextActive() && !IsRendererDeviceRestarting()) {
      continue;
    }
    auto upload = texture_preparer
                      ? texture_preparer(dependency.texture_path)
                      : TextureManager::PrepareTextureUploadFromLoader(
                            dependency.texture_path, loader);
    if (!upload.valid) {
      bundle.status = M2ResultStatus::kFailed;
      bundle.reason = M2ResultReason::kMissingTexture;
      bundle.detail = dependency.texture_path;
      bundle.textures.clear();
      return bundle;
    }
    bundle.textures.push_back(std::move(upload));
  }

  bundle.model = std::move(preparation.prepared);
  bundle.status = M2ResultStatus::kReady;
  return bundle;
}

M2ResourceStreamer::M2ResourceStreamer(
    M2ModelRepository& repository, M2ResourceStreamerBackend backend,
    NowCallback now)
    : backend_(std::move(backend)), repository_(repository),
      now_(std::move(now)),
      mailbox_(std::make_shared<CompletionMailbox>()) {
  if (!now_) {
    now_ = [] { return Clock::now(); };
  }
}

M2ResourceStreamer::~M2ResourceStreamer() { Shutdown(); }

void M2ResourceStreamer::SetFileLoader(M2StreamFileLoader loader) {
  file_loader_ = std::move(loader);

  std::vector<std::uint32_t> obsolete_failures;
  for (const auto& [id, record] : records_) {
    if (record.phase == Phase::kFailed &&
        record.strong_lease_count == 0u) {
      obsolete_failures.push_back(id);
    }
  }
  for (const std::uint32_t id : obsolete_failures) {
    RemoveRecord(id);
  }
}

void M2ResourceStreamer::SetPrefixFileLoader(M2StreamPrefixFileLoader loader) {

  prefix_file_loader_ = std::move(loader);
}

void M2ResourceStreamer::Start(const std::uint32_t worker_count) {
  if (workers_.IsInitialized()) {
    return;
  }
  mailbox_ = std::make_shared<CompletionMailbox>();
  workers_.Initialize(worker_count);
}

void M2ResourceStreamer::Shutdown() {
  if (workers_.IsInitialized()) {
    workers_.Shutdown();
  }

  if (backend_.recycle_model) {
    for (const auto& [id, record] : records_) {
      (void)id;
      if (record.model_id != 0u) {
        try {
          static_cast<void>(backend_.recycle_model(record.model_id));
        } catch (...) {
        }
      }
    }
  }
  for (const auto& [id, record] : records_) {
    (void)record;
    repository_.ForgetUnpublishedIdentity(id);
  }
  leases_.clear();
  records_.clear();
  commit_queue_.clear();
  file_loader_ = {};
  prefix_file_loader_ = {};
  mailbox_ = std::make_shared<CompletionMailbox>();
}

void M2ResourceStreamer::ReleaseDeviceResources() {

  if (workers_.IsInitialized()) {
    for (const auto& [id, record] : records_) {
      (void)id;
      if (record.worker_task_id != 0u) {
        static_cast<void>(workers_.CancelTask(record.worker_task_id));
        static_cast<void>(workers_.ForgetTask(record.worker_task_id));
      }
    }
  }
  for (const auto& [id, record] : records_) {
    (void)record;
    repository_.ForgetUnpublishedIdentity(id);
  }
  leases_.clear();
  records_.clear();
  commit_queue_.clear();
  mailbox_ = std::make_shared<CompletionMailbox>();
}

M2ResourceStreamer::TimePoint M2ResourceStreamer::Now() const {
  return now_();
}

M2StreamTicket M2ResourceStreamer::Acquire(
    const std::string& model_path, const M2StreamPriority priority) {
  const std::string authored_path =
      openwow::data::m2::NormalizeModelPath(model_path);
  const M2ModelIdentity identity =
      repository_.ReserveIdentity(authored_path);
  if (!identity.valid) {
    return {};
  }

  const TimePoint now = Now();
  Record* acquired = nullptr;
  if (auto record_it = records_.find(identity.model_id);
      record_it != records_.end()) {
    Record& record = record_it->second;
    if (priority > record.priority) {
      record.priority = priority;
      if (record.queued_for_commit) {
        std::erase(commit_queue_, record.id);
        record.queued_for_commit = false;
        QueueForCommit(record);
      } else if (record.phase == Phase::kPreparing &&
                 record.worker_task_id != 0u &&
                 workers_.GetTaskStatus(record.worker_task_id) ==
                     core::TaskStatus::Pending &&
                 workers_.CancelTask(record.worker_task_id)) {
        static_cast<void>(workers_.ForgetTask(record.worker_task_id));
        record.worker_task_id = 0u;
        Schedule(record);
      }
    }
    if (record.phase == Phase::kFailed &&
        record.strong_lease_count == 0u &&
        now >= record.retry_after) {
      record.generation = next_generation_++;
      if (record.generation == 0u) {
        record.generation = next_generation_++;
      }
      record.phase = Phase::kPreparing;
      record.status = M2ResultStatus::kNotReady;
      record.reason = M2ResultReason::kNone;
      record.detail.clear();
      record.next_texture = 0u;
      record.prepared.reset();

      record.spatial_info = {};
      record.collision_geometry.reset();
      record.cpu_model_ready = false;
      record.header_bounds_ready = false;
      Schedule(record);
    }
    record.recycle_after = {};
    acquired = &record;
  } else {
    Record record;
    record.id = identity.model_id;
    record.generation = next_generation_++;
    if (record.generation == 0u) {
      record.generation = next_generation_++;
    }
    record.authored_path = identity.load_path;
    record.priority = priority;
    if (repository_.IsRenderReady(identity.model_id)) {
      record.phase = Phase::kReady;
      record.status = M2ResultStatus::kReady;
      record.model_id = identity.model_id;

      {

        std::lock_guard repository_lock(repository_.mutex());
        if (const auto model_it = repository_.models().find(identity.model_id);
            model_it != repository_.models().end() && model_it->second) {
          record.spatial_info = BuildM2ModelSpatialInfo(*model_it->second);
          record.collision_geometry = model_it->second->collision_geometry;
          record.cpu_model_ready = true;

        }
      }
    }
    auto [inserted_it, inserted] =
        records_.emplace(record.id, std::move(record));
    (void)inserted;
    acquired = &inserted_it->second;
    if (acquired->phase != Phase::kReady) {
      Schedule(*acquired);
    }
  }

  std::uint64_t lease_id = 0u;
  do {
    lease_id = next_lease_id_++;
  } while (lease_id == 0u || leases_.contains(lease_id));
  leases_.emplace(lease_id,
                  Lease{
                      .record_id = acquired->id,
                      .generation = acquired->generation,
                  });
  ++acquired->strong_lease_count;
  return {.resource_id = lease_id, .generation = acquired->generation};
}

void M2ResourceStreamer::Schedule(Record& record) {
  if (!backend_.prepare || !file_loader_) {
    MarkFailed(record, M2ResultStatus::kNotReady,
               M2ResultReason::kMissingFile, "resource loader unavailable");
    return;
  }

  record.phase = Phase::kPreparing;
  try {
    if (!workers_.IsInitialized()) {
      const std::uint32_t hardware_threads =
          std::thread::hardware_concurrency();
      Start(std::clamp(hardware_threads == 0u ? 2u : hardware_threads / 2u,
                       2u, 4u));
    }
    const auto prepare = backend_.prepare;
    const auto loader = file_loader_;
    const auto prefix_loader = prefix_file_loader_;
    const auto mailbox = mailbox_;
    const std::string path = record.authored_path;
    const std::uint32_t resource_id = record.id;
    const std::uint64_t generation = record.generation;
    const auto task_priority = [&record] {
      switch (record.priority) {
        case M2StreamPriority::kAmbient:
          return core::TaskPriority::Low;
        case M2StreamPriority::kNormal:
          return core::TaskPriority::Normal;
        case M2StreamPriority::kVisibleUnit:
          return core::TaskPriority::High;
        case M2StreamPriority::kCritical:
          return core::TaskPriority::Critical;
      }
      return core::TaskPriority::Normal;
    }();
    record.worker_task_id = workers_.Submit(
        "m2-resource:" + path, task_priority,
        [prepare, loader, prefix_loader, mailbox, path, resource_id,
         generation]() mutable {

          if (prefix_loader) {
            try {
              const auto prefix_bytes = prefix_loader(
                  path, data::model::kM2HeaderBoundsPrefixBytes);
              const auto header_bounds =
                  data::model::ParseM2HeaderBounds(prefix_bytes);
              if (header_bounds.ok) {
                std::lock_guard lock(mailbox->mutex);
                mailbox->header_bounds_completions.push_back({
                    .resource_id = resource_id,
                    .generation = generation,
                    .spatial_info =
                        BuildM2ModelSpatialInfoFromHeaderBounds(header_bounds),
                });
              }
            } catch (...) {

            }
          }

          M2PreparedResourceBundle prepared;
          try {
            const M2StreamTexturePreparer prepare_texture =
                [loader](const std::string& texture_path) {
                  PreparedTextureUpload upload;
                  try {
                    upload = TextureManager::PrepareTextureUploadFromLoader(
                        texture_path, loader);
                  } catch (const std::exception& error) {
                    upload.path = texture_path;
                    upload.error = error.what();
                  } catch (...) {
                    upload.path = texture_path;
                    upload.error = "unknown texture preparation exception";
                  }
                  return upload;
                };

            const M2ModelEarlyReadyCallback on_early_ready =
                [mailbox, resource_id, generation](
                    const M2ModelSpatialInfo& spatial_info,
                    std::shared_ptr<const M2ModelCollisionGeometry>
                        collision_geometry) {
                  std::lock_guard lock(mailbox->mutex);
                  mailbox->early_completions.push_back({
                      .resource_id = resource_id,
                      .generation = generation,
                      .spatial_info = spatial_info,
                      .collision_geometry = std::move(collision_geometry),
                  });
                };
            prepared = prepare(path, loader, prepare_texture, on_early_ready);
          } catch (const std::exception& error) {
            prepared.status = M2ResultStatus::kFailed;
            prepared.reason = M2ResultReason::kParseFailed;
            prepared.detail = error.what();
          } catch (...) {
            prepared.status = M2ResultStatus::kFailed;
            prepared.reason = M2ResultReason::kParseFailed;
            prepared.detail = "unknown M2 preparation exception";
          }
          std::lock_guard lock(mailbox->mutex);
          mailbox->completions.push_back({
              .resource_id = resource_id,
              .generation = generation,
              .prepared = std::move(prepared),
          });
        });
  } catch (const std::exception& error) {
    MarkFailed(record, M2ResultStatus::kFailed,
               M2ResultReason::kParseFailed, error.what());
  } catch (...) {
    MarkFailed(record, M2ResultStatus::kFailed,
               M2ResultReason::kParseFailed,
               "unknown task scheduling exception");
  }
}

void M2ResourceStreamer::Release(const M2StreamTicket ticket) {
  const auto lease_it = leases_.find(ticket.resource_id);
  if (lease_it == leases_.end() ||
      lease_it->second.generation != ticket.generation) {
    return;
  }

  const Lease lease = lease_it->second;
  const auto record_it = records_.find(lease.record_id);
  if (record_it == records_.end() ||
      record_it->second.generation != lease.generation ||
      record_it->second.strong_lease_count == 0u) {
    leases_.erase(lease_it);
    return;
  }
  Record& record = record_it->second;
  leases_.erase(lease_it);
  --record.strong_lease_count;
  if (record.strong_lease_count != 0u) {
    return;
  }

  if (record.phase == Phase::kPreparing) {
    if (record.worker_task_id != 0u) {
      static_cast<void>(workers_.CancelTask(record.worker_task_id));
      static_cast<void>(workers_.ForgetTask(record.worker_task_id));
    }
    RemoveRecord(record.id);
    return;
  }

  if (record.phase == Phase::kCommitTextures ||
      record.phase == Phase::kCommitModel) {

    QueueForCommit(record);
    return;
  }

  if (record.phase == Phase::kReady) {
    record.recycle_after = Now() + kRetailRecycleDelay;
  }
}

M2StreamQuery M2ResourceStreamer::Query(const M2StreamTicket ticket) const {
  const auto lease = leases_.find(ticket.resource_id);
  if (lease == leases_.end() ||
      lease->second.generation != ticket.generation) {
    return {.state = M2StreamState::kInvalid,
            .status = M2ResultStatus::kFailed,
            .reason = M2ResultReason::kInvalidHandle};
  }
  const auto it = records_.find(lease->second.record_id);
  if (it == records_.end() || it->second.generation != ticket.generation) {
    return {.state = M2StreamState::kInvalid,
            .status = M2ResultStatus::kFailed,
            .reason = M2ResultReason::kInvalidHandle};
  }

  const Record& record = it->second;
  M2StreamState state = M2StreamState::kPreparing;
  switch (record.phase) {
    case Phase::kPreparing:
      state = M2StreamState::kPreparing;
      break;
    case Phase::kCommitTextures:
    case Phase::kCommitModel:
      state = M2StreamState::kCommitPending;
      break;
    case Phase::kReady:
      state = M2StreamState::kReady;
      break;
    case Phase::kFailed:
      state = M2StreamState::kFailed;
      break;
  }
  return {
      .state = state,
      .status = record.status,
      .reason = record.reason,
      .detail = record.detail,
      .model_id = record.model_id,
      .spatial_info = record.spatial_info,
      .collision_geometry = record.collision_geometry,
      .cpu_model_ready = record.cpu_model_ready,

      .header_bounds_ready = record.header_bounds_ready || record.cpu_model_ready,
      .texture_dependencies = record.texture_dependencies,
      .committed_texture_count = record.next_texture,
  };
}

M2InstanceCreateResult M2ResourceStreamer::CreateInstance(
    const M2StreamTicket ticket) const {
  const auto lease = leases_.find(ticket.resource_id);
  if (lease == leases_.end() ||
      lease->second.generation != ticket.generation) {
    return {
        .status = M2ResultStatus::kFailed,
        .reason = M2ResultReason::kInvalidHandle,
    };
  }
  const auto found = records_.find(lease->second.record_id);
  if (found == records_.end() ||
      found->second.generation != ticket.generation) {
    return {
        .status = M2ResultStatus::kFailed,
        .reason = M2ResultReason::kInvalidHandle,
    };
  }
  const Record& record = found->second;
  if (record.phase != Phase::kReady || record.model_id == 0u) {
    return {
        .status = record.status == M2ResultStatus::kReady
                      ? M2ResultStatus::kNotReady
                      : record.status,
        .reason = record.reason,
        .detail = record.detail,
    };
  }
  if (!backend_.create_instance) {
    return {
        .status = M2ResultStatus::kFailed,
        .reason = M2ResultReason::kInvalidHandle,
        .detail = "instance creation backend unavailable",
    };
  }
  try {
    return backend_.create_instance(record.model_id);
  } catch (const std::exception& error) {
    return {
        .status = M2ResultStatus::kFailed,
        .reason = M2ResultReason::kInvalidHandle,
        .detail = error.what(),
    };
  } catch (...) {
    return {
        .status = M2ResultStatus::kFailed,
        .reason = M2ResultReason::kInvalidHandle,
        .detail = "unknown instance creation exception",
    };
  }
}

void M2ResourceStreamer::DrainHeaderBoundsCompletions() {
  std::deque<HeaderBoundsCompletion> header_bounds_completions;
  {
    std::lock_guard lock(mailbox_->mutex);
    header_bounds_completions.swap(mailbox_->header_bounds_completions);
  }
  for (auto& completion : header_bounds_completions) {
    const auto it = records_.find(completion.resource_id);

    if (it == records_.end() || it->second.generation != completion.generation ||
        it->second.phase != Phase::kPreparing) {
      continue;
    }
    Record& record = it->second;

    if (!record.cpu_model_ready) {
      record.spatial_info = completion.spatial_info;
    }
    record.header_bounds_ready = true;
  }
}

void M2ResourceStreamer::DrainEarlyCompletions() {
  std::deque<EarlyReadyCompletion> early_completions;
  {
    std::lock_guard lock(mailbox_->mutex);
    early_completions.swap(mailbox_->early_completions);
  }
  for (auto& completion : early_completions) {
    const auto it = records_.find(completion.resource_id);

    if (it == records_.end() || it->second.generation != completion.generation ||
        it->second.phase != Phase::kPreparing) {
      continue;
    }
    Record& record = it->second;
    record.spatial_info = completion.spatial_info;
    record.collision_geometry = std::move(completion.collision_geometry);
    record.cpu_model_ready = true;
  }
}

void M2ResourceStreamer::DrainCompletions(M2StreamPumpResult& result) {
  std::deque<Completion> completions;
  {
    std::lock_guard lock(mailbox_->mutex);
    completions.swap(mailbox_->completions);
  }
  result.completions = static_cast<std::uint32_t>(std::min(
      completions.size(),
      static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())));

  for (auto& completion : completions) {
    const auto it = records_.find(completion.resource_id);
    if (it == records_.end() ||
        it->second.generation != completion.generation ||
        it->second.phase != Phase::kPreparing) {
      continue;
    }

    Record& record = it->second;
    if (record.worker_task_id != 0u) {
      static_cast<void>(workers_.ForgetTask(record.worker_task_id));
    }
    record.worker_task_id = 0u;
    if (completion.prepared.status != M2ResultStatus::kReady ||
        !completion.prepared.model) {
      MarkFailed(record, completion.prepared.status,
                 completion.prepared.reason,
                 std::move(completion.prepared.detail));
      continue;
    }

    record.spatial_info = completion.prepared.spatial_info;
    record.collision_geometry = completion.prepared.collision_geometry;
    record.cpu_model_ready = true;
    record.texture_dependencies = completion.prepared.texture_dependencies;
    record.next_texture = 0u;
    record.prepared.emplace(std::move(completion.prepared));
    record.phase = record.prepared->textures.empty()
                       ? Phase::kCommitModel
                       : Phase::kCommitTextures;
    record.status = M2ResultStatus::kNotReady;
    QueueForCommit(record);
  }
}

void M2ResourceStreamer::QueueForCommit(Record& record) {
  if (record.queued_for_commit) {
    return;
  }
  record.queued_for_commit = true;
  const auto insertion = std::find_if(
      commit_queue_.begin(), commit_queue_.end(),
      [this, &record](const std::uint32_t queued_id) {
        const auto queued = records_.find(queued_id);
        return queued != records_.end() &&
               queued->second.priority < record.priority;
      });
  commit_queue_.insert(insertion, record.id);
}

void M2ResourceStreamer::MarkFailed(Record& record,
                                    const M2ResultStatus status,
                                    const M2ResultReason reason,
                                    std::string detail) {
  record.phase = Phase::kFailed;
  record.status = status == M2ResultStatus::kReady
                      ? M2ResultStatus::kFailed
                      : status;
  record.reason = reason;
  record.detail = std::move(detail);
  record.prepared.reset();
  record.queued_for_commit = false;
  record.retry_after = Now() + kNegativeCacheDelay;
}

bool M2ResourceStreamer::CommitOne(Record& record,
                                   const M2StreamCommitBudget& budget,
                                   M2StreamPumpResult& result) {
  if (!record.prepared.has_value()) {
    MarkFailed(record, M2ResultStatus::kFailed,
               M2ResultReason::kInvalidHandle,
               "prepared resource package missing");
    return true;
  }

  if (IsRendererDeviceRestarting()) {
    return false;
  }

  if (record.phase == Phase::kCommitTextures) {
    if (record.next_texture >= record.prepared->textures.size()) {
      record.phase = Phase::kCommitModel;
      return true;
    }
    const auto& texture = record.prepared->textures[record.next_texture];
    const std::uint64_t bytes = texture.upload_size;
    const bool texture_budget_exhausted =
        result.textures_committed >= budget.textures;
    const bool byte_budget_exhausted =
        result.textures_committed != 0u &&
        (bytes > budget.upload_bytes ||
         result.upload_bytes > budget.upload_bytes - bytes);
    if (texture_budget_exhausted || byte_budget_exhausted) {
      return false;
    }

    std::string texture_path = texture.path;
    PreparedTextureUpload upload = std::move(
        record.prepared->textures[record.next_texture]);
    bool committed = false;
    try {
      committed = backend_.commit_texture &&
                  backend_.commit_texture(std::move(upload));
    } catch (const std::exception& error) {
      MarkFailed(record, M2ResultStatus::kFailed,
                 M2ResultReason::kMissingTexture, error.what());
      return true;
    } catch (...) {
      MarkFailed(record, M2ResultStatus::kFailed,
                 M2ResultReason::kMissingTexture,
                 "unknown texture commit exception");
      return true;
    }
    if (!committed) {
      MarkFailed(record, M2ResultStatus::kFailed,
                 M2ResultReason::kMissingTexture,
                 std::move(texture_path));
      return true;
    }
    ++record.next_texture;
    ++result.textures_committed;
    result.upload_bytes += bytes;
    if (record.next_texture == record.prepared->textures.size()) {
      record.phase = Phase::kCommitModel;
    }
    return true;
  }

  if (record.phase == Phase::kCommitModel) {
    if (result.models_committed >= budget.models) {
      return false;
    }
    if (!backend_.commit_model) {
      MarkFailed(record, M2ResultStatus::kFailed,
                 M2ResultReason::kInvalidHandle,
                 "model commit backend unavailable");
      return true;
    }
    M2ModelLoadResult commit;
    try {
      commit = backend_.commit_model(std::move(record.prepared->model));
    } catch (const std::exception& error) {
      MarkFailed(record, M2ResultStatus::kFailed,
                 M2ResultReason::kParseFailed, error.what());
      return true;
    } catch (...) {
      MarkFailed(record, M2ResultStatus::kFailed,
                 M2ResultReason::kParseFailed,
                 "unknown model commit exception");
      return true;
    }
    record.prepared.reset();
    ++result.models_committed;
    if (commit.status != M2ResultStatus::kReady || commit.model_id == 0u) {
      MarkFailed(record, commit.status, commit.reason,
                 std::move(commit.detail));
      return true;
    }

    record.phase = Phase::kReady;
    record.status = M2ResultStatus::kReady;
    record.reason = M2ResultReason::kNone;
    record.detail.clear();
    record.model_id = commit.model_id;
    record.queued_for_commit = false;
    if (record.strong_lease_count == 0u) {
      record.recycle_after = Now() + kRetailRecycleDelay;
    }
    return true;
  }

  return true;
}

M2StreamPumpResult M2ResourceStreamer::Pump(
    const M2StreamCommitBudget& budget) {
  M2StreamPumpResult result;

  DrainHeaderBoundsCompletions();
  DrainEarlyCompletions();
  DrainCompletions(result);

  const TimePoint started = Now();
  while (!commit_queue_.empty()) {
    if ((result.textures_committed != 0u || result.models_committed != 0u) &&
        Now() - started >= budget.wall_time) {
      break;
    }

    const std::uint32_t id = commit_queue_.front();
    commit_queue_.pop_front();
    const auto it = records_.find(id);
    if (it == records_.end()) {
      continue;
    }
    Record& record = it->second;
    record.queued_for_commit = false;
    const bool progressed = CommitOne(record, budget, result);
    if ((record.phase == Phase::kCommitTextures ||
         record.phase == Phase::kCommitModel) &&
        record.prepared.has_value()) {
      QueueForCommit(record);
    }
    if (!progressed) {
      break;
    }
  }

  RecycleExpired(result);
  return result;
}

void M2ResourceStreamer::RecycleExpired(M2StreamPumpResult& result) {
  const TimePoint now = Now();

  constexpr std::chrono::milliseconds kRecycleScanInterval{250};
  if (next_recycle_scan_ != TimePoint{} && now < next_recycle_scan_) {
    return;
  }
  next_recycle_scan_ = now + kRecycleScanInterval;
  std::vector<std::uint32_t> remove;
  for (auto& [id, record] : records_) {

    if (record.phase == Phase::kFailed &&
        record.strong_lease_count == 0u &&
        record.retry_after != TimePoint{} && now >= record.retry_after) {
      remove.push_back(id);
      continue;
    }
    if (record.phase != Phase::kReady ||
        record.strong_lease_count != 0u ||
        record.recycle_after == TimePoint{} || now < record.recycle_after ||
        !backend_.recycle_model) {
      continue;
    }
    M2ResultStatus recycled = M2ResultStatus::kNotReady;
    try {
      recycled = backend_.recycle_model(record.model_id);
    } catch (...) {
      record.recycle_after = now + std::chrono::seconds{1};
      continue;
    }
    if (recycled == M2ResultStatus::kReady ||
        recycled == M2ResultStatus::kFailed) {
      remove.push_back(id);
      ++result.models_recycled;
    } else {

      record.recycle_after = now + std::chrono::seconds{1};
    }
  }
  for (const std::uint32_t id : remove) {
    RemoveRecord(id);
  }
}

void M2ResourceStreamer::RemoveRecord(const std::uint32_t resource_id) {
  const auto it = records_.find(resource_id);
  if (it == records_.end()) {
    return;
  }
  if (it->second.strong_lease_count != 0u) {
    return;
  }
  repository_.ForgetUnpublishedIdentity(resource_id);
  records_.erase(it);
}

}

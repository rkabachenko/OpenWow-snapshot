#include "openwow/render/m2/m2_sequence_streamer.h"

#include "openwow/data/model/m2_external_sequence_tracks.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/runtime/scheduling/thread_pool_system.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <limits>
#include <span>
#include <thread>
#include <utility>

namespace openwow::render::m2 {

namespace {

enum class SequenceResidency : std::uint8_t {
  kInline,
  kUnloaded,
  kLoading,
  kResident,
  kFailed,
};

struct SequenceLoadCompletion {
  std::uint32_t model_id = 0;
  std::uint64_t generation = 0;
  std::string path;
  std::shared_ptr<const std::vector<std::uint8_t>> blob;
  data::model::M2LoadResult parsed_model;
  std::string error;
  bool retryable = false;
};

struct CompletionMailbox {
  std::mutex mutex;
  std::deque<SequenceLoadCompletion> completions;

  std::atomic<bool> has_completions{false};
};

struct ModelSequences {
  std::uint64_t generation = 0;
  std::vector<SequenceResidency> residency;
  std::vector<std::string> external_paths;
  std::unordered_map<std::string, std::vector<std::uint16_t>> sequences_by_path;
  std::unordered_map<std::string,
                     std::shared_ptr<const std::vector<std::uint8_t>>>
      resident_blobs;
  std::deque<std::string> load_queue;
  std::string active_load_path;
};

}

struct M2SequenceStreamer::Impl {
  Impl(M2SystemMutex &store_mutex, ModelStore &model_store,
       FileLoader &loader)
      : mutex(store_mutex), models(model_store), file_loader(loader),
        mailbox(std::make_shared<CompletionMailbox>()) {}

  void ScheduleNextLocked(std::uint32_t model_id,
                          const ResumePending &resume_pending) {
    auto state_it = sequences.find(model_id);
    auto model_it = models.find(model_id);
    if (state_it == sequences.end() || model_it == models.end()) {
      return;
    }
    auto &state = state_it->second;
    auto &resource = *model_it->second;
    if (!state.active_load_path.empty() || state.load_queue.empty() ||
        !accept_loads) {
      return;
    }

    std::string path = std::move(state.load_queue.front());
    state.load_queue.pop_front();
    state.active_load_path = path;

    if (!pool_initialized) {
      const std::uint32_t hardware_threads = std::thread::hardware_concurrency();
      const std::uint32_t worker_count = std::clamp(
          hardware_threads == 0u ? 2u : hardware_threads / 2u, 2u, 4u);
      pool.Initialize(worker_count);
      pool_initialized = true;
    }

    const auto loader = file_loader;
    const auto source_bytes = resource.source_model_bytes;
    const auto resident_blobs = state.resident_blobs;
    const std::string model_path = resource.model_path;
    const std::uint64_t generation = state.generation;
    const auto completion_mailbox = mailbox;

    try {
      (void)pool.Submit(
          "M2 sequence " + path, core::TaskPriority::High,
          [loader, source_bytes, resident_blobs, model_path, model_id,
           generation, path, completion_mailbox]() mutable {
            SequenceLoadCompletion completion;
            completion.model_id = model_id;
            completion.generation = generation;
            completion.path = path;
            try {
              if (!loader || !source_bytes) {
                completion.error = "sequence loader unavailable";
              } else {
                auto bytes = loader(path);
                if (bytes.empty()) {
                  completion.error = "file not found";
                  completion.retryable = true;
                } else {
                  completion.blob =
                      std::make_shared<const std::vector<std::uint8_t>>(
                          std::move(bytes));
                  auto available_blobs = resident_blobs;
                  available_blobs.insert_or_assign(path, completion.blob);
                  completion.parsed_model = data::model::LoadM2FromBytes(
                      *source_bytes, model_path,
                      [&available_blobs](const std::string &candidate) {
                        const auto it = available_blobs.find(candidate);
                        return it == available_blobs.end()
                                   ? std::vector<std::uint8_t>{}
                                   : *it->second;
                      });
                  if (!completion.parsed_model.ok) {
                    completion.error = completion.parsed_model.error.empty()
                                           ? "corrupt sequence data"
                                           : completion.parsed_model.error;
                  }
                }
              }
            } catch (const std::exception &error) {
              completion.error = std::string("loader exception: ") + error.what();
              completion.retryable = true;
            } catch (...) {
              completion.error = "loader exception";
              completion.retryable = true;
            }

            std::lock_guard lock(completion_mailbox->mutex);
            completion_mailbox->completions.push_back(std::move(completion));
            completion_mailbox->has_completions.store(true, std::memory_order_release);
          });
    } catch (const std::exception &error) {
      state.active_load_path.clear();
      if (const auto path_it = state.sequences_by_path.find(path);
          path_it != state.sequences_by_path.end()) {
        for (const std::uint16_t sequence_index : path_it->second) {
          state.residency[sequence_index] = SequenceResidency::kUnloaded;
        }
      }
      diagnostics::Log(diagnostics::LogLevel::kWarn,
                       "M2: failed to queue external sequence " + path +
                           ": " + error.what());
      resume_pending(model_id, nullptr);
      ScheduleNextLocked(model_id, resume_pending);
    }
  }

  M2SystemMutex &mutex;
  ModelStore &models;
  FileLoader &file_loader;
  core::ThreadPoolSystem pool;
  bool pool_initialized = false;
  bool accept_loads = true;
  std::shared_ptr<CompletionMailbox> mailbox;
  std::uint64_t next_generation = 1;
  std::unordered_map<std::uint32_t, ModelSequences> sequences;
};

M2SequenceStreamer::M2SequenceStreamer(M2SystemMutex &mutex,
                                       ModelStore &models,
                                       FileLoader &file_loader)
    : impl_(std::make_unique<Impl>(mutex, models, file_loader)) {}

M2SequenceStreamer::~M2SequenceStreamer() { Shutdown(); }

void M2SequenceStreamer::Start() {
  std::lock_guard lock(impl_->mutex);
  impl_->accept_loads = true;
}

void M2SequenceStreamer::Shutdown() {
  bool shutdown_pool = false;
  {
    std::lock_guard lock(impl_->mutex);
    impl_->accept_loads = false;
    impl_->sequences.clear();
    shutdown_pool = impl_->pool_initialized;
    impl_->pool_initialized = false;
  }
  if (shutdown_pool) {
    impl_->pool.Shutdown();
  }
  std::lock_guard mailbox_lock(impl_->mailbox->mutex);
  impl_->mailbox->completions.clear();
  impl_->mailbox->has_completions.store(false, std::memory_order_release);
}

void M2SequenceStreamer::ClearLocked() { impl_->sequences.clear(); }

void M2SequenceStreamer::RegisterModelLocked(
    const std::uint32_t model_id, const detail::M2ModelResource &resource) {
  ModelSequences state;
  state.generation = impl_->next_generation++;
  state.residency.resize(resource.model_data.animation_sequences.size());
  state.external_paths.resize(resource.model_data.animation_sequences.size());
  for (std::size_t index = 0;
       index < resource.model_data.animation_sequences.size(); ++index) {
    const auto &sequence = resource.model_data.animation_sequences[index];
    if (!data::model::M2SequenceUsesExternalData(sequence)) {
      state.residency[index] = SequenceResidency::kInline;
      continue;
    }
    const auto path = data::model::BuildM2SequenceDataPath(
        resource.model_path, resource.model_data.animation_sequences, index);
    if (!path.has_value() || index > std::numeric_limits<std::uint16_t>::max()) {
      state.residency[index] = SequenceResidency::kFailed;
      continue;
    }
    state.residency[index] = SequenceResidency::kUnloaded;
    state.external_paths[index] = *path;
    state.sequences_by_path[*path].push_back(static_cast<std::uint16_t>(index));
  }
  impl_->sequences.insert_or_assign(model_id, std::move(state));
}

void M2SequenceStreamer::RemoveModelLocked(const std::uint32_t model_id) {
  impl_->sequences.erase(model_id);
}

M2ResultStatus M2SequenceStreamer::EnsureResidentLocked(
    const std::uint32_t model_id, const std::uint16_t sequence_index,
    const ResumePending &resume_pending) {
  const auto state_it = impl_->sequences.find(model_id);
  if (state_it == impl_->sequences.end() ||
      sequence_index >= state_it->second.residency.size()) {
    return M2ResultStatus::kFailed;
  }
  auto &state = state_it->second;
  const auto residency = state.residency[sequence_index];
  if (residency == SequenceResidency::kInline ||
      residency == SequenceResidency::kResident) {
    return M2ResultStatus::kReady;
  }
  if (residency == SequenceResidency::kLoading) {
    return M2ResultStatus::kNotReady;
  }
  if (residency == SequenceResidency::kFailed || !impl_->accept_loads ||
      !impl_->file_loader) {
    return M2ResultStatus::kFailed;
  }

  const std::string &path = state.external_paths[sequence_index];
  const auto path_it = state.sequences_by_path.find(path);
  if (path.empty() || path_it == state.sequences_by_path.end()) {
    state.residency[sequence_index] = SequenceResidency::kFailed;
    return M2ResultStatus::kFailed;
  }
  for (const std::uint16_t linked_sequence : path_it->second) {
    if (linked_sequence < state.residency.size() &&
        state.residency[linked_sequence] == SequenceResidency::kUnloaded) {
      state.residency[linked_sequence] = SequenceResidency::kLoading;
    }
  }
  state.load_queue.push_back(path);
  impl_->ScheduleNextLocked(model_id, resume_pending);
  return M2ResultStatus::kNotReady;
}

M2ResultStatus M2SequenceStreamer::PendingResidencyStatusLocked(
    const std::uint32_t model_id,
    const std::uint16_t sequence_index) const {
  const auto state_it = impl_->sequences.find(model_id);
  if (state_it == impl_->sequences.end() ||
      sequence_index >= state_it->second.residency.size()) {
    return M2ResultStatus::kFailed;
  }
  const auto residency = state_it->second.residency[sequence_index];
  if (residency == SequenceResidency::kInline ||
      residency == SequenceResidency::kResident) {
    return M2ResultStatus::kReady;
  }
  return residency == SequenceResidency::kLoading
             ? M2ResultStatus::kNotReady
             : M2ResultStatus::kFailed;
}

bool M2SequenceStreamer::HasPendingCompletions() const noexcept {
  return impl_->mailbox->has_completions.load(std::memory_order_acquire);
}

void M2SequenceStreamer::Pump(const ResumePending &resume_pending) {

  if (!HasPendingCompletions()) {
    return;
  }
  std::deque<SequenceLoadCompletion> completions;
  {
    std::lock_guard lock(impl_->mailbox->mutex);
    completions.swap(impl_->mailbox->completions);
    impl_->mailbox->has_completions.store(false, std::memory_order_release);
  }
  if (completions.empty()) {
    return;
  }

  DeferredCallbacks callbacks;
  std::vector<std::string> warnings;
  {
    std::lock_guard lock(impl_->mutex);
    for (auto &completion : completions) {
      const auto state_it = impl_->sequences.find(completion.model_id);
      const auto model_it = impl_->models.find(completion.model_id);
      if (state_it == impl_->sequences.end() || model_it == impl_->models.end() ||
          state_it->second.generation != completion.generation ||
          state_it->second.active_load_path != completion.path) {
        continue;
      }

      auto &state = state_it->second;
      auto &resource = *model_it->second;
      state.active_load_path.clear();
      const auto path_it = state.sequences_by_path.find(completion.path);
      if (completion.error.empty() && completion.parsed_model.ok &&
          completion.blob) {
        std::string merge_error;
        if (!data::model::AdoptM2ExternalSequenceTracks(
                &resource.model_data, std::move(completion.parsed_model.model),
                path_it == state.sequences_by_path.end()
                    ? std::span<const std::uint16_t>{}
                    : std::span<const std::uint16_t>{path_it->second},
                &merge_error)) {
          completion.error = merge_error.empty()
                                 ? "external sequence track merge failed"
                                 : std::move(merge_error);
        }
      }
      if (completion.error.empty() && completion.parsed_model.ok &&
          completion.blob) {
        state.resident_blobs.insert_or_assign(completion.path,
                                              std::move(completion.blob));
        if (path_it != state.sequences_by_path.end()) {
          for (const std::uint16_t index : path_it->second) {
            state.residency[index] = SequenceResidency::kResident;
          }
        }
      } else {
        if (path_it != state.sequences_by_path.end()) {
          for (const std::uint16_t index : path_it->second) {
            state.residency[index] = completion.retryable
                                         ? SequenceResidency::kUnloaded
                                         : SequenceResidency::kFailed;
          }
        }
        if (!completion.retryable) {
          warnings.push_back("M2: external sequence load failed: " +
                             completion.path + " for model " +
                             resource.model_path + ": " + completion.error);
        }
      }
      resume_pending(completion.model_id, &callbacks);
      impl_->ScheduleNextLocked(completion.model_id, resume_pending);
    }
  }

  for (const auto &warning : warnings) {
    diagnostics::Log(diagnostics::LogLevel::kWarn, warning);
  }
  for (auto &callback : callbacks) {
    callback();
  }
}

}

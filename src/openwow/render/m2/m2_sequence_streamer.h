#pragma once

#include "openwow/render/m2/m2_model_repository.h"
#include "openwow/render/m2/m2_runtime_state.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace openwow::render::m2 {

class M2SequenceStreamer {
public:
  using ModelStore =
      std::unordered_map<std::uint32_t, std::unique_ptr<detail::M2ModelResource>>;
  using FileLoader =
      std::function<std::vector<std::uint8_t>(const std::string &)>;
  using DeferredCallbacks = std::vector<std::function<void()>>;
  using ResumePending =
      std::function<void(std::uint32_t, DeferredCallbacks *)>;

  M2SequenceStreamer(M2SystemMutex &mutex, ModelStore &models,
                     FileLoader &file_loader);
  ~M2SequenceStreamer();

  M2SequenceStreamer(const M2SequenceStreamer &) = delete;
  M2SequenceStreamer &operator=(const M2SequenceStreamer &) = delete;

  void Start();
  void Shutdown();
  void ClearLocked();
  void RegisterModelLocked(std::uint32_t model_id,
                           const detail::M2ModelResource &resource);
  void RemoveModelLocked(std::uint32_t model_id);
  [[nodiscard]] M2ResultStatus EnsureResidentLocked(
      std::uint32_t model_id, std::uint16_t sequence_index,
      const ResumePending &resume_pending);
  [[nodiscard]] M2ResultStatus PendingResidencyStatusLocked(
      std::uint32_t model_id, std::uint16_t sequence_index) const;
  void Pump(const ResumePending &resume_pending);

  [[nodiscard]] bool HasPendingCompletions() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}

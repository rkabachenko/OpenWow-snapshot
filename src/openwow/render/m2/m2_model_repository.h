#pragma once

#include "openwow/foundation/threading/recursive_mutex.h"
#include "openwow/render/m2/m2_runtime_state.h"

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace openwow::render::m2 {

using M2SystemMutex = foundation::RecursiveMutex;

inline constexpr std::size_t kM2ModelBucketFloor = 1024u;

struct M2ModelLoadFlight {
  std::mutex mutex;
  std::condition_variable completion;
  bool complete{false};
  M2ModelLoadResult result;
};

struct M2ModelIdentity {
  bool valid{false};
  bool created{false};
  std::uint32_t model_id{0};
  std::string load_path;
  std::string cache_key;
};

class M2ModelRepository {
 public:
  using ModelMap =
      std::unordered_map<std::uint32_t, std::unique_ptr<detail::M2ModelResource>>;

  M2ModelRepository();

  [[nodiscard]] static M2ModelIdentity Canonicalize(const std::string& path);
  [[nodiscard]] M2ModelIdentity ReserveIdentity(const std::string& path);
  [[nodiscard]] bool IsRenderReady(std::uint32_t model_id) const;
  void ForgetUnpublishedIdentity(std::uint32_t model_id);
  void Clear();
  [[nodiscard]] M2SystemMutex& mutex() noexcept { return mutex_; }
  [[nodiscard]] ModelMap& models() noexcept { return models_; }
  [[nodiscard]] const ModelMap& models() const noexcept { return models_; }
  [[nodiscard]] auto& paths() noexcept { return model_paths_; }
  [[nodiscard]] auto& load_flights() noexcept { return model_load_flights_; }

 private:
  mutable M2SystemMutex mutex_;
  ModelMap models_;
  std::unordered_map<std::string, std::uint32_t> model_paths_;
  std::unordered_map<std::string, std::shared_ptr<M2ModelLoadFlight>>
      model_load_flights_;
  std::uint32_t next_model_id_{1};
};

}

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace openwow::render {

enum class StreamQuality : std::uint8_t {
  Low    = 0,
  Medium = 1,
  High   = 2,
};

enum class TextureStreamState : std::uint8_t {
  Unloaded    = 0,
  LowLoaded   = 1,
  MidLoaded   = 2,
  FullLoaded  = 3,
};

struct DecodedMip {
  std::uint32_t width{0};
  std::uint32_t height{0};
  std::uint32_t mip_level{0};
  std::vector<std::uint8_t> rgba_data;
  std::uint64_t vram_bytes{0};
};

struct StreamedTexture {
  std::string path;
  TextureStreamState state{TextureStreamState::Unloaded};

  std::uint64_t gpu_handle{0};

  std::vector<DecodedMip> mips;

  std::uint32_t loaded_mip_level{UINT32_MAX};

  std::uint64_t current_vram{0};

  std::chrono::steady_clock::time_point last_access;

  float distance{1e9f};

  bool visible{false};
};

struct UploadRequest {
  std::string path;
  DecodedMip mip;
};

class VramBudget {
 public:
  static constexpr std::uint64_t kDefaultBudgetMB = 512;

  explicit VramBudget(std::uint64_t budget_mb = kDefaultBudgetMB);

  void SetBudgetMB(std::uint64_t mb);
  [[nodiscard]] std::uint64_t GetBudgetBytes() const { return budget_bytes_; }
  [[nodiscard]] std::uint64_t GetBudgetMB() const {
    return budget_bytes_ / (1024 * 1024);
  }

  void Allocate(std::uint64_t bytes);
  void Free(std::uint64_t bytes);
  [[nodiscard]] std::uint64_t GetUsed() const { return used_bytes_; }
  [[nodiscard]] std::uint64_t GetAvailable() const;
  [[nodiscard]] bool IsOverBudget() const { return used_bytes_ > budget_bytes_; }
  [[nodiscard]] float UsageRatio() const;
  void Reset();

 private:
  std::uint64_t budget_bytes_;
  std::uint64_t used_bytes_{0};
};

struct LoadRequest {
  std::string path;
  std::uint32_t target_mip_level{0};
  float priority{0.0f};

  bool operator<(const LoadRequest& other) const {
    return priority < other.priority;
  }
};

class TextureStreamer {
 public:
  TextureStreamer();
  ~TextureStreamer();

  TextureStreamer(const TextureStreamer&) = delete;
  TextureStreamer& operator=(const TextureStreamer&) = delete;

  void SetQuality(StreamQuality quality);
  [[nodiscard]] StreamQuality GetQuality() const { return quality_; }

  void SetMaxVRAMMB(std::uint64_t mb);
  [[nodiscard]] std::uint64_t GetMaxVRAMMB() const {
    return vram_budget_.GetBudgetMB();
  }

  void SetFileLoader(
      std::function<std::vector<std::uint8_t>(const std::string&)> loader);

  using GpuUploadCallback =
      std::function<std::uint64_t(const std::string& path, const DecodedMip& mip)>;
  void SetGpuUploadCallback(GpuUploadCallback cb);

  using GpuDestroyCallback = std::function<void(std::uint64_t handle)>;
  void SetGpuDestroyCallback(GpuDestroyCallback cb);

  void Start();

  [[nodiscard]] bool ShutdownWorker();
  void Stop();
  [[nodiscard]] bool IsRunning() const { return running_.load(); }

  void RequestTexture(const std::string& path, float distance = 0.0f,
                      bool visible = true);

  void UpdateDistance(const std::string& path, float distance, bool visible);

  void ReleaseTexture(const std::string& path);

  [[nodiscard]] std::uint64_t GetHandle(const std::string& path) const;

  [[nodiscard]] TextureStreamState GetState(const std::string& path) const;

  std::uint32_t ProcessUploads(std::uint32_t max_per_frame = 8);

  [[nodiscard]] std::uint64_t GetVramUsed() const {
    return vram_budget_.GetUsed();
  }
  [[nodiscard]] std::uint64_t GetVramBudget() const {
    return vram_budget_.GetBudgetBytes();
  }
  [[nodiscard]] float GetVramUsageRatio() const {
    return vram_budget_.UsageRatio();
  }
  [[nodiscard]] bool IsOverBudget() const {
    return vram_budget_.IsOverBudget();
  }

  [[nodiscard]] std::size_t GetTextureCount() const;
  [[nodiscard]] std::size_t GetPendingCount() const;

  [[nodiscard]] static std::uint32_t QualityToMinMip(StreamQuality q);

  [[nodiscard]] static float ComputePriority(float distance, bool visible,
                                              TextureStreamState current_state);

  std::uint32_t EvictLRU();

 private:
  void WorkerLoop();
  void EnqueueLoad(const std::string& path, std::uint32_t target_mip);

  mutable std::mutex mutex_;
  std::unordered_map<std::string, StreamedTexture> textures_;
  std::priority_queue<LoadRequest> load_queue_;

  std::thread worker_thread_;
  std::atomic<bool> running_{false};
  std::condition_variable work_cv_;
  std::mutex work_mutex_;

  mutable std::mutex upload_mutex_;
  std::vector<UploadRequest> pending_uploads_;

  StreamQuality quality_{StreamQuality::High};
  VramBudget vram_budget_;

  std::function<std::vector<std::uint8_t>(const std::string&)> file_loader_;
  GpuUploadCallback gpu_upload_cb_;
  GpuDestroyCallback gpu_destroy_cb_;
};

}

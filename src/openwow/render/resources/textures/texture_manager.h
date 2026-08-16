#pragma once

#include <bgfx/bgfx.h>

#include "openwow/render/api/renderer_context.h"
#include "openwow/render/resources/textures/texture_lease.h"
#include "openwow/render/resources/textures/texture_mip_upload.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

namespace openwow::data {
class TextureCacheRowStore;
struct TextureCacheRowIdentity;
}

namespace openwow::core {
class ThreadPoolSystem;
}

namespace openwow::game {
struct TabardEmblemRenderTargetDescriptor;
}

#if defined(__clang__)
#define OPENWOW_TEXTURE_GUARDED_BY(mutex) __attribute__((guarded_by(mutex)))
#define OPENWOW_TEXTURE_REQUIRES(mutex) \
  __attribute__((requires_capability(mutex)))
#else
#define OPENWOW_TEXTURE_GUARDED_BY(mutex)
#define OPENWOW_TEXTURE_REQUIRES(mutex)
#endif

namespace openwow::render {

struct PreparedTextureUpload {
  std::string path;
  std::uint32_t row_hash{0};
  std::string row_path;
  std::uint64_t row_generation{0};

  std::vector<std::uint8_t> rgba_bytes;
  BlpUploadFormat upload_format{BlpUploadFormat::kRgba8};
  std::uint32_t width{0};
  std::uint32_t height{0};
  std::uint32_t upload_size{0};
  std::uint8_t mip_count{1};
  bool complete_mip_chain{false};
  bool is_cube{false};
  bool is_opaque{false};
  bool valid{false};
  std::string error;

  std::string tabard_render_target_name;
  std::shared_ptr<const std::vector<std::uint8_t>>
      tabard_fallback_update;
};

struct TextureManagerStreamingStats {
  std::size_t pending{0};
  std::size_t prepared{0};
  std::size_t failed{0};
};

class TextureManager final : public api::RendererDeviceLifecycleObserver {
 public:
  static constexpr std::size_t kMaxAsyncRequests = 256u;

  TextureManager();
  ~TextureManager();

  void BindRendererContext(api::RendererContext* renderer_context);

  bool Initialize();

  void SetFileLoader(
      std::function<std::vector<uint8_t>(const std::string&)> loader);

  [[nodiscard]] TextureLease AcquireTexture(const std::string& path);
  [[nodiscard]] TextureLease AcquireTextureStrict(const std::string& path);

  [[nodiscard]] TextureLease AcquireCachedTexture(const std::string& path);

  [[nodiscard]] TextureLease AcquireCachedTextureStrict(
      const std::string& path);

  [[nodiscard]] TextureLease AcquireCachedTextureStrictWithDimensions(
      const std::string& path, std::uint32_t& out_width,
      std::uint32_t& out_height);

  [[nodiscard]] TextureLease AcquireTextureAsync(
      const std::string& path,
      TextureLoadFailurePolicy failure_policy =
          TextureLoadFailurePolicy::kStrict,
      TextureLoadPriority priority = TextureLoadPriority::kDemand);

  [[nodiscard]] bool QueueTextureLoad(
      const std::string& path,
      TextureLoadFailurePolicy failure_policy =
          TextureLoadFailurePolicy::kStrict,
      TextureLoadPriority priority = TextureLoadPriority::kDemand);

  std::size_t PumpPreparedUploads(std::size_t max_uploads = 8u);
  [[nodiscard]] TextureManagerStreamingStats StreamingStats() const;

  [[nodiscard]] static PreparedTextureUpload PrepareTextureUpload(
      const std::string& path,
      const std::vector<std::uint8_t>& source_bytes);

  [[nodiscard]] static PreparedTextureUpload PrepareTextureUploadFromLoader(
      const std::string& path,
      const std::function<std::vector<std::uint8_t>(const std::string&)>& loader);

  bgfx::TextureHandle CommitPreparedTexture(const PreparedTextureUpload& upload);

  [[nodiscard]] TextureLease AcquireTabardEmblemRenderTargetAsync(
      const openwow::game::TabardEmblemRenderTargetDescriptor& descriptor,
      TextureLoadPriority priority = TextureLoadPriority::kDemand);

  [[nodiscard]] bool HasResidentTexture(const std::string& path) const;

  [[nodiscard]] std::uint64_t EvictionGeneration() const noexcept {
    return eviction_generation_.load(std::memory_order_relaxed);
  }

  [[nodiscard]] std::pair<std::uint32_t, std::uint32_t> GetTextureDimensions(
      const std::string& path) const;

  bool IsOpaque(const std::string& path) const;

  bgfx::TextureHandle GetWhiteTexture() const noexcept {
    return white_tex_.load(std::memory_order_acquire);
  }
  bgfx::TextureHandle GetBlackTexture() const noexcept {
    return black_tex_.load(std::memory_order_acquire);
  }
  bgfx::TextureHandle GetCheckerTexture() const noexcept {
    return checker_tex_.load(std::memory_order_acquire);
  }

  void Shutdown();

  void ClearCache();

  void SetMemoryBudget(std::uint64_t bytes);
  [[nodiscard]] std::uint64_t GetMemoryBudget() const;

  [[nodiscard]] std::uint64_t GetMemoryUsage() const;

  void EvictToBudget();

  [[nodiscard]] std::size_t CachedCount() const;

 private:
  TextureManager(const TextureManager&) = delete;
  TextureManager& operator=(const TextureManager&) = delete;
  void OnRendererDeviceWillReset() override;
  void OnRendererDeviceReady(api::DeviceGeneration generation) override;

  static bgfx::TextureHandle MakeSolid1x1(uint8_t r, uint8_t g, uint8_t b,
                                           uint8_t a);

  static bgfx::TextureHandle MakeChecker8x8();

  static std::uint64_t EstimateTextureBytes(std::uint32_t width, std::uint32_t height,
                                            bool has_mips);

  bgfx::TextureHandle LoadTextureInternal(const std::string& path,
                                          bool allow_failure_placeholder);

  using RecencyList = std::list<std::uint32_t>;
  using RecencyPosition = RecencyList::iterator;

  struct CachedTexture {
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;

    std::shared_ptr<TextureAllocation> allocation;
    uint32_t width{0};
    uint32_t height{0};
    bool is_opaque{false};
    std::uint64_t size_bytes{0};

    RecencyPosition lru_position{};
    bool is_placeholder{false};

    bool staged_for_publish{false};
  };

  using CacheIterator =
      std::unordered_map<std::uint32_t, CachedTexture>::iterator;

  [[nodiscard]] TextureLease AcquireTextureInternal(const std::string& path,
                                                     bool allow_failure_placeholder);

  [[nodiscard]] bgfx::TextureHandle StoreFailedTextureLocked(
      std::uint32_t row_hash) OPENWOW_TEXTURE_REQUIRES(cache_mutex_);
  [[nodiscard]] TextureLease LeaseCacheEntryLocked(
      std::uint32_t row_hash, bool allow_failure_placeholder = true)
      OPENWOW_TEXTURE_REQUIRES(cache_mutex_);
  [[nodiscard]] TextureLease LeaseCacheEntryWithDimensionsLocked(
      std::uint32_t row_hash, bool allow_failure_placeholder,
      std::uint32_t& out_width, std::uint32_t& out_height)
      OPENWOW_TEXTURE_REQUIRES(cache_mutex_);
  [[nodiscard]] bool QueueTextureLoadInternal(
      const openwow::data::TextureCacheRowIdentity& row,
      std::string request_path,
      TextureLoadFailurePolicy failure_policy,
      TextureLoadPriority priority,
      const openwow::game::TabardEmblemRenderTargetDescriptor*
          tabard_descriptor = nullptr);
  void EnsureAsyncWorkersLocked() OPENWOW_TEXTURE_REQUIRES(cache_mutex_);
  void ResetAsyncPreparation(bool restart);
  [[nodiscard]] bool StoreCacheEntryLocked(
      std::uint32_t row_hash, CachedTexture texture) noexcept
      OPENWOW_TEXTURE_REQUIRES(cache_mutex_);
  void RetireCacheEntryLocked(CacheIterator it) noexcept
      OPENWOW_TEXTURE_REQUIRES(cache_mutex_);
  void RetireAllocationLocked(
      std::shared_ptr<TextureAllocation> allocation) noexcept
      OPENWOW_TEXTURE_REQUIRES(cache_mutex_);

  void ReclaimRetiredAllocations();
  void TouchCacheEntryLocked(CachedTexture& texture) noexcept
      OPENWOW_TEXTURE_REQUIRES(cache_mutex_);
  void EvictToBudgetLocked(const std::uint32_t* preserve_row_hash)
      OPENWOW_TEXTURE_REQUIRES(cache_mutex_);
  void ClearCacheLocked(bool force) OPENWOW_TEXTURE_REQUIRES(cache_mutex_);

  mutable std::mutex cache_mutex_;
  std::unordered_map<std::uint32_t, CachedTexture> cache_
      OPENWOW_TEXTURE_GUARDED_BY(cache_mutex_);
  RecencyList lru_ OPENWOW_TEXTURE_GUARDED_BY(cache_mutex_);

  std::atomic<std::uint64_t> eviction_generation_{0};

  std::vector<std::shared_ptr<TextureAllocation>> retired_allocations_
      OPENWOW_TEXTURE_GUARDED_BY(cache_mutex_);
  std::atomic<bgfx::TextureHandle> white_tex_{
      bgfx::TextureHandle{bgfx::kInvalidHandle}};
  std::atomic<bgfx::TextureHandle> black_tex_{
      bgfx::TextureHandle{bgfx::kInvalidHandle}};
  std::atomic<bgfx::TextureHandle> checker_tex_{
      bgfx::TextureHandle{bgfx::kInvalidHandle}};

  api::RendererContext* renderer_context_{nullptr};
  bool renderer_observer_registered_{false};
  bool restore_after_device_reset_{false};
  bool initialized_{false};
  std::unordered_map<
      std::string,
      std::shared_ptr<const std::vector<std::uint8_t>>>
      tabard_emblem_fallback_pixels_ OPENWOW_TEXTURE_GUARDED_BY(cache_mutex_);
  std::unordered_map<std::string, std::uint32_t>
      tabard_render_target_rows_ OPENWOW_TEXTURE_GUARDED_BY(cache_mutex_);
  std::function<std::vector<uint8_t>(const std::string&)> file_loader_
      OPENWOW_TEXTURE_GUARDED_BY(cache_mutex_);

  const std::shared_ptr<openwow::data::TextureCacheRowStore> source_rows_;
  struct AsyncState;
  std::shared_ptr<AsyncState> async_state_
      OPENWOW_TEXTURE_GUARDED_BY(cache_mutex_);
  std::unique_ptr<openwow::core::ThreadPoolSystem> async_workers_
      OPENWOW_TEXTURE_GUARDED_BY(cache_mutex_);
  std::array<TextureLease, kMaxAsyncRequests> async_handoff_leases_
      OPENWOW_TEXTURE_GUARDED_BY(cache_mutex_){};
  std::size_t async_handoff_count_ OPENWOW_TEXTURE_GUARDED_BY(cache_mutex_){0};

  static constexpr std::uint64_t kDefaultCacheMemoryBudgetBytes =
      256ull * 1024ull * 1024ull;

  std::uint64_t memory_budget_ OPENWOW_TEXTURE_GUARDED_BY(cache_mutex_){
      kDefaultCacheMemoryBudgetBytes};
  std::uint64_t memory_usage_ OPENWOW_TEXTURE_GUARDED_BY(cache_mutex_){0};
};

static_assert(std::atomic<bgfx::TextureHandle>::is_always_lock_free,
              "placeholder texture handles must be lock-free atomics");

}

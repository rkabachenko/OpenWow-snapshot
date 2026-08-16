#pragma once

#include "openwow/vfs/virtual_file_system.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace openwow::core {
class ThreadPoolSystem;
}

namespace openwow::data {
class TextureCacheRowStore;
}

namespace openwow::render {

struct GlueTexture {
  bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
  int width{0};
  int height{0};
  std::uint64_t size_bytes{0};

  std::uint64_t last_frame_used{0};
};

enum class GlueTextureAlphaMode {
  kPremultiplied,
  kStraight,
};

using GlueTextureSourceLoader =
    std::function<std::optional<std::vector<std::uint8_t>>(
        const std::string&)>;

struct GlueTextureStreamConfig {
  const openwow::vfs::VirtualFileSystem* vfs{nullptr};
  GlueTextureSourceLoader source_loader;
  std::uint32_t async_worker_count{2};
};

struct GlueTextureStreamingStats {
  std::size_t pending{0};
  std::size_t prepared{0};
  std::size_t failed{0};
};

class GlueTextureStream {
 public:
  static constexpr std::size_t kMaxAsyncRequests = 256u;

  static constexpr std::size_t kDemandAsyncRequestReserve = 64u;
  static constexpr std::size_t kMaxPrefetchAsyncRequests =
      kMaxAsyncRequests - kDemandAsyncRequestReserve;

  explicit GlueTextureStream(const openwow::vfs::VirtualFileSystem* vfs);
  explicit GlueTextureStream(GlueTextureStreamConfig config);
  ~GlueTextureStream();

  GlueTextureStream(const GlueTextureStream&) = delete;
  GlueTextureStream& operator=(const GlueTextureStream&) = delete;

  void Shutdown();

  void BeginFrame();
  void SetMemoryBudget(std::uint64_t bytes);
  [[nodiscard]] std::uint64_t GetMemoryBudget() const noexcept { return memory_budget_; }
  [[nodiscard]] std::uint64_t GetMemoryUsage() const noexcept { return memory_usage_; }
  [[nodiscard]] std::size_t CachedTextureCount() const noexcept {
    return textures_.size() + dynamic_textures_.size();
  }
  std::optional<GlueTexture> Load(
      const std::string& virtual_path,
      GlueTextureAlphaMode alpha_mode = GlueTextureAlphaMode::kPremultiplied);

  std::optional<GlueTexture> LoadAsync(
      const std::string& virtual_path,
      GlueTextureAlphaMode alpha_mode = GlueTextureAlphaMode::kPremultiplied);

  void QueueAsyncLoad(
      const std::string& virtual_path,
      GlueTextureAlphaMode alpha_mode = GlueTextureAlphaMode::kPremultiplied);

  [[nodiscard]] std::optional<std::pair<int, int>> PeekTextureDimensions(
      const std::string& virtual_path,
      GlueTextureAlphaMode alpha_mode = GlueTextureAlphaMode::kPremultiplied);

  std::size_t PumpPreparedUploads(std::size_t max_uploads = 8);

  [[nodiscard]] GlueTextureStreamingStats StreamingStats() const;

  std::optional<GlueTexture> UploadRgba(
      const std::string& key,
      const std::uint8_t* rgba,
      int width,
      int height,
      std::uint32_t version,
      std::uint64_t flags = BGFX_TEXTURE_NONE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);

  bgfx::TextureHandle UploadDynamic(const std::string& key,
                                     const uint8_t* rgba,
                                     int width, int height,
                                     uint32_t version);

 private:
  struct PreparedTexture;
  struct AsyncState;

  struct ResolvedTexturePath {
    std::uint32_t row_hash{0};
    std::string row_path;
    std::uint64_t row_generation{0};
    std::string cache_key;
  };

  enum class RecencyOwner : std::uint8_t { kStatic, kDynamic };
  struct RecencyNode {
    RecencyOwner owner{RecencyOwner::kStatic};
    std::string key;
  };
  using RecencyList = std::list<RecencyNode>;
  using RecencyPosition = RecencyList::iterator;

  struct CachedGlueTexture {
    GlueTexture texture;

    RecencyPosition lru_position{};
  };

  struct DynamicEntry {
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    int width{0};
    int height{0};
    uint32_t version{0};
    std::uint64_t size_bytes{0};
    std::uint64_t last_frame_used{0};
    RecencyPosition lru_position{};
  };

  using StaticIterator =
      std::unordered_map<std::string, CachedGlueTexture>::iterator;
  using DynamicIterator =
      std::unordered_map<std::string, DynamicEntry>::iterator;

  [[nodiscard]] bool StoreTextureEntry(
      const std::string& key, GlueTexture entry) noexcept;
  void DestroyTextureEntry(StaticIterator it);
  void DestroyDynamicEntry(DynamicIterator it);
  void Touch(CachedGlueTexture& entry);
  void Touch(DynamicEntry& entry);
  [[nodiscard]] std::optional<GlueTexture> FindCachedTexture(
      const std::string& cache_key);
  [[nodiscard]] std::optional<ResolvedTexturePath> ResolveTexturePath(
      const std::string& virtual_path,
      GlueTextureAlphaMode alpha_mode);
  [[nodiscard]] static PreparedTexture PrepareTexture(
      std::string path,
      std::string cache_key,
      GlueTextureAlphaMode alpha_mode,
      const std::vector<std::uint8_t>& source_bytes);
  [[nodiscard]] std::optional<GlueTexture> CommitPreparedTexture(
      PreparedTexture prepared);
  void QueueAsyncLoadInternal(ResolvedTexturePath request,
                              GlueTextureAlphaMode alpha_mode,
                              bool demand);
  void EnsureAsyncWorkers();
  void EvictToBudget();
  void EvictToBudgetPreserving(const std::string* preserve_static_key,
                               const std::string* preserve_dynamic_key);

  const openwow::vfs::VirtualFileSystem* vfs_{nullptr};
  GlueTextureSourceLoader source_loader_;
  std::uint32_t async_worker_count_{2};
  std::shared_ptr<AsyncState> async_state_;
  std::unique_ptr<openwow::core::ThreadPoolSystem> async_workers_;

  std::shared_ptr<openwow::data::TextureCacheRowStore> source_rows_;
  std::unordered_map<std::string, CachedGlueTexture> textures_;
  std::unordered_map<std::string, DynamicEntry> dynamic_textures_;

  RecencyList lru_;
  std::uint64_t memory_budget_{0};
  std::uint64_t memory_usage_{0};
  std::uint64_t frame_serial_{0};
  bool frame_tracking_active_{false};
};

}

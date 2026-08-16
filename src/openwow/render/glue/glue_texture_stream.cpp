#include "openwow/render/glue/glue_texture_stream.h"

#include "openwow/runtime/scheduling/thread_pool_system.h"
#include "openwow/data/blp/blp_texture_loader.h"
#include "openwow/data/image/image_decoder.h"
#include "openwow/data/texture_cache.h"
#include "openwow/render/resources/textures/texture_cache_budget.h"
#include "openwow/render/resources/textures/texture_mip_upload.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace openwow::render {

namespace {

std::string MakeCacheKey(const std::uint32_t row_hash,
                         const GlueTextureAlphaMode alpha_mode) {
  return "retail-row:" + std::to_string(row_hash) +
         ((alpha_mode == GlueTextureAlphaMode::kStraight) ? "#straight"
                                                           : "#premul");
}

bool TextureTraceEnabled() {
  const char* value = std::getenv("OPENWOW_TEXTURE_TRACE");
  return value != nullptr && value[0] != '\0' && std::string(value) != "0";
}

void TraceTexture(const std::string& message) {
  if (TextureTraceEnabled()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "TextureCache: " + message);
  }
}

std::uint64_t EstimateTextureBytes(const int width, const int height, const bool has_mips) {
  if (width <= 0 || height <= 0) {
    return 0;
  }
  std::uint64_t bytes = static_cast<std::uint64_t>(width) *
                        static_cast<std::uint64_t>(height) * 4ull;
  if (has_mips) {
    bytes += bytes / 3ull;
  }
  return bytes;
}

void PremultiplyAlphaRgba(std::vector<std::uint8_t>* pixels_rgba) {
  if (pixels_rgba == nullptr || pixels_rgba->empty()) return;
  auto& px = *pixels_rgba;
  const std::size_t count = px.size() / 4u;
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t off = i * 4u;
    const std::uint32_t a = px[off + 3];
    if (a == 255u) continue;
    if (a == 0u) {
      px[off + 0] = 0;
      px[off + 1] = 0;
      px[off + 2] = 0;
      continue;
    }
    const std::uint32_t r = px[off + 0];
    const std::uint32_t g = px[off + 1];
    const std::uint32_t b = px[off + 2];
    px[off + 0] = static_cast<std::uint8_t>((r * a + 127u) / 255u);
    px[off + 1] = static_cast<std::uint8_t>((g * a + 127u) / 255u);
    px[off + 2] = static_cast<std::uint8_t>((b * a + 127u) / 255u);
  }
}

}

struct GlueTextureStream::PreparedTexture {
  std::uint32_t row_hash{0};
  std::uint64_t row_generation{0};
  std::string path;
  std::string cache_key;

  std::vector<std::uint8_t> rgba;
  BlpUploadFormat upload_format{BlpUploadFormat::kRgba8};
  int width{0};
  int height{0};
  std::uint32_t upload_size{0};
  bool complete_mip_chain{false};
  bool valid{false};
  std::string error;
};

struct GlueTextureStream::AsyncState {
  struct PendingRequest {
    std::uint32_t task_id{0};
    bool demand{false};
  };

  static_assert(std::is_nothrow_move_constructible_v<PreparedTexture>,
                "bounded texture handoff requires no-throw package moves");

  [[nodiscard]] bool PushPrepared(PreparedTexture prepared) noexcept {
    if (prepared_count == prepared_slots.size()) {
      return false;
    }
    const std::size_t tail =
        (prepared_head + prepared_count) % prepared_slots.size();
    prepared_slots[tail].emplace(std::move(prepared));
    ++prepared_count;
    return true;
  }

  [[nodiscard]] PreparedTexture PopPrepared() noexcept {
    auto& slot = prepared_slots[prepared_head];
    PreparedTexture prepared = std::move(*slot);
    slot.reset();
    prepared_head = (prepared_head + 1u) % prepared_slots.size();
    --prepared_count;
    return prepared;
  }

  mutable std::mutex mutex;
  std::unordered_map<std::string, PendingRequest> pending;
  std::array<std::optional<PreparedTexture>,
             GlueTextureStream::kMaxAsyncRequests>
      prepared_slots;
  std::size_t prepared_head{0u};
  std::size_t prepared_count{0u};
  std::uint64_t generation{1};
  bool accepting{true};
};

GlueTextureStream::GlueTextureStream(const openwow::vfs::VirtualFileSystem* vfs)
    : GlueTextureStream(GlueTextureStreamConfig{.vfs = vfs}) {}

GlueTextureStream::GlueTextureStream(GlueTextureStreamConfig config)
    : vfs_(config.vfs),
      source_loader_(std::move(config.source_loader)),
      async_worker_count_(std::max(1u, config.async_worker_count)),
      async_state_(std::make_shared<AsyncState>()),
      source_rows_(std::make_shared<openwow::data::TextureCacheRowStore>()),
      memory_budget_(256ull * 1024ull * 1024ull) {
  if (!source_loader_ && vfs_ != nullptr) {
    const auto* source_vfs = vfs_;
    source_loader_ = [source_vfs](const std::string& path) {
      return source_vfs->ReadFileBytes(path);
    };
  }
}

GlueTextureStream::~GlueTextureStream() {
  Shutdown();
}

void GlueTextureStream::Shutdown() {
  if (async_state_ != nullptr) {
    {
      std::lock_guard lock(async_state_->mutex);
      async_state_->accepting = false;
      ++async_state_->generation;
    }
    if (async_workers_ != nullptr) {
      async_workers_->Shutdown();
      async_workers_.reset();
    }
  }

  try {
    async_state_ = std::make_shared<AsyncState>();
  } catch (...) {

    async_state_.reset();
  }

  for (auto& [key, entry] : textures_) {
    (void)key;
    if (bgfx::isValid(entry.texture.handle)) {
      bgfx::destroy(entry.texture.handle);
    }
  }
  textures_.clear();
  if (source_rows_ != nullptr) {
    source_rows_->Clear();
  }

  for (auto& [key, entry] : dynamic_textures_) {
    (void)key;
    if (bgfx::isValid(entry.handle)) {
      bgfx::destroy(entry.handle);
    }
  }
  dynamic_textures_.clear();

  lru_.clear();
  memory_usage_ = 0;
  frame_serial_ = 0;
  frame_tracking_active_ = false;
}

void GlueTextureStream::BeginFrame() {

  RefreshBlockCompressionSupport();

  if (frame_tracking_active_) {
    EvictToBudget();
  }

  frame_tracking_active_ = true;
  if (frame_serial_ == std::numeric_limits<std::uint64_t>::max()) {
    frame_serial_ = 1;
    for (auto& [key, entry] : textures_) {
      (void)key;
      entry.texture.last_frame_used = 0;
    }
    for (auto& [key, entry] : dynamic_textures_) {
      (void)key;
      entry.last_frame_used = 0;
    }
  } else {
    ++frame_serial_;
  }
}

void GlueTextureStream::SetMemoryBudget(const std::uint64_t bytes) {
  memory_budget_ = bytes;
  if (!frame_tracking_active_) {
    EvictToBudget();
  }
}

std::optional<GlueTextureStream::ResolvedTexturePath>
GlueTextureStream::ResolveTexturePath(
    const std::string& virtual_path,
    const GlueTextureAlphaMode alpha_mode) {
  if (virtual_path.empty() || source_rows_ == nullptr) {
    return std::nullopt;
  }

  const auto row = source_rows_->Resolve(virtual_path);

  return ResolvedTexturePath{
      .row_hash = row.hash,
      .row_path = row.path,
      .row_generation = row.generation,
      .cache_key = MakeCacheKey(row.hash, alpha_mode),
  };
}

std::optional<GlueTexture> GlueTextureStream::Load(
    const std::string& virtual_path,
    const GlueTextureAlphaMode alpha_mode) {
  if (!source_loader_) {
    TraceTexture("load skipped empty path or missing source loader: " + virtual_path);
    return std::nullopt;
  }
  const auto request = ResolveTexturePath(virtual_path, alpha_mode);
  if (!request.has_value()) {
    TraceTexture("load skipped empty path or missing source loader: " + virtual_path);
    return std::nullopt;
  }

  if (auto cached = FindCachedTexture(request->cache_key); cached.has_value()) {
    return cached;
  }

  const auto source = source_rows_->Load(
      {.hash = request->row_hash,
       .path = request->row_path,
       .generation = request->row_generation},
      [loader = source_loader_](const std::string& path) {
        auto bytes = loader(path);
        return bytes.has_value() ? std::move(*bytes)
                                 : std::vector<std::uint8_t>{};
      });
  if (!source) {
    TraceTexture("source read failed: " + request->row_path);
    return std::nullopt;
  }

  auto prepared = PrepareTexture(request->row_path, request->cache_key,
                                 alpha_mode, *source.bytes);
  prepared.row_hash = request->row_hash;
  prepared.row_generation = request->row_generation;
  if (!prepared.valid) {
    source_rows_->MarkTerminalFailure(
        {.hash = request->row_hash,
         .path = request->row_path,
         .generation = request->row_generation});
    TraceTexture("decode failed: " + request->row_path +
                 " error=" + prepared.error);
    return std::nullopt;
  }
  auto committed = CommitPreparedTexture(std::move(prepared));
  if (!committed.has_value()) {
    source_rows_->MarkTerminalFailure(
        {.hash = request->row_hash,
         .path = request->row_path,
         .generation = request->row_generation});
  }
  return committed;
}

std::optional<GlueTexture> GlueTextureStream::LoadAsync(
    const std::string& virtual_path,
    const GlueTextureAlphaMode alpha_mode) {
  if (!source_loader_ || virtual_path.empty() || async_state_ == nullptr) {
    return std::nullopt;
  }
  const std::uint32_t row_hash =
      openwow::data::HashTextureCachePath(virtual_path);
  const std::string cache_key = MakeCacheKey(row_hash, alpha_mode);
  if (auto cached = FindCachedTexture(cache_key); cached.has_value()) {
    return cached;
  }
  if (source_rows_->IsTerminalFailure(row_hash)) return std::nullopt;
  {
    std::lock_guard lock(async_state_->mutex);
    if (!async_state_->accepting ||
        (!async_state_->pending.contains(cache_key) &&
         async_state_->pending.size() >= kMaxAsyncRequests)) {
      return std::nullopt;
    }
  }

  auto request = ResolveTexturePath(virtual_path, alpha_mode);
  if (!request.has_value()) return std::nullopt;
  QueueAsyncLoadInternal(std::move(*request), alpha_mode, true);
  return std::nullopt;
}

void GlueTextureStream::QueueAsyncLoad(
    const std::string& virtual_path,
    const GlueTextureAlphaMode alpha_mode) {
  if (!source_loader_ || virtual_path.empty() || async_state_ == nullptr) {
    return;
  }
  const std::uint32_t row_hash =
      openwow::data::HashTextureCachePath(virtual_path);
  const std::string cache_key = MakeCacheKey(row_hash, alpha_mode);
  if (textures_.contains(cache_key) ||
      source_rows_->IsTerminalFailure(row_hash)) {
    return;
  }
  {
    std::lock_guard lock(async_state_->mutex);
    static_assert(kDemandAsyncRequestReserve < kMaxAsyncRequests);
    if (!async_state_->accepting ||
        (!async_state_->pending.contains(cache_key) &&
         async_state_->pending.size() >= kMaxPrefetchAsyncRequests)) {
      return;
    }
  }

  auto request = ResolveTexturePath(virtual_path, alpha_mode);
  if (!request.has_value()) return;
  QueueAsyncLoadInternal(std::move(*request), alpha_mode, false);
}

std::optional<std::pair<int, int>> GlueTextureStream::PeekTextureDimensions(
    const std::string& virtual_path,
    const GlueTextureAlphaMode alpha_mode) {
  if (virtual_path.empty()) return std::nullopt;
  const std::uint32_t row_hash =
      openwow::data::HashTextureCachePath(virtual_path);
  const std::string cache_key = MakeCacheKey(row_hash, alpha_mode);
  if (const auto it = textures_.find(cache_key); it != textures_.end()) {
    return std::pair<int, int>{it->second.texture.width,
                               it->second.texture.height};
  }
  QueueAsyncLoad(virtual_path, alpha_mode);
  return std::nullopt;
}

void GlueTextureStream::QueueAsyncLoadInternal(
    ResolvedTexturePath request,
    const GlueTextureAlphaMode alpha_mode,
    const bool demand) {
  if (!source_loader_ || async_state_ == nullptr) return;

  const std::string& path = request.row_path;
  const std::string& cache_key = request.cache_key;
  const openwow::data::TextureCacheRowIdentity row_identity{
      .hash = request.row_hash,
      .path = request.row_path,
      .generation = request.row_generation,
  };
  if (textures_.contains(cache_key)) return;
  if (!source_rows_->IsCurrent(row_identity) ||
      source_rows_->IsTerminalFailure(request.row_hash)) {
    return;
  }

  const auto state = async_state_;
  std::uint32_t prefetch_task_to_promote = 0;
  {
    std::lock_guard lock(state->mutex);
    if (!state->accepting) return;
    if (const auto pending = state->pending.find(cache_key);
        pending != state->pending.end()) {
      if (!demand || pending->second.demand || pending->second.task_id == 0) return;
      prefetch_task_to_promote = pending->second.task_id;
    }
  }

  if (prefetch_task_to_promote != 0) {
    if (async_workers_ == nullptr ||
        !async_workers_->CancelTask(prefetch_task_to_promote)) {

      return;
    }
    static_cast<void>(async_workers_->ForgetTask(prefetch_task_to_promote));
    std::lock_guard lock(state->mutex);
    const auto pending = state->pending.find(cache_key);
    if (pending == state->pending.end() ||
        pending->second.task_id != prefetch_task_to_promote || pending->second.demand) {
      return;
    }
    state->pending.erase(pending);
  }

  std::uint64_t generation = 0;
  {
    std::lock_guard lock(state->mutex);
    if (!state->accepting) {
      return;
    }
    if (state->pending.contains(cache_key)) {
      return;
    }
    if (!source_rows_->IsCurrent(row_identity) ||
        source_rows_->IsTerminalFailure(request.row_hash) ||
        state->pending.size() >= kMaxAsyncRequests) {
      return;
    }
    try {
      state->pending.emplace(
          cache_key, AsyncState::PendingRequest{.demand = demand});
    } catch (...) {
      return;
    }
    generation = state->generation;
  }

  try {
    EnsureAsyncWorkers();
    const auto loader = source_loader_;
    const auto rows = source_rows_;
    const std::uint32_t task_id = async_workers_->Submit(
        "ui-texture:" + path,
        demand ? openwow::core::TaskPriority::High : openwow::core::TaskPriority::Low,
        [state, rows, loader, row_identity, cache_key, alpha_mode,
         generation]() mutable {
          PreparedTexture prepared;
          try {
            const auto source = rows->Load(
                row_identity,
                [loader](const std::string& stored_path) {
                  auto bytes = loader(stored_path);
                  return bytes.has_value() ? std::move(*bytes)
                                           : std::vector<std::uint8_t>{};
                });
            if (source) {
              prepared = PrepareTexture(row_identity.path, cache_key, alpha_mode,
                                        *source.bytes);
              prepared.row_hash = row_identity.hash;
              prepared.row_generation = row_identity.generation;
              if (!prepared.valid) {
                rows->MarkTerminalFailure(row_identity);
              }
            } else {
              prepared.row_hash = row_identity.hash;
              prepared.path = row_identity.path;
              prepared.cache_key = cache_key;
              prepared.error = "source read failed";
            }
          } catch (const std::exception& exception) {
            rows->MarkTerminalFailure(row_identity);
            try {
              prepared.error = exception.what();
            } catch (...) {
            }
          } catch (...) {
            rows->MarkTerminalFailure(row_identity);
            try {
              prepared.error = "unknown source/decode exception";
            } catch (...) {
            }
          }

          prepared.row_hash = row_identity.hash;
          prepared.row_generation = row_identity.generation;
          if (prepared.path.empty()) {
            prepared.path = std::move(row_identity.path);
          }
          if (prepared.cache_key.empty()) {
            prepared.cache_key = std::move(cache_key);
          }

          std::lock_guard lock(state->mutex);
          if (state->accepting && state->generation == generation) {
            static_cast<void>(state->PushPrepared(std::move(prepared)));
          }
        });
    {
      std::lock_guard lock(state->mutex);
      if (state->accepting && state->generation == generation) {
        if (auto pending = state->pending.find(cache_key);
            pending != state->pending.end() && pending->second.task_id == 0 &&
            pending->second.demand == demand) {
          pending->second.task_id = task_id;
        }
      }
    }
    TraceTexture(std::string("async queued ") + (demand ? "demand: " : "prefetch: ") + path);
  } catch (const std::exception& exception) {
    std::lock_guard lock(state->mutex);
    state->pending.erase(cache_key);
    TraceTexture("async queue failed: " + path + " error=" + exception.what());
  } catch (...) {
    std::lock_guard lock(state->mutex);
    state->pending.erase(cache_key);
    TraceTexture("async queue failed: " + path + " error=unknown exception");
  }
}

std::size_t GlueTextureStream::PumpPreparedUploads(const std::size_t max_uploads) {
  if (max_uploads == 0 || async_state_ == nullptr) return 0;

  std::size_t committed = 0;
  for (std::size_t index = 0u; index < max_uploads; ++index) {
    std::optional<PreparedTexture> ready;
    {
      std::lock_guard lock(async_state_->mutex);
      if (async_state_->prepared_count == 0u) {
        break;
      }
      ready.emplace(async_state_->PopPrepared());
    }
    auto& prepared = *ready;
    std::uint32_t completed_task_id = 0u;
    {
      std::lock_guard lock(async_state_->mutex);
      if (const auto pending =
              async_state_->pending.find(prepared.cache_key);
          pending != async_state_->pending.end()) {
        completed_task_id = pending->second.task_id;
        async_state_->pending.erase(pending);
      }
    }
    if (completed_task_id != 0u && async_workers_ != nullptr) {
      static_cast<void>(async_workers_->ForgetTask(completed_task_id));
    }

    if (!prepared.valid) {
      source_rows_->MarkTerminalFailure(
          {.hash = prepared.row_hash,
           .path = std::move(prepared.path),
           .generation = prepared.row_generation});
      TraceTexture("async preparation failed: " + prepared.error);
      continue;
    }

    const bool uploaded = CommitPreparedTexture(std::move(prepared)).has_value();
    if (uploaded) {
      ++committed;
    }
  }
  return committed;
}

GlueTextureStreamingStats GlueTextureStream::StreamingStats() const {
  if (async_state_ == nullptr) return {};
  std::size_t pending = 0u;
  std::size_t prepared = 0u;
  {
    std::lock_guard lock(async_state_->mutex);
    pending = async_state_->pending.size();
    prepared = async_state_->prepared_count;
  }
  return GlueTextureStreamingStats{
      .pending = pending,
      .prepared = prepared,
      .failed = source_rows_ != nullptr
                    ? source_rows_->TerminalFailureCount()
                    : 0u,
  };
}

std::optional<GlueTexture> GlueTextureStream::FindCachedTexture(
    const std::string& cache_key) {
  const auto it = textures_.find(cache_key);
  if (it == textures_.end()) return std::nullopt;

  Touch(it->second);

  if (TextureTraceEnabled()) {
    TraceTexture("hit: " + cache_key + " handle="
                 + std::to_string(it->second.texture.handle.idx)
                 + " usage=" + std::to_string(memory_usage_));
  }
  return it->second.texture;
}

GlueTextureStream::PreparedTexture GlueTextureStream::PrepareTexture(
    std::string path,
    std::string cache_key,
    const GlueTextureAlphaMode alpha_mode,
    const std::vector<std::uint8_t>& source_bytes) {
  PreparedTexture prepared{
      .path = std::move(path),
      .cache_key = std::move(cache_key),
  };
  if (source_bytes.empty()) {
    prepared.error = "empty texture source";
    return prepared;
  }

  if (alpha_mode == GlueTextureAlphaMode::kStraight &&
      openwow::data::BLPTextureLoader::IsValidBLP(source_bytes)) {
    const auto blp = openwow::data::BLPTextureLoader::Load(source_bytes);
    if (blp.isValid && blp.header.width > 0 && blp.header.height > 0) {

      auto mip_upload = openwow::render::BuildBlpRgbaMipUpload(
          blp, openwow::render::CurrentBlockCompressionSupport());
      if (mip_upload.decoded_mip_count > 0 && !mip_upload.bytes.empty() &&
          !mip_upload.mip_sizes.empty() && mip_upload.width > 0 && mip_upload.height > 0 &&
          mip_upload.width <= std::numeric_limits<std::uint16_t>::max() &&
          mip_upload.height <= std::numeric_limits<std::uint16_t>::max()) {
        const std::uint64_t upload_size = mip_upload.complete_mip_chain
                                              ? mip_upload.bytes.size()
                                              : mip_upload.mip_sizes.front();
        if (upload_size > 0 && upload_size <= mip_upload.bytes.size() &&
            upload_size <= std::numeric_limits<std::uint32_t>::max()) {
          prepared.width = static_cast<int>(mip_upload.width);
          prepared.height = static_cast<int>(mip_upload.height);
          prepared.upload_size = static_cast<std::uint32_t>(upload_size);
          prepared.complete_mip_chain = mip_upload.complete_mip_chain;
          prepared.rgba = std::move(mip_upload.bytes);
          prepared.upload_format = mip_upload.format;
          prepared.valid = true;
          return prepared;
        }
      }
    }

  }

  auto decoded = openwow::data::image::DecodeImage(source_bytes);
  if (!decoded.ok || decoded.width <= 0 || decoded.height <= 0 ||
      decoded.width > std::numeric_limits<std::uint16_t>::max() ||
      decoded.height > std::numeric_limits<std::uint16_t>::max() ||
      decoded.pixels_rgba.empty() ||
      decoded.pixels_rgba.size() > std::numeric_limits<std::uint32_t>::max()) {
    prepared.error = decoded.error.empty() ? "invalid decoded image" : std::move(decoded.error);
    return prepared;
  }

  if (alpha_mode == GlueTextureAlphaMode::kPremultiplied) {
    PremultiplyAlphaRgba(&decoded.pixels_rgba);
  }
  prepared.width = decoded.width;
  prepared.height = decoded.height;
  prepared.upload_size = static_cast<std::uint32_t>(decoded.pixels_rgba.size());
  prepared.rgba = std::move(decoded.pixels_rgba);
  prepared.valid = true;
  return prepared;
}

std::optional<GlueTexture> GlueTextureStream::CommitPreparedTexture(
    PreparedTexture prepared) {
  if (!prepared.valid || prepared.width <= 0 || prepared.height <= 0 ||
      prepared.upload_size == 0 || prepared.upload_size > prepared.rgba.size()) {
    return std::nullopt;
  }
  const openwow::data::TextureCacheRowIdentity identity{
      .hash = prepared.row_hash,
      .path = std::move(prepared.path),
      .generation = prepared.row_generation,
  };
  if (identity.hash == 0u || identity.path.empty() ||
      !source_rows_->IsCurrent(identity) ||
      source_rows_->IsTerminalFailure(identity.hash)) {
    return std::nullopt;
  }
  if (auto cached = FindCachedTexture(prepared.cache_key); cached.has_value()) return cached;

  const auto bgfx_storage_size = [&](const bool has_mips) {
    bgfx::TextureInfo info{};
    bgfx::calcTextureSize(info, static_cast<std::uint16_t>(prepared.width),
                          static_cast<std::uint16_t>(prepared.height), 1u,
                          false, has_mips, 1u,
                          ToBgfxTextureFormat(prepared.upload_format));
    return static_cast<std::uint64_t>(info.storageSize);
  };

  const bool created_with_mips =
      prepared.complete_mip_chain &&
      prepared.upload_size >= bgfx_storage_size(true);
  if (prepared.upload_size < bgfx_storage_size(false)) {
    source_rows_->MarkTerminalFailure(identity);
    TraceTexture("upload shorter than its own base level: " + identity.path);
    return std::nullopt;
  }

  const bgfx::Memory* memory = bgfx::copy(prepared.rgba.data(), prepared.upload_size);
  const bgfx::TextureHandle handle = bgfx::createTexture2D(
      static_cast<std::uint16_t>(prepared.width),
      static_cast<std::uint16_t>(prepared.height),
      created_with_mips,
      1,
      ToBgfxTextureFormat(prepared.upload_format),
      BGFX_TEXTURE_NONE,
      memory);
  if (!bgfx::isValid(handle)) {
    source_rows_->MarkTerminalFailure(identity);
    TraceTexture("bgfx upload failed: " + identity.path + " size="
                 + std::to_string(prepared.width) + "x" + std::to_string(prepared.height));
    return std::nullopt;
  }

  GlueTexture entry{
      .handle = handle,
      .width = prepared.width,
      .height = prepared.height,

      .size_bytes = EstimateTextureBytes(prepared.width, prepared.height,
                                         created_with_mips) *
                    BlpUploadFormatBytesPerBlock(prepared.upload_format) /
                    BlpUploadFormatBytesPerBlock(BlpUploadFormat::kRgba8),
  };
  if (!StoreTextureEntry(prepared.cache_key, entry)) {
    source_rows_->MarkTerminalFailure(identity);
    return std::nullopt;
  }
  TraceTexture("load ok: " + identity.path + " size=" + std::to_string(prepared.width) + "x"
               + std::to_string(prepared.height)
               + (created_with_mips ? " mips=complete" : " mips=base"));
  return entry;
}

void GlueTextureStream::EnsureAsyncWorkers() {
  if (async_workers_ != nullptr) return;
  auto workers = std::make_unique<openwow::core::ThreadPoolSystem>();
  workers->Initialize(async_worker_count_);
  async_workers_ = std::move(workers);
}

std::optional<GlueTexture> GlueTextureStream::UploadRgba(
    const std::string& key,
    const std::uint8_t* rgba,
    int width,
    int height,
    std::uint32_t version,
    std::uint64_t flags) {
  if (key.empty() || rgba == nullptr || width <= 0 || height <= 0 ||
      width > std::numeric_limits<std::uint16_t>::max() ||
      height > std::numeric_limits<std::uint16_t>::max()) {
    return std::nullopt;
  }

  const std::size_t pixel_count =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if (pixel_count > std::numeric_limits<std::uint32_t>::max() / 4u) {
    return std::nullopt;
  }

  const auto [dynamic_it, created] = dynamic_textures_.try_emplace(key);
  auto& entry = dynamic_it->second;
  if (created) {

    try {
      entry.lru_position =
          lru_.insert(lru_.end(), RecencyNode{RecencyOwner::kDynamic, key});
    } catch (...) {
      dynamic_textures_.erase(dynamic_it);
      return std::nullopt;
    }
  }

  if (entry.version == version && bgfx::isValid(entry.handle)) {
    Touch(entry);
    return GlueTexture{
        .handle = entry.handle,
        .width = entry.width,
        .height = entry.height,
        .size_bytes = entry.size_bytes,
        .last_frame_used = entry.last_frame_used,
    };
  }

  if (bgfx::isValid(entry.handle)) {
    bgfx::destroy(entry.handle);
    entry.handle = BGFX_INVALID_HANDLE;
  }
  if (memory_usage_ >= entry.size_bytes) {
    memory_usage_ -= entry.size_bytes;
  } else {
    memory_usage_ = 0;
  }
  entry.size_bytes = 0;

  const auto data_size = static_cast<std::uint32_t>(pixel_count * 4u);
  const bgfx::Memory* mem = bgfx::copy(rgba, data_size);

  entry.handle = bgfx::createTexture2D(
      static_cast<uint16_t>(width),
      static_cast<uint16_t>(height),
      false, 1, bgfx::TextureFormat::RGBA8,
      flags,
      mem);
  if (!bgfx::isValid(entry.handle)) {

    lru_.erase(entry.lru_position);
    dynamic_textures_.erase(dynamic_it);
    return std::nullopt;
  }

  entry.width = width;
  entry.height = height;
  entry.version = version;
  entry.size_bytes = data_size;
  Touch(entry);
  memory_usage_ += entry.size_bytes;
  if (!frame_tracking_active_) {
    EvictToBudgetPreserving(nullptr, &key);
  }

  return GlueTexture{
      .handle = entry.handle,
      .width = entry.width,
      .height = entry.height,
      .size_bytes = entry.size_bytes,
      .last_frame_used = entry.last_frame_used,
  };
}

bgfx::TextureHandle GlueTextureStream::UploadDynamic(
    const std::string& key, const uint8_t* rgba,
    int width, int height, uint32_t version) {
  if (const auto entry = UploadRgba(key, rgba, width, height, version)) {
    return entry->handle;
  }
  return BGFX_INVALID_HANDLE;
}

bool GlueTextureStream::StoreTextureEntry(
    const std::string& key, GlueTexture entry) noexcept {
  struct TextureHandleOwner {
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    bool owns{true};
    ~TextureHandleOwner() {
      if (owns && bgfx::isValid(handle)) {
        bgfx::destroy(handle);
      }
    }
  } handle_owner{entry.handle};

  RecencyPosition inserted_position = lru_.end();
  try {
    if (frame_tracking_active_) {
      entry.last_frame_used = frame_serial_;
    }
    const std::uint64_t size_bytes = entry.size_bytes;
    if (auto it = textures_.find(key); it != textures_.end()) {
      DestroyTextureEntry(it);
      textures_.erase(it);
    }

    inserted_position =
        lru_.insert(lru_.end(), RecencyNode{RecencyOwner::kStatic, key});
    textures_.emplace(key, CachedGlueTexture{entry, inserted_position});

    inserted_position = lru_.end();
    handle_owner.owns = false;
    memory_usage_ += size_bytes;
  } catch (...) {
    if (inserted_position != lru_.end()) {
      lru_.erase(inserted_position);
    }
    return false;
  }
  if (!frame_tracking_active_) {
    try {
      EvictToBudgetPreserving(&key, nullptr);
    } catch (...) {

    }
  }
  return true;
}

void GlueTextureStream::DestroyTextureEntry(const StaticIterator it) {
  if (it == textures_.end()) {
    return;
  }

  if (it->second.lru_position != lru_.end()) {
    lru_.erase(it->second.lru_position);
    it->second.lru_position = lru_.end();
  }
  if (memory_usage_ >= it->second.texture.size_bytes) {
    memory_usage_ -= it->second.texture.size_bytes;
  } else {
    memory_usage_ = 0;
  }
  if (bgfx::isValid(it->second.texture.handle)) {
    bgfx::destroy(it->second.texture.handle);
  }
}

void GlueTextureStream::DestroyDynamicEntry(const DynamicIterator it) {
  if (it == dynamic_textures_.end()) {
    return;
  }

  if (it->second.lru_position != lru_.end()) {
    lru_.erase(it->second.lru_position);
    it->second.lru_position = lru_.end();
  }
  if (memory_usage_ >= it->second.size_bytes) {
    memory_usage_ -= it->second.size_bytes;
  } else {
    memory_usage_ = 0;
  }
  if (bgfx::isValid(it->second.handle)) {
    bgfx::destroy(it->second.handle);
  }
}

void GlueTextureStream::Touch(CachedGlueTexture& entry) {

  lru_.splice(lru_.end(), lru_, entry.lru_position);
  if (frame_tracking_active_) {
    entry.texture.last_frame_used = frame_serial_;
  }
}

void GlueTextureStream::Touch(DynamicEntry& entry) {
  lru_.splice(lru_.end(), lru_, entry.lru_position);
  if (frame_tracking_active_) {
    entry.last_frame_used = frame_serial_;
  }
}

void GlueTextureStream::EvictToBudget() {
  EvictToBudgetPreserving(nullptr, nullptr);
}

void GlueTextureStream::EvictToBudgetPreserving(const std::string* preserve_static_key,
                                               const std::string* preserve_dynamic_key) {

  if (memory_budget_ == 0u || memory_usage_ <= memory_budget_) {
    return;
  }

  const std::size_t eviction_quota =
      TextureCacheEvictionQuota(memory_usage_, memory_budget_);

  const std::size_t visit_limit =
      std::min(kTextureCacheMaxEntriesVisitedPerSweep, lru_.size());

  std::size_t evicted = 0u;
  std::size_t visited = 0u;
  for (auto node = lru_.begin();
       node != lru_.end() && evicted < eviction_quota &&
       visited < visit_limit && memory_usage_ > memory_budget_;
       ++visited) {

    const auto next = std::next(node);
    const RecencyOwner owner = node->owner;
    const std::string& key = node->key;

    const bool preserved =
        owner == RecencyOwner::kStatic
            ? (preserve_static_key != nullptr && key == *preserve_static_key)
            : (preserve_dynamic_key != nullptr && key == *preserve_dynamic_key);

    if (owner == RecencyOwner::kStatic) {

      const auto it = textures_.find(key);
      if (preserved || (frame_tracking_active_ &&
                        it->second.texture.last_frame_used >= frame_serial_)) {

        Touch(it->second);
        node = next;
        continue;
      }
      if (TextureTraceEnabled()) {
        TraceTexture("evict static: " + key + " handle="
                     + std::to_string(it->second.texture.handle.idx)
                     + " usage=" + std::to_string(memory_usage_)
                     + " budget=" + std::to_string(memory_budget_));
      }
      DestroyTextureEntry(it);
      textures_.erase(it);
    } else {
      const auto it = dynamic_textures_.find(key);
      if (preserved || (frame_tracking_active_ &&
                        it->second.last_frame_used >= frame_serial_)) {
        Touch(it->second);
        node = next;
        continue;
      }
      if (TextureTraceEnabled()) {
        TraceTexture("evict dynamic: " + key + " handle="
                     + std::to_string(it->second.handle.idx)
                     + " usage=" + std::to_string(memory_usage_)
                     + " budget=" + std::to_string(memory_budget_));
      }
      DestroyDynamicEntry(it);
      dynamic_textures_.erase(it);
    }

    ++evicted;
    node = next;
  }
}

}

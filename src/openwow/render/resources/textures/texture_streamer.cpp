
#include "openwow/render/resources/textures/texture_streamer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace openwow::render {

VramBudget::VramBudget(std::uint64_t budget_mb)
    : budget_bytes_(budget_mb * 1024ULL * 1024ULL) {}

void VramBudget::SetBudgetMB(std::uint64_t mb) {
  budget_bytes_ = mb * 1024ULL * 1024ULL;
}

void VramBudget::Allocate(std::uint64_t bytes) {
  used_bytes_ += bytes;
}

void VramBudget::Free(std::uint64_t bytes) {
  if (bytes > used_bytes_) {
    used_bytes_ = 0;
  } else {
    used_bytes_ -= bytes;
  }
}

std::uint64_t VramBudget::GetAvailable() const {
  if (used_bytes_ >= budget_bytes_) return 0;
  return budget_bytes_ - used_bytes_;
}

float VramBudget::UsageRatio() const {
  if (budget_bytes_ == 0) return 1.0f;
  return static_cast<float>(static_cast<double>(used_bytes_) /
                            static_cast<double>(budget_bytes_));
}

void VramBudget::Reset() {
  used_bytes_ = 0;
}

std::uint32_t TextureStreamer::QualityToMinMip(StreamQuality q) {
  switch (q) {
    case StreamQuality::Low:    return 4;
    case StreamQuality::Medium: return 2;
    case StreamQuality::High:   return 0;
  }
  return 0;
}

float TextureStreamer::ComputePriority(float distance, bool visible,
                                       TextureStreamState current_state) {

  float priority = visible ? 1000.0f : 100.0f;

  if (distance > 0.001f) {
    priority += 500.0f / distance;
  } else {
    priority += 500.0f;
  }

  if (current_state == TextureStreamState::Unloaded) {
    priority += 2000.0f;
  } else if (current_state == TextureStreamState::LowLoaded) {
    priority += 500.0f;
  }

  return priority;
}

TextureStreamer::TextureStreamer() = default;

TextureStreamer::~TextureStreamer() {
  Stop();
}

void TextureStreamer::SetQuality(StreamQuality quality) {
  std::lock_guard lock(mutex_);
  quality_ = quality;
}

void TextureStreamer::SetMaxVRAMMB(std::uint64_t mb) {
  std::lock_guard lock(mutex_);
  vram_budget_.SetBudgetMB(mb);
}

void TextureStreamer::SetFileLoader(
    std::function<std::vector<std::uint8_t>(const std::string&)> loader) {
  std::lock_guard lock(mutex_);
  file_loader_ = std::move(loader);
}

void TextureStreamer::SetGpuUploadCallback(GpuUploadCallback cb) {
  std::lock_guard lock(mutex_);
  gpu_upload_cb_ = std::move(cb);
}

void TextureStreamer::SetGpuDestroyCallback(GpuDestroyCallback cb) {
  std::lock_guard lock(mutex_);
  gpu_destroy_cb_ = std::move(cb);
}

void TextureStreamer::Start() {
  if (worker_thread_.joinable() || running_.load()) return;
  running_.store(true);
  try {
    worker_thread_ = std::thread(&TextureStreamer::WorkerLoop, this);
  } catch (...) {
    running_.store(false);
    throw;
  }
}

bool TextureStreamer::ShutdownWorker() {
  const bool had_worker = worker_thread_.joinable();
  running_.store(false);

  if (had_worker) {
    work_cv_.notify_all();
    worker_thread_.join();
  }

  return had_worker;
}

void TextureStreamer::Stop() {
  (void)ShutdownWorker();
}

void TextureStreamer::RequestTexture(const std::string& path, float distance,
                                     bool visible) {
  std::lock_guard lock(mutex_);
  auto it = textures_.find(path);
  if (it == textures_.end()) {
    StreamedTexture tex;
    tex.path = path;
    tex.distance = distance;
    tex.visible = visible;
    tex.last_access = std::chrono::steady_clock::now();
    textures_[path] = std::move(tex);
    it = textures_.find(path);
  } else {
    it->second.distance = distance;
    it->second.visible = visible;
    it->second.last_access = std::chrono::steady_clock::now();
  }

  std::uint32_t target_mip = QualityToMinMip(quality_);

  if (it->second.loaded_mip_level <= target_mip) {
    return;
  }

  float priority = ComputePriority(distance, visible, it->second.state);
  {
    std::lock_guard wlock(work_mutex_);
    load_queue_.push({path, target_mip, priority});
  }
  work_cv_.notify_one();
}

void TextureStreamer::UpdateDistance(const std::string& path, float distance,
                                    bool visible) {
  std::lock_guard lock(mutex_);
  auto it = textures_.find(path);
  if (it == textures_.end()) return;
  it->second.distance = distance;
  it->second.visible = visible;
  it->second.last_access = std::chrono::steady_clock::now();
}

void TextureStreamer::ReleaseTexture(const std::string& path) {
  std::lock_guard lock(mutex_);
  auto it = textures_.find(path);
  if (it == textures_.end()) return;

  if (it->second.current_vram > 0) {
    vram_budget_.Free(it->second.current_vram);
  }

  if (it->second.gpu_handle != 0 && gpu_destroy_cb_) {
    gpu_destroy_cb_(it->second.gpu_handle);
  }

  textures_.erase(it);
}

std::uint64_t TextureStreamer::GetHandle(const std::string& path) const {
  std::lock_guard lock(mutex_);
  auto it = textures_.find(path);
  if (it == textures_.end()) return 0;
  return it->second.gpu_handle;
}

TextureStreamState TextureStreamer::GetState(const std::string& path) const {
  std::lock_guard lock(mutex_);
  auto it = textures_.find(path);
  if (it == textures_.end()) return TextureStreamState::Unloaded;
  return it->second.state;
}

std::uint32_t TextureStreamer::ProcessUploads(std::uint32_t max_per_frame) {
  std::vector<UploadRequest> uploads;
  {
    std::lock_guard lock(upload_mutex_);
    if (pending_uploads_.empty()) return 0;
    std::uint32_t count =
        std::min(max_per_frame, static_cast<std::uint32_t>(pending_uploads_.size()));
    uploads.assign(pending_uploads_.begin(),
                   pending_uploads_.begin() + static_cast<std::ptrdiff_t>(count));
    pending_uploads_.erase(pending_uploads_.begin(),
                           pending_uploads_.begin() + static_cast<std::ptrdiff_t>(count));
  }

  std::uint32_t uploaded = 0;
  for (auto& req : uploads) {
    std::lock_guard lock(mutex_);
    auto it = textures_.find(req.path);
    if (it == textures_.end()) continue;

    std::uint64_t handle = 0;
    if (gpu_upload_cb_) {
      handle = gpu_upload_cb_(req.path, req.mip);
    }

    if (it->second.gpu_handle != 0 && it->second.gpu_handle != handle &&
        gpu_destroy_cb_) {
      gpu_destroy_cb_(it->second.gpu_handle);
    }

    vram_budget_.Free(it->second.current_vram);
    vram_budget_.Allocate(req.mip.vram_bytes);

    it->second.gpu_handle = handle;
    it->second.current_vram = req.mip.vram_bytes;
    it->second.loaded_mip_level = req.mip.mip_level;
    it->second.last_access = std::chrono::steady_clock::now();

    if (req.mip.mip_level == 0) {
      it->second.state = TextureStreamState::FullLoaded;
    } else if (req.mip.mip_level <= 2) {
      it->second.state = TextureStreamState::MidLoaded;
    } else {
      it->second.state = TextureStreamState::LowLoaded;
    }

    ++uploaded;
  }

  if (vram_budget_.IsOverBudget()) {
    EvictLRU();
  }

  return uploaded;
}

std::size_t TextureStreamer::GetTextureCount() const {
  std::lock_guard lock(mutex_);
  return textures_.size();
}

std::size_t TextureStreamer::GetPendingCount() const {
  std::lock_guard lock(upload_mutex_);
  return pending_uploads_.size();
}

std::uint32_t TextureStreamer::EvictLRU() {

  std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>>
      candidates;

  {
    std::lock_guard lock(mutex_);
    candidates.reserve(textures_.size());
    for (const auto& [path, tex] : textures_) {
      if (tex.current_vram > 0) {
        candidates.emplace_back(path, tex.last_access);
      }
    }
  }

  std::sort(candidates.begin(), candidates.end(),
            [](const auto& a, const auto& b) {
              return a.second < b.second;
            });

  std::uint32_t evicted = 0;
  for (const auto& [path, _] : candidates) {
    if (!vram_budget_.IsOverBudget()) break;

    std::lock_guard lock(mutex_);
    auto it = textures_.find(path);
    if (it == textures_.end()) continue;

    vram_budget_.Free(it->second.current_vram);
    if (it->second.gpu_handle != 0 && gpu_destroy_cb_) {
      gpu_destroy_cb_(it->second.gpu_handle);
    }

    it->second.gpu_handle = 0;
    it->second.current_vram = 0;
    it->second.loaded_mip_level = UINT32_MAX;
    it->second.state = TextureStreamState::Unloaded;
    ++evicted;
  }

  return evicted;
}

void TextureStreamer::WorkerLoop() {
  while (running_.load()) {
    LoadRequest request;
    {
      std::unique_lock lock(work_mutex_);
      work_cv_.wait_for(lock, std::chrono::milliseconds(50), [this] {
        return !load_queue_.empty() || !running_.load();
      });

      if (!running_.load()) break;
      if (load_queue_.empty()) continue;

      request = load_queue_.top();
      load_queue_.pop();
    }

    {
      std::lock_guard lock(mutex_);
      auto it = textures_.find(request.path);
      if (it == textures_.end()) continue;
      if (it->second.loaded_mip_level <= request.target_mip_level) continue;
    }

    std::vector<std::uint8_t> file_data;
    {
      std::lock_guard lock(mutex_);
      if (file_loader_) {
        file_data = file_loader_(request.path);
      }
    }
    if (file_data.empty()) continue;

    std::uint32_t base_width = 256;
    std::uint32_t base_height = 256;
    if (file_data.size() >= 20) {
      std::memcpy(&base_width, file_data.data() + 12, 4);
      std::memcpy(&base_height, file_data.data() + 16, 4);
    }

    std::uint32_t mip = request.target_mip_level;
    std::uint32_t w = std::max(1u, base_width >> mip);
    std::uint32_t h = std::max(1u, base_height >> mip);

    DecodedMip decoded_mip;
    decoded_mip.width = w;
    decoded_mip.height = h;
    decoded_mip.mip_level = mip;
    decoded_mip.vram_bytes = static_cast<std::uint64_t>(w) * h * 4;
    decoded_mip.rgba_data.resize(decoded_mip.vram_bytes, 128);

    if (file_data.size() >= 148 && file_data.size() > 4 &&
        file_data[0] == 'B' && file_data[1] == 'L' &&
        file_data[2] == 'P' && file_data[3] == '2') {

      std::uint32_t mip_offset = 0;
      std::uint32_t mip_size = 0;
      if (mip < 16) {
        std::memcpy(&mip_offset, file_data.data() + 20 + mip * 4, 4);
        std::memcpy(&mip_size, file_data.data() + 84 + mip * 4, 4);
      }

      if (mip_offset > 0 && mip_size > 0 &&
          static_cast<std::uint64_t>(mip_offset) + mip_size <=
              file_data.size()) {

        decoded_mip.vram_bytes = static_cast<std::uint64_t>(w) * h * 4;
        decoded_mip.rgba_data.assign(decoded_mip.vram_bytes, 200);
      }
    }

    {
      std::lock_guard lock(upload_mutex_);
      pending_uploads_.push_back({request.path, std::move(decoded_mip)});
    }
  }
}

void TextureStreamer::EnqueueLoad(const std::string& path,
                                  std::uint32_t target_mip) {
  float distance = 0.0f;
  bool visible = true;
  TextureStreamState state = TextureStreamState::Unloaded;
  {
    std::lock_guard lock(mutex_);
    auto it = textures_.find(path);
    if (it != textures_.end()) {
      distance = it->second.distance;
      visible = it->second.visible;
      state = it->second.state;
    }
  }

  float priority = ComputePriority(distance, visible, state);
  {
    std::lock_guard wlock(work_mutex_);
    load_queue_.push({path, target_mip, priority});
  }
  work_cv_.notify_one();
}

}

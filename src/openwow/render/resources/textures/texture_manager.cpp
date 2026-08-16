#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/render/backend/bgfx/bgfx_texture_lease.h"

#include "openwow/runtime/scheduling/thread_pool_system.h"
#include "openwow/data/blp/blp_decoder.h"
#include "openwow/data/blp/blp_mip.h"
#include "openwow/data/blp/blp_texture_loader.h"
#include "openwow/data/image/image_decoder.h"
#include "openwow/data/texture_cache.h"
#include "openwow/game/tabard_renderer.h"
#include "openwow/render/models/characters/portrait_icon_texture.h"
#include "openwow/render/resources/textures/texture_cache_budget.h"
#include "openwow/render/resources/textures/texture_mip_upload.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <iterator>
#include <limits>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

#if defined(__clang__)
#pragma clang diagnostic error "-Wthread-safety"
#endif

namespace openwow::render {

struct TextureAllocation {
  TextureAllocation(const bgfx::TextureHandle texture_handle,
                    const bool owns_texture_handle,
                    api::RendererContext* context,
                    const api::DeviceGeneration generation) noexcept
      : handle(texture_handle),
        context(context),
        generation(generation),
        owns_handle(owns_texture_handle) {}
  TextureAllocation(const TextureAllocation&) = delete;
  TextureAllocation& operator=(const TextureAllocation&) = delete;

  std::atomic<bgfx::TextureHandle> handle;

  api::RendererContext* context{};
  api::DeviceGeneration generation{};
  bool owns_handle{true};
  void Release() noexcept {
    const bgfx::TextureHandle released =
        handle.exchange(bgfx::TextureHandle{bgfx::kInvalidHandle},
                        std::memory_order_acq_rel);
    if (owns_handle && bgfx::isValid(released) && context != nullptr &&
        context->Status() == api::RendererStatus::Ready &&
        context->Generation() == generation) {
      bgfx::destroy(released);
    }
    context = nullptr;
    owns_handle = false;
  }
  ~TextureAllocation() { Release(); }
};

struct TextureManager::AsyncState {
  struct PendingRequest {
    std::uint32_t task_id{0};
    TextureLoadFailurePolicy failure_policy{
        TextureLoadFailurePolicy::kStrict};
    TextureLoadPriority priority{TextureLoadPriority::kPrefetch};
  };

  static_assert(
      std::is_nothrow_move_constructible_v<PreparedTextureUpload>,
      "bounded texture handoff requires allocation-free package moves");

  [[nodiscard]] bool PushPrepared(PreparedTextureUpload upload) noexcept {
    if (prepared_count == prepared_slots.size()) {
      return false;
    }
    const std::size_t tail =
        (prepared_head + prepared_count) % prepared_slots.size();
    prepared_slots[tail].emplace(std::move(upload));
    ++prepared_count;
    return true;
  }

  [[nodiscard]] PreparedTextureUpload PopPrepared() noexcept {
    auto& slot = prepared_slots[prepared_head];
    PreparedTextureUpload upload = std::move(*slot);
    slot.reset();
    prepared_head = (prepared_head + 1u) % prepared_slots.size();
    --prepared_count;
    return upload;
  }

  mutable std::mutex mutex;
  std::unordered_map<std::uint32_t, PendingRequest> pending;
  std::array<std::optional<PreparedTextureUpload>,
             TextureManager::kMaxAsyncRequests>
      prepared_slots;
  std::size_t prepared_head{0};
  std::size_t prepared_count{0};
  std::uint64_t generation{1};
  bool accepting{true};
};

bgfx::TextureHandle BgfxTextureLeaseAccess::Get(
    const TextureLease& lease) noexcept {
  return lease.allocation_ != nullptr
             ? lease.allocation_->handle.load(std::memory_order_acquire)
             : bgfx::TextureHandle{bgfx::kInvalidHandle};
}

bool TextureLease::valid() const noexcept {
  return allocation_ != nullptr &&
         bgfx::isValid(allocation_->handle.load(std::memory_order_acquire));
}

namespace {

constexpr bgfx::TextureHandle kInvalidTextureHandle = BGFX_INVALID_HANDLE;

[[nodiscard]] std::shared_ptr<openwow::render::TextureAllocation>
MakeTextureAllocation(
    const bgfx::TextureHandle handle, const bool owns_native_handle,
    openwow::render::api::RendererContext* const context,
    const openwow::render::api::DeviceGeneration generation) noexcept {
  openwow::render::TextureAllocation guard(handle, owns_native_handle, context,
                                           generation);
  try {
    auto allocation = std::make_shared<openwow::render::TextureAllocation>(
        handle, owns_native_handle, context, generation);
    guard.owns_handle = false;
    return allocation;
  } catch (...) {
    return nullptr;
  }
}

struct RetailTgaLayout {
  std::uint32_t source_width{0};
  std::uint32_t source_height{0};
  std::uint32_t texture_width{0};
  std::uint32_t texture_height{0};
  std::uint8_t alpha_depth{0};
  bool is_cube{false};
};

constexpr std::array<std::uint8_t, 6> kRetailTgaCubeFaceSources{
    0u, 2u, 4u, 5u, 3u, 1u};

std::uint16_t ReadU16Le(const std::vector<std::uint8_t>& bytes,
                        const std::size_t offset) noexcept {
  return static_cast<std::uint16_t>(
      bytes[offset] |
      (static_cast<std::uint16_t>(bytes[offset + 1u]) << 8u));
}

std::optional<RetailTgaLayout> InspectRetailTgaLayout(
    const std::vector<std::uint8_t>& source_bytes,
    const openwow::data::image::DecodedImage& decoded) noexcept {
  constexpr std::size_t kTgaHeaderSize = 18u;
  if (source_bytes.size() < kTgaHeaderSize || decoded.width <= 0 ||
      decoded.height <= 0) {
    return std::nullopt;
  }

  const std::uint8_t image_type = source_bytes[2u];
  if (image_type != 1u && image_type != 2u && image_type != 3u &&
      image_type != 9u && image_type != 10u && image_type != 11u) {
    return std::nullopt;
  }

  const std::uint32_t source_width = ReadU16Le(source_bytes, 12u);
  const std::uint32_t source_height = ReadU16Le(source_bytes, 14u);
  if (source_width == 0u || source_height == 0u ||
      source_width != static_cast<std::uint32_t>(decoded.width) ||
      source_height != static_cast<std::uint32_t>(decoded.height)) {
    return std::nullopt;
  }

  const bool is_cube =
      source_height <= std::numeric_limits<std::uint32_t>::max() / 6u &&
      source_width == source_height * 6u;
  return RetailTgaLayout{
      .source_width = source_width,
      .source_height = source_height,
      .texture_width = is_cube ? source_height : source_width,
      .texture_height = source_height,
      .alpha_depth =
          static_cast<std::uint8_t>(source_bytes[17u] & 0x0Fu),
      .is_cube = is_cube,
  };
}

std::uint8_t CountMipLevels(std::uint32_t width,
                            std::uint32_t height) noexcept {
  std::uint8_t count = 1u;
  while (width > 1u || height > 1u) {
    width = std::max(width >> 1u, 1u);
    height = std::max(height >> 1u, 1u);
    ++count;
  }
  return count;
}

std::optional<std::size_t> RgbaMipChainSize(
    std::uint32_t width, std::uint32_t height,
    const std::uint32_t face_count) noexcept {
  std::uint64_t bytes = 0u;
  while (true) {
    bytes += static_cast<std::uint64_t>(width) * height * 4u * face_count;
    if (bytes > std::numeric_limits<std::uint32_t>::max()) {
      return std::nullopt;
    }
    if (width == 1u && height == 1u) {
      return static_cast<std::size_t>(bytes);
    }
    width = std::max(width >> 1u, 1u);
    height = std::max(height >> 1u, 1u);
  }
}

bool BuildRetailTgaMipUpload(
    const RetailTgaLayout& layout,
    std::vector<std::uint8_t> decoded_rgba,
    PreparedTextureUpload& prepared) {
  const std::uint32_t face_count = layout.is_cube ? 6u : 1u;
  const auto total_size = RgbaMipChainSize(
      layout.texture_width, layout.texture_height, face_count);
  if (!total_size.has_value() ||
      decoded_rgba.size() !=
          static_cast<std::size_t>(layout.source_width) *
              layout.source_height * 4u) {
    return false;
  }

  std::vector<std::uint8_t> mip_bytes;
  mip_bytes.reserve(*total_size);
  const std::uint8_t mip_count =
      CountMipLevels(layout.texture_width, layout.texture_height);

  for (std::uint32_t destination_face = 0u;
       destination_face < face_count; ++destination_face) {
    std::uint32_t width = layout.texture_width;
    std::uint32_t height = layout.texture_height;
    std::vector<std::uint8_t> current(
        static_cast<std::size_t>(width) * height * 4u);

    if (layout.is_cube) {
      const std::uint32_t source_face =
          kRetailTgaCubeFaceSources[destination_face];
      for (std::uint32_t row = 0u; row < height; ++row) {
        const std::size_t source_offset =
            (static_cast<std::size_t>(row) * layout.source_width +
             static_cast<std::size_t>(source_face) * width) *
            4u;
        const std::size_t destination_offset =
            static_cast<std::size_t>(row) * width * 4u;
        std::copy_n(decoded_rgba.data() + source_offset,
                    static_cast<std::size_t>(width) * 4u,
                    current.data() + destination_offset);
      }
    } else {
      current = std::move(decoded_rgba);
    }

    while (true) {
      mip_bytes.insert(mip_bytes.end(), current.begin(), current.end());
      if (width == 1u && height == 1u) {
        break;
      }

      const std::uint32_t next_width = std::max(width >> 1u, 1u);
      const std::uint32_t next_height = std::max(height >> 1u, 1u);
      std::vector<std::uint32_t> next_pixels(
          static_cast<std::size_t>(next_width) * next_height);
      openwow::data::BlpMip_BoxFilterDownsample(
          next_pixels.data(), next_width,
          next_height, current.data(), width, height);
      current.resize(next_pixels.size() * sizeof(std::uint32_t));
      std::memcpy(current.data(), next_pixels.data(), current.size());
      width = next_width;
      height = next_height;
    }
  }

  if (mip_bytes.size() != *total_size) {
    return false;
  }
  prepared.width = layout.texture_width;
  prepared.height = layout.texture_height;
  prepared.upload_size = static_cast<std::uint32_t>(mip_bytes.size());
  prepared.mip_count = mip_count;
  prepared.complete_mip_chain = mip_count > 1u;
  prepared.is_cube = layout.is_cube;
  prepared.is_opaque = layout.alpha_depth == 0u;
  prepared.rgba_bytes = std::move(mip_bytes);
  prepared.valid = true;
  return true;
}

std::string MakeTabardEmblemRenderTargetCacheKey(
    const openwow::game::TabardEmblemRenderTargetDescriptor& descriptor) {
  return "__openwow_tabard_rt__" + descriptor.renderTargetName + "|" +
         descriptor.sourceTexturePath + "|" +
         std::to_string(descriptor.width) + "|" +
         std::to_string(descriptor.height) + "|" +
         std::to_string(descriptor.pitch) + "|" +
         (descriptor.forceWhiteRgb ? "1" : "0") + "|" +
         (descriptor.copySourceAlpha ? "1" : "0");
}

std::optional<std::vector<std::uint8_t>> TryBuildPortraitIconTextureImage(
    const std::vector<std::uint8_t>& source_texture_bytes,
    const std::vector<std::uint8_t>& mask_texture_bytes) {
  const auto parsed_source =
      openwow::data::BLPTextureLoader::Load(source_texture_bytes);
  if (!parsed_source.isValid ||
      parsed_source.mipCount <= kPortraitIconTextureMipLevel) {
    return std::nullopt;
  }

  auto source_mip_rgba = openwow::data::BLPTextureLoader::DecompressMip(
      parsed_source, kPortraitIconTextureMipLevel);
  const std::size_t expected_bytes =
      static_cast<std::size_t>(kPortraitIconTextureExtent) *
      static_cast<std::size_t>(kPortraitIconTextureExtent) * 4u;
  if (source_mip_rgba.size() != expected_bytes) {
    return std::nullopt;
  }

  const auto decoded_mask = openwow::data::image::DecodeImage(mask_texture_bytes);
  if (!decoded_mask.ok || decoded_mask.width != kPortraitIconTextureExtent ||
      decoded_mask.height != kPortraitIconTextureExtent ||
      decoded_mask.pixels_rgba.size() != expected_bytes) {
    return std::nullopt;
  }

  for (std::size_t offset = 0; offset < expected_bytes; offset += 4u) {
    source_mip_rgba[offset + 3] = decoded_mask.pixels_rgba[offset + 3];
  }

  return source_mip_rgba;
}

PreparedTextureUpload DecodeTextureUpload(
    const std::string& path,
    const openwow::data::TextureCacheRowIdentity& row,
    const std::vector<std::uint8_t>& source_bytes) {
  PreparedTextureUpload prepared;
  prepared.path = path;
  prepared.row_hash = row.hash;
  prepared.row_path = row.path;
  prepared.row_generation = row.generation;
  if (path.empty()) {
    prepared.error = "empty texture path";
    return prepared;
  }
  if (source_bytes.empty()) {
    prepared.error = "missing texture source";
    return prepared;
  }

  if (data::BLPTextureLoader::IsValidBLP(source_bytes)) {
    auto blp = data::BLPTextureLoader::Load(source_bytes);
    if (blp.isValid && !blp.mips.empty() && blp.header.width > 0 &&
        blp.header.height > 0 &&
        blp.header.width <= std::numeric_limits<std::uint16_t>::max() &&
        blp.header.height <= std::numeric_limits<std::uint16_t>::max()) {

      auto mip_upload =
          BuildBlpRgbaMipUpload(blp, CurrentBlockCompressionSupport());
      if (mip_upload.decoded_mip_count == 0 || mip_upload.bytes.empty() ||
          mip_upload.mip_sizes.empty()) {
        prepared.error = "BLP mip decode failed";
        return prepared;
      }

      prepared.width = blp.header.width;
      prepared.height = blp.header.height;
      prepared.complete_mip_chain = mip_upload.complete_mip_chain;
      const std::uint64_t upload_size = prepared.complete_mip_chain
                                            ? mip_upload.bytes.size()
                                            : mip_upload.mip_sizes.front();
      if (upload_size > std::numeric_limits<std::uint32_t>::max()) {
        prepared.error = "BLP upload exceeds bgfx memory extent";
        return prepared;
      }
      prepared.upload_size = static_cast<std::uint32_t>(upload_size);
      if (prepared.upload_size == 0 ||
          prepared.upload_size > mip_upload.bytes.size()) {
        prepared.error = "invalid BLP upload extent";
        return prepared;
      }
      prepared.rgba_bytes = std::move(mip_upload.bytes);
      prepared.upload_format = mip_upload.format;
      prepared.mip_count = mip_upload.decoded_mip_count;
      prepared.is_opaque =
          blp.header.alphaDepth == data::BLPTexAlphaDepth::NoAlpha;
      prepared.valid = true;
      return prepared;
    }
  }

  auto decoded = data::image::DecodeImage(source_bytes);
  if (!decoded.ok || decoded.pixels_rgba.empty() || decoded.width <= 0 ||
      decoded.height <= 0 ||
      decoded.height > std::numeric_limits<std::uint16_t>::max()) {
    prepared.error = decoded.error.empty() ? "image decode failed" : decoded.error;
    return prepared;
  }

  if (const auto tga_layout =
          InspectRetailTgaLayout(source_bytes, decoded);
      tga_layout.has_value()) {
    if (tga_layout->texture_width >
            std::numeric_limits<std::uint16_t>::max() ||
        !BuildRetailTgaMipUpload(
            *tga_layout, std::move(decoded.pixels_rgba), prepared)) {
      prepared.error = "invalid TGA texture layout";
    }
    return prepared;
  }

  if (decoded.width > std::numeric_limits<std::uint16_t>::max()) {
    prepared.error = "decoded image width exceeds bgfx texture extent";
    return prepared;
  }

  const std::uint64_t upload_size = static_cast<std::uint64_t>(decoded.width) *
                                    static_cast<std::uint64_t>(decoded.height) * 4u;
  if (upload_size == 0 || upload_size > decoded.pixels_rgba.size() ||
      upload_size > std::numeric_limits<std::uint32_t>::max()) {
    prepared.error = "invalid decoded image extent";
    return prepared;
  }

  prepared.width = static_cast<std::uint32_t>(decoded.width);
  prepared.height = static_cast<std::uint32_t>(decoded.height);
  prepared.upload_size = static_cast<std::uint32_t>(upload_size);
  prepared.rgba_bytes = std::move(decoded.pixels_rgba);
  prepared.is_opaque = !decoded.has_alpha_channel;
  prepared.valid = true;
  return prepared;
}

PreparedTextureUpload PrepareResolvedTextureUpload(
    const std::string& request_path,
    const openwow::data::TextureCacheRowIdentity& row,
    openwow::data::TextureCacheRowStore& rows,
    const openwow::data::TextureCacheRowStore::SourceLoader& loader) {
  if (const auto portrait_source = TryParsePortraitIconTextureKey(row.path);
      portrait_source.has_value()) {
    PreparedTextureUpload prepared{
        .path = request_path,
        .row_hash = row.hash,
        .row_path = row.path,
        .row_generation = row.generation,
    };
    const auto source_identity =
        rows.Resolve(std::string(*portrait_source) + ".blp");
    const auto mask_identity = rows.Resolve(
        std::string(kPortraitIconMaskTexturePath) + ".blp");
    const auto source = rows.Load(source_identity, loader);
    const auto mask = rows.Load(mask_identity, loader);
    if (!source || !mask) {
      prepared.error = "missing portrait texture dependency";
      return prepared;
    }

    auto portrait_rgba =
        TryBuildPortraitIconTextureImage(*source.bytes, *mask.bytes);
    if (!portrait_rgba.has_value() || portrait_rgba->empty() ||
        portrait_rgba->size() >
            static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max())) {
      prepared.error = "portrait texture composition failed";
      return prepared;
    }
    prepared.rgba_bytes = std::move(*portrait_rgba);
    prepared.width = kPortraitIconTextureExtent;
    prepared.height = kPortraitIconTextureExtent;
    prepared.upload_size =
        static_cast<std::uint32_t>(prepared.rgba_bytes.size());
    prepared.is_opaque = false;
    prepared.valid = true;
    return prepared;
  }

  const auto source = rows.Load(row, loader);
  if (!source) {
    return PreparedTextureUpload{
        .path = request_path,
        .row_hash = row.hash,
        .row_path = row.path,
        .row_generation = row.generation,
        .error = "missing texture source",
    };
  }
  return DecodeTextureUpload(request_path, source.identity, *source.bytes);
}

PreparedTextureUpload PrepareResolvedTabardRenderTargetUpload(
    const std::string& request_path,
    const openwow::data::TextureCacheRowIdentity& row,
    const openwow::game::TabardEmblemRenderTargetDescriptor& descriptor,
    openwow::data::TextureCacheRowStore& rows,
    const openwow::data::TextureCacheRowStore::SourceLoader& loader,
    const std::shared_ptr<const std::vector<std::uint8_t>>& fallback_pixels) {
  PreparedTextureUpload prepared{
      .path = request_path,
      .row_hash = row.hash,
      .row_path = row.path,
      .row_generation = row.generation,
  };
  if (descriptor.width == 0u || descriptor.height == 0u ||
      descriptor.width > std::numeric_limits<std::uint16_t>::max() ||
      descriptor.height > std::numeric_limits<std::uint16_t>::max()) {
    prepared.error = "invalid tabard render-target dimensions";
    return prepared;
  }

  const std::uint64_t expected_bytes =
      static_cast<std::uint64_t>(descriptor.width) *
      static_cast<std::uint64_t>(descriptor.height) * 4u;
  if (expected_bytes == 0u ||
      expected_bytes > std::numeric_limits<std::uint32_t>::max()) {
    prepared.error = "tabard render target exceeds bgfx memory extent";
    return prepared;
  }

  bool composed_source = false;
  if (loader && !descriptor.sourceTexturePath.empty()) {
    const auto source_identity = rows.Resolve(descriptor.sourceTexturePath);
    const auto source = rows.Load(source_identity, loader);
    if (source) {
      if (auto image =
              openwow::game::TryBuildGuildTabardEmblemRenderTargetImage(
                  descriptor, *source.bytes);
          image.has_value() &&
          image->pixelsRgba.size() ==
              static_cast<std::size_t>(expected_bytes)) {
        prepared.rgba_bytes = std::move(image->pixelsRgba);
        composed_source = true;
      }
    }
  }

  if (prepared.rgba_bytes.empty()) {
    if (fallback_pixels != nullptr &&
        fallback_pixels->size() ==
            static_cast<std::size_t>(expected_bytes)) {
      prepared.rgba_bytes = *fallback_pixels;
    } else {
      prepared.rgba_bytes.assign(
          static_cast<std::size_t>(expected_bytes), 0u);
    }
  }

  if (composed_source) {
    prepared.tabard_fallback_update =
        std::make_shared<const std::vector<std::uint8_t>>(
            prepared.rgba_bytes);
  }
  prepared.tabard_render_target_name = descriptor.renderTargetName;
  prepared.width = descriptor.width;
  prepared.height = descriptor.height;
  prepared.upload_size = static_cast<std::uint32_t>(expected_bytes);
  prepared.is_opaque = false;
  prepared.valid = true;
  return prepared;
}

}

TextureManager::TextureManager()
    : source_rows_(std::make_shared<openwow::data::TextureCacheRowStore>()),
      async_state_(std::make_shared<AsyncState>()) {}

TextureManager::~TextureManager() {
  BindRendererContext(nullptr);
  ResetAsyncPreparation(false);
}

void TextureManager::BindRendererContext(
    api::RendererContext* renderer_context) {
  if (renderer_context_ == renderer_context) {
    return;
  }
  if (renderer_context_ != nullptr && renderer_observer_registered_) {
    renderer_context_->RemoveDeviceLifecycleObserver(*this);
  }
  renderer_context_ = renderer_context;
  renderer_observer_registered_ = false;
  if (renderer_context_ != nullptr) {
    renderer_context_->AddDeviceLifecycleObserver(*this);
    renderer_observer_registered_ = true;
  }
}

void TextureManager::OnRendererDeviceWillReset() {
  restore_after_device_reset_ = initialized_;
  if (initialized_) {
    Shutdown();
  }
}

void TextureManager::OnRendererDeviceReady(
    const api::DeviceGeneration) {
  if (restore_after_device_reset_) {
    static_cast<void>(Initialize());
    restore_after_device_reset_ = false;
  }
}

bool TextureManager::Initialize() {
  {
    std::lock_guard lock(cache_mutex_);
    if (async_state_ == nullptr) {
      try {
        async_state_ = std::make_shared<AsyncState>();
      } catch (...) {
        return false;
      }
    }
  }
  if (initialized_) return true;

  const BlockCompressionSupport block_support = RefreshBlockCompressionSupport();
  diagnostics::Log(
      diagnostics::LogLevel::kInfo,
      std::string("TextureManager: GPU block-compression support BC1=") +
          (block_support.bc1 ? "1" : "0") + " BC2=" +
          (block_support.bc2 ? "1" : "0") + " BC3=" +
          (block_support.bc3 ? "1" : "0"));

  const bgfx::TextureHandle white = MakeSolid1x1(255, 255, 255, 255);
  const bgfx::TextureHandle black = MakeSolid1x1(0, 0, 0, 255);
  const bgfx::TextureHandle checker = MakeChecker8x8();
  white_tex_.store(white, std::memory_order_release);
  black_tex_.store(black, std::memory_order_release);
  checker_tex_.store(checker, std::memory_order_release);

  if (!bgfx::isValid(white) || !bgfx::isValid(black) ||
      !bgfx::isValid(checker)) {
    diagnostics::Log(diagnostics::LogLevel::kError,
              "TextureManager: failed to create placeholder textures");
    Shutdown();
    return false;
  }

  initialized_ = true;
  diagnostics::Log(diagnostics::LogLevel::kInfo, "TextureManager: initialized");
  return true;
}

void TextureManager::SetFileLoader(
    std::function<std::vector<uint8_t>(const std::string&)> loader) {
  ClearCache();
  std::lock_guard lock(cache_mutex_);
  file_loader_ = std::move(loader);
}

TextureLease TextureManager::AcquireTexture(const std::string& path) {
  return AcquireTextureInternal(path, true);
}

TextureLease TextureManager::AcquireTextureStrict(const std::string& path) {
  return AcquireTextureInternal(path, false);
}

TextureLease TextureManager::AcquireCachedTexture(const std::string& path) {
  if (path.empty()) {
    return {};
  }

  const std::uint32_t row_hash = openwow::data::HashTextureCachePath(path);
  std::lock_guard lock(cache_mutex_);
  return LeaseCacheEntryLocked(row_hash);
}

TextureLease TextureManager::AcquireCachedTextureStrict(
    const std::string& path) {
  if (path.empty()) {
    return {};
  }
  const std::uint32_t row_hash = openwow::data::HashTextureCachePath(path);
  std::lock_guard lock(cache_mutex_);
  return LeaseCacheEntryLocked(row_hash, false);
}

TextureLease TextureManager::AcquireCachedTextureStrictWithDimensions(
    const std::string& path, std::uint32_t& out_width,
    std::uint32_t& out_height) {
  if (path.empty()) {
    out_width = 0;
    out_height = 0;
    return {};
  }
  const std::uint32_t row_hash = openwow::data::HashTextureCachePath(path);
  std::lock_guard lock(cache_mutex_);
  return LeaseCacheEntryWithDimensionsLocked(row_hash, false, out_width,
                                             out_height);
}

TextureLease TextureManager::AcquireTextureAsync(
    const std::string& path,
    const TextureLoadFailurePolicy failure_policy,
    const TextureLoadPriority priority) {
  if (path.empty()) {
    return {};
  }

  const std::uint32_t row_hash =
      openwow::data::HashTextureCachePath(path);
  const bool allow_placeholder =
      failure_policy == TextureLoadFailurePolicy::kCheckerPlaceholder;
  {
    std::lock_guard lock(cache_mutex_);
    if (cache_.contains(row_hash)) {
      return LeaseCacheEntryLocked(row_hash, allow_placeholder);
    }
  }
  if (source_rows_->IsTerminalFailure(row_hash)) {
    if (!allow_placeholder) {
      return {};
    }
    std::lock_guard lock(cache_mutex_);
    static_cast<void>(StoreFailedTextureLocked(row_hash));
    return LeaseCacheEntryLocked(row_hash, true);
  }
  std::shared_ptr<AsyncState> state;
  {
    std::lock_guard lock(cache_mutex_);
    if (!file_loader_) {
      return {};
    }
    state = async_state_;
  }
  if (state == nullptr) {
    return {};
  }
  {
    std::lock_guard lock(state->mutex);
    if (!state->accepting ||
        (!state->pending.contains(row_hash) &&
         state->pending.size() >= kMaxAsyncRequests)) {
      return {};
    }
  }

  const auto row = source_rows_->Resolve(path);
  static_cast<void>(QueueTextureLoadInternal(
      row, path, failure_policy, priority));
  std::lock_guard lock(cache_mutex_);
  return LeaseCacheEntryLocked(row_hash, allow_placeholder);
}

bool TextureManager::QueueTextureLoad(
    const std::string& path,
    const TextureLoadFailurePolicy failure_policy,
    const TextureLoadPriority priority) {
  if (path.empty()) {
    return false;
  }
  const std::uint32_t row_hash =
      openwow::data::HashTextureCachePath(path);
  {
    std::lock_guard lock(cache_mutex_);
    if (const auto cached = cache_.find(row_hash); cached != cache_.end()) {
      return !cached->second.is_placeholder ||
             failure_policy == TextureLoadFailurePolicy::kCheckerPlaceholder;
    }
  }
  if (source_rows_->IsTerminalFailure(row_hash)) {
    if (failure_policy != TextureLoadFailurePolicy::kCheckerPlaceholder) {
      return false;
    }
    std::lock_guard lock(cache_mutex_);
    static_cast<void>(StoreFailedTextureLocked(row_hash));
    return true;
  }
  std::shared_ptr<AsyncState> state;
  {
    std::lock_guard lock(cache_mutex_);
    if (!file_loader_) {
      return false;
    }
    state = async_state_;
  }
  if (state == nullptr) {
    return false;
  }
  {
    std::lock_guard lock(state->mutex);
    if (!state->accepting ||
        (!state->pending.contains(row_hash) &&
         state->pending.size() >= kMaxAsyncRequests)) {
      return false;
    }
  }
  const auto row = source_rows_->Resolve(path);
  return QueueTextureLoadInternal(row, path, failure_policy, priority);
}

TextureLease TextureManager::AcquireTextureInternal(
    const std::string& path, const bool allow_failure_placeholder) {
  if (path.empty()) {
    return {};
  }
  const auto handle = LoadTextureInternal(path, allow_failure_placeholder);
  if (!bgfx::isValid(handle)) {
    return {};
  }
  const std::uint32_t row_hash = openwow::data::HashTextureCachePath(path);
  std::lock_guard lock(cache_mutex_);
  return LeaseCacheEntryLocked(row_hash, allow_failure_placeholder);
}

TextureLease TextureManager::LeaseCacheEntryLocked(
    const std::uint32_t row_hash,
    const bool allow_failure_placeholder) {
  const auto it = cache_.find(row_hash);
  if (it == cache_.end() || it->second.allocation == nullptr ||
      (it->second.is_placeholder && !allow_failure_placeholder)) {
    return {};
  }
  TouchCacheEntryLocked(it->second);

  it->second.staged_for_publish = false;
  return TextureLease{it->second.allocation};
}

TextureLease TextureManager::LeaseCacheEntryWithDimensionsLocked(
    const std::uint32_t row_hash, const bool allow_failure_placeholder,
    std::uint32_t& out_width, std::uint32_t& out_height) {
  const auto it = cache_.find(row_hash);
  if (it == cache_.end() || it->second.allocation == nullptr ||
      (it->second.is_placeholder && !allow_failure_placeholder)) {
    out_width = 0;
    out_height = 0;
    return {};
  }
  TouchCacheEntryLocked(it->second);

  it->second.staged_for_publish = false;
  out_width = it->second.width;
  out_height = it->second.height;
  return TextureLease{it->second.allocation};
}

bool TextureManager::QueueTextureLoadInternal(
    const openwow::data::TextureCacheRowIdentity& row,
    std::string request_path,
    const TextureLoadFailurePolicy failure_policy,
    const TextureLoadPriority priority,
    const openwow::game::TabardEmblemRenderTargetDescriptor*
        tabard_descriptor) {
  std::shared_ptr<AsyncState> state;
  std::function<std::vector<std::uint8_t>(const std::string&)> loader;
  {

    std::lock_guard lock(cache_mutex_);
    state = async_state_;
    try {
      loader = file_loader_;
    } catch (...) {
      return false;
    }
  }
  if ((tabard_descriptor == nullptr && !loader) || state == nullptr ||
      !source_rows_->IsCurrent(row)) {
    return false;
  }

  TextureLoadFailurePolicy effective_failure_policy = failure_policy;
  std::uint32_t prefetch_task_to_promote = 0;
  {
    std::lock_guard lock(state->mutex);
    if (!state->accepting) {
      return false;
    }
    if (auto pending = state->pending.find(row.hash);
        pending != state->pending.end()) {
      if (pending->second.failure_policy ==
          TextureLoadFailurePolicy::kCheckerPlaceholder) {
        effective_failure_policy = pending->second.failure_policy;
      }
      if (failure_policy == TextureLoadFailurePolicy::kCheckerPlaceholder) {
        pending->second.failure_policy = failure_policy;
        effective_failure_policy = failure_policy;
      }
      if (priority == TextureLoadPriority::kPrefetch ||
          pending->second.priority == TextureLoadPriority::kDemand ||
          pending->second.task_id == 0u) {
        return true;
      }
      prefetch_task_to_promote = pending->second.task_id;
    }
  }

  if (prefetch_task_to_promote != 0u) {
    bool cancelled = false;
    {
      std::lock_guard lock(cache_mutex_);

      cancelled = async_state_ == state && async_workers_ != nullptr &&
                  async_workers_->CancelTask(prefetch_task_to_promote);
      if (cancelled) {
        static_cast<void>(
            async_workers_->ForgetTask(prefetch_task_to_promote));
      }
    }
    if (!cancelled) {
      return true;
    }
    std::lock_guard lock(state->mutex);
    const auto pending = state->pending.find(row.hash);
    if (pending == state->pending.end() ||
        pending->second.task_id != prefetch_task_to_promote ||
        pending->second.priority == TextureLoadPriority::kDemand) {
      return true;
    }
    state->pending.erase(pending);
  }

  std::optional<openwow::game::TabardEmblemRenderTargetDescriptor>
      tabard_request;
  std::shared_ptr<const std::vector<std::uint8_t>> tabard_fallback;
  try {
    if (tabard_descriptor != nullptr) {
      tabard_request.emplace(*tabard_descriptor);
      std::lock_guard lock(cache_mutex_);
      if (const auto fallback = tabard_emblem_fallback_pixels_.find(
              tabard_descriptor->renderTargetName);
          fallback != tabard_emblem_fallback_pixels_.end()) {
        tabard_fallback = fallback->second;
      }
    }
  } catch (...) {
    return false;
  }

  std::uint64_t async_generation = 0;
  {
    std::lock_guard lock(state->mutex);
    if (!state->accepting) {
      return false;
    }
    if (auto pending = state->pending.find(row.hash);
        pending != state->pending.end()) {
      if (effective_failure_policy ==
          TextureLoadFailurePolicy::kCheckerPlaceholder) {
        pending->second.failure_policy = effective_failure_policy;
      }
      return true;
    }
    if (!source_rows_->IsCurrent(row) ||
        source_rows_->IsTerminalFailure(row.hash) ||
        state->pending.size() >= kMaxAsyncRequests) {
      return false;
    }
    try {
      state->pending.emplace(
          row.hash,
          AsyncState::PendingRequest{
              .failure_policy = effective_failure_policy,
              .priority = priority,
          });
    } catch (...) {
      return false;
    }
    async_generation = state->generation;
  }

  bool submitted = false;
  std::uint32_t task_id = 0u;
  try {
    const auto rows = source_rows_;
    auto task =
        [state, rows, loader = std::move(loader), row,
         request_path = std::move(request_path),
         tabard_request = std::move(tabard_request),
         tabard_fallback = std::move(tabard_fallback),
         async_generation]() mutable {
          PreparedTextureUpload prepared;
          try {
            if (tabard_request.has_value()) {
              prepared = PrepareResolvedTabardRenderTargetUpload(
                  request_path, row, *tabard_request, *rows, loader,
                  tabard_fallback);
            } else {
              prepared = PrepareResolvedTextureUpload(
                  request_path, row, *rows, loader);
            }
            if (!prepared.valid) {
              rows->MarkTerminalFailure(row);
            }
          } catch (const std::exception& exception) {
            rows->MarkTerminalFailure(row);
            try {
              prepared.error = exception.what();
            } catch (...) {
            }
          } catch (...) {
            rows->MarkTerminalFailure(row);
            try {
              prepared.error = "unknown source/decode exception";
            } catch (...) {
            }
          }

          if (prepared.path.empty()) {
            prepared.path = std::move(request_path);
          }
          prepared.row_hash = row.hash;
          if (prepared.row_path.empty()) {
            prepared.row_path = std::move(row.path);
          }
          prepared.row_generation = row.generation;

          std::lock_guard lock(state->mutex);
          if (state->accepting && state->generation == async_generation) {

            static_cast<void>(state->PushPrepared(std::move(prepared)));
          }
        };

    std::lock_guard lock(cache_mutex_);

    if (async_state_ == state) {
      EnsureAsyncWorkersLocked();
      task_id = async_workers_->Submit(
          "texture:" + row.path,
          priority == TextureLoadPriority::kDemand
              ? openwow::core::TaskPriority::High
              : openwow::core::TaskPriority::Low,
          std::move(task));
      submitted = true;
    }
  } catch (...) {
    std::lock_guard lock(state->mutex);
    state->pending.erase(row.hash);
    return false;
  }

  if (!submitted) {

    std::lock_guard lock(state->mutex);
    state->pending.erase(row.hash);
    return false;
  }

  std::lock_guard lock(state->mutex);
  if (state->accepting && state->generation == async_generation) {
    if (auto pending = state->pending.find(row.hash);
        pending != state->pending.end() && pending->second.task_id == 0u) {
      pending->second.task_id = task_id;
    }
  }
  return true;
}

void TextureManager::EnsureAsyncWorkersLocked() {
  if (async_workers_ != nullptr) {
    return;
  }
  auto workers = std::make_unique<openwow::core::ThreadPoolSystem>();
  workers->Initialize(2u);
  async_workers_ = std::move(workers);
}

std::size_t TextureManager::PumpPreparedUploads(
    const std::size_t max_uploads) {
  std::shared_ptr<AsyncState> state;
  {
    std::lock_guard lock(cache_mutex_);
    if (max_uploads == 0u || async_state_ == nullptr) {
      return 0u;
    }
    state = async_state_;

    for (std::size_t index = 0u; index < async_handoff_count_; ++index) {
      async_handoff_leases_[index] = {};
    }
    async_handoff_count_ = 0u;
    EvictToBudgetLocked(nullptr);
  }
  ReclaimRetiredAllocations();

  std::size_t committed = 0u;
  for (std::size_t index = 0u; index < max_uploads; ++index) {
    std::optional<PreparedTextureUpload> ready;
    TextureLoadFailurePolicy failure_policy =
        TextureLoadFailurePolicy::kStrict;
    std::uint32_t completed_task_id = 0u;
    {
      std::lock_guard lock(state->mutex);
      if (state->prepared_count == 0u) {
        break;
      }
      ready.emplace(state->PopPrepared());
      if (const auto pending = state->pending.find(ready->row_hash);
          pending != state->pending.end()) {
        failure_policy = pending->second.failure_policy;
        completed_task_id = pending->second.task_id;
        state->pending.erase(pending);
      }
    }
    if (completed_task_id != 0u) {
      std::lock_guard lock(cache_mutex_);
      if (async_workers_ != nullptr) {
        static_cast<void>(async_workers_->ForgetTask(completed_task_id));
      }
    }

    const openwow::data::TextureCacheRowIdentity identity{
        .hash = ready->row_hash,
        .path = ready->row_path,
        .generation = ready->row_generation,
    };
    bool published = false;
    if (ready->valid && source_rows_->IsCurrent(identity)) {
      published = bgfx::isValid(CommitPreparedTexture(*ready));
    }
    if (published) {
      std::lock_guard lock(cache_mutex_);
      if (async_handoff_count_ < async_handoff_leases_.size()) {
        async_handoff_leases_[async_handoff_count_++] =
            LeaseCacheEntryLocked(identity.hash, false);
      }
      ++committed;
      continue;
    }

    if (!source_rows_->IsCurrent(identity)) {
      continue;
    }
    source_rows_->MarkTerminalFailure(identity);
    if (failure_policy == TextureLoadFailurePolicy::kCheckerPlaceholder) {
      std::lock_guard lock(cache_mutex_);
      if (!cache_.contains(identity.hash)) {
        static_cast<void>(StoreFailedTextureLocked(identity.hash));
      }
    }
  }
  return committed;
}

TextureManagerStreamingStats TextureManager::StreamingStats() const {
  std::shared_ptr<AsyncState> state;
  {
    std::lock_guard lock(cache_mutex_);
    state = async_state_;
  }
  std::size_t pending = 0u;
  std::size_t prepared = 0u;
  if (state != nullptr) {
    std::lock_guard lock(state->mutex);
    pending = state->pending.size();
    prepared = state->prepared_count;
  }
  return TextureManagerStreamingStats{
      .pending = pending,
      .prepared = prepared,
      .failed = source_rows_ != nullptr
                    ? source_rows_->TerminalFailureCount()
                    : 0u,
  };
}

void TextureManager::ResetAsyncPreparation(const bool restart) {
  std::shared_ptr<AsyncState> replacement;
  if (restart) {
    try {
      replacement = std::make_shared<AsyncState>();
    } catch (...) {

    }
  }
  std::shared_ptr<AsyncState> state;
  std::unique_ptr<openwow::core::ThreadPoolSystem> workers;
  {

    std::lock_guard lock(cache_mutex_);
    state = std::move(async_state_);
    async_state_ = std::move(replacement);
    workers = std::move(async_workers_);
    for (std::size_t index = 0u; index < async_handoff_count_; ++index) {
      async_handoff_leases_[index] = {};
    }
    async_handoff_count_ = 0u;
  }
  if (state != nullptr) {
    std::lock_guard lock(state->mutex);
    state->accepting = false;
    ++state->generation;
  }
  if (workers != nullptr) {

    workers->Shutdown();
    workers.reset();
  }
}

PreparedTextureUpload TextureManager::PrepareTextureUpload(
    const std::string& path,
    const std::vector<std::uint8_t>& source_bytes) {
  return DecodeTextureUpload(
      path,
      {.hash = openwow::data::HashTextureCachePath(path),
       .path = openwow::data::CopyTextureCacheRowPath(path)},
      source_bytes);
}

PreparedTextureUpload TextureManager::PrepareTextureUploadFromLoader(
    const std::string& path,
    const std::function<std::vector<std::uint8_t>(const std::string&)>& loader) {
  PreparedTextureUpload missing;
  missing.path = path;
  if (path.empty()) {
    missing.error = "empty texture path";
    return missing;
  }

  if (!loader) {
    missing.error = "missing texture loader";
    return missing;
  }

  const std::string stored_path =
      openwow::data::CopyTextureCacheRowPath(path);
  auto load = [&loader](const std::string& candidate) {
    try {
      return loader(candidate);
    } catch (...) {
      return std::vector<std::uint8_t>{};
    }
  };

  std::vector<std::uint8_t> source =
      load(openwow::data::MakeRetailTextureCacheBlpPath(stored_path));
  if (source.empty()) {
    source =
        load(openwow::data::MakeRetailTextureCacheTgaPath(stored_path));
  }
  return DecodeTextureUpload(
      path,
      {.hash = openwow::data::HashTextureCachePath(path),
       .path = stored_path},
      source);
}

bgfx::TextureHandle TextureManager::CommitPreparedTexture(
    const PreparedTextureUpload& upload) {
  if (!upload.valid || upload.path.empty() || upload.width == 0 ||
      upload.height == 0 || upload.upload_size == 0 ||
      upload.upload_size > upload.rgba_bytes.size() ||
      upload.width > std::numeric_limits<std::uint16_t>::max() ||
      upload.height > std::numeric_limits<std::uint16_t>::max()) {
    return BGFX_INVALID_HANDLE;
  }
  openwow::data::TextureCacheRowIdentity identity;
  if (upload.row_generation != 0u) {

    identity = {
        .hash = upload.row_hash,
        .path = upload.row_path,
        .generation = upload.row_generation,
    };
    if (identity.hash == 0u || identity.path.empty() ||
        openwow::data::HashTextureCachePath(upload.path) != identity.hash ||
        !source_rows_->IsCurrent(identity)) {
      return BGFX_INVALID_HANDLE;
    }
  } else {
    identity = source_rows_->Resolve(upload.path);
    if ((upload.row_hash != 0u && upload.row_hash != identity.hash) ||
        (!upload.row_path.empty() &&
         !openwow::data::TextureCachePathsAlias(upload.row_path,
                                                identity.path))) {
      return BGFX_INVALID_HANDLE;
    }
  }
  const std::uint32_t row_hash = identity.hash;

  source_rows_->ClearTerminalFailure(identity);
  if (source_rows_->IsTerminalFailure(row_hash)) {
    return BGFX_INVALID_HANDLE;
  }

  {
    std::lock_guard lock(cache_mutex_);
    if (auto it = cache_.find(row_hash); it != cache_.end()) {
      if (!it->second.is_placeholder) {
        TouchCacheEntryLocked(it->second);
        it->second.staged_for_publish = true;
        return it->second.handle;
      }
      RetireCacheEntryLocked(it);
      cache_.erase(it);
    }
  }

  bgfx::TextureHandle handle{bgfx::kInvalidHandle};

  bool created_with_mips = upload.complete_mip_chain;
  if (upload.is_cube) {

    if (upload.width != upload.height || upload.mip_count == 0u ||
        upload.upload_format != BlpUploadFormat::kRgba8) {
      return BGFX_INVALID_HANDLE;
    }

    bgfx::TextureInfo cube_info{};
    bgfx::calcTextureSize(cube_info, static_cast<std::uint16_t>(upload.width),
                          static_cast<std::uint16_t>(upload.height), 1u, true,
                          upload.complete_mip_chain, 1u,
                          bgfx::TextureFormat::RGBA8);
    if (upload.mip_count != cube_info.numMips) {
      return BGFX_INVALID_HANDLE;
    }
    handle = bgfx::createTextureCube(
        static_cast<std::uint16_t>(upload.width),
        upload.complete_mip_chain, 1u, bgfx::TextureFormat::RGBA8);
    if (bgfx::isValid(handle)) {
      std::size_t offset = 0u;
      for (std::uint8_t face = 0u; face < 6u; ++face) {
        std::uint32_t width = upload.width;
        std::uint32_t height = upload.height;
        for (std::uint8_t mip = 0u; mip < upload.mip_count; ++mip) {
          const std::size_t mip_size =
              static_cast<std::size_t>(width) * height * 4u;
          if (offset > upload.upload_size ||
              mip_size > upload.upload_size - offset) {
            bgfx::destroy(handle);
            return BGFX_INVALID_HANDLE;
          }
          bgfx::updateTextureCube(
              handle, 0u, face, mip, 0u, 0u,
              static_cast<std::uint16_t>(width),
              static_cast<std::uint16_t>(height),
              bgfx::copy(upload.rgba_bytes.data() + offset,
                         static_cast<std::uint32_t>(mip_size)));
          offset += mip_size;
          width = std::max(width >> 1u, 1u);
          height = std::max(height >> 1u, 1u);
        }
      }
      if (offset != upload.upload_size) {
        bgfx::destroy(handle);
        return BGFX_INVALID_HANDLE;
      }
    }
  } else {

    const auto bgfx_storage_size = [&](const bool has_mips) {
      bgfx::TextureInfo info{};
      bgfx::calcTextureSize(info, static_cast<std::uint16_t>(upload.width),
                            static_cast<std::uint16_t>(upload.height), 1u,
                            false, has_mips, 1u,
                            ToBgfxTextureFormat(upload.upload_format));
      return static_cast<std::uint64_t>(info.storageSize);
    };

    created_with_mips = upload.complete_mip_chain &&
                        upload.upload_size >= bgfx_storage_size(true);
    if (upload.upload_size < bgfx_storage_size(false)) {
      return BGFX_INVALID_HANDLE;
    }
    const bgfx::Memory* upload_mem =
        bgfx::copy(upload.rgba_bytes.data(), upload.upload_size);
    handle = bgfx::createTexture2D(
        static_cast<std::uint16_t>(upload.width),
        static_cast<std::uint16_t>(upload.height),
        created_with_mips, 1,
        ToBgfxTextureFormat(upload.upload_format),
        BGFX_TEXTURE_NONE, upload_mem);
  }
  if (!bgfx::isValid(handle)) {
    source_rows_->MarkTerminalFailure(identity);
    diagnostics::Log(diagnostics::LogLevel::kWarn,
              "TextureManager: prepared texture upload failed for " +
                  (upload.row_path.empty() ? upload.path : upload.row_path));
    return BGFX_INVALID_HANDLE;
  }

  auto allocation = MakeTextureAllocation(
      handle, true, renderer_context_,
      renderer_context_ != nullptr ? renderer_context_->Generation()
                                   : api::DeviceGeneration{});
  if (allocation == nullptr) {
    source_rows_->MarkTerminalFailure(identity);
    return BGFX_INVALID_HANDLE;
  }

  bool stored = false;
  {
    std::lock_guard lock(cache_mutex_);
    stored = StoreCacheEntryLocked(
        row_hash,
        CachedTexture{
            .handle = handle,
            .allocation = std::move(allocation),
            .width = upload.width,
            .height = upload.height,
            .is_opaque = upload.is_opaque,

            .size_bytes = EstimateTextureBytes(
                              upload.width, upload.height,
                              created_with_mips) *
                          BlpUploadFormatBytesPerBlock(upload.upload_format) /
                          BlpUploadFormatBytesPerBlock(
                              BlpUploadFormat::kRgba8) *
                          (upload.is_cube ? 6u : 1u),
            .staged_for_publish = true,
        });
    if (stored && !upload.tabard_render_target_name.empty()) {
      try {
        tabard_render_target_rows_.insert_or_assign(
            upload.tabard_render_target_name,
            row_hash);
        if (upload.tabard_fallback_update != nullptr) {
          tabard_emblem_fallback_pixels_.insert_or_assign(
              upload.tabard_render_target_name,
              upload.tabard_fallback_update);
        }
      } catch (...) {

      }
    }
  }
  if (!stored) {

    source_rows_->MarkTerminalFailure(identity);
    return BGFX_INVALID_HANDLE;
  }
  return handle;
}

bgfx::TextureHandle TextureManager::LoadTextureInternal(const std::string& path,
                                                        const bool allow_failure_placeholder) {
  if (path.empty()) return BGFX_INVALID_HANDLE;

  const auto row = source_rows_->Resolve(path);
  const auto fail = [&]() -> bgfx::TextureHandle {
    source_rows_->MarkTerminalFailure(row);
    if (!allow_failure_placeholder) {
      return kInvalidTextureHandle;
    }
    std::lock_guard lock(cache_mutex_);
    return StoreFailedTextureLocked(row.hash);
  };

  {
    std::lock_guard lock(cache_mutex_);
    if (const auto it = cache_.find(row.hash); it != cache_.end()) {
      if (it->second.is_placeholder && !allow_failure_placeholder) {
        return kInvalidTextureHandle;
      }
      TouchCacheEntryLocked(it->second);
      return it->second.handle;
    }
  }
  if (source_rows_->IsTerminalFailure(row.hash)) {
    if (!allow_failure_placeholder) {
      return kInvalidTextureHandle;
    }
    std::lock_guard lock(cache_mutex_);
    return StoreFailedTextureLocked(row.hash);
  }

  std::function<std::vector<std::uint8_t>(const std::string&)> loader;
  {
    std::lock_guard lock(cache_mutex_);
    loader = file_loader_;
  }
  if (!loader) {
    diagnostics::Log(diagnostics::LogLevel::kWarn,
              "TextureManager: no file loader set — cannot load " + row.path);
    return fail();
  }

  PreparedTextureUpload prepared = PrepareResolvedTextureUpload(
      path, row, *source_rows_, loader);
  if (!prepared.valid) {
    if (!prepared.error.empty() && prepared.error != "missing texture source") {
      diagnostics::Log(diagnostics::LogLevel::kWarn,
                "TextureManager: decode failed for " + row.path + " — " +
                    prepared.error);
    }
    return fail();
  }
  const bgfx::TextureHandle handle = CommitPreparedTexture(std::move(prepared));
  return bgfx::isValid(handle) ? handle : fail();
}

TextureLease TextureManager::AcquireTabardEmblemRenderTargetAsync(
    const openwow::game::TabardEmblemRenderTargetDescriptor& descriptor,
    const TextureLoadPriority priority) {
  if (descriptor.width == 0u || descriptor.height == 0u ||
      descriptor.width > std::numeric_limits<std::uint16_t>::max() ||
      descriptor.height > std::numeric_limits<std::uint16_t>::max()) {
    return {};
  }

  std::string cache_key;
  try {
    cache_key = MakeTabardEmblemRenderTargetCacheKey(descriptor);
  } catch (...) {
    return {};
  }
  const std::uint32_t row_hash =
      openwow::data::HashTextureCachePath(cache_key);
  {
    std::lock_guard lock(cache_mutex_);
    if (cache_.contains(row_hash)) {
      return LeaseCacheEntryLocked(row_hash, false);
    }
  }
  if (source_rows_->IsTerminalFailure(row_hash)) {
    return {};
  }
  std::shared_ptr<AsyncState> state;
  {
    std::lock_guard lock(cache_mutex_);
    state = async_state_;
  }
  if (state == nullptr) {
    return {};
  }
  {
    std::lock_guard lock(state->mutex);
    if (!state->accepting ||
        (!state->pending.contains(row_hash) &&
         state->pending.size() >= kMaxAsyncRequests)) {
      return {};
    }
  }

  const auto row = source_rows_->Resolve(cache_key);
  static_cast<void>(QueueTextureLoadInternal(
      row, std::move(cache_key), TextureLoadFailurePolicy::kStrict, priority,
      &descriptor));
  std::lock_guard lock(cache_mutex_);
  if (const auto fallback = tabard_render_target_rows_.find(
          descriptor.renderTargetName);
      fallback != tabard_render_target_rows_.end()) {
    auto lease = LeaseCacheEntryLocked(fallback->second, false);
    if (lease) {
      return lease;
    }
    tabard_render_target_rows_.erase(fallback);
  }
  return LeaseCacheEntryLocked(row_hash, false);
}

bool TextureManager::HasResidentTexture(const std::string& path) const {
  if (path.empty()) {
    return false;
  }
  const auto row_hash = openwow::data::HashTextureCachePath(path);
  std::lock_guard lock(cache_mutex_);
  const auto found = cache_.find(row_hash);
  return found != cache_.end() && !found->second.is_placeholder &&
         bgfx::isValid(found->second.handle);
}

std::pair<std::uint32_t, std::uint32_t> TextureManager::GetTextureDimensions(
    const std::string& path) const {
  if (path.empty()) {
    return {0, 0};
  }
  const auto row_hash = openwow::data::HashTextureCachePath(path);
  std::lock_guard lock(cache_mutex_);
  const auto it = cache_.find(row_hash);
  if (it == cache_.end()) {
    return {0, 0};
  }

  return {it->second.width, it->second.height};
}

bool TextureManager::IsOpaque(const std::string& path) const {
  if (path.empty()) {
    return false;
  }
  const auto row_hash = openwow::data::HashTextureCachePath(path);
  std::lock_guard lock(cache_mutex_);
  const auto it = cache_.find(row_hash);
  if (it == cache_.end()) {
    return false;
  }
  return it->second.is_opaque;
}

std::size_t TextureManager::CachedCount() const {
  std::lock_guard lock(cache_mutex_);
  return cache_.size();
}

bgfx::TextureHandle TextureManager::StoreFailedTextureLocked(
    const std::uint32_t row_hash) {
  const bgfx::TextureHandle placeholder =
      checker_tex_.load(std::memory_order_acquire);

  auto allocation = MakeTextureAllocation(placeholder, false, nullptr,
                                          api::DeviceGeneration{});
  if (allocation == nullptr) {
    return kInvalidTextureHandle;
  }
  return StoreCacheEntryLocked(
             row_hash,
             CachedTexture{
                 .handle = placeholder,
                 .allocation = std::move(allocation),
                 .width = 0,
                 .height = 0,
                 .is_opaque = true,
                 .size_bytes = 0,
                 .is_placeholder = true,
             })
             ? placeholder
             : kInvalidTextureHandle;
}

void TextureManager::ClearCache() {
  ResetAsyncPreparation(true);
  std::vector<std::uint32_t> retained_rows;
  {
    std::lock_guard lock(cache_mutex_);
    ClearCacheLocked(false);
    retained_rows.reserve(cache_.size());
    for (const auto& [row_hash, texture] : cache_) {
      (void)texture;
      retained_rows.push_back(row_hash);
    }
  }
  ReclaimRetiredAllocations();
  source_rows_->InvalidateSources(retained_rows);
}

void TextureManager::ClearCacheLocked(const bool force) {
  for (auto it = cache_.begin(); it != cache_.end();) {
    const bool leased = it->second.allocation != nullptr &&
                        it->second.allocation.use_count() > 1;

    if (!force && !it->second.is_placeholder &&
        leased) {
      ++it;
      continue;
    }
    auto victim = it++;
    RetireCacheEntryLocked(victim);
    cache_.erase(victim);
  }
  tabard_emblem_fallback_pixels_.clear();
  tabard_render_target_rows_.clear();
  if (force) {
    memory_usage_ = 0;
  }
}

void TextureManager::Shutdown() {
  ResetAsyncPreparation(false);
  std::vector<std::shared_ptr<TextureAllocation>> retired;
  {
    std::lock_guard lock(cache_mutex_);
    ClearCacheLocked(true);
    retired.swap(retired_allocations_);
  }

  for (const auto& allocation : retired) {
    if (allocation != nullptr) {
      allocation->Release();
    }
  }
  retired.clear();
  source_rows_->Clear();

  if (const bgfx::TextureHandle white = white_tex_.exchange(
          kInvalidTextureHandle, std::memory_order_acq_rel);
      bgfx::isValid(white)) {
    bgfx::destroy(white);
  }
  if (const bgfx::TextureHandle black = black_tex_.exchange(
          kInvalidTextureHandle, std::memory_order_acq_rel);
      bgfx::isValid(black)) {
    bgfx::destroy(black);
  }
  if (const bgfx::TextureHandle checker = checker_tex_.exchange(
          kInvalidTextureHandle, std::memory_order_acq_rel);
      bgfx::isValid(checker)) {
    bgfx::destroy(checker);
  }

  initialized_ = false;
  diagnostics::Log(diagnostics::LogLevel::kInfo, "TextureManager: shutdown");
}

bgfx::TextureHandle TextureManager::MakeSolid1x1(uint8_t r, uint8_t g,
                                                   uint8_t b, uint8_t a) {
  const bgfx::Memory* mem = bgfx::alloc(4);
  mem->data[0] = r;
  mem->data[1] = g;
  mem->data[2] = b;
  mem->data[3] = a;
  return bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0,
                                mem);
}

bgfx::TextureHandle TextureManager::MakeChecker8x8() {
  constexpr int kSize = 8;
  constexpr int kPixels = kSize * kSize;
  const bgfx::Memory* mem = bgfx::alloc(kPixels * 4);
  for (int y = 0; y < kSize; ++y) {
    for (int x = 0; x < kSize; ++x) {
      const int idx = (y * kSize + x) * 4;
      const bool bright = ((x + y) & 1) == 0;
      mem->data[idx + 0] = bright ? 255 : 0;
      mem->data[idx + 1] = 0;
      mem->data[idx + 2] = bright ? 255 : 0;
      mem->data[idx + 3] = 255;
    }
  }
  return bgfx::createTexture2D(kSize, kSize, false, 1,
                                bgfx::TextureFormat::RGBA8, 0, mem);
}

std::uint64_t TextureManager::EstimateTextureBytes(std::uint32_t width,
                                                   std::uint32_t height,
                                                   bool has_mips) {
  std::uint64_t bytes = static_cast<std::uint64_t>(width) *
                        static_cast<std::uint64_t>(height) * 4ull;
  if (has_mips) {
    bytes += bytes / 3ull;
  }
  return bytes;
}

void TextureManager::TouchCacheEntryLocked(CachedTexture& texture) noexcept {

  lru_.splice(lru_.end(), lru_, texture.lru_position);
}

bool TextureManager::StoreCacheEntryLocked(const std::uint32_t row_hash,
                                           CachedTexture texture) noexcept {
  if (texture.allocation == nullptr) {
    return false;
  }

  RecencyPosition inserted_position = lru_.end();
  try {
    const std::uint64_t size_bytes = texture.size_bytes;

    if (auto it = cache_.find(row_hash); it != cache_.end()) {
      RetireCacheEntryLocked(it);
      cache_.erase(it);
    }

    inserted_position = lru_.insert(lru_.end(), row_hash);
    texture.lru_position = inserted_position;

    cache_.emplace(row_hash, std::move(texture));

    inserted_position = lru_.end();
    memory_usage_ += size_bytes;
    EvictToBudgetLocked(&row_hash);
    return true;
  } catch (...) {
    if (inserted_position != lru_.end()) {
      lru_.erase(inserted_position);
    }
    RetireAllocationLocked(std::move(texture.allocation));
    return false;
  }
}

void TextureManager::RetireCacheEntryLocked(const CacheIterator it) noexcept {
  if (it == cache_.end()) {
    return;
  }

  eviction_generation_.fetch_add(1u, std::memory_order_relaxed);

  if (it->second.lru_position != lru_.end()) {
    lru_.erase(it->second.lru_position);
    it->second.lru_position = lru_.end();
  }

  if (memory_usage_ >= it->second.size_bytes) {
    memory_usage_ -= it->second.size_bytes;
  } else {
    memory_usage_ = 0;
  }

  RetireAllocationLocked(std::move(it->second.allocation));
  it->second.handle = BGFX_INVALID_HANDLE;
}

void TextureManager::RetireAllocationLocked(
    std::shared_ptr<TextureAllocation> allocation) noexcept {
  if (allocation == nullptr) {
    return;
  }
  try {
    retired_allocations_.push_back(allocation);
    allocation.reset();
  } catch (...) {

  }
  if (allocation != nullptr) {
    allocation->Release();
  }
}

void TextureManager::ReclaimRetiredAllocations() {
  std::vector<std::shared_ptr<TextureAllocation>> reclaimable;
  {
    std::lock_guard lock(cache_mutex_);
    if (retired_allocations_.empty()) {
      return;
    }
    try {
      reclaimable.reserve(retired_allocations_.size());
    } catch (...) {
      return;
    }
    std::size_t keep = 0u;
    for (std::size_t index = 0u; index < retired_allocations_.size(); ++index) {
      auto& allocation = retired_allocations_[index];

      if (allocation != nullptr && allocation.use_count() > 1) {
        if (keep != index) {
          retired_allocations_[keep] = std::move(allocation);
        }
        ++keep;
        continue;
      }
      reclaimable.push_back(std::move(allocation));
    }

    retired_allocations_.erase(retired_allocations_.begin() +
                                   static_cast<std::ptrdiff_t>(keep),
                               retired_allocations_.end());
  }

  reclaimable.clear();
}

std::uint64_t TextureManager::GetMemoryBudget() const {
  std::lock_guard lock(cache_mutex_);
  return memory_budget_;
}

std::uint64_t TextureManager::GetMemoryUsage() const {
  std::lock_guard lock(cache_mutex_);
  return memory_usage_;
}

void TextureManager::SetMemoryBudget(std::uint64_t bytes) {
  {
    std::lock_guard lock(cache_mutex_);
    memory_budget_ = bytes;
    EvictToBudgetLocked(nullptr);
  }
  ReclaimRetiredAllocations();
}

void TextureManager::EvictToBudget() {
  {
    std::lock_guard lock(cache_mutex_);
    EvictToBudgetLocked(nullptr);
  }
  ReclaimRetiredAllocations();
}

void TextureManager::EvictToBudgetLocked(
    const std::uint32_t* preserve_row_hash) {
  if (memory_usage_ <= memory_budget_) {
    return;
  }

  const std::size_t eviction_quota =
      TextureCacheEvictionQuota(memory_usage_, memory_budget_);

  const std::size_t visit_limit =
      std::min(kTextureCacheMaxEntriesVisitedPerSweep, lru_.size());

  std::size_t evicted = 0u;
  std::size_t visited = 0u;
  for (auto node = lru_.begin();
       node != lru_.end() && evicted < eviction_quota && visited < visit_limit;
       ++visited) {

    const auto next = std::next(node);
    const std::uint32_t row_hash = *node;

    if (preserve_row_hash != nullptr && row_hash == *preserve_row_hash) {
      node = next;
      continue;
    }

    const auto it = cache_.find(row_hash);

    const bool leased = it->second.allocation != nullptr &&
                        it->second.allocation.use_count() > 1;
    if (leased || it->second.staged_for_publish) {

      TouchCacheEntryLocked(it->second);
      node = next;
      continue;
    }

    RetireCacheEntryLocked(it);
    cache_.erase(it);
    ++evicted;
    node = next;
    if (memory_usage_ <= memory_budget_) {
      break;
    }
  }
}

}

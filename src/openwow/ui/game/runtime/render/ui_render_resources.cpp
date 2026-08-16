#include "openwow/ui/game/runtime/render/ui_render_resources.h"

#include "openwow/data/formats/blp/texture_path.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/render/backend/bgfx/bgfx_text_cache.h"
#include "openwow/render/models/characters/model_portrait.h"
#include "openwow/render/models/characters/portrait_icon_texture.h"
#include "openwow/render/models/characters/portrait_renderer.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/render/ui/text_renderer.h"
#include "openwow/render/ui/ui_renderer.h"
#include "openwow/render/ui/ui_shaders.h"

#include <limits>
#include <utility>

namespace openwow::ui::game::runtime::render {

namespace {

using openwow::data::blp::ResolveNormalizedTexturePath;

constexpr openwow::render::TextureLoadPriority kTextureCreateLoadPriority =
    openwow::render::TextureLoadPriority::kPrefetch;

constexpr openwow::render::TextureLoadPriority kNaturalSizeLoadPriority =
    openwow::render::TextureLoadPriority::kDemand;

std::optional<UiTextureInfo> LoadSharedTexture(
    openwow::render::TextureManager& textures, const std::string& path,
    std::unordered_map<std::string, std::string>& normalized_path_cache) {
  if (path.empty()) return std::nullopt;

  const bool is_portrait_icon_key =
      openwow::render::TryParsePortraitIconTextureKey(path).has_value();
  const std::string& resolved_path =
      is_portrait_icon_key
          ? path
          : ResolveNormalizedTexturePath(path, normalized_path_cache);
  if (resolved_path.empty()) return std::nullopt;

  std::uint32_t width = 0;
  std::uint32_t height = 0;
  auto lease = textures.AcquireCachedTextureStrictWithDimensions(
      resolved_path, width, height);
  if (!lease) {
    static_cast<void>(textures.QueueTextureLoad(
        resolved_path, openwow::render::TextureLoadFailurePolicy::kStrict,
        openwow::render::TextureLoadPriority::kDemand));
    return std::nullopt;
  }
  return UiTextureInfo{.lease = std::move(lease),
                       .width = static_cast<int>(width),
                       .height = static_cast<int>(height)};
}

}

UiRenderResources::UiRenderResources(
    openwow::render::TextureManager& texture_manager,
    openwow::render::m2::M2System& m2_system)
    : texture_manager_(texture_manager),
      m2_system_(m2_system),
      ui_renderer_(std::make_unique<openwow::render::ui::UiRenderer>()),
      text_renderer_(std::make_unique<openwow::render::ui::TextRenderer>()),
      texture_loader_([this](const std::string& path) {
        return LoadSharedTexture(texture_manager_, path,
                                 normalized_texture_path_cache_);
      }) {}

UiRenderResources::~UiRenderResources() {
  BindRendererContext(nullptr);
  Release();
}

void UiRenderResources::BindRendererContext(
    openwow::render::api::RendererContext* renderer_context) {
  if (renderer_context_ == renderer_context) return;
  if (renderer_context_ != nullptr && observer_registered_) {
    renderer_context_->RemoveDeviceLifecycleObserver(*this);
  }
  renderer_context_ = renderer_context;
  observer_registered_ = false;
  if (renderer_context_ != nullptr) {
    renderer_context_->AddDeviceLifecycleObserver(*this);
    observer_registered_ = true;
    device_generation_ = renderer_context_->Generation();
  } else {
    device_generation_ = {};
  }
}

void UiRenderResources::SetVirtualFileSystem(
    const openwow::vfs::VirtualFileSystem* vfs) noexcept {
  vfs_ = vfs;
}

void UiRenderResources::SetTextureLoader(UiTextureLoad loader) {
  texture_loader_ = std::move(loader);
}

void UiRenderResources::QueueTextureLoad(const std::string& texture_path) {
  if (texture_path.empty() ||
      openwow::render::TryParsePortraitIconTextureKey(texture_path).has_value()) {

    return;
  }
  const std::string& resolved_path = ResolveNormalizedTexturePath(
      texture_path, normalized_texture_path_cache_);
  if (resolved_path.empty()) return;
  static_cast<void>(texture_manager_.QueueTextureLoad(
      resolved_path, openwow::render::TextureLoadFailurePolicy::kStrict,
      kTextureCreateLoadPriority));
}

std::optional<openwow::ui::TexturePixelSize>
UiRenderResources::ResolveTexturePixelSize(const std::string& texture_path) {
  if (texture_path.empty()) return std::nullopt;
  const bool is_portrait_icon_key =
      openwow::render::TryParsePortraitIconTextureKey(texture_path).has_value();
  const std::string& resolved_path =
      is_portrait_icon_key
          ? texture_path
          : ResolveNormalizedTexturePath(texture_path,
                                         normalized_texture_path_cache_);
  if (resolved_path.empty()) return std::nullopt;
  const auto [width, height] =
      texture_manager_.GetTextureDimensions(resolved_path);
  if (width == 0u && height == 0u) {

    if (!is_portrait_icon_key) {
      static_cast<void>(texture_manager_.QueueTextureLoad(
          resolved_path, openwow::render::TextureLoadFailurePolicy::kStrict,
          kNaturalSizeLoadPriority));
    }
    return std::nullopt;
  }
  return openwow::ui::TexturePixelSize{.width = width, .height = height};
}

std::optional<openwow::ui::ModelBoundingBox>
UiRenderResources::ResolveModelBoundingBox(const std::string& model_path) {
  if (model_path.empty()) return std::nullopt;

  const auto loaded = m2_system_.LoadModel(model_path);
  if (loaded.status != openwow::render::m2::M2ResultStatus::kReady ||
      loaded.model_id == 0u) {
    return std::nullopt;
  }

  const auto spatial = m2_system_.QueryModelSpatialInfo(loaded.model_id);
  if (spatial.status != openwow::render::m2::M2ResultStatus::kReady) {
    return std::nullopt;
  }
  const auto& bounds = spatial.spatial.local_bounds;
  return openwow::ui::ModelBoundingBox{
      .min = {bounds[0], bounds[1], bounds[2]},
      .max = {bounds[3], bounds[4], bounds[5]},
  };
}

bool UiRenderResources::Restore() {
  text_cache_ = std::make_unique<openwow::render::BgfxTextCache>(vfs_);
  owns_ui_program_cache_ = openwow::render::ui::RetainUiProgramCache();
  const bool ui_ready = ui_renderer_->Init();
  portrait_renderer_ =
      std::make_unique<openwow::render::PortraitRenderer>(m2_system_);
  portrait_renderer_->Initialize();
  return ui_ready;
}

void UiRenderResources::Release() {

  model_surfaces_.clear();
  model_diagnostics_.clear();
  if (bgfx::isValid(movie_texture_)) {
    bgfx::destroy(movie_texture_);
    movie_texture_ = BGFX_INVALID_HANDLE;
  }
  movie_texture_width_ = 0;
  movie_texture_height_ = 0;
  movie_texture_version_ = 0;
  movie_texture_has_version_ = false;
  if (portrait_renderer_) {
    portrait_renderer_->Shutdown();
    portrait_renderer_.reset();
  }
  if (text_cache_) {
    text_cache_->Shutdown();
    text_cache_.reset();
  }
  ui_renderer_->Shutdown();
  if (owns_ui_program_cache_) {
    openwow::render::ui::ReleaseUiProgramCache();
    owns_ui_program_cache_ = false;
  }
}

void UiRenderResources::SetRuntimeActive(const bool active) noexcept {
  runtime_active_ = active;
}

void UiRenderResources::OnRendererDeviceWillReset() {
  if (runtime_active_) Release();
}

void UiRenderResources::OnRendererDeviceReady(
    const openwow::render::api::DeviceGeneration generation) {
  device_generation_ = generation;
  if (runtime_active_ && !Restore()) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kError,
        "GameUIManager: renderer-device resource restore failed");
  }
}

openwow::render::ui::UiRenderer& UiRenderResources::ui_renderer() noexcept {
  return *ui_renderer_;
}
const openwow::render::ui::UiRenderer& UiRenderResources::ui_renderer() const noexcept {
  return *ui_renderer_;
}
openwow::render::ui::TextRenderer& UiRenderResources::text_renderer() noexcept {
  return *text_renderer_;
}
openwow::render::BgfxTextCache* UiRenderResources::text_cache() noexcept {
  return text_cache_.get();
}
openwow::render::PortraitRenderer* UiRenderResources::portrait_renderer() noexcept {
  return portrait_renderer_.get();
}
openwow::render::TextureManager& UiRenderResources::texture_manager() noexcept {
  return texture_manager_;
}
openwow::render::m2::M2System& UiRenderResources::m2_system() noexcept {
  return m2_system_;
}
const UiTextureLoad& UiRenderResources::texture_loader() const noexcept {
  return texture_loader_;
}

bgfx::TextureHandle UiRenderResources::UploadMovieFrame(
    const std::uint8_t* rgba, const int width, const int height,
    const std::uint32_t version) {
  if (rgba == nullptr || width <= 0 || height <= 0 ||
      width > std::numeric_limits<std::uint16_t>::max() ||
      height > std::numeric_limits<std::uint16_t>::max()) {
    return BGFX_INVALID_HANDLE;
  }
  const std::size_t pixel_count =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if (pixel_count > std::numeric_limits<std::uint32_t>::max() / 4u) {
    return BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(movie_texture_) && movie_texture_width_ == width &&
      movie_texture_height_ == height && movie_texture_has_version_ &&
      movie_texture_version_ == version) {
    return movie_texture_;
  }

  const std::uint32_t byte_count =
      static_cast<std::uint32_t>(pixel_count * 4u);
  const bgfx::Memory* const memory = bgfx::copy(rgba, byte_count);
  if (!bgfx::isValid(movie_texture_) || movie_texture_width_ != width ||
      movie_texture_height_ != height) {
    if (bgfx::isValid(movie_texture_)) {
      bgfx::destroy(movie_texture_);
    }
    movie_texture_ = bgfx::createTexture2D(
        static_cast<std::uint16_t>(width),
        static_cast<std::uint16_t>(height), false, 1,
        bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_NONE | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
        memory);
  } else {
    bgfx::updateTexture2D(
        movie_texture_, 0, 0, 0, 0,
        static_cast<std::uint16_t>(width),
        static_cast<std::uint16_t>(height), memory);
  }
  if (!bgfx::isValid(movie_texture_)) {
    movie_texture_width_ = 0;
    movie_texture_height_ = 0;
    movie_texture_has_version_ = false;
    return BGFX_INVALID_HANDLE;
  }
  movie_texture_width_ = width;
  movie_texture_height_ = height;
  movie_texture_version_ = version;
  movie_texture_has_version_ = true;
  return movie_texture_;
}
std::vector<openwow::render::ui::MeshVertex>&
UiRenderResources::text_mesh_vertices() noexcept { return text_mesh_vertices_; }
std::vector<std::uint16_t>& UiRenderResources::text_mesh_indices() noexcept {
  return text_mesh_indices_;
}
stateful_widgets::SliderState& UiRenderResources::slider_state() noexcept {
  return slider_state_;
}
stateful_widgets::CooldownState& UiRenderResources::cooldown_state() noexcept {
  return cooldown_state_;
}
stateful_widgets::ScrollingMessageFrameState&
UiRenderResources::scrolling_message_state() noexcept {
  return scrolling_message_state_;
}
std::vector<stateful_widgets::ScrollingMessageMeasurement>&
UiRenderResources::scrolling_message_measurements() noexcept {
  return scrolling_message_measurements_;
}
std::vector<stateful_widgets::ScrollingMessageLinePlan>&
UiRenderResources::scrolling_message_line_plans() noexcept {
  return scrolling_message_line_plans_;
}
std::unordered_map<std::string, std::unique_ptr<openwow::render::ModelPortrait>>&
UiRenderResources::model_surfaces() noexcept { return model_surfaces_; }
std::unique_ptr<openwow::render::ModelPortrait>&
UiRenderResources::AcquireModelSurface(const std::string& key) {
  auto& surface = model_surfaces_[key];
  if (!surface) {
    surface = std::make_unique<openwow::render::ModelPortrait>(m2_system_);
  }
  return surface;
}
std::unordered_map<std::string, std::string>&
UiRenderResources::model_diagnostics() noexcept { return model_diagnostics_; }

}

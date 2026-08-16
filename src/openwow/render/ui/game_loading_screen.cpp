#include "openwow/ui/game/loading_screen.h"

#include "openwow/core/client_misc.h"
#include "openwow/data/blp/blp_decoder.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/data/formats/dbc/dbc_structures.h"
#include "openwow/render/ui/text_renderer.h"
#include "openwow/render/ui/ui_shaders.h"
#include "openwow/render/ui/ui_text_escapes.h"
#include "openwow/screens/loading_screen_manager.h"
#include "openwow/ui/ui_aspect_scales.h"
#include "openwow/ui/game/loading_screen_progress_bar.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace openwow::ui::game {

namespace detail {

constexpr std::uint64_t kLoadingScreenBlendState =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
    | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,
                            BGFX_STATE_BLEND_INV_SRC_ALPHA);

bool ShouldAttemptWideLoadingScreenTexture(const bool has_wide_screen,
                                           const float current_aspect_ratio) {
  return has_wide_screen
      && current_aspect_ratio
             > kStandardLoadingScreenAspectRatio
                   + kWideLoadingScreenAspectRatioThreshold;
}

std::string BuildWideLoadingScreenTexturePath(std::string_view base_path) {
  const std::size_t dot = base_path.find('.');
  if (dot == std::string_view::npos) {
    std::string wide_path(base_path);
    wide_path += "Wide";
    return wide_path;
  }

  std::string wide_path;
  wide_path.reserve(base_path.size() + 4);
  wide_path.append(base_path.substr(0, dot));
  wide_path += "Wide";
  wide_path.append(base_path.substr(dot));
  return wide_path;
}

std::string SelectLoadingScreenTexturePath(
    std::string_view base_path, const bool has_wide_screen,
    const float current_aspect_ratio,
    const std::function<bool(const std::string&)>& archive_path_probe) {
  if (base_path.empty()) {
    return {};
  }

  if (ShouldAttemptWideLoadingScreenTexture(has_wide_screen,
                                            current_aspect_ratio)
      && archive_path_probe) {
    const std::string wide_path =
        BuildWideLoadingScreenTexturePath(base_path);
    if (archive_path_probe(wide_path)) {
      return wide_path;
    }
  }

  return std::string(base_path);
}

float ResolveLoadingScreenTargetAspectRatio(const bool using_wide_texture) {
  return using_wide_texture ? kWideLoadingScreenAspectRatio
                            : kStandardLoadingScreenAspectRatio;
}

std::uint32_t ResolveVisibleRibbonQuadCount(std::uint32_t overlay_vertex_count,
                                            float progress) {
  if (overlay_vertex_count <= 8) {
    return 0;
  }

  const auto ribbon_quad_count = (overlay_vertex_count - 8u) / 4u;
  if (ribbon_quad_count < 2) {
    return 0;
  }

  const float clamped_progress = std::clamp(progress, 0.0f, 1.0f);
  std::uint32_t visible_quad_count = 0;
  const float denominator = static_cast<float>(ribbon_quad_count - 1u);
  while (visible_quad_count < ribbon_quad_count) {
    const float normalized_index =
        static_cast<float>(visible_quad_count) / denominator;
    if (normalized_index > clamped_progress) {
      break;
    }
    ++visible_quad_count;
  }

  return visible_quad_count;
}

float ResolveLoadingScreenTextScale(
    const openwow::render::ui::TextRenderer& text_renderer,
    float screen_height, float normalized_font_height) {
  if (screen_height <= 0.0f || text_renderer.line_height() <= 0.0f) {
    return 1.0f;
  }

  return (screen_height * normalized_font_height)
      / text_renderer.line_height();
}

}

struct GameLoadingScreen::GpuState {
  openwow::render::ui::TextRenderer text_renderer;
  std::uint64_t prepared_text_generation{0u};
  float prepared_text_width_px{-1.0f};
  float prepared_text_scale{-1.0f};
  std::string wrapped_text;
  std::string visible_text;

  bgfx::TextureHandle white_tex = BGFX_INVALID_HANDLE;
  bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_tex = BGFX_INVALID_HANDLE;
  bgfx::VertexLayout layout;

  bgfx::TextureHandle bg_tex = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle loading_bar_fill_tex = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle loading_bar_border_tex = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle dynamic_elements_tex = BGFX_INVALID_HANDLE;
  std::array<bgfx::TextureHandle, kLoadingScreenWorldTileCount>
      world_tile_textures = [] {
        std::array<bgfx::TextureHandle, kLoadingScreenWorldTileCount> handles{};
        handles.fill(BGFX_INVALID_HANDLE);
        return handles;
      }();
};

namespace {

bgfx::TextureHandle TextureHandleFromIndex(std::uint16_t index) {
  return bgfx::TextureHandle{index};
}

std::uint16_t TextureHandleIndex(bgfx::TextureHandle handle) {
  return handle.idx;
}

}

GameLoadingScreen::GameLoadingScreen()
    : gpu_(std::make_unique<GpuState>()) {}

GameLoadingScreen::~GameLoadingScreen() = default;

bool GameLoadingScreen::Initialize() {
  if (initialized_) return true;

  if (!gpu_->text_renderer.Init(14)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "GameLoadingScreen: text renderer init failed");
    return false;
  }

  const uint32_t white = 0xFFFFFFFF;
  gpu_->white_tex = bgfx::createTexture2D(
      1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0,
      bgfx::copy(&white, sizeof(white)));

  const auto handles = openwow::render::ui::LoadUiProgram();
  gpu_->program = handles.program;
  gpu_->s_tex = handles.s_tex;

  gpu_->layout.begin()
      .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .end();

  initialized_ = true;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "GameLoadingScreen: initialized");
  return true;
}

void GameLoadingScreen::Shutdown() {
  gpu_->text_renderer.Shutdown();
  if (bgfx::isValid(gpu_->bg_tex)) {
    bgfx::destroy(gpu_->bg_tex);
    gpu_->bg_tex = BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(gpu_->loading_bar_fill_tex)) {
    bgfx::destroy(gpu_->loading_bar_fill_tex);
    gpu_->loading_bar_fill_tex = BGFX_INVALID_HANDLE;
  }
  if (bgfx::isValid(gpu_->loading_bar_border_tex)) {
    bgfx::destroy(gpu_->loading_bar_border_tex);
    gpu_->loading_bar_border_tex = BGFX_INVALID_HANDLE;
  }
  ReleaseTransportOverlayTextures();
  if (bgfx::isValid(gpu_->white_tex)) {
    bgfx::destroy(gpu_->white_tex);
    gpu_->white_tex = BGFX_INVALID_HANDLE;
  }
  openwow::render::ui::DestroyUiProgram(gpu_->program, gpu_->s_tex);
  initialized_ = false;
}

void GameLoadingScreen::PrepareMap(std::uint32_t map_id) {
  content_aspect_ratio_ = detail::ResolveLoadingScreenTargetAspectRatio(false);

  EnsureLoadingBarTexturesLoaded();

  LoadBackgroundTexture(map_id);
}

void GameLoadingScreen::ReleaseMap() {
  ReleaseTransportOverlayTextures();
}

void GameLoadingScreen::Render(
    openwow::screens::LoadingScreenManager& state,
    std::uint8_t view_id, float screen_w, float screen_h) {
  state.ResetTextRenderReceipt();
  if (!initialized_ || !state.IsVisible()) return;
  const float progress = state.GetProgress();

  float view[16];
  float proj[16];
  bx::mtxIdentity(view);
  bx::mtxOrtho(proj, 0.0f, screen_w, screen_h, 0.0f, 0.0f, 1000.0f, 0.0f,
               bgfx::getCaps()->homogeneousDepth);
  bgfx::setViewTransform(view_id, view, proj);
  bgfx::setViewRect(view_id, 0, 0,
                    static_cast<uint16_t>(screen_w),
                    static_cast<uint16_t>(screen_h));

  const auto content_rect = BuildContentViewportRect(screen_w, screen_h);
  const float content_x = content_rect.left * screen_w;
  const float content_w = (content_rect.right - content_rect.left) * screen_w;
  const float content_y = (1.0f - content_rect.top) * screen_h;
  const float content_h = (content_rect.top - content_rect.bottom) * screen_h;

  DrawQuad(view_id, 0.0f, 0.0f, screen_w, screen_h,
           detail::kLoadingScreenClearColorAbgr);
  if (bgfx::isValid(gpu_->bg_tex)) {
    DrawTexturedQuad(view_id, content_x, content_y, content_w, content_h,
                     TextureHandleIndex(gpu_->bg_tex));
  }

  EnsureLoadingBarTexturesLoaded();
  for (const auto& base_quad : BuildLoadingScreenProgressBarQuads(
           kLoadingScreenBottomProgressBarElements, progress)) {
    const auto quad = MapLoadingScreenQuadToViewport(base_quad, content_rect);
    DrawNormalizedTexturedQuad(view_id, screen_w, screen_h, quad.left,
                               quad.right, quad.bottom, quad.top,
                               ResolveLoadingBarTexture(quad.texture_path));
  }

  const auto& presentation = state.GetTextPresentation();
  if (presentation.built && !presentation.text.empty() &&
      gpu_->text_renderer.is_ready()) {
    const auto layout = detail::BuildLoadingScreenTextPixelLayout(
        screen_w, screen_h, presentation.anchor_x, presentation.anchor_y,
        presentation.width, presentation.font_height,
        presentation.shadow_offset_x, presentation.shadow_offset_y);
    const float scale = detail::ResolveLoadingScreenTextScale(
        gpu_->text_renderer, screen_h, presentation.font_height);
    if (gpu_->prepared_text_generation != presentation.source_generation ||
        gpu_->prepared_text_width_px != layout.box_width_px ||
        gpu_->prepared_text_scale != scale) {
      gpu_->prepared_text_generation = presentation.source_generation;
      gpu_->prepared_text_width_px = layout.box_width_px;
      gpu_->prepared_text_scale = scale;
      gpu_->wrapped_text = gpu_->text_renderer.WrapRichText(
          presentation.text, layout.box_width_px, scale);
      gpu_->visible_text =
          openwow::render::ui::UiTextEscapes::StripColorCodes(
              gpu_->wrapped_text);
    }
    const auto extent =
        gpu_->text_renderer.MeasureRichText(gpu_->wrapped_text, scale);
    const float text_top = layout.bottom_anchor_px - extent.height;

    gpu_->text_renderer.BeginFrame(view_id, screen_w, screen_h);

    const bool shadow_submitted = gpu_->text_renderer.DrawRichText(
        view_id, layout.box_left_px + layout.shadow_offset_x_px,
        text_top + layout.shadow_offset_y_px, gpu_->visible_text,
        presentation.shadow_color_argb, scale);
    const bool body_submitted = gpu_->text_renderer.DrawRichText(
        view_id, layout.box_left_px, text_top, gpu_->wrapped_text,
        presentation.text_color_argb, scale);
    state.PublishTextRenderReceipt(shadow_submitted && body_submitted);
  }
}

void GameLoadingScreen::RenderTransportProgressOverlay(
    openwow::screens::LoadingScreenManager& state,
    std::uint8_t view_id, float screen_w, float screen_h) {
  state.ResetTextRenderReceipt();
  if (!initialized_ || !state.IsVisible()) return;

  float view[16];
  float proj[16];
  bx::mtxIdentity(view);
  bx::mtxOrtho(proj, 0.0f, screen_w, screen_h, 0.0f, 0.0f, 1000.0f, 0.0f,
               bgfx::getCaps()->homogeneousDepth);
  bgfx::setViewTransform(view_id, view, proj);
  bgfx::setViewRect(view_id, 0, 0,
                    static_cast<uint16_t>(screen_w),
                    static_cast<uint16_t>(screen_h));

  const auto& assets = openwow::core::LoadingScreen_GetDynamicMapChangeAssets();
  const auto content_rect = BuildContentViewportRect(screen_w, screen_h);
  if (assets.dynamic_elements_loaded
      && assets.world_tile_texture_count == kLoadingScreenWorldTileCount
      && EnsureTransportOverlayTexturesLoaded()) {
    DrawQuad(view_id, 0.0f, 0.0f, screen_w, screen_h,
             detail::kLoadingScreenClearColorAbgr);
    RenderTransportWorldBackground(view_id, screen_w, screen_h, content_rect);
    RenderTransportDynamicOverlay(view_id, screen_w, screen_h, content_rect,
                                  state.GetProgress());
  } else {
    ReleaseTransportOverlayTextures();
  }

  EnsureLoadingBarTexturesLoaded();
  for (const auto& base_quad : BuildLoadingScreenProgressBarQuads(
           kLoadingScreenTopProgressBarElements, state.GetProgress())) {
    const auto quad = MapLoadingScreenQuadToViewport(base_quad, content_rect);
    DrawNormalizedTexturedQuad(view_id, screen_w, screen_h, quad.left,
                               quad.right, quad.bottom, quad.top,
                               ResolveLoadingBarTexture(quad.texture_path));
  }
}

void GameLoadingScreen::DrawQuad(std::uint8_t view_id, float x, float y,
                                  float w, float h, std::uint32_t abgr) {
  if (!bgfx::isValid(gpu_->program) || !bgfx::isValid(gpu_->white_tex)) return;
  if (w <= 0.0f || h <= 0.0f) return;

  struct QV {
    float x, y, u, v;
    uint32_t abgr;
  };

  bgfx::TransientVertexBuffer tvb;
  bgfx::TransientIndexBuffer tib;
  if (bgfx::getAvailTransientVertexBuffer(4, gpu_->layout) < 4
      || bgfx::getAvailTransientIndexBuffer(6) < 6) {
    return;
  }
  bgfx::allocTransientVertexBuffer(&tvb, 4, gpu_->layout);
  bgfx::allocTransientIndexBuffer(&tib, 6);

  auto* v = reinterpret_cast<QV*>(tvb.data);
  v[0] = {x, y, 0, 0, abgr};
  v[1] = {x + w, y, 1, 0, abgr};
  v[2] = {x + w, y + h, 1, 1, abgr};
  v[3] = {x, y + h, 0, 1, abgr};

  auto* idx = reinterpret_cast<uint16_t*>(tib.data);
  idx[0] = 0; idx[1] = 1; idx[2] = 2;
  idx[3] = 0; idx[4] = 2; idx[5] = 3;

  const uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
      | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);

  bgfx::setState(state);
  bgfx::setVertexBuffer(0, &tvb);
  bgfx::setIndexBuffer(&tib);
  bgfx::setTexture(0, gpu_->s_tex, gpu_->white_tex,
                   BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
  bgfx::submit(view_id, gpu_->program);
}

void GameLoadingScreen::DrawTexturedQuad(std::uint8_t view_id, float x,
                                          float y, float w, float h,
                                          std::uint16_t texture_index, float u0,
                                          float v0, float u1, float v1,
                                          std::uint32_t abgr) {
  const bgfx::TextureHandle tex = TextureHandleFromIndex(texture_index);
  if (!bgfx::isValid(gpu_->program) || !bgfx::isValid(tex)) return;
  if (w <= 0.0f || h <= 0.0f) return;

  struct QV {
    float x, y, u, v;
    uint32_t abgr;
  };

  bgfx::TransientVertexBuffer tvb;
  bgfx::TransientIndexBuffer tib;
  if (bgfx::getAvailTransientVertexBuffer(4, gpu_->layout) < 4
      || bgfx::getAvailTransientIndexBuffer(6) < 6) {
    return;
  }
  bgfx::allocTransientVertexBuffer(&tvb, 4, gpu_->layout);
  bgfx::allocTransientIndexBuffer(&tib, 6);

  auto* v = reinterpret_cast<QV*>(tvb.data);
  v[0] = {x, y, u0, v0, abgr};
  v[1] = {x + w, y, u1, v0, abgr};
  v[2] = {x + w, y + h, u1, v1, abgr};
  v[3] = {x, y + h, u0, v1, abgr};

  auto* idx = reinterpret_cast<uint16_t*>(tib.data);
  idx[0] = 0; idx[1] = 1; idx[2] = 2;
  idx[3] = 0; idx[4] = 2; idx[5] = 3;

  bgfx::setState(detail::kLoadingScreenBlendState);
  bgfx::setVertexBuffer(0, &tvb);
  bgfx::setIndexBuffer(&tib);
  bgfx::setTexture(0, gpu_->s_tex, tex,
                   BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
  bgfx::submit(view_id, gpu_->program);
}

void GameLoadingScreen::DrawNormalizedTexturedQuad(std::uint8_t view_id,
                                                   float screen_w,
                                                   float screen_h,
                                                   float left,
                                                   float right,
                                                   float bottom,
                                                   float top,
                                                   std::uint16_t texture_index,
                                                   float u0,
                                                   float v0,
                                                   float u1,
                                                   float v1,
                                                   std::uint32_t abgr) {
  const float x = left * screen_w;
  const float y = (1.0f - top) * screen_h;
  const float w = (right - left) * screen_w;
  const float h = (top - bottom) * screen_h;
  DrawTexturedQuad(view_id, x, y, w, h, texture_index, u0, v0, u1, v1, abgr);
}

LoadingScreenViewportRect GameLoadingScreen::BuildContentViewportRect(
    float screen_w, float screen_h) const {
  if (screen_w <= 0.0f || screen_h <= 0.0f) {
    return {};
  }

  const float screen_aspect = screen_w / screen_h;
  return BuildAspectFittedViewportRect(screen_aspect, content_aspect_ratio_);
}

void GameLoadingScreen::LoadBackgroundTexture(std::uint32_t map_id) {

  if (bgfx::isValid(gpu_->bg_tex)) {
    bgfx::destroy(gpu_->bg_tex);
    gpu_->bg_tex = BGFX_INVALID_HANDLE;
  }

  content_aspect_ratio_ = detail::ResolveLoadingScreenTargetAspectRatio(false);
  if (!file_loader_) return;

  if (dbc_loader_) {
    const auto* dbc =
        static_cast<const openwow::data::dbc::DbcLoader*>(dbc_loader_);

    const auto* map_entry = dbc->map().LookupEntry(map_id);
    if (!map_entry || map_entry->loading_screen_id == 0) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
          "GameLoadingScreen: No loading screen ID for map " +
          std::to_string(map_id));
    } else {
      const auto* ls =
          dbc->loading_screens().LookupEntry(map_entry->loading_screen_id);
      if (!ls) {
        openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
            "GameLoadingScreen: LoadingScreens entry " +
            std::to_string(map_entry->loading_screen_id) + " not found");
      } else {
        const float current_aspect_ratio = openwow::ui::GetCachedUiAspectRatio();
        if (detail::ShouldAttemptWideLoadingScreenTexture(
                ls->has_wide_screen != 0, current_aspect_ratio)
            && archive_path_probe_) {
          const std::string wide_path =
              detail::BuildWideLoadingScreenTexturePath(ls->path);
          if (archive_path_probe_(wide_path)
              && TryLoadBackgroundTexturePath(wide_path, true)) {
            return;
          }
        }

        if (TryLoadBackgroundTexturePath(ls->path, false)) {
          return;
        }
      }
    }
  }

  TryLoadBackgroundTexturePath(detail::kDefaultLoadingScreenTexturePath, false);
}

void GameLoadingScreen::EnsureLoadingBarTexturesLoaded() {
  if (bgfx::isValid(gpu_->loading_bar_fill_tex)
      && bgfx::isValid(gpu_->loading_bar_border_tex)) {
    return;
  }
  if (!file_loader_) return;

  if (!bgfx::isValid(gpu_->loading_bar_fill_tex)) {
    gpu_->loading_bar_fill_tex =
        TextureHandleFromIndex(LoadTextureAsset(kLoadingBarFillTexturePath));
  }
  if (!bgfx::isValid(gpu_->loading_bar_border_tex)) {
    gpu_->loading_bar_border_tex =
        TextureHandleFromIndex(LoadTextureAsset(kLoadingBarBorderTexturePath));
  }
}

bool GameLoadingScreen::EnsureTransportOverlayTexturesLoaded() {
  if (!file_loader_) {
    return false;
  }

  if (!bgfx::isValid(gpu_->dynamic_elements_tex)) {
    gpu_->dynamic_elements_tex =
        TextureHandleFromIndex(
            LoadTextureAsset(std::string(kLoadingScreenDynamicElementsTexturePath)));
  }

  if (!bgfx::isValid(gpu_->dynamic_elements_tex)) {
    return false;
  }

  for (std::size_t tile_index = 0; tile_index < gpu_->world_tile_textures.size();
       ++tile_index) {
    if (!bgfx::isValid(gpu_->world_tile_textures[tile_index])) {
      gpu_->world_tile_textures[tile_index] =
          TextureHandleFromIndex(LoadTextureAsset(
              BuildLoadingScreenWorldTilePath(tile_index)));
    }

    if (!bgfx::isValid(gpu_->world_tile_textures[tile_index])) {
      return false;
    }
  }

  return true;
}

void GameLoadingScreen::ReleaseTransportOverlayTextures() {
  if (bgfx::isValid(gpu_->dynamic_elements_tex)) {
    bgfx::destroy(gpu_->dynamic_elements_tex);
    gpu_->dynamic_elements_tex = BGFX_INVALID_HANDLE;
  }

  for (auto& texture : gpu_->world_tile_textures) {
    if (bgfx::isValid(texture)) {
      bgfx::destroy(texture);
    }
    texture = BGFX_INVALID_HANDLE;
  }
}

std::uint16_t GameLoadingScreen::LoadTextureAsset(
    const std::string& texture_path) {
  if (!file_loader_) return kInvalidTextureHandleIndex;

  const std::string tex_path = BuildLoadingScreenTextureArchivePath(texture_path);

  auto bytes = file_loader_(tex_path);
  if (bytes.empty()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
        "GameLoadingScreen: Failed to load BLP: " + tex_path);
    return kInvalidTextureHandleIndex;
  }

  auto decoded = openwow::data::blp::DecodeBlp(bytes);
  if (!decoded.ok || decoded.pixels_rgba.empty()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
        "GameLoadingScreen: Failed to decode BLP: " + tex_path
        + (decoded.error.empty() ? "" : " — " + decoded.error));
    return kInvalidTextureHandleIndex;
  }

  auto texture = bgfx::createTexture2D(
      static_cast<uint16_t>(decoded.width),
      static_cast<uint16_t>(decoded.height),
      false, 1, bgfx::TextureFormat::RGBA8, 0,
      bgfx::copy(decoded.pixels_rgba.data(),
                  static_cast<uint32_t>(decoded.pixels_rgba.size())));

  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
      "GameLoadingScreen: Loaded texture asset " + tex_path +
      " (" + std::to_string(decoded.width) + "x" +
      std::to_string(decoded.height) + ")");
  return TextureHandleIndex(texture);
}

bool GameLoadingScreen::TryLoadBackgroundTexturePath(
    std::string_view texture_path, const bool use_wide_aspect) {
  if (texture_path.empty()) {
    return false;
  }

  const bgfx::TextureHandle texture =
      TextureHandleFromIndex(LoadTextureAsset(std::string(texture_path)));
  if (!bgfx::isValid(texture)) {
    return false;
  }

  gpu_->bg_tex = texture;
  content_aspect_ratio_ =
      detail::ResolveLoadingScreenTargetAspectRatio(use_wide_aspect);
  return true;
}

std::uint16_t GameLoadingScreen::ResolveLoadingBarTexture(
    std::string_view texture_path) const {
  if (texture_path == kLoadingBarFillTexturePath
      && bgfx::isValid(gpu_->loading_bar_fill_tex)) {
    return TextureHandleIndex(gpu_->loading_bar_fill_tex);
  }
  if (texture_path == kLoadingBarBorderTexturePath
      && bgfx::isValid(gpu_->loading_bar_border_tex)) {
    return TextureHandleIndex(gpu_->loading_bar_border_tex);
  }
  return TextureHandleIndex(gpu_->white_tex);
}

void GameLoadingScreen::RenderTransportWorldBackground(
    std::uint8_t view_id, float screen_w, float screen_h,
    const LoadingScreenViewportRect& content_rect) {
  const float width = content_rect.right - content_rect.left;
  const float height = content_rect.top - content_rect.bottom;

  for (std::size_t tile_index = 0; tile_index < kLoadingScreenWorldTileCount;
       ++tile_index) {
    const auto& tile = kLoadingScreenWorldTileLayout[tile_index];
    const float left = content_rect.left + tile.left * width;
    const float right = content_rect.left + tile.right * width;
    const float top = content_rect.bottom + (1.0f - tile.top) * height;
    const float bottom = content_rect.bottom + (1.0f - tile.bottom) * height;
    DrawNormalizedTexturedQuad(view_id, screen_w, screen_h, left, right, bottom,
                               top,
                               TextureHandleIndex(
                                   gpu_->world_tile_textures[tile_index]),
                               tile.u0, tile.v0, tile.u1, tile.v1);
  }
}

void GameLoadingScreen::RenderTransportDynamicOverlay(
    std::uint8_t view_id, float screen_w, float screen_h,
    const LoadingScreenViewportRect& content_rect, const float progress) {
  const auto& overlay_assets =
      openwow::core::LoadingScreen_GetDynamicMapChangeAssets();
  const auto* overlay_vertices = overlay_assets.overlay_vertices.vertices;
  const auto overlay_vertex_count = overlay_assets.overlay_vertices.count;
  if (!overlay_vertices || overlay_vertex_count < 12
      || !bgfx::isValid(gpu_->dynamic_elements_tex)) {
    return;
  }

  const auto visible_ribbon_quad_count =
      detail::ResolveVisibleRibbonQuadCount(overlay_vertex_count, progress);
  const std::uint32_t index_count = visible_ribbon_quad_count * 6u + 12u;

  bgfx::TransientVertexBuffer tvb;
  bgfx::TransientIndexBuffer tib;
  if (bgfx::getAvailTransientVertexBuffer(overlay_vertex_count, gpu_->layout)
          < overlay_vertex_count
      || bgfx::getAvailTransientIndexBuffer(index_count) < index_count) {
    return;
  }

  bgfx::allocTransientVertexBuffer(&tvb, overlay_vertex_count, gpu_->layout);
  bgfx::allocTransientIndexBuffer(&tib, index_count);

  struct OverlayVertex {
    float x;
    float y;
    float u;
    float v;
    std::uint32_t abgr;
  };

  auto* vertex_data = reinterpret_cast<OverlayVertex*>(tvb.data);
  const float width = content_rect.right - content_rect.left;
  const float height = content_rect.top - content_rect.bottom;
  for (std::uint32_t index = 0; index < overlay_vertex_count; ++index) {
    const auto& source = overlay_vertices[index];
    const std::uint32_t color =
        index < 8u ? source.color : 0xFFFFFFFFu;
    const float normalized_x = content_rect.left + source.x * width;
    const float normalized_y =
        content_rect.bottom + (1.0f - source.y) * height;
    vertex_data[index] = {
        normalized_x * screen_w,
        (1.0f - normalized_y) * screen_h,
        source.u,
        source.v,
        color,
    };
  }

  auto* indices = reinterpret_cast<std::uint16_t*>(tib.data);
  std::uint32_t cursor = 0;
  for (std::uint32_t quad_index = 0; quad_index < visible_ribbon_quad_count;
       ++quad_index) {
    const std::uint16_t base =
        static_cast<std::uint16_t>(8u + quad_index * 4u);
    indices[cursor++] = static_cast<std::uint16_t>(base + 0u);
    indices[cursor++] = static_cast<std::uint16_t>(base + 1u);
    indices[cursor++] = static_cast<std::uint16_t>(base + 2u);
    indices[cursor++] = static_cast<std::uint16_t>(base + 1u);
    indices[cursor++] = static_cast<std::uint16_t>(base + 3u);
    indices[cursor++] = static_cast<std::uint16_t>(base + 2u);
  }

  for (const auto marker_index :
       detail::kLoadingScreenTransportOverlayMarkerIndices) {
    indices[cursor++] = marker_index;
  }

  bgfx::setState(detail::kLoadingScreenBlendState);
  bgfx::setVertexBuffer(0, &tvb);
  bgfx::setIndexBuffer(&tib, 0, index_count);
  bgfx::setTexture(0, gpu_->s_tex, gpu_->dynamic_elements_tex,
                   BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
  bgfx::submit(view_id, gpu_->program);
}

}

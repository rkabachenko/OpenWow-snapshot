
#include "openwow/ui/game/minimap.h"
#include "openwow/render/backend/bgfx/bgfx_texture_lease.h"

#include "openwow/data/texture_cache.h"
#include "openwow/render/resources/shaders/shader_registry.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/render/ui/ui_renderer.h"
#include "openwow/game/minimap_system.h"
#include "openwow/ui/game/minimap_system.h"
#include "openwow/ui/game/framescript/widgets/minimap_texture_uv.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/ui/widgets/media/rotated_texture_quad.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <utility>

namespace openwow::ui {

struct MinimapVertex {
  float x;
  float y;
  float u;
  float v;
  std::uint32_t abgr;
};

struct Minimap::RenderState {
  bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
  bool owns_texture{false};
  std::uint32_t texture_width{0};
  std::uint32_t texture_height{0};

  bgfx::ProgramHandle icon_program = BGFX_INVALID_HANDLE;
  bgfx::TextureHandle white_texture = BGFX_INVALID_HANDLE;
  bgfx::UniformHandle s_icon_texture = BGFX_INVALID_HANDLE;
  bgfx::VertexLayout layout{};
  bgfx::TextureHandle surface_texture = BGFX_INVALID_HANDLE;
  bgfx::FrameBufferHandle surface_framebuffer = BGFX_INVALID_HANDLE;
  openwow::render::TextureLease mask_texture_lease;
  std::unordered_map<std::uint32_t, openwow::render::TextureLease>
      retained_texture_leases;
};

namespace {

static bgfx::VertexLayout s_minimap_layout;
static bool s_layout_initialized = false;

void EnsureLayout() {
  if (s_layout_initialized) {
    return;
  }

  s_minimap_layout.begin()
      .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .end();
  s_layout_initialized = true;
}

constexpr std::array<std::uint16_t, 6> kQuadIndices{0, 1, 2, 0, 2, 3};
constexpr std::uint32_t kWhite = 0xFFFFFFFFu;
constexpr std::uint64_t kMaskAlphaWriteState = BGFX_STATE_WRITE_A;
constexpr std::uint64_t kMaskedTerrainBlendState =
    BGFX_STATE_WRITE_RGB |
    BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_DST_ALPHA,
                          BGFX_STATE_BLEND_INV_DST_ALPHA);
constexpr std::uint64_t kAlphaBlendState =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
    BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                          BGFX_STATE_BLEND_INV_SRC_ALPHA);
constexpr std::array<const char*, 7> kVehicleStateTexturePaths{{
    "Interface\\Minimap\\Vehicle-Ground-Unoccupied",
    "Interface\\Minimap\\Vehicle-Ground-Occupied",
    "Interface\\Minimap\\Vehicle-Air-Unoccupied",
    "Interface\\Minimap\\Vehicle-Air-Occupied",
    "Interface\\Minimap\\Vehicle-Ground-Unoccupied",
    "Interface\\Minimap\\Vehicle-Air-Horde",
    "Interface\\Minimap\\Vehicle-Air-Alliance",
}};

[[nodiscard]] std::string ResolveMinimapTextureRowPath(
    const std::string_view configured_path) {
  std::string resolved(configured_path);
  const std::size_t separator = resolved.find_last_of("\\/");
  const std::size_t dot = resolved.find_last_of('.');
  if (dot == std::string::npos ||
      (separator != std::string::npos && dot < separator)) {
    resolved.append(".blp");
    return resolved;
  }

  resolved.replace(dot, std::string::npos, ".blp");
  return resolved;
}

std::array<MinimapBackgroundVertex, 4> BuildScreenQuad(float x0, float y0,
                                                       float x1, float y1) {
  return {{{x0, y0, 0.0f, 0.0f},
           {x1, y0, 1.0f, 0.0f},
           {x1, y1, 1.0f, 1.0f},
           {x0, y1, 0.0f, 1.0f}}};
}

std::array<MinimapBackgroundVertex, 4> BuildScreenQuad(
    float x0, float y0, float x1, float y1,
    const openwow::ui::game::MinimapAtlasUvQuad& uv) {
  return {{{x0, y0, uv.upper_left_u, uv.upper_left_v},
           {x1, y0, uv.upper_right_u, uv.upper_right_v},
           {x1, y1, uv.lower_right_u, uv.lower_right_v},
           {x0, y1, uv.lower_left_u, uv.lower_left_v}}};
}

}

Minimap::Minimap(openwow::render::TextureManager& texture_manager,
                 openwow::ui::MinimapSystem& minimap_state,
                 openwow::game::MinimapSystem& minimap_content)
    : texture_manager_(texture_manager),
      minimap_state_(minimap_state),
      minimap_content_(minimap_content),
      render_(std::make_unique<RenderState>()) {}

Minimap::~Minimap() { Shutdown(); }

bool Minimap::Initialize(float screen_x, float screen_y, float radius) {
  if (initialized_) {
    return true;
  }

  pos_x_ = screen_x;
  pos_y_ = screen_y;
  radius_ = radius;

  if (!RestoreRendererDeviceResources()) {
    return false;
  }

  initialized_ = true;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "Minimap: initialized at (" +
                         std::to_string(static_cast<int>(pos_x_)) + "," +
                         std::to_string(static_cast<int>(pos_y_)) +
                         ") radius=" +
                         std::to_string(static_cast<int>(radius_)));
  return true;
}

bool Minimap::RestoreRendererDeviceResources() {
  EnsureLayout();
  render_->layout = s_minimap_layout;

  const auto type = bgfx::getRendererType();
  {
    render_->icon_program = openwow::render::CreateEmbeddedProgram(
        openwow::render::ShaderProgramId::Ui, type);
  }
  render_->s_icon_texture =
      bgfx::createUniform("s_uiTex", bgfx::UniformType::Sampler);

  {
    const std::uint32_t white_pixel = 0xFFFFFFFFu;
    const bgfx::Memory* mem = bgfx::copy(&white_pixel, sizeof(white_pixel));
    render_->white_texture = bgfx::createTexture2D(
        1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0, mem);
  }

  render_->surface_texture = bgfx::createTexture2D(
      kRenderTargetExtent, kRenderTargetExtent, false, 1,
      bgfx::TextureFormat::RGBA8,
      BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
  if (bgfx::isValid(render_->surface_texture)) {
    const bgfx::TextureHandle attachment = render_->surface_texture;
    render_->surface_framebuffer = bgfx::createFrameBuffer(1, &attachment, false);
  }

  if (!bgfx::isValid(render_->icon_program) ||
      !bgfx::isValid(render_->white_texture) ||
      !bgfx::isValid(render_->s_icon_texture) ||
      !bgfx::isValid(render_->surface_framebuffer)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "Minimap: icon shader/texture creation failed");
    ReleaseRendererDeviceResources();
    return false;
  }

  const auto& minimap_state = minimap_state_;
  render_->mask_texture_lease =
      texture_manager_.AcquireTextureAsync(
          ResolveMinimapTextureRowPath(mask_texture_path_),
          openwow::render::TextureLoadFailurePolicy::kCheckerPlaceholder,
          openwow::render::TextureLoadPriority::kPrefetch);
  const std::array<std::string, 8> stock_texture_paths = {
      static_poi_arrow_texture_,
      guide_poi_arrow_texture_,
      corpse_poi_arrow_texture_,
      group_member_arrow_texture_,
      player_arrow_texture_,
      minimap_state.GetPoiIconTexturePath(),
      minimap_state.GetObjectIconTexturePath(),
      minimap_state.GetPartyRaidBlipsTexturePath(),
  };
  for (const auto& path : stock_texture_paths) {
    static_cast<void>(RequestRetainedTexture(
        path, openwow::render::TextureLoadFailurePolicy::kCheckerPlaceholder,
        openwow::render::TextureLoadPriority::kPrefetch));
  }
  for (const char* path : kVehicleStateTexturePaths) {
    static_cast<void>(RequestRetainedTexture(
        path, openwow::render::TextureLoadFailurePolicy::kCheckerPlaceholder,
        openwow::render::TextureLoadPriority::kPrefetch));
  }
  return true;
}

void Minimap::ReleaseRendererDeviceResources() {
  if (bgfx::isValid(render_->surface_framebuffer)) {
    bgfx::destroy(render_->surface_framebuffer);
  }
  if (bgfx::isValid(render_->surface_texture)) {
    bgfx::destroy(render_->surface_texture);
  }
  if (render_->owns_texture && bgfx::isValid(render_->texture)) {
    bgfx::destroy(render_->texture);
  }
  if (bgfx::isValid(render_->icon_program)) {
    bgfx::destroy(render_->icon_program);
  }
  if (bgfx::isValid(render_->white_texture)) {
    bgfx::destroy(render_->white_texture);
  }
  if (bgfx::isValid(render_->s_icon_texture)) {
    bgfx::destroy(render_->s_icon_texture);
  }

  render_->texture = BGFX_INVALID_HANDLE;
  render_->owns_texture = false;
  render_->texture_width = 0;
  render_->texture_height = 0;
  render_->icon_program = BGFX_INVALID_HANDLE;
  render_->white_texture = BGFX_INVALID_HANDLE;
  render_->s_icon_texture = BGFX_INVALID_HANDLE;
  render_->surface_framebuffer = BGFX_INVALID_HANDLE;
  render_->surface_texture = BGFX_INVALID_HANDLE;
}

void Minimap::Shutdown() {
  ReleaseRendererDeviceResources();
  render_->mask_texture_lease = {};
  render_->retained_texture_leases.clear();
  background_tiles_.clear();
  icons_.clear();
  rotating_arrows_.clear();
  initialized_ = false;
}

void Minimap::Update(float player_x, float player_y, float player_z,
                     float facing) {
  player_x_ = player_x;
  player_y_ = player_y;
  player_z_ = player_z;
  facing_ = facing;

  if (initialized_) {
    const auto& minimap_state = minimap_state_;
    static_cast<void>(RequestRetainedTexture(
        minimap_state.GetPoiIconTexturePath(),
        openwow::render::TextureLoadFailurePolicy::kCheckerPlaceholder,
        openwow::render::TextureLoadPriority::kDemand));
    static_cast<void>(RequestRetainedTexture(
        minimap_state.GetObjectIconTexturePath(),
        openwow::render::TextureLoadFailurePolicy::kCheckerPlaceholder,
        openwow::render::TextureLoadPriority::kDemand));
    static_cast<void>(RequestRetainedTexture(
        minimap_state.GetPartyRaidBlipsTexturePath(),
        openwow::render::TextureLoadFailurePolicy::kCheckerPlaceholder,
        openwow::render::TextureLoadPriority::kDemand));
  }
}

void Minimap::SetZoom(float zoom) {
  zoom_ = std::clamp(zoom, 0.5f, 4.0f);
}

void Minimap::SetVisibleRadius(const float visible_radius) {
  if (std::isfinite(visible_radius) && visible_radius > 0.0f) {
    visible_radius_ = visible_radius;
  }
}

void Minimap::SetScreenPosition(float screen_x, float screen_y, float radius) {
  pos_x_ = screen_x;
  pos_y_ = screen_y;
  radius_ = radius;
}

void Minimap::ClearIcons() { icons_.clear(); }

void Minimap::AddIcon(const MinimapIcon& icon) {
  if (initialized_ && !icon.texture_path.empty()) {
    static_cast<void>(RequestRetainedTexture(
        icon.texture_path,
        openwow::render::TextureLoadFailurePolicy::kCheckerPlaceholder,
        openwow::render::TextureLoadPriority::kDemand));
  }
  icons_.push_back(icon);
}

void Minimap::ClearBackgroundTiles() { background_tiles_.clear(); }

void Minimap::AddBackgroundTile(MinimapBackgroundTile tile) {
  if (!tile.texture_path.empty()) {
    if (initialized_ && !tile.texture_lease.valid()) {
      static_cast<void>(RequestRetainedTexture(
          tile.texture_path,
          openwow::render::TextureLoadFailurePolicy::kStrict,
          openwow::render::TextureLoadPriority::kDemand));
    }
    background_tiles_.push_back(std::move(tile));
  }
}

void Minimap::SetMaskTexturePath(std::string texture_path) {
  if (mask_texture_path_ != texture_path) {
    render_->mask_texture_lease = {};
  }
  mask_texture_path_ = std::move(texture_path);
  if (initialized_ && !mask_texture_path_.empty()) {
    render_->mask_texture_lease =
        texture_manager_.AcquireTextureAsync(
            ResolveMinimapTextureRowPath(mask_texture_path_),
            openwow::render::TextureLoadFailurePolicy::kCheckerPlaceholder,
            openwow::render::TextureLoadPriority::kDemand);
  }
}

const std::string& Minimap::GetMaskTexturePath() const {
  return mask_texture_path_;
}

void Minimap::InvalidateTextureLeases() {
  render_->mask_texture_lease = {};
  render_->retained_texture_leases.clear();
  for (auto& tile : background_tiles_) {
    tile.texture_lease = {};
  }
}

void Minimap::ClearRotatingArrows() { rotating_arrows_.clear(); }

void Minimap::AddRotatingArrow(const MinimapRotatingArrow& arrow) {
  rotating_arrows_.push_back(arrow);
}

void Minimap::SetRotatingArrowTexturePath(
    MinimapRotatingArrowKind kind, const std::string& texture_path) {
  switch (kind) {
    case MinimapRotatingArrowKind::kStaticPoi:
      static_poi_arrow_texture_ = texture_path;
      break;
    case MinimapRotatingArrowKind::kGroupMember:
      group_member_arrow_texture_ = texture_path;
      break;
    case MinimapRotatingArrowKind::kCorpsePoi:
      corpse_poi_arrow_texture_ = texture_path;
      break;
    case MinimapRotatingArrowKind::kGuidePoi:
    default:
      guide_poi_arrow_texture_ = texture_path;
      break;
  }
  if (initialized_ && !texture_path.empty()) {
    static_cast<void>(RequestRetainedTexture(
        texture_path,
        openwow::render::TextureLoadFailurePolicy::kCheckerPlaceholder,
        openwow::render::TextureLoadPriority::kDemand));
  }
}

void Minimap::SetPlayerArrowTexturePath(const std::string& texture_path) {

  if (texture_path == player_arrow_texture_) {
    return;
  }
  player_arrow_texture_ = texture_path;
  player_arrow_natural_width_ = 0.0f;
  player_arrow_natural_height_ = 0.0f;
  if (initialized_ && !texture_path.empty()) {
    static_cast<void>(RequestRetainedTexture(
        texture_path,
        openwow::render::TextureLoadFailurePolicy::kCheckerPlaceholder,
        openwow::render::TextureLoadPriority::kDemand));
  }
}

void Minimap::SetUiUnitScale(const float scale) {
  if (scale > 0.0f) {
    ui_unit_scale_ = scale;
  }
}

void Minimap::SetPlayerArrowSize(const float width, const float height) {
  player_arrow_width_ = width;
  player_arrow_height_ = height;
}

void Minimap::SetIndoorMinimapActive(bool active) {
  indoor_minimap_active_ = active;
}

void Minimap::SetTexture(const std::uint8_t* rgba_data, std::uint32_t width,
                         std::uint32_t height) {
  if (!rgba_data || width == 0 || height == 0) {
    return;
  }

  if (render_->owns_texture && bgfx::isValid(render_->texture)) {
    bgfx::destroy(render_->texture);
    render_->texture = BGFX_INVALID_HANDLE;
  }

  const std::uint32_t byte_size = width * height * 4;
  const bgfx::Memory* mem = bgfx::copy(rgba_data, byte_size);
  render_->texture = bgfx::createTexture2D(
      static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height),
      false, 1, bgfx::TextureFormat::RGBA8,
      BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, mem);
  render_->owns_texture = bgfx::isValid(render_->texture);
  render_->texture_width = width;
  render_->texture_height = height;

  if (!bgfx::isValid(render_->texture)) {
    render_->texture_width = 0;
    render_->texture_height = 0;
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "Minimap: texture creation failed (" +
                           std::to_string(width) + "x" +
                           std::to_string(height) + ")");
  }
}

bool Minimap::HasTexture() const { return bgfx::isValid(render_->texture); }

std::array<MinimapBackgroundVertex, 4> Minimap::BuildBackgroundQuad()
    const {
  const float x0 = pos_x_ - radius_;
  const float y0 = pos_y_ - radius_;
  const float x1 = pos_x_ + radius_;
  const float y1 = pos_y_ + radius_;

  auto tex_coords =
      openwow::ui::media::ComputeRotatedTextureQuad(rotating_ ? -facing_
                                                               : 0.0f);
  const float inv_zoom = 1.0f / zoom_;
  const auto scale_uv = [inv_zoom](float value, std::uint32_t extent) {
    const float scaled = (value - 0.5f) * inv_zoom + 0.5f;
    return openwow::ui::game::ContractNormalizedTextureCoordForHalfTexel(
        scaled, extent);
  };

  return {{{x0, y0, scale_uv(tex_coords.upper_left_u, render_->texture_width),
            scale_uv(tex_coords.upper_left_v, render_->texture_height)},
           {x1, y0, scale_uv(tex_coords.upper_right_u, render_->texture_width),
            scale_uv(tex_coords.upper_right_v, render_->texture_height)},
           {x1, y1, scale_uv(tex_coords.lower_right_u, render_->texture_width),
            scale_uv(tex_coords.lower_right_v, render_->texture_height)},
           {x0, y1, scale_uv(tex_coords.lower_left_u, render_->texture_width),
            scale_uv(tex_coords.lower_left_v, render_->texture_height)}}};
}

bool Minimap::SubmitQuad(
    std::uint8_t view_id, std::uint16_t texture_index,
    const std::array<MinimapBackgroundVertex, 4>& vertices,
    std::uint32_t color, std::uint64_t state) const {
  const bgfx::TextureHandle texture{texture_index};
  if (!bgfx::isValid(render_->icon_program) ||
      !bgfx::isValid(render_->s_icon_texture) ||
      !bgfx::isValid(texture)) {
    return false;
  }

  bgfx::TransientVertexBuffer tvb;
  bgfx::TransientIndexBuffer tib;
  if (bgfx::getAvailTransientVertexBuffer(4, render_->layout) < 4 ||
      bgfx::getAvailTransientIndexBuffer(
          static_cast<std::uint32_t>(kQuadIndices.size())) <
          static_cast<std::uint32_t>(kQuadIndices.size())) {
    return false;
  }

  bgfx::allocTransientVertexBuffer(&tvb, 4, render_->layout);
  bgfx::allocTransientIndexBuffer(
      &tib, static_cast<std::uint32_t>(kQuadIndices.size()));

  auto* vtx = reinterpret_cast<MinimapVertex*>(tvb.data);
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    vtx[i] = {vertices[i].x, vertices[i].y, vertices[i].u, vertices[i].v,
              color};
  }

  auto* idx = reinterpret_cast<std::uint16_t*>(tib.data);
  std::copy(kQuadIndices.begin(), kQuadIndices.end(), idx);

  bgfx::setState(state);
  bgfx::setVertexBuffer(0, &tvb);
  bgfx::setIndexBuffer(&tib);
  bgfx::setTexture(0, render_->s_icon_texture, texture,
                   BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
  bgfx::submit(view_id, render_->icon_program);
  return true;
}

bool Minimap::SubmitQuads(
    const std::uint8_t view_id, const std::uint16_t texture_index,
    const std::vector<std::array<MinimapBackgroundVertex, 4>>& quads,
    const std::vector<std::uint32_t>& colors, const std::uint64_t state) const {
  const bgfx::TextureHandle texture{texture_index};
  if (quads.empty() || colors.size() != quads.size() ||
      !bgfx::isValid(render_->icon_program) ||
      !bgfx::isValid(render_->s_icon_texture) || !bgfx::isValid(texture)) {
    return false;
  }

  constexpr std::size_t kVerticesPerQuad = 4;
  constexpr std::size_t kIndicesPerQuad = 6;
  const std::size_t vertex_count = quads.size() * kVerticesPerQuad;
  const std::size_t index_count = quads.size() * kIndicesPerQuad;
  if (vertex_count > std::numeric_limits<std::uint16_t>::max() ||
      bgfx::getAvailTransientVertexBuffer(static_cast<std::uint32_t>(vertex_count),
                                          render_->layout) < vertex_count ||
      bgfx::getAvailTransientIndexBuffer(static_cast<std::uint32_t>(index_count)) <
          index_count) {
    return false;
  }

  bgfx::TransientVertexBuffer tvb;
  bgfx::TransientIndexBuffer tib;
  bgfx::allocTransientVertexBuffer(&tvb, static_cast<std::uint32_t>(vertex_count),
                                   render_->layout);
  bgfx::allocTransientIndexBuffer(&tib, static_cast<std::uint32_t>(index_count));

  auto* vertices = reinterpret_cast<MinimapVertex*>(tvb.data);
  auto* indices = reinterpret_cast<std::uint16_t*>(tib.data);
  for (std::size_t quad_index = 0; quad_index < quads.size(); ++quad_index) {
    const auto vertex_base = quad_index * kVerticesPerQuad;
    for (std::size_t i = 0; i < kVerticesPerQuad; ++i) {
      const auto& source = quads[quad_index][i];
      vertices[vertex_base + i] = {
          source.x, source.y, source.u, source.v, colors[quad_index]};
    }
    for (std::size_t i = 0; i < kIndicesPerQuad; ++i) {
      indices[quad_index * kIndicesPerQuad + i] = static_cast<std::uint16_t>(
          vertex_base + kQuadIndices[i]);
    }
  }

  bgfx::setState(state);
  bgfx::setVertexBuffer(0, &tvb);
  bgfx::setIndexBuffer(&tib);
  bgfx::setTexture(0, render_->s_icon_texture, texture,
                   BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
  bgfx::submit(view_id, render_->icon_program);
  return true;
}

std::uint16_t Minimap::AcquireRetainedTexture(
    const std::string_view texture_path) {
  if (texture_path.empty()) {
    return bgfx::kInvalidHandle;
  }

  const std::string resolved = ResolveMinimapTextureRowPath(texture_path);
  const std::uint32_t row_hash =
      openwow::data::HashTextureCachePath(resolved);
  auto [it, inserted] =
      render_->retained_texture_leases.try_emplace(row_hash);
  if (inserted || !it->second.valid()) {
    it->second =
        texture_manager_.AcquireCachedTexture(
            resolved);
  }
  return openwow::render::BgfxTextureLeaseAccess::Get(it->second).idx;
}

void Minimap::RequestRetainedTexture(
    const std::string_view texture_path,
    const openwow::render::TextureLoadFailurePolicy failure_policy,
    const openwow::render::TextureLoadPriority priority) {
  if (texture_path.empty()) {
    return;
  }
  const std::string resolved = ResolveMinimapTextureRowPath(texture_path);
  const std::uint32_t row_hash =
      openwow::data::HashTextureCachePath(resolved);
  auto& lease = render_->retained_texture_leases[row_hash];
  if (!lease.valid() ||
      failure_policy == openwow::render::TextureLoadFailurePolicy::kStrict) {
    lease = texture_manager_.AcquireTextureAsync(
        resolved, failure_policy, priority);
  }
}

void Minimap::ClearMaskAlpha(std::uint8_t view_id) const {
  if (!bgfx::isValid(render_->white_texture)) {
    return;
  }

  SubmitQuad(view_id, render_->white_texture.idx,
             BuildScreenQuad(pos_x_ - radius_, pos_y_ - radius_,
                             pos_x_ + radius_, pos_y_ + radius_),
             0x00FFFFFFu, kMaskAlphaWriteState);
}

void Minimap::RenderMaskPass(std::uint8_t view_id,
                             std::uint16_t mask_texture_index) const {
  const bgfx::TextureHandle mask_texture{mask_texture_index};
  if (!bgfx::isValid(mask_texture)) {
    return;
  }

  SubmitQuad(view_id, mask_texture.idx,
             BuildScreenQuad(pos_x_ - radius_, pos_y_ - radius_,
                             pos_x_ + radius_, pos_y_ + radius_),
             kWhite, kMaskAlphaWriteState);
}

void Minimap::RenderBackground(std::uint8_t view_id,
                               const bool backbuffer_masked) {
  if (!bgfx::isValid(render_->icon_program) ||
      !bgfx::isValid(render_->s_icon_texture)) {
    return;
  }

  auto& texture_manager = texture_manager_;
  if (backbuffer_masked) {
    if (!render_->mask_texture_lease.valid()) {
      render_->mask_texture_lease =
          texture_manager.AcquireCachedTexture(
              ResolveMinimapTextureRowPath(mask_texture_path_));
    }
    const bgfx::TextureHandle mask_texture =
        openwow::render::BgfxTextureLeaseAccess::Get(
            render_->mask_texture_lease);
    if (!bgfx::isValid(mask_texture)) return;
    ClearMaskAlpha(view_id);
    RenderMaskPass(view_id, mask_texture.idx);
  }
  const std::uint64_t terrain_state =
      backbuffer_masked
          ? kMaskedTerrainBlendState
          : (BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);

  if (!background_tiles_.empty()) {
    for (const auto& tile : background_tiles_) {

      openwow::render::TextureLease fallback_lease;
      bgfx::TextureHandle tile_texture = BGFX_INVALID_HANDLE;
      if (tile.texture_lease.valid()) {
        tile_texture =
            openwow::render::BgfxTextureLeaseAccess::Get(tile.texture_lease);
      } else {
        fallback_lease = texture_manager.AcquireCachedTexture(
            ResolveMinimapTextureRowPath(tile.texture_path));
        tile_texture =
            openwow::render::BgfxTextureLeaseAccess::Get(fallback_lease);
      }
      if (!bgfx::isValid(tile_texture)) {
        continue;
      }

      if (!SubmitQuad(view_id, tile_texture.idx, tile.vertices, tile.color,
                      terrain_state)) {
        break;
      }
      ++last_render_terrain_submission_count_;
    }
  } else if (bgfx::isValid(render_->texture)) {
    if (SubmitQuad(view_id, render_->texture.idx, BuildBackgroundQuad(),
                   kWhite, terrain_state)) {
      ++last_render_terrain_submission_count_;
    }
  }

  if (backbuffer_masked) ClearMaskAlpha(view_id);
}

void Minimap::Render(std::uint8_t view_id, float screen_width,
                     float screen_height) {
  last_render_terrain_submission_count_ = 0u;
  if (!initialized_ || screen_width <= 0.0f || screen_height <= 0.0f) {
    return;
  }

  float view_mtx[16];
  float proj_mtx[16];
  bx::mtxIdentity(view_mtx);
  bx::mtxOrtho(proj_mtx, 0.0f, screen_width, screen_height, 0.0f, 0.0f,
               1000.0f, 0.0f, bgfx::getCaps()->homogeneousDepth);

  bgfx::setViewTransform(view_id, view_mtx, proj_mtx);
  bgfx::setViewRect(view_id, 0, 0, static_cast<std::uint16_t>(screen_width),
                    static_cast<std::uint16_t>(screen_height));

  bgfx::setViewMode(view_id, bgfx::ViewMode::Sequential);

  RenderBackground(view_id, true);
  RenderSubmittedBatches(view_id);
  RenderRotatingArrows(view_id);
  RenderIcons(view_id, screen_width, screen_height);
  RenderPlayerArrow(view_id);
}

void Minimap::RenderToTexture(const std::uint8_t view_id) {
  last_render_terrain_submission_count_ = 0u;
  if (!initialized_ || !bgfx::isValid(render_->surface_framebuffer)) return;

  float view_mtx[16];
  float proj_mtx[16];
  bx::mtxIdentity(view_mtx);
  bx::mtxOrtho(proj_mtx, pos_x_ - radius_, pos_x_ + radius_,
               pos_y_ + radius_, pos_y_ - radius_, 0.0f, 1000.0f, 0.0f,
               bgfx::getCaps()->homogeneousDepth);
  bgfx::setViewFrameBuffer(view_id, render_->surface_framebuffer);
  bgfx::setViewRect(view_id, 0, 0, kRenderTargetExtent,
                    kRenderTargetExtent);
  bgfx::setViewClear(view_id, BGFX_CLEAR_COLOR, 0x00000000u, 1.0f, 0u);
  bgfx::setViewTransform(view_id, view_mtx, proj_mtx);
  bgfx::setViewMode(view_id, bgfx::ViewMode::Sequential);
  RenderBackground(view_id, false);
  RenderSubmittedBatches(view_id);
  RenderRotatingArrows(view_id);
  RenderIcons(view_id, static_cast<float>(kRenderTargetExtent),
              static_cast<float>(kRenderTargetExtent));
  RenderPlayerArrow(view_id);
  bgfx::touch(view_id);
}

MinimapSurfaceSubmitter Minimap::SurfaceSubmitter() const {
  return [this](openwow::render::ui::UiRenderer& renderer,
                const MinimapCompositeCommand& command) {
    if (!render_ || !bgfx::isValid(render_->surface_texture)) {
      return false;
    }
    if (!render_->mask_texture_lease.valid()) {
      render_->mask_texture_lease =
          texture_manager_.AcquireCachedTexture(
              ResolveMinimapTextureRowPath(mask_texture_path_));
    }
    const bgfx::TextureHandle mask =
        openwow::render::BgfxTextureLeaseAccess::Get(
            render_->mask_texture_lease);
    openwow::render::ui::Quad quad;
    quad.texture = render_->surface_texture;
    quad.mask_texture = mask;
    quad.x = command.x;
    quad.y = command.y;
    quad.w = command.width;
    quad.h = command.height;
    quad.abgr = command.abgr;
    quad.blend = openwow::render::ui::BlendMode::kAlpha;
    quad.flip_texture_y = bgfx::getCaps()->originBottomLeft;
    return renderer.Submit(quad);
  };
}

const std::string& Minimap::GetRotatingArrowTexturePath(
    MinimapRotatingArrowKind kind) const {
  switch (kind) {
    case MinimapRotatingArrowKind::kStaticPoi:
      return static_poi_arrow_texture_;
    case MinimapRotatingArrowKind::kGroupMember:
      return group_member_arrow_texture_;
    case MinimapRotatingArrowKind::kCorpsePoi:
      return corpse_poi_arrow_texture_;
    case MinimapRotatingArrowKind::kGuidePoi:
    default:
      return guide_poi_arrow_texture_;
  }
}

void Minimap::RenderRotatingArrows(std::uint8_t view_id) {
  if (!bgfx::isValid(render_->icon_program) ||
      !bgfx::isValid(render_->s_icon_texture)) {
    return;
  }

  const float ring_radius =
      kMinimapRotatingArrowRingRadiusUiUnits * ui_unit_scale_;
  const float half_size =
      kMinimapRotatingArrowQuadSpanUiUnits * 0.5f * ui_unit_scale_;
  for (const auto kind : {MinimapRotatingArrowKind::kStaticPoi,
                          MinimapRotatingArrowKind::kGuidePoi,
                          MinimapRotatingArrowKind::kCorpsePoi,
                          MinimapRotatingArrowKind::kGroupMember}) {
    std::vector<std::array<MinimapBackgroundVertex, 4>> quads;
    std::vector<std::uint32_t> colors;
    quads.reserve(rotating_arrows_.size());
    colors.reserve(rotating_arrows_.size());

    for (const auto& arrow : rotating_arrows_) {
      if (arrow.kind != kind ||
          (indoor_minimap_active_ && !arrow.visible_in_indoor_minimap)) {
        continue;
      }

      const float render_angle =
          rotating_ ? (arrow.angle_radians - facing_) : arrow.angle_radians;
      const float center_x = pos_x_ + std::cos(render_angle) * ring_radius;
      const float center_y = pos_y_ - std::sin(render_angle) * ring_radius;
      const auto tex_coords =
          openwow::ui::media::ComputeMinimapRotatingIconTextureQuad(
              render_angle);
      quads.push_back({{
          {center_x - half_size, center_y - half_size,
           tex_coords.upper_left_u, tex_coords.upper_left_v},
          {center_x + half_size, center_y - half_size,
           tex_coords.upper_right_u, tex_coords.upper_right_v},
          {center_x + half_size, center_y + half_size,
           tex_coords.lower_right_u, tex_coords.lower_right_v},
          {center_x - half_size, center_y + half_size,
           tex_coords.lower_left_u, tex_coords.lower_left_v},
      }});
      colors.push_back(kWhite);
    }

    if (quads.empty()) {
      continue;
    }
    const std::uint16_t texture =
        AcquireRetainedTexture(GetRotatingArrowTexturePath(kind));
    if (texture != bgfx::kInvalidHandle) {
      SubmitQuads(view_id, texture, quads, colors, kAlphaBlendState);
    }
  }
}

void Minimap::RenderPlayerArrow(const std::uint8_t view_id) {
  if (!bgfx::isValid(render_->icon_program) ||
      !bgfx::isValid(render_->s_icon_texture) ||
      player_arrow_texture_.empty()) {
    return;
  }

  const std::uint16_t texture = AcquireRetainedTexture(player_arrow_texture_);
  if (texture == bgfx::kInvalidHandle) {
    return;
  }

  if ((player_arrow_width_ <= 0.0f || player_arrow_height_ <= 0.0f) &&
      player_arrow_natural_width_ <= 0.0f) {
    std::uint32_t natural_w = 0;
    std::uint32_t natural_h = 0;
    const auto lease = texture_manager_.AcquireCachedTextureStrictWithDimensions(
        ResolveMinimapTextureRowPath(player_arrow_texture_), natural_w,
        natural_h);
    if (lease.valid() && natural_w > 0 && natural_h > 0) {
      player_arrow_natural_width_ = static_cast<float>(natural_w);
      player_arrow_natural_height_ = static_cast<float>(natural_h);
    }
  }
  const float width_units = player_arrow_width_ > 0.0f
                                ? player_arrow_width_
                                : player_arrow_natural_width_;
  const float height_units = player_arrow_height_ > 0.0f
                                 ? player_arrow_height_
                                 : player_arrow_natural_height_;
  if (width_units <= 0.0f || height_units <= 0.0f) {
    return;
  }

  const float render_angle = rotating_ ? 0.0f : facing_;
  const float half_width = width_units * ui_unit_scale_ * 0.5f;
  const float half_height = height_units * ui_unit_scale_ * 0.5f;
  const auto tex_coords =
      openwow::ui::media::ComputeRotatedTextureQuad(render_angle);
  const std::array<MinimapBackgroundVertex, 4> quad{{
      {pos_x_ - half_width, pos_y_ - half_height, tex_coords.upper_left_u,
       tex_coords.upper_left_v},
      {pos_x_ + half_width, pos_y_ - half_height, tex_coords.upper_right_u,
       tex_coords.upper_right_v},
      {pos_x_ + half_width, pos_y_ + half_height, tex_coords.lower_right_u,
       tex_coords.lower_right_v},
      {pos_x_ - half_width, pos_y_ + half_height, tex_coords.lower_left_u,
       tex_coords.lower_left_v},
  }};
  SubmitQuad(view_id, texture, quad, kWhite, kAlphaBlendState);
}

void Minimap::RenderIcons(std::uint8_t view_id, float ,
                          float ) {
  if (!bgfx::isValid(render_->icon_program) ||
      !bgfx::isValid(render_->white_texture)) {
    return;
  }

  auto& texture_manager = texture_manager_;
  struct IconTextureBatch {
    std::string texture_path;
    std::vector<std::array<MinimapBackgroundVertex, 4>> quads;
    std::vector<std::uint32_t> colors;
  };
  std::vector<IconTextureBatch> textured_batches;
  std::vector<std::array<MinimapBackgroundVertex, 4>> solid_quads;
  std::vector<std::uint32_t> solid_colors;
  solid_quads.reserve(icons_.size());
  solid_colors.reserve(icons_.size());

  for (const auto& icon : icons_) {
    float screen_ix = 0.0f;
    float screen_iy = 0.0f;
    ProjectWorldToScreen(icon.world_x, icon.world_y, screen_ix, screen_iy);

    const float sx = screen_ix - pos_x_;
    const float sy = pos_y_ - screen_iy;
    if (sx * sx + sy * sy > radius_ * radius_) {
      continue;
    }

    const float half_size = icon.quad_span_pixels * 0.5f;
    auto icon_quad = BuildScreenQuad(screen_ix - half_size,
                                     screen_iy - half_size,
                                     screen_ix + half_size,
                                     screen_iy + half_size);
    auto icon_color = icon.color;

    if (icon.texture_kind == MinimapIconTextureKind::kPoiAtlas) {

      if (icon.atlas_icon_index >=
          openwow::ui::game::kMinimapPoiAtlasIconCount) {
        continue;
      }

      if (AcquireRetainedTexture(icon.texture_path) ==
          bgfx::kInvalidHandle) {
        continue;
      }

      const auto [texture_width, texture_height] =
          texture_manager.GetTextureDimensions(
              ResolveMinimapTextureRowPath(icon.texture_path));
      const auto texture_extent = std::max(texture_width, texture_height);
      if (texture_extent == 0) {
        continue;
      }

      icon_quad = BuildScreenQuad(
          screen_ix - half_size, screen_iy - half_size, screen_ix + half_size,
          screen_iy + half_size,
          openwow::ui::game::ComputeMinimapPoiAtlasUvQuad(
              icon.atlas_icon_index, texture_extent));
      icon_color = kWhite;
      auto batch = std::find_if(
          textured_batches.begin(), textured_batches.end(),
          [&](const IconTextureBatch& candidate) {
            return candidate.texture_path == icon.texture_path;
          });
      if (batch == textured_batches.end()) {
        IconTextureBatch new_batch;
        new_batch.texture_path = icon.texture_path;
        textured_batches.push_back(std::move(new_batch));
        batch = std::prev(textured_batches.end());
      }
      batch->quads.push_back(icon_quad);
      batch->colors.push_back(icon_color);
    } else {
      solid_quads.push_back(icon_quad);
      solid_colors.push_back(icon_color);
    }
  }

  if (!solid_quads.empty()) {
    SubmitQuads(view_id, render_->white_texture.idx, solid_quads, solid_colors,
                kAlphaBlendState);
  }
  for (const auto& batch : textured_batches) {
    const std::uint16_t texture =
        AcquireRetainedTexture(batch.texture_path);
    if (texture != bgfx::kInvalidHandle) {
      SubmitQuads(view_id, texture, batch.quads, batch.colors,
                  kAlphaBlendState);
    }
  }
}

void Minimap::RenderSubmittedBatches(const std::uint8_t view_id) {
  auto& content = minimap_content_;
  const auto& state = minimap_state_;
  const std::string party_raid_texture = state.GetPartyRaidBlipsTexturePath();
  const std::string object_texture = state.GetObjectIconTexturePath();
  const std::string static_arrow_texture = state.GetRotatingArrowTexturePath(
      MinimapRotatingArrowKind::kStaticPoi);

  for (std::uint32_t batch_index = 0;
       batch_index < content.GetSubmittedBatchCount(); ++batch_index) {
    const auto* batch = content.GetSubmittedBatch(batch_index);
    if (batch == nullptr || batch->vertexCount == 0 ||
        batch->vertexCount != batch->texCoordCount || batch->indexCount == 0 ||
        batch->vertexCount > std::numeric_limits<std::uint16_t>::max()) {
      continue;
    }

    std::string texture_path;
    std::uint32_t color = kWhite;
    switch (batch->kind) {
      case openwow::game::MinimapSubmittedBatchKind::kPartyRaidAtlas:
        texture_path = party_raid_texture;
        break;
      case openwow::game::MinimapSubmittedBatchKind::kObjectAtlas:
        texture_path = object_texture;
        if (batch->sourceCategoryIndex != 0) {
          color = 0xFFB0B0B0u;
        }
        break;
      case openwow::game::MinimapSubmittedBatchKind::kVehicleStateTexture:
        if (batch->sourceCategoryIndex < 16 ||
            batch->sourceCategoryIndex >= 16 + kVehicleStateTexturePaths.size()) {
          continue;
        }
        texture_path =
            kVehicleStateTexturePaths[batch->sourceCategoryIndex - 16];
        break;
      case openwow::game::MinimapSubmittedBatchKind::kVehicleIconTexture:
        texture_path = static_arrow_texture;
        break;
    }

    const std::uint16_t texture = AcquireRetainedTexture(texture_path);
    if (texture == bgfx::kInvalidHandle ||
        bgfx::getAvailTransientVertexBuffer(batch->vertexCount,
                                            render_->layout) <
            batch->vertexCount ||
        bgfx::getAvailTransientIndexBuffer(batch->indexCount) <
            batch->indexCount) {
      continue;
    }

    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;
    bgfx::allocTransientVertexBuffer(&tvb, batch->vertexCount, render_->layout);
    bgfx::allocTransientIndexBuffer(&tib, batch->indexCount);
    auto* vertices = reinterpret_cast<MinimapVertex*>(tvb.data);
    auto* indices = reinterpret_cast<std::uint16_t*>(tib.data);

    bool valid = true;
    for (std::uint32_t i = 0; i < batch->vertexCount; ++i) {
      const auto* position = content.GetSubmittedVertex(batch->vertexStart + i);
      const auto* uv = content.GetSubmittedTexCoord(batch->texCoordStart + i);
      if (position == nullptr || uv == nullptr) {
        valid = false;
        break;
      }
      vertices[i] = {position->x, position->y, uv->u, uv->v, color};
    }
    for (std::uint32_t i = 0; valid && i < batch->indexCount; ++i) {
      const auto* source_index =
          content.GetSubmittedIndex(batch->indexStart + i);
      if (source_index == nullptr || *source_index < batch->vertexStart ||
          *source_index >= batch->vertexStart + batch->vertexCount) {
        valid = false;
        break;
      }
      indices[i] = static_cast<std::uint16_t>(*source_index - batch->vertexStart);
    }
    if (!valid) {
      continue;
    }

    bgfx::setState(kAlphaBlendState);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setIndexBuffer(&tib);
    bgfx::setTexture(0, render_->s_icon_texture,
                     bgfx::TextureHandle{texture},
                     BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    bgfx::submit(view_id, render_->icon_program);
  }
}

bool Minimap::HandleClick(float screen_x, float screen_y, float& out_world_x,
                          float& out_world_y) {
  const float dx = screen_x - pos_x_;
  const float dy = screen_y - pos_y_;
  if (dx * dx + dy * dy > radius_ * radius_) {
    return false;
  }

  const float world_range = visible_radius_;
  const float rotation_angle = rotating_ ? facing_ : 0.0f;
  const float cos_f = std::cos(rotation_angle);
  const float sin_f = std::sin(rotation_angle);
  const float unrotated_screen_x = dx * cos_f + dy * sin_f;
  const float unrotated_screen_y = -dx * sin_f + dy * cos_f;
  out_world_x =
      player_x_ - unrotated_screen_y / radius_ * world_range;
  out_world_y =
      player_y_ - unrotated_screen_x / radius_ * world_range;
  return true;
}

void Minimap::ProjectWorldToScreen(float world_x, float world_y,
                                   float& screen_x, float& screen_y) const {
  const float dx = world_x - player_x_;
  const float dy = world_y - player_y_;

  const float unrotated_screen_x = -dy;
  const float unrotated_screen_y = -dx;

  const float rotation_angle = rotating_ ? facing_ : 0.0f;
  const float cos_f = std::cos(rotation_angle);
  const float sin_f = std::sin(rotation_angle);
  const float rx =
      unrotated_screen_x * cos_f - unrotated_screen_y * sin_f;
  const float ry =
      unrotated_screen_x * sin_f + unrotated_screen_y * cos_f;

  const float world_range = visible_radius_;
  screen_x = pos_x_ + rx / world_range * radius_;
  screen_y = pos_y_ + ry / world_range * radius_;
}

void Minimap::ProjectWorldToScreenUnrotated(
    const float world_x, const float world_y, float& screen_x,
    float& screen_y) const {

  screen_x =
      pos_x_ - (world_y - player_y_) / visible_radius_ * radius_;
  screen_y =
      pos_y_ - (world_x - player_x_) / visible_radius_ * radius_;
}

}

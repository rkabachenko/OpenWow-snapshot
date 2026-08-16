
#include "openwow/render/world/environment/celestial_renderer.h"
#include "openwow/render/backend/bgfx/bgfx_texture_lease.h"

#include "openwow/world/environment/day_night.h"
#include "openwow/render/resources/shaders/shader_registry.h"
#include "openwow/render/world/environment/sky_settings.h"
#include "openwow/render/resources/textures/texture_manager.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace openwow::render {

namespace {

constexpr std::uint64_t kGlareAdditiveBlend =
    BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);

constexpr std::uint64_t kGlareAlphaBlend =
    BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                          BGFX_STATE_BLEND_INV_SRC_ALPHA);

constexpr float kSkyPassUniformScale = 6.6666665f;

uint32_t PackColor(float r, float g, float b, float a) {
  return (static_cast<uint32_t>(a * 255.0f) << 24) |
         (static_cast<uint32_t>(b * 255.0f) << 16) |
         (static_cast<uint32_t>(g * 255.0f) << 8) |
         (static_cast<uint32_t>(r * 255.0f) << 0);
}

}

CelestialRenderer::CelestialRenderer(TextureManager& texture_manager)
    : texture_manager_(texture_manager) {}

CelestialRenderer::~CelestialRenderer() { Shutdown(); }

bool CelestialRenderer::Initialize() {
  if (initialized_) return true;

  vertex_layout_.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .end();

  const auto type = bgfx::getRendererType();
  program_ = CreateEmbeddedProgram(ShaderProgramId::Celestial, type);

  if (!bgfx::isValid(program_)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                        "CelestialRenderer: shader program creation failed");
    return false;
  }

  s_tex_color_ = bgfx::createUniform("s_texColor", bgfx::UniformType::Sampler);
  u_celestial_params_ = bgfx::createUniform("u_celestialParams", bgfx::UniformType::Vec4);

  if (!bgfx::isValid(s_tex_color_) || !bgfx::isValid(u_celestial_params_)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                        "CelestialRenderer: uniform creation failed");
    Shutdown();
    return false;
  }

  auto& texture_manager = texture_manager_;
  sun_glare_texture_ = texture_manager.AcquireTextureStrict(
      openwow::game::kDayNightSunGlareTexturePath);
  moon_glare_texture_ = texture_manager.AcquireTextureStrict(
      openwow::game::kDayNightMoonGlareTexturePath);

  initialized_ = true;
  return true;
}

void CelestialRenderer::Shutdown() {
  sun_glare_texture_ = {};
  moon_glare_texture_ = {};
  DestroyCloudTextures();
  if (bgfx::isValid(program_)) bgfx::destroy(program_);
  if (bgfx::isValid(s_tex_color_)) bgfx::destroy(s_tex_color_);
  if (bgfx::isValid(u_celestial_params_)) bgfx::destroy(u_celestial_params_);
  program_ = BGFX_INVALID_HANDLE;
  s_tex_color_ = BGFX_INVALID_HANDLE;
  u_celestial_params_ = BGFX_INVALID_HANDLE;
  initialized_ = false;
}

void CelestialRenderer::DestroyCloudTextures() {
  for (bgfx::TextureHandle& texture : cloud_textures_) {
    if (bgfx::isValid(texture)) {
      bgfx::destroy(texture);
    }
    texture = BGFX_INVALID_HANDLE;
  }
  cloud_texture_grid_size_ = 0;
}

void CelestialRenderer::EnsureCloudTextures(const std::uint32_t grid_size) {
  if (grid_size == 0u) {
    DestroyCloudTextures();
    return;
  }
  if (grid_size == cloud_texture_grid_size_ && bgfx::isValid(cloud_textures_[0]) &&
      bgfx::isValid(cloud_textures_[1])) {
    return;
  }

  DestroyCloudTextures();
  const auto side = static_cast<uint16_t>(grid_size);
  for (bgfx::TextureHandle& texture : cloud_textures_) {

    texture = bgfx::createTexture2D(side, side, false, 1,
                                    bgfx::TextureFormat::BGRA8,
                                    BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    if (!bgfx::isValid(texture)) {
      openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                                "CelestialRenderer: DNClouds texture creation failed");
      DestroyCloudTextures();
      return;
    }
  }
  cloud_texture_grid_size_ = grid_size;
}

void CelestialRenderer::SetGlareEnabled(const bool enabled) {
  glare_enabled_ = enabled;
}

void CelestialRenderer::SetZoneSkybox(const world::ZoneSkyboxEntry& entry) {
  zone_skybox_ = entry;
  has_zone_skybox_ = true;
}

void CelestialRenderer::ClearZoneSkybox() {
  has_zone_skybox_ = false;
  zone_skybox_ = {};
}

bool CelestialRenderer::HasZoneSkybox() const { return has_zone_skybox_; }

const world::ZoneSkyboxEntry& CelestialRenderer::GetZoneSkybox() const {
  return zone_skybox_;
}

void CelestialRenderer::Update() {

  auto& texture_manager = texture_manager_;
  if (!sun_glare_texture_.valid()) {
    sun_glare_texture_ = texture_manager.AcquireTextureAsync(
        openwow::game::kDayNightSunGlareTexturePath,
        TextureLoadFailurePolicy::kStrict, TextureLoadPriority::kDemand);
  }
  if (!moon_glare_texture_.valid()) {
    moon_glare_texture_ = texture_manager.AcquireTextureAsync(
        openwow::game::kDayNightMoonGlareTexturePath,
        TextureLoadFailurePolicy::kStrict, TextureLoadPriority::kDemand);
  }

  const std::uint32_t grid_size = openwow::game::DayNight_GetCloudLayerGridSize();
  EnsureCloudTextures(grid_size);

  const std::vector<std::uint32_t>& texels = openwow::game::DayNight_GetCloudLayerTexels();
  for (std::uint32_t slot = 0; slot < cloud_textures_.size(); ++slot) {
    const openwow::game::DayNightCloudTextureUpload upload =
        openwow::game::DayNight_TakeCloudLayerUpload(slot);
    if (upload.empty() || upload.gridSize != cloud_texture_grid_size_ ||
        !bgfx::isValid(cloud_textures_[slot])) {
      continue;
    }

    const std::size_t first_texel =
        static_cast<std::size_t>(upload.firstRow) * upload.gridSize;
    const std::size_t texel_count =
        static_cast<std::size_t>(upload.endRow - upload.firstRow) * upload.gridSize;
    if (first_texel + texel_count > texels.size()) {
      continue;
    }

    const bgfx::Memory* mem =
        bgfx::copy(texels.data() + first_texel,
                   static_cast<uint32_t>(texel_count * sizeof(std::uint32_t)));
    bgfx::updateTexture2D(cloud_textures_[slot], 0, 0, 0,
                          static_cast<uint16_t>(upload.firstRow),
                          static_cast<uint16_t>(upload.gridSize),
                          static_cast<uint16_t>(upload.endRow - upload.firstRow), mem);
  }
}

namespace {

void BuildBillboardQuad(CelestialRenderer::CelestialVertex verts[4],
                        const float position[3],
                        const float right[3], const float up[3],
                        float half_size,
                        uint32_t color,
                        float texcoord_u0, float texcoord_v0,
                        float texcoord_u1, float texcoord_v1) {

  float corners[4][3] = {
      {-half_size, -half_size, 0.0f},
      { half_size, -half_size, 0.0f},
      { half_size,  half_size, 0.0f},
      {-half_size,  half_size, 0.0f},
  };

  float texcoords[4][2] = {
      {texcoord_u0, texcoord_v1},
      {texcoord_u1, texcoord_v1},
      {texcoord_u1, texcoord_v0},
      {texcoord_u0, texcoord_v0},
  };

  for (int i = 0; i < 4; ++i) {

    verts[i].x = position[0] + corners[i][0] * right[0] + corners[i][1] * up[0];
    verts[i].y = position[1] + corners[i][0] * right[1] + corners[i][1] * up[1];
    verts[i].z = position[2] + corners[i][0] * right[2] + corners[i][1] * up[2];
    verts[i].abgr = color;
    verts[i].u = texcoords[i][0];
    verts[i].v = texcoords[i][1];
  }
}

constexpr uint16_t kQuadIndices[6] = {0, 1, 2, 0, 2, 3};

}

void CelestialRenderer::Render(uint8_t view_id, const float* view_mtx,
                                const float* proj_mtx, float camera_x,
                                float camera_y, float camera_z,
                                bgfx::Encoder* const encoder) {
  if (!initialized_) return;
  if (!bgfx::isValid(program_)) return;

  const DrawEncoder draw{encoder};

  bgfx::setViewTransform(view_id, view_mtx, proj_mtx);

  float view_right[3], view_up[3];
  view_right[0] = view_mtx[0];
  view_right[1] = view_mtx[4];
  view_right[2] = view_mtx[8];
  view_up[0] = view_mtx[1];
  view_up[1] = view_mtx[5];
  view_up[2] = view_mtx[9];

  const auto submit_textured_billboard =
      [&](const openwow::game::DayNightBillboardSubmission& submission) {
        if (submission.texturePath == nullptr ||
            submission.halfExtent <= 0.0f) {
          return;
        }

        TextureLease texture;
        const std::string_view texture_path(submission.texturePath);
        if (texture_path == openwow::game::kDayNightSunGlareTexturePath) {
          texture = sun_glare_texture_;
        } else if (texture_path ==
                   openwow::game::kDayNightMoonGlareTexturePath) {
          texture = moon_glare_texture_;
        } else {
          texture = texture_manager_.AcquireCachedTexture(
              submission.texturePath);
        }
        if (!texture.valid() ||
            bgfx::getAvailTransientVertexBuffer(4u, vertex_layout_) < 4u ||
            bgfx::getAvailTransientIndexBuffer(6u) < 6u) {
          return;
        }

        const float position[3] = {
            submission.position.x,
            submission.position.y,
            submission.position.z,
        };
        CelestialVertex vertices[4];
        BuildBillboardQuad(
            vertices, position, view_right, view_up, submission.halfExtent,
            PackColor(submission.colorRgba[0], submission.colorRgba[1],
                      submission.colorRgba[2], submission.colorRgba[3]),
            0.0f, 0.0f, 1.0f, 1.0f);

        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;
        bgfx::allocTransientVertexBuffer(&tvb, 4u, vertex_layout_);
        bgfx::allocTransientIndexBuffer(&tib, 6u);

        if (tvb.size < sizeof(vertices) || tib.size < sizeof(kQuadIndices)) {
          return;
        }
        std::memcpy(tvb.data, vertices, sizeof(vertices));
        std::memcpy(tib.data, kQuadIndices, sizeof(kQuadIndices));

        const float texture_params[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        draw.setUniform(u_celestial_params_, texture_params);
        draw.setTexture(0, s_tex_color_,
                        BgfxTextureLeaseAccess::Get(texture),
                        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        draw.setVertexBuffer(0, &tvb);
        draw.setIndexBuffer(&tib);

        std::uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                              BGFX_STATE_MSAA;
        state |= submission.blendMode ==
                         openwow::game::DayNightBillboardBlendMode::Additive
                     ? kGlareAdditiveBlend
                     : kGlareAlphaBlend;
        draw.setState(state);
        draw.submit(view_id, program_);
      };

  std::array<openwow::game::DayNightBillboardSubmission, 3> disc_billboards{};
  const std::size_t disc_count =
      openwow::game::DayNight_UpdateCelestialDiscBillboards(
          disc_billboards.data(), disc_billboards.size());
  for (std::size_t index = 0;
       index < std::min(disc_count, disc_billboards.size()); ++index) {
    submit_textured_billboard(disc_billboards[index]);
  }

  std::array<openwow::game::DayNightBillboardSubmission, 2>
      glare_billboards{};
  const std::size_t glare_count =
      openwow::game::DayNight_UpdateGlareBillboards(
          glare_billboards.data(), glare_billboards.size());
  for (std::size_t index = 0;
       index < std::min(glare_count, glare_billboards.size()); ++index) {
    submit_textured_billboard(glare_billboards[index]);
  }

  SubmitCloudDome(view_id, camera_x, camera_y, camera_z, draw);
}

void CelestialRenderer::SubmitCloudDome(const uint8_t view_id, const float camera_x,
                                        const float camera_y, const float camera_z,
                                        const DrawEncoder& draw) {
  const std::uint32_t active_index =
      openwow::game::DayNight_GetCloudLayerActiveTextureIndex();
  if (!openwow::game::DayNight_IsCloudLayerEnabled() ||
      active_index >= cloud_textures_.size() ||
      !bgfx::isValid(cloud_textures_[active_index])) {
    return;
  }

  const openwow::game::DayNightCloudDomeMesh& mesh =
      openwow::game::DayNight_GetCloudDomeMesh();
  const std::size_t vertex_count = mesh.positions.size();
  const std::size_t index_count = mesh.indices.size();
  if (vertex_count == 0u || index_count == 0u ||
      mesh.texcoords.size() != vertex_count || mesh.colors.size() != vertex_count) {
    return;
  }
  if (bgfx::getAvailTransientVertexBuffer(static_cast<uint32_t>(vertex_count),
                                          vertex_layout_) < vertex_count ||
      bgfx::getAvailTransientIndexBuffer(static_cast<uint32_t>(index_count)) <
          index_count) {
    return;
  }

  bgfx::TransientVertexBuffer tvb;
  bgfx::TransientIndexBuffer tib;
  bgfx::allocTransientVertexBuffer(&tvb, static_cast<uint32_t>(vertex_count),
                                   vertex_layout_);
  bgfx::allocTransientIndexBuffer(&tib, static_cast<uint32_t>(index_count));

  if (tvb.stride == 0u || tvb.size / tvb.stride < vertex_count ||
      tib.size / sizeof(std::uint16_t) < index_count) {
    return;
  }

  auto* vertices = reinterpret_cast<CelestialVertex*>(tvb.data);
  for (std::size_t index = 0; index < vertex_count; ++index) {
    vertices[index].x = mesh.positions[index].x;
    vertices[index].y = mesh.positions[index].y;
    vertices[index].z = mesh.positions[index].z;

    vertices[index].abgr = mesh.colors[index];
    vertices[index].u = mesh.texcoords[index].x;
    vertices[index].v = mesh.texcoords[index].y;
  }
  std::memcpy(tib.data, mesh.indices.data(),
              index_count * sizeof(std::uint16_t));

  const float texture_params[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  draw.setUniform(u_celestial_params_, texture_params);
  draw.setTexture(0, s_tex_color_, cloud_textures_[active_index],
                  BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);

  float model[16] = {};
  model[0] = kSkyPassUniformScale;
  model[5] = kSkyPassUniformScale;
  model[10] = kSkyPassUniformScale;
  model[15] = 1.0f;
  model[12] = camera_x;
  model[13] = camera_y;
  model[14] = camera_z;
  draw.setTransform(model);

  draw.setVertexBuffer(0, &tvb);
  draw.setIndexBuffer(&tib);
  draw.setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA |
                BGFX_STATE_PT_TRISTRIP | kGlareAlphaBlend);
  draw.submit(view_id, program_);
}

void CelestialRenderer::Reset() {
  glare_enabled_ = true;
  has_zone_skybox_ = false;
  zone_skybox_ = {};
}

}

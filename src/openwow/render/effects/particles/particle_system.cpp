#include "openwow/render/effects/particles/particle_system.h"

#include "openwow/render/resources/shaders/shader_registry.h"
#include "openwow/foundation/diagnostics/logging.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace openwow::render {

namespace {

thread_local std::mt19937 g_rng{std::random_device{}()};

float RandFloat(float lo, float hi) {
  std::uniform_real_distribution<float> d(lo, hi);
  return d(g_rng);
}

float RandFloat01() { return RandFloat(0.0f, 1.0f); }

float Lerp3(const std::array<float, 3>& vals,
            const std::array<float, 3>& times, float t) {
  if (t <= times[0]) return vals[0];
  if (t >= times[2]) return vals[2];
  if (t <= times[1]) {
    float f = (t - times[0]) / std::max(times[1] - times[0], 1e-6f);
    return vals[0] + (vals[1] - vals[0]) * f;
  }
  float f = (t - times[1]) / std::max(times[2] - times[1], 1e-6f);
  return vals[1] + (vals[2] - vals[1]) * f;
}

uint32_t LerpColor3(const std::array<uint32_t, 3>& cols,
                    const std::array<float, 3>& times, float t) {
  auto unpack = [](uint32_t c, int shift) -> float {
    return static_cast<float>((c >> shift) & 0xFF) / 255.0f;
  };
  auto pack = [](float v, int shift) -> uint32_t {
    auto b = static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f);
    return static_cast<uint32_t>(b) << shift;
  };

  const uint32_t* c0;
  const uint32_t* c1;
  float f;
  if (t <= times[0]) {
    return cols[0];
  } else if (t >= times[2]) {
    return cols[2];
  } else if (t <= times[1]) {
    c0 = &cols[0];
    c1 = &cols[1];
    f = (t - times[0]) / std::max(times[1] - times[0], 1e-6f);
  } else {
    c0 = &cols[1];
    c1 = &cols[2];
    f = (t - times[1]) / std::max(times[2] - times[1], 1e-6f);
  }

  float r = unpack(*c0, 0) + (unpack(*c1, 0) - unpack(*c0, 0)) * f;
  float g = unpack(*c0, 8) + (unpack(*c1, 8) - unpack(*c0, 8)) * f;
  float b = unpack(*c0, 16) + (unpack(*c1, 16) - unpack(*c0, 16)) * f;
  float a = unpack(*c0, 24) + (unpack(*c1, 24) - unpack(*c0, 24)) * f;

  return pack(r, 0) | pack(g, 8) | pack(b, 16) | pack(a, 24);
}

void Normalise3(float v[3]) {
  float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (len > 1e-8f) {
    float inv = 1.0f / len;
    v[0] *= inv;
    v[1] *= inv;
    v[2] *= inv;
  }
}

}

struct ParticleVertex {
  float position[3];
  uint32_t abgr;
  float texcoord[2];
};

void ParticleEmitter::Configure(const ParticleEmitterConfig& config) {
  config_ = config;
  particles_.clear();
  particles_.reserve(config_.max_particles);
  emit_accumulator_ = 0.0f;
}

void ParticleEmitter::SetPosition(float x, float y, float z) {
  pos_[0] = x;
  pos_[1] = y;
  pos_[2] = z;
}

void ParticleEmitter::SetDirection(float dx, float dy, float dz) {
  dir_[0] = dx;
  dir_[1] = dy;
  dir_[2] = dz;
  Normalise3(dir_);
}

void ParticleEmitter::SpawnParticle() {
  if (particles_.size() >= config_.max_particles) return;

  Particle p{};

  switch (config_.shape) {
    case EmitterShape::kPlane: {
      float ox = RandFloat(-config_.area_width * 0.5f,
                           config_.area_width * 0.5f);
      float oz = RandFloat(-config_.area_height * 0.5f,
                           config_.area_height * 0.5f);
      p.position[0] = pos_[0] + ox;
      p.position[1] = pos_[1];
      p.position[2] = pos_[2] + oz;
      break;
    }
    case EmitterShape::kSphere: {

      float theta = RandFloat(0.0f, 2.0f * 3.14159265f);
      float phi = std::acos(RandFloat(-1.0f, 1.0f));
      float r = config_.area_width * std::cbrt(RandFloat01());
      float sp = std::sin(phi);
      p.position[0] = pos_[0] + r * sp * std::cos(theta);
      p.position[1] = pos_[1] + r * std::cos(phi);
      p.position[2] = pos_[2] + r * sp * std::sin(theta);
      break;
    }
    case EmitterShape::kSpline:

      p.position[0] = pos_[0];
      p.position[1] = pos_[1];
      p.position[2] = pos_[2];
      break;
  }

  float speed = config_.emission_speed *
                (1.0f + RandFloat(-config_.speed_variation,
                                  config_.speed_variation));

  float spread_x = RandFloat(-0.3f, 0.3f);
  float spread_z = RandFloat(-0.3f, 0.3f);
  p.velocity[0] = (dir_[0] + spread_x) * speed;
  p.velocity[1] = (dir_[1]) * speed;
  p.velocity[2] = (dir_[2] + spread_z) * speed;

  float life = config_.lifespan *
               (1.0f + RandFloat(-config_.lifespan_variation,
                                 config_.lifespan_variation));
  life = std::max(life, 0.05f);
  p.life = life;
  p.max_life = life;
  p.size = config_.scales[0];

  particles_.push_back(p);
}

void ParticleEmitter::Update(float dt) {
  if (!active_ || dt <= 0.0f) return;

  emit_accumulator_ += dt * config_.emission_rate;
  uint32_t to_spawn = static_cast<uint32_t>(emit_accumulator_);
  emit_accumulator_ -= static_cast<float>(to_spawn);

  for (uint32_t i = 0; i < to_spawn; ++i) {
    SpawnParticle();
  }

  for (auto& p : particles_) {
    p.position[0] += p.velocity[0] * dt;
    p.position[1] += p.velocity[1] * dt;
    p.position[2] += p.velocity[2] * dt;

    p.velocity[1] -= config_.gravity * dt;

    p.life -= dt;

    float t = 1.0f - std::clamp(p.life / p.max_life, 0.0f, 1.0f);
    p.size = Lerp3(config_.scales, config_.color_times, t);
  }

  particles_.erase(
      std::remove_if(particles_.begin(), particles_.end(),
                     [](const Particle& p) { return p.life <= 0.0f; }),
      particles_.end());

  if (particles_.size() > config_.max_particles) {
    particles_.erase(particles_.begin(),
                     particles_.begin() +
                         static_cast<ptrdiff_t>(particles_.size() -
                                                config_.max_particles));
  }
}

ParticleSystem::ParticleSystem() = default;

ParticleSystem::~ParticleSystem() { Shutdown(); }

bool ParticleSystem::Initialize() {
  if (initialized_) return true;

  layout_.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .end();

  const auto type = bgfx::getRendererType();
  program_ = CreateEmbeddedProgram(ShaderProgramId::Particle, type);

  if (!bgfx::isValid(program_)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "ParticleSystem: shader program creation failed");
    return false;
  }

  constexpr uint32_t kMaxVerts = kMaxTotalParticles * 4;
  dvb_ = bgfx::createDynamicVertexBuffer(kMaxVerts, layout_,
                                          BGFX_BUFFER_ALLOW_RESIZE);
  if (!bgfx::isValid(dvb_)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "ParticleSystem: dynamic VB creation failed");
    bgfx::destroy(program_);
    program_ = BGFX_INVALID_HANDLE;
    return false;
  }

  constexpr uint32_t kMaxIndices = kMaxTotalParticles * 6;
  dib_ = bgfx::createDynamicIndexBuffer(kMaxIndices,
                                         BGFX_BUFFER_ALLOW_RESIZE);
  if (!bgfx::isValid(dib_)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "ParticleSystem: dynamic IB creation failed");
    bgfx::destroy(dvb_);
    bgfx::destroy(program_);
    dvb_ = BGFX_INVALID_HANDLE;
    program_ = BGFX_INVALID_HANDLE;
    return false;
  }

  u_fog_params_ = bgfx::createUniform("u_fogParams", bgfx::UniformType::Vec4);
  u_fog_color_  = bgfx::createUniform("u_fogColor",  bgfx::UniformType::Vec4);

  initialized_ = true;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "ParticleSystem: initialized");
  return true;
}

uint32_t ParticleSystem::CreateEmitter(const ParticleEmitterConfig& config) {
  uint32_t id = next_id_++;

  for (auto& slot : emitters_) {
    if (!slot.in_use) {
      slot.id = id;
      slot.in_use = true;
      slot.emitter.Configure(config);
      return id;
    }
  }

  emitters_.push_back({});
  auto& slot = emitters_.back();
  slot.id = id;
  slot.in_use = true;
  slot.emitter.Configure(config);
  return id;
}

ParticleEmitter* ParticleSystem::GetEmitter(uint32_t id) {
  for (auto& slot : emitters_) {
    if (slot.in_use && slot.id == id) return &slot.emitter;
  }
  return nullptr;
}

void ParticleSystem::RemoveEmitter(uint32_t id) {
  for (auto& slot : emitters_) {
    if (slot.in_use && slot.id == id) {
      slot.in_use = false;
      return;
    }
  }
}

void ParticleSystem::Update(float dt) {
  for (auto& slot : emitters_) {
    if (slot.in_use) {
      slot.emitter.Update(dt);
    }
  }
}

void ParticleSystem::Render(uint8_t view_id, const float* view_mtx,
                            const float* proj_mtx, float cam_x,
                            float cam_y, float cam_z) {
  if (!initialized_) return;

  batch_planner_.BeginFrame();

  batch_planner_.SetCameraPosition(cam_x, cam_y, cam_z);

  uint32_t submitted_particles = 0;
  for (const auto& slot : emitters_) {
    if (!slot.in_use) {
      continue;
    }

    const auto& cfg = slot.emitter.config();
    const auto& particles = slot.emitter.particles();
    for (std::size_t particle_index = 0;
         particle_index < particles.size() &&
         submitted_particles < kMaxTotalParticles;
         ++particle_index) {
      const auto& particle = particles[particle_index];
      ParticleRenderInstance instance{};
      instance.particleId = submitted_particles + 1u;
      instance.emitterId = slot.id;
      instance.emitterParticleIndex = static_cast<uint32_t>(particle_index);
      instance.posX = particle.position[0];
      instance.posY = particle.position[1];
      instance.posZ = particle.position[2];
      instance.blendMode = static_cast<uint32_t>(cfg.blend_mode);
      instance.renderStateId = cfg.no_z_buffer ? 1u : 0u;
      instance.sortLayer =
          cfg.blend_mode == ParticleBlendMode::kOpaque ? 0u : 1u;
      instance.type =
          cfg.world_space ? ParticleRenderType::WorldSpace
                          : ParticleRenderType::Billboard;
      batch_planner_.Submit(std::move(instance));
      ++submitted_particles;
    }
  }

  if (submitted_particles == 0) {
    return;
  }

  auto batches = batch_planner_.SortAndBatch();
  if (batches.empty()) {
    return;
  }

  bgfx::setViewTransform(view_id, view_mtx, proj_mtx);

  float right[3] = {view_mtx[0], view_mtx[4], view_mtx[8]};
  float up[3] = {view_mtx[1], view_mtx[5], view_mtx[9]};

  uint32_t vb_offset = 0;
  uint32_t ib_offset = 0;

  for (const auto& batch : batches) {
    const uint32_t remaining_capacity = kMaxTotalParticles - (vb_offset / 4);
    if (remaining_capacity == 0) {
      break;
    }

    std::vector<ParticleVertex> verts;
    std::vector<uint16_t> indices;
    verts.reserve(std::min<std::size_t>(batch.instances.size(), remaining_capacity) * 4u);
    indices.reserve(std::min<std::size_t>(batch.instances.size(), remaining_capacity) * 6u);

    uint16_t base_idx = 0;
    uint32_t emitted = 0;
    for (const auto& instance : batch.instances) {
      if (emitted >= remaining_capacity) {
        break;
      }

      auto* emitter = GetEmitter(instance.emitterId);
      if (!emitter) {
        continue;
      }

      const auto& particles = emitter->particles();
      if (instance.emitterParticleIndex >= particles.size()) {
        continue;
      }

      const auto& cfg = emitter->config();
      const auto& p = particles[instance.emitterParticleIndex];

      float t = 1.0f - std::clamp(p.life / p.max_life, 0.0f, 1.0f);

      uint32_t abgr = LerpColor3(cfg.colors, cfg.color_times, t);
      float sz = Lerp3(cfg.scales, cfg.color_times, t);
      sz = std::max(sz, 0.0f);

      float dx_r = right[0] * sz;
      float dy_r = right[1] * sz;
      float dz_r = right[2] * sz;
      float dx_u = up[0] * sz;
      float dy_u = up[1] * sz;
      float dz_u = up[2] * sz;

      const float px = p.position[0];
      const float py = p.position[1];
      const float pz = p.position[2];

      float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
      if (cfg.texture_rows > 1 || cfg.texture_cols > 1) {
        uint16_t cell = cfg.head_cell_begin;
        if (cfg.head_cell_end > cfg.head_cell_begin) {
          uint16_t range = cfg.head_cell_end - cfg.head_cell_begin;
          cell = cfg.head_cell_begin +
                 static_cast<uint16_t>(t * static_cast<float>(range));
          cell = std::min(cell, cfg.head_cell_end);
        }
        uint16_t row = cell / cfg.texture_cols;
        uint16_t col = cell % cfg.texture_cols;
        float inv_cols = 1.0f / static_cast<float>(cfg.texture_cols);
        float inv_rows = 1.0f / static_cast<float>(cfg.texture_rows);
        u0 = static_cast<float>(col) * inv_cols;
        v0 = static_cast<float>(row) * inv_rows;
        u1 = u0 + inv_cols;
        v1 = v0 + inv_rows;
      }

      ParticleVertex bl{};
      bl.position[0] = px - dx_r - dx_u;
      bl.position[1] = py - dy_r - dy_u;
      bl.position[2] = pz - dz_r - dz_u;
      bl.abgr = abgr;
      bl.texcoord[0] = u0;
      bl.texcoord[1] = v0;

      ParticleVertex br{};
      br.position[0] = px + dx_r - dx_u;
      br.position[1] = py + dy_r - dy_u;
      br.position[2] = pz + dz_r - dz_u;
      br.abgr = abgr;
      br.texcoord[0] = u1;
      br.texcoord[1] = v0;

      ParticleVertex tr{};
      tr.position[0] = px + dx_r + dx_u;
      tr.position[1] = py + dy_r + dy_u;
      tr.position[2] = pz + dz_r + dz_u;
      tr.abgr = abgr;
      tr.texcoord[0] = u1;
      tr.texcoord[1] = v1;

      ParticleVertex tl{};
      tl.position[0] = px - dx_r + dx_u;
      tl.position[1] = py - dy_r + dy_u;
      tl.position[2] = pz - dz_r + dz_u;
      tl.abgr = abgr;
      tl.texcoord[0] = u0;
      tl.texcoord[1] = v1;

      verts.push_back(bl);
      verts.push_back(br);
      verts.push_back(tr);
      verts.push_back(tl);

      indices.push_back(base_idx + 0);
      indices.push_back(base_idx + 1);
      indices.push_back(base_idx + 2);
      indices.push_back(base_idx + 0);
      indices.push_back(base_idx + 2);
      indices.push_back(base_idx + 3);

      base_idx += 4;
      ++emitted;
    }

    if (verts.empty()) continue;

    const bgfx::Memory* vmem = bgfx::copy(
        verts.data(),
        static_cast<uint32_t>(verts.size() * sizeof(ParticleVertex)));
    const bgfx::Memory* imem = bgfx::copy(
        indices.data(),
        static_cast<uint32_t>(indices.size() * sizeof(uint16_t)));
    bgfx::update(dvb_, vb_offset, vmem);
    bgfx::update(dib_, ib_offset, imem);

    uint64_t blend_state = 0;
    switch (static_cast<ParticleBlendMode>(batch.blendMode)) {
      case ParticleBlendMode::kOpaque:
        blend_state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                      BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
        break;
      case ParticleBlendMode::kAlphaKey:

        blend_state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                      BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS |
                      BGFX_STATE_ALPHA_REF(0x80);
        break;
      case ParticleBlendMode::kAlpha:
        blend_state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                      BGFX_STATE_DEPTH_TEST_LESS |
                      BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                            BGFX_STATE_BLEND_INV_SRC_ALPHA);
        break;
      case ParticleBlendMode::kAdd:
        blend_state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                      BGFX_STATE_DEPTH_TEST_LESS |
                      BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                            BGFX_STATE_BLEND_ONE);
        break;
      case ParticleBlendMode::kMod:
        blend_state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                      BGFX_STATE_DEPTH_TEST_LESS |
                      BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_DST_COLOR,
                                            BGFX_STATE_BLEND_ZERO);
        break;
      case ParticleBlendMode::kMod2x:
        blend_state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                      BGFX_STATE_DEPTH_TEST_LESS |
                      BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_DST_COLOR,
                                            BGFX_STATE_BLEND_SRC_COLOR);
        break;
      default:
        blend_state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                      BGFX_STATE_DEPTH_TEST_LESS |
                      BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,
                                            BGFX_STATE_BLEND_INV_SRC_ALPHA);
        break;
    }

    if (static_cast<ParticleBlendMode>(batch.blendMode) != ParticleBlendMode::kOpaque
        && static_cast<ParticleBlendMode>(batch.blendMode) != ParticleBlendMode::kAlphaKey) {
      blend_state &= ~BGFX_STATE_WRITE_Z;
    }

    if (batch.renderStateId != 0u) {
      blend_state &= ~BGFX_STATE_DEPTH_TEST_MASK;
    }

    blend_state |= BGFX_STATE_MSAA;

    bgfx::setVertexBuffer(0, dvb_, vb_offset,
                          static_cast<uint32_t>(verts.size()));
    bgfx::setIndexBuffer(dib_, ib_offset, static_cast<uint32_t>(indices.size()));
    bgfx::setUniform(u_fog_params_, fog_.params.data());
    bgfx::setUniform(u_fog_color_, fog_.color.data());
    bgfx::setState(blend_state);
    bgfx::submit(view_id, program_);

    vb_offset += static_cast<uint32_t>(verts.size());
    ib_offset += static_cast<uint32_t>(indices.size());
  }
}

void ParticleSystem::SetFogParams(const RenderFogState& fog) {
  fog_ = fog;
}

void ParticleSystem::ClearAll() {
  for (auto& slot : emitters_) {
    slot.in_use = false;
  }
  emitters_.clear();
}

void ParticleSystem::Shutdown() {
  ClearAll();

  if (bgfx::isValid(dib_)) bgfx::destroy(dib_);
  if (bgfx::isValid(dvb_)) bgfx::destroy(dvb_);
  if (bgfx::isValid(program_)) bgfx::destroy(program_);
  if (bgfx::isValid(u_fog_params_)) bgfx::destroy(u_fog_params_);
  if (bgfx::isValid(u_fog_color_))  bgfx::destroy(u_fog_color_);
  dib_ = BGFX_INVALID_HANDLE;
  dvb_ = BGFX_INVALID_HANDLE;
  program_ = BGFX_INVALID_HANDLE;
  u_fog_params_ = BGFX_INVALID_HANDLE;
  u_fog_color_ = BGFX_INVALID_HANDLE;
  initialized_ = false;
}

size_t ParticleSystem::emitter_count() const {
  size_t n = 0;
  for (const auto& slot : emitters_) {
    if (slot.in_use) ++n;
  }
  return n;
}

size_t ParticleSystem::total_particle_count() const {
  size_t n = 0;
  for (const auto& slot : emitters_) {
    if (slot.in_use) {
      n += slot.emitter.particles().size();
    }
  }
  return n;
}

}

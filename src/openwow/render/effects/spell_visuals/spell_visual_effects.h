#pragma once

#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/spell_visual_pipeline.h"
#include "openwow/game/object_presentation_snapshot.h"
#include "openwow/render/effects/particles/particle_system.h"
#include "openwow/render/effects/projectiles/projectile_motion.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace openwow::game {
struct SpellGoVisualData;
}

namespace openwow::render {

enum class VisualPhase : std::uint8_t {
  kPrecast = 0,
  kCast,
  kImpact,
  kState,
  kStateDone,
  kChannel,
  kCasterImpact,
  kTargetImpact,
  kEffect,
};

struct SpellVisualEffect {
  std::uint32_t spell_id = 0;
  std::uint32_t spell_visual_kit_id = 0;
  VisualPhase phase = VisualPhase::kCast;
  float x = 0.0f, y = 0.0f, z = 0.0f;
  std::uint64_t source_guid = 0;
  std::uint64_t target_guid = 0;
  float elapsed = 0.0f;
  float duration = 0.0f;
  bool active = true;
  bool tracks_guid_position = true;

  std::uint32_t emitter_handle = 0;
  bool has_emitter = false;
};

struct SpellMissile {
  std::uint32_t spell_id = 0;
  std::uint64_t caster_guid = 0;
  std::uint64_t target_guid = 0;
  Vec3 start;
  Vec3 target;
  Vec3 current;
  Vec3 direction;
  float elapsed = 0.0f;
  float duration = 0.0f;
  std::uint32_t emitter_handle = 0;
  bool has_emitter = false;
  bool active = true;
  bool has_direction = false;
};

class SpellVisualEffects {
 public:
  SpellVisualEffects() = default;
  ~SpellVisualEffects() = default;

  SpellVisualEffects(const SpellVisualEffects&) = delete;
  SpellVisualEffects& operator=(const SpellVisualEffects&) = delete;

  bool Initialize(ParticleSystem* particles,
                  const game::ObjectPresentationSnapshot* objects);

  void BindDbc(const openwow::data::dbc::DbcLoader* dbc);
  void BindSoundKitSink(
      std::function<void(std::uint32_t, const float*)> sink) {
    sound_kit_sink_ = std::move(sink);
  }

  void Shutdown();

  void OnSpellStart(std::uint64_t caster_guid, std::uint32_t spell_id);

  void OnSpellGo(std::uint64_t caster_guid, std::uint32_t spell_id,
                 const game::SpellGoVisualData& visual_data);

  void OnPlaySpellVisual(std::uint64_t target_guid,
                         std::uint32_t spell_visual_kit_id);

  void OnPlaySpellImpact(std::uint64_t target_guid,
                         std::uint32_t spell_visual_kit_id);

  void Update(float dt);

  void Clear();

  [[nodiscard]] std::size_t active_effect_count() const { return effects_.size(); }
  [[nodiscard]] std::size_t active_missile_count() const { return missiles_.size(); }
  [[nodiscard]] bool UsesMissileVisual(std::uint32_t spell_id) const;

 private:

  std::uint32_t GetSpellVisualId(std::uint32_t spell_id) const;

  bool ResolveKit(std::uint32_t kit_id,
                  ParticleEmitterConfig& out_config) const;

  bool ResolveEffectName(std::uint32_t effect_name_id,
                         ParticleEmitterConfig& out_config) const;

  void PlaySpellVisualKitSound(std::uint32_t kit_id,
                               float x, float y, float z);

  std::optional<ParticleEmitterConfig> MakeConfigForPhase(std::uint32_t spell_id,
                                                          VisualPhase phase) const;

  bool GetObjectPosition(std::uint64_t guid,
                         float& x, float& y, float& z) const;

  std::uint32_t CreateEffectEmitter(const ParticleEmitterConfig& config,
                                    float x, float y, float z);

  void UpdateEffectPositions();

  void UpdateMissiles(float dt);

  void LaunchMissileToGuid(std::uint32_t spell_id, std::uint64_t caster_guid,
                           std::uint64_t target_guid,
                           const game::SpellGoVisualData& visual_data);
  void LaunchMissileToPoint(std::uint32_t spell_id, std::uint64_t caster_guid,
                            std::uint64_t target_guid,
                            const Vec3& target_position,
                            const game::SpellGoVisualData& visual_data);
  void SpawnImpactEffect(std::uint32_t spell_id, std::uint64_t caster_guid,
                         std::uint64_t target_guid, const Vec3& position);

  void CleanupExpired();

  ParticleSystem* particles_ = nullptr;
  const game::ObjectPresentationSnapshot* objects_ = nullptr;
  const openwow::data::dbc::DbcLoader* dbc_ = nullptr;
  std::function<void(std::uint32_t, const float*)> sound_kit_sink_;
  openwow::game::SpellVisualPipeline pipeline_{};

  std::vector<SpellVisualEffect> effects_;
  std::vector<SpellMissile> missiles_;

  bool initialized_ = false;
};

}

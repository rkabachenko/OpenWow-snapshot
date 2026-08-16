
#include "openwow/game/world_scene.h"

#include "openwow/audio/playback/sound_runtime.h"
#include "openwow/runtime/time/game_clock.h"
#include "openwow/data/formats/dbc/dbc_loader.h"
#include "openwow/game/objects/cgcorpse.h"
#include "openwow/game/objects/cggameobject.h"
#include "openwow/game/objects/cgunit.h"
#include "openwow/game/ceffect_c.h"
#include "openwow/game/gameobject_model_sound_callback.h"
#include "openwow/game/nameplate_damage_flash.h"
#include "openwow/game/player_control_runtime.h"
#include "openwow/game/spell_cast_runtime.h"
#include "openwow/game/spell_target_resolver.h"
#include "openwow/game/spell_c_internals.h"
#include "openwow/game/spell_visual_system.h"
#include "openwow/game/spell_visual_m2_event.h"
#include "openwow/game/world_session.h"
#include "openwow/game/world_environment_state.h"
#include "openwow/game/client_config.h"
#include "openwow/core/locale_system.h"
#include "openwow/platform/process/os_platform.h"
#include "openwow/render/m2/m2_system.h"
#include "openwow/render/m2/m2_transparent_draw_order.h"
#include "openwow/render/api/renderer_context.h"
#include "openwow/render/scene/blob_shadow.h"
#include "openwow/render/scene/m2_projected_texture_decal.h"
#include "openwow/ui/game/cvar_system.h"
#include "openwow/render/scene/chat_bubble.h"
#include "openwow/render/effects/projectiles/missile_trajectory_renderer.h"
#include "openwow/render/scene/nameplate_renderer.h"
#include "openwow/render/scene/unit_name_renderer.h"
#include "openwow/render/scene/object_renderer.h"
#include "openwow/game/world_presentation_publisher.h"
#include "openwow/render/effects/particles/particle_system.h"
#include "openwow/render/backend/bgfx/renderer_context_services.h"
#include "openwow/render/api/math/render_matrix_math.h"
#include "openwow/render/api/math/view_projection.h"
#include "openwow/render/models/characters/bowstring_renderer.h"
#include "openwow/render/scene/selection_circle.h"
#include "openwow/render/effects/spell_visuals/spell_visual_effects.h"
#include "openwow/render/effects/spell_visuals/spell_visual_renderer.h"
#include "openwow/render/world/water/water_particulate_system.h"
#include "openwow/render/world/environment/world_model_lighting.h"
#include "openwow/render/scene/world_frame.h"
#include "openwow/render/world/presentation/world_presentation_scene.h"
#include "openwow/world/coordinates/frustum.h"
#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/foundation/math/row_major_mat4x4.h"
#include "openwow/world/streaming/world_map.h"
#include "openwow/world/world_render_pipeline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace openwow::game {

namespace {

constexpr std::size_t kMaxQueuedSpellVisualM2Events = 4096u;
constexpr std::size_t kMaxQueuedSpellVisualImpacts = 4096u;
constexpr std::uint32_t kEffectSoundModeLoop = 0x00000001u;
constexpr std::uint32_t kEffectSoundDoNotBindToOwner = 0x00200000u;
constexpr std::uint32_t kLocalImpactPlaybackPriority = 110u;

}

void WorldScene::VisitDoodadCollisionTriangles(
    const std::array<float, 6>& world_bounds,
    const std::function<void(const render::DoodadCollisionTriangle&)>& visitor,
    const bool include_object_owned) const {
  world_presentation_scene_.VisitDoodadCollisionTriangles(world_bounds, visitor,
                                                          include_object_owned);
}

bool WorldScene::IsDoodadWorldEntryLoadDrained() const {
  return world_presentation_scene_.IsDoodadWorldEntryLoadDrained();
}

std::uint64_t WorldScene::DoodadCollisionRevision() const noexcept {
  return world_presentation_scene_.DoodadCollisionRevision();
}

struct WorldScene::RenderResources {
  RenderResources(render::TextureManager& texture_manager,
                  render::m2::M2System& m2_system,
                  render::SkySettingsProvider sky_settings)
      : world_presentation_scene(
            texture_manager, m2_system, std::move(sky_settings)),
        object_renderer(
            std::make_unique<render::ObjectRenderer>(
                texture_manager, m2_system)),
        presentation_publisher(object_renderer->equipment_renderer()),
        chat_bubble_presenter(texture_manager),
        missile_trajectory_renderer(texture_manager),
        water_particulates(texture_manager) {}

  render::WorldPresentationScene world_presentation_scene;
  render::ParticleSystem particles;
  render::SpellVisualEffects spell_visuals;
  render::SpellVisualRenderer spell_visual_renderer;
  std::unique_ptr<render::ObjectRenderer> object_renderer;
  WorldPresentationPublisher presentation_publisher;
  render::NameplateRenderer nameplate_renderer;
  render::UnitNameRenderer unit_name_renderer;
  render::ChatBubblePresenter chat_bubble_presenter;
  render::SelectionCircle selection_circle;
  render::BowstringRenderer bowstring;
  render::BlobShadowRenderer blob_shadows;
  render::M2ProjectedTextureDecalRenderer m2_projected_texture_decals;
  render::MissileTrajectoryRenderer missile_trajectory_renderer;
  render::WaterParticulateSystem water_particulates;
};

namespace {

constexpr float kHoursPerDay = 24.0f;
constexpr float kDestructibleInvalidGeoboxRadius = 50.0f;
constexpr float kDestructibleMinimumGeoboxRadius = 0.001f;

[[nodiscard]] std::optional<world::Bounds> ResolveGameObjectWmoWorldBounds(
    const data::dbc::DbcLoader &dbc, const render::AreaScenePresentationState &state,
    const std::uint32_t geobox_display_id, const bool destructible_building) {
  const auto *const display = dbc.gameobject_display_info().LookupEntry(geobox_display_id);
  if (display == nullptr) {
    return std::nullopt;
  }

  const world::Bounds local_bounds{
      display->min_x, display->min_y, display->min_z,
      display->max_x, display->max_y, display->max_z,
  };
  if (!std::all_of(local_bounds.begin(), local_bounds.end(),
                   [](const float value) { return std::isfinite(value); })) {
    return std::nullopt;
  }

  world::Bounds bounds = local_bounds;
  if (destructible_building) {

    const auto maximum_square = [](const float minimum, const float maximum) {
      return std::max(minimum * minimum, maximum * maximum);
    };
    float radius = std::sqrt(
        maximum_square(display->min_x, display->max_x) +
        maximum_square(display->min_y, display->max_y) +
        maximum_square(display->min_z, display->max_z));
    if (!std::isfinite(radius)) {
      return std::nullopt;
    }
    if (radius < kDestructibleMinimumGeoboxRadius) {
      radius = kDestructibleInvalidGeoboxRadius;
    }
    bounds = {-radius, -radius, -radius, radius, radius, radius};
  } else if (bounds[0] > bounds[3] || bounds[1] > bounds[4] || bounds[2] > bounds[5]) {
    return std::nullopt;
  }

  world::Bounds world_bounds{};
  math::row_major_mat4x4::TransformAABBByRowMajorAffine4x4(
      world_bounds.data(), bounds.data(), state.world_transform.data());
  return world_bounds;
}

[[nodiscard]] std::optional<float> ResolveModelLocalBoundsZExtent(
    const std::array<float, 6>& bounds) {
  if (!std::isfinite(bounds[2]) || !std::isfinite(bounds[5])) {
    return std::nullopt;
  }
  return bounds[5] - bounds[2];
}

[[nodiscard]] float Cubic(const float value) {
  return value * value * value;
}

struct DestructibleTransitionPresentation {
  std::array<float, 4> vertical_offsets_down{};
  std::array<bool, 4> visible{};
};

[[nodiscard]] DestructibleTransitionPresentation
BuildDestructibleTransitionPresentation(
    const CGGameObject_C::DestructibleVisualControlState& visual,
    const auto& transition,
    const std::uint32_t current_time_ms) {
  DestructibleTransitionPresentation presentation;
  if (visual.active_state_index < presentation.visible.size()) {
    presentation.visible[visual.active_state_index] = true;
  }
  if (!transition.active || transition.destination_state >= presentation.visible.size() ||
      transition.source_state >= presentation.visible.size()) {
    return presentation;
  }

  const float progress = transition.duration_ms == 0u
                             ? 1.0f
                             : std::clamp(
                                   static_cast<float>(current_time_ms - transition.start_time_ms) /
                                       static_cast<float>(transition.duration_ms),
                                   0.0f, 1.0f);
  presentation.visible.fill(false);
  presentation.visible[transition.destination_state] = true;
  presentation.vertical_offsets_down[transition.destination_state] =
      transition.mode == 0u
          ? (progress <= 0.5f ? transition.destination_height
                              : Cubic(2.0f - 2.0f * progress) *
                                    transition.destination_height)
          : Cubic(1.0f - progress) * transition.destination_height;

  if (transition.mode != 3u) {
    presentation.visible[transition.source_state] = true;
    if (transition.mode == 0u) {
      presentation.vertical_offsets_down[transition.source_state] =
          (progress < 0.5f ? 8.0f * Cubic(progress) : 1.0f) *
          transition.source_height;
    } else if (transition.mode == 2u) {
      presentation.vertical_offsets_down[transition.source_state] =
          Cubic(progress) * transition.source_height;
    }
  }
  return presentation;
}

[[nodiscard]] bool IsDestructibleTransitionEligible(
    const CGGameObject_C::DestructibleVisualControlState& visual) {

  const std::int32_t transition_speed =
      static_cast<std::int32_t>(visual.rebuild_transition_speed);
  return visual.previous_active_state_index >= 0 &&
         visual.previous_active_state_index !=
             static_cast<std::int8_t>(visual.active_state_index) &&
         (visual.active_state_index == 0u || visual.active_state_index == 3u) &&
         visual.rebuild_transition_mode != 4u && transition_speed > 0;
}

std::optional<float> IntersectCameraFacet(
    const std::array<float, 3>& origin,
    const std::array<float, 3>& direction,
    const openwow::world::CollisionFacetView& facet) {
  const auto& v0 = facet.vertices[0];
  const auto& v1 = facet.vertices[1];
  const auto& v2 = facet.vertices[2];
  const std::array<float, 3> edge1{v1[0] - v0[0], v1[1] - v0[1],
                                   v1[2] - v0[2]};
  const std::array<float, 3> edge2{v2[0] - v0[0], v2[1] - v0[1],
                                   v2[2] - v0[2]};
  const std::array<float, 3> p{
      direction[1] * edge2[2] - direction[2] * edge2[1],
      direction[2] * edge2[0] - direction[0] * edge2[2],
      direction[0] * edge2[1] - direction[1] * edge2[0]};
  const float determinant = edge1[0] * p[0] + edge1[1] * p[1] +
                            edge1[2] * p[2];
  constexpr float kEpsilon = 1.0e-7f;
  if (std::abs(determinant) < kEpsilon) {
    return std::nullopt;
  }
  const float inverse = 1.0f / determinant;
  const std::array<float, 3> offset{origin[0] - v0[0], origin[1] - v0[1],
                                    origin[2] - v0[2]};
  const float u = (offset[0] * p[0] + offset[1] * p[1] +
                   offset[2] * p[2]) * inverse;
  if (u < 0.0f || u > 1.0f) {
    return std::nullopt;
  }
  const std::array<float, 3> q{
      offset[1] * edge1[2] - offset[2] * edge1[1],
      offset[2] * edge1[0] - offset[0] * edge1[2],
      offset[0] * edge1[1] - offset[1] * edge1[0]};
  const float v = (direction[0] * q[0] + direction[1] * q[1] +
                   direction[2] * q[2]) * inverse;
  if (v < 0.0f || u + v > 1.0f) {
    return std::nullopt;
  }
  const float distance = (edge2[0] * q[0] + edge2[1] * q[1] +
                          edge2[2] * q[2]) * inverse;
  return distance >= 0.0f ? std::optional<float>{distance} : std::nullopt;
}

struct CameraClearancePrism {
  std::array<float, 3> eye{};
  std::array<float, 3> axis{};
  std::array<float, 3> right{};
  std::array<float, 3> up{};
  float half_width = 0.0f;
  float half_height = 0.0f;
  float near_plane = 0.0f;
  float arm = 0.0f;
  float proj_near = 0.0f;
  float proj_far = 0.0f;
  std::array<float, 6> world_bounds{};
};

constexpr float kCameraClearanceQuadScale = 1.75f;

std::optional<CameraClearancePrism> BuildCameraClearancePrism(
    const std::array<float, 3>& pivot, const std::array<float, 3>& eye,
    const float near_plane, const float arm, const float vertical_fov,
    const float aspect, const float far_plane) {
  if (!(arm > near_plane) || vertical_fov <= 0.0f || aspect <= 0.0f ||
      !(far_plane > near_plane)) {
    return std::nullopt;
  }
  CameraClearancePrism prism{};
  prism.eye = eye;
  prism.near_plane = near_plane;
  prism.arm = arm;
  prism.proj_near = near_plane;
  prism.proj_far = far_plane;
  prism.axis = {pivot[0] - eye[0], pivot[1] - eye[1], pivot[2] - eye[2]};
  const float axis_length = std::sqrt(prism.axis[0] * prism.axis[0] +
                                      prism.axis[1] * prism.axis[1] +
                                      prism.axis[2] * prism.axis[2]);
  if (axis_length < 1.0e-4f) {
    return std::nullopt;
  }
  for (auto& component : prism.axis) component /= axis_length;

  std::array<float, 3> up_seed{0.0f, 0.0f, 1.0f};
  if (std::fabs(prism.axis[2]) > 0.999f) {
    up_seed = {1.0f, 0.0f, 0.0f};
  }
  prism.right = {prism.axis[1] * up_seed[2] - prism.axis[2] * up_seed[1],
                 prism.axis[2] * up_seed[0] - prism.axis[0] * up_seed[2],
                 prism.axis[0] * up_seed[1] - prism.axis[1] * up_seed[0]};
  const float right_length = std::sqrt(prism.right[0] * prism.right[0] +
                                       prism.right[1] * prism.right[1] +
                                       prism.right[2] * prism.right[2]);
  if (right_length < 1.0e-6f) {
    return std::nullopt;
  }
  for (auto& component : prism.right) component /= right_length;
  prism.up = {prism.right[1] * prism.axis[2] - prism.right[2] * prism.axis[1],
              prism.right[2] * prism.axis[0] - prism.right[0] * prism.axis[2],
              prism.right[0] * prism.axis[1] - prism.right[1] * prism.axis[0]};

  const float tan_half_v = std::tan(vertical_fov * 0.5f);
  prism.half_height = tan_half_v * near_plane * kCameraClearanceQuadScale;
  prism.half_width =
      tan_half_v * aspect * near_plane * kCameraClearanceQuadScale;

  prism.world_bounds = {std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::max(),
                        std::numeric_limits<float>::lowest(),
                        std::numeric_limits<float>::lowest(),
                        std::numeric_limits<float>::lowest()};
  for (const float depth : {near_plane, arm}) {
    for (const float sx : {-1.0f, 1.0f}) {
      for (const float sy : {-1.0f, 1.0f}) {
        for (std::size_t component = 0; component < 3; ++component) {
          const float value =
              eye[component] + prism.axis[component] * depth +
              prism.right[component] * (sx * prism.half_width) +
              prism.up[component] * (sy * prism.half_height);
          prism.world_bounds[component] =
              std::min(prism.world_bounds[component], value);
          prism.world_bounds[component + 3] =
              std::max(prism.world_bounds[component + 3], value);
        }
      }
    }
  }
  return prism;
}

float PrismPenetrationFraction(const CameraClearancePrism& prism,
                               const openwow::world::CollisionFacetView& facet) {

  std::array<std::array<float, 3>, 9> polygon{};
  std::array<std::array<float, 3>, 9> scratch{};
  std::size_t count = 3;
  for (std::size_t index = 0; index < 3; ++index) {
    const auto& vertex = facet.vertices[index];
    const std::array<float, 3> offset{vertex[0] - prism.eye[0],
                                      vertex[1] - prism.eye[1],
                                      vertex[2] - prism.eye[2]};
    polygon[index] = {
        offset[0] * prism.axis[0] + offset[1] * prism.axis[1] +
            offset[2] * prism.axis[2],
        offset[0] * prism.right[0] + offset[1] * prism.right[1] +
            offset[2] * prism.right[2],
        offset[0] * prism.up[0] + offset[1] * prism.up[1] +
            offset[2] * prism.up[2]};
  }

  struct Slab {
    std::size_t component;
    float sign;
    float bound;
  };
  const std::array<Slab, 6> slabs{{{0, -1.0f, -prism.near_plane},
                                   {0, 1.0f, prism.arm},
                                   {1, 1.0f, prism.half_width},
                                   {1, -1.0f, prism.half_width},
                                   {2, 1.0f, prism.half_height},
                                   {2, -1.0f, prism.half_height}}};
  for (const auto& slab : slabs) {
    std::size_t kept = 0;
    for (std::size_t index = 0; index < count; ++index) {
      const auto& current = polygon[index];
      const auto& next = polygon[(index + 1u) % count];
      const float current_distance =
          slab.bound - slab.sign * current[slab.component];
      const float next_distance =
          slab.bound - slab.sign * next[slab.component];
      if (current_distance >= 0.0f) {
        if (kept < scratch.size()) scratch[kept++] = current;
      }
      if ((current_distance >= 0.0f) != (next_distance >= 0.0f)) {
        const float span = current_distance - next_distance;
        if (std::fabs(span) > 1.0e-9f && kept < scratch.size()) {
          const float fraction = current_distance / span;
          scratch[kept++] = {
              current[0] + (next[0] - current[0]) * fraction,
              current[1] + (next[1] - current[1]) * fraction,
              current[2] + (next[2] - current[2]) * fraction};
        }
      }
    }
    count = kept;
    if (count == 0) {
      return 0.0f;
    }
    polygon = scratch;
  }

  const float near_plus_far = prism.proj_near + prism.proj_far;
  const float two_near_far = 2.0f * prism.proj_near * prism.proj_far;
  const float inv_range = 1.0f / (prism.proj_far - prism.proj_near);
  float max_fraction = 0.0f;
  for (std::size_t index = 0; index < count; ++index) {
    const float axial = polygon[index][0];
    if (axial <= 0.0f) {
      continue;
    }
    const float ndc = (near_plus_far - two_near_far / axial) * inv_range;
    max_fraction = std::max(max_fraction, std::min(ndc, 1.0f));
  }
  return max_fraction;
}

bool HasActiveTargetingSpell(const WorldSession& session) {
  const auto targeting = session.spells().GetTargeting().GetState();
  return targeting.isActive && targeting.spellId != 0u;
}

bool ActiveSpellAllowsFlaggedUnitTargets(
    const WorldSession& session) {
  const auto targeting = session.spells().GetTargeting().GetState();
  if (!targeting.isActive || targeting.spellId == 0u) {
    return false;
  }

  const auto* const dbc = session.GetDbcLoader();
  if (dbc == nullptr) {
    return false;
  }

  const auto* const spell = dbc->spell().LookupEntry(targeting.spellId);
  if (spell == nullptr) {
    return false;
  }

  constexpr std::uint32_t kAttrExAllowsFlaggedUnitTargets = 0x10000000u;
  constexpr std::uint32_t kAttrEx2AllowsFlaggedUnitTargets = 0x00200000u;
  return (spell->attributes_ex & kAttrExAllowsFlaggedUnitTargets) != 0u ||
         (spell->attributes_ex2 & kAttrEx2AllowsFlaggedUnitTargets) != 0u;
}

}

float NormalizeWorldHourOfDay(float hour_of_day) {
  hour_of_day = std::fmod(hour_of_day, kHoursPerDay);
  if (hour_of_day < 0.0f) {
    hour_of_day += kHoursPerDay;
  }
  return hour_of_day;
}

float WorldHourOfDayToNormalizedTime(const float hour_of_day) {
  return NormalizeWorldHourOfDay(hour_of_day) / kHoursPerDay;
}

WorldSceneRenderCamera BuildWorldSceneRenderCamera(
    const float* const row_major_view_matrix,
    const float camera_x, const float camera_y, const float camera_z) {
  WorldSceneRenderCamera camera{
      .position = {camera_x, camera_y, camera_z},
  };
  if (row_major_view_matrix == nullptr) {
    return camera;
  }

  camera.forward = {
      row_major_view_matrix[2],
      row_major_view_matrix[6],
      row_major_view_matrix[10],
  };
  const float length_squared =
      camera.forward[0] * camera.forward[0] +
      camera.forward[1] * camera.forward[1] +
      camera.forward[2] * camera.forward[2];
  if (length_squared <= 1.0e-6f) {
    camera.forward = {1.0f, 0.0f, 0.0f};
    return camera;
  }

  const float inverse_length = 1.0f / std::sqrt(length_squared);
  for (float& component : camera.forward) {
    component *= inverse_length;
  }
  return camera;
}

WorldScene::WorldScene(render::TextureManager& texture_manager,
                       render::m2::M2System& m2_system,
                       render::WorldFrame& world_frame,
                       WorldEnvironmentState& world_environment,
                       render::SkySettingsProvider sky_settings,
                       openwow::audio::SoundRuntime& sound_runtime)
    : world_map_owner_(std::make_unique<world::WorldMap>(
          &sound_runtime)),
      world_map_(*world_map_owner_),
      sound_runtime_(sound_runtime),
      texture_manager_(texture_manager),
      m2_system_(m2_system),
      world_frame_(world_frame),
      world_environment_(world_environment),
      render_resources_(std::make_unique<RenderResources>(
          texture_manager, m2_system_, std::move(sky_settings))),
      world_presentation_scene_(
          render_resources_->world_presentation_scene),
      particles_(render_resources_->particles),
      spell_visuals_(render_resources_->spell_visuals),
      spell_visual_renderer_(render_resources_->spell_visual_renderer),
      object_renderer_(render_resources_->object_renderer),
      nameplate_renderer_(render_resources_->nameplate_renderer),
      unit_name_renderer_(render_resources_->unit_name_renderer),
      chat_bubble_presenter_(render_resources_->chat_bubble_presenter),
      selection_circle_(render_resources_->selection_circle),
      bowstring_renderer_(render_resources_->bowstring),
      blob_shadows_(render_resources_->blob_shadows),
      m2_projected_texture_decals_(
          render_resources_->m2_projected_texture_decals),
      missile_trajectory_renderer_(
          render_resources_->missile_trajectory_renderer),
      water_particulates_(render_resources_->water_particulates) {
  world_audio_position_sink_ =
      [this](const std::uint32_t map_id, const float x, const float y) {
        sound_runtime_.UpdateChunkAudioForPlayerPosition(map_id, x, y);
      };
  const auto sound_kit_sink = [this](const std::uint32_t sound_kit_id,
                                     const float* position) {
    (void)sound_runtime_.PlaySoundKit(sound_kit_id, position);
  };
  const auto missile_sound_start_sink =
      [this](const std::uint32_t sound_kit_id, const float* position) {
    std::uint32_t handle = 0u;
    (void)sound_runtime_.PlaySoundKit(sound_kit_id, position, &handle);
    return handle;
  };
  const auto effect_sound_start_sink =
      [this](const std::uint32_t sound_kit_id, const float* position,
             const render::SpellSoundPlaybackMode playback_mode,
             const std::uint64_t bind_owner_guid) {
        audio::SoundKitPlaybackOptions options{};
        switch (playback_mode) {
          case render::SpellSoundPlaybackMode::kForceLoop:
            options.loop_mode = audio::SoundLoopMode::kForceLoop;
            break;
          case render::SpellSoundPlaybackMode::kForceOneShot:
            options.loop_mode = audio::SoundLoopMode::kForceOneShot;
            break;
          case render::SpellSoundPlaybackMode::kUseSoundKit:
            break;
        }
        std::uint32_t handle = 0u;
        if (sound_runtime_.PlaySoundKit(sound_kit_id, position, &handle,
                                        options) != 0) {
          return std::uint32_t{0};
        }
        if (bind_owner_guid != 0u) {
          (void)sound_runtime_.BindSoundHandleToObjectGuid(handle,
                                                           bind_owner_guid);
        }
        return handle;
      };
  spell_visuals_.BindSoundKitSink(sound_kit_sink);
  spell_visual_renderer_.BindSoundKitSink(sound_kit_sink);
  spell_visual_renderer_.BindMissileSoundStartSink(
      missile_sound_start_sink);
  spell_visual_renderer_.BindEffectSoundStartSink(
      effect_sound_start_sink);
  spell_visual_renderer_.BindSoundPositionSink(
      [this](const std::uint32_t handle, const float* position) {
        (void)sound_runtime_.SetSoundHandlePosition(handle, position);
      });
  spell_visual_renderer_.BindSoundStopSink(
      [this](const std::uint32_t handle, const float fade_seconds) {
        (void)sound_runtime_.StopActiveSoundHandle(
            handle, false, fade_seconds, true);
      });
  spell_visual_renderer_.BindGroundHeightSink(
      [this](const float x, const float y, const float z) {
        return world_environment_.QuerySupportSurfaceHeight(x, y, z);
      });
  spell_visual_renderer_.BindCameraShakeSink(
      [this](const std::uint32_t shake_id,
             const std::array<float, 3>& position) {
        camera_.TriggerCameraShake(shake_id, position);
      });
  spell_visual_renderer_.BindM2EventSink(
      [this](const render::SpellVisualM2PresentationEvent& event) {
        if (spell_visual_m2_events_.size() >=
            kMaxQueuedSpellVisualM2Events) {
          spell_visual_m2_events_.pop_front();
          if (!spell_visual_m2_event_overflow_reported_) {
            spell_visual_m2_event_overflow_reported_ = true;
            diagnostics::Log(
                diagnostics::LogLevel::kWarn,
                "WorldScene: spell visual M2 event queue overflow; "
                "discarding oldest stale event");
          }
        }
        spell_visual_m2_events_.push_back(event);
      });
  spell_visual_renderer_.BindM2InstanceRetiredSink(
      [this](const std::uint32_t instance_id) {
        std::erase_if(spell_visual_m2_events_,
                      [instance_id](const auto& event) {
                        return event.instance_id == instance_id;
                      });
        const auto found = spell_visual_m2_sound_handles_.find(instance_id);
        if (found == spell_visual_m2_sound_handles_.end()) {
          return;
        }
        for (const auto handle : found->second) {
          if (sound_runtime_.IsSoundHandlePlaying(handle)) {
            (void)sound_runtime_.StopActiveSoundHandle(
                handle, false, 0.15f, true);
          } else {
            sound_runtime_.FreeSoundHandle(handle);
          }
        }
        spell_visual_m2_sound_handles_.erase(found);
      });
  spell_visual_renderer_.BindDeferredImpactSink(
      [this](const render::SpellVisualDeferredImpactCommand& command) {
        if (spell_visual_deferred_impacts_.size() >=
            kMaxQueuedSpellVisualImpacts) {
          spell_visual_deferred_impacts_.pop_front();
          if (!spell_visual_impact_overflow_reported_) {
            spell_visual_impact_overflow_reported_ = true;
            diagnostics::Log(
                diagnostics::LogLevel::kWarn,
                "WorldScene: deferred spell-impact queue overflow; "
                "discarding oldest stale impact");
          }
        }
        spell_visual_deferred_impacts_.push_back(command);
      });
  object_renderer_->BindDynamicObjectEventSink(
      [this](const render::DynamicObjectPresentationEvent& event) {
        auto position = event.position;
        if (event.kind ==
            render::DynamicObjectPresentationEventKind::kSound) {
          (void)sound_runtime_.PlaySoundKit(
              event.visual_id, position.data());
          return;
        }
        camera_.TriggerSpellEffectCameraShakes(event.visual_id, position);
      });
  object_renderer_->BindUnitAnimationCompletionSink(
      [this](const UnitAnimationCompletionEvent& event) {
        unit_animation_completions_.push_back(event);
      });
  object_renderer_->BindGameObjectM2EventSink(
      [this](const render::GameObjectM2PresentationEvent& event) {
        game_object_m2_events_.push_back(event);
      });

  object_renderer_->SetAreaSceneReadinessResolver(
      [this](const render::RenderInstance& instance)
          -> std::optional<render::AreaSceneReadinessState> {
        const auto binding = object_wmo_bindings_.find(instance.handle);
        if (binding == object_wmo_bindings_.end()) {
          return std::nullopt;
        }
        bool has_owner = false;
        bool all_ready = true;
        for (const std::uint64_t owner : binding->second.owners) {
          if (owner == 0u) {
            continue;
          }
          has_owner = true;
          if (!world_map_.IsObjectWmoPlacementRenderReady(owner)) {
            all_ready = false;
          }
        }
        if (!has_owner) {
          return std::nullopt;
        }
        render::AreaSceneReadinessState state{};
        state.runtime_primary_ready = all_ready;
        state.runtime_dependencies_ready = all_ready;
        state.loading_screen_ready = all_ready;
        return state;
      });
  world_presentation_scene_.BindWmoDoodadM2EventSink(
      [this](const render::WmoDoodadM2PresentationEvent& event) {
        for (const auto& [handle, binding] : object_wmo_bindings_) {
          if (std::find(binding.owners.begin(), binding.owners.end(), event.owner) ==
              binding.owners.end()) {
            continue;
          }
          game_object_m2_events_.push_back({
              .owner = handle,
              .event = event.event,
              .source = render::GameObjectM2PresentationEventSource::kPerSequenceModel,
          });
          return;
        }
      });
  world_environment_.SetOutdoorPositionQuery(
      [this](const float x, const float y,
             const float z) -> std::optional<bool> {
        return world_map_.IsOutdoorsAtPosition(x, y, z);
      });
  world_environment_.SetSupportSurfaceHeightQuery(
      [this](const float x, const float y, const float z) {
        return collision_.GetGroundHeight(x, y, z);
      });

  collision_.SetWmoFacetProvider(
      [this](const std::array<float, 6>& bounds,
             const openwow::world::CollisionFacetVisitor& visitor) {
        world_map_.VisitMovementCollisionFacets(bounds, visitor);
      },
      [this] { return world_map_.MovementCollisionFacetRevision(); });
  world_environment_.SetLiquidSurfaceHeightQuery(
      [this](const float x, const float y, const float z) {
        return world_map_.GetLiquidSurfaceHeightAtPosition(x, y, z);
      });
  world_environment_.SetSnowPositionQuery(
      [this](const float x, const float y, const float z) -> std::optional<bool> {
        return world_map_.IsPositionInSnowArea(x, y, z);
      });

  world_map_.SetTileLifecycleCallbacks(
      [this](const openwow::world::TileCoord& coord,
             const openwow::data::terrain::AdtFile& adt) {
        collision_.terrain().SetTileData(coord.x, coord.y, adt);
      },
      [this](const openwow::world::TileCoord& coord) {
        collision_.terrain().RemoveTile(coord.x, coord.y);
      });

  world_frame_.BindPickingScene(object_renderer_.get(), &collision_);

  camera_.BindCollision(&collision_);
  camera_.BindCollisionQuery(
      [this](const float ox, const float oy, const float oz,
             const float dx, const float dy, const float dz,
             const float max_distance) -> std::optional<float> {
        std::optional<float> closest;
        if (const auto terrain = collision_.Raycast(
                ox, oy, oz, dx, dy, dz, max_distance)) {
          closest = terrain->distance;
        }

        const std::array<float, 3> origin{ox, oy, oz};
        const std::array<float, 3> direction{dx, dy, dz};
        const std::array<float, 3> end{
            ox + dx * max_distance, oy + dy * max_distance,
            oz + dz * max_distance};
        constexpr float kQueryPadding = 0.05f;
        const std::array<float, 6> bounds{
            std::min(ox, end[0]) - kQueryPadding,
            std::min(oy, end[1]) - kQueryPadding,
            std::min(oz, end[2]) - kQueryPadding,
            std::max(ox, end[0]) + kQueryPadding,
            std::max(oy, end[1]) + kQueryPadding,
            std::max(oz, end[2]) + kQueryPadding};
        world_map_.VisitMovementCollisionFacets(
            bounds, [&](const openwow::world::CollisionFacetView& facet) {
              if ((facet.source_flags &
                   openwow::data::wmo::kMopyNoCamCollide) != 0u) {
                return;
              }
              const auto hit = IntersectCameraFacet(origin, direction, facet);
              if (hit.has_value() && *hit <= max_distance &&
                  (!closest.has_value() || *hit < *closest)) {
                closest = *hit;
              }
            });
        return closest;
      });
  camera_.BindNearClearanceQuery(
      [this](const std::array<float, 3>& pivot, const std::array<float, 3>& eye,
             const float near_plane, const float arm,
             const float vertical_fov, const float aspect,
             const float far_plane) -> float {
        const auto prism = BuildCameraClearancePrism(
            pivot, eye, near_plane, arm, vertical_fov, aspect, far_plane);
        if (!prism.has_value()) {
          return 0.0f;
        }
        float penetration = 0.0f;
        const auto reduce =
            [&](const openwow::world::CollisionFacetView& facet) {
              if ((facet.source_flags &
                   openwow::data::wmo::kMopyNoCamCollide) != 0u) {
                return;
              }
              penetration = std::max(penetration,
                                     PrismPenetrationFraction(*prism, facet));
            };
        world_map_.VisitMovementCollisionFacets(prism->world_bounds, reduce);
        collision_.terrain().VisitFacets(
            prism->world_bounds[0], prism->world_bounds[3],
            prism->world_bounds[1], prism->world_bounds[4],
            prism->world_bounds[2], prism->world_bounds[5], reduce);
        return penetration;
      });

}

WorldScene::~WorldScene() {
  BindRendererContext(nullptr);

  Shutdown();
  spell_visual_renderer_.BindCameraShakeSink({});
  spell_visual_renderer_.BindM2EventSink({});
  spell_visual_renderer_.BindM2InstanceRetiredSink({});
  spell_visual_renderer_.BindDeferredImpactSink({});
  spell_visual_renderer_.BindGroundHeightSink({});
  spell_visual_renderer_.BindSoundStopSink({});
  spell_visual_renderer_.BindSoundPositionSink({});
  spell_visual_renderer_.BindEffectSoundStartSink({});
  spell_visual_renderer_.BindMissileSoundStartSink({});
  spell_visual_renderer_.BindSoundKitSink({});
  world_environment_.SetOutdoorPositionQuery({});
  world_environment_.SetSupportSurfaceHeightQuery({});
  world_environment_.SetLiquidSurfaceHeightQuery({});
  world_environment_.SetSnowPositionQuery({});
  if (object_renderer_) {
    object_renderer_->BindUnitAnimationCompletionSink({});
    object_renderer_->BindGameObjectM2EventSink({});
  }
  world_presentation_scene_.BindWmoDoodadM2EventSink({});
  world_frame_.BindPickingScene(nullptr, nullptr);

  camera_.BindCollision(nullptr);
  camera_.BindCollisionQuery({});

  world_map_.SetTileLifecycleCallbacks({}, {});
}

void WorldScene::BindRendererContext(
    render::api::RendererContext* renderer_context) {
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
    renderer_device_generation_ =
        renderer_context_->Generation();
  } else {
    renderer_device_generation_ = {};
  }
}

bool WorldScene::Initialize() {
  if (initialized_) return true;

  if (!world_map_.Initialize()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kError,
                       "WorldScene: WorldMap initialization failed");
    return false;
  }

  if (!RestoreRenderDeviceResources()) {
    world_map_.Shutdown();
    return false;
  }

  InitializeSpellVisuals();

  initialized_ = true;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                       "WorldScene: initialized");
  return true;
}

bool WorldScene::RestoreRenderDeviceResources() {
  if (render_device_resources_ready_) {
    return true;
  }
  if (!m2_system_.Initialize()) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kError,
        "WorldScene: M2 device resource initialization failed");
    return false;
  }
  if (!world_presentation_scene_.Initialize()) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kError,
        "WorldScene: world presentation initialization failed");
    return false;
  }

  world_presentation_scene_.SetWeatherGroundHeightSampler(
      [this](const float x, const float y, const float z) {
        return collision_.GetGroundHeight(x, y, z);
      });

  if (!particles_.Initialize()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "WorldScene: ParticleSystem initialization failed (non-fatal)");
  }

  if (!object_renderer_->Initialize()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "WorldScene: ObjectRenderer initialization failed (non-fatal)");
  }

  if (!nameplate_renderer_.Initialize()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "WorldScene: NameplateRenderer initialization failed (non-fatal)");
  }

  if (!unit_name_renderer_.Initialize()) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "WorldScene: UnitNameRenderer initialization failed (non-fatal)");
  }

  if (!chat_bubble_presenter_.Initialize()) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "WorldScene: ChatBubblePresenter initialization failed (non-fatal)");
  }

  if (!selection_circle_.Initialize(texture_manager_)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "WorldScene: SelectionCircle initialization failed (non-fatal)");
  }

  if (!bowstring_renderer_.Initialize()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "WorldScene: BowstringRenderer initialization failed (non-fatal)");
  }
  object_renderer_->BindBowstringRenderer(&bowstring_renderer_);

  if (!blob_shadows_.Initialize()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "WorldScene: BlobShadowRenderer initialization failed (non-fatal)");
  }

  if (!m2_projected_texture_decals_.Initialize()) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "WorldScene: M2ProjectedTextureDecalRenderer initialization failed "
        "(non-fatal)");
  }

  if (!missile_trajectory_renderer_.Initialize()) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "WorldScene: MissileTrajectoryRenderer initialization failed (non-fatal)");
  }

  if (!water_particulates_.Initialize()) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "WorldScene: WaterParticulateSystem initialization failed (non-fatal)");
  }

  render_device_resources_ready_ = true;
  return true;
}

void WorldScene::ReleaseRenderDeviceResources() {
  if (!render_device_resources_ready_) {
    return;
  }
  spell_visual_renderer_.Shutdown();
  chat_bubble_presenter_.Shutdown();
  unit_name_renderer_.Shutdown();
  nameplate_renderer_.Shutdown();
  selection_circle_.Shutdown();
  object_renderer_->BindBowstringRenderer(nullptr);
  bowstring_renderer_.Shutdown();
  blob_shadows_.Shutdown();
  m2_projected_texture_decals_.Shutdown();
  missile_trajectory_renderer_.Shutdown();
  water_particulates_.Shutdown();
  object_renderer_->Shutdown();
  particles_.Shutdown();
  world_presentation_scene_.Shutdown();

  render_device_resources_ready_ = false;
}

void WorldScene::OnRendererDeviceWillReset() {
  ReleaseRenderDeviceResources();
}

void WorldScene::OnRendererDeviceReady(
    const render::api::DeviceGeneration generation) {
  renderer_device_generation_ = generation;
  if (!initialized_) {
    return;
  }
  if (!RestoreRenderDeviceResources()) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kError,
        "WorldScene: renderer-device resource restore failed");
    return;
  }

  InitializeSpellVisuals();
  world_map_.QueueFullPresentationReplay();
  ConsumeWorldPresentationCommands();
}

void WorldScene::Shutdown() {
  if (!initialized_) return;

  world_environment_.SetIndoors(false);

  SpellVisuals_CleanAll();
  SpellVisuals_ClearChainRenderCallbacks();
  SpellVisuals_ClearAreaModelRenderCallbacks();
  spell_visuals_.Shutdown();
  ClearObjectPresentation();
  ReleaseRenderDeviceResources();
  world_map_.Shutdown();
  collision_.terrain().Clear();

  initialized_ = false;
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo, "WorldScene: shutdown");
}

void WorldScene::UnloadMap() {
  if (!initialized_) {
    return;
  }

  world_environment_.SetIndoors(false);
  ClearObjectPresentation();
  world_map_.UnloadMap();
  ConsumeWorldPresentationCommands();
  collision_.terrain().Clear();
  SpellVisuals_CleanAll();
  ClearObjectOwnedSpellVisualState();
}

void WorldScene::ClearObjectOwnedSpellVisualState() {
  spell_visuals_.Clear();
  spell_visual_renderer_.Clear();
  particles_.ClearAll();
}

void WorldScene::RetireDestroyedObjectGeneration() {
  if (!initialized_) {
    return;
  }

  ClearObjectPresentation();

  SpellVisuals_CleanAll();
  ClearObjectOwnedSpellVisualState();
}

bool WorldScene::SetScreenSize(const std::uint32_t width,
                               const std::uint32_t height) {
  constexpr auto kMaxViewExtent =
      static_cast<std::uint32_t>(std::numeric_limits<std::uint16_t>::max());
  return world_map_.SetScreenSize(
      static_cast<std::uint16_t>(std::min(width, kMaxViewExtent)),
      static_cast<std::uint16_t>(std::min(height, kMaxViewExtent)));
}

void WorldScene::ConsumeWorldPresentationCommands() {
  world_map_.AcknowledgePresentation(world_presentation_scene_.Consume(
      world_map_.DrainPresentationCommands()));
}

void WorldScene::Update(const float dt, const float cam_x,
                        const float cam_y, const float cam_z,
                        const float environment_detail,
                        const float weather_particle_density,
                        const bool use_weather_shaders) {
  if (!initialized_) return;
  last_frame_delta_seconds_ = dt;

  world_map_.UpdateStreamingPosition(cam_x, cam_y);

  world_map_.Update(dt);
  ConsumeWorldPresentationCommands();

  object_renderer_->Update(dt);

  world_presentation_scene_.Update(
      dt, {cam_x, cam_y, cam_z}, environment_detail,
      weather_particle_density, use_weather_shaders,
      !world_map_.IsOutdoorsAtPosition(cam_x, cam_y, cam_z));
  NameplateDamageFlashState::Get().Update(dt);

  water_particulates_.Update(
      dt, cam_x, cam_y, cam_z,
      world_map_.GetUnderwaterLiquidTypeId(cam_x, cam_y, cam_z),
      openwow::world::CWorld_HasRenderFlag(
          openwow::world::WorldRenderFlag::kWaterParticulates));

  particles_.Update(dt);

  spell_visuals_.Update(dt);

  spell_visual_renderer_.Update(dt);

}

void WorldScene::PrepareFrame(const render::api::RendererContext* renderer_context,
                              const WorldSceneRenderCamera& render_camera,
                              const float* view_mtx, const float* proj_mtx) {

  frame_prepared_ = false;
  visible_entity_ids_.clear();
  visible_entity_bounding_spheres_.clear();
  if (!initialized_ || !render_device_resources_ready_) return;
  if (renderer_context != nullptr &&
      renderer_context->Generation() !=
          renderer_device_generation_) {
    return;
  }

  const float cam_x = render_camera.position[0];
  const float cam_y = render_camera.position[1];
  const float cam_z = render_camera.position[2];
  const bool homogeneous_depth =
      renderer_context == nullptr ||
      renderer_context->Capabilities().homogeneous_depth;
  const render::ViewProjection matrices =
      render::ViewProjection::CopyOf(view_mtx, proj_mtx, homogeneous_depth);

  world::CameraSnapshot presentation_camera{
      .position = render_camera.position,
      .forward = render_camera.forward,
      .view = matrices.view(),
      .projection = matrices.projection(),
      .near_clip = 0.5f,
      .far_clip = render_camera.far_clip};
  presentation_snapshot_ = world_map_.PublishPresentationSnapshot(
      presentation_camera, render_camera.far_clip);
  presentation_snapshot_.shadows = shadow_settings_;
  ConsumeWorldPresentationCommands();

  const float far_clip_squared =
      render_camera.far_clip * render_camera.far_clip;

  world::Frustum frustum;
  for (std::size_t plane = 0; plane < frustum.planes.size(); ++plane) {
    std::copy_n(presentation_snapshot_.camera.frustum_planes.begin() +
                    plane * 4u,
                4u, frustum.planes[plane].begin());
  }

  object_renderer_->PrepareVisibleInstances(
      object_presentation_snapshot_.active,
      render::ObjectRenderer::FrameVisibilityFilters{
          .admit =
              [cam_x, cam_y, cam_z,
               far_clip_squared](const ObjectPresentationRecord& object) {
                const float dx = object.x - cam_x;
                const float dy = object.y - cam_y;
                const float dz = object.z - cam_z;
                return dx * dx + dy * dy + dz * dz <= far_clip_squared;
              },

          .admit_bounding_sphere =
              [&frustum](const std::array<float, 4>& sphere,
                         const bool has_bounding_sphere) {
                return !has_bounding_sphere ||
                       frustum.TestSphere(sphere[0], sphere[1], sphere[2],
                                          sphere[3]);
              },
      },
      visible_entity_ids_, visible_entity_bounding_spheres_);
  frame_prepared_ = true;
}

void WorldScene::Render(const render::api::RendererContext* renderer_context,
                        const WorldSceneRenderViews& views,
                        const WorldSceneRenderCamera& render_camera,
                        const float* view_mtx, const float* proj_mtx,
                        const render::WorldOverlayMetrics& overlay_metrics,
                        const float world_capture_width,
                        const float world_capture_height,
                        render::m2::M2TransparentDrawOrder& alpha_view_draw_order,
                        bool force_transparency_pass) {
  if (!initialized_ || !render_device_resources_ready_) return;
  if (renderer_context != nullptr &&
      renderer_context->Generation() !=
          renderer_device_generation_) {
    return;
  }

  if (!frame_prepared_) {
    return;
  }
  frame_prepared_ = false;

  m2_system_.SetProjectedTexturesEnabled(
      ui::game::CVarSystem::Instance().GetCVarBool("projectedTextures"));

  const float cam_x = render_camera.position[0];
  const float cam_y = render_camera.position[1];
  const float cam_z = render_camera.position[2];

  const float screen_w = world_capture_width;
  const float screen_h = world_capture_height;
  const bool homogeneous_depth =
      renderer_context == nullptr ||
      renderer_context->Capabilities().homogeneous_depth;
  const render::ViewProjection matrices =
      render::ViewProjection::CopyOf(view_mtx, proj_mtx, homogeneous_depth);
  const render::BgfxColumnMajorViewProjection bgfx_matrices =
      matrices.AsBgfxColumnMajor();

  m2_system_.UpdateAllEffects(
      last_frame_delta_seconds_,
      ::openwow::render::RenderMatrix4x4View{view_mtx, 16u});
  world_presentation_scene_.Render(
      presentation_snapshot_, views.world, matrices,
      static_cast<std::uint16_t>(screen_w),
      static_cast<std::uint16_t>(screen_h), alpha_view_draw_order);

  const std::span<const std::uint64_t> visible_entity_ids{visible_entity_ids_};

  std::span<const std::uint64_t> opaque_pass_entity_ids{visible_entity_ids};
  const auto& occlusion_buffer = world_presentation_scene_.occlusion_buffer();
  if (occlusion_buffer.active() &&
      visible_entity_bounding_spheres_.size() == visible_entity_ids_.size()) {
    opaque_pass_entity_ids_.clear();

    const render::RenderMatrix4x4 world_view_projection =
        render::MultiplyMatrix4x4(bgfx_matrices.view, bgfx_matrices.projection);
    opaque_pass_entity_ids_.reserve(visible_entity_ids_.size());
    for (std::size_t index = 0; index < visible_entity_ids_.size(); ++index) {
      const auto& sphere = visible_entity_bounding_spheres_[index];
      const ObjectGuid guid{visible_entity_ids_[index]};
      if (sphere[3] > 0.0f &&
          object_renderer_->IsOpaquePassOcclusionCullable(guid)) {

        const render::RenderVec3 bounds_min{sphere[0] - sphere[3],
                                            sphere[1] - sphere[3],
                                            sphere[2] - sphere[3]};
        const render::RenderVec3 bounds_max{sphere[0] + sphere[3],
                                            sphere[1] + sphere[3],
                                            sphere[2] + sphere[3]};
        if (occlusion_buffer.IsBoundsOccluded(
                render::RenderVec3View{bounds_min},
                render::RenderVec3View{bounds_max},
                render::RenderMatrix4x4View{world_view_projection})) {
          continue;
        }
      }
      opaque_pass_entity_ids_.push_back(visible_entity_ids_[index]);
    }
    opaque_pass_entity_ids = std::span<const std::uint64_t>{
        opaque_pass_entity_ids_};
  }

  const auto configure_world_view = [&](std::uint8_t view_id) {

    bgfx::setViewMode(view_id, bgfx::ViewMode::Default);
    return render::ConfigureRendererContextView(
        renderer_context,
        view_id,
        render::RendererViewClearFlags::kNone,
        0x00000000u,
        1.0f,
        0,
        static_cast<std::uint32_t>(screen_w),
        static_cast<std::uint32_t>(screen_h),
        bgfx_matrices.view.data(),
        bgfx_matrices.projection.data());
  };

  const std::uint8_t shadow_view = views.blob_shadows;
  (void)configure_world_view(shadow_view);
  blob_shadows_.Render(
      shadow_view, bgfx_matrices.view.data(),
      bgfx_matrices.projection.data(), object_presentation_snapshot_,
      object_renderer_.get(),
      [this](const std::array<float, 6>& bounds,
             const openwow::world::CollisionFacetVisitor& visitor) {
        world_map_.VisitMovementCollisionFacets(bounds, visitor);
        collision_.terrain().VisitFacets(bounds[0], bounds[3], bounds[1],
                                         bounds[4], bounds[2], bounds[5],
                                         visitor);
      });

  const std::uint8_t object_view = views.objects;
  object_renderer_->SetCameraPosition(cam_x, cam_y, cam_z);
  const auto& environment = presentation_snapshot_.environment;
  const render::WorldM2SceneState world_m2_scene_state{
      .light_direction = environment.model_light_direction,
      .ambient_color = environment.model_ambient,
      .diffuse_color = environment.model_diffuse,
      .fog = {
          .params = {environment.fog_start, environment.fog_end,
                     environment.fog_density, 0.0f},
          .color = environment.fog_color,
      },
  };
  object_renderer_->SetWorldM2SceneState(world_m2_scene_state);
  render::m2::M2BatchUniforms spell_visual_uniforms{};
  render::ApplyWorldM2SceneState(world_m2_scene_state,
                                 &spell_visual_uniforms);
  spell_visual_renderer_.SetWorldBatchUniforms(spell_visual_uniforms);
  object_renderer_->Render(object_view, bgfx_matrices.view.data(),
                           bgfx_matrices.projection.data(), screen_w, screen_h,
                           opaque_pass_entity_ids);

  const auto ceffect_render_result = CEffect_C::RenderAll(
      object_view, bgfx_matrices.view.data(),
      render::m2::M2RenderPassScope::kOpaqueOnly, nullptr);
  if (render::m2::IsTerminalM2ResultStatus(ceffect_render_result.status)) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "WorldScene: CEffect_C M2 render " +
            std::string(render::m2::M2ResultStatusName(ceffect_render_result.status)) +
            " reason=" +
            std::string(render::m2::M2ResultReasonName(ceffect_render_result.reason)) +
            " detail=" + ceffect_render_result.detail);
  }
  const auto spell_visual_render_result =
      spell_visual_renderer_.Render(object_view, bgfx_matrices.view.data(),
                                    render::m2::M2RenderPassScope::kOpaqueOnly);
  if (render::m2::IsTerminalM2ResultStatus(spell_visual_render_result.status)) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "WorldScene: spell visual M2 render " +
            std::string(render::m2::M2ResultStatusName(spell_visual_render_result.status)) +
            " reason=" +
            std::string(render::m2::M2ResultReasonName(spell_visual_render_result.reason)) +
            " detail=" + spell_visual_render_result.detail);
  }

  const std::uint8_t mount_view = views.mounts;
  (void)configure_world_view(mount_view);

  bgfx::setViewMode(mount_view, bgfx::ViewMode::DepthAscending);
  render::m2::M2TransparentDrawOrder mount_view_draw_order;
  object_renderer_->RenderMounts(mount_view, bgfx_matrices.view.data(),
                                 bgfx_matrices.projection.data(),
                                 object_presentation_snapshot_, mount_view_draw_order);
  object_renderer_->RenderTransparent(
      mount_view, bgfx_matrices.view.data(), bgfx_matrices.projection.data(),
      screen_w, screen_h, visible_entity_ids, mount_view_draw_order);

  const auto ceffect_transparent_result = CEffect_C::RenderAll(
      mount_view, bgfx_matrices.view.data(),
      render::m2::M2RenderPassScope::kTransparentOnly, &mount_view_draw_order);
  if (render::m2::IsTerminalM2ResultStatus(ceffect_transparent_result.status)) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "WorldScene: transparent CEffect_C M2 render " +
            std::string(render::m2::M2ResultStatusName(
                ceffect_transparent_result.status)) +
            " reason=" +
            std::string(render::m2::M2ResultReasonName(
                ceffect_transparent_result.reason)) +
            " detail=" + ceffect_transparent_result.detail);
  }
  const auto spell_visual_transparent_result =
      spell_visual_renderer_.Render(
          mount_view, bgfx_matrices.view.data(),
          render::m2::M2RenderPassScope::kTransparentOnly, &mount_view_draw_order);
  if (render::m2::IsTerminalM2ResultStatus(
          spell_visual_transparent_result.status)) {
    openwow::diagnostics::Log(
        openwow::diagnostics::LogLevel::kWarn,
        "WorldScene: transparent spell visual M2 render " +
            std::string(render::m2::M2ResultStatusName(
                spell_visual_transparent_result.status)) +
            " reason=" +
            std::string(render::m2::M2ResultReasonName(
                spell_visual_transparent_result.reason)) +
            " detail=" + spell_visual_transparent_result.detail);
  }

  if (force_transparency_pass || initialized_) {
    const std::uint8_t particle_view = views.particles;
    (void)configure_world_view(particle_view);

    particles_.Render(particle_view, bgfx_matrices.view.data(),
                      bgfx_matrices.projection.data(), cam_x, cam_y, cam_z);
  }

  {
    const auto projected_draws = m2_system_.TakeProjectedTextureDraws();
    if (!projected_draws.empty()) {
      const std::uint8_t decal_view = views.blob_shadows;
      (void)configure_world_view(decal_view);
      m2_projected_texture_decals_.Render(
          decal_view, bgfx_matrices.view.data(),
          bgfx_matrices.projection.data(), projected_draws,
          [this](const std::array<float, 6>& bounds,
                 const openwow::world::CollisionFacetVisitor& visitor) {
            world_map_.VisitMovementCollisionFacets(bounds, visitor);
            collision_.terrain().VisitFacets(bounds[0], bounds[3], bounds[1],
                                             bounds[4], bounds[2], bounds[5],
                                             visitor);
          });
    }
  }

  const std::uint8_t circle_view = views.selection_circle;
  (void)configure_world_view(circle_view);
  selection_circle_.Render(
      circle_view, bgfx_matrices.view.data(), bgfx_matrices.projection.data(),
      cam_x, cam_y, cam_z,
      [this](const std::array<float, 6>& bounds,
             const openwow::world::CollisionFacetVisitor& visitor) {
        world_map_.VisitMovementCollisionFacets(bounds, visitor);
        collision_.terrain().VisitFacets(bounds[0], bounds[3], bounds[1],
                                         bounds[4], bounds[2], bounds[5],
                                         visitor);
      });

  unit_name_renderer_.Render(views.unit_names, overlay_metrics, view_mtx,
                             proj_mtx);

  const std::uint8_t nameplate_view = views.nameplates;
  const std::uint64_t compositor_generation =
      renderer_context != nullptr
          ? renderer_context->FinalCompositor().active_generation
          : 0u;

  nameplate_renderer_.PublishFrameLayout(overlay_metrics, view_mtx, proj_mtx,
                                         compositor_generation);

  const auto chat_bubbles = ChatBubbleSystem::Get().GetAllBubbles();
  chat_bubble_presenter_.Render(nameplate_view, overlay_metrics.framebuffer_width,
                                overlay_metrics.framebuffer_height, view_mtx,
                                proj_mtx, chat_bubbles,
                                object_presentation_snapshot_,
                                *object_renderer_,
                                compositor_generation);
}

void WorldScene::LoadMap(std::uint32_t map_id, const std::string& map_name) {
  world_map_.SetMap(map_id, map_name);
  world_environment_.SetIndoors(false);
  openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kInfo,
                     "WorldScene: LoadMap id=" + std::to_string(map_id)
                         + " name=" + map_name);
}

void WorldScene::UpdatePlayerPosition(float x, float y, float z) {
  world_map_.UpdatePlayerPosition(x, y, z);
  world_environment_.SetIndoors(
      !world_map_.IsOutdoorsAtPosition(x, y, z));

  if (world_audio_position_sink_) {
    world_audio_position_sink_(world_map_.map_id(), x, y);
  }
}

void WorldScene::SetTimeOfDay(float hour_of_day) {
  time_of_day_ = WorldHourOfDayToNormalizedTime(hour_of_day);
  world_map_.SetTimeOfDay(time_of_day_);
}

void WorldScene::SetScreenEffectLightParamSlotOverride(
    std::optional<std::uint8_t> light_param_slot_override) {
  world_map_.SetScreenEffectLightParamSlotOverride(
      light_param_slot_override);
}

void WorldScene::SetScreenEffectFogOverride(
    std::optional<render::FogBandOverride> fog_override) {
  world_map_.SetScreenEffectFogOverride(
      fog_override
          ? std::optional<world::WorldFogOverride>(
                world::WorldFogOverride{
                    .start_factor = fog_override->band.start_factor,
                    .end_distance = fog_override->band.end_distance,
                    .packed_argb =
                        render::PackFogColorToArgb(fog_override->band.color),
                    .sky_dome_enabled =
                        fog_override->sky_dome_enabled ? 1u : 0u})
          : std::nullopt);
}

void WorldScene::SetSuppressLocalLighting(const bool suppress_local_lighting) {
  world_map_.SetSuppressLocalLighting(suppress_local_lighting);
}

void WorldScene::SetSpecularEnabled(const bool enabled) {
  world_presentation_scene_.SetSpecularEnabled(enabled);
}

void WorldScene::SetWeather(const std::uint32_t type, const float intensity,
                            const bool smooth) {
  world_map_.SetWeather(type, intensity, smooth);
}

render::ObjectRenderer& WorldScene::object_renderer() {
  return *object_renderer_;
}

const render::ObjectRenderer& WorldScene::object_renderer() const {
  return *object_renderer_;
}

void WorldScene::PublishObjectPresentation(
    ObjectManager& obj_mgr, WorldSession& world_session) {
  auto spell_model_events = std::move(spell_visual_m2_events_);
  spell_visual_m2_events_.clear();
  spell_visual_m2_event_overflow_reported_ = false;
  for (const auto& presentation : spell_model_events) {
    CGUnit_C* event_unit = nullptr;
    auto owner_guid = presentation.owner.guid;
    if (presentation.requires_owner_resolution) {

      owner_guid = ObjectGuid(presentation.required_owner_guid);
      auto* const event_object = obj_mgr.GetMutable(owner_guid);
      if (event_object == nullptr) {
        continue;
      }
      event_unit = dynamic_cast<CGUnit_C*>(event_object);
    } else if (!owner_guid.IsEmpty()) {
      const auto current_handle = obj_mgr.GetObjectHandle(owner_guid);
      if (!current_handle.has_value() ||
          *current_handle != presentation.owner) {
        continue;
      }
      event_unit = obj_mgr.GetMutableUnit(owner_guid);
    }

    if (presentation.kind == render::SpellVisualM2EventKind::kMissile &&
        event_unit == nullptr) {
      continue;
    }

    (void)DispatchSpellVisualM2Event(
        presentation.event,
        {.sound = [this, &obj_mgr, &presentation, owner_guid](
                      const render::m2::M2TriggeredEvent& sound_event) {
           audio::SoundKitPlaybackOptions options{};
           if (presentation.kind == render::SpellVisualM2EventKind::kMissile) {
             if ((presentation.raw_flags & kEffectSoundModeLoop) != 0u) {
               options.loop_mode = audio::SoundLoopMode::kForceLoop;
             } else {
               options.loop_mode = audio::SoundLoopMode::kForceOneShot;
               if (!owner_guid.IsEmpty() &&
                   owner_guid ==
                       obj_mgr.player_control().ActiveMoverGuid()) {
                 options.playback_priority = kLocalImpactPlaybackPriority;
               }
             }
           }

           std::uint32_t handle = 0u;
           const auto result =
               presentation.kind == render::SpellVisualM2EventKind::kMissile
                   ? sound_runtime_.PlaySoundKit(
                         sound_event.data, sound_event.world_position.data(),
                         &handle, options)
                   : sound_runtime_.PlaySoundKit(
                         sound_event.data, sound_event.world_position.data(),
                         &handle);
           if (result == 0 && !owner_guid.IsEmpty() &&
               (presentation.raw_flags &
                kEffectSoundDoNotBindToOwner) == 0u) {
             (void)sound_runtime_.BindSoundHandleToObjectGuid(
                 handle, owner_guid.GetRawValue());
           }
           if (result == 0 && handle != 0u &&
               presentation.kind == render::SpellVisualM2EventKind::kMissile) {
             spell_visual_m2_sound_handles_[presentation.instance_id]
                 .push_back(handle);
           }
         },
         .hit = [&world_session, event_unit] {
           if (event_unit != nullptr) {
             RefreshSpellVisualM2HitReaction(*event_unit, world_session);
           }
         }});
  }

  auto deferred_impacts = std::move(spell_visual_deferred_impacts_);
  spell_visual_deferred_impacts_.clear();
  spell_visual_impact_overflow_reported_ = false;
  for (const auto& impact : deferred_impacts) {
    CGUnit_C* impact_owner = nullptr;
    const std::array<float, 3>* world_position = nullptr;
    if (impact.target_guid != 0u) {

      if (impact.target.guid.IsEmpty()) continue;
      const auto current = obj_mgr.GetObjectHandle(impact.target.guid);
      if (!current.has_value() || *current != impact.target) continue;
      impact_owner = obj_mgr.GetMutableUnit(impact.target.guid);
    } else {

      if (impact.caster.guid.IsEmpty()) continue;
      const auto current = obj_mgr.GetObjectHandle(impact.caster.guid);
      if (!current.has_value() || *current != impact.caster) continue;
      impact_owner = obj_mgr.GetMutableUnit(impact.caster.guid);
      world_position = &impact.world_position;
    }
    if (impact_owner == nullptr) continue;
    (void)impact_owner->SpellVisuals().CreateFromKit(
        world_session, impact.kit_id, 1u, world_position,
        false, {}, impact.spell_id, impact.spell_visual_id,
        SpellVisualPresentationPhase::kEffect,
        SpellVisualLifecycleAction::kTransient);
  }

  auto completions = std::move(unit_animation_completions_);
  unit_animation_completions_.clear();
  for (const auto& completion : completions) {
    const auto current_handle =
        obj_mgr.GetObjectHandle(completion.owner.guid);
    auto* const unit = obj_mgr.GetMutableUnit(completion.owner.guid);
    if (unit == nullptr || !current_handle.has_value() ||
        *current_handle != completion.owner) {
      continue;
    }
    unit->Animation().HandlePlaybackCompletion(
        world_session, completion.request_serial, completion.animation_id);
  }
  object_presentation_snapshot_ =
      obj_mgr.PublishPresentationSnapshot(world_session);
  object_presentation_snapshot_.has_active_targeting_spell =
      HasActiveTargetingSpell(world_session);
  object_presentation_snapshot_.active_spell_allows_flagged_units =
      ActiveSpellAllowsFlaggedUnitTargets(world_session);
  object_presentation_snapshot_.active_spell_allows_tapped =
      openwow::game::ActiveTargetingSpellAllowsTapped(
          world_session, world_session.spells());
  object_presentation_snapshot_.spell_missile_corrections =
      world_session.TakeSpellMissileCorrections();
  {
    auto packet_spell_visuals =
        world_session.TakeReadySpellVisualPresentationEvents();
    object_presentation_snapshot_.spell_visual_events.insert(
        object_presentation_snapshot_.spell_visual_events.end(),
        std::make_move_iterator(packet_spell_visuals.begin()),
        std::make_move_iterator(packet_spell_visuals.end()));
  }
  if (object_renderer_) {
    render_resources_->presentation_publisher.PublishSpellVisuals(
        obj_mgr, object_presentation_snapshot_);

    auto &render_presentation =
        render_resources_->presentation_publisher.PublishObjects(
            obj_mgr, object_presentation_snapshot_,
            world_session.transport_mgr(), world_session);
    object_renderer_->ConsumePresentation(
        render_presentation,
        object_presentation_snapshot_);
  }
}

void WorldScene::SynchronizeObjectModelBindings(
    ObjectManager& obj_mgr, WorldSession& world_session) {
  if (!object_renderer_) {
    return;
  }

  auto game_object_events = std::move(game_object_m2_events_);
  game_object_m2_events_.clear();
  for (const auto& event : game_object_events) {
    const auto current_handle = obj_mgr.GetObjectHandle(event.owner.guid);
    auto* const game_object = obj_mgr.GetMutableGameObject(event.owner.guid);
    if (!current_handle.has_value() || *current_handle != event.owner ||
        game_object == nullptr) {
      continue;
    }
    if (event.source == render::GameObjectM2PresentationEventSource::kPerSequenceModel) {
      (void)GameObjectPerSequenceModelSoundCallback(
          obj_mgr, &camera_, event.event.identifier, event.event.data,
          event.event.world_position.data(), event.owner.guid.GetRawValue());
    } else {
      (void)GameObjectPrimaryModelSoundCallback(
          obj_mgr, &camera_, event.event.identifier, event.event.data,
          event.event.world_position.data(), event.owner.guid.GetRawValue());
    }
  }

  for (const auto &retired : object_presentation_snapshot_.retired) {
    const auto binding = object_wmo_bindings_.find(retired.handle);
    if (binding == object_wmo_bindings_.end()) {
      continue;
    }
    for (const std::uint64_t owner : binding->second.owners) {
      world_map_.RemoveObjectWmoPlacement(owner);
    }
    world_map_.RemoveObjectWmoPlacement(binding->second.rebuild_effect_owner);
    object_wmo_bindings_.erase(binding);
  }

  const std::uint32_t current_time_ms = core::GameClock::GetTickCount32();
  for (const auto& record : object_presentation_snapshot_.active) {
    auto* const object = obj_mgr.GetMutable(record.handle.guid);
    if (object == nullptr) {
      continue;
    }
    const std::uint32_t instance_id =
        object_renderer_->QueryPrimaryM2InstanceId(record.handle);
    if (object->GetPrimaryM2InstanceId() != instance_id) {
      object->SetPrimaryM2InstanceId(instance_id);
      if (instance_id != 0u && object->IsUnit()) {
        static_cast<CGUnit_C*>(object)->Presentation().OnModelLoaded(
            world_session, instance_id);
      }
    }
    if (object->IsGameObject()) {
      auto* const game_object = static_cast<CGGameObject_C*>(object);

      if (game_object->IsAnyTransport()) {
        game_object->SynchronizeRenderAssetReadiness(
            object_renderer_->IsRuntimeRenderAssetReady(record.handle.guid));
        game_object->SynchronizeMOTransportModelReadiness(
            object_renderer_->IsModelReadyForAnimation(record.handle.guid));
      }
      std::optional<float> destructible_nameplate_model_height;

      std::optional<std::vector<std::array<float, 4>>> active_wmo_convex_planes;
      render::AreaScenePresentationState area_scene;
      const bool has_area_scene = object_renderer_->QueryAreaScenePresentationState(
          record.handle, &area_scene);
      ObjectWmoBinding* destructible_binding = nullptr;
      const CGGameObject_C::DestructibleVisualControlState* destructible_visual = nullptr;
      DestructibleTransitionPresentation destructible_presentation;
      if (game_object->IsDestructibleBuilding()) {
        auto [binding, inserted] = object_wmo_bindings_.try_emplace(record.handle);
        (void)inserted;
        destructible_binding = &binding->second;
        const auto& visual = game_object->GetDestructibleVisualControlState();
        destructible_visual = &visual;
        auto& transition = destructible_binding->destructible_transition;

        if (transition.observed_transition_serial != visual.transition_serial) {
          transition = {};
          transition.observed_transition_serial = visual.transition_serial;
          if (IsDestructibleTransitionEligible(visual)) {
            transition.source_state = static_cast<std::uint8_t>(
                visual.previous_active_state_index);
            transition.destination_state = visual.active_state_index;
            transition.mode = visual.rebuild_transition_mode;
            transition.waiting_for_models = true;
          }
        }

        if (transition.waiting_for_models) {
          const auto resolve_state_height =
              [this, &record, &visual, destructible_binding](const std::uint8_t state)
                  -> std::optional<float> {
            if (state >= visual.states.size()) {
              return std::nullopt;
            }
            const std::uint64_t owner = destructible_binding->owners[state];
            if (owner != 0u) {
              if (const auto bounds = world_map_.EnsureObjectWmoLocalBounds(owner);
                  bounds.has_value()) {
                return ResolveModelLocalBoundsZExtent(*bounds);
              }
              return std::nullopt;
            }
            if (visual.states[state].render_display_id == 0u) {
              return 0.0f;
            }
            render::DestructibleM2StateSpatialQueryResult spatial;
            if (!object_renderer_->QueryDestructibleM2StateSpatial(record.handle, state,
                                                                    &spatial)) {
              return std::nullopt;
            }
            return ResolveModelLocalBoundsZExtent(spatial.local_bounds);
          };

          const auto destination_height = resolve_state_height(transition.destination_state);
          const bool source_moves = transition.mode == 0u || transition.mode == 2u;
          const auto source_height = source_moves
                                         ? resolve_state_height(transition.source_state)
                                         : std::optional<float>{0.0f};
          if (destination_height.has_value() && source_height.has_value()) {

            const std::int32_t transition_speed =
                static_cast<std::int32_t>(visual.rebuild_transition_speed);

            const float duration =
                (*destination_height /
                 (static_cast<float>(transition_speed) * 0.33333334f)) *
                1000.0f;
            transition.destination_height = *destination_height;
            transition.source_height = *source_height;
            transition.start_time_ms = current_time_ms;
            transition.duration_ms = duration <= 0.0
                                         ? 0u
                                         : static_cast<std::uint32_t>(std::min(
                                               static_cast<double>(duration),
                                               static_cast<double>(
                                                   std::numeric_limits<std::uint32_t>::max())));
            transition.waiting_for_models = false;
            transition.active = true;
            transition.doodad_transfer_pending = true;
          }
        }

        destructible_presentation = BuildDestructibleTransitionPresentation(
            visual, transition, current_time_ms);

        if (transition.active &&
            transition.start_time_ms + transition.duration_ms <= current_time_ms) {
          transition.active = false;
          destructible_presentation = BuildDestructibleTransitionPresentation(
              visual, transition, current_time_ms);
        }
        for (std::size_t state = 0u;
             state < destructible_presentation.visible.size(); ++state) {
          object_renderer_->SetDestructibleM2StatePresentation(
              record.handle, static_cast<std::uint8_t>(state),
              destructible_presentation.visible[state] && area_scene.visible,
              destructible_presentation.vertical_offsets_down[state]);
        }
        object_renderer_->SetDestructibleRebuildEffectPresentation(
            record.handle, transition.active && area_scene.visible);
      }
      if (has_area_scene) {
        auto [binding, inserted] = object_wmo_bindings_.try_emplace(record.handle);
        (void)inserted;
        if (destructible_binding == nullptr) {
          destructible_binding = &binding->second;
        }
        std::array<bool, 4> published_states{};
        for (std::size_t model_index = 0u; model_index < area_scene.model_count; ++model_index) {
          const auto &model = area_scene.models[model_index];
          if (model.state_index >= binding->second.owners.size()) {
            continue;
          }
          const auto bounds = ResolveGameObjectWmoWorldBounds(
              obj_mgr.dbc_loader(), area_scene,
              game_object->IsDestructibleBuilding() ? game_object->GetDisplayId()
                                                    : model.display_id,
              game_object->IsDestructibleBuilding());
          if (!bounds.has_value()) {
            continue;
          }
          std::uint64_t &owner = destructible_binding->owners[model.state_index];
          if (owner == 0u) {
            owner = next_object_wmo_owner_++;
          }
          const bool is_destructible_state = game_object->IsDestructibleBuilding();
          const bool visible = is_destructible_state
                                   ? destructible_presentation.visible[model.state_index] &&
                                         area_scene.visible
                                   : model.visible;
          auto model_matrix = area_scene.world_transform;
          std::array<std::uint16_t, 3> doodad_sets = model.additional_doodad_sets;
          std::array<world::WmoDoodadAnimationControl, 2> doodad_animations{};

          if (const auto move_phase = game_object->GetMOTransportMovePhase();
              move_phase.has_value()) {
            const auto phase_animation = [](const MOTransportMovePhase phase) {
              switch (phase) {
              case MOTransportMovePhase::Accelerating:
                return world::WmoDoodadAnimation::kTransportShipStart;
              case MOTransportMovePhase::ConstantSpeed:
                return world::WmoDoodadAnimation::kTransportShipMoving;
              case MOTransportMovePhase::Decelerating:
                return world::WmoDoodadAnimation::kTransportShipStop;
              default:

                return world::WmoDoodadAnimation::kNone;
              }
            }(*move_phase);
            if (phase_animation != world::WmoDoodadAnimation::kNone) {
              doodad_animations[0] = {
                  .doodad_set = 0u,
                  .animation = phase_animation,
              };
            }
          }
          if (is_destructible_state) {
            model_matrix[14] -=
                destructible_presentation.vertical_offsets_down[model.state_index];
            const auto& transition = destructible_binding->destructible_transition;
            if (transition.active && model.state_index == transition.source_state) {

              doodad_sets = model.state_index == 0u || model.state_index == 3u
                                ? std::array<std::uint16_t, 3>{
                                      model.impact_effect_doodad_set,
                                      model.ambient_doodad_set,
                                      0u,
                                  }
                                : std::array<std::uint16_t, 3>{
                                      model.destruction_or_init_doodad_set,
                                      model.impact_effect_doodad_set,
                                      0u,
                                  };
              doodad_animations = model.state_index == 0u || model.state_index == 3u
                                      ? std::array<world::WmoDoodadAnimationControl, 2>{
                                            world::WmoDoodadAnimationControl{
                                                .doodad_set = model.impact_effect_doodad_set,
                                                .animation = world::WmoDoodadAnimation::kDestructibleTransition},
                                            world::WmoDoodadAnimationControl{
                                                .doodad_set = model.ambient_doodad_set,
                                                .animation = world::WmoDoodadAnimation::kDestructibleTransition},
                                        }
                                      : std::array<world::WmoDoodadAnimationControl, 2>{
                                            world::WmoDoodadAnimationControl{
                                                .doodad_set = model.destruction_or_init_doodad_set,
                                                .animation = world::WmoDoodadAnimation::kDestructibleTransition},
                                            world::WmoDoodadAnimationControl{
                                                .doodad_set = model.impact_effect_doodad_set,
                                                .animation = world::WmoDoodadAnimation::kDestructibleTransition},
                                        };
            } else if (model.state_index == area_scene.destructible_active_state) {
              const auto ambient_animation = area_scene.destructible_active_state == 0u
                                                 ? world::WmoDoodadAnimation::kDestructibleAmbientStop
                                                 : world::WmoDoodadAnimation::kDestructibleAmbientLoop;
              if (destructible_visual != nullptr &&
                  destructible_visual->impact_effect_enabled && model.state_index < 3u &&
                  model.impact_effect_doodad_set != 0u) {

                doodad_sets = {model.impact_effect_doodad_set,
                                model.ambient_doodad_set, 0u};
                doodad_animations[0] = {
                    .doodad_set = model.impact_effect_doodad_set,
                    .animation = world::WmoDoodadAnimation::kDestructibleImpact,
                };
                doodad_animations[1] = {
                    .doodad_set = model.ambient_doodad_set,
                    .animation = ambient_animation,
                };
              } else {
                doodad_animations[0] = {
                    .doodad_set = model.ambient_doodad_set,
                    .animation = ambient_animation,
                };
              }
            }
          }
          world_map_.SynchronizeObjectWmoPlacement(
              owner,
              world::ObjectWmoPlacement{
                  .path = model.resource_path,
                  .model_matrix = model_matrix,
                  .world_bounds = *bounds,
                  .uniform_scale = area_scene.uniform_scale,
                  .additional_doodad_sets = doodad_sets,
                  .doodad_animation_controls = doodad_animations,
                  .visible = visible,

                  .object_guid = record.handle.guid.GetRawValue(),
              });
          if (visible) {
            if (const auto local_bounds = world_map_.EnsureObjectWmoLocalBounds(owner);
                local_bounds.has_value()) {
              destructible_nameplate_model_height =
                  ResolveModelLocalBoundsZExtent(*local_bounds);
            }

            active_wmo_convex_planes =
                world_map_.QueryObjectWmoConvexVolumePlanes(owner);
          }
          published_states[model.state_index] = true;
        }
        if (game_object->IsDestructibleBuilding() &&
            destructible_binding->destructible_transition.doodad_transfer_pending) {
          auto& transition = destructible_binding->destructible_transition;
          const std::uint64_t source_owner =
              destructible_binding->owners[transition.source_state];
          const std::uint64_t destination_owner =
              destructible_binding->owners[transition.destination_state];
          const auto resolve_transfer_sets = [&area_scene](const std::uint8_t state)
              -> std::optional<std::array<std::uint16_t, 2>> {

            const auto found = std::find_if(
                area_scene.models.begin(),
                area_scene.models.begin() + area_scene.model_count,
                [state](const render::AreaScenePresentationState::Model& model) {
                  return model.state_index == state;
                });
            if (found == area_scene.models.begin() + area_scene.model_count) {
              return std::nullopt;
            }
            return state == 0u || state == 3u
                       ? std::array<std::uint16_t, 2>{
                             found->impact_effect_doodad_set,
                             found->ambient_doodad_set,
                         }
                       : std::array<std::uint16_t, 2>{
                             found->destruction_or_init_doodad_set,
                             found->impact_effect_doodad_set,
                         };
          };
          const auto virtual_set = [](const std::uint16_t authored_set,
                                      const std::uint8_t source_state) {
            return static_cast<std::uint16_t>(
                static_cast<std::uint32_t>(authored_set) +
                100u * (static_cast<std::uint32_t>(source_state) + 1u));
          };

          const std::uint8_t prior_source =
              destructible_binding->prior_doodad_transfer_source_state;
          if (prior_source < destructible_binding->owners.size()) {
            const std::uint64_t prior_owner = destructible_binding->owners[prior_source];
            if (const auto prior_sets = resolve_transfer_sets(prior_source);
                prior_sets.has_value()) {
              for (const std::uint16_t authored_set : *prior_sets) {
                world_map_.TransferObjectWmoDoodadSet(
                    source_owner, prior_owner, virtual_set(authored_set, prior_source),
                    authored_set);
              }
            }
          }
          if (const auto source_sets = resolve_transfer_sets(transition.source_state);
              source_sets.has_value()) {
            for (const std::uint16_t source_set : *source_sets) {

              world_map_.TransferObjectWmoDoodadSet(
                  source_owner, destination_owner, source_set,
                  virtual_set(source_set, transition.source_state));
            }
          }
          destructible_binding->prior_doodad_transfer_source_state =
              transition.source_state;
          transition.doodad_transfer_pending = false;
        }
        for (std::size_t state = 0u; state < destructible_binding->owners.size(); ++state) {
          if (!published_states[state] && destructible_binding->owners[state] != 0u) {
            world_map_.RemoveObjectWmoPlacement(destructible_binding->owners[state]);
            destructible_binding->owners[state] = 0u;
          }
        }
        if (game_object->IsDestructibleBuilding()) {
          const bool show_rebuild_effect =
              destructible_binding->destructible_transition.active && area_scene.visible;
          if (area_scene.rebuild_effect_resource_path.empty()) {
            if (destructible_binding->rebuild_effect_owner != 0u) {
              world_map_.RemoveObjectWmoPlacement(
                  destructible_binding->rebuild_effect_owner);
              destructible_binding->rebuild_effect_owner = 0u;
            }
          } else {
            if (destructible_binding->rebuild_effect_owner == 0u) {
              destructible_binding->rebuild_effect_owner = next_object_wmo_owner_++;
            }
            if (const auto bounds = ResolveGameObjectWmoWorldBounds(
                    obj_mgr.dbc_loader(), area_scene,
                    area_scene.destructible_rebuild_effect_display_id,
                    false);
                bounds.has_value()) {
              world_map_.SynchronizeObjectWmoPlacement(
                  destructible_binding->rebuild_effect_owner,
                  world::ObjectWmoPlacement{
                      .path = area_scene.rebuild_effect_resource_path,
                      .model_matrix = area_scene.world_transform,
                      .world_bounds = *bounds,
                      .uniform_scale = area_scene.uniform_scale,
                      .visible = show_rebuild_effect,
                  });
            }
          }
        }
      } else if (const auto binding = object_wmo_bindings_.find(record.handle);
                 binding != object_wmo_bindings_.end() && !game_object->IsDestructibleBuilding()) {
        for (const std::uint64_t owner : binding->second.owners) {
          world_map_.RemoveObjectWmoPlacement(owner);
        }
        object_wmo_bindings_.erase(binding);
      }

      game_object->SynchronizeModelAnimationCompletion(
          object_renderer_->QueryGameObjectAnimationDurationMs(record.handle.guid),
          current_time_ms);

      render::ModelSpatialQueryResult spatial{};
      if (object_renderer_->QueryModelSpatialState(record.handle.guid, &spatial)) {
        game_object->SynchronizeModelSpatialBounds(spatial.world_bounds);

        game_object->SynchronizeModelLocalBounds(spatial.local_bounds);
        if (!destructible_nameplate_model_height.has_value()) {
          destructible_nameplate_model_height =
              ResolveModelLocalBoundsZExtent(spatial.local_bounds);
        }
      } else {
        game_object->SynchronizeModelLocalBounds(std::nullopt);
      }

      game_object->SynchronizeModelConvexVolumePlanes(
          std::move(active_wmo_convex_planes));
      if (game_object->IsDestructibleBuilding()) {
        game_object->SynchronizeDestructibleNameplateModelHeight(
            destructible_nameplate_model_height);
      }
      if (game_object->GetGoType() == GameObjectType::Trapdoor) {
        (void)game_object->UpdateTrapdoorRenderSync(
            object_renderer_->IsRuntimeRenderAssetReady(record.handle.guid));
      }
    }
    if (instance_id != 0u && object->GetActiveOverlayModelIndex() != 0u &&
        !object->IsOverlayBoneAttached()) {
      object->AttachOverlayModelToBone();
    }
  }
}

void WorldScene::ClearObjectPresentation() {
  for (const auto &[handle, binding] : object_wmo_bindings_) {
    (void)handle;
    for (const std::uint64_t owner : binding.owners) {
      world_map_.RemoveObjectWmoPlacement(owner);
    }
    world_map_.RemoveObjectWmoPlacement(binding.rebuild_effect_owner);
  }
  object_wmo_bindings_.clear();
  if (object_renderer_) {
    object_renderer_->ClearPresentation();
  }

  render_resources_->presentation_publisher.Reset();
  object_presentation_snapshot_ = {};

  visible_entity_ids_.clear();
  visible_entity_bounding_spheres_.clear();
  frame_prepared_ = false;
  unit_animation_completions_.clear();
  game_object_m2_events_.clear();
  spell_visual_m2_events_.clear();
  spell_visual_deferred_impacts_.clear();
  for (const auto& [_, handles] : spell_visual_m2_sound_handles_) {
    for (const auto handle : handles) {
      if (sound_runtime_.IsSoundHandlePlaying(handle)) {
        (void)sound_runtime_.StopActiveSoundHandle(handle, false, 0.15f,
                                                   true);
      } else {
        sound_runtime_.FreeSoundHandle(handle);
      }
    }
  }
  spell_visual_m2_sound_handles_.clear();
  spell_visual_m2_event_overflow_reported_ = false;
  spell_visual_impact_overflow_reported_ = false;
}

void WorldScene::SetObjectRendererFileLoader(
    std::function<std::vector<std::uint8_t>(const std::string&)> loader) {
  openwow::core::LocaleSystem locale;
  locale.SetLocaleFromString(ClientConfig::Get().GetLocale());
  const auto font_path = openwow::platform::BuildFontPath(
      openwow::core::LocaleSystem::GetFontForLocale(locale.GetLocale()));

  chat_bubble_presenter_.SetFontPath(font_path);
  chat_bubble_presenter_.SetFileLoader(loader);
  world_presentation_scene_.SetFileLoader(loader);
  if (object_renderer_) {
    object_renderer_->SetFileLoader(std::move(loader));
  }
}

void WorldScene::SetPrefixFileLoader(
    std::function<std::vector<std::uint8_t>(const std::string&, std::size_t)>
        loader) {
  world_presentation_scene_.SetPrefixFileLoader(std::move(loader));
}

void WorldScene::ApplyEquipmentPresentation(
    const EquipmentPresentation& presentation) {
  render_resources_->presentation_publisher.ApplyEquipmentPresentation(
      presentation);
}

void WorldScene::RenderMissileTrajectory(
    const std::uint8_t view_id, const float* view_mtx,
    const float* proj_mtx, const MissileArcRenderSnapshot& snapshot,
    render::m2::M2TransparentDrawOrder& alpha_view_draw_order) {
  missile_trajectory_renderer_.Render(
      view_id, view_mtx, proj_mtx,
      render::RenderFogState{
          .params = {presentation_snapshot_.environment.fog_start,
                     presentation_snapshot_.environment.fog_end,
                     presentation_snapshot_.environment.fog_density, 0.0f},
          .color = presentation_snapshot_.environment.fog_color,
      },
      snapshot, alpha_view_draw_order);
}

void WorldScene::ConsumeSpellVisualEvents() {
  for (const auto& retired : object_presentation_snapshot_.retired) {
    spell_visual_renderer_.DestroyEffectsForObject(retired.handle);
  }
  for (const auto& event : object_presentation_snapshot_.spell_visual_events) {
    spell_visual_renderer_.ConsumePresentationEvent(event);
  }

  for (const auto& correction :
       object_presentation_snapshot_.spell_missile_corrections) {
    spell_visual_renderer_.ApplyMissilePositionCorrection(correction);
  }
}

void WorldScene::BindDbc(const openwow::data::dbc::DbcLoader* dbc) {
  camera_.BindDbc(dbc);
  m2_system_.BindDbc(dbc);
  world_map_.BindDbc(dbc);
  world_presentation_scene_.BindDbc(dbc);
  water_particulates_.BindDbc(dbc);
  if (object_renderer_) {
    object_renderer_->BindDbc(dbc);
  }
  render_resources_->presentation_publisher.BindDbc(dbc);
  spell_visuals_.BindDbc(dbc);
  spell_visual_renderer_.BindDbc(dbc);
}

void WorldScene::InitializeSpellVisuals() {
  if (!spell_visuals_.Initialize(
          &particles_, &object_presentation_snapshot_)) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "WorldScene: SpellVisualEffects initialization failed (non-fatal)");
  }

  SpellVisualChainRenderCallbacks chain_callbacks{};
  chain_callbacks.create =
      [this](const SpellVisualChainRenderRequest& request) {
        return missile_trajectory_renderer_.CreateSpellChain(request);
      };
  chain_callbacks.update =
      [this](const std::uint32_t handle, const float* const source,
             const float* const target, const bool visible) {
        return missile_trajectory_renderer_.UpdateSpellChain(
            handle, source, target, visible);
      };
  chain_callbacks.destroy = [this](const std::uint32_t handle) {
    missile_trajectory_renderer_.DestroySpellChain(handle);
  };
  SpellVisuals_SetChainRenderCallbacks(std::move(chain_callbacks));

  if (!spell_visual_renderer_.Initialize(
          &m2_system_, &object_presentation_snapshot_,
          [this](const ObjectHandle handle) {
            return object_renderer_
                       ? object_renderer_->QueryPrimaryM2InstanceId(handle)
                       : 0u;
          })) {
    openwow::diagnostics::Log(openwow::diagnostics::LogLevel::kWarn,
                       "WorldScene: SpellVisualRenderer initialization failed (non-fatal)");
  } else {
    SpellVisualAreaModelRenderCallbacks callbacks{};
    callbacks.create = [this](const SpellVisualAreaModelRenderRequest& request) {
      return spell_visual_renderer_.CreatePersistentAreaModel(
          request.model_path, request.position, request.radius,
          request.duration_seconds);
    };
    callbacks.destroy = [this](const std::uint32_t effect_id) {
      spell_visual_renderer_.DestroyEffect(effect_id);
    };
    callbacks.set_position =
        [this](const std::uint32_t effect_id, const float* const position) {
          return spell_visual_renderer_.SetPersistentAreaEffectPosition(
              effect_id, position);
        };
    callbacks.create_shard =
        [this](const SpellVisualAreaShardRenderRequest& request) {
          return spell_visual_renderer_.CreateAreaModelShard(
              request.area_effect_id, request.model_path,
              request.world_position.data());
        };
    callbacks.destroy_shard =
        [this](const std::uint32_t effect_id,
               const std::uint32_t instance_id) {
          spell_visual_renderer_.DestroyAreaModelShard(effect_id, instance_id);
        };
    callbacks.set_shard_visible =
        [this](const std::uint32_t instance_id, const bool visible) {
          return spell_visual_renderer_.SetAreaModelShardVisible(instance_id,
                                                                 visible);
        };
    callbacks.set_shard_transform =
        [this](const std::uint32_t instance_id, const float* const matrix) {
          return spell_visual_renderer_.SetAreaModelShardWorldTransform(
              instance_id, matrix);
        };
    callbacks.start_shard_animation =
        [this](const std::uint32_t instance_id,
           std::function<void()> completion) {
          (void)m2_system_.SetEffectEmittersEnabled(instance_id, true);
          (void)m2_system_.SetTriggeredEventCallback(
              instance_id,
              [this, instance_id](const openwow::render::m2::M2TriggeredEvent& event) {
                (void)SpellVisualKit_AreaModel_SoundEventCallback(
                    sound_runtime_, area_model_sound_throttle_,
                    instance_id, static_cast<std::uint32_t>(event.bone),
                    event.identifier, event.data, event.world_position.data());
              });
          (void)m2_system_.SetAnimationCompletionCallback(
              instance_id,
              [completion = std::move(completion)](std::uint32_t) {
                if (completion) {
                  completion();
                }
              });

          auto animation =
              m2_system_.QueryInstanceAnimationInfo(instance_id);
          const std::uint32_t animation_id =
              animation.status == openwow::render::m2::M2ResultStatus::kReady
                  ? animation.info.resolved_animation_id
                  : 0u;
          (void)m2_system_.SetAnimation(instance_id, animation_id, 1.0f);
          animation = m2_system_.QueryInstanceAnimationInfo(instance_id);
          if (animation.status ==
                  openwow::render::m2::M2ResultStatus::kReady &&
              animation.info.duration_ms != 0u) {
            return animation.info.duration_ms;
          }
          return 1000u;
        };
    SpellVisuals_SetAreaModelRenderCallbacks(std::move(callbacks));
  }
}

void WorldScene::RenderWaterParticulates(const render::api::RendererContext* renderer_context,
                                         const std::uint8_t view_id,
                                         const float* view_mtx,
                                         const float* proj_mtx,
                                         const float screen_w,
                                         const float screen_h) {
  if (!initialized_) {
    return;
  }

  const bool homogeneous_depth =
      renderer_context == nullptr ||
      renderer_context->Capabilities().homogeneous_depth;
  const auto matrices = render::ViewProjection::CopyOf(
      view_mtx, proj_mtx, homogeneous_depth);
  const auto bgfx_matrices = matrices.AsBgfxColumnMajor();

  (void)render::ConfigureRendererContextView(
      renderer_context,
      view_id,
      render::RendererViewClearFlags::kNone,
      0x00000000u,
      1.0f,
      0,
      static_cast<std::uint32_t>(screen_w),
      static_cast<std::uint32_t>(screen_h),
      bgfx_matrices.view.data(),
      bgfx_matrices.projection.data());

  const auto& environment = presentation_snapshot_.environment;
  const std::array<float, 4> fog_params{environment.fog_start,
                                        environment.fog_end,
                                        environment.fog_density, 0.0f};
  water_particulates_.Render(view_id, bgfx_matrices.view.data(),
                             bgfx_matrices.projection.data(), fog_params,
                             environment.fog_color);
}

void WorldScene::UpdateNameplates(const ObjectManager& obj_mgr,
                                  WorldSession& world_session,
                                  uint64_t target_guid,
                                  uint64_t mouseover_guid,
                                  bool show_world_nameplates,
                                  float player_x, float player_y,
                                  float player_z) {

  auto presentation =
      render_resources_->presentation_publisher.PublishOverheadText(
          obj_mgr, world_session, target_guid, mouseover_guid,
          show_world_nameplates,
          nameplate_renderer_.show_class_color_in_nameplate(),
          player_x, player_y, player_z);
  unit_name_renderer_.ConsumePresentation(std::move(presentation.unit_names));
  nameplate_renderer_.ConsumePresentation(std::move(presentation.nameplates));
}

void WorldScene::BeginSelectionDecals() { selection_circle_.BeginFrame(); }

void WorldScene::SubmitSelectionDecal(const render::SelectionDecal& decal) {
  selection_circle_.Submit(decal);
}

}

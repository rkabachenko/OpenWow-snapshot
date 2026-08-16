#include "openwow/render/m2/m2_render_validation.h"

#include "openwow/foundation/diagnostics/logging.h"
#include "openwow/render/m2/m2_material_pipeline.h"
#include "openwow/render/m2/m2_particle_system.h"
#include "openwow/render/m2/m2_runtime_state.h"
#include "openwow/render/m2/m2_skin_geometry.h"
#include "openwow/render/m2/m2_texture_unit_preparation.h"

#include <cmath>
#include <limits>
#include <sstream>

namespace openwow::render::m2 {
namespace {

constexpr std::uint8_t kParticleEmitterTypeSpline = 3u;
constexpr std::size_t kParticleBezierMinPointCount = 4u;
constexpr std::size_t kParticleBezierSharedPointStride = 3u;
constexpr std::uint32_t kTextureTypeFilename = 0u;

void SetIssue(M2RenderValidationIssue* issue, const M2ResultReason reason,
              std::string detail) {
  if (issue != nullptr) {
    issue->reason = reason;
    issue->detail = std::move(detail);
  }
}

[[nodiscard]] M2RenderPreparationResult Failure(const M2ResultStatus status,
                                                const M2ResultReason reason,
                                                std::string detail) {
  return {.status = status, .reason = reason, .detail = std::move(detail)};
}

[[nodiscard]] M2ResultStatus StatusForIssue(const M2ResultReason reason) noexcept {
  switch (reason) {
    case M2ResultReason::kUnsupportedSkinProfile:
    case M2ResultReason::kUnsupportedBlendMode:
    case M2ResultReason::kUnsupportedShader:
    case M2ResultReason::kParticleUnsupported:
    case M2ResultReason::kRibbonUnsupported:
    case M2ResultReason::kNoDrawableGeometry:
      return M2ResultStatus::kUnsupported;
    case M2ResultReason::kTextureNotReady:
    case M2ResultReason::kShaderNotReady:
      return M2ResultStatus::kNotReady;
    case M2ResultReason::kNone:
      return M2ResultStatus::kReady;
    default:
      return M2ResultStatus::kFailed;
  }
}

[[nodiscard]] std::string FormatPreparationDiagnostics(
    const M2TextureUnitPreparationResult& result) {
  std::ostringstream out;
  bool wrote = false;
  const auto prefix = [&] {
    if (wrote) {
      out << ';';
    }
    wrote = true;
  };
  if (result.texture_combo_zero_fixup_count != 0u) {
    prefix();
    out << " texture_pairs=" << result.texture_combo_zero_fixup_count
        << " first_texture_unit=" << result.first_texture_combo_zero_fixup_unit;
  }
  if (result.uv_combo_zero_fixup_count != 0u) {
    prefix();
    out << " uv_pairs=" << result.uv_combo_zero_fixup_count
        << " first_uv_texture_unit=" << result.first_uv_combo_zero_fixup_unit;
  }
  if (result.invalid_texture_unit_mode_count != 0u) {
    prefix();
    out << " invalid_texture_unit_modes=" << result.invalid_texture_unit_mode_count
        << " first_mode_texture_unit=" << result.first_invalid_texture_unit_mode_unit;
  }
  if (result.missing_shader_combo_count != 0u) {
    prefix();
    out << " missing_shader_combos=" << result.missing_shader_combo_count
        << " first_shader_texture_unit=" << result.first_missing_shader_combo_unit;
  }
  if (result.missing_tex_unit_count != 0u) {
    prefix();
    out << " missing_tex_units=" << result.missing_tex_unit_count
        << " first_tex_unit_texture_unit=" << result.first_missing_tex_unit;
  }
  if (result.missing_render_flags_count != 0u) {
    prefix();
    out << " missing_render_flags=" << result.missing_render_flags_count
        << " first_render_flags_texture_unit=" << result.first_missing_render_flags_unit;
  }
  return wrote ? out.str() : "none";
}

[[nodiscard]] bool ValidateTextureIndex(const data::model::M2Model& model,
                                        const std::uint16_t texture_index,
                                        const std::string_view label,
                                        M2RenderValidationIssue* issue) {
  if (texture_index < model.textures.size()) {
    return true;
  }
  SetIssue(issue, M2ResultReason::kMissingTexture,
           std::string(label) + " texture index out of range: " +
               std::to_string(texture_index));
  return false;
}

[[nodiscard]] bool ValidateUvAnimationIndex(
    const data::model::M2Model& model,
    const std::optional<std::uint16_t> uv_animation_index,
    const std::string_view label, M2RenderValidationIssue* issue) {
  if (!uv_animation_index.has_value() || model.uv_animations.empty() ||
      *uv_animation_index < model.uv_animations.size()) {
    return true;
  }
  SetIssue(issue, M2ResultReason::kInvalidTextureUnit,
           std::string(label) + " uv animation index out of range: " +
               std::to_string(*uv_animation_index));
  return false;
}

[[nodiscard]] bool StaticVec3IsFinite(const float (&values)[3]) noexcept {
  return std::isfinite(values[0]) && std::isfinite(values[1]) &&
         std::isfinite(values[2]);
}

[[nodiscard]] bool TrackValueIsFinite(const float value) noexcept {
  return std::isfinite(value);
}
[[nodiscard]] bool TrackValueIsFinite(const data::model::M2Vec2& value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y);
}
[[nodiscard]] bool TrackValueIsFinite(const data::model::M2Vec3& value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

template <typename T>
[[nodiscard]] bool ValidateFiniteTrack(const data::model::M2Track<T>& track,
                                       const std::string_view label,
                                       const std::string_view field,
                                       const M2ResultReason reason,
                                       M2RenderValidationIssue* issue) {

  for (const auto& value : track.key_values) {
    if (!TrackValueIsFinite(value)) {
      SetIssue(issue, reason, std::string(label) + " " + std::string(field) +
                                  " track values must be finite");
      return false;
    }
  }
  return true;
}

template <typename T>
[[nodiscard]] bool ValidateLifetimeTrackStructure(
    const data::model::M2ParticleLifetimeTrack<T>& track,
    const std::string_view label, const std::string_view field,
    M2RenderValidationIssue* issue) {
  if (track.times.size() != track.values.size()) {
    SetIssue(issue, M2ResultReason::kInvalidParticle,
             std::string(label) + " " + std::string(field) +
                 " lifetime timestamp/value count mismatch");
    return false;
  }
  for (std::size_t index = 0; index < track.times.size(); ++index) {
    if (track.times[index] > 32767u ||
        (index > 0u && track.times[index] < track.times[index - 1u])) {
      SetIssue(issue, M2ResultReason::kInvalidParticle,
               std::string(label) + " " + std::string(field) +
                   " lifetime timestamps must be monotonic in [0,32767]");
      return false;
    }
  }
  return true;
}

template <typename T>
[[nodiscard]] bool ValidateFiniteLifetimeValues(
    const data::model::M2ParticleLifetimeTrack<T>& track,
    const std::string_view label, const std::string_view field,
    M2RenderValidationIssue* issue) {
  for (const auto& value : track.values) {
    if (!TrackValueIsFinite(value)) {
      SetIssue(issue, M2ResultReason::kInvalidParticle,
               std::string(label) + " " + std::string(field) +
                   " lifetime values must be finite");
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool ValidateParticleEmitter(
    const data::model::M2Model& model,
    const data::model::M2ParticleEmitter& emitter, const std::size_t emitter_index,
    M2RenderValidationIssue* issue) {
  const std::string label = "particle_emitter=" + std::to_string(emitter_index);
  if (emitter.bone >= model.bones.size()) {
    SetIssue(issue, M2ResultReason::kInvalidParticle,
             label + " bone index out of range: " + std::to_string(emitter.bone));
    return false;
  }
  if (!ValidateTextureIndex(model, emitter.texture, label, issue)) {
    return false;
  }
  if (!ResolveM2ParticleRenderTypeFromBlendMode(emitter.blending_type).has_value()) {
    SetIssue(issue, M2ResultReason::kUnsupportedBlendMode,
             label + " unsupported blend mode: " +
                 std::to_string(emitter.blending_type));
    return false;
  }
  if (emitter.emitter_type > kParticleEmitterTypeSpline) {
    SetIssue(issue, M2ResultReason::kParticleUnsupported,
             label + " unsupported emitter type: " +
                 std::to_string(emitter.emitter_type));
    return false;
  }
  if (emitter.emitter_type == kParticleEmitterTypeSpline) {
    const std::size_t count = emitter.spline_points.size();
    if (count < kParticleBezierMinPointCount ||
        ((count - 1u) % kParticleBezierSharedPointStride) != 0u) {
      SetIssue(issue, M2ResultReason::kInvalidParticle,
               label + " type-3 particle path point count must be 3n+1 and at least 4");
      return false;
    }
    for (std::size_t index = 0; index < count; ++index) {
      if (!TrackValueIsFinite(emitter.spline_points[index])) {
        SetIssue(issue, M2ResultReason::kInvalidParticle,
                 label + " type-3 particle path point " + std::to_string(index) +
                     " must be finite");
        return false;
      }
    }
  }
  if (!IsM2ParticleAtlasDimensionValid(emitter.texture_tile_rows)) {
    SetIssue(issue, M2ResultReason::kInvalidParticle,
             label + " texture tile rows must be a non-zero power of two: " +
                 std::to_string(emitter.texture_tile_rows));
    return false;
  }
  if (!IsM2ParticleAtlasDimensionValid(emitter.texture_tile_cols)) {
    SetIssue(issue, M2ResultReason::kInvalidParticle,
             label + " texture tile columns must be a non-zero power of two: " +
                 std::to_string(emitter.texture_tile_cols));
    return false;
  }
  if (!StaticVec3IsFinite(emitter.position)) {
    SetIssue(issue, M2ResultReason::kInvalidParticle,
             label + " emitter position values must be finite");
    return false;
  }
  if (!ValidateFiniteTrack(emitter.emission_speed, label, "emission_speed",
                           M2ResultReason::kInvalidParticle, issue) ||
      !ValidateFiniteTrack(emitter.speed_variation, label, "speed_variation",
                           M2ResultReason::kInvalidParticle, issue) ||
      !ValidateFiniteTrack(emitter.vertical_range, label, "vertical_range",
                           M2ResultReason::kInvalidParticle, issue) ||
      !ValidateFiniteTrack(emitter.horizontal_range, label, "horizontal_range",
                           M2ResultReason::kInvalidParticle, issue) ||
      !ValidateFiniteTrack(emitter.gravity, label, "gravity",
                           M2ResultReason::kInvalidParticle, issue) ||
      !ValidateFiniteTrack(emitter.lifespan, label, "lifespan",
                           M2ResultReason::kInvalidParticle, issue) ||
      !ValidateFiniteTrack(emitter.emission_rate, label, "emission_rate",
                           M2ResultReason::kInvalidParticle, issue) ||
      !ValidateFiniteTrack(emitter.emission_area_length, label, "emission_area_length",
                           M2ResultReason::kInvalidParticle, issue) ||
      !ValidateFiniteTrack(emitter.emission_area_width, label, "emission_area_width",
                           M2ResultReason::kInvalidParticle, issue) ||
      !ValidateFiniteTrack(emitter.z_source, label, "z_source",
                           M2ResultReason::kInvalidParticle, issue) ||
      !ValidateLifetimeTrackStructure(emitter.color_track, label, "color", issue) ||
      !ValidateLifetimeTrackStructure(emitter.alpha_track, label, "alpha", issue) ||
      !ValidateLifetimeTrackStructure(emitter.scale_track, label, "scale", issue) ||
      !ValidateLifetimeTrackStructure(emitter.head_cell_track, label, "head_cell", issue) ||
      !ValidateLifetimeTrackStructure(emitter.tail_cell_track, label, "tail_cell", issue) ||
      !ValidateFiniteLifetimeValues(emitter.color_track, label, "color", issue) ||
      !ValidateFiniteLifetimeValues(emitter.scale_track, label, "scale", issue)) {
    return false;
  }
  if (!std::isfinite(emitter.lifespan_vary) ||
      !std::isfinite(emitter.emission_rate_vary) || !std::isfinite(emitter.tail_length) ||
      !std::isfinite(emitter.drag) || !std::isfinite(emitter.base_spin) ||
      !std::isfinite(emitter.base_spin_vary) || !std::isfinite(emitter.spin) ||
      !std::isfinite(emitter.spin_vary) || !StaticVec3IsFinite(emitter.wind_vector) ||
      !std::isfinite(emitter.wind_time) || !std::isfinite(emitter.follow_speed1) ||
      !std::isfinite(emitter.follow_scale1) || !std::isfinite(emitter.follow_speed2) ||
      !std::isfinite(emitter.follow_scale2)) {
    SetIssue(issue, M2ResultReason::kInvalidParticle,
             label + " static particle effect values must be finite");
    return false;
  }
  if (!std::isfinite(emitter.twinkle_speed) ||
      !std::isfinite(emitter.twinkle_percent) ||
      !std::isfinite(emitter.twinkle_scale_min) ||
      !std::isfinite(emitter.twinkle_scale_max)) {
    SetIssue(issue, M2ResultReason::kInvalidParticle,
             label + " twinkle particle modulation values must be finite");
    return false;
  }
  if (!std::isfinite(emitter.scale_vary.x) || !std::isfinite(emitter.scale_vary.y)) {
    SetIssue(issue, M2ResultReason::kInvalidParticle,
             label + " scale variation must be finite");
    return false;
  }
  if (!StaticVec3IsFinite(emitter.tumble_min) ||
      !StaticVec3IsFinite(emitter.tumble_max)) {
    SetIssue(issue, M2ResultReason::kInvalidParticle,
             label + " tumble particle rotation values must be finite");
    return false;
  }
  return true;
}

[[nodiscard]] bool ValidateRibbonEmitter(
    const data::model::M2Model& model,
    const data::model::M2RibbonEmitter& ribbon, const std::size_t ribbon_index,
    M2RenderValidationIssue* issue) {
  const std::string label = "ribbon_emitter=" + std::to_string(ribbon_index);
  if (ribbon.bone_index >= model.bones.size()) {
    SetIssue(issue, M2ResultReason::kInvalidRibbon,
             label + " bone index out of range: " +
                 std::to_string(ribbon.bone_index));
    return false;
  }
  if (!StaticVec3IsFinite(ribbon.position)) {
    SetIssue(issue, M2ResultReason::kInvalidRibbon,
             label + " emitter position values must be finite");
    return false;
  }
  if (ribbon.texture_indices.empty()) {
    SetIssue(issue, M2ResultReason::kMissingTexture,
             label + " has no texture indices");
    return false;
  }
  for (const std::uint16_t texture_index : ribbon.texture_indices) {
    if (!ValidateTextureIndex(model, texture_index, label, issue)) {
      return false;
    }
  }
  if (ribbon.material_indices.empty()) {
    SetIssue(issue, M2ResultReason::kInvalidMaterial,
             label + " has no material indices");
    return false;
  }
  if (ribbon.material_indices.size() < ribbon.texture_indices.size()) {
    SetIssue(issue, M2ResultReason::kInvalidMaterial,
             label + " material index count is smaller than texture pass count");
    return false;
  }
  for (std::size_t index = 0; index < ribbon.texture_indices.size(); ++index) {
    const std::uint16_t material_index = ribbon.material_indices[index];
    if (material_index >= model.render_flags.size()) {
      SetIssue(issue, M2ResultReason::kInvalidMaterial,
               label + " pass " + std::to_string(index) +
                   " material index out of range: " + std::to_string(material_index));
      return false;
    }
    if (model.render_flags[material_index].blend_mode >=
        static_cast<std::uint16_t>(M2BlendMode::Count)) {
      SetIssue(issue, M2ResultReason::kUnsupportedBlendMode,
               label + " pass " + std::to_string(index) +
                   " unsupported blend mode: " +
                   std::to_string(model.render_flags[material_index].blend_mode));
      return false;
    }
  }
  if (ribbon.texture_rows == 0u || ribbon.texture_cols == 0u) {
    SetIssue(issue, M2ResultReason::kInvalidRibbon,
             label + " texture atlas dimensions must be non-zero");
    return false;
  }
  if (!std::isfinite(ribbon.edges_per_second) ||
      !std::isfinite(ribbon.edge_lifetime) || !std::isfinite(ribbon.gravity)) {
    SetIssue(issue, M2ResultReason::kInvalidRibbon,
             label + " edge rate, lifetime, and gravity must be finite");
    return false;
  }
  if (!ValidateFiniteTrack(ribbon.color, label, "color",
                           M2ResultReason::kInvalidRibbon, issue) ||
      !ValidateFiniteTrack(ribbon.height_above, label, "height_above",
                           M2ResultReason::kInvalidRibbon, issue) ||
      !ValidateFiniteTrack(ribbon.height_below, label, "height_below",
                           M2ResultReason::kInvalidRibbon, issue)) {
    return false;
  }
  const std::uint32_t capacity =
      ComputeM2RibbonSegmentCapacity(ribbon.edges_per_second, ribbon.edge_lifetime);
  if (capacity == 0u) {
    SetIssue(issue, M2ResultReason::kInvalidRibbon,
             label + " edge rate and lifetime do not produce a valid retail trail capacity");
    return false;
  }
  if (capacity == std::numeric_limits<std::uint32_t>::max()) {
    SetIssue(issue, M2ResultReason::kRibbonUnsupported,
             label + " edge rate and lifetime produce an unsupported segment count");
    return false;
  }
  const std::uint32_t tile_count =
      static_cast<std::uint32_t>(ribbon.texture_rows) * ribbon.texture_cols;
  for (const std::uint16_t slot : ribbon.tex_slot.key_values) {
    if (slot >= tile_count) {
      SetIssue(issue, M2ResultReason::kInvalidRibbon,
               label + " texture slot out of atlas range: " +
                   std::to_string(slot));
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<M2TexGen> ToTexGen(
    const M2ResolvedSkinTextureSource source) {
  switch (source) {
    case M2ResolvedSkinTextureSource::kTexCoord0: return M2TexGen::TexCoord0;
    case M2ResolvedSkinTextureSource::kTexCoord1: return M2TexGen::TexCoord1;
    case M2ResolvedSkinTextureSource::kEnvSphere: return M2TexGen::EnvSphere;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<int> ToShaderVariant(
    const M2ResolvedSkinTextureShaderKind kind) noexcept {
  switch (kind) {
    case M2ResolvedSkinTextureShaderKind::kDiffuseEnv:
    case M2ResolvedSkinTextureShaderKind::kConsumedTextureUnit: return 0;
    case M2ResolvedSkinTextureShaderKind::kDiffuseT1EnvOpaqueMod2xNAAlpha: return 1;
    case M2ResolvedSkinTextureShaderKind::kDiffuseT1EnvOpaqueAddAlpha: return 2;
    case M2ResolvedSkinTextureShaderKind::kDiffuseT1EnvOpaqueAddAlphaAlpha: return 3;
  }
  return std::nullopt;
}

}

bool ValidateM2TextureUnitForRender(
    const data::model::M2Model& model, const data::model::M2Skin& skin,
    const data::model::SkinTextureUnit& texture_unit,
    const std::size_t texture_unit_index, M2RenderValidationIssue* issue) {
  const std::string label = "texture_unit=" + std::to_string(texture_unit_index);
  if (texture_unit.submesh_index >= skin.submeshes.size()) {
    SetIssue(issue, M2ResultReason::kInvalidTextureUnit,
             label + " submesh index out of range: " +
                 std::to_string(texture_unit.submesh_index));
    return false;
  }
  if (!IsSupportedM2SkinTextureUnitMode(texture_unit.mode)) {
    SetIssue(issue, M2ResultReason::kInvalidTextureUnit,
             label + " invalid texture-unit mode: " + std::to_string(texture_unit.mode));
    return false;
  }
  if (texture_unit.render_flags_index >= model.render_flags.size()) {
    SetIssue(issue, M2ResultReason::kInvalidMaterial,
             label + " render flags index out of range: " +
                 std::to_string(texture_unit.render_flags_index));
    return false;
  }
  const auto& render_flags = model.render_flags[texture_unit.render_flags_index];
  if (render_flags.blend_mode >= static_cast<std::uint16_t>(M2BlendMode::Count)) {
    SetIssue(issue, M2ResultReason::kUnsupportedBlendMode,
             label + " unsupported blend mode: " +
                 std::to_string(render_flags.blend_mode));
    return false;
  }
  const auto shader = ResolveM2SkinTextureUnitShader(model, texture_unit);
  if (!shader.valid) {
    SetIssue(issue, M2ResultReason::kUnsupportedShader,
             label + " invalid shader id: " + std::to_string(shader.shader_id));
    return false;
  }
  if (!shader.draws || shader.texture_count == 0u) {
    return true;
  }
  const auto combos = ResolveM2SkinTextureUnitCombos(model, texture_unit);
  if (!ValidateTextureIndex(model, combos.primary_texture_index, "primary", issue)) {
    if (issue != nullptr) issue->detail = label + " " + issue->detail;
    return false;
  }
  if (shader.texture_count > 1u) {
    if (!combos.secondary_texture_index.has_value()) {
      SetIssue(issue, M2ResultReason::kMissingTexture,
               label + " secondary texture index missing");
      return false;
    }
    if (!ValidateTextureIndex(model, *combos.secondary_texture_index, "secondary", issue)) {
      if (issue != nullptr) issue->detail = label + " " + issue->detail;
      return false;
    }
  }
  if (!ValidateUvAnimationIndex(model, combos.primary_uv_animation_index, "primary", issue) ||
      !ValidateUvAnimationIndex(model, combos.secondary_uv_animation_index, "secondary", issue)) {
    if (issue != nullptr) issue->detail = label + " " + issue->detail;
    return false;
  }
  return true;
}

bool ValidateM2EffectRenderInputs(const data::model::M2Model& model,
                                  M2RenderValidationIssue* issue) {
  for (std::size_t index = 0; index < model.particle_emitters.size(); ++index) {
    if (!ValidateParticleEmitter(model, model.particle_emitters[index], index, issue)) {
      return false;
    }
  }
  for (std::size_t index = 0; index < model.ribbon_emitters.size(); ++index) {
    if (!ValidateRibbonEmitter(model, model.ribbon_emitters[index], index, issue)) {
      return false;
    }
  }
  return true;
}

M2RenderPreparationResult PrepareM2RenderPackage(
    const data::model::M2Model& model, const data::model::M2Skin& skin,
    detail::M2ModelResource& resource) {
  auto retail_model = model;
  auto retail_skin = skin;
  const auto preparation = PrepareM2SkinTextureUnitsForRender(retail_model, retail_skin);
  if (preparation.HasInvalidRenderInputs()) {
    const std::string detail = FormatPreparationDiagnostics(preparation);
    diagnostics::Log(diagnostics::LogLevel::kWarn,
                     "M2System: invalid M2 texture-unit preparation: " + detail);
    return Failure(M2ResultStatus::kFailed, M2ResultReason::kInvalidTextureUnit, detail);
  }
  if (preparation.HasRetailZeroComboFixup()) {
    diagnostics::Log(diagnostics::LogLevel::kWarn,
                     "M2System: retail zero-slot combo fixup: " +
                         FormatPreparationDiagnostics(preparation));
  }
  M2SkinGeometry geometry;
  const bool has_geometry = BuildM2SkinGeometry(retail_model, retail_skin, &geometry);
  if (!has_geometry && (!retail_skin.indices.empty() || !retail_skin.triangles.empty())) {
    diagnostics::Log(diagnostics::LogLevel::kWarn,
                     "M2System: skin geometry init failed: " + geometry.error);
    return Failure(M2ResultStatus::kFailed, M2ResultReason::kInvalidSkinGeometry,
                   geometry.error);
  }
  resource.has_bones = !retail_model.bones.empty();
  resource.has_billboard_bones = detail::ComputeM2ModelHasBillboardBones(retail_model);
  resource.has_live_global_sequence = detail::ComputeM2ModelHasLiveGlobalSequence(retail_model);
  resource.model_data = std::move(retail_model);
  resource.skin_data = has_geometry ? geometry.normalized_skin : retail_skin;
  resource.skin_geometry = has_geometry ? geometry : M2SkinGeometry{};
  if (has_geometry) {
    resource.model_data.vertices = geometry.vertices;
  }
  resource.bounds_min = {model.header.bounding_box_min[0], model.header.bounding_box_min[1],
                         model.header.bounding_box_min[2]};
  resource.bounds_max = {model.header.bounding_box_max[0], model.header.bounding_box_max[1],
                         model.header.bounding_box_max[2]};
  resource.bounds_radius = model.header.bounding_sphere_radius;
  if (has_geometry && resource.skin_data.texture_units.empty()) {
    diagnostics::Log(diagnostics::LogLevel::kWarn,
                     "M2System: render skin has no texture units");
    return Failure(M2ResultStatus::kUnsupported, M2ResultReason::kNoDrawableGeometry,
                   "render skin has no texture units");
  }
  M2RenderValidationIssue issue;
  if (!ValidateM2EffectRenderInputs(resource.model_data, &issue)) {
    const M2ResultStatus status = StatusForIssue(issue.reason);
    diagnostics::Log(diagnostics::LogLevel::kWarn,
                     std::string("M2System: ") + M2ResultStatusName(status) +
                         " M2 effect render input: " + issue.detail);
    return Failure(status, issue.reason, issue.detail);
  }

  const auto& render_model = resource.model_data;
  const auto& render_skin = resource.skin_data;
  resource.render_batches.clear();
  resource.projected_batches.clear();

  const std::size_t render_batch_texture_unit_count =
      has_geometry ? render_skin.texture_units.size() : 0u;
  for (std::size_t index = 0; index < render_batch_texture_unit_count; ++index) {
    const auto& texture_unit = render_skin.texture_units[index];
    const auto shader = ResolveM2SkinTextureUnitShader(render_model, texture_unit);
    issue = {};
    if (!ValidateM2TextureUnitForRender(render_model, render_skin, texture_unit, index,
                                        &issue)) {
      const M2ResultStatus status = StatusForIssue(issue.reason);
      diagnostics::Log(diagnostics::LogLevel::kWarn,
                       std::string("M2System: ") + M2ResultStatusName(status) +
                           " M2 texture-unit render input: " + issue.detail);
      return Failure(status, issue.reason, issue.detail);
    }
    if (!shader.draws) {
      continue;
    }
    detail::M2ModelResource::RenderBatch batch;
    const auto& submesh = render_skin.submeshes[texture_unit.submesh_index];
    batch.texture_unit_index = index;
    batch.submesh_index = texture_unit.submesh_index;
    batch.start_index = submesh.index_start;
    batch.index_count = submesh.index_count;
    batch.geoset_id = submesh.section_id;
    batch.texture_unit_order_key =
        static_cast<std::uint32_t>(index) << kM2TextureUnitOrderKeyShift;
    batch.render_pass_order_key =
        static_cast<std::uint32_t>(texture_unit.render_flags_index)
        << kM2RenderPassOrderKeyShift;
    const auto combos = ResolveM2SkinTextureUnitCombos(render_model, texture_unit);
    batch.texture0_index = combos.primary_texture_index;
    batch.texture1_index = combos.secondary_texture_index.value_or(0u);
    const auto uv_index = [&render_model](const std::optional<std::uint16_t> value) {
      return !value.has_value() || render_model.uv_animations.empty()
                 ? -1
                 : static_cast<int>(*value);
    };
    batch.uv_animation0_index = uv_index(combos.primary_uv_animation_index);
    batch.uv_animation1_index = uv_index(combos.secondary_uv_animation_index);
    const auto& flags = render_model.render_flags[texture_unit.render_flags_index];
    batch.blend_mode = static_cast<M2BlendMode>(flags.blend_mode);
    batch.material_flags = flags.flags;
    batch.shader_id = shader.shader_id;
    batch.shader_op1 = shader.op1;
    batch.shader_op2 = shader.op2;
    batch.texture_count = shader.texture_count;
    const auto variant = ToShaderVariant(shader.shader_kind);
    const auto tex0_gen = ToTexGen(shader.layer0_source);
    const auto tex1_gen = ToTexGen(shader.layer1_source);
    if (!variant.has_value() || !tex0_gen.has_value() || !tex1_gen.has_value()) {
      return Failure(M2ResultStatus::kUnsupported,
                     M2ResultReason::kUnsupportedShader,
                     "invalid resolved shader source or variant");
    }
    batch.shader_variant = *variant;
    batch.tex0_gen = *tex0_gen;
    batch.tex1_gen = *tex1_gen;

    if ((texture_unit.flags & kM2TextureUnitFlagProjectedTexture) != 0u) {
      resource.projected_batches.push_back(batch);
    } else {
      resource.render_batches.push_back(batch);
    }
  }
  if (has_geometry && resource.render_batches.empty() &&
      resource.projected_batches.empty()) {
    diagnostics::Log(diagnostics::LogLevel::kWarn,
                     "M2System: render skin has no drawable texture units");
    return Failure(M2ResultStatus::kUnsupported, M2ResultReason::kNoDrawableGeometry,
                   "render skin has no drawable texture units");
  }
  resource.gpu_textures.assign(model.textures.size(), BGFX_INVALID_HANDLE);
  resource.gpu_texture_leases.assign(model.textures.size(), TextureLease{});
  for (std::size_t index = 0; index < model.textures.size(); ++index) {
    const auto& texture = model.textures[index];
    if (texture.type == kTextureTypeFilename && texture.name_text.empty()) {
      const std::string detail =
          "regular model texture has no file path at index " + std::to_string(index);
      diagnostics::Log(diagnostics::LogLevel::kWarn, "M2System: " + detail);
      return Failure(M2ResultStatus::kFailed, M2ResultReason::kMissingTexture, detail);
    }
  }
  resource.loaded = true;
  return {};
}

}

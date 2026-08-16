#pragma once

#include "openwow/data/model/m2_model.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace openwow::render::m2 {

struct M2ResolvedSkinTextureUnitCombos {
  std::uint16_t primary_texture_index{0};
  std::optional<std::uint16_t> secondary_texture_index;
  std::optional<std::uint16_t> primary_uv_animation_index;
  std::optional<std::uint16_t> secondary_uv_animation_index;
};

enum class M2ResolvedSkinTextureSource : std::uint8_t {
  kTexCoord0 = 0,
  kTexCoord1 = 1,
  kEnvSphere = 2,
};

enum class M2ResolvedSkinTextureShaderKind : std::uint8_t {
  kDiffuseEnv = 0,
  kConsumedTextureUnit,
  kDiffuseT1EnvOpaqueMod2xNAAlpha,
  kDiffuseT1EnvOpaqueAddAlpha,
  kDiffuseT1EnvOpaqueAddAlphaAlpha,
};

struct M2ResolvedSkinTextureUnitShader {
  std::uint16_t shader_id{0};
  std::uint8_t texture_count{1};
  std::uint8_t op1{0};
  std::uint8_t op2{0};
  M2ResolvedSkinTextureSource layer0_source{M2ResolvedSkinTextureSource::kTexCoord0};
  M2ResolvedSkinTextureSource layer1_source{M2ResolvedSkinTextureSource::kTexCoord1};
  M2ResolvedSkinTextureShaderKind shader_kind{M2ResolvedSkinTextureShaderKind::kDiffuseEnv};
  bool valid{true};
  bool draws{true};
};

struct M2TextureUnitPreparationResult {
  std::uint32_t texture_combo_zero_fixup_count{0};
  std::uint32_t uv_combo_zero_fixup_count{0};
  std::uint32_t invalid_texture_unit_mode_count{0};
  std::uint32_t missing_shader_combo_count{0};
  std::uint32_t missing_tex_unit_count{0};
  std::uint32_t missing_render_flags_count{0};
  std::size_t first_texture_combo_zero_fixup_unit{0};
  std::size_t first_uv_combo_zero_fixup_unit{0};
  std::size_t first_invalid_texture_unit_mode_unit{0};
  std::size_t first_missing_shader_combo_unit{0};
  std::size_t first_missing_tex_unit{0};
  std::size_t first_missing_render_flags_unit{0};

  [[nodiscard]] bool HasRetailZeroComboFixup() const noexcept {
    return texture_combo_zero_fixup_count != 0u || uv_combo_zero_fixup_count != 0u;
  }

  [[nodiscard]] bool HasInvalidRenderInputs() const noexcept {
    return invalid_texture_unit_mode_count != 0u || missing_shader_combo_count != 0u ||
           missing_tex_unit_count != 0u || missing_render_flags_count != 0u;
  }
};

[[nodiscard]] bool IsSupportedM2SkinTextureUnitMode(std::uint16_t mode) noexcept;

[[nodiscard]] M2ResolvedSkinTextureUnitCombos ResolveM2SkinTextureUnitCombos(
    const data::model::M2Model &model,
    const data::model::SkinTextureUnit &texture_unit);

M2TextureUnitPreparationResult PrepareM2SkinTextureUnitsForRender(
    data::model::M2Model &model,
    data::model::M2Skin &skin);

[[nodiscard]] M2ResolvedSkinTextureUnitShader ResolveM2SkinTextureUnitShader(
    const data::model::M2Model &model,
    const data::model::SkinTextureUnit &texture_unit);

}

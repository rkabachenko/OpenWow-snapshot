#include "openwow/render/m2/m2_texture_unit_preparation.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace openwow::render::m2 {

bool IsSupportedM2SkinTextureUnitMode(const std::uint16_t mode) noexcept {
  constexpr std::uint16_t kNoTexture = 0u;
  constexpr std::uint16_t kSingleLayer = 1u;
  constexpr std::uint16_t kDualLayer = 2u;
  return mode == kNoTexture || mode == kSingleLayer || mode == kDualLayer;
}

namespace {

constexpr std::uint32_t kM2GlobalFlagHasTextureCombinerCombos = 0x8u;

constexpr std::uint16_t kM2SkinShaderHighBit = 0x8000u;
constexpr std::uint16_t kM2SkinShaderIdMask = 0x7FFFu;
constexpr std::uint16_t kM2SkinShaderTexCoord1Bit = 0x4000u;
constexpr std::uint16_t kM2SkinShaderEnvBit = 0x8u;
constexpr std::uint16_t kM2SkinShaderOpMask = 0x7u;
constexpr std::uint16_t kM2SkinShaderLayerMask = 0xFu;
constexpr std::uint16_t kM2SkinShaderClearPrimaryOpMask = 0xFF8Fu;

constexpr std::uint16_t kM2SkinShaderConsumedTextureUnit = 0x8000u;
constexpr std::uint16_t kM2SkinShaderOpaqueMod2xNAAlpha = 0x8001u;
constexpr std::uint16_t kM2SkinShaderOpaqueAddAlpha = 0x8002u;
constexpr std::uint16_t kM2SkinShaderOpaqueAddAlphaAlpha = 0x8003u;
constexpr std::uint16_t kM2SkinShaderOpaqueMod2xNA = 0x000Eu;

constexpr std::uint16_t kM2TextureUnitModeNoTexture = 0u;
constexpr std::uint16_t kM2TextureUnitModeSingleLayer = 1u;
constexpr std::uint16_t kM2TextureUnitModeDualLayer = 2u;
constexpr std::uint16_t kM2TextureUnitEnvThreshold = 2u;
constexpr std::uint16_t kM2BlendOpaque = 0u;
constexpr std::uint16_t kM2BlendAlphaKey = 1u;
constexpr std::uint16_t kM2BlendAlpha = 2u;
constexpr std::uint16_t kM2BlendAdd = 4u;
constexpr std::uint16_t kM2BlendMod2x = 6u;
constexpr std::uint16_t kM2MaterialFlagUnlit = 0x1u;
constexpr std::uint16_t kM2CombinerOpOpaque = 0u;
constexpr std::uint16_t kM2CombinerOpMod = 1u;
constexpr std::uint16_t kM2CombinerOpAdd = 3u;
constexpr std::uint16_t kM2CombinerOpMod2x = 4u;
constexpr std::uint16_t kM2CombinerOpMod2xNA = 6u;

[[nodiscard]] std::uint16_t ResolveLookupOrIdentity(
    const std::vector<std::uint16_t> &lookup,
    const std::size_t combo_index) {
  if (combo_index < lookup.size()) {
    return lookup[combo_index];
  }
  return static_cast<std::uint16_t>(combo_index);
}

[[nodiscard]] std::uint8_t TextureCountForSkinTextureUnitMode(const std::uint16_t mode) noexcept {
  if (mode == kM2TextureUnitModeNoTexture) {
    return 0u;
  }
  if (mode == kM2TextureUnitModeDualLayer) {
    return 2u;
  }
  if (mode == kM2TextureUnitModeSingleLayer) {
    return 1u;
  }
  return 0u;
}

void RecordInvalidTextureUnitMode(M2TextureUnitPreparationResult &result,
                                  const std::size_t texture_unit_index) noexcept {
  if (result.invalid_texture_unit_mode_count == 0u) {
    result.first_invalid_texture_unit_mode_unit = texture_unit_index;
  }
  ++result.invalid_texture_unit_mode_count;
}

void RecordMissingShaderCombo(M2TextureUnitPreparationResult &result,
                              const std::size_t texture_unit_index) noexcept {
  if (result.missing_shader_combo_count == 0u) {
    result.first_missing_shader_combo_unit = texture_unit_index;
  }
  ++result.missing_shader_combo_count;
}

void RecordMissingTexUnit(M2TextureUnitPreparationResult &result,
                          const std::size_t texture_unit_index) noexcept {
  if (result.missing_tex_unit_count == 0u) {
    result.first_missing_tex_unit = texture_unit_index;
  }
  ++result.missing_tex_unit_count;
}

void RecordMissingRenderFlags(M2TextureUnitPreparationResult &result,
                              const std::size_t texture_unit_index) noexcept {
  if (result.missing_render_flags_count == 0u) {
    result.first_missing_render_flags_unit = texture_unit_index;
  }
  ++result.missing_render_flags_count;
}

[[nodiscard]] M2TextureUnitPreparationResult ValidateSkinTextureUnitPreparationInputs(
    const data::model::M2Model &model,
    const data::model::M2Skin &skin) noexcept {
  M2TextureUnitPreparationResult result{};
  const bool uses_shader_combos =
      (model.header.global_flags & kM2GlobalFlagHasTextureCombinerCombos) != 0u;

  for (std::size_t texture_unit_index = 0; texture_unit_index < skin.texture_units.size();
       ++texture_unit_index) {
    const auto &texture_unit = skin.texture_units[texture_unit_index];
    if (!IsSupportedM2SkinTextureUnitMode(texture_unit.mode)) {
      RecordInvalidTextureUnitMode(result, texture_unit_index);
      continue;
    }

    if ((texture_unit.shading & kM2SkinShaderHighBit) != 0u) {
      continue;
    }

    const std::uint8_t texture_count = TextureCountForSkinTextureUnitMode(texture_unit.mode);
    if (texture_count == 0u) {
      continue;
    }

    if (static_cast<std::size_t>(texture_unit.render_flags_index) >= model.render_flags.size()) {
      RecordMissingRenderFlags(result, texture_unit_index);
    }

    if (uses_shader_combos) {
      if (static_cast<std::size_t>(texture_unit.shading) + texture_count >
          model.shader_combos.size()) {
        RecordMissingShaderCombo(result, texture_unit_index);
      }
      if (static_cast<std::size_t>(texture_unit.tex_unit_lookup) + texture_count >
          model.tex_units.size()) {
        RecordMissingTexUnit(result, texture_unit_index);
      }
    } else if (static_cast<std::size_t>(texture_unit.tex_unit_lookup) >=
               model.tex_units.size()) {
      RecordMissingTexUnit(result, texture_unit_index);
    }
  }

  return result;
}

[[nodiscard]] bool IsSupportedDiffuseEnvCombinerPair(const std::uint8_t texture_count,
                                                     const std::uint8_t op1,
                                                     const std::uint8_t op2) {
  if (texture_count <= 1u) {
    return true;
  }

  switch (op1) {
  case 0u:
  case 1u:
    return op2 <= 7u;
  case 3u:
    return op2 == 1u;
  case 4u:
    return op2 == 1u || op2 == 4u;
  default:
    return false;
  }
}

[[nodiscard]] std::uint16_t ResolveRawSkinTextureUnitShaderId(
    const data::model::M2Model &model,
    const data::model::SkinTextureUnit &texture_unit) {
  const auto raw_shader_id = texture_unit.shading;
  const std::uint8_t texture_count = TextureCountForSkinTextureUnitMode(texture_unit.mode);
  const auto resolve_tex_unit = [&](const std::size_t combo_index) {
    return ResolveLookupOrIdentity(model.tex_units, combo_index);
  };

  if (texture_count != 0u &&
      static_cast<std::size_t>(texture_unit.render_flags_index) >= model.render_flags.size()) {
    return raw_shader_id;
  }
  const std::uint16_t blend_mode =
      texture_count != 0u ? model.render_flags[texture_unit.render_flags_index].blend_mode
                          : kM2BlendOpaque;

  std::uint16_t shader_id = 0u;
  if ((model.header.global_flags & kM2GlobalFlagHasTextureCombinerCombos) == 0u) {
    if (texture_count == 0u) {
      return 0u;
    }

    std::uint16_t op = blend_mode != kM2BlendOpaque ? kM2CombinerOpMod : kM2CombinerOpOpaque;
    const auto tex_unit = resolve_tex_unit(texture_unit.tex_unit_lookup);
    if (tex_unit > kM2TextureUnitEnvThreshold) {
      op = static_cast<std::uint16_t>(op | kM2SkinShaderEnvBit);
    }
    shader_id = static_cast<std::uint16_t>(op << 4);
    if (tex_unit == 1u) {
      shader_id = static_cast<std::uint16_t>(shader_id | kM2SkinShaderTexCoord1Bit);
    }
  } else {
    for (std::uint8_t layer = 0; layer < texture_count; ++layer) {
      std::uint16_t op = ResolveLookupOrIdentity(model.shader_combos,
                                                 static_cast<std::size_t>(raw_shader_id) + layer);
      if (layer == 0u && blend_mode == kM2BlendOpaque) {
        op = kM2CombinerOpOpaque;
      }

      const auto tex_unit =
          resolve_tex_unit(static_cast<std::size_t>(texture_unit.tex_unit_lookup) + layer);
      if (tex_unit > kM2TextureUnitEnvThreshold) {
        op = static_cast<std::uint16_t>(op | kM2SkinShaderEnvBit);
      }

      if (layer == 0u) {
        shader_id = static_cast<std::uint16_t>(shader_id | ((op & kM2SkinShaderLayerMask) << 4));
      } else {
        shader_id = static_cast<std::uint16_t>(shader_id | (op & kM2SkinShaderLayerMask));
      }

      if (tex_unit == 1u && layer + 1u == texture_count) {
        shader_id = static_cast<std::uint16_t>(shader_id | kM2SkinShaderTexCoord1Bit);
      }
    }
  }

  return shader_id;
}

void ResolveRetailSkinTextureUnitShaders(const data::model::M2Model &model,
                                         data::model::M2Skin &skin) {
  for (auto &texture_unit : skin.texture_units) {
    if (!IsSupportedM2SkinTextureUnitMode(texture_unit.mode)) {
      continue;
    }
    if ((texture_unit.shading & kM2SkinShaderHighBit) == 0u) {
      texture_unit.shading = ResolveRawSkinTextureUnitShaderId(model, texture_unit);
    }
  }
}

[[nodiscard]] std::uint16_t EncodeUvAnimationIndex(const std::uint16_t value) noexcept {
  return value == 0xFFFFu ? 0u : static_cast<std::uint16_t>(value + 1u);
}

[[nodiscard]] std::int16_t DecodeUvAnimationIndex(const std::uint16_t value) noexcept {
  return value == 0u ? static_cast<std::int16_t>(-1)
                     : static_cast<std::int16_t>(value - 1u);
}

void ExpandSkinTextureLookupIndices(const data::model::M2Model &model,
                                    data::model::M2Skin &skin) {
  for (auto &unit : skin.texture_units) {
    if (!IsSupportedM2SkinTextureUnitMode(unit.mode)) {
      continue;
    }

    if (unit.mode == kM2TextureUnitModeDualLayer) {
      const auto texture0 = static_cast<std::uint8_t>(
          ResolveLookupOrIdentity(model.tex_lookup, unit.texture_index));
      const auto texture1 = static_cast<std::uint8_t>(ResolveLookupOrIdentity(
          model.tex_lookup, static_cast<std::size_t>(unit.texture_index) + 1u));
      unit.texture_index = static_cast<std::uint16_t>(
          static_cast<std::uint16_t>(texture0) |
          (static_cast<std::uint16_t>(texture1) << 8u));

      const auto uv0 = static_cast<std::uint8_t>(EncodeUvAnimationIndex(
          ResolveLookupOrIdentity(model.uv_anim_lookup, unit.uv_anim_index)));
      const auto uv1 = static_cast<std::uint8_t>(EncodeUvAnimationIndex(
          ResolveLookupOrIdentity(model.uv_anim_lookup,
                                  static_cast<std::size_t>(unit.uv_anim_index) + 1u)));
      unit.uv_anim_index = static_cast<std::uint16_t>(
          static_cast<std::uint16_t>(uv0) |
          (static_cast<std::uint16_t>(uv1) << 8u));
      continue;
    }

    unit.texture_index = ResolveLookupOrIdentity(model.tex_lookup, unit.texture_index);
    unit.uv_anim_index = EncodeUvAnimationIndex(
        ResolveLookupOrIdentity(model.uv_anim_lookup, unit.uv_anim_index));
  }
}

struct RetailSkinTextureUnitRewriteResult {
  bool created_packed_combo{false};
  bool saw_duplicate_material{false};
};

[[nodiscard]] RetailSkinTextureUnitRewriteResult ApplyRetailSkinTextureUnitShaderRewrite(
    const data::model::M2Model &model,
    data::model::M2Skin &skin) {
  std::optional<std::size_t> leading_unit_index;
  std::uint8_t mod2x_alpha_state = 0u;
  std::uint8_t add_alpha_state = 0u;
  RetailSkinTextureUnitRewriteResult result;
  std::uint32_t previous_material = std::numeric_limits<std::uint32_t>::max();

  const auto render_flags_for =
      [&model](const data::model::SkinTextureUnit &unit) -> const data::model::M2RenderFlags * {
    if (static_cast<std::size_t>(unit.render_flags_index) < model.render_flags.size()) {
      return &model.render_flags[unit.render_flags_index];
    }
    return nullptr;
  };
  const auto texture_coord_unit = [&model](const std::size_t index) -> std::uint16_t {
    return ResolveLookupOrIdentity(model.tex_units, index);
  };
  const auto transparency_lookup = [&model](const std::size_t index) -> std::uint16_t {
    return ResolveLookupOrIdentity(model.transparency_lookup, index);
  };
  const auto same_transparency_as_lead = [&](const data::model::SkinTextureUnit &unit) -> bool {
    return leading_unit_index.has_value() &&
           transparency_lookup(skin.texture_units[*leading_unit_index].transparency_index) ==
               transparency_lookup(unit.transparency_index);
  };
  const auto same_unlit_flag_as_lead =
      [&](const data::model::M2RenderFlags &render_flags) -> bool {
    if (!leading_unit_index.has_value()) {
      return false;
    }
    const auto *leading_flags = render_flags_for(skin.texture_units[*leading_unit_index]);
    return leading_flags != nullptr &&
           ((leading_flags->flags ^ render_flags.flags) & kM2MaterialFlagUnlit) == 0u;
  };

  for (std::size_t index = 0; index < skin.texture_units.size(); ++index) {
    auto &unit = skin.texture_units[index];
    if (!IsSupportedM2SkinTextureUnitMode(unit.mode)) {
      mod2x_alpha_state = 0u;
      add_alpha_state = 0u;
      continue;
    }
    if (unit.render_flags_index == previous_material) {
      result.saw_duplicate_material = true;
      continue;
    }
    previous_material = unit.render_flags_index;

    const auto *render_flags = render_flags_for(unit);
    if (render_flags == nullptr) {
      mod2x_alpha_state = 0u;
      add_alpha_state = 0u;
      continue;
    }

    const std::uint16_t blend_mode = render_flags->blend_mode;
    const std::uint16_t low_shader_op = unit.shading & kM2SkinShaderOpMask;

    if (unit.tex_unit_number == 0u) {
      leading_unit_index = index;
      mod2x_alpha_state = 0u;
      add_alpha_state = 0u;
      if (unit.mode != kM2TextureUnitModeNoTexture && blend_mode == kM2BlendOpaque) {
        unit.shading = static_cast<std::uint16_t>(unit.shading & kM2SkinShaderClearPrimaryOpMask);
      }
    }

    if (mod2x_alpha_state == 0u) {
      if (blend_mode == kM2BlendOpaque && unit.mode == kM2TextureUnitModeDualLayer &&
          (low_shader_op == kM2CombinerOpMod2x ||
           low_shader_op == kM2CombinerOpMod2xNA)) {
        const auto primary_tex_coord = texture_coord_unit(unit.tex_unit_lookup);
        const auto secondary_tex_coord = texture_coord_unit(
            static_cast<std::size_t>(unit.tex_unit_lookup) + 1u);
        if (primary_tex_coord != 0u || secondary_tex_coord > kM2TextureUnitEnvThreshold) {
          mod2x_alpha_state = 1u;
        }
      }
    } else if (mod2x_alpha_state == 1u) {
      if ((blend_mode == kM2BlendAlphaKey || blend_mode == kM2BlendAlpha) &&
          unit.mode == kM2TextureUnitModeSingleLayer && same_unlit_flag_as_lead(*render_flags) &&
          leading_unit_index.has_value()) {
        auto &lead = skin.texture_units[*leading_unit_index];
        if (unit.texture_index == static_cast<std::uint16_t>(lead.texture_index & 0x00FFu) &&
            same_transparency_as_lead(unit)) {
          unit.shading = kM2SkinShaderConsumedTextureUnit;
          lead.shading = kM2SkinShaderOpaqueMod2xNAAlpha;
          mod2x_alpha_state = 3u;
        } else {
          mod2x_alpha_state = 0u;
        }
      } else {
        mod2x_alpha_state = 0u;
      }
    }

    if (add_alpha_state == 1u) {
      if ((blend_mode == kM2BlendAdd || blend_mode == kM2BlendMod2x) &&
          unit.mode == kM2TextureUnitModeSingleLayer &&
          texture_coord_unit(unit.tex_unit_lookup) > kM2TextureUnitEnvThreshold &&
          same_transparency_as_lead(unit) && leading_unit_index.has_value()) {
        auto &lead = skin.texture_units[*leading_unit_index];
        unit.shading = kM2SkinShaderConsumedTextureUnit;
        lead.shading = blend_mode == kM2BlendAdd ? kM2SkinShaderOpaqueAddAlpha
                                                 : kM2SkinShaderOpaqueMod2xNA;
        lead.mode = kM2TextureUnitModeDualLayer;
        lead.texture_index =
            static_cast<std::uint16_t>((lead.texture_index & 0x00FFu) |
                                       ((unit.texture_index & 0x00FFu) << 8));
        lead.uv_anim_index =
            static_cast<std::uint16_t>((lead.uv_anim_index & 0x00FFu) |
                                       ((unit.uv_anim_index & 0x00FFu) << 8));
        result.created_packed_combo = true;
        add_alpha_state = 2u;
      } else {
        add_alpha_state = 0u;
      }
    } else if (add_alpha_state == 2u) {
      if ((blend_mode == kM2BlendAlphaKey || blend_mode == kM2BlendAlpha) &&
          unit.mode == kM2TextureUnitModeSingleLayer && same_unlit_flag_as_lead(*render_flags) &&
          leading_unit_index.has_value()) {
        auto &lead = skin.texture_units[*leading_unit_index];
        if (unit.texture_index == static_cast<std::uint16_t>(lead.texture_index & 0x00FFu) &&
            same_transparency_as_lead(unit)) {
          unit.shading = kM2SkinShaderConsumedTextureUnit;
          lead.shading = lead.shading == kM2SkinShaderOpaqueAddAlpha
                             ? kM2SkinShaderOpaqueAddAlphaAlpha
                             : kM2SkinShaderOpaqueMod2xNAAlpha;
          add_alpha_state = 3u;
        } else {
          add_alpha_state = 0u;
        }
      } else {
        add_alpha_state = 0u;
      }
    }

    if (add_alpha_state == 0u && blend_mode == kM2BlendOpaque &&
        unit.mode == kM2TextureUnitModeSingleLayer &&
        texture_coord_unit(unit.tex_unit_lookup) == 0u) {
      leading_unit_index = index;
      add_alpha_state = 1u;
    }
  }

  return result;
}

template <typename T>
void AppendUnique(std::vector<T> &values, const T value) {
  if (std::find(values.begin(), values.end(), value) == values.end()) {
    values.push_back(value);
  }
}

template <typename T>
void InsertRetailLookupPair(std::vector<T> &values, const T first, const T second) {

  for (std::size_t index = 0; index + 1u < values.size(); ++index) {
    if (values[index] == first && values[index + 1u] == second) {
      return;
    }
  }

  std::size_t insertion_index = values.size();
  for (std::size_t index = 0; index + 1u < values.size(); index += 2u) {
    if (values[index] > first ||
        (values[index] == first && values[index + 1u] > second)) {
      insertion_index = index;
      break;
    }
  }
  values.insert(values.begin() + static_cast<std::ptrdiff_t>(insertion_index),
                {first, second});
}

void RebuildRetailSkinLookupTables(data::model::M2Model &model,
                                   const data::model::M2Skin &skin) {
  std::vector<std::uint16_t> textures;
  std::vector<std::uint16_t> uv_animations;
  textures.reserve(skin.texture_units.size() * 2u);
  uv_animations.reserve(skin.texture_units.size() * 2u);

  std::uint32_t previous_material = std::numeric_limits<std::uint32_t>::max();
  for (const auto &unit : skin.texture_units) {
    if (!IsSupportedM2SkinTextureUnitMode(unit.mode) ||
        unit.render_flags_index == previous_material) {
      continue;
    }
    previous_material = unit.render_flags_index;

    if (unit.mode == kM2TextureUnitModeDualLayer) {
      InsertRetailLookupPair(
          textures,
          static_cast<std::uint16_t>(unit.texture_index & 0x00FFu),
          static_cast<std::uint16_t>((unit.texture_index >> 8u) & 0x00FFu));
      InsertRetailLookupPair(
          uv_animations,
          static_cast<std::uint16_t>(DecodeUvAnimationIndex(
              static_cast<std::uint16_t>(unit.uv_anim_index & 0x00FFu))),
          static_cast<std::uint16_t>(DecodeUvAnimationIndex(
              static_cast<std::uint16_t>((unit.uv_anim_index >> 8u) & 0x00FFu))));
    }
  }

  previous_material = std::numeric_limits<std::uint32_t>::max();
  for (const auto &unit : skin.texture_units) {
    if (!IsSupportedM2SkinTextureUnitMode(unit.mode) ||
        unit.render_flags_index == previous_material) {
      continue;
    }
    previous_material = unit.render_flags_index;
    if (unit.mode == kM2TextureUnitModeDualLayer) {
      continue;
    }

    AppendUnique(textures, unit.texture_index);
    AppendUnique(uv_animations,
                 static_cast<std::uint16_t>(DecodeUvAnimationIndex(unit.uv_anim_index)));
  }

  model.tex_lookup = std::move(textures);
  model.uv_anim_lookup = std::move(uv_animations);
}

void CopyRetailDuplicateMaterialTextureUnits(data::model::M2Skin &skin) {
  std::uint32_t last_material = std::numeric_limits<std::uint32_t>::max();
  data::model::SkinTextureUnit *previous_unit = nullptr;
  for (auto &unit : skin.texture_units) {
    if (!IsSupportedM2SkinTextureUnitMode(unit.mode)) {
      previous_unit = nullptr;
      last_material = std::numeric_limits<std::uint32_t>::max();
      continue;
    }
    if (previous_unit != nullptr && unit.render_flags_index == last_material) {
      unit.shading = previous_unit->shading;
      unit.mode = previous_unit->mode;
      unit.texture_index = previous_unit->texture_index;
      unit.uv_anim_index = previous_unit->uv_anim_index;
    }
    last_material = unit.render_flags_index;
    previous_unit = &unit;
  }
}

void RecordTextureComboZeroFixup(M2TextureUnitPreparationResult &result,
                                 const std::size_t texture_unit_index) noexcept {
  if (result.texture_combo_zero_fixup_count == 0u) {
    result.first_texture_combo_zero_fixup_unit = texture_unit_index;
  }
  ++result.texture_combo_zero_fixup_count;
}

void RecordUvComboZeroFixup(M2TextureUnitPreparationResult &result,
                            const std::size_t texture_unit_index) noexcept {
  if (result.uv_combo_zero_fixup_count == 0u) {
    result.first_uv_combo_zero_fixup_unit = texture_unit_index;
  }
  ++result.uv_combo_zero_fixup_count;
}

M2TextureUnitPreparationResult NormalizeSkinTextureLookupIndices(
    const data::model::M2Model &model,
    data::model::M2Skin &skin) {
  M2TextureUnitPreparationResult result{};
  std::int32_t previous_material = -1;

  for (std::size_t texture_unit_index = 0; texture_unit_index < skin.texture_units.size();
       ++texture_unit_index) {
    auto &texture_unit = skin.texture_units[texture_unit_index];
    if (!IsSupportedM2SkinTextureUnitMode(texture_unit.mode)) {
      previous_material = -1;
      continue;
    }
    if (texture_unit.render_flags_index == static_cast<std::uint16_t>(previous_material)) {
      continue;
    }
    previous_material = texture_unit.render_flags_index;

    if (texture_unit.mode == kM2TextureUnitModeDualLayer) {
      const auto tex_lo = static_cast<std::int16_t>(texture_unit.texture_index & 0xFFu);
      const auto tex_hi = static_cast<std::int16_t>((texture_unit.texture_index >> 8) & 0xFFu);

      std::uint16_t texture_lookup_index = 0u;
      bool found_texture_pair = false;
      if (model.tex_lookup.size() > 1u) {
        for (std::size_t i = 0; i + 1 < model.tex_lookup.size(); ++i) {
          if (static_cast<std::int16_t>(model.tex_lookup[i]) == tex_lo &&
              static_cast<std::int16_t>(model.tex_lookup[i + 1]) == tex_hi) {
            texture_lookup_index = static_cast<std::uint16_t>(i);
            found_texture_pair = true;
            break;
          }
        }
      }
      if (!found_texture_pair) {
        RecordTextureComboZeroFixup(result, texture_unit_index);
      }
      texture_unit.texture_index = texture_lookup_index;

      const auto raw_uv = texture_unit.uv_anim_index;
      const auto uv_lo = static_cast<std::int16_t>(
          (raw_uv & 0xFFu) != 0u ? static_cast<std::int16_t>((raw_uv & 0xFFu) - 1)
                                 : static_cast<std::int16_t>(-1));
      const auto uv_hi = static_cast<std::int16_t>(
          ((raw_uv >> 8) & 0xFFu) != 0u ? static_cast<std::int16_t>(((raw_uv >> 8) & 0xFFu) - 1)
                                        : static_cast<std::int16_t>(-1));

      std::uint16_t uv_lookup_index = 0u;
      bool found_uv_pair = false;
      if (model.uv_anim_lookup.size() > 1u) {
        for (std::size_t i = 0; i + 1 < model.uv_anim_lookup.size(); ++i) {
          if (static_cast<std::int16_t>(model.uv_anim_lookup[i]) == uv_lo &&
              static_cast<std::int16_t>(model.uv_anim_lookup[i + 1]) == uv_hi) {
            uv_lookup_index = static_cast<std::uint16_t>(i);
            found_uv_pair = true;
            break;
          }
        }
      }
      if (!found_uv_pair) {
        RecordUvComboZeroFixup(result, texture_unit_index);
      }
      texture_unit.uv_anim_index = uv_lookup_index;
    } else {
      if (!model.tex_lookup.empty()) {
        const auto target = texture_unit.texture_index;
        std::uint16_t pos = static_cast<std::uint16_t>(model.tex_lookup.size());
        for (std::size_t i = 0; i < model.tex_lookup.size(); ++i) {
          if (model.tex_lookup[i] == target) {
            pos = static_cast<std::uint16_t>(i);
            break;
          }
        }
        texture_unit.texture_index = pos;
      }

      if (!model.uv_anim_lookup.empty()) {
        const auto raw = texture_unit.uv_anim_index;
        const auto search_val = static_cast<std::int16_t>(
            raw != 0u ? static_cast<std::int16_t>(raw - 1) : static_cast<std::int16_t>(-1));

        std::uint16_t pos = static_cast<std::uint16_t>(model.uv_anim_lookup.size());
        for (std::size_t i = 0; i < model.uv_anim_lookup.size(); ++i) {
          if (static_cast<std::int16_t>(model.uv_anim_lookup[i]) == search_val) {
            pos = static_cast<std::uint16_t>(i);
            break;
          }
        }
        texture_unit.uv_anim_index = pos;
      }
    }
  }

  return result;
}

}

M2ResolvedSkinTextureUnitCombos ResolveM2SkinTextureUnitCombos(
    const data::model::M2Model &model,
    const data::model::SkinTextureUnit &texture_unit) {
  const auto resolve_uv_lookup_or_identity =
      [&model](const std::size_t combo_index) -> std::optional<std::uint16_t> {
    const auto resolved = ResolveLookupOrIdentity(model.uv_anim_lookup, combo_index);
    if (resolved == 0xFFFFu) {
      return std::nullopt;
    }
    return resolved;
  };

  const auto resolve_packed_dual_texture_index =
      [&model](const std::size_t combo_index) -> std::uint16_t {
    return static_cast<std::uint8_t>(ResolveLookupOrIdentity(model.tex_lookup, combo_index));
  };

  const auto resolve_packed_dual_uv_index =
      [&resolve_uv_lookup_or_identity](const std::size_t combo_index)
      -> std::optional<std::uint16_t> {
    const auto resolved = resolve_uv_lookup_or_identity(combo_index);
    if (!resolved.has_value()) {
      return std::nullopt;
    }

    const auto encoded = static_cast<std::uint8_t>(*resolved + 1u);
    if (encoded == 0u) {
      return std::nullopt;
    }
    return static_cast<std::uint16_t>(encoded - 1u);
  };

  M2ResolvedSkinTextureUnitCombos out{};
  if (!IsSupportedM2SkinTextureUnitMode(texture_unit.mode)) {
    return out;
  }
  if (texture_unit.mode == kM2TextureUnitModeNoTexture) {
    return out;
  }

  if (texture_unit.mode == kM2TextureUnitModeDualLayer) {
    out.primary_texture_index = resolve_packed_dual_texture_index(texture_unit.texture_index);
    out.secondary_texture_index = resolve_packed_dual_texture_index(
        static_cast<std::size_t>(texture_unit.texture_index) + 1u);
    out.primary_uv_animation_index = resolve_packed_dual_uv_index(texture_unit.uv_anim_index);
    out.secondary_uv_animation_index =
        resolve_packed_dual_uv_index(static_cast<std::size_t>(texture_unit.uv_anim_index) + 1u);
    return out;
  }

  out.primary_texture_index = ResolveLookupOrIdentity(model.tex_lookup, texture_unit.texture_index);
  out.primary_uv_animation_index = resolve_uv_lookup_or_identity(texture_unit.uv_anim_index);
  return out;
}

M2TextureUnitPreparationResult PrepareM2SkinTextureUnitsForRender(
    data::model::M2Model &model,
    data::model::M2Skin &skin) {
  M2TextureUnitPreparationResult result = ValidateSkinTextureUnitPreparationInputs(model, skin);
  ResolveRetailSkinTextureUnitShaders(model, skin);
  ExpandSkinTextureLookupIndices(model, skin);
  const RetailSkinTextureUnitRewriteResult rewrite_result =
      ApplyRetailSkinTextureUnitShaderRewrite(model, skin);
  if (rewrite_result.created_packed_combo) {
    RebuildRetailSkinLookupTables(model, skin);
  }
  const M2TextureUnitPreparationResult lookup_result =
      NormalizeSkinTextureLookupIndices(model, skin);
  if (rewrite_result.saw_duplicate_material) {
    CopyRetailDuplicateMaterialTextureUnits(skin);
  }
  result.texture_combo_zero_fixup_count = lookup_result.texture_combo_zero_fixup_count;
  result.uv_combo_zero_fixup_count = lookup_result.uv_combo_zero_fixup_count;
  result.first_texture_combo_zero_fixup_unit = lookup_result.first_texture_combo_zero_fixup_unit;
  result.first_uv_combo_zero_fixup_unit = lookup_result.first_uv_combo_zero_fixup_unit;
  return result;
}

M2ResolvedSkinTextureUnitShader ResolveM2SkinTextureUnitShader(
    const data::model::M2Model &model,
    const data::model::SkinTextureUnit &texture_unit) {
  M2ResolvedSkinTextureUnitShader out{};
  if (!IsSupportedM2SkinTextureUnitMode(texture_unit.mode)) {
    out.valid = false;
    out.texture_count = 0u;
    return out;
  }
  out.texture_count = TextureCountForSkinTextureUnitMode(texture_unit.mode);

  const auto shader_id = texture_unit.shading;
  if ((shader_id & kM2SkinShaderHighBit) != 0u) {
    out.shader_id = shader_id;
    out.texture_count = 2u;
    out.layer0_source = M2ResolvedSkinTextureSource::kTexCoord0;
    out.layer1_source = M2ResolvedSkinTextureSource::kEnvSphere;

    switch (shader_id & kM2SkinShaderIdMask) {
    case 0u:
      out.texture_count = 0u;
      out.shader_kind = M2ResolvedSkinTextureShaderKind::kConsumedTextureUnit;
      out.draws = false;
      return out;
    case 1u:
      out.op1 = kM2CombinerOpOpaque;
      out.op2 = kM2CombinerOpMod2xNA;
      out.shader_kind = M2ResolvedSkinTextureShaderKind::kDiffuseT1EnvOpaqueMod2xNAAlpha;
      return out;
    case 2u:
      out.op1 = kM2CombinerOpOpaque;
      out.op2 = kM2CombinerOpAdd;
      out.shader_kind = M2ResolvedSkinTextureShaderKind::kDiffuseT1EnvOpaqueAddAlpha;
      return out;
    case 3u:
      out.op1 = kM2CombinerOpOpaque;
      out.op2 = kM2CombinerOpAdd;
      out.shader_kind = M2ResolvedSkinTextureShaderKind::kDiffuseT1EnvOpaqueAddAlphaAlpha;
      return out;
    default:
      out.valid = false;
      return out;
    }
  }

  out.shader_id = shader_id;
  out.op1 = static_cast<std::uint8_t>((shader_id >> 4) & kM2SkinShaderOpMask);
  out.op2 = static_cast<std::uint8_t>(shader_id & kM2SkinShaderOpMask);
  if (!IsSupportedDiffuseEnvCombinerPair(out.texture_count, out.op1, out.op2)) {
    out.valid = false;
    return out;
  }
  if (out.texture_count == 0u) {
    return out;
  }
  if (static_cast<std::size_t>(texture_unit.render_flags_index) >= model.render_flags.size()) {
    out.valid = false;
    return out;
  }

  const bool env0 = ((out.shader_id >> 4) & kM2SkinShaderEnvBit) != 0u;
  const bool env1 = (out.shader_id & kM2SkinShaderEnvBit) != 0u;
  if (out.texture_count <= 1u) {
    const auto texture_coord = ResolveLookupOrIdentity(model.tex_units, texture_unit.tex_unit_lookup);
    out.layer0_source =
        env0 ? M2ResolvedSkinTextureSource::kEnvSphere
             : (((out.shader_id & kM2SkinShaderTexCoord1Bit) != 0u || texture_coord != 0u)
                    ? M2ResolvedSkinTextureSource::kTexCoord1
                    : M2ResolvedSkinTextureSource::kTexCoord0);
    out.layer1_source = M2ResolvedSkinTextureSource::kTexCoord1;
    return out;
  }

  out.layer0_source =
      env0 ? M2ResolvedSkinTextureSource::kEnvSphere : M2ResolvedSkinTextureSource::kTexCoord0;
  out.layer1_source =
      env1 ? M2ResolvedSkinTextureSource::kEnvSphere : M2ResolvedSkinTextureSource::kTexCoord1;
  return out;
}

}

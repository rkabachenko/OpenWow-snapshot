#pragma once

#include "openwow/render/m2/m2_public_types.h"

#include <cstddef>
#include <string>

namespace openwow::data::model {
struct M2Model;
struct M2Skin;
struct SkinTextureUnit;
}

namespace openwow::render::m2 {

namespace detail {
struct M2ModelResource;
}

struct M2RenderPreparationResult {
  M2ResultStatus status = M2ResultStatus::kReady;
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
};

struct M2RenderValidationIssue {
  M2ResultReason reason = M2ResultReason::kNone;
  std::string detail;
};

[[nodiscard]] bool ValidateM2TextureUnitForRender(
    const data::model::M2Model& model, const data::model::M2Skin& skin,
    const data::model::SkinTextureUnit& texture_unit,
    std::size_t texture_unit_index, M2RenderValidationIssue* issue);
[[nodiscard]] bool ValidateM2EffectRenderInputs(
    const data::model::M2Model& model, M2RenderValidationIssue* issue);
[[nodiscard]] M2RenderPreparationResult PrepareM2RenderPackage(
    const data::model::M2Model& model, const data::model::M2Skin& skin,
    detail::M2ModelResource& resource);

}

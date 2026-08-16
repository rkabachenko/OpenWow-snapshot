#pragma once

#include "openwow/data/model/m2_model.h"
#include "openwow/render/m2/m2_public_types.h"

#include <cstdint>
#include <functional>
#include <optional>

namespace openwow::render::m2 {

struct M2AnimationAliasInfo {
  std::uint32_t flags = 0;
  std::uint32_t target_animation_id = 0;
};

using M2AnimationAliasLookup =
    std::function<std::optional<M2AnimationAliasInfo>(std::uint32_t)>;

struct M2WeightedVariantSelection {
  std::uint16_t sequence_index = kInvalidM2AnimationSequenceIndex;
  std::int32_t sub_animation_index = -1;
};

[[nodiscard]] bool M2ModelHasAnimationId(
    const data::model::M2Model& model,
    std::uint32_t animation_id,
    const M2AnimationAliasLookup& alias_lookup = {});

[[nodiscard]] std::uint32_t ResolveM2AnimationId(
    const data::model::M2Model& model,
    std::uint32_t animation_id,
    const M2AnimationAliasLookup& alias_lookup = {});

[[nodiscard]] std::uint16_t FindM2AnimationSequenceIndex(
    const data::model::M2Model& model,
    std::uint32_t animation_id,
    std::uint32_t sub_animation_index);

[[nodiscard]] std::uint32_t CountM2AnimationVariants(
    const data::model::M2Model& model,
    std::uint32_t animation_id);

[[nodiscard]] M2WeightedVariantSelection SelectWeightedM2AnimationVariant(
    const data::model::M2Model& model,
    std::uint16_t head_sequence_index,
    std::uint32_t random_value);

[[nodiscard]] M2AnimationSelectionResult ResolveM2AnimationSelection(
    const data::model::M2Model& model,
    const M2AnimationRequest& request,
    std::uint32_t random_value,
    const M2AnimationAliasLookup& alias_lookup = {});

}

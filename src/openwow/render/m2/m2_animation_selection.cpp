#include "openwow/render/m2/m2_animation_selection.h"

#include <array>
#include <vector>

namespace openwow::render::m2 {

namespace {

[[nodiscard]] bool IsValidSequenceIndex(
    const data::model::M2Model& model,
    const std::uint16_t sequence_index) {
  return sequence_index != kInvalidM2AnimationSequenceIndex
         && sequence_index < model.animation_sequences.size();
}

[[nodiscard]] std::uint32_t ToPositiveFrequency(
    const data::model::M2AnimationSequenceRecord& sequence) {
  return sequence.frequency > 0
             ? static_cast<std::uint32_t>(sequence.frequency)
             : 0u;
}

[[nodiscard]] bool IsValidAnimationBehaviorId(
    const std::uint32_t animation_id) {
  return animation_id < kInvalidM2AnimationBehaviorId;
}

[[nodiscard]] std::uint16_t FirstSequenceIndexForAnimationId(
    const data::model::M2Model& model, const std::uint32_t animation_id) {
  static_assert(data::model::kM2NoSequenceForAnimationId ==
                kInvalidM2AnimationSequenceIndex);
  if (animation_id < model.first_sequence_index_by_animation_id.size()) {
    return model.first_sequence_index_by_animation_id[animation_id];
  }
  for (std::size_t index = 0; index < model.animation_sequences.size(); ++index) {
    if (model.animation_sequences[index].animation_id == animation_id) {
      return static_cast<std::uint16_t>(index);
    }
  }
  return kInvalidM2AnimationSequenceIndex;
}

[[nodiscard]] bool M2ModelContainsAnimationId(
    const data::model::M2Model& model,
    const std::uint32_t animation_id) {
  if (!IsValidAnimationBehaviorId(animation_id)) {
    return false;
  }
  return FirstSequenceIndexForAnimationId(model, animation_id) !=
         kInvalidM2AnimationSequenceIndex;
}

}

bool M2ModelHasAnimationId(const data::model::M2Model& model,
                           const std::uint32_t animation_id,
                           const M2AnimationAliasLookup& alias_lookup) {
  const auto resolved_animation_id =
      ResolveM2AnimationId(model, animation_id, alias_lookup);
  return M2ModelContainsAnimationId(model, resolved_animation_id);
}

std::uint32_t ResolveM2AnimationId(const data::model::M2Model& model,
                                   const std::uint32_t animation_id,
                                   const M2AnimationAliasLookup& alias_lookup) {
  std::uint32_t resolved_animation_id = animation_id;

  if (!IsValidAnimationBehaviorId(resolved_animation_id)
      || M2ModelContainsAnimationId(model, resolved_animation_id)
      || !alias_lookup) {
    return resolved_animation_id;
  }

  std::array<std::uint32_t, kMaxM2AnimationAliasDepth> visited{};
  std::size_t visited_count = 0;

  while (visited_count < visited.size()) {
    if (M2ModelContainsAnimationId(model, resolved_animation_id)) {
      break;
    }

    bool seen = false;
    for (std::size_t index = 0; index < visited_count; ++index) {
      if (visited[index] == resolved_animation_id) {
        seen = true;
        break;
      }
    }
    if (seen) {
      break;
    }

    visited[visited_count++] = resolved_animation_id;
    const auto alias_info = alias_lookup(resolved_animation_id);
    if (!alias_info.has_value()
        || alias_info->target_animation_id == 0
        || alias_info->target_animation_id == resolved_animation_id) {
      break;
    }

    resolved_animation_id = alias_info->target_animation_id;
  }

  return resolved_animation_id;
}

std::uint16_t FindM2AnimationSequenceIndex(
    const data::model::M2Model& model,
    const std::uint32_t animation_id,
    const std::uint32_t sub_animation_index) {
  std::uint16_t sequence_index =
      FirstSequenceIndexForAnimationId(model, animation_id);

  if (!IsValidSequenceIndex(model, sequence_index)) {
    return kInvalidM2AnimationSequenceIndex;
  }

  if (sub_animation_index == 0) {
    return sequence_index;
  }

  std::size_t traversed_sub_animation_index = 0;
  std::vector<bool> visited(model.animation_sequences.size(), false);
  while (traversed_sub_animation_index < sub_animation_index) {
    const auto& sequence = model.animation_sequences[sequence_index];
    if (sequence.next_animation < 0
        || static_cast<std::size_t>(sequence.next_animation) >= model.animation_sequences.size()) {
      return kInvalidM2AnimationSequenceIndex;
    }

    const auto next_index =
        static_cast<std::uint16_t>(sequence.next_animation);
    if (visited[next_index]) {
      return kInvalidM2AnimationSequenceIndex;
    }
    visited[next_index] = true;

    sequence_index = next_index;
    ++traversed_sub_animation_index;
  }

  return sequence_index;
}

std::uint32_t CountM2AnimationVariants(
    const data::model::M2Model& model,
    const std::uint32_t animation_id) {
  const std::uint16_t head_index =
      FirstSequenceIndexForAnimationId(model, animation_id);

  if (!IsValidSequenceIndex(model, head_index)) {
    return 0;
  }

  std::uint32_t count = 1;
  std::uint16_t current = head_index;
  std::vector<bool> visited(model.animation_sequences.size(), false);
  visited[current] = true;

  while (true) {
    const auto& seq = model.animation_sequences[current];
    if (seq.next_animation < 0
        || static_cast<std::size_t>(seq.next_animation)
               >= model.animation_sequences.size()) {
      break;
    }
    const auto next = static_cast<std::uint16_t>(seq.next_animation);
    if (visited[next]) {
      break;
    }
    visited[next] = true;
    current = next;
    ++count;
  }

  return count;
}

M2WeightedVariantSelection SelectWeightedM2AnimationVariant(
    const data::model::M2Model& model,
    const std::uint16_t head_sequence_index,
    std::uint32_t random_value) {
  M2WeightedVariantSelection selection{
      .sequence_index = head_sequence_index,
      .sub_animation_index = 0,
  };
  if (!IsValidSequenceIndex(model, head_sequence_index)) {
    selection.sequence_index = kInvalidM2AnimationSequenceIndex;
    selection.sub_animation_index = -1;
    return selection;
  }

  std::uint16_t current_sequence_index = head_sequence_index;
  std::int32_t current_sub_animation_index = 0;
  std::vector<bool> visited(model.animation_sequences.size(), false);

  while (IsValidSequenceIndex(model, current_sequence_index)) {
    if (visited[current_sequence_index]) {
      break;
    }
    visited[current_sequence_index] = true;

    const auto& sequence = model.animation_sequences[current_sequence_index];
    const std::uint32_t frequency = ToPositiveFrequency(sequence);
    if (random_value < frequency) {
      selection.sequence_index = current_sequence_index;
      selection.sub_animation_index = current_sub_animation_index;
      return selection;
    }

    random_value -= frequency;
    ++current_sub_animation_index;

    if (sequence.next_animation < 0
        || static_cast<std::size_t>(sequence.next_animation) >= model.animation_sequences.size()) {
      break;
    }
    current_sequence_index = static_cast<std::uint16_t>(sequence.next_animation);
  }

  return selection;
}

M2AnimationSelectionResult ResolveM2AnimationSelection(
    const data::model::M2Model& model,
    const M2AnimationRequest& request,
    const std::uint32_t random_value,
    const M2AnimationAliasLookup& alias_lookup) {
  M2AnimationSelectionResult result{
      .requested_animation_id = request.animation_id,
      .resolved_animation_id = request.animation_id,
  };

  if (request.animation_lookup_id < 0) {
    result.resolved_lookup_sequence_index = 0;
  } else {
    const auto lookup_index =
        static_cast<std::size_t>(request.animation_lookup_id);
    if (lookup_index >= model.animation_lookup.size()) {
      return result;
    }

    const auto sequence_index = model.animation_lookup[lookup_index];
    result.resolved_lookup_sequence_index = sequence_index;
    if (!IsValidSequenceIndex(model, sequence_index)) {
      return result;
    }

    const auto& sequence = model.animation_sequences[sequence_index];
    result.resolved = true;
    result.resolved_animation_id = sequence.animation_id;
    result.resolved_sequence_index = sequence_index;
    result.resolved_sub_animation_index = sequence.sub_animation_id;
    return result;
  }

  if (model.animation_sequences.empty()) {
    return result;
  }

  result.resolved_animation_id =
      ResolveM2AnimationId(model, request.animation_id, alias_lookup);
  if (!IsValidAnimationBehaviorId(result.resolved_animation_id)) {
    return result;
  }
  if (!M2ModelContainsAnimationId(model, result.resolved_animation_id)) {

    constexpr std::uint32_t kRetailStandAnimationId = 0x93u;
    if (result.resolved_animation_id != kRetailStandAnimationId &&
        M2ModelContainsAnimationId(model, kRetailStandAnimationId)) {
      result.resolved_animation_id = kRetailStandAnimationId;
    } else if (!model.animation_sequences.empty()) {
      result.resolved_animation_id =
          model.animation_sequences.front().animation_id;
    } else {
      return result;
    }
  }

  const bool requested_random_variant = request.sub_animation_index < 0;
  if (!requested_random_variant) {
    const auto exact_sequence_index = FindM2AnimationSequenceIndex(
        model,
        result.resolved_animation_id,
        static_cast<std::uint32_t>(request.sub_animation_index));
    if (IsValidSequenceIndex(model, exact_sequence_index)) {
      result.resolved = true;
      result.resolved_sequence_index = exact_sequence_index;
      result.resolved_sub_animation_index = request.sub_animation_index;
      return result;
    }
  }

  const auto head_sequence_index =
      FindM2AnimationSequenceIndex(model, result.resolved_animation_id, 0);
  if (!IsValidSequenceIndex(model, head_sequence_index)) {
    return result;
  }

  const auto weighted_variant = SelectWeightedM2AnimationVariant(
      model,
      head_sequence_index,
      random_value);
  if (!IsValidSequenceIndex(model, weighted_variant.sequence_index)) {
    return result;
  }

  result.resolved = true;
  result.resolved_sequence_index = weighted_variant.sequence_index;
  result.resolved_sub_animation_index = weighted_variant.sub_animation_index;
  result.used_random_variant = true;
  return result;
}

}

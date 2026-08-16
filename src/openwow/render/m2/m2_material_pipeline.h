#pragma once

#include "openwow/data/model/m2_model.h"
#include "openwow/render/api/math/render_math_types.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace openwow::render::m2 {

class M2Animator;

enum class M2DrawBatchScope : std::uint8_t {
  kAllTextureUnits = 0,
  kWorldPrimaryTextureUnits = 1,
};

inline constexpr std::uint8_t kM2TextureUnitFlagSecondaryWorldPass = 0x20u;

inline constexpr std::uint8_t kM2TextureUnitFlagProjectedTexture = 0x04u;
inline constexpr std::uint32_t kM2GlobalFlagSpecialTextureUnitSort = 0x80u;
inline constexpr std::uint32_t kM2TextureUnitOrderKeyShift = 1u;
inline constexpr std::uint32_t kM2RenderPassOrderKeyShift = 2u;

enum class M2TransparentSortMode : std::uint8_t {
  kStandard = 0,
  kSpecialPassPriority = 1,
};

[[nodiscard]] M2TransparentSortMode
ResolveM2TransparentSortMode(std::uint32_t model_global_flags) noexcept;

[[nodiscard]] bool
TextureUnitMatchesDrawBatchScope(const data::model::SkinTextureUnit &texture_unit,
                                 M2DrawBatchScope scope) noexcept;

[[nodiscard]] bool M2ModelNeedsMaterialAnimator(const data::model::M2Model &model) noexcept;

struct M2TextureUnitMaterialSample {
  RenderVec4 color{1.0f, 1.0f, 1.0f, 1.0f};
  float transparency_alpha = 1.0f;
  float color_alpha = 1.0f;
};

inline constexpr std::size_t kNoM2MaterialSlot = static_cast<std::size_t>(-1);

struct M2TextureUnitMaterialSlots {
  std::size_t transparency = kNoM2MaterialSlot;
  std::size_t color = kNoM2MaterialSlot;

  [[nodiscard]] friend bool operator==(const M2TextureUnitMaterialSlots &lhs,
                                       const M2TextureUnitMaterialSlots &rhs) noexcept {
    return lhs.transparency == rhs.transparency && lhs.color == rhs.color;
  }
};

[[nodiscard]] M2TextureUnitMaterialSlots ResolveM2TextureUnitMaterialSlots(
    const data::model::M2Model &model,
    const data::model::SkinTextureUnit &texture_unit) noexcept;

[[nodiscard]] M2TextureUnitMaterialSample SampleM2MaterialSlots(
    M2TextureUnitMaterialSlots slots, const M2Animator *animator, int animation_index,
    std::uint32_t animation_time_ms, float model_alpha,
    const std::optional<RenderVec4> &tint_color);

[[nodiscard]] M2TextureUnitMaterialSample SampleM2TextureUnitMaterial(
    const data::model::M2Model &model, const data::model::SkinTextureUnit &texture_unit,
    const M2Animator *animator, int animation_index, std::uint32_t animation_time_ms,
    float model_alpha, const std::optional<RenderVec4> &tint_color = std::nullopt);

class M2SubmissionMaterialCache {
public:

  void BeginSubmission(const data::model::M2Model &model, const M2Animator *animator,
                       int animation_index, std::uint32_t animation_time_ms,
                       float model_alpha, const std::optional<RenderVec4> &tint_color);

  [[nodiscard]] M2TextureUnitMaterialSample Sample(
      const data::model::SkinTextureUnit &texture_unit);

private:
  struct Entry {
    M2TextureUnitMaterialSlots slots;
    M2TextureUnitMaterialSample sample;
  };

  const data::model::M2Model *model_ = nullptr;
  const M2Animator *animator_ = nullptr;
  int animation_index_ = 0;
  std::uint32_t animation_time_ms_ = 0;
  float model_alpha_ = 1.0f;
  std::optional<RenderVec4> tint_color_;

  std::vector<Entry> entries_;
  std::size_t used_ = 0;
};

struct M2TextureUnitSortInput {
  std::size_t texture_unit_index = 0;
  std::uint32_t blend_mode = 0;
  std::int8_t priority_plane = 0;
  std::uint32_t texture_unit_order_key = 0;
  std::uint32_t geoset_id = 0;
  float sort_distance = 0.0f;
  std::uint32_t render_pass_order_key = 0;
  std::int32_t type = 0;
};

struct M2TextureUnitSortKey : M2TextureUnitSortInput {
  std::size_t input_index = 0;
  std::uint32_t pass_group = 0;
  std::uint32_t pass_priority = 0;
};

[[nodiscard]] std::optional<std::uint32_t>
ResolveM2TextureUnitPassGroupForBlendMode(std::uint32_t blend_mode) noexcept;

[[nodiscard]] bool BuildM2TransparentSortKeysInto(
    std::span<const M2TextureUnitSortInput> inputs, M2TransparentSortMode mode,
    std::vector<M2TextureUnitSortKey> *out_keys);

[[nodiscard]] std::optional<std::vector<M2TextureUnitSortKey>>
BuildM2TransparentSortKeys(const std::vector<M2TextureUnitSortInput> &inputs,
                           M2TransparentSortMode mode);

[[nodiscard]] bool CompareM2TransparentSortKeys(const M2TextureUnitSortKey &lhs,
                                                const M2TextureUnitSortKey &rhs) noexcept;

[[nodiscard]] float ComputeM2TextureUnitSortDistance(const data::model::SkinSubmesh &submesh,
                                                     const RenderMatrix4x4 &model_mtx,
                                                     std::optional<RenderMatrix4x4View>
                                                         view_mtx) noexcept;

}

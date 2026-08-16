#pragma once

#include "openwow/foundation/math/row_major_mat4x4.h"
#include "openwow/foundation/math/vec3_normalize_if_length_squared_exceeds_client_epsilon.h"
#include "openwow/render/models/animation/model_light_record.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cmath>

namespace openwow::render {

struct ProjectedDirectionalLightState {
  std::array<float, 3> normalized_light_direction{};
  std::array<float, 3> ambient_rgb{};
  std::array<float, 3> projected_diffuse_rgb{};
  std::array<float, 3> specular_rgb{};
};

struct WorldLightAccumulator {
  static constexpr std::uint32_t kOwnerTransformFinalizedFlag = 0x1u;
  static constexpr std::uint32_t kProjectedDirectionalFinalizedFlag = 0x2u;
  static constexpr std::uint32_t kRetailInitFlag0x20 = 0x20u;
  static constexpr std::uint32_t kProjectedShadowPlanePresentFlag = 0x40u;
  static constexpr std::size_t kMaxNearbyPointLights = 4;
  static constexpr float kProjectedDirectionalFallbackLengthSquared = 0.0000099999997f;

  std::uint32_t owner_handle = 0;
  std::array<float, 4> bounds_sphere{};
  std::uint32_t flags = 0;
  std::array<float, 9> directional_color_basis{};
  std::array<float, 3> weighted_light_vector{};
  std::array<float, 3> accumulated_diffuse_rgb{};
  std::array<float, 3> accumulated_ambient_rgb{};
  std::array<float, 3> projected_diffuse_rgb{};
  std::array<float, 3> accumulated_specular_rgb{};
  std::array<float, 3> normalized_light_direction{};
  std::array<std::uint32_t, kMaxNearbyPointLights> nearby_light_handles{};
  std::array<float, kMaxNearbyPointLights> nearby_light_distance_squared{};
  std::uint32_t nearby_light_count = 0;
  float fog_start = 0.0f;
  float fog_end = 0.0f;
  float fog_inverse_range = 0.0f;
  float fog_blend_scale = 0.0f;
  std::array<float, 3> fog_color_rgb{};
  std::array<float, 3> projected_shadow_plane_normal{};
  float projected_shadow_plane_distance = 0.0f;

  void Initialize(const std::uint32_t owner, const std::array<float, 4> &sphere) noexcept {
    *this = {};
    owner_handle = owner;
    flags |= kRetailInitFlag0x20;
    bounds_sphere = sphere;
  }

  void InitializeAtWorldPosition(const std::array<float, 3> &world_position) noexcept {
    Initialize(0, {world_position[0], world_position[1], world_position[2], 0.0f});
  }

  void ClearAccumulatedLightingState() noexcept {
    directional_color_basis = {};
    weighted_light_vector = {};
    accumulated_diffuse_rgb = {};
    accumulated_ambient_rgb = {};
    projected_diffuse_rgb = {};
    accumulated_specular_rgb = {};
    normalized_light_direction = {};
  }

  void AddAmbientRgb(const std::array<float, 3> &ambient_rgb) noexcept {
    accumulated_ambient_rgb[0] += ambient_rgb[0];
    accumulated_ambient_rgb[1] += ambient_rgb[1];
    accumulated_ambient_rgb[2] += ambient_rgb[2];
  }

  void AddDirectionalRgb(const std::array<float, 3> &diffuse_rgb,
                         const std::array<float, 3> &light_direction,
                         const std::array<float, 9> *owner_direction_transform = nullptr) noexcept {
    std::array<float, 3> transformed_direction = light_direction;
    if (owner_direction_transform != nullptr) {
      transformed_direction[0] = (*owner_direction_transform)[0] * light_direction[0] +
                                 (*owner_direction_transform)[3] * light_direction[1] +
                                 (*owner_direction_transform)[6] * light_direction[2];
      transformed_direction[1] = (*owner_direction_transform)[1] * light_direction[0] +
                                 (*owner_direction_transform)[4] * light_direction[1] +
                                 (*owner_direction_transform)[7] * light_direction[2];
      transformed_direction[2] = (*owner_direction_transform)[2] * light_direction[0] +
                                 (*owner_direction_transform)[5] * light_direction[1] +
                                 (*owner_direction_transform)[8] * light_direction[2];
    }

    directional_color_basis[0] += transformed_direction[0] * diffuse_rgb[0];
    directional_color_basis[1] += transformed_direction[1] * diffuse_rgb[0];
    directional_color_basis[2] += transformed_direction[2] * diffuse_rgb[0];
    directional_color_basis[3] += transformed_direction[0] * diffuse_rgb[1];
    directional_color_basis[4] += transformed_direction[1] * diffuse_rgb[1];
    directional_color_basis[5] += transformed_direction[2] * diffuse_rgb[1];
    directional_color_basis[6] += transformed_direction[0] * diffuse_rgb[2];
    directional_color_basis[7] += transformed_direction[1] * diffuse_rgb[2];
    directional_color_basis[8] += transformed_direction[2] * diffuse_rgb[2];

    constexpr float kDirectionalLuminanceR = 0.212671f;
    constexpr float kDirectionalLuminanceG = 0.71516001f;
    constexpr float kDirectionalLuminanceB = 0.072168998f;
    const float luminance = diffuse_rgb[0] * kDirectionalLuminanceR +
                            diffuse_rgb[1] * kDirectionalLuminanceG +
                            diffuse_rgb[2] * kDirectionalLuminanceB;
    weighted_light_vector[0] += transformed_direction[0] * luminance;
    weighted_light_vector[1] += transformed_direction[1] * luminance;
    weighted_light_vector[2] += transformed_direction[2] * luminance;

    accumulated_diffuse_rgb[0] += diffuse_rgb[0];
    accumulated_diffuse_rgb[1] += diffuse_rgb[1];
    accumulated_diffuse_rgb[2] += diffuse_rgb[2];
    normalized_light_direction = transformed_direction;
    projected_diffuse_rgb = diffuse_rgb;
  }

  void SetFogParams(const std::array<float, 3> &fog_rgb, const float start, const float end,
                    const float blend_scale) noexcept {
    fog_start = start;
    fog_end = end;
    fog_inverse_range = 1.0f / (end - start);
    fog_blend_scale = blend_scale;
    fog_color_rgb = fog_rgb;
  }

  void SetModelFogParams(const std::array<float, 3> &fog_rgb, const float start,
                         const float end) noexcept {
    SetFogParams(fog_rgb, start, end, 1.0f);
  }

  void AccumulateModelLightRecord(
      const std::uint32_t light_handle, const ModelLightRecord &light,
      const std::array<float, 9> *owner_direction_transform = nullptr) noexcept {
    if (light.enabled == 0) {
      return;
    }

    if (light.light_kind == ModelLightRecord::kRetailPointKind) {
      InsertNearbyPointLight(light_handle, light.point_position);
      return;
    }

    AddAmbientRgb(light.ambient_rgb);
    AddDirectionalRgb(light.diffuse_rgb, light.direction, owner_direction_transform);
    accumulated_specular_rgb[0] += light.specular_rgb[0];
    accumulated_specular_rgb[1] += light.specular_rgb[1];
    accumulated_specular_rgb[2] += light.specular_rgb[2];
  }

  void FinalizeProjectedDirectionalState() noexcept {
    if ((flags & kProjectedDirectionalFinalizedFlag) != 0) {
      return;
    }

    normalized_light_direction = weighted_light_vector;
    const float direction_length_squared =
        normalized_light_direction[0] * normalized_light_direction[0] +
        normalized_light_direction[1] * normalized_light_direction[1] +
        normalized_light_direction[2] * normalized_light_direction[2];
    if (direction_length_squared <= kProjectedDirectionalFallbackLengthSquared) {
      normalized_light_direction = {0.0f, 0.0f, -1.0f};
    } else {
      openwow::math::vec3::NormalizeInPlaceIfLengthSquaredExceedsClientEpsilon(
          normalized_light_direction.data());
    }

    const double projected_row0 = directional_color_basis[0] * normalized_light_direction[0] +
                                  directional_color_basis[1] * normalized_light_direction[1] +
                                  directional_color_basis[2] * normalized_light_direction[2];
    const double projected_row1 = directional_color_basis[3] * normalized_light_direction[0] +
                                  directional_color_basis[4] * normalized_light_direction[1] +
                                  directional_color_basis[5] * normalized_light_direction[2];
    const double projected_row2 = directional_color_basis[6] * normalized_light_direction[0] +
                                  directional_color_basis[7] * normalized_light_direction[1] +
                                  directional_color_basis[8] * normalized_light_direction[2];

    projected_diffuse_rgb[0] =
        static_cast<float>(projected_row0 * 1.25 - accumulated_diffuse_rgb[0] * 0.25);
    projected_diffuse_rgb[1] =
        static_cast<float>(projected_row1 * 1.25 - accumulated_diffuse_rgb[1] * 0.25);
    projected_diffuse_rgb[2] =
        static_cast<float>(projected_row2 * 1.25 - accumulated_diffuse_rgb[2] * 0.25);

    accumulated_ambient_rgb[0] +=
        static_cast<float>((accumulated_diffuse_rgb[0] - projected_row0) * 0.25);
    accumulated_ambient_rgb[1] +=
        static_cast<float>((accumulated_diffuse_rgb[1] - projected_row1) * 0.25);
    accumulated_ambient_rgb[2] +=
        static_cast<float>((accumulated_diffuse_rgb[2] - projected_row2) * 0.25);
    flags |= kProjectedDirectionalFinalizedFlag;
  }

  [[nodiscard]] ProjectedDirectionalLightState GetProjectedDirectionalState() noexcept {
    FinalizeProjectedDirectionalState();
    return {
        .normalized_light_direction = normalized_light_direction,
        .ambient_rgb = accumulated_ambient_rgb,
        .projected_diffuse_rgb = projected_diffuse_rgb,
        .specular_rgb = accumulated_specular_rgb,
    };
  }

  struct NearbyLightSlotData {
    std::array<float, 3> world_position{};
    std::array<float, 3> diffuse_rgb{};
    std::array<float, 3> scene_light_scalars{};
  };

  template <typename ResolveLightHandle>
  [[nodiscard]] bool GetNearbyLightSlotData(
      const std::uint32_t slot_index, NearbyLightSlotData &out,
      ResolveLightHandle &&resolve_light_handle) const noexcept {
    if (slot_index >= nearby_light_count) {
      return false;
    }

    const ModelLightRecord *const light = resolve_light_handle(nearby_light_handles[slot_index]);
    if (light == nullptr) {
      return false;
    }

    out.world_position = light->point_position;
    out.diffuse_rgb = light->diffuse_rgb;
    out.scene_light_scalars = light->scene_light_scalars;
    return true;
  }

  template <typename ResolveLightHandle>
  void FinalizeOwnerTransformState(const float *owner_matrix4x4,
                                   ResolveLightHandle &&resolve_light_handle) noexcept {
    if ((flags & kOwnerTransformFinalizedFlag) != 0) {
      return;
    }

    if (owner_handle != 0 && owner_matrix4x4 != nullptr) {
      auto &&resolve = resolve_light_handle;
      for (std::uint32_t index = 0; index < nearby_light_count; ++index) {
        ModelLightRecord *const light = resolve(nearby_light_handles[index]);
        assert(light != nullptr);
        if (light == nullptr) {
          continue;
        }

        openwow::math::row_major_mat4x4::TransformPointByRowMajorAffine4x4(
            light->transformed_point_position.data(), light->point_position.data(),
            owner_matrix4x4);
      }

      if ((flags & (kRetailInitFlag0x20 | kProjectedShadowPlanePresentFlag)) ==
          (kRetailInitFlag0x20 | kProjectedShadowPlanePresentFlag)) {
        std::array<float, 3> point_on_plane{
            -projected_shadow_plane_distance * projected_shadow_plane_normal[0],
            -projected_shadow_plane_distance * projected_shadow_plane_normal[1],
            -projected_shadow_plane_distance * projected_shadow_plane_normal[2],
        };
        std::array<float, 3> transformed_plane_normal{};
        std::array<float, 3> transformed_point_on_plane{};

        transformed_plane_normal[0] = owner_matrix4x4[0] * projected_shadow_plane_normal[0] +
                                      owner_matrix4x4[4] * projected_shadow_plane_normal[1] +
                                      owner_matrix4x4[8] * projected_shadow_plane_normal[2];
        transformed_plane_normal[1] = owner_matrix4x4[1] * projected_shadow_plane_normal[0] +
                                      owner_matrix4x4[5] * projected_shadow_plane_normal[1] +
                                      owner_matrix4x4[9] * projected_shadow_plane_normal[2];
        transformed_plane_normal[2] = owner_matrix4x4[2] * projected_shadow_plane_normal[0] +
                                      owner_matrix4x4[6] * projected_shadow_plane_normal[1] +
                                      owner_matrix4x4[10] * projected_shadow_plane_normal[2];
        projected_shadow_plane_normal = transformed_plane_normal;

        openwow::math::vec3::NormalizeInPlaceIfLengthSquaredExceedsClientEpsilon(
            projected_shadow_plane_normal.data());
        openwow::math::row_major_mat4x4::TransformPointByRowMajorAffine4x4(
            transformed_point_on_plane.data(), point_on_plane.data(), owner_matrix4x4);
        projected_shadow_plane_distance = static_cast<float>(-(
            static_cast<double>(transformed_point_on_plane[0]) * projected_shadow_plane_normal[0] +
            static_cast<double>(transformed_point_on_plane[1]) * projected_shadow_plane_normal[1] +
            static_cast<double>(transformed_point_on_plane[2]) * projected_shadow_plane_normal[2]));
      }
    }

    flags |= kOwnerTransformFinalizedFlag;
  }

private:
  void InsertNearbyPointLight(const std::uint32_t light_handle,
                              const std::array<float, 3> &point_position) noexcept {
    const float dx = point_position[0] - bounds_sphere[0];
    const float dy = point_position[1] - bounds_sphere[1];
    const float dz = point_position[2] - bounds_sphere[2];
    const float distance_squared = dx * dx + dy * dy + dz * dz;

    std::uint32_t insert_index = nearby_light_count;
    if (insert_index >= kMaxNearbyPointLights) {
      if (distance_squared >= nearby_light_distance_squared[kMaxNearbyPointLights - 1]) {
        return;
      }
      insert_index = static_cast<std::uint32_t>(kMaxNearbyPointLights - 1);
    }

    while (insert_index != 0 &&
           distance_squared <= nearby_light_distance_squared[insert_index - 1]) {
      nearby_light_handles[insert_index] = nearby_light_handles[insert_index - 1];
      nearby_light_distance_squared[insert_index] = nearby_light_distance_squared[insert_index - 1];
      --insert_index;
    }

    nearby_light_handles[insert_index] = light_handle;
    nearby_light_distance_squared[insert_index] = distance_squared;
    if (nearby_light_count < kMaxNearbyPointLights) {
      ++nearby_light_count;
    }
  }
};

}

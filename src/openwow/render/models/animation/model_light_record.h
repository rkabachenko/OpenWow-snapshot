#pragma once

#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cmath>

namespace openwow::render {

struct ModelLightOwnerListState {
  static constexpr std::uint32_t kSkipNonPointLightListFlag = 0x1u;

  std::uint32_t flags = 0;
  std::uint32_t non_point_light_head_slot_handle = 0;
  std::uint32_t point_light_bucket_table_handle = 0;
};

struct ModelLightRecord {
  static constexpr std::uint32_t kRetailPointKind = 1u;
  static constexpr std::uint32_t kRetailPointBucketTableBytes = 0x4000u;
  static constexpr std::uint32_t kRetailPointBucketAxisMask = 0x3Fu;
  static constexpr std::uint32_t kRetailPointBucketRowShift = 6u;
  static constexpr std::uint32_t kRetailPointBucketWordCount = 64u * 64u;
  static constexpr float kRetailPointBucketScalar =
      std::bit_cast<float>(std::uint32_t{0x3D4CCCCDu});
  static constexpr float kRetailSceneLightScalar0 = 0.0f;
  static constexpr float kRetailSceneLightScalar1 = 0.69999999f;
  static constexpr float kRetailSceneLightScalar2 = 0.029999999f;
  static constexpr float kRetailDirectionNormalizeLengthSqEpsilon = 2.3841858e-07f;

  std::uint32_t owner_handle = 0;
  std::uint32_t reserved_0x04 = 0;
  std::uint32_t light_kind = 0;
  std::array<float, 3> point_position{};
  std::array<float, 3> transformed_point_position{};
  std::array<float, 3> direction{};
  std::array<float, 3> ambient_rgb{};
  std::array<float, 3> diffuse_rgb{};
  std::array<float, 3> specular_rgb{};
  std::array<float, 3> scene_light_scalars{};
  std::uint32_t enabled = 0;
  std::uint32_t previous_link_slot_handle = 0;
  std::uint32_t next_light_handle = 0;

  void Initialize() noexcept {
    *this = {};
    light_kind = kRetailPointKind;
    scene_light_scalars[0] = kRetailSceneLightScalar0;
    scene_light_scalars[1] = kRetailSceneLightScalar1;
    scene_light_scalars[2] = kRetailSceneLightScalar2;
  }

  void CopyAndNormalizeDirection(const std::array<float, 3>& source_direction) noexcept {
    direction = source_direction;
    const float length_sq = direction[0] * direction[0] +
                            direction[1] * direction[1] +
                            direction[2] * direction[2];
    if (length_sq <= kRetailDirectionNormalizeLengthSqEpsilon) {
      return;
    }

    const float inv_length = 1.0f / std::sqrt(length_sq);
    direction[0] *= inv_length;
    direction[1] *= inv_length;
    direction[2] *= inv_length;
  }

  template <typename ResolveWordAddress>
  std::uint32_t UnlinkOwnerList(ResolveWordAddress&& resolve_word_address) noexcept {
    auto&& resolve = resolve_word_address;

    if (previous_link_slot_handle != 0) {
      std::uint32_t* const previous_link_slot = resolve(previous_link_slot_handle);
      assert(previous_link_slot != nullptr);
      *previous_link_slot = next_light_handle;
    }

    const std::uint32_t detached_next_light_handle = next_light_handle;
    if (detached_next_light_handle != 0) {
      std::uint32_t* const next_light_words = resolve(detached_next_light_handle);
      assert(next_light_words != nullptr);
      next_light_words[kPreviousLinkSlotWordIndex] = previous_link_slot_handle;
    }

    next_light_handle = 0;
    previous_link_slot_handle = 0;
    return detached_next_light_handle;
  }

  template <typename EnsurePointBucketTable, typename ResolveWordAddress>
  bool AttachOwnerList(std::uint32_t self_handle,
                       ModelLightOwnerListState& owner_list_state,
                       EnsurePointBucketTable&& ensure_point_bucket_table,
                       ResolveWordAddress&& resolve_word_address) noexcept {
    if (enabled == 0 || owner_handle == 0) {
      return false;
    }

    auto&& resolve = resolve_word_address;
    auto&& ensure_bucket_table = ensure_point_bucket_table;

    if (light_kind == kRetailPointKind) {
      if (owner_list_state.point_light_bucket_table_handle == 0) {
        owner_list_state.point_light_bucket_table_handle = ensure_bucket_table();
      }

      assert(owner_list_state.point_light_bucket_table_handle != 0);
      if (owner_list_state.point_light_bucket_table_handle == 0) {
        return false;
      }

      const std::uint32_t bucket_slot_handle =
          owner_list_state.point_light_bucket_table_handle +
          ComputePointBucketIndex(point_position[0], point_position[1]) *
              sizeof(std::uint32_t);
      AttachToSlot(self_handle, bucket_slot_handle, resolve);
      return true;
    }

    if ((owner_list_state.flags & ModelLightOwnerListState::kSkipNonPointLightListFlag) != 0) {
      return false;
    }

    assert(owner_list_state.non_point_light_head_slot_handle != 0);
    if (owner_list_state.non_point_light_head_slot_handle == 0) {
      return false;
    }

    AttachToSlot(self_handle, owner_list_state.non_point_light_head_slot_handle, resolve);
    return true;
  }

  template <typename EnsurePointBucketTable, typename ResolveWordAddress>
  void SetKind(std::uint32_t self_handle,
               const std::uint32_t kind,
               ModelLightOwnerListState& owner_list_state,
               EnsurePointBucketTable&& ensure_point_bucket_table,
               ResolveWordAddress&& resolve_word_address) noexcept {
    if (light_kind == kind) {
      return;
    }

    light_kind = kind;
    if (enabled == 0 || owner_handle == 0) {
      return;
    }

    auto&& resolve = resolve_word_address;
    UnlinkOwnerList(resolve);
    AttachOwnerList(
        self_handle, owner_list_state, ensure_point_bucket_table, resolve);
  }

  template <typename EnsurePointBucketTable, typename ResolveWordAddress>
  void SetPointPosition(
      std::uint32_t self_handle,
      const std::array<float, 3>& position,
      ModelLightOwnerListState& owner_list_state,
      EnsurePointBucketTable&& ensure_point_bucket_table,
      ResolveWordAddress&& resolve_word_address) noexcept {
    point_position = position;
    if (enabled == 0 || owner_handle == 0 || light_kind != kRetailPointKind) {
      return;
    }

    auto&& resolve = resolve_word_address;
    UnlinkOwnerList(resolve);
    AttachOwnerList(
        self_handle, owner_list_state, ensure_point_bucket_table, resolve);
  }

  template <typename EnsurePointBucketTable, typename ResolveWordAddress>
  void SetEnabled(std::uint32_t self_handle,
                  const std::uint32_t enabled_value,
                  ModelLightOwnerListState& owner_list_state,
                  EnsurePointBucketTable&& ensure_point_bucket_table,
                  ResolveWordAddress&& resolve_word_address) noexcept {
    if ((enabled == 0u) == (enabled_value == 0u)) {
      return;
    }

    enabled = enabled_value;
    if (owner_handle == 0) {
      return;
    }

    auto&& resolve = resolve_word_address;
    if (enabled_value != 0) {
      AttachOwnerList(
          self_handle, owner_list_state, ensure_point_bucket_table, resolve);
      return;
    }

    UnlinkOwnerList(resolve);
  }

 private:
  static constexpr std::size_t kPreviousLinkSlotWordIndex = 25u;
  static std::uint32_t NextLightHandleOffset() noexcept {
    return static_cast<std::uint32_t>(offsetof(ModelLightRecord, next_light_handle));
  }

  static std::uint32_t ComputePointBucketIndex(float x, float y) noexcept {
    const float scaled_x = x * kRetailPointBucketScalar;
    const float scaled_y = y * kRetailPointBucketScalar;
    const auto bucket_column = static_cast<std::int32_t>(std::floor(scaled_x)) &
                               static_cast<std::int32_t>(kRetailPointBucketAxisMask);
    const auto bucket_row = static_cast<std::int32_t>(std::floor(scaled_y)) &
                            static_cast<std::int32_t>(kRetailPointBucketAxisMask);
    return (static_cast<std::uint32_t>(bucket_row) << kRetailPointBucketRowShift) +
           static_cast<std::uint32_t>(bucket_column);
  }

  template <typename ResolveWordAddress>
  void AttachToSlot(std::uint32_t self_handle,
                    std::uint32_t slot_handle,
                    ResolveWordAddress&& resolve_word_address) noexcept {
    assert(self_handle != 0);
    if (self_handle == 0) {
      return;
    }

    auto&& resolve = resolve_word_address;
    std::uint32_t* const slot_word = resolve(slot_handle);
    assert(slot_word != nullptr);
    if (slot_word == nullptr) {
      return;
    }

    previous_link_slot_handle = slot_handle;
    next_light_handle = *slot_word;
    *slot_word = self_handle;

    if (next_light_handle != 0) {
      std::uint32_t* const next_light_words = resolve(next_light_handle);
      assert(next_light_words != nullptr);
      if (next_light_words != nullptr) {
        next_light_words[kPreviousLinkSlotWordIndex] =
            self_handle + NextLightHandleOffset();
      }
    }
  }
};

static_assert(sizeof(ModelLightRecord) == 0x6C);
static_assert(offsetof(ModelLightRecord, owner_handle) == 0x00);
static_assert(offsetof(ModelLightRecord, reserved_0x04) == 0x04);
static_assert(offsetof(ModelLightRecord, light_kind) == 0x08);
static_assert(offsetof(ModelLightRecord, point_position) == 0x0C);
static_assert(offsetof(ModelLightRecord, transformed_point_position) == 0x18);
static_assert(offsetof(ModelLightRecord, direction) == 0x24);
static_assert(offsetof(ModelLightRecord, ambient_rgb) == 0x30);
static_assert(offsetof(ModelLightRecord, diffuse_rgb) == 0x3C);
static_assert(offsetof(ModelLightRecord, specular_rgb) == 0x48);
static_assert(offsetof(ModelLightRecord, scene_light_scalars) == 0x54);
static_assert(offsetof(ModelLightRecord, enabled) == 0x60);
static_assert(offsetof(ModelLightRecord, previous_link_slot_handle) == 0x64);
static_assert(offsetof(ModelLightRecord, next_light_handle) == 0x68);

}

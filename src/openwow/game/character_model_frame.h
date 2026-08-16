
#pragma once

#include "openwow/render/models/animation/m2_animation_bounds.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace openwow::game {

class CGObject_C;
class ObjectManager;

struct CharacterModelTextureResourceState {

  std::function<bool(bool wait_for_load)> resolve;
};

struct CharacterModelSceneResourceState {
  static constexpr std::uint32_t kDataReady = 0x1u;
  static constexpr std::uint32_t kTexturesReady = 0x2u;
  static constexpr std::uint32_t kAttachedScenesReady = 0x200u;

  std::uint32_t readiness_flags = 0;
  std::vector<CharacterModelTextureResourceState *> textures;
  std::vector<CharacterModelSceneResourceState *> attached_scenes;
};

struct CharacterModelResolvedUnitVisualState {

  bool suppress_visual_refresh = false;

  std::function<bool(bool immediate)> refresh_character_model;

  CharacterModelSceneResourceState *scene = nullptr;
};

struct CharacterModelFrameUpdateState {

  CharacterModelSceneResourceState *scene = nullptr;

  std::function<CharacterModelResolvedUnitVisualState *()> resolve_bound_unit;
};

[[nodiscard]] std::array<float, 3> CharacterModelBase_ComputeCameraTargetFromAnimBounds(
    const openwow::render::M2AnimationSpatialBounds& bounds) noexcept;
void CharacterModelBase_UpdateCameraTargetFromAnimBounds(
    std::uintptr_t cameraHandle,
    const openwow::render::M2AnimationSpatialBounds& bounds);

struct M2BoundingSphere {
    float center[3] = {};
    float radius = 0.0f;
};

static constexpr float kDefaultPortraitFov       = 0.5f;
static constexpr float kDefaultPortraitAngleX    = 5.5555558f;
static constexpr float kDefaultPortraitAngleY    = 0.0f;
static constexpr float kDefaultPortraitAngleZ    = 2.4166667f;
static constexpr float kDefaultPortraitClipSlot3 = 0.027777778f;
static constexpr std::uint32_t kCameraSlotFov    = 4u;
static constexpr std::uint32_t kCameraSlotTarget = 8u;
static constexpr std::uint32_t kCameraSlotAngles = 7u;
static constexpr std::uint32_t kCameraSlotClip   = 3u;

[[nodiscard]] std::uintptr_t CharacterModelBase_CreateDefaultPortraitCamera(
    const M2BoundingSphere& bounding_sphere);

[[nodiscard]] std::uintptr_t CharacterModelBase_SetupDefaultCamera(
    std::uintptr_t existing_camera,
    const M2BoundingSphere& bounding_sphere);

[[nodiscard]] bool CharacterModelBase_EnsureSceneResourcesReady(
    CharacterModelSceneResourceState &scene, bool wait_for_load,
    bool recurse_attached_scenes);

[[nodiscard]] bool CharacterModelBase_RefreshVisuals(
    CharacterModelResolvedUnitVisualState &unit);

[[nodiscard]] bool CharacterModelBase_Update(
    CharacterModelFrameUpdateState &frame);

static constexpr std::uint32_t kUnitSharedModelOffset = 0xB4;

[[nodiscard]] void* CharacterModelBase_CreateModelFromSourceShared(
    void* source_shared_data,
    const std::function<void*(void* shared)>& create_model_from_shared);

struct CharacterModelBaseGuidUpdateInput {

  bool has_backing_model = false;

  std::uint64_t bound_guid = 0;

  std::uint32_t pet_creature_display_id = 0;
};

void CharacterModelBase_ResolveAndLoadFromGuid(
    ObjectManager& objects,
    const CharacterModelBaseGuidUpdateInput& input,
    const std::function<void(CGObject_C*)>& load_unit_model,
    const std::function<void(std::uint32_t)>& load_creature_template,
    const std::function<void()>& show_visible);

}

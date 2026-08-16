
#include "openwow/game/character_model_frame.h"
#include "openwow/game/ccamera.h"
#include "openwow/game/object_manager.h"

namespace openwow::game {

namespace {

constexpr float kCharacterModelCameraTargetHeightScale = 0.33000001f;
constexpr float kCharacterModelCameraTargetDepthScale = 0.5f;
constexpr std::uint32_t kCharacterModelCameraTargetSlot = 8u;

}

std::array<float, 3> CharacterModelBase_ComputeCameraTargetFromAnimBounds(
    const openwow::render::M2AnimationSpatialBounds& bounds) noexcept {
    return {
        0.0f,
        (bounds.bounds_max[1] - bounds.bounds_min[1]) *
            kCharacterModelCameraTargetHeightScale,
        (bounds.bounds_max[2] - bounds.bounds_min[2]) *
            kCharacterModelCameraTargetDepthScale,
    };
}

void CharacterModelBase_UpdateCameraTargetFromAnimBounds(
    const std::uintptr_t cameraHandle,
    const openwow::render::M2AnimationSpatialBounds& bounds) {
    if (cameraHandle == 0) {
        return;
    }

    const auto camera_target =
        CharacterModelBase_ComputeCameraTargetFromAnimBounds(bounds);
    M2CameraAccessorSetVec3Masked(cameraHandle, kCharacterModelCameraTargetSlot,
                                    camera_target.data(), 0);
}

std::uintptr_t CharacterModelBase_CreateDefaultPortraitCamera(
    const M2BoundingSphere& bounding_sphere) {
    const std::uintptr_t camera = CCamera_Create();
    if (camera == 0) {
        return 0;
    }

    M2CameraAccessorSetFloat(camera, kCameraSlotFov, kDefaultPortraitFov);

    M2CameraAccessorSetVec3Masked(camera, kCameraSlotTarget,
                                    bounding_sphere.center, 0);

    const float angles[3] = {kDefaultPortraitAngleX,
                             kDefaultPortraitAngleY,
                             kDefaultPortraitAngleZ};
    M2CameraAccessorSetVec3Masked(camera, kCameraSlotAngles, angles, 0);

    M2CameraAccessorSetFloat(camera, kCameraSlotClip, kDefaultPortraitClipSlot3);

    return camera;
}

std::uintptr_t CharacterModelBase_SetupDefaultCamera(
    const std::uintptr_t existing_camera,
    const M2BoundingSphere& bounding_sphere) {
    if (existing_camera != 0) {
        return CCamera_Clone(existing_camera);
    }
    return CharacterModelBase_CreateDefaultPortraitCamera(bounding_sphere);
}

bool CharacterModelBase_EnsureSceneResourcesReady(
    CharacterModelSceneResourceState &scene, const bool wait_for_load,
    const bool recurse_attached_scenes) {
    if ((scene.readiness_flags & CharacterModelSceneResourceState::kDataReady) ==
        0u) {
        return false;
    }

    if ((scene.readiness_flags &
         CharacterModelSceneResourceState::kTexturesReady) == 0u) {
        for (auto *texture : scene.textures) {
            if (texture == nullptr) {
                continue;
            }

            if (!texture->resolve || !texture->resolve(wait_for_load)) {
                return false;
            }
        }

        scene.readiness_flags |= CharacterModelSceneResourceState::kTexturesReady;
    }

    if (recurse_attached_scenes &&
        (scene.readiness_flags &
         CharacterModelSceneResourceState::kAttachedScenesReady) == 0u) {
        for (auto *attached_scene : scene.attached_scenes) {
            if (attached_scene == nullptr) {
                continue;
            }

            if (!CharacterModelBase_EnsureSceneResourcesReady(
                    *attached_scene, wait_for_load, true)) {
                return false;
            }
        }

        scene.readiness_flags |=
            CharacterModelSceneResourceState::kAttachedScenesReady;
    }

    return true;
}

bool CharacterModelBase_RefreshVisuals(
    CharacterModelResolvedUnitVisualState &unit) {
    if (unit.suppress_visual_refresh) {
        return false;
    }

    if (unit.refresh_character_model &&
        !unit.refresh_character_model(false)) {
        return false;
    }

    if (unit.scene == nullptr) {
        return false;
    }

    return CharacterModelBase_EnsureSceneResourcesReady(*unit.scene, false, true);
}

bool CharacterModelBase_Update(CharacterModelFrameUpdateState &frame) {
    if (frame.scene == nullptr ||
        !CharacterModelBase_EnsureSceneResourcesReady(*frame.scene, false, true)) {
        return false;
    }

    if (!frame.resolve_bound_unit) {
        return true;
    }

    CharacterModelResolvedUnitVisualState *unit = frame.resolve_bound_unit();
    if (unit == nullptr) {
        return true;
    }

    return CharacterModelBase_RefreshVisuals(*unit);
}

void* CharacterModelBase_CreateModelFromSourceShared(
    void* source_shared_data,
    const std::function<void*(void* shared)>& create_model_from_shared) {
    if (source_shared_data == nullptr) {
        return nullptr;
    }
    if (!create_model_from_shared) {
        return nullptr;
    }
    return create_model_from_shared(source_shared_data);
}

void CharacterModelBase_ResolveAndLoadFromGuid(
    ObjectManager& objects,
    const CharacterModelBaseGuidUpdateInput& input,
    const std::function<void(CGObject_C*)>& load_unit_model,
    const std::function<void(std::uint32_t)>& load_creature_template,
    const std::function<void()>& show_visible) {

    if (input.has_backing_model) {
        if (show_visible) {
            show_visible();
        }
        return;
    }

    if (input.bound_guid != 0) {

        auto* unit = CGObject_HasFlags(objects, input.bound_guid, 8);
        if (unit != nullptr && load_unit_model) {
            load_unit_model(unit);
            if (show_visible) {
                show_visible();
            }
            return;
        }
    } else if (input.pet_creature_display_id != 0 && load_creature_template) {

        load_creature_template(input.pet_creature_display_id);
    }

    if (show_visible) {
        show_visible();
    }
}

}

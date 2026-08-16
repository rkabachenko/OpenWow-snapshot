#pragma once

struct lua_State;
namespace openwow::world {
class WorldCamera;
}

namespace openwow::ui::game::detail {

float ReadOptionalCameraZoomIncrement(lua_State* state);
void ZoomActiveCamera(openwow::world::WorldCamera& camera, bool zoom_in,
                      float amount);
void SyncCameraMotionSettings(openwow::world::WorldCamera& camera);

void SyncCameraSmoothingSettings(openwow::world::WorldCamera& camera);
void SyncCameraViewPreset(openwow::world::WorldCamera& camera, int view_index);
void SyncCameraViewPresets(openwow::world::WorldCamera& camera);
void SaveCameraViewPreset(openwow::world::WorldCamera& camera, int view_index);
void SetCameraViewPreset(openwow::world::WorldCamera& camera, int view_index);
void ResetCameraViewPreset(openwow::world::WorldCamera& camera, int view_index);
void StepCameraViewPreset(openwow::world::WorldCamera& camera, int direction);
void FlipActiveCameraYawDegrees(openwow::world::WorldCamera& camera,
                                double degrees);

}
